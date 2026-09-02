// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfileAdoption.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
constexpr int kManifestVersion = 1;

QString normalizedDigest(const QString &digest) {
    return digest.trimmed().toLower();
}

QString operationName(ProfileAdoption::Operation operation) {
    switch (operation) {
    case ProfileAdoption::Operation::FirstAccount:
        return QStringLiteral("first_account");
    case ProfileAdoption::Operation::LocalAttachment:
        return QStringLiteral("local_attachment");
    }
    return QString();
}

std::optional<ProfileAdoption::Operation> operationFromName(const QString &name) {
    if (name.isEmpty() || name == QLatin1String("first_account"))
        return ProfileAdoption::Operation::FirstAccount;
    if (name == QLatin1String("local_attachment"))
        return ProfileAdoption::Operation::LocalAttachment;
    return std::nullopt;
}

std::optional<ProfileAdoption::State> stateFromName(const QString &name) {
    if (name == QLatin1String("preparing"))
        return ProfileAdoption::State::Preparing;
    if (name == QLatin1String("target_verified"))
        return ProfileAdoption::State::TargetVerified;
    if (name == QLatin1String("promoted"))
        return ProfileAdoption::State::Promoted;
    if (name == QLatin1String("legacy_quarantined"))
        return ProfileAdoption::State::LegacyQuarantined;
    if (name == QLatin1String("retry_pending"))
        return ProfileAdoption::State::RetryPending;
    if (name == QLatin1String("committed"))
        return ProfileAdoption::State::Committed;
    return std::nullopt;
}
}

std::optional<ProfileAdoption> ProfileAdoption::begin(const ProfilePaths &paths,
                                                      const QString &sourceSemanticDigest,
                                                      QString *error) {
    if (paths.kind() != ProfilePaths::Kind::Account) {
        setError(error, QStringLiteral("Profile adoption requires an account profile."));
        return std::nullopt;
    }

    const QString sourceDigest = normalizedDigest(sourceSemanticDigest);
    if (sourceDigest.isEmpty()) {
        setError(error, QStringLiteral("Profile adoption requires a source semantic digest."));
        return std::nullopt;
    }

    const QString journalPath = paths.adoptionJournalPath();
    const QString stagingRoot = paths.accountStagingRoot();
    const QString finalRoot = paths.profileRoot();
    if (journalPath.isEmpty() || stagingRoot.isEmpty() || finalRoot.isEmpty()) {
        setError(error, QStringLiteral("Profile adoption paths are incomplete."));
        return std::nullopt;
    }

    if (!paths.isManagedProfilePath(stagingRoot) || !paths.isManagedProfilePath(finalRoot)) {
        setError(error, QStringLiteral("Profile adoption resolved outside the managed profile root."));
        return std::nullopt;
    }

    if (QFileInfo::exists(journalPath)) {
        setError(error, QStringLiteral("A profile adoption journal already exists for this account."));
        return std::nullopt;
    }
    if (QFileInfo::exists(stagingRoot)) {
        setError(error, QStringLiteral("A staged account profile already exists."));
        return std::nullopt;
    }
    if (QFileInfo::exists(finalRoot)) {
        setError(error, QStringLiteral("The final account profile already exists."));
        return std::nullopt;
    }

    if (!QDir().mkpath(stagingRoot)) {
        setError(error, QStringLiteral("Could not create the staged account profile."));
        return std::nullopt;
    }

    const QFileInfo journalInfo(journalPath);
    if (!QDir().mkpath(journalInfo.absolutePath())) {
        removeManagedTree(paths, stagingRoot, nullptr);
        setError(error, QStringLiteral("Could not create the profile adoption journal directory."));
        return std::nullopt;
    }

    Snapshot snapshot;
    snapshot.accountId = paths.profileId();
    snapshot.state = State::Preparing;
    snapshot.sourceSemanticDigest = sourceDigest;
    snapshot.stagingRoot = stagingRoot;
    snapshot.finalRoot = finalRoot;
    snapshot.legacyBackupRoot = paths.adoptionBackupRoot();

    ProfileAdoption adoption(
        paths,
        snapshot,
        paths.adoptionJournalPath());
    if (!adoption.writeSnapshot(error)) {
        removeManagedTree(paths, stagingRoot, nullptr);
        QFile::remove(journalPath);
        return std::nullopt;
    }
    return adoption;
}

std::optional<ProfileAdoption> ProfileAdoption::open(const ProfilePaths &paths,
                                                     QString *error) {
    const QString journalPath = paths.adoptionJournalPath();
    const auto snapshot = readSnapshot(
        paths,
        journalPath,
        Operation::FirstAccount,
        error);
    if (!snapshot.has_value())
        return std::nullopt;

    ProfileAdoption adoption(paths, *snapshot, journalPath);
    if (!adoption.reconcileInterruptedPromotion(error))
        return std::nullopt;
    return adoption;
}

std::optional<ProfileAdoption> ProfileAdoption::beginLocalAttachment(
    const ProfilePaths &paths,
    const QString &sourceSemanticDigest,
    const QString &previousTargetSemanticDigest,
    const QString &previousTargetActivityDigest,
    QString *error) {
    if (paths.kind() != ProfilePaths::Kind::Account) {
        setError(error, QStringLiteral("Local profile attachment requires an account profile."));
        return std::nullopt;
    }

    const QString sourceDigest = normalizedDigest(sourceSemanticDigest);
    if (sourceDigest.isEmpty()) {
        setError(error, QStringLiteral("Local profile attachment requires a source semantic digest."));
        return std::nullopt;
    }

    const QString journalPath = paths.localAttachmentJournalPath();
    const QString stagingRoot = paths.accountStagingRoot();
    const QString finalRoot = paths.profileRoot();
    const QString replacementBackupRoot = paths.accountReplacementBackupRoot();
    if (journalPath.isEmpty() || stagingRoot.isEmpty() || finalRoot.isEmpty()
        || replacementBackupRoot.isEmpty()) {
        setError(error, QStringLiteral("Local profile attachment paths are incomplete."));
        return std::nullopt;
    }

    if (!paths.isManagedProfilePath(stagingRoot)
        || !paths.isManagedProfilePath(finalRoot)
        || !paths.isManagedProfilePath(replacementBackupRoot)) {
        setError(error, QStringLiteral("Local profile attachment resolved outside the managed profile root."));
        return std::nullopt;
    }

    if (QFileInfo::exists(journalPath)) {
        setError(error, QStringLiteral("A local profile attachment journal already exists for this account."));
        return std::nullopt;
    }
    if (QFileInfo::exists(stagingRoot) || QFileInfo::exists(replacementBackupRoot)) {
        setError(error, QStringLiteral("A stale local profile attachment staging or rollback tree already exists."));
        return std::nullopt;
    }

    const bool finalExists = QFileInfo::exists(finalRoot);
    const QString previousDigest = normalizedDigest(previousTargetSemanticDigest);
    if (finalExists != !previousDigest.isEmpty()) {
        setError(error, QStringLiteral("The account profile baseline does not match local attachment state."));
        return std::nullopt;
    }

    if (!QDir().mkpath(stagingRoot)) {
        setError(error, QStringLiteral("Could not create the staged account profile attachment."));
        return std::nullopt;
    }

    const QFileInfo journalInfo(journalPath);
    if (!QDir().mkpath(journalInfo.absolutePath())) {
        removeManagedTree(paths, stagingRoot, nullptr);
        setError(error, QStringLiteral("Could not create the local profile attachment journal directory."));
        return std::nullopt;
    }

    Snapshot snapshot;
    snapshot.accountId = paths.profileId();
    snapshot.operation = Operation::LocalAttachment;
    snapshot.state = State::Preparing;
    snapshot.sourceSemanticDigest = sourceDigest;
    snapshot.stagingRoot = stagingRoot;
    snapshot.finalRoot = finalRoot;
    snapshot.legacyBackupRoot = paths.adoptionBackupRoot();
    snapshot.previousTargetSemanticDigest = previousDigest;
    snapshot.previousTargetActivityDigest = normalizedDigest(previousTargetActivityDigest);
    snapshot.replacementBackupRoot = replacementBackupRoot;

    ProfileAdoption attachment(paths, snapshot, journalPath);
    if (!attachment.writeSnapshot(error)) {
        removeManagedTree(paths, stagingRoot, nullptr);
        QFile::remove(journalPath);
        return std::nullopt;
    }
    return attachment;
}

std::optional<ProfileAdoption> ProfileAdoption::openLocalAttachment(
    const ProfilePaths &paths,
    QString *error) {
    const QString journalPath = paths.localAttachmentJournalPath();
    const auto snapshot = readSnapshot(
        paths,
        journalPath,
        Operation::LocalAttachment,
        error);
    if (!snapshot.has_value())
        return std::nullopt;

    ProfileAdoption attachment(paths, *snapshot, journalPath);
    if (!attachment.reconcileInterruptedLocalAttachment(error))
        return std::nullopt;
    return attachment;
}

ProfileAdoption::Snapshot ProfileAdoption::snapshot() const {
    return m_snapshot;
}

ProfileAdoption::Operation ProfileAdoption::operation() const {
    return m_snapshot.operation;
}

ProfileAdoption::State ProfileAdoption::state() const {
    return m_snapshot.state;
}

bool ProfileAdoption::markTargetVerified(const QString &targetSemanticDigest,
                                         QString *error) {
    if (m_snapshot.state != State::Preparing)
        return setError(error, QStringLiteral("Target verification is only valid while preparing adoption."));

    const QString targetDigest = normalizedDigest(targetSemanticDigest);
    if (targetDigest.isEmpty())
        return setError(error, QStringLiteral("Target verification requires a semantic digest."));
    if (m_snapshot.operation == Operation::FirstAccount
        && targetDigest != m_snapshot.sourceSemanticDigest) {
        return setError(error, QStringLiteral("The staged profile does not match the source semantic digest."));
    }
    const QFileInfo stagingInfo(m_snapshot.stagingRoot);
    if (!stagingInfo.exists() || !stagingInfo.isDir())
        return setError(error, QStringLiteral("The staged profile no longer exists."));
    if (stagingInfo.isSymLink())
        return setError(error, QStringLiteral("The staged profile may not be a symbolic link."));

    m_snapshot.targetSemanticDigest = targetDigest;
    m_snapshot.state = State::TargetVerified;
    return writeSnapshot(error);
}

bool ProfileAdoption::markActivityTargetVerified(const QString &activitySourceDigest,
                                                 const QString &activityTargetDigest,
                                                 QString *error) {
    if (m_snapshot.state != State::Preparing
        && m_snapshot.state != State::TargetVerified) {
        return setError(error, QStringLiteral(
            "Activity target verification is only valid while preparing adoption."));
    }

    const QString sourceDigest = normalizedDigest(activitySourceDigest);
    const QString targetDigest = normalizedDigest(activityTargetDigest);

    // First-account adoption is a byte-preserving migration, so source and
    // target must match. Local attachment intentionally produces a merged
    // account ledger and records that merged digest instead.
    if (m_snapshot.operation == Operation::FirstAccount
        && sourceDigest != targetDigest) {
        return setError(error, QStringLiteral(
            "The staged activity ledger does not match the source activity digest."));
    }

    m_snapshot.activitySourceDigest = sourceDigest;
    m_snapshot.activityTargetDigest = targetDigest;
    return writeSnapshot(error);
}

bool ProfileAdoption::promote(QString *error) {
    if (m_snapshot.state != State::TargetVerified)
        return setError(error, QStringLiteral("Only a verified staged profile can be promoted."));
    if (!ensureManagedPath(m_snapshot.stagingRoot, error)
        || !ensureManagedPath(m_snapshot.finalRoot, error)) {
        return false;
    }
    if (!QFileInfo::exists(m_snapshot.stagingRoot))
        return setError(error, QStringLiteral("The verified staged profile no longer exists."));

    if (m_snapshot.operation == Operation::LocalAttachment) {
        if (!ensureManagedPath(m_snapshot.replacementBackupRoot, error))
            return false;
        if (QFileInfo::exists(m_snapshot.replacementBackupRoot))
            return setError(error, QStringLiteral("The account rollback profile already exists."));

        const bool expectedExistingProfile =
            !m_snapshot.previousTargetSemanticDigest.isEmpty();
        if (QFileInfo::exists(m_snapshot.finalRoot) != expectedExistingProfile) {
            return setError(error, QStringLiteral("The account profile changed before local attachment promotion."));
        }

        QDir parent(QFileInfo(m_snapshot.finalRoot).absolutePath());
        const QString finalName = QFileInfo(m_snapshot.finalRoot).fileName();
        const QString stagingName = QFileInfo(m_snapshot.stagingRoot).fileName();
        const QString backupName = QFileInfo(m_snapshot.replacementBackupRoot).fileName();

        if (expectedExistingProfile
            && !parent.rename(finalName, backupName)) {
            return setError(error, QStringLiteral("Could not preserve the previous account profile for rollback."));
        }

        if (!parent.rename(stagingName, finalName)) {
            if (expectedExistingProfile)
                parent.rename(backupName, finalName);
            return setError(error, QStringLiteral("Could not promote the merged account profile."));
        }
    } else {
        if (QFileInfo::exists(m_snapshot.finalRoot))
            return setError(error, QStringLiteral("The final account profile already exists."));

        QDir parent(QFileInfo(m_snapshot.stagingRoot).absolutePath());
        if (!parent.rename(QFileInfo(m_snapshot.stagingRoot).fileName(),
                           QFileInfo(m_snapshot.finalRoot).fileName())) {
            return setError(error, QStringLiteral("Could not promote the verified account profile."));
        }
    }

    m_snapshot.state = State::Promoted;
    return writeSnapshot(error);
}

bool ProfileAdoption::markLegacyQuarantined(const QString &backupSemanticDigest,
                                             QString *error) {
    if (m_snapshot.state != State::Promoted)
        return setError(error, QStringLiteral("Legacy state can only be marked quarantined after profile promotion."));
    if (m_snapshot.legacyBackupRoot.isEmpty() || !QFileInfo::exists(m_snapshot.legacyBackupRoot))
        return setError(error, QStringLiteral("The legacy rollback backup does not exist."));

    const QString backupDigest = normalizedDigest(backupSemanticDigest);
    if (backupDigest.isEmpty())
        return setError(error, QStringLiteral("Legacy quarantine requires a rollback-backup semantic digest."));
    if (backupDigest != m_snapshot.sourceSemanticDigest)
        return setError(error, QStringLiteral("The rollback backup does not match the source semantic digest."));

    const QString adoptionRoot = QFileInfo(m_paths.adoptionJournalPath()).absolutePath();
    const QString backup = QDir::cleanPath(QFileInfo(m_snapshot.legacyBackupRoot).absoluteFilePath());
    const QString backupBase = QDir::cleanPath(adoptionRoot + QLatin1String("/backups"));
    if (!(backup == backupBase || backup.startsWith(backupBase + QLatin1Char('/'))))
        return setError(error, QStringLiteral("The legacy rollback backup is outside the managed adoption directory."));

    m_snapshot.legacyBackupSemanticDigest = backupDigest;
    m_snapshot.state = State::LegacyQuarantined;
    return writeSnapshot(error);
}

bool ProfileAdoption::markActivityLegacyQuarantined(const QString &activityLegacyBackupDigest,
                                                    QString *error) {
    if (m_snapshot.state != State::Promoted) {
        return setError(error, QStringLiteral(
            "Activity legacy state can only be marked quarantined after profile promotion."));
    }

    const QString backupDigest = normalizedDigest(activityLegacyBackupDigest);
    if (backupDigest != m_snapshot.activitySourceDigest) {
        return setError(error, QStringLiteral(
            "The activity rollback backup does not match the source activity digest."));
    }

    m_snapshot.activityLegacyBackupDigest = backupDigest;
    return writeSnapshot(error);
}

bool ProfileAdoption::commit(QString *error) {
    if (m_snapshot.state != State::LegacyQuarantined)
        return setError(error, QStringLiteral("Profile adoption cannot commit before legacy state is quarantined."));
    if (!QFileInfo::exists(m_snapshot.finalRoot))
        return setError(error, QStringLiteral("The promoted account profile no longer exists."));
    if (!QFileInfo::exists(m_snapshot.legacyBackupRoot))
        return setError(error, QStringLiteral("The rollback backup no longer exists."));
    if (m_snapshot.targetSemanticDigest != m_snapshot.sourceSemanticDigest
        || m_snapshot.legacyBackupSemanticDigest != m_snapshot.sourceSemanticDigest) {
        return setError(error, QStringLiteral("Profile adoption semantic verification is incomplete."));
    }
    if (m_snapshot.activityTargetDigest != m_snapshot.activitySourceDigest
        || m_snapshot.activityLegacyBackupDigest != m_snapshot.activitySourceDigest) {
        return setError(error, QStringLiteral("Profile adoption activity-ledger verification is incomplete."));
    }

    m_snapshot.state = State::Committed;
    return writeSnapshot(error);
}

bool ProfileAdoption::commitLocalAttachment(QString *error) {
    if (m_snapshot.operation != Operation::LocalAttachment)
        return setError(error, QStringLiteral("This profile transaction is not a local attachment."));
    if (m_snapshot.state != State::Promoted)
        return setError(error, QStringLiteral("Local profile attachment cannot commit before promotion."));
    if (!QFileInfo::exists(m_snapshot.finalRoot))
        return setError(error, QStringLiteral("The merged account profile no longer exists."));
    if (m_snapshot.targetSemanticDigest.isEmpty())
        return setError(error, QStringLiteral("Local profile attachment semantic verification is incomplete."));

    m_snapshot.state = State::Committed;
    return writeSnapshot(error);
}

bool ProfileAdoption::cleanupLocalAttachment(QString *error) {
    if (m_snapshot.operation != Operation::LocalAttachment)
        return setError(error, QStringLiteral("This profile transaction is not a local attachment."));
    if (m_snapshot.state != State::Committed)
        return setError(error, QStringLiteral("Only a committed local profile attachment can be cleaned up."));

    if (QFileInfo::exists(m_snapshot.replacementBackupRoot)
        && !removeManagedTree(m_paths, m_snapshot.replacementBackupRoot, error)) {
        return false;
    }
    if (QFileInfo::exists(m_snapshot.stagingRoot)
        && !removeManagedTree(m_paths, m_snapshot.stagingRoot, error)) {
        return false;
    }
    if (!QFile::remove(m_journalPath) && QFileInfo::exists(m_journalPath))
        return setError(error, QStringLiteral("Could not remove the committed local profile attachment journal."));
    return true;
}

bool ProfileAdoption::rollbackLocalAttachment(QString *error) {
    if (m_snapshot.operation != Operation::LocalAttachment)
        return setError(error, QStringLiteral("This profile transaction is not a local attachment."));

    if (!ensureManagedPath(m_snapshot.stagingRoot, error)
        || !ensureManagedPath(m_snapshot.finalRoot, error)
        || !ensureManagedPath(m_snapshot.replacementBackupRoot, error)) {
        return false;
    }

    if (QFileInfo::exists(m_snapshot.stagingRoot)
        && !removeManagedTree(m_paths, m_snapshot.stagingRoot, error)) {
        return false;
    }

    const bool hadPreviousProfile = !m_snapshot.previousTargetSemanticDigest.isEmpty();
    const bool backupExists = QFileInfo::exists(m_snapshot.replacementBackupRoot);

    if (backupExists) {
        if (QFileInfo::exists(m_snapshot.finalRoot)
            && !removeManagedTree(m_paths, m_snapshot.finalRoot, error)) {
            return false;
        }

        QDir parent(QFileInfo(m_snapshot.finalRoot).absolutePath());
        if (!parent.rename(
                QFileInfo(m_snapshot.replacementBackupRoot).fileName(),
                QFileInfo(m_snapshot.finalRoot).fileName())) {
            return setError(error, QStringLiteral("Could not restore the previous account profile after attachment failure."));
        }
    } else if (!hadPreviousProfile
               && QFileInfo::exists(m_snapshot.finalRoot)
               && (m_snapshot.state == State::Promoted
                   || m_snapshot.state == State::Committed)) {
        if (!removeManagedTree(m_paths, m_snapshot.finalRoot, error))
            return false;
    } else if (hadPreviousProfile
               && !QFileInfo::exists(m_snapshot.finalRoot)) {
        return setError(error, QStringLiteral("The previous account profile rollback copy is missing."));
    }

    if (!QFile::remove(m_journalPath) && QFileInfo::exists(m_journalPath))
        return setError(error, QStringLiteral("Could not remove the local profile attachment journal during rollback."));
    return true;
}

bool ProfileAdoption::rollbackBeforeLegacyQuarantine(QString *error) {
    if (m_snapshot.state == State::LegacyQuarantined
        || m_snapshot.state == State::Committed) {
        return setError(
            error,
            QStringLiteral(
                "Automatic rollback is not safe after legacy state is quarantined."));
    }

    if (QFileInfo::exists(m_snapshot.stagingRoot)
        && !removeManagedTree(m_paths, m_snapshot.stagingRoot, error)) {
        return false;
    }

    if (QFileInfo::exists(m_snapshot.finalRoot)
        && !removeManagedTree(m_paths, m_snapshot.finalRoot, error)) {
        return false;
    }

    if (!QFile::remove(m_paths.adoptionJournalPath())
        && QFileInfo::exists(m_paths.adoptionJournalPath())) {
        return setError(error, QStringLiteral("Could not remove the profile adoption journal."));
    }
    return true;
}

bool ProfileAdoption::rollbackAfterLegacyRestore(
    const QString &restoredSemanticDigest,
    QString *error) {
    if (m_snapshot.state != State::LegacyQuarantined
        && m_snapshot.state != State::Committed) {
        return setError(
            error,
            QStringLiteral(
                "Post-quarantine rollback requires a quarantined or committed adoption."));
    }

    const QString restoredDigest =
        normalizedDigest(restoredSemanticDigest);
    if (restoredDigest.isEmpty()
        || restoredDigest != m_snapshot.sourceSemanticDigest) {
        return setError(
            error,
            QStringLiteral(
                "Legacy personal state was not restored to the source semantic digest."));
    }

    if (m_snapshot.legacyBackupRoot.isEmpty()
        || !QFileInfo::exists(m_snapshot.legacyBackupRoot)) {
        return setError(
            error,
            QStringLiteral(
                "Post-quarantine rollback requires the verified legacy backup."));
    }

    if (QFileInfo::exists(m_snapshot.stagingRoot)
        && !removeManagedTree(
            m_paths,
            m_snapshot.stagingRoot,
            error)) {
        return false;
    }

    if (QFileInfo::exists(m_snapshot.finalRoot)
        && !removeManagedTree(
            m_paths,
            m_snapshot.finalRoot,
            error)) {
        return false;
    }

    m_snapshot.state = State::RetryPending;
    m_snapshot.targetSemanticDigest.clear();
    m_snapshot.legacyBackupSemanticDigest.clear();
    m_snapshot.activityTargetDigest.clear();
    m_snapshot.activityLegacyBackupDigest.clear();
    return writeSnapshot(error);
}

bool ProfileAdoption::refreshRetrySourceSemanticDigest(
    const QString &semanticDigest,
    QString *error) {
    if (m_snapshot.state != State::RetryPending) {
        return setError(
            error,
            QStringLiteral(
                "Retry source digest can only be refreshed for a pending retry."));
    }

    const QString normalized = normalizedDigest(semanticDigest);
    if (normalized.isEmpty()) {
        return setError(
            error,
            QStringLiteral("Retry source semantic digest is empty."));
    }

    m_snapshot.sourceSemanticDigest = normalized;
    return writeSnapshot(error);
}

QString ProfileAdoption::stateName(State state) {
    switch (state) {
    case State::Preparing:
        return QStringLiteral("preparing");
    case State::TargetVerified:
        return QStringLiteral("target_verified");
    case State::Promoted:
        return QStringLiteral("promoted");
    case State::LegacyQuarantined:
        return QStringLiteral("legacy_quarantined");
    case State::RetryPending:
        return QStringLiteral("retry_pending");
    case State::Committed:
        return QStringLiteral("committed");
    }
    return QString();
}

ProfileAdoption::ProfileAdoption(
    const ProfilePaths &paths,
    const Snapshot &snapshot,
    const QString &journalPath)
    : m_paths(paths),
      m_snapshot(snapshot),
      m_journalPath(journalPath) {}

std::optional<ProfileAdoption::Snapshot> ProfileAdoption::readSnapshot(
    const ProfilePaths &paths,
    const QString &journalPath,
    Operation expectedOperation,
    QString *error) {
    if (paths.kind() != ProfilePaths::Kind::Account) {
        setError(error, QStringLiteral("Profile transaction journal requires an account profile."));
        return std::nullopt;
    }

    QFile file(journalPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Could not open the profile transaction journal."));
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("The profile transaction journal is malformed."));
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toInt() != kManifestVersion) {
        setError(error, QStringLiteral("The profile transaction journal version is unsupported."));
        return std::nullopt;
    }

    const auto parsedOperation = operationFromName(
        object.value(QStringLiteral("operation")).toString());
    const auto parsedState = stateFromName(
        object.value(QStringLiteral("state")).toString());
    if (!parsedOperation.has_value() || *parsedOperation != expectedOperation
        || !parsedState.has_value()) {
        setError(error, QStringLiteral("The profile transaction journal kind or state is invalid."));
        return std::nullopt;
    }

    Snapshot snapshot;
    snapshot.accountId = object.value(QStringLiteral("account_id")).toString();
    snapshot.operation = *parsedOperation;
    snapshot.state = *parsedState;
    snapshot.sourceSemanticDigest = object.value(QStringLiteral("source_semantic_digest")).toString();
    snapshot.targetSemanticDigest = object.value(QStringLiteral("target_semantic_digest")).toString();
    snapshot.legacyBackupSemanticDigest = object.value(QStringLiteral("legacy_backup_semantic_digest")).toString();
    snapshot.legacyBackupRoot = object.value(QStringLiteral("legacy_backup_root")).toString();
    snapshot.stagingRoot = object.value(QStringLiteral("staging_root")).toString();
    snapshot.finalRoot = object.value(QStringLiteral("final_root")).toString();
    snapshot.previousTargetSemanticDigest = object.value(QStringLiteral("previous_target_semantic_digest")).toString();
    snapshot.previousTargetActivityDigest = object.value(QStringLiteral("previous_target_activity_digest")).toString();
    snapshot.replacementBackupRoot = object.value(QStringLiteral("replacement_backup_root")).toString();
    snapshot.activitySourceDigest = object.value(QStringLiteral("activity_source_digest")).toString();
    snapshot.activityTargetDigest = object.value(QStringLiteral("activity_target_digest")).toString();
    snapshot.activityLegacyBackupDigest = object.value(QStringLiteral("activity_legacy_backup_digest")).toString();

    const bool commonMismatch =
        snapshot.accountId != paths.profileId()
        || snapshot.stagingRoot != paths.accountStagingRoot()
        || snapshot.finalRoot != paths.profileRoot()
        || snapshot.sourceSemanticDigest.isEmpty();
    const bool operationMismatch =
        snapshot.operation == Operation::FirstAccount
            ? snapshot.legacyBackupRoot != paths.adoptionBackupRoot()
            : snapshot.replacementBackupRoot != paths.accountReplacementBackupRoot();
    if (commonMismatch || operationMismatch) {
        setError(error, QStringLiteral("The profile transaction journal does not match the requested account profile."));
        return std::nullopt;
    }

    return snapshot;
}

bool ProfileAdoption::writeSnapshot(QString *error) const {
    QJsonObject object;
    object.insert(QStringLiteral("version"), kManifestVersion);
    object.insert(QStringLiteral("operation"), operationName(m_snapshot.operation));
    object.insert(QStringLiteral("account_id"), m_snapshot.accountId);
    object.insert(QStringLiteral("state"), stateName(m_snapshot.state));
    object.insert(QStringLiteral("source_semantic_digest"), m_snapshot.sourceSemanticDigest);
    object.insert(QStringLiteral("target_semantic_digest"), m_snapshot.targetSemanticDigest);
    object.insert(QStringLiteral("legacy_backup_semantic_digest"), m_snapshot.legacyBackupSemanticDigest);
    object.insert(QStringLiteral("legacy_backup_root"), m_snapshot.legacyBackupRoot);
    object.insert(QStringLiteral("staging_root"), m_snapshot.stagingRoot);
    object.insert(QStringLiteral("final_root"), m_snapshot.finalRoot);
    object.insert(QStringLiteral("previous_target_semantic_digest"), m_snapshot.previousTargetSemanticDigest);
    object.insert(QStringLiteral("previous_target_activity_digest"), m_snapshot.previousTargetActivityDigest);
    object.insert(QStringLiteral("replacement_backup_root"), m_snapshot.replacementBackupRoot);
    object.insert(QStringLiteral("activity_source_digest"), m_snapshot.activitySourceDigest);
    object.insert(QStringLiteral("activity_target_digest"), m_snapshot.activityTargetDigest);
    object.insert(QStringLiteral("activity_legacy_backup_digest"), m_snapshot.activityLegacyBackupDigest);

    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);

    QSaveFile file(m_journalPath);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("Could not open the profile transaction journal for writing."));
    if (file.write(payload) != payload.size())
        return setError(error, QStringLiteral("Could not write the profile transaction journal."));
    if (!file.commit())
        return setError(error, QStringLiteral("Could not commit the profile transaction journal."));
    return true;
}

bool ProfileAdoption::reconcileInterruptedPromotion(QString *error) {
    const bool stagingExists = QFileInfo::exists(m_snapshot.stagingRoot);
    const bool finalExists = QFileInfo::exists(m_snapshot.finalRoot);

    if (m_snapshot.state == State::TargetVerified && !stagingExists && finalExists) {
        m_snapshot.state = State::Promoted;
        return writeSnapshot(error);
    }

    if ((m_snapshot.state == State::Preparing
         || m_snapshot.state == State::TargetVerified)
        && !stagingExists
        && !finalExists) {
        return true;
    }

    if ((m_snapshot.state == State::Promoted
         || m_snapshot.state == State::LegacyQuarantined
         || m_snapshot.state == State::Committed)
        && !finalExists) {
        return setError(error, QStringLiteral("The promoted account profile is missing."));
    }

    return true;
}

bool ProfileAdoption::reconcileInterruptedLocalAttachment(QString *error) {
    if (m_snapshot.operation != Operation::LocalAttachment)
        return setError(error, QStringLiteral("The profile transaction is not a local attachment."));

    const bool stagingExists = QFileInfo::exists(m_snapshot.stagingRoot);
    const bool finalExists = QFileInfo::exists(m_snapshot.finalRoot);
    const bool backupExists = QFileInfo::exists(m_snapshot.replacementBackupRoot);
    const bool hadPreviousProfile = !m_snapshot.previousTargetSemanticDigest.isEmpty();

    if (m_snapshot.state == State::Preparing)
        return true;

    if (m_snapshot.state == State::TargetVerified) {
        if (!hadPreviousProfile) {
            if (!stagingExists && finalExists) {
                m_snapshot.state = State::Promoted;
                return writeSnapshot(error);
            }
            return stagingExists && !finalExists
                ? true
                : setError(error, QStringLiteral("The interrupted local attachment target state is inconsistent."));
        }

        if (stagingExists && finalExists && !backupExists)
            return true;

        QDir parent(QFileInfo(m_snapshot.finalRoot).absolutePath());
        const QString finalName = QFileInfo(m_snapshot.finalRoot).fileName();
        const QString stagingName = QFileInfo(m_snapshot.stagingRoot).fileName();
        const QString backupName = QFileInfo(m_snapshot.replacementBackupRoot).fileName();

        if (stagingExists && !finalExists && backupExists) {
            if (!parent.rename(stagingName, finalName)) {
                parent.rename(backupName, finalName);
                return setError(error, QStringLiteral("Could not finish interrupted local attachment promotion."));
            }
            m_snapshot.state = State::Promoted;
            return writeSnapshot(error);
        }

        if (!stagingExists && finalExists && backupExists) {
            m_snapshot.state = State::Promoted;
            return writeSnapshot(error);
        }

        return setError(error, QStringLiteral("The interrupted local attachment rollback state is inconsistent."));
    }

    if ((m_snapshot.state == State::Promoted
         || m_snapshot.state == State::Committed)
        && !finalExists) {
        return setError(error, QStringLiteral("The merged account profile is missing."));
    }

    return true;
}

bool ProfileAdoption::ensureManagedPath(const QString &path, QString *error) const {
    if (!m_paths.isManagedProfilePath(path))
        return setError(error, QStringLiteral("Refusing to modify a path outside the managed profile root."));

    const QFileInfo info(path);
    if (info.exists() && info.isSymLink())
        return setError(error, QStringLiteral("Refusing to modify a symbolic-link profile path."));
    return true;
}

bool ProfileAdoption::removeManagedTree(const ProfilePaths &paths,
                                        const QString &path,
                                        QString *error) {
    if (!paths.isManagedProfilePath(path))
        return setError(error, QStringLiteral("Refusing to remove a path outside the managed profile root."));

    const QFileInfo info(path);
    if (info.exists() && info.isSymLink())
        return setError(error, QStringLiteral("Refusing to remove a symbolic-link profile path."));

    QDir dir(path);
    if (!dir.exists())
        return true;
    if (!dir.removeRecursively())
        return setError(error, QStringLiteral("Could not remove the managed profile tree."));
    return true;
}

bool ProfileAdoption::setError(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
