// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "FirstAccountProfileCoordinator.h"

#include "ActivityStore.h"
#include "AccountAttachmentReceipt.h"
#include "LegacyPersonalStateStorage.h"
#include "ProfilePreferencesStore.h"
#include "ProfileStoreRuntime.h"
#include "SyncStateStore.h"

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
#include <QSet>
#include <QUuid>

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

bool ensureAttachmentReceipt(
    const ProfilePaths &paths,
    ProfilePaths::Kind sourceKind,
    const PersonalStateSnapshot &source,
    const QString &activityDigest,
    QString *error) {
    const QString sourceKindName =
        sourceKind == ProfilePaths::Kind::LocalOnly
        ? AccountAttachmentReceipt::sourceKindLocalOnly()
        : AccountAttachmentReceipt::sourceKindLegacyLocal();
    const QString sourceProfileId =
        sourceKind == ProfilePaths::Kind::LocalOnly
        ? ProfilePaths::localOnly(paths.appDataRoot()).profileId()
        : ProfilePaths::legacyLocal().profileId();

    const AccountAttachmentReceipt::ReadResult existing =
        AccountAttachmentReceipt::read(paths);
    if (existing.status == AccountAttachmentReceipt::ReadStatus::Invalid) {
        if (error)
            *error = adoptionFailure(
                QStringLiteral("The account attachment receipt is invalid."),
                existing.error);
        return false;
    }

    AccountAttachmentReceiptData receipt;
    if (existing.status == AccountAttachmentReceipt::ReadStatus::Ok) {
        receipt = existing.data;
        if (receipt.sourceKind != sourceKindName
            || receipt.sourceProfileId != sourceProfileId
            || receipt.sourceSemanticDigest != source.semanticDigest()
            || receipt.sourceActivityDigest != activityDigest
            || receipt.sourceRetired) {
            if (error)
                *error = QStringLiteral(
                    "The existing account attachment receipt does not match the local source.");
            return false;
        }
        return true;
    }

    receipt.version = 1;
    receipt.attachmentId =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    receipt.sourceKind = sourceKindName;
    receipt.sourceProfileId = sourceProfileId;
    receipt.sourceSemanticDigest = source.semanticDigest();
    receipt.sourceActivityDigest = activityDigest;
    receipt.accountId = paths.profileId();
    SyncStateStore stateStore;
    const auto syncState = stateStore.load(paths.syncStatePath(), error);
    if (!syncState.has_value())
        return false;
    QSet<QString> preexistingIds;
    for (const SyncWireMutation &mutation : syncState->outbox)
        preexistingIds.insert(mutation.mutationId);
    for (auto it = syncState->pausedCategories.constBegin();
         it != syncState->pausedCategories.constEnd(); ++it) {
        for (const SyncWireMutation &mutation : it->pendingMutations)
            preexistingIds.insert(mutation.mutationId);
    }
    receipt.preexistingMutationIds = preexistingIds.values();
    receipt.preexistingMutationIds.sort();
    receipt.sourceRetired = false;
    receipt.retirementPhase =
        AccountAttachmentReceipt::retirementPhasePending();
    return AccountAttachmentReceipt::save(paths, receipt, error);
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

qint64 jsonTimestamp(
    const QJsonValue &value,
    const QString &key) {
    if (!value.isObject())
        return 0;
    const QJsonValue timestamp =
        value.toObject().value(key);
    return timestamp.isDouble()
        ? static_cast<qint64>(timestamp.toDouble())
        : timestamp.toString().toLongLong();
}

QJsonObject mergeTimestampedObjects(
    const QJsonObject &account,
    const QJsonObject &local,
    const QStringList &timestampKeys) {
    QJsonObject merged = account;
    for (auto it = local.constBegin();
         it != local.constEnd();
         ++it) {
        if (!merged.contains(it.key())) {
            merged.insert(it.key(), it.value());
            continue;
        }

        const QJsonValue current = merged.value(it.key());
        qint64 currentTime = 0;
        qint64 localTime = 0;
        for (const QString &key : timestampKeys) {
            currentTime = qMax(currentTime, jsonTimestamp(current, key));
            localTime = qMax(localTime, jsonTimestamp(it.value(), key));
        }
        if (localTime > currentTime)
            merged.insert(it.key(), it.value());
    }
    return merged;
}

QJsonObject mergeHistoryObjects(
    const QJsonObject &account,
    const QJsonObject &local) {
    QJsonObject merged = account;
    for (auto it = local.constBegin();
         it != local.constEnd();
         ++it) {
        if (!merged.contains(it.key())) {
            merged.insert(it.key(), it.value());
            continue;
        }

        const QJsonObject left = merged.value(it.key()).toObject();
        const QJsonObject right = it.value().toObject();
        if (left.isEmpty() || right.isEmpty())
            continue;

        QJsonObject combined = left;
        const qint64 first = qMin(
            jsonTimestamp(left, QStringLiteral("firstActivityAt")),
            jsonTimestamp(right, QStringLiteral("firstActivityAt")));
        const qint64 last = qMax(
            jsonTimestamp(left, QStringLiteral("lastActivityAt")),
            jsonTimestamp(right, QStringLiteral("lastActivityAt")));
        const qint64 completed = qMax(
            jsonTimestamp(left, QStringLiteral("completedAt")),
            jsonTimestamp(right, QStringLiteral("completedAt")));
        if (first > 0)
            combined.insert(QStringLiteral("firstActivityAt"), first);
        if (last > 0)
            combined.insert(QStringLiteral("lastActivityAt"), last);
        if (completed > 0)
            combined.insert(QStringLiteral("completedAt"), completed);
        merged.insert(it.key(), combined);
    }
    return merged;
}

QJsonObject mergeSearchHistory(
    const QJsonObject &account,
    const QJsonObject &local) {
    QJsonObject merged = account;
    for (auto it = local.constBegin();
         it != local.constEnd();
         ++it) {
        QStringList values;
        const auto append = [&values](const QJsonValue &value) {
            for (const QJsonValue &candidate : value.toArray()) {
                const QString text = candidate.toString().trimmed();
                if (text.isEmpty())
                    continue;
                bool duplicate = false;
                for (const QString &existing : values) {
                    if (existing.compare(text, Qt::CaseInsensitive) == 0) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate && values.size() < 6)
                    values.append(text);
            }
        };
        append(merged.value(it.key()));
        append(it.value());

        QJsonArray array;
        for (const QString &value : values)
            array.append(value);
        merged.insert(it.key(), array);
    }
    return merged;
}

PersonalStateSnapshot mergeSnapshots(
    const PersonalStateSnapshot &account,
    const PersonalStateSnapshot &local) {
    PersonalStateSnapshot merged = account;
    merged.progressEntries = mergeTimestampedObjects(
        account.progressEntries,
        local.progressEntries,
        {QStringLiteral("updatedAt")});
    merged.collectionEntries = mergeTimestampedObjects(
        account.collectionEntries,
        local.collectionEntries,
        {QStringLiteral("updatedAt"), QStringLiteral("addedAt")});
    merged.audioPairings = mergeTimestampedObjects(
        account.audioPairings,
        local.audioPairings,
        {QStringLiteral("updatedAt")});
    merged.historyRecords = mergeHistoryObjects(
        account.historyRecords,
        local.historyRecords);
    merged.searchHistory = mergeSearchHistory(
        account.searchHistory,
        local.searchHistory);

    for (auto it = local.progressLastSeason.constBegin();
         it != local.progressLastSeason.constEnd();
         ++it) {
        const QJsonValue current =
            merged.progressLastSeason.value(it.key());
        if (!current.isDouble() || it.value().toDouble() > current.toDouble())
            merged.progressLastSeason.insert(it.key(), it.value());
    }
    for (auto it = local.progressWatchedMarks.constBegin();
         it != local.progressWatchedMarks.constEnd();
         ++it) {
        const QJsonValue current =
            merged.progressWatchedMarks.value(it.key());
        if (!current.isDouble() || it.value().toDouble() > current.toDouble())
            merged.progressWatchedMarks.insert(it.key(), it.value());
    }
    merged.showExplicit = account.showExplicit || local.showExplicit;
    return merged;
}

QJsonObject historyRecordFromActivityFact(
    const QVariantMap &fact) {
    const QString kind =
        fact.value(QStringLiteral("kind"))
            .toString()
            .trimmed();
    const QString id =
        fact.value(QStringLiteral("itemKey"))
            .toString()
            .trimmed();
    const QString type =
        fact.value(QStringLiteral("type"))
            .toString();
    if (kind.isEmpty() || id.isEmpty())
        return {};

    qint64 first = 0;
    qint64 last = 0;
    qint64 completed = 0;
    if (type == QStringLiteral("playback_delta")) {
        first = fact.value(QStringLiteral("startAtMs")).toLongLong();
        last = fact.value(QStringLiteral("endAtMs")).toLongLong();
    } else if (type == QStringLiteral("reading_delta")) {
        first = fact.value(QStringLiteral("atMs")).toLongLong();
        last = first;
    } else if (type == QStringLiteral("media_completed")) {
        first = fact.value(QStringLiteral("atMs")).toLongLong();
        last = first;
        completed = first;
    } else {
        return {};
    }

    if (first <= 0 || last < first)
        return {};

    QJsonObject record{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), id},
        {QStringLiteral("firstActivityAt"), first},
        {QStringLiteral("lastActivityAt"), last}};
    if (completed > 0)
        record.insert(QStringLiteral("completedAt"), completed);
    return record;
}

bool matchesWithActivityProjection(
    const PersonalStateSnapshot &expected,
    const PersonalStateSnapshot &current,
    const QList<QVariantMap> &facts) {
    if (current.progressEntries != expected.progressEntries
        || current.progressLastSeason != expected.progressLastSeason
        || current.progressWatchedMarks != expected.progressWatchedMarks
        || current.collectionEntries != expected.collectionEntries
        || current.searchHistory != expected.searchHistory
        || current.audioPairings != expected.audioPairings
        || current.showExplicit != expected.showExplicit) {
        return false;
    }

    PersonalStateSnapshot projected = expected;
    for (const QVariantMap &fact : facts) {
        if (!fact.value(QStringLiteral("syncable")).toBool())
            continue;
        const QJsonObject record = historyRecordFromActivityFact(fact);
        if (record.isEmpty())
            continue;

        const QString key =
            record.value(QStringLiteral("kind"))
                .toString()
            + QChar(0x1f)
            + record.value(QStringLiteral("id"))
                .toString();
        projected.historyRecords = mergeHistoryObjects(
            projected.historyRecords,
            QJsonObject{{key, record}});
    }

    return current.historyRecords == projected.historyRecords;
}

bool migrationSourceHasActivity(
    ProfileStoreRuntime *profileRuntime,
    const LegacyPersonalStateStorage &sourceStorage) {
    const ProfilePaths::Kind activeKind =
        profileRuntime->activeProfile().kind();
    if ((activeKind == ProfilePaths::Kind::LegacyLocal
         || activeKind == ProfilePaths::Kind::LocalOnly)
        && profileRuntime->activityStore()) {
        return !profileRuntime->activityStore()
                    ->historyProjectionFacts()
                    .isEmpty();
    }

    if (!QFileInfo::exists(sourceStorage.activityDbPath()))
        return false;

    ActivityStore sourceActivity(sourceStorage.activityDbPath());
    return sourceActivity.healthy()
        && !sourceActivity.historyProjectionFacts().isEmpty();
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
    const bool activeAccountMatches =
        active.kind() == ProfilePaths::Kind::Account
        && active.profileId() == paths->profileId();
    if (active.kind()
            == ProfilePaths::Kind::Account) {
        if (!activeAccountMatches)
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

    bool explicitProfile = false;
    QString sourceError;
    const auto sourceStorage =
        currentMigrationSource(
            &explicitProfile,
            &sourceError);
    if (!sourceStorage.has_value()) {
        if (!sourceError.isEmpty())
            return setError(error, sourceError);
    } else {
        QString captureError;
        const auto source =
            sourceStorage->capture(&captureError);
        if (!source.has_value()) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Could not capture local personal state before sign-in."),
                    captureError));
        }

        const bool hasActivity =
            migrationSourceHasActivity(
                m_profileRuntime,
                *sourceStorage);
        if (!source->isEmpty() || hasActivity) {
            if (!QFileInfo::exists(paths->profileRoot())) {
                if (explicitProfile)
                    return runLocalOnlyAdoption(
                        *paths,
                        *sourceStorage,
                        error);
                return runFreshAdoption(*paths, error);
            }

            return mergeExistingAccount(
                *paths,
                *sourceStorage,
                explicitProfile
                    ? ProfilePaths::Kind::LocalOnly
                    : ProfilePaths::Kind::LegacyLocal,
                error);
        }
    }

    if (!QFileInfo::exists(paths->profileRoot())) {
        if (!createEmptyProfile(
                *paths,
                error)) {
            return false;
        }
    }

    if (activeAccountMatches)
        return true;

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
            return prepareAccountSession(
                accountId,
                error);
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

    return prepareAccountSession(
        accountId,
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

std::optional<LegacyPersonalStateStorage>
FirstAccountProfileCoordinator::
currentMigrationSource(
    bool *explicitProfile,
    QString *error) const {
    if (explicitProfile)
        *explicitProfile = false;

    const ProfilePaths &active =
        m_profileRuntime->activeProfile();
    const ProfilePaths localPaths =
        ProfilePaths::localOnly(m_appDataRoot);

    QString localError;
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(
            localPaths,
            &localError);
    if (!localStorage.has_value()) {
        if (active.kind() == ProfilePaths::Kind::LocalOnly) {
            if (error)
                *error = localError;
            return std::nullopt;
        }
    } else {
        QString captureError;
        const auto local =
            localStorage->capture(&captureError);
        if (!local.has_value()) {
            if (active.kind() == ProfilePaths::Kind::LocalOnly) {
                if (error)
                    *error = captureError;
                return std::nullopt;
            }
        } else if (active.kind() == ProfilePaths::Kind::LocalOnly
                   || !local->isEmpty()
                   || migrationSourceHasActivity(
                       m_profileRuntime,
                       *localStorage)) {
            if (explicitProfile)
                *explicitProfile = true;
            return localStorage;
        }
    }

    QString legacyError;
    const auto legacy =
        m_profileRuntime->legacyStorage().capture(
            &legacyError);
    if (!legacy.has_value()) {
        if (active.kind() == ProfilePaths::Kind::LegacyLocal
            || !localStorage.has_value()) {
            if (error)
                *error = legacyError;
            return std::nullopt;
        }
    } else if (active.kind() == ProfilePaths::Kind::LegacyLocal
               || !legacy->isEmpty()
               || migrationSourceHasActivity(
                   m_profileRuntime,
                   m_profileRuntime->legacyStorage())) {
        return m_profileRuntime->legacyStorage();
    }

    return std::nullopt;
}

std::optional<FirstAccountProfileCoordinator::ResolvedAdoptionSource>
FirstAccountProfileCoordinator::
resolveAdoptionSource(
    const ProfileAdoption &adoption,
    QString *error) const {
    const ProfileAdoption::Snapshot snapshot = adoption.snapshot();
    if (snapshot.sourceKindRecorded) {
        if (snapshot.sourceKind == ProfilePaths::Kind::LegacyLocal) {
            return ResolvedAdoptionSource{
                m_profileRuntime->legacyStorage(),
                ProfilePaths::Kind::LegacyLocal};
        }

        const ProfilePaths localPaths =
            ProfilePaths::localOnly(m_appDataRoot);
        QString localError;
        const auto localStorage =
            LegacyPersonalStateStorage::forProfile(
                localPaths,
                &localError);
        if (!localStorage.has_value()) {
            setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "Could not resolve the explicit-local adoption source."),
                    localError));
            return std::nullopt;
        }

        return ResolvedAdoptionSource{
            *localStorage,
            ProfilePaths::Kind::LocalOnly};
    }

    // v1 journals did not persist the source kind.  Recover only when the
    // recorded semantic identity identifies exactly one source.  A matching
    // activity digest is part of the identity when the journal has one; this
    // keeps activity-only legacy/local sources distinguishable as well.
    QString legacyError;
    const auto legacy =
        m_profileRuntime->legacyStorage().capture(&legacyError);
    const ProfilePaths localPaths =
        ProfilePaths::localOnly(m_appDataRoot);
    QString localError;
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(
            localPaths,
            &localError);
    std::optional<PersonalStateSnapshot> local;
    if (localStorage.has_value())
        local = localStorage->capture(&localError);

    // An old journal does not identify which source was adopted.  An
    // unreadable candidate cannot be treated as an empty candidate: doing so
    // could make a healthy store appear to be the unique source and cause a
    // retry to adopt the wrong data.  Keep this compatibility path fail
    // closed until both candidates have been inspected successfully.
    if (!legacy.has_value()
        || !localStorage.has_value()
        || !local.has_value()) {
        QString detail;
        if (!legacy.has_value())
            detail = legacyError;
        if (!localStorage.has_value() || !local.has_value()) {
            if (!detail.isEmpty())
                detail += QStringLiteral(" ");
            detail += localError;
        }
        setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not inspect both local adoption sources safely."),
                detail));
        return std::nullopt;
    }

    const auto activityMatches =
        [&](const LegacyPersonalStateStorage &storage) {
            const QString expected = snapshot.activitySourceDigest;
            return expected.isEmpty()
                || ActivityStore::fileDigestSha256(
                       storage.activityDbPath())
                    == expected;
        };

    const bool legacyMatches =
        legacy->matchesSemanticDigest(
               snapshot.sourceSemanticDigest)
        && activityMatches(m_profileRuntime->legacyStorage());
    const bool localMatches =
        local->matchesSemanticDigest(
               snapshot.sourceSemanticDigest)
        && activityMatches(*localStorage);

    if (legacyMatches && !localMatches) {
        return ResolvedAdoptionSource{
            m_profileRuntime->legacyStorage(),
            ProfilePaths::Kind::LegacyLocal};
    }
    if (localMatches && !legacyMatches) {
        return ResolvedAdoptionSource{
            *localStorage,
            ProfilePaths::Kind::LocalOnly};
    }

    if (legacyMatches && localMatches) {
        // An empty v1 source has no private state whose ownership could be
        // confused.  Keep the legacy default for that safe compatibility
        // case; populated matches in both stores remain ambiguous.
        if (snapshot.activitySourceDigest.isEmpty()
            && legacy->isEmpty()
            && local->isEmpty()) {
            return ResolvedAdoptionSource{
                m_profileRuntime->legacyStorage(),
                ProfilePaths::Kind::LegacyLocal};
        }
        setError(
            error,
            QStringLiteral(
                "The interrupted adoption source is ambiguous; both local stores match."));
        return std::nullopt;
    }

    QString detail;
    if (!legacy.has_value() && !legacyError.isEmpty())
        detail = legacyError;
    if (!localStorage.has_value() && !localError.isEmpty()) {
        if (!detail.isEmpty())
            detail += QStringLiteral(" ");
        detail += localError;
    }
    setError(
        error,
        adoptionFailure(
            QStringLiteral(
                "Could not identify the source of the interrupted adoption."),
            detail));
    return std::nullopt;
}

bool FirstAccountProfileCoordinator::
clearMigrationSource(
    const LegacyPersonalStateStorage &sourceStorage,
    bool explicitProfile,
    QString *error) {
    Q_UNUSED(explicitProfile);
    if (!sourceStorage.clearPersonalState(error))
        return false;

    const QString activityPath =
        sourceStorage.activityDbPath();
    QFile::remove(activityPath);
    QFile::remove(activityPath + QStringLiteral("-wal"));
    QFile::remove(activityPath + QStringLiteral("-shm"));

    const auto cleared =
        sourceStorage.capture(error);
    if (!cleared.has_value())
        return false;
    if (!cleared->isEmpty()) {
        return setError(
            error,
            QStringLiteral(
                "Local personal state was not fully merged into the account."));
    }
    return true;
}

bool FirstAccountProfileCoordinator::
runLocalOnlyAdoption(
    const ProfilePaths &paths,
    const LegacyPersonalStateStorage &sourceStorage,
    QString *error) {
    m_profileRuntime->flushPersonalStores();

    QString captureError;
    const auto source =
        sourceStorage.capture(&captureError);
    if (!source.has_value()) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not capture local personal state."),
                captureError));
    }

    const QString activitySourceDigest =
        ActivityStore::fileDigestSha256(
            sourceStorage.activityDbPath());
    auto adoption =
        ProfileAdoption::begin(
            paths,
            source->semanticDigest(),
            ProfilePaths::Kind::LocalOnly,
            error);
    if (!adoption.has_value())
        return false;

    clearCreationIntent(paths, nullptr);
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
            sourceStorage,
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
            error)
        || !adoption->markActivityTargetVerified(
            activitySourceDigest,
            stagedActivityDigest,
            error)
        || !adoption->promote(error)) {
        return false;
    }

    if (!writeBackup(paths, *source, error))
        return false;
    QString activityBackupDigest;
    if (!backupActivityLedger(
            paths,
            sourceStorage,
            activitySourceDigest,
            &activityBackupDigest,
            error)) {
        return false;
    }

    m_profileRuntime->suspendPersonalStoresForMigration();
    if (!clearMigrationSource(
            sourceStorage,
            true,
            error)) {
        restoreSourceForRetry(
            paths,
            sourceStorage,
            ProfilePaths::Kind::LocalOnly,
            *source,
            nullptr);
        return false;
    }

    if (!adoption->markActivityLegacyQuarantined(
            activityBackupDigest,
            error)
        || !adoption->markLegacyQuarantined(
            source->semanticDigest(),
            error)) {
        restoreSourceForRetry(
            paths,
            sourceStorage,
            ProfilePaths::Kind::LocalOnly,
            *source,
            nullptr);
        return false;
    }

    if (!adoption->commit(error))
        return false;

    m_quarantinedThisProcess.insert(paths.profileId());
    return activate(paths, error);
}

bool FirstAccountProfileCoordinator::
mergeExistingAccount(
    const ProfilePaths &paths,
    const LegacyPersonalStateStorage &sourceStorage,
    ProfilePaths::Kind sourceKind,
    QString *error) {
    const auto targetStorage =
        LegacyPersonalStateStorage::forProfile(
            paths,
            error);
    if (!targetStorage.has_value())
        return false;

    m_profileRuntime->flushPersonalStores();

    QString sourceError;
    const auto source =
        sourceStorage.capture(&sourceError);
    const auto target =
        targetStorage->capture(error);
    if (!source.has_value() || !target.has_value()) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not read both local and account state for merging."),
                source.has_value() ? QString() : sourceError));
    }

    const PersonalStateSnapshot merged =
        mergeSnapshots(*target, *source);

    QList<QVariantMap> activityFacts;
    const ProfilePaths::Kind activeKind =
        m_profileRuntime->activeProfile().kind();
    if ((activeKind == ProfilePaths::Kind::LegacyLocal
         || activeKind == ProfilePaths::Kind::LocalOnly)
        && m_profileRuntime->activityStore()) {
        activityFacts =
            m_profileRuntime->activityStore()
                ->historyProjectionFacts();
    } else {
        // Constructing ActivityStore creates a SQLite file.  Do not turn a
        // source with no Activity ledger into a new empty ledger during a
        // repeated sign-in; the receipt's empty digest is its durable
        // identity for that source state.
        const QString activityPath = sourceStorage.activityDbPath();
        if (QFileInfo::exists(activityPath)) {
            ActivityStore sourceActivity(activityPath);
            if (sourceActivity.healthy())
                activityFacts = sourceActivity.historyProjectionFacts();
        }
    }

    m_profileRuntime->suspendPersonalStoresForMigration();
    if (!targetStorage->restorePersonalState(merged, error)) {
        sourceStorage.restorePersonalState(*source, nullptr);
        m_profileRuntime->activateLocalOnlyProfile(nullptr);
        return false;
    }

    ActivityStore targetActivity(
        targetStorage->activityDbPath());
    if (!targetActivity.healthy(error)) {
        sourceStorage.restorePersonalState(*source, nullptr);
        m_profileRuntime->activateLocalOnlyProfile(nullptr);
        return false;
    }
    for (const QVariantMap &fact : activityFacts) {
        const QString type =
            fact.value(QStringLiteral("type")).toString();
        bool accepted = false;
        if (type == QStringLiteral("playback_delta"))
            accepted = targetActivity.recordPlaybackDelta(fact);
        else if (type == QStringLiteral("reading_delta"))
            accepted = targetActivity.recordReadingDelta(fact);
        else if (type == QStringLiteral("media_completed"))
            accepted = targetActivity.recordCompletion(fact);
        if (!accepted) {
            sourceStorage.restorePersonalState(*source, nullptr);
            m_profileRuntime->activateLocalOnlyProfile(nullptr);
            return setError(
                error,
                QStringLiteral(
                    "The local activity history could not be merged into the account."));
        }
    }

    QString verifyError;
    if (!verifyProfile(
            paths,
            paths.profileRoot(),
            merged,
            nullptr,
            &verifyError)) {
        sourceStorage.restorePersonalState(*source, nullptr);
        m_profileRuntime->activateLocalOnlyProfile(nullptr);
        return setError(error, verifyError);
    }

    const QString activitySourceDigest =
        ActivityStore::fileDigestSha256(
            sourceStorage.activityDbPath());
    if (!ensureAttachmentReceipt(
            paths,
            sourceKind,
            *source,
            activitySourceDigest,
            error)) {
        sourceStorage.restorePersonalState(*source, nullptr);
        m_profileRuntime->activateLocalOnlyProfile(nullptr);
        return false;
    }

    if (!activate(paths, error)) {
        sourceStorage.restorePersonalState(*source, nullptr);
        m_profileRuntime->activateLocalOnlyProfile(nullptr);
        return false;
    }
    return true;
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
            ProfilePaths::Kind::LegacyLocal,
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
        m_profileRuntime->legacyStorage(),
        ProfilePaths::Kind::LegacyLocal,
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
        const auto resolved =
            resolveAdoptionSource(adoption, error);
        if (!resolved.has_value())
            return false;

        QString captureError;
        const auto source =
            resolved->storage
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

        if (resolved->kind == ProfilePaths::Kind::LocalOnly)
            return runLocalOnlyAdoption(
                paths,
                resolved->storage,
                error);
        return runFreshAdoption(paths, error);
    }

    case ProfileAdoption::State::Promoted: {
        const auto resolved =
            resolveAdoptionSource(adoption, error);
        if (!resolved.has_value())
            return false;

        QString captureError;
        const auto source =
            resolved->storage
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

            if (resolved->kind == ProfilePaths::Kind::LocalOnly)
                return runLocalOnlyAdoption(
                    paths,
                    resolved->storage,
                    error);
            return runFreshAdoption(paths, error);
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
            resolved->storage,
            resolved->kind,
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
        auto resolved =
            resolveAdoptionSource(adoption, nullptr);
        if (!resolved.has_value()
            && !adoption.snapshot().sourceKindRecorded) {
            // Compatibility for the old retry journal: its legacy-only
            // writer could not identify a source after a history projection
            // changed the semantic digest.  Permit that existing recovery
            // path only when one materialized source is present; two sources
            // remain ambiguous and fail closed.
            QString legacyError;
            const auto legacy =
                m_profileRuntime->legacyStorage().capture(&legacyError);
            const ProfilePaths localPaths =
                ProfilePaths::localOnly(m_appDataRoot);
            QString localError;
            const auto localStorage =
                LegacyPersonalStateStorage::forProfile(
                    localPaths,
                    &localError);
            std::optional<PersonalStateSnapshot> local;
            if (localStorage.has_value())
                local = localStorage->capture(&localError);

            // The fallback may only recover the old projection-based retry
            // when both possible sources were readable.  A failed capture is
            // evidence, not absence, and selecting the other store would
            // misattribute the retry.
            if (!legacy.has_value()
                || !localStorage.has_value()
                || !local.has_value()) {
                QString detail;
                if (!legacy.has_value())
                    detail = legacyError;
                if (!localStorage.has_value() || !local.has_value()) {
                    if (!detail.isEmpty())
                        detail += QStringLiteral(" ");
                    detail += localError;
                }
                return setError(
                    error,
                    adoptionFailure(
                        QStringLiteral(
                            "Could not inspect both local adoption sources safely for retry."),
                        detail));
            }

            const auto hasMaterializedState =
                [&](const std::optional<PersonalStateSnapshot> &state,
                    const LegacyPersonalStateStorage &storage) {
                    return state.has_value()
                        && (!state->isEmpty()
                            || (!adoption.snapshot().activitySourceDigest.isEmpty()
                                && QFileInfo::exists(
                                    storage.activityDbPath())));
                };
            const bool legacyPresent =
                hasMaterializedState(
                    legacy,
                    m_profileRuntime->legacyStorage());
            const bool localPresent =
                hasMaterializedState(local, *localStorage);
            if (legacyPresent && !localPresent) {
                resolved = ResolvedAdoptionSource{
                    m_profileRuntime->legacyStorage(),
                    ProfilePaths::Kind::LegacyLocal};
            } else if (localPresent && !legacyPresent) {
                resolved = ResolvedAdoptionSource{
                    *localStorage,
                    ProfilePaths::Kind::LocalOnly};
            }
        }
        if (!resolved.has_value()) {
            return setError(
                error,
                QStringLiteral(
                    "Could not identify the source of the adoption retry."));
        }

        QString captureError;
        const auto source =
            resolved->storage
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
            bool onlyActivityProjection = false;
            const auto backup = readBackup(paths, nullptr);
            const QString activityPath =
                resolved->storage.activityDbPath();
            if (backup.has_value()
                && QFileInfo::exists(activityPath)) {
                ActivityStore activity(activityPath);
                if (activity.healthy()) {
                    onlyActivityProjection =
                        matchesWithActivityProjection(
                            *backup,
                            *source,
                            activity.historyProjectionFacts());
                }
            }
            if (onlyActivityProjection) {
                if (!adoption.rollbackBeforeLegacyQuarantine(error))
                    return false;
                if (resolved->kind == ProfilePaths::Kind::LocalOnly)
                    return runLocalOnlyAdoption(
                        paths,
                        resolved->storage,
                        error);
                return runFreshAdoption(paths, error);
            }

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

        if (resolved->kind == ProfilePaths::Kind::LocalOnly)
            return runLocalOnlyAdoption(
                paths,
                resolved->storage,
                error);
        return runFreshAdoption(paths, error);
    }

    case ProfileAdoption::State::Committed:
        if (!activate(paths, error))
            return false;
        return mergeResidualLocalOnlyState(paths, error);
    }

    return setError(
        error,
        QStringLiteral(
            "The profile adoption state is unsupported."));
}

bool FirstAccountProfileCoordinator::
mergeResidualLocalOnlyState(
    const ProfilePaths &paths,
    QString *error) {
    const AccountAttachmentReceipt::ReadResult attachment =
        AccountAttachmentReceipt::read(paths);
    if (attachment.status
        == AccountAttachmentReceipt::ReadStatus::Invalid) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "The account attachment receipt is invalid."),
                attachment.error));
    }
    if (attachment.status
            == AccountAttachmentReceipt::ReadStatus::Ok
        && !attachment.data.sourceRetired) {
        // A promoted local-only source belongs to the pending cloud
        // attachment.  Do not merge or clear it again while recovering the
        // local ownership journal.
        return true;
    }

    const ProfilePaths localPaths =
        ProfilePaths::localOnly(m_appDataRoot);
    QString localError;
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(
            localPaths,
            &localError);
    if (!localStorage.has_value()) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not inspect local personal state after account migration."),
                localError));
    }

    const auto local =
        localStorage->capture(&localError);
    if (!local.has_value()) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not read local personal state after account migration."),
                localError));
    }

    if (local->isEmpty())
        return true;

    return mergeExistingAccount(
        paths,
        *localStorage,
        ProfilePaths::Kind::LocalOnly,
        error);
}

bool FirstAccountProfileCoordinator::
finishPromotedAdoption(
    const ProfilePaths &paths,
    ProfileAdoption adoption,
    const LegacyPersonalStateStorage &sourceStorage,
    ProfilePaths::Kind sourceKind,
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
            sourceStorage,
            activitySourceDigest,
            &activityBackupDigest,
            error)) {
        return false;
    }

    // Promotion is the local ownership boundary.  The source remains intact
    // until the account attachment coordinator proves cloud absorption and
    // performs the exact receipt-bound retirement.  Persist this receipt
    // before committing the promoted adoption so a crash cannot leave a
    // committed account with no durable attachment evidence.
    const QString sourceProfileId =
        sourceKind == ProfilePaths::Kind::LocalOnly
        ? ProfilePaths::localOnly(paths.appDataRoot()).profileId()
        : ProfilePaths::legacyLocal().profileId();
    AccountAttachmentReceiptData attachment;
    const AccountAttachmentReceipt::ReadResult existingReceipt =
        AccountAttachmentReceipt::read(paths);
    if (existingReceipt.status
        == AccountAttachmentReceipt::ReadStatus::Invalid) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "The account attachment receipt is invalid."),
                existingReceipt.error));
    }
    if (existingReceipt.status
        == AccountAttachmentReceipt::ReadStatus::Ok) {
        attachment = existingReceipt.data;
        if (attachment.sourceKind
                != (sourceKind == ProfilePaths::Kind::LocalOnly
                        ? AccountAttachmentReceipt::sourceKindLocalOnly()
                        : AccountAttachmentReceipt::sourceKindLegacyLocal())
            || attachment.sourceProfileId != sourceProfileId
            || attachment.sourceSemanticDigest
                   != source.semanticDigest()
            || attachment.sourceActivityDigest
                   != activitySourceDigest) {
            return setError(
                error,
                QStringLiteral(
                    "The account attachment receipt does not match the promoted source."));
        }
    } else {
        attachment.version = 1;
        attachment.attachmentId =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        attachment.sourceKind =
            sourceKind == ProfilePaths::Kind::LocalOnly
            ? AccountAttachmentReceipt::sourceKindLocalOnly()
            : AccountAttachmentReceipt::sourceKindLegacyLocal();
        attachment.sourceProfileId = sourceProfileId;
        attachment.sourceSemanticDigest = source.semanticDigest();
        attachment.sourceActivityDigest = activitySourceDigest;
        attachment.accountId = paths.profileId();
        attachment.sourceRetired = false;
        attachment.retirementPhase =
            AccountAttachmentReceipt::retirementPhasePending();
        if (!AccountAttachmentReceipt::save(
                paths,
                attachment,
                error)) {
            return false;
        }
    }

    m_profileRuntime
        ->suspendPersonalStoresForMigration();
    if (!adoption.commitForAttachment(error))
        return false;

    Q_UNUSED(activityBackupDigest);
    return activate(paths, error);
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

    // A post-quarantine journal has already cleared its source.  Use the
    // recorded source kind when present so an explicit-local adoption never
    // treats unrelated legacy settings as the source of truth.  v1 journals
    // had no source field; their compatibility default remains legacy-local,
    // the only source the old writer could record, and any non-empty legacy
    // state fails closed below.
    LegacyPersonalStateStorage sourceStorage =
        m_profileRuntime->legacyStorage();
    if (adoption.snapshot().sourceKindRecorded
        && adoption.snapshot().sourceKind == ProfilePaths::Kind::LocalOnly) {
        const ProfilePaths localPaths =
            ProfilePaths::localOnly(m_appDataRoot);
        const auto localStorage =
            LegacyPersonalStateStorage::forProfile(
                localPaths,
                error);
        if (!localStorage.has_value())
            return false;
        sourceStorage = *localStorage;
    }

    const auto legacy =
        sourceStorage.capture(error);
    if (!legacy.has_value())
        return false;

    if (!legacy->isEmpty()) {
        return setError(
            error,
            QStringLiteral(
                "Adoption source state reappeared before adoption commit."));
    }

    if (!migratedProfileFilesPresent(
            paths,
            *backup,
            adoption.snapshot().activitySourceDigest)) {
        return setError(
            error,
            QStringLiteral(
                "A promoted profile file disappeared; the account adoption evidence was preserved."));
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
    if (!current.has_value())
        return false;

    QString currentDigest;
    if (!verifyProfile(
            paths,
            paths.profileRoot(),
            *current,
            &currentDigest,
            error))
        return false;

    // Activity is mutable account data after promotion.  Restart recovery
    // checks that the current ledger remains readable, but never compares its
    // bytes with the pre-activation digest or rolls the account back because
    // new events were recorded.
    if (QFileInfo::exists(paths.activityDbPath())) {
        ActivityStore activity(paths.activityDbPath());
        QString activityError;
        if (!activity.healthy(&activityError)) {
            return setError(
                error,
                adoptionFailure(
                    QStringLiteral(
                        "The promoted account activity ledger is unhealthy; adoption evidence was preserved."),
                    activityError));
        }
    }

    // The journal remains LegacyQuarantined until this durable commit.  Any
    // failure above or here leaves the account profile and rollback backup in
    // place so a later repair can inspect them without resurrecting private
    // pre-account state in legacy storage.
    if (!adoption.commit(error))
        return false;

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
restoreSourceForRetry(
    const ProfilePaths &paths,
    const LegacyPersonalStateStorage &sourceStorage,
    ProfilePaths::Kind sourceKind,
    const PersonalStateSnapshot &snapshot,
    QString *error) {
    QString restoreError;
    if (!restoreLegacyActivityFromBackup(
            paths,
            sourceStorage,
            &restoreError)) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not restore the interrupted adoption activity ledger."),
                restoreError));
    }

    if (!sourceStorage.restorePersonalState(
            snapshot,
            &restoreError)) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not restore the interrupted adoption source."),
                restoreError));
    }

    const auto restored =
        sourceStorage.capture(&restoreError);
    if (!restored.has_value()
        || !restored->matchesSemanticDigest(
               snapshot.semanticDigest())) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "Could not verify the restored interrupted adoption source."),
                restoreError));
    }

    bool reopened = false;
    if (sourceKind == ProfilePaths::Kind::LocalOnly) {
        reopened = m_profileRuntime->activateLocalOnlyProfile(
            &restoreError);
    } else if (sourceKind == ProfilePaths::Kind::LegacyLocal) {
        reopened = m_profileRuntime->reloadLegacyProfile(
            &restoreError);
    }
    if (!reopened) {
        return setError(
            error,
            adoptionFailure(
                QStringLiteral(
                    "The interrupted adoption source was restored on disk but could not be reopened."),
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
    return copyActivityLedgerToStaging(
        paths,
        m_profileRuntime->legacyStorage(),
        sourceDigest,
        stagedDigest,
        error);
}

bool FirstAccountProfileCoordinator::
copyActivityLedgerToStaging(
    const ProfilePaths &paths,
    const LegacyPersonalStateStorage &sourceStorage,
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
            sourceStorage.activityDbPath(),
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
    return backupActivityLedger(
        paths,
        m_profileRuntime->legacyStorage(),
        expectedDigest,
        backupDigest,
        error);
}

bool FirstAccountProfileCoordinator::
backupActivityLedger(
    const ProfilePaths &paths,
    const LegacyPersonalStateStorage &sourceStorage,
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
            sourceStorage.activityDbPath(),
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
    return restoreLegacyActivityFromBackup(
        paths,
        m_profileRuntime->legacyStorage(),
        error);
}

bool FirstAccountProfileCoordinator::
restoreLegacyActivityFromBackup(
    const ProfilePaths &paths,
    const LegacyPersonalStateStorage &sourceStorage,
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
        sourceStorage.activityDbPath();
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

    QFile::remove(legacyPath + QStringLiteral("-wal"));
    QFile::remove(legacyPath + QStringLiteral("-shm"));

    return true;
}

bool FirstAccountProfileCoordinator::
quarantineLegacyActivityLedger(
    QString *error) const {
    return quarantineLegacyActivityLedger(
        m_profileRuntime->legacyStorage(),
        error);
}

bool FirstAccountProfileCoordinator::
quarantineLegacyActivityLedger(
    const LegacyPersonalStateStorage &sourceStorage,
    QString *error) const {
    const QString legacyPath =
        sourceStorage.activityDbPath();
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
