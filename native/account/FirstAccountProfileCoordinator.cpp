// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "FirstAccountProfileCoordinator.h"

#include "ActivityStore.h"
#include "LegacyPersonalStateStorage.h"
#include "ProfilePreferencesStore.h"
#include "ProfileStoreRuntime.h"

#include "AudioPairingStore.h"
#include "CollectionStore.h"
#include "HistoryStore.h"
#include "ProgressStore.h"
#include "SearchHistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
bool equalMap(
    const QVariantMap &left,
    const QVariantMap &right) {
    return left == right;
}

QString adoptionFailure(
    const QString &prefix,
    const QString &detail) {
    if (detail.trimmed().isEmpty())
        return prefix;
    return prefix + QStringLiteral(" ") + detail;
}


bool migratedProfileFilesPresent(
    const ProfilePaths &paths,
    const PersonalStateSnapshot &source,
    const QString &activitySourceDigest) {
    if (!activitySourceDigest.isEmpty()
        && !QFileInfo::exists(
            paths.activityDbPath())) {
        return false;
    }

    const bool needsProgress =
        !source.progressEntries.isEmpty()
        || !source.progressLastSeason.isEmpty()
        || !source.progressWatchedMarks.isEmpty();
    if (needsProgress
        && !QFileInfo::exists(
            paths.progressIniPath())) {
        return false;
    }

    if (!source.collectionEntries.isEmpty()
        && !QFileInfo::exists(
            paths.collectionIniPath())) {
        return false;
    }

    if (!source.searchHistory.isEmpty()
        && !QFileInfo::exists(
            paths.searchHistoryIniPath())) {
        return false;
    }

    if (!source.audioPairings.isEmpty()
        && !QFileInfo::exists(
            paths.audioPairingIniPath())) {
        return false;
    }

    if (!source.historyRecords.isEmpty()
        && !QFileInfo::exists(
            paths.historyIniPath())) {
        return false;
    }

    return QFileInfo::exists(
        paths.preferencesIniPath());
}
}

FirstAccountProfileCoordinator::
FirstAccountProfileCoordinator(
    ProfileStoreRuntime *profileRuntime,
    const QString &appDataRoot)
    : m_profileRuntime(profileRuntime),
      m_appDataRoot(appDataRoot) {
    Q_ASSERT(m_profileRuntime);
}

bool FirstAccountProfileCoordinator::
prepareCreatedAccount(
    const QString &accountId,
    QString *error) {
    const auto paths =
        ProfilePaths::account(
            accountId,
            m_appDataRoot);
    if (!paths.has_value()) {
        return setError(
            error,
            QStringLiteral(
                "The account profile identifier is invalid."));
    }

    if (!writeCreationIntent(
            *paths,
            error)) {
        return false;
    }

    if (QFileInfo::exists(
            paths->adoptionJournalPath())) {
        auto adoption =
            ProfileAdoption::open(
                *paths,
                error);
        if (!adoption.has_value())
            return false;

        clearCreationIntent(
            *paths,
            nullptr);

        return resumeAdoption(
            *paths,
            *adoption,
            error);
    }

    if (QFileInfo::exists(paths->profileRoot())) {
        return setError(
            error,
            QStringLiteral(
                "A profile already exists for the newly created account."));
    }

    return runFreshAdoption(*paths, error);
}

bool FirstAccountProfileCoordinator::
prepareAccountSession(
    const QString &accountId,
    QString *error) {
    const auto paths =
        ProfilePaths::account(
            accountId,
            m_appDataRoot);
    if (!paths.has_value()) {
        return setError(
            error,
            QStringLiteral(
                "The account profile identifier is invalid."));
    }

    const ProfilePaths &active =
        m_profileRuntime->activeProfile();
    if (active.kind()
            == ProfilePaths::Kind::Account) {
        if (active.profileId()
            == paths->profileId()) {
            return true;
        }

        return setError(
            error,
            QStringLiteral(
                "The active account profile must be sealed before another account opens."));
    }

    if (QFileInfo::exists(
            paths->adoptionJournalPath())) {
        auto adoption =
            ProfileAdoption::open(
                *paths,
                error);
        if (!adoption.has_value())
            return false;

        if (adoption->state()
                == ProfileAdoption::State::LegacyQuarantined
            && !m_quarantinedThisProcess.contains(
                paths->profileId())) {
            return verifyRestartAndCommit(
                *paths,
                *adoption,
                error);
        }

        return resumeAdoption(
            *paths,
            *adoption,
            error);
    }

    if (hasCreationIntent(*paths)) {
        if (QFileInfo::exists(
                paths->profileRoot())) {
            return setError(
                error,
                QStringLiteral(
                    "A pending first-account adoption conflicts with an existing profile."));
        }

        return runFreshAdoption(
            *paths,
            error);
    }

    if (!QFileInfo::exists(paths->profileRoot())) {
        if (!createEmptyProfile(
                *paths,
                error)) {
            return false;
        }
    }

    return activate(*paths, error);
}

bool FirstAccountProfileCoordinator::
prepareRememberedAccount(
    const QString &accountId,
    QString *error) {
    const auto paths =
        ProfilePaths::account(
            accountId,
            m_appDataRoot);
    if (!paths.has_value()) {
        return setError(
            error,
            QStringLiteral(
                "The remembered account profile identifier is invalid."));
    }

    const ProfilePaths &active =
        m_profileRuntime->activeProfile();
    if (active.kind()
            == ProfilePaths::Kind::Account) {
        if (active.profileId()
            == paths->profileId()) {
            return true;
        }

        return setError(
            error,
            QStringLiteral(
                "The active account profile must be sealed before another account opens."));
    }

    if (QFileInfo::exists(
            paths->adoptionJournalPath())
        || hasCreationIntent(*paths)) {
        return prepareAccountSession(
            accountId,
            error);
    }

    if (!QFileInfo::exists(
            paths->profileRoot())) {
        return setError(
            error,
            QStringLiteral(
                "The remembered account profile is missing from this device."));
    }

    return activate(
        *paths,
        error);
}

bool FirstAccountProfileCoordinator::
prepareLocalOnly(
    QString *error) {
    const auto claimed =
        legacyPersonalStateClaimed(
            error);
    if (!claimed.has_value())
        return false;

    const ProfilePaths &active =
        m_profileRuntime->activeProfile();
    if (active.kind()
        == ProfilePaths::Kind::LocalOnly) {
        return true;
    }

    m_profileRuntime
        ->suspendPersonalStoresForMigration();

    if (*claimed) {
        return m_profileRuntime
            ->activateLocalOnlyProfile(
                error);
    }

    return m_profileRuntime
        ->reloadLegacyProfile(
            error);
}

bool FirstAccountProfileCoordinator::
runFreshAdoption(
    const ProfilePaths &paths,
    QString *error) {
    m_profileRuntime->flushPersonalStores();

    QString captureError;
    const auto source =
        m_profileRuntime->legacyStorage()
            .capture(&captureError);
    if (!source.has_value()) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not capture local personal state."),
                captureError));
    }

    // Captured alongside personal state, before the journal exists — a
    // clean checkpoint+file digest of whatever legacy activity ledger is
    // currently on disk (empty digest is the valid "nothing to migrate"
    // sentinel, e.g. a fresh install predating this feature — §17).
    QString activityCaptureError;
    const QString activitySourceDigest =
        captureLegacyActivityDigest(
            &activityCaptureError);

    auto adoption =
        ProfileAdoption::begin(
            paths,
            source->semanticDigest(),
            error);
    if (!adoption.has_value())
        return false;

    clearCreationIntent(
        paths,
        nullptr);

    const auto stagedStorage =
        LegacyPersonalStateStorage::forProfileRoot(
            paths,
            paths.accountStagingRoot(),
            error);
    if (!stagedStorage.has_value())
        return false;

    if (!stagedStorage->restorePersonalState(
            *source,
            error)) {
        return false;
    }

    QString stagedActivityDigest;
    if (!copyActivityLedgerToStaging(
            paths,
            activitySourceDigest,
            &stagedActivityDigest,
            error)) {
        return false;
    }

    QString stagedDigest;
    if (!verifyProfile(
            paths,
            paths.accountStagingRoot(),
            *source,
            &stagedDigest,
            error)) {
        return false;
    }

    if (!adoption->markTargetVerified(
            stagedDigest,
            error)) {
        return false;
    }

    if (!adoption->markActivityTargetVerified(
            activitySourceDigest,
            stagedActivityDigest,
            error)) {
        return false;
    }

    if (!adoption->promote(error))
        return false;

    return finishPromotedAdoption(
        paths,
        *adoption,
        *source,
        activitySourceDigest,
        error);
}

bool FirstAccountProfileCoordinator::
resumeAdoption(
    const ProfilePaths &paths,
    ProfileAdoption adoption,
    QString *error) {
    switch (adoption.state()) {
    case ProfileAdoption::State::Preparing:
    case ProfileAdoption::State::TargetVerified: {
        QString captureError;
        const auto source =
            m_profileRuntime->legacyStorage()
                .capture(&captureError);
        if (!source.has_value()) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Could not inspect local state while recovering adoption."),
                    captureError));
        }

        if (!source->matchesSemanticDigest(
                adoption.snapshot()
                    .sourceSemanticDigest)) {
            return setError(
                error,
                QStringLiteral(
                    "Local personal state changed during an interrupted adoption."));
        }

        if (!adoption
                 .rollbackBeforeLegacyQuarantine(
                     error)) {
            return false;
        }

        return runFreshAdoption(
            paths,
            error);
    }

    case ProfileAdoption::State::Promoted: {
        QString captureError;
        const auto source =
            m_profileRuntime->legacyStorage()
                .capture(&captureError);
        if (!source.has_value()) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Could not inspect local state after profile promotion."),
                    captureError));
        }

        const QString sourceDigest =
            adoption.snapshot()
                .sourceSemanticDigest;
        if (!source->matchesSemanticDigest(
                sourceDigest)) {
            return setError(
                error,
                QStringLiteral(
                    "Legacy personal state changed after profile promotion."));
        }

        if (source->semanticDigest()
            != sourceDigest) {
            if (!adoption
                     .rollbackBeforeLegacyQuarantine(
                         error)) {
                return false;
            }

            return runFreshAdoption(
                paths,
                error);
        }

        // The activity digest is trusted from the journal here rather than
        // recomputed: it was captured atomically with `source` in the same
        // synchronous runFreshAdoption() call that produced this journal, so
        // the personal-state identity check just above already vouches for
        // "this is still the same interrupted adoption attempt." Recomputing
        // independently would risk a false rollback if the legacy activity
        // file was already (partially) quarantined by a prior crashed
        // attempt.
        return finishPromotedAdoption(
            paths,
            adoption,
            *source,
            adoption.snapshot().activitySourceDigest,
            error);
    }

    case ProfileAdoption::State::LegacyQuarantined:
        if (m_quarantinedThisProcess.contains(
                paths.profileId())) {
            const auto storage =
                LegacyPersonalStateStorage::forProfile(
                    paths,
                    error);
            if (!storage.has_value())
                return false;

            const auto current =
                storage->capture(error);
            if (!current.has_value())
                return false;

            QString currentDigest;
            if (!verifyProfile(
                    paths,
                    paths.profileRoot(),
                    *current,
                    &currentDigest,
                    error)) {
                return false;
            }

            return activate(paths, error);
        }

        return verifyRestartAndCommit(
            paths,
            adoption,
            error);

    case ProfileAdoption::State::RetryPending: {
        QString captureError;
        const auto source =
            m_profileRuntime->legacyStorage()
                .capture(&captureError);
        if (!source.has_value()) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Could not inspect restored local state before retrying adoption."),
                    captureError));
        }

        if (!source->matchesSemanticDigest(
                adoption.snapshot()
                    .sourceSemanticDigest)) {
            return setError(
                error,
                QStringLiteral(
                    "Restored local personal state changed before adoption retry."));
        }

        if (!adoption
                 .rollbackBeforeLegacyQuarantine(
                     error)) {
            return false;
        }

        return runFreshAdoption(
            paths,
            error);
    }

    case ProfileAdoption::State::Committed:
        return activate(paths, error);
    }

    return setError(
        error,
        QStringLiteral(
            "The profile adoption state is unsupported."));
}

bool FirstAccountProfileCoordinator::
finishPromotedAdoption(
    const ProfilePaths &paths,
    ProfileAdoption adoption,
    const PersonalStateSnapshot &source,
    const QString &activitySourceDigest,
    QString *error) {
    QString targetDigest;
    if (!verifyProfile(
            paths,
            paths.profileRoot(),
            source,
            &targetDigest,
            error)) {
        return false;
    }

    if (targetDigest
        != source.semanticDigest()) {
        return setError(
            error,
            QStringLiteral(
                "The promoted account profile does not match local personal state."));
    }

    QString promotedActivityDigest;
    if (!verifyActivityDigest(
            paths,
            paths.profileRoot(),
            activitySourceDigest,
            &promotedActivityDigest,
            error)) {
        return false;
    }

    if (!writeBackup(
            paths,
            source,
            error)) {
        return false;
    }

    const auto backup =
        readBackup(paths, error);
    if (!backup.has_value())
        return false;

    if (backup->semanticDigest()
        != source.semanticDigest()) {
        return setError(
            error,
            QStringLiteral(
                "The rollback backup does not match local personal state."));
    }

    QString activityBackupDigest;
    if (!backupActivityLedger(
            paths,
            activitySourceDigest,
            &activityBackupDigest,
            error)) {
        return false;
    }

    m_profileRuntime
        ->suspendPersonalStoresForMigration();

    // NOTE: restoreLegacyActivityFromBackup() always runs BEFORE
    // restoreLegacyForRetry() in every failure branch below —
    // restoreLegacyForRetry()'s reloadLegacyProfile() call reopens a live
    // ActivityStore connection at the legacy path, so the file on disk must
    // already be back in its pre-quarantine state before that connection
    // opens (reopening first would create/lock a fresh empty file that a
    // later restore would then have to fight for the handle on).

    if (!quarantineLegacyActivityLedger(error)) {
        restoreLegacyActivityFromBackup(
            paths,
            nullptr);
        restoreLegacyForRetry(
            source,
            nullptr);
        return false;
    }

    if (!m_profileRuntime
             ->legacyStorage()
             .clearPersonalState(error)) {
        restoreLegacyActivityFromBackup(
            paths,
            nullptr);
        restoreLegacyForRetry(
            source,
            nullptr);
        return false;
    }

    const auto cleared =
        m_profileRuntime
            ->legacyStorage()
            .capture(error);
    if (!cleared.has_value()) {
        restoreLegacyActivityFromBackup(
            paths,
            nullptr);
        restoreLegacyForRetry(
            source,
            nullptr);
        return false;
    }

    if (!cleared->isEmpty()) {
        restoreLegacyActivityFromBackup(
            paths,
            nullptr);
        restoreLegacyForRetry(
            source,
            nullptr);
        return setError(
            error,
            QStringLiteral(
                "Legacy personal state was not fully quarantined."));
    }

    // markActivityLegacyQuarantined() runs BEFORE markLegacyQuarantined():
    // both require state==Promoted, and markLegacyQuarantined() is the call
    // that actually advances state to LegacyQuarantined — calling it first
    // would leave no valid state for the activity call to run in.
    if (!adoption.markActivityLegacyQuarantined(
            activityBackupDigest,
            error)) {
        restoreLegacyActivityFromBackup(
            paths,
            nullptr);
        restoreLegacyForRetry(
            source,
            nullptr);
        return false;
    }

    if (!adoption.markLegacyQuarantined(
            backup->semanticDigest(),
            error)) {
        restoreLegacyActivityFromBackup(
            paths,
            nullptr);
        restoreLegacyForRetry(
            source,
            nullptr);
        return false;
    }

    m_quarantinedThisProcess.insert(
        paths.profileId());

    if (!activate(paths, error)) {
        return restoreLegacyAndRollback(
            paths,
            &adoption,
            source,
            error);
    }

    return true;
}

bool FirstAccountProfileCoordinator::
verifyRestartAndCommit(
    const ProfilePaths &paths,
    ProfileAdoption adoption,
    QString *error) {
    const auto backup =
        readBackup(paths, error);
    if (!backup.has_value())
        return false;

    const QString sourceDigest =
        adoption.snapshot()
            .sourceSemanticDigest;
    if (!backup->matchesSemanticDigest(
            sourceDigest)) {
        return setError(
            error,
            QStringLiteral(
                "The rollback backup failed restart verification."));
    }

    const auto legacy =
        m_profileRuntime->legacyStorage()
            .capture(error);
    if (!legacy.has_value())
        return false;

    if (!legacy->isEmpty()) {
        return setError(
            error,
            QStringLiteral(
                "Legacy personal state reappeared before adoption commit."));
    }

    if (!migratedProfileFilesPresent(
            paths,
            *backup,
            adoption.snapshot().activitySourceDigest)) {
        QString restoreError;
        if (!restoreLegacyAndRollback(
                paths,
                &adoption,
                *backup,
                &restoreError)) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "A promoted profile file disappeared and rollback could not complete."),
                    restoreError));
        }

        return setError(
            error,
            QStringLiteral(
                "A promoted profile file disappeared; local personal state was restored."));
    }

    const auto storage =
        LegacyPersonalStateStorage::forProfile(
            paths,
            error);
    if (!storage.has_value()) {
        return false;
    }

    const auto current =
        storage->capture(error);
    if (!current.has_value()) {
        QString restoreError;
        if (!restoreLegacyAndRollback(
                paths,
                &adoption,
                *backup,
                &restoreError)) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Restart verification failed and rollback could not complete."),
                    restoreError));
        }

        return setError(
            error,
            QStringLiteral(
                "Restart verification failed; local personal state was restored."));
    }

    QString currentDigest;
    if (!verifyProfile(
            paths,
            paths.profileRoot(),
            *current,
            &currentDigest,
            error)) {
        QString restoreError;
        if (!restoreLegacyAndRollback(
                paths,
                &adoption,
                *backup,
                &restoreError)) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Restart verification failed and rollback could not complete."),
                    restoreError));
        }

        return setError(
            error,
            QStringLiteral(
                "Restart verification failed; local personal state was restored."));
    }

    QString currentActivityDigest;
    if (!verifyActivityDigest(
            paths,
            paths.profileRoot(),
            adoption.snapshot().activityTargetDigest,
            &currentActivityDigest,
            error)) {
        QString restoreError;
        if (!restoreLegacyAndRollback(
                paths,
                &adoption,
                *backup,
                &restoreError)) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Restart verification failed and rollback could not complete."),
                    restoreError));
        }

        return setError(
            error,
            QStringLiteral(
                "Restart verification failed; local personal state was restored."));
    }

    if (!adoption.commit(error)) {
        QString restoreError;
        if (!restoreLegacyAndRollback(
                paths,
                &adoption,
                *backup,
                &restoreError)) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Adoption commit failed and rollback could not complete."),
                    restoreError));
        }
        return false;
    }

    return activate(paths, error);
}

bool FirstAccountProfileCoordinator::
createEmptyProfile(
    const ProfilePaths &paths,
    QString *error) {
    if (paths.kind()
        != ProfilePaths::Kind::Account) {
        return setError(
            error,
            QStringLiteral(
                "Only account profiles can be created here."));
    }

    if (!QDir().mkpath(
            paths.profileRoot())) {
        return setError(
            error,
            QStringLiteral(
                "Could not create the account profile directory."));
    }

    const auto storage =
        LegacyPersonalStateStorage::forProfile(
            paths,
            error);
    if (!storage.has_value())
        return false;

    PersonalStateSnapshot empty;
    if (!storage->restorePersonalState(
            empty,
            error)) {
        return false;
    }

    QString digest;
    if (!verifyProfile(
            paths,
            paths.profileRoot(),
            empty,
            &digest,
            error)) {
        return false;
    }

    return true;
}

bool FirstAccountProfileCoordinator::
verifyProfile(
    const ProfilePaths &paths,
    const QString &profileRoot,
    const PersonalStateSnapshot &expected,
    QString *semanticDigest,
    QString *error) const {
    const auto storage =
        LegacyPersonalStateStorage::forProfileRoot(
            paths,
            profileRoot,
            error);
    if (!storage.has_value())
        return false;

    const auto rawReadback =
        storage->capture(error);
    if (!rawReadback.has_value())
        return false;

    if (rawReadback->semanticDigest()
        != expected.semanticDigest()) {
        return setError(
            error,
            QStringLiteral(
                "The profile persistence readback does not match the expected semantic state."));
    }

    ProgressStore progress(
        storage->progressIniPath());
    CollectionStore collection(
        storage->collectionIniPath());
    SearchHistoryStore searchHistory(
        storage->searchHistoryIniPath());
    AudioPairingStore audioPairing(
        storage->audioPairingIniPath());
    HistoryStore history(
        storage->historyIniPath());
    ProfilePreferencesStore preferences(
        storage->preferencesIniPath());

    for (auto it =
             expected.progressEntries.constBegin();
         it != expected.progressEntries.constEnd();
         ++it) {
        if (!it.value().isObject()) {
            return setError(
                error,
                QStringLiteral(
                    "A progress record is malformed."));
        }

        const QVariantMap record =
            it.value().toObject()
                .toVariantMap();
        const QString kind =
            record.value(
                QStringLiteral("kind"))
                .toString();
        const QString id =
            record.value(
                QStringLiteral("id"))
                .toString();

        if (kind.isEmpty()
            || id.isEmpty()
            || !equalMap(
                progress.get(kind, id),
                record)) {
            return setError(
                error,
                QStringLiteral(
                    "ProgressStore semantic readback failed."));
        }
    }

    for (auto it =
             expected.progressLastSeason.constBegin();
         it != expected.progressLastSeason.constEnd();
         ++it) {
        if (progress.lastSeason(it.key())
            != it.value().toInt()) {
            return setError(
                error,
                QStringLiteral(
                    "ProgressStore last-season readback failed."));
        }
    }

    for (auto it =
             expected.progressWatchedMarks.constBegin();
         it != expected.progressWatchedMarks.constEnd();
         ++it) {
        if (progress.watchedMark(it.key())
            != it.value().toInt()) {
            return setError(
                error,
                QStringLiteral(
                    "ProgressStore watched-mark readback failed."));
        }
    }

    QHash<QString, QVariantMap> collectionReadback;
    QSet<QString> worlds;
    for (auto it =
             expected.collectionEntries.constBegin();
         it != expected.collectionEntries.constEnd();
         ++it) {
        if (!it.value().isObject()) {
            return setError(
                error,
                QStringLiteral(
                    "A collection record is malformed."));
        }

        const QVariantMap record =
            it.value().toObject()
                .toVariantMap();
        const QString world =
            record.value(
                QStringLiteral("world"))
                .toString();
        if (world.isEmpty()) {
            return setError(
                error,
                QStringLiteral(
                    "A collection record has no world."));
        }
        worlds.insert(world);
    }

    for (const QString &world : worlds) {
        const QVariantList items =
            collection.items(world);
        for (const QVariant &item : items) {
            const QVariantMap record =
                item.toMap();
            const QString id =
                record.value(
                    QStringLiteral("id"))
                    .toString();
            if (!id.isEmpty()) {
                collectionReadback.insert(
                    world
                        + QStringLiteral("\x1f")
                        + id,
                    record);
            }
        }
    }

    if (collectionReadback.size()
        != expected.collectionEntries.size()) {
        return setError(
            error,
            QStringLiteral(
                "CollectionStore semantic readback count failed."));
    }

    for (auto it =
             expected.collectionEntries.constBegin();
         it != expected.collectionEntries.constEnd();
         ++it) {
        const QVariantMap expectedRecord =
            it.value().toObject()
                .toVariantMap();
        const QString world =
            expectedRecord.value(
                QStringLiteral("world"))
                .toString();
        const QString id =
            expectedRecord.value(
                QStringLiteral("id"))
                .toString();
        if (!equalMap(
                collectionReadback.value(
                    world
                        + QStringLiteral("\x1f")
                        + id),
                expectedRecord)) {
            return setError(
                error,
                QStringLiteral(
                    "CollectionStore semantic readback failed."));
        }
    }

    for (auto it =
             expected.searchHistory.constBegin();
         it != expected.searchHistory.constEnd();
         ++it) {
        QStringList expectedEntries;
        const QJsonArray array =
            it.value().toArray();
        for (const QJsonValue &entry : array) {
            if (entry.isString())
                expectedEntries.append(
                    entry.toString());
        }

        if (searchHistory.list(it.key())
            != expectedEntries) {
            return setError(
                error,
                QStringLiteral(
                    "SearchHistoryStore semantic readback failed."));
        }
    }

    const QVariantList pairings =
        audioPairing.allPairings();
    QHash<QString, QVariantMap> pairingReadback;
    for (const QVariant &item : pairings) {
        const QVariantMap record =
            item.toMap();
        const QString bookId =
            record.value(
                QStringLiteral("bookId"))
                .toString();
        if (!bookId.isEmpty())
            pairingReadback.insert(bookId, record);
    }

    if (pairingReadback.size()
        != expected.audioPairings.size()) {
        return setError(
            error,
            QStringLiteral(
                "AudioPairingStore semantic readback count failed."));
    }

    for (auto it =
             expected.audioPairings.constBegin();
         it != expected.audioPairings.constEnd();
         ++it) {
        if (!it.value().isObject()
            || !equalMap(
                pairingReadback.value(it.key()),
                it.value().toObject()
                    .toVariantMap())) {
            return setError(
                error,
                QStringLiteral(
                    "AudioPairingStore semantic readback failed."));
        }
    }

    const QVariantList historyEntries =
        history.syncEntries();
    if (historyEntries.size()
        != expected.historyRecords.size()) {
        return setError(
            error,
            QStringLiteral(
                "HistoryStore semantic readback count failed."));
    }

    for (auto it =
             expected.historyRecords.constBegin();
         it != expected.historyRecords.constEnd();
         ++it) {
        if (!it.value().isObject()) {
            return setError(
                error,
                QStringLiteral(
                    "A History record is malformed."));
        }

        const QVariantMap expectedRecord =
            it.value()
                .toObject()
                .toVariantMap();
        const QString kind =
            expectedRecord
                .value(
                    QStringLiteral("kind"))
                .toString();
        const QString id =
            expectedRecord
                .value(
                    QStringLiteral("id"))
                .toString();

        if (kind.isEmpty()
            || id.isEmpty()
            || !equalMap(
                history.get(kind, id),
                expectedRecord)) {
            return setError(
                error,
                QStringLiteral(
                    "HistoryStore semantic readback failed."));
        }
    }

    if (preferences.showExplicit()
        != expected.showExplicit) {
        return setError(
            error,
            QStringLiteral(
                "ProfilePreferencesStore semantic readback failed."));
    }

    if (semanticDigest)
        *semanticDigest =
            rawReadback->semanticDigest();
    return true;
}

bool FirstAccountProfileCoordinator::
writeBackup(
    const ProfilePaths &paths,
    const PersonalStateSnapshot &snapshot,
    QString *error) const {
    const QString root =
        paths.adoptionBackupRoot();

    if (!QDir().mkpath(root)) {
        return setError(
            error,
            QStringLiteral(
                "Could not create the legacy rollback backup directory."));
    }

    const QByteArray payload =
        QJsonDocument(snapshot.toJson())
            .toJson(QJsonDocument::Compact);

    QSaveFile file(
        backupFilePath(paths));
    if (!file.open(QIODevice::WriteOnly)) {
        return setError(
            error,
            QStringLiteral(
                "Could not open the legacy rollback backup."));
    }

    if (file.write(payload)
        != payload.size()) {
        return setError(
            error,
            QStringLiteral(
                "Could not write the legacy rollback backup."));
    }

    if (!file.commit()) {
        return setError(
            error,
            QStringLiteral(
                "Could not commit the legacy rollback backup."));
    }

    return true;
}

std::optional<PersonalStateSnapshot>
FirstAccountProfileCoordinator::readBackup(
    const ProfilePaths &paths,
    QString *error) const {
    QFile file(
        backupFilePath(paths));
    if (!file.open(QIODevice::ReadOnly)) {
        setError(
            error,
            QStringLiteral(
                "Could not open the legacy rollback backup."));
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            file.readAll(),
            &parseError);

    if (parseError.error
            != QJsonParseError::NoError
        || !document.isObject()) {
        setError(
            error,
            QStringLiteral(
                "The legacy rollback backup is malformed."));
        return std::nullopt;
    }

    return PersonalStateSnapshot::fromJson(
        document.object(),
        error);
}

bool FirstAccountProfileCoordinator::
restoreLegacyForRetry(
    const PersonalStateSnapshot &snapshot,
    QString *error) {
    QString restoreError;
    if (!m_profileRuntime
             ->legacyStorage()
             .restorePersonalState(
                 snapshot,
                 &restoreError)) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not restore legacy personal state."),
                restoreError));
    }

    const auto restored =
        m_profileRuntime
            ->legacyStorage()
            .capture(&restoreError);
    if (!restored.has_value()
        || restored->semanticDigest()
            != snapshot.semanticDigest()) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not verify restored legacy personal state."),
                restoreError));
    }

    if (!m_profileRuntime->reloadLegacyProfile(
            &restoreError)) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Legacy state was restored on disk but could not be reopened."),
                restoreError));
    }

    return true;
}

bool FirstAccountProfileCoordinator::
restoreLegacyAndRollback(
    const ProfilePaths &paths,
    ProfileAdoption *adoption,
    const PersonalStateSnapshot &snapshot,
    QString *error) {
    m_profileRuntime
        ->suspendPersonalStoresForMigration();

    QString restoreError;
    if (!restoreLegacyActivityFromBackup(
            paths,
            &restoreError)) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not restore the legacy activity ledger."),
                restoreError));
    }

    if (!m_profileRuntime
             ->legacyStorage()
             .restorePersonalState(
                 snapshot,
                 &restoreError)) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not restore legacy personal state."),
                restoreError));
    }

    const auto restored =
        m_profileRuntime
            ->legacyStorage()
            .capture(&restoreError);
    if (!restored.has_value()) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not verify restored legacy personal state."),
                restoreError));
    }

    if (restored->semanticDigest()
        != snapshot.semanticDigest()) {
        return setError(
            error,
            QStringLiteral(
                "Restored legacy personal state does not match the rollback backup."));
    }

    const QString sourceDigest =
        adoption->snapshot()
            .sourceSemanticDigest;
    if (!restored->matchesSemanticDigest(
            sourceDigest)) {
        return setError(
            error,
            QStringLiteral(
                "Restored legacy personal state does not match the adoption source digest."));
    }

    if (!adoption->rollbackAfterLegacyRestore(
            sourceDigest,
            &restoreError)) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Legacy state was restored, but adoption cleanup failed."),
                restoreError));
    }

    if (!m_profileRuntime->reloadLegacyProfile(
            &restoreError)) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Legacy state was restored on disk but could not be reopened."),
                restoreError));
    }

    // Reopening legacy mode replays the restored activity ledger into derived
    // History/Progress stores. That deterministic, Colosseum-owned projection
    // can legitimately change the personal-state semantic digest after the
    // exact rollback verification above. Persist the post-replay digest as the
    // RetryPending baseline so a later sign-in still rejects any mutation that
    // happens after this trusted replay.
    const auto retrySource =
        m_profileRuntime->legacyStorage().capture(&restoreError);
    if (!retrySource.has_value()) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Legacy state was reopened but its retry baseline could not be captured."),
                restoreError));
    }
    if (!adoption->refreshRetrySourceSemanticDigest(
            retrySource->semanticDigest(),
            &restoreError)) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Legacy state was reopened but its retry baseline could not be persisted."),
                restoreError));
    }

    return true;
}

bool FirstAccountProfileCoordinator::activate(
    const ProfilePaths &paths,
    QString *error) {
    return m_profileRuntime
        ->activateAccountProfile(
            paths.profileId(),
            error);
}

// --- Activity-ledger adoption (CPP-PORT-CONTRACT §17) -----------------------
//
// The activity.sqlite file rides inside the SAME staging root/final profile
// root every other personal-state file does, so ProfileAdoption::promote()'s
// atomic directory rename already carries it across without any extra code
// here. What these helpers add is: capturing a digest of whatever legacy
// ledger exists (or the empty "nothing to migrate" sentinel), copying its
// bytes into staging before promote(), verifying digests at each checkpoint
// the personal-state path already has one for, and quarantining/restoring
// the legacy file in step with legacy personal state.
//
// Safe-copy reasoning: every call below runs inside one synchronous
// coordinator method with no event-loop turn (no processEvents/await) in
// between capture and copy, so nothing else in this single-threaded Qt
// process can write to the file mid-copy. checkpointForSafeCopy() (WAL
// TRUNCATE) merges recent commits into the single main file first, so a
// plain QFile::copy of just "activity.sqlite" — no "-wal"/"-shm" sidecars —
// is a faithful snapshot. This matches CPP-PORT-CONTRACT §17's suggested
// "SHA-256 of the sqlite file bytes after a clean close + wal checkpoint";
// the store is checkpointed rather than closed mid-flow because it must keep
// serving the still-live legacy session until adoption actually commits.

QString FirstAccountProfileCoordinator::
captureLegacyActivityDigest(
    QString *error) const {
    // Only a LIVE legacy-mode ActivityStore can hold uncommitted WAL pages
    // for THIS exact path — any other active profile kind (sealed, during
    // these unit tests; local/account in normal use) has its own unrelated
    // activity.sqlite open, so there is nothing to checkpoint here and the
    // legacy file on disk (if any) is already whatever a prior clean close
    // left it as.
    if (m_profileRuntime->activeProfile().kind()
            == ProfilePaths::Kind::LegacyLocal
        && m_profileRuntime->activityStore()) {
        // Best-effort: activity is observational (§25) — a checkpoint
        // failure does not block account creation, it just means the digest
        // below is taken from whatever is already flushed to disk.
        m_profileRuntime->activityStore()
            ->checkpointForSafeCopy(error);
    }

    return ActivityStore::fileDigestSha256(
        m_profileRuntime->legacyStorage()
            .activityDbPath());
}

bool FirstAccountProfileCoordinator::
copyActivityLedgerToStaging(
    const ProfilePaths &paths,
    const QString &sourceDigest,
    QString *stagedDigest,
    QString *error) const {
    if (stagedDigest)
        stagedDigest->clear();

    if (sourceDigest.isEmpty()) {
        // No legacy activity ledger to migrate — a fresh installation
        // predating this feature, or one that never recorded activity while
        // legacy-local. Valid, not a failure (§17).
        return true;
    }

    const auto staged =
        LegacyPersonalStateStorage::forProfileRoot(
            paths,
            paths.accountStagingRoot(),
            error);
    if (!staged.has_value())
        return false;

    const QString destination =
        staged->activityDbPath();
    const QFileInfo destinationInfo(destination);
    if (!QDir().mkpath(
            destinationInfo.absolutePath())) {
        return setError(
            error,
            QStringLiteral(
                "Could not create the staged activity ledger directory."));
    }

    if (!QFile::copy(
            m_profileRuntime->legacyStorage()
                .activityDbPath(),
            destination)) {
        return setError(
            error,
            QStringLiteral(
                "Could not copy the legacy activity ledger into the staged profile."));
    }

    const QString copiedDigest =
        ActivityStore::fileDigestSha256(destination);
    if (copiedDigest != sourceDigest) {
        return setError(
            error,
            QStringLiteral(
                "The staged activity ledger copy does not match the source activity ledger."));
    }

    if (stagedDigest)
        *stagedDigest = copiedDigest;
    return true;
}

bool FirstAccountProfileCoordinator::
verifyActivityDigest(
    const ProfilePaths &paths,
    const QString &profileRoot,
    const QString &expectedDigest,
    QString *actualDigest,
    QString *error) const {
    if (actualDigest)
        actualDigest->clear();

    if (expectedDigest.isEmpty()) {
        // Nothing was migrated for this profile, so there is nothing to
        // verify — deliberately skip reading the file at all. Once the
        // profile has been activated once, ActivityStore's own
        // ensureSchema() will have created a schema-stamped but
        // semantically-empty activity.sqlite there (a normal side effect of
        // simply opening it), which is a real file with real bytes but is
        // not "the migrated ledger" adoption promised to preserve — it must
        // never be compared against an empty digest sentinel.
        return true;
    }

    const auto storage =
        LegacyPersonalStateStorage::forProfileRoot(
            paths,
            profileRoot,
            error);
    if (!storage.has_value())
        return false;

    const QString digest =
        ActivityStore::fileDigestSha256(
            storage->activityDbPath());
    if (actualDigest)
        *actualDigest = digest;

    if (digest != expectedDigest) {
        return setError(
            error,
            QStringLiteral(
                "The profile activity ledger does not match the expected activity digest."));
    }
    return true;
}

bool FirstAccountProfileCoordinator::
backupActivityLedger(
    const ProfilePaths &paths,
    const QString &expectedDigest,
    QString *backupDigest,
    QString *error) const {
    if (backupDigest)
        backupDigest->clear();

    if (expectedDigest.isEmpty()) {
        // Nothing was migrated, so there is nothing to back up either — the
        // empty digest is the sentinel both sides compare against.
        return true;
    }

    const QString destination =
        activityBackupFilePath(paths);
    if (!QDir().mkpath(
            paths.adoptionBackupRoot())) {
        return setError(
            error,
            QStringLiteral(
                "Could not create the activity ledger rollback backup directory."));
    }

    if (QFileInfo::exists(destination)
        && !QFile::remove(destination)) {
        return setError(
            error,
            QStringLiteral(
                "Could not replace the existing activity ledger rollback backup."));
    }

    if (!QFile::copy(
            paths.activityDbPath(),
            destination)) {
        return setError(
            error,
            QStringLiteral(
                "Could not write the activity ledger rollback backup."));
    }

    const QString digest =
        ActivityStore::fileDigestSha256(destination);
    if (digest != expectedDigest) {
        return setError(
            error,
            QStringLiteral(
                "The activity ledger rollback backup does not match the promoted profile."));
    }

    if (backupDigest)
        *backupDigest = digest;
    return true;
}

bool FirstAccountProfileCoordinator::
restoreLegacyActivityFromBackup(
    const ProfilePaths &paths,
    QString *error) const {
    const QString backupPath =
        activityBackupFilePath(paths);
    if (!QFileInfo::exists(backupPath)) {
        // Nothing was ever backed up (adoption failed before reaching the
        // backup step, or there was no legacy activity data at all) — the
        // legacy file, if any, was never touched, so there is nothing to
        // restore. Not a failure.
        return true;
    }

    const QString legacyPath =
        m_profileRuntime->legacyStorage()
            .activityDbPath();
    if (legacyPath.isEmpty())
        return true;

    const QFileInfo legacyInfo(legacyPath);
    if (!QDir().mkpath(
            legacyInfo.absolutePath())) {
        return setError(
            error,
            QStringLiteral(
                "Could not recreate the legacy activity ledger directory."));
    }

    if (QFileInfo::exists(legacyPath)
        && !QFile::remove(legacyPath)) {
        return setError(
            error,
            QStringLiteral(
                "Could not clear the legacy activity ledger before restoring it."));
    }

    if (!QFile::copy(
            backupPath,
            legacyPath)) {
        return setError(
            error,
            QStringLiteral(
                "Could not restore the legacy activity ledger from backup."));
    }

    return true;
}

bool FirstAccountProfileCoordinator::
quarantineLegacyActivityLedger(
    QString *error) const {
    const QString legacyPath =
        m_profileRuntime->legacyStorage()
            .activityDbPath();
    if (legacyPath.isEmpty()
        || !QFileInfo::exists(legacyPath)) {
        return true; // nothing to quarantine
    }

    // Precondition: the caller has already suspended personal stores for
    // migration, so no live QSqlDatabase connection holds this file open —
    // removing it here is safe (CPP-PORT-CONTRACT §17 "flush/close it before
    // profile migration").
    if (!QFile::remove(legacyPath)) {
        return setError(
            error,
            QStringLiteral(
                "Could not remove the legacy activity ledger during quarantine."));
    }

    // WAL sidecars: a prior clean checkpoint+close normally leaves these
    // absent or empty, but clear them defensively so no orphaned sidecar
    // survives beside a now-deleted main file.
    QFile::remove(legacyPath + QStringLiteral("-wal"));
    QFile::remove(legacyPath + QStringLiteral("-shm"));
    return true;
}

std::optional<bool>
FirstAccountProfileCoordinator::
legacyPersonalStateClaimed(
    QString *error) const {
    const ProfilePaths local =
        ProfilePaths::localOnly(
            m_appDataRoot);
    const QString adoptionRoot =
        QDir(local.appDataRoot())
            .filePath(
                QStringLiteral(
                    "profile-adoption"));

    QDir directory(adoptionRoot);
    if (!directory.exists())
        return false;

    const QStringList journals =
        directory.entryList(
            QStringList()
                << QStringLiteral("*.json"),
            QDir::Files,
            QDir::Name);

    for (const QString &journal : journals) {
        const QString accountId =
            QFileInfo(journal)
                .completeBaseName();
        const auto paths =
            ProfilePaths::account(
                accountId,
                m_appDataRoot);
        if (!paths.has_value())
            continue;

        QString adoptionError;
        const auto adoption =
            ProfileAdoption::open(
                *paths,
                &adoptionError);
        if (!adoption.has_value()) {
            setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Could not inspect profile-adoption ownership state."),
                    adoptionError));
            return std::nullopt;
        }

        if (adoption->state()
                == ProfileAdoption::State::LegacyQuarantined
            || adoption->state()
                == ProfileAdoption::State::Committed) {
            return true;
        }
    }

    return false;
}

QString FirstAccountProfileCoordinator::
backupFilePath(
    const ProfilePaths &paths) {
    return QDir(
        paths.adoptionBackupRoot())
        .filePath(
            QStringLiteral(
                "personal-state.json"));
}

QString FirstAccountProfileCoordinator::
activityBackupFilePath(
    const ProfilePaths &paths) {
    return QDir(
        paths.adoptionBackupRoot())
        .filePath(
            QStringLiteral(
                "activity.sqlite"));
}

QString FirstAccountProfileCoordinator::
creationIntentPath(
    const ProfilePaths &paths) {
    return paths.adoptionJournalPath()
        + QStringLiteral(".created");
}

bool FirstAccountProfileCoordinator::
writeCreationIntent(
    const ProfilePaths &paths,
    QString *error) {
    const QString path =
        creationIntentPath(paths);
    if (QFileInfo::exists(path))
        return true;

    const QFileInfo info(path);
    if (!QDir().mkpath(
            info.absolutePath())) {
        return setError(
            error,
            QStringLiteral(
                "Could not create the first-account adoption intent directory."));
    }

    QJsonObject object;
    object.insert(
        QStringLiteral("version"),
        1);
    object.insert(
        QStringLiteral("account_id"),
        paths.profileId());

    const QByteArray payload =
        QJsonDocument(object)
            .toJson(QJsonDocument::Compact);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return setError(
            error,
            QStringLiteral(
                "Could not persist first-account adoption intent."));
    }

    if (file.write(payload)
        != payload.size()
        || !file.commit()) {
        return setError(
            error,
            QStringLiteral(
                "Could not commit first-account adoption intent."));
    }

    return true;
}

bool FirstAccountProfileCoordinator::
clearCreationIntent(
    const ProfilePaths &paths,
    QString *error) {
    const QString path =
        creationIntentPath(paths);
    if (!QFile::remove(path)
        && QFileInfo::exists(path)) {
        return setError(
            error,
            QStringLiteral(
                "Could not clear first-account adoption intent."));
    }
    return true;
}

bool FirstAccountProfileCoordinator::
hasCreationIntent(
    const ProfilePaths &paths) {
    const QString path =
        creationIntentPath(paths);
    if (!QFileInfo::exists(path))
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return true;

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            file.readAll(),
            &parseError);
    if (parseError.error
            != QJsonParseError::NoError
        || !document.isObject()) {
        return true;
    }

    const QJsonObject object =
        document.object();
    return object
        .value(QStringLiteral("account_id"))
        .toString()
        == paths.profileId();
}

bool FirstAccountProfileCoordinator::setError(
    QString *error,
    const QString &message) {
    if (error)
        *error = message;
    return false;
}
