#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "LegacyPersonalStateStorage.h"
#include "ProfileAdoption.h"
#include "ProfilePaths.h"

#include <QSet>
#include <QString>

#include <optional>

class ProfileStoreRuntime;

class FirstAccountProfileCoordinator final {
public:
    explicit FirstAccountProfileCoordinator(
        ProfileStoreRuntime *profileRuntime,
        const QString &appDataRoot = QString());

    bool prepareCreatedAccount(
        const QString &accountId,
        QString *error = nullptr);

    bool prepareAccountSession(
        const QString &accountId,
        QString *error = nullptr);

    bool prepareRememberedAccount(
        const QString &accountId,
        QString *error = nullptr);

    bool prepareLocalOnly(
        QString *error = nullptr);

private:
    bool runFreshAdoption(
        const ProfilePaths &paths,
        QString *error);

    bool runLocalOnlyAdoption(
        const ProfilePaths &paths,
        const LegacyPersonalStateStorage &sourceStorage,
        QString *error);

    bool mergeExistingAccount(
        const ProfilePaths &paths,
        const LegacyPersonalStateStorage &sourceStorage,
        QString *error);

    std::optional<LegacyPersonalStateStorage> currentMigrationSource(
        bool *explicitProfile,
        QString *error) const;

    bool mergeResidualLocalOnlyState(
        const ProfilePaths &paths,
        QString *error);

    bool clearMigrationSource(
        const LegacyPersonalStateStorage &sourceStorage,
        bool explicitProfile,
        QString *error);

    bool resumeAdoption(
        const ProfilePaths &paths,
        ProfileAdoption adoption,
        QString *error);

    bool finishPromotedAdoption(
        const ProfilePaths &paths,
        ProfileAdoption adoption,
        const PersonalStateSnapshot &source,
        const QString &activitySourceDigest,
        QString *error);

    // Activity-ledger adoption (CPP-PORT-CONTRACT §17) — the ledger rides
    // inside the same staging/promote/backup machinery as personal state
    // (its file lives inside accountStagingRoot()/profileRoot(), so
    // ProfileAdoption::promote()'s directory rename already moves it
    // atomically); these helpers only add capture/copy/digest-verify/
    // quarantine steps around that shared machinery. See their .cpp comments
    // for the single-threaded-synchronous-flow safety reasoning behind using
    // a WAL checkpoint + plain file copy instead of a full backup API.
    QString captureLegacyActivityDigest(
        QString *error) const;

    bool copyActivityLedgerToStaging(
        const ProfilePaths &paths,
        const QString &sourceDigest,
        QString *stagedDigest,
        QString *error) const;

    bool copyActivityLedgerToStaging(
        const ProfilePaths &paths,
        const LegacyPersonalStateStorage &sourceStorage,
        const QString &sourceDigest,
        QString *stagedDigest,
        QString *error) const;

    bool verifyActivityDigest(
        const ProfilePaths &paths,
        const QString &profileRoot,
        const QString &expectedDigest,
        QString *actualDigest,
        QString *error) const;

    bool backupActivityLedger(
        const ProfilePaths &paths,
        const QString &expectedDigest,
        QString *backupDigest,
        QString *error) const;

    bool backupActivityLedger(
        const ProfilePaths &paths,
        const LegacyPersonalStateStorage &sourceStorage,
        const QString &expectedDigest,
        QString *backupDigest,
        QString *error) const;

    bool restoreLegacyActivityFromBackup(
        const ProfilePaths &paths,
        QString *error) const;

    bool quarantineLegacyActivityLedger(
        QString *error) const;

    static QString activityBackupFilePath(
        const ProfilePaths &paths);

    bool verifyRestartAndCommit(
        const ProfilePaths &paths,
        ProfileAdoption adoption,
        QString *error);

    bool createEmptyProfile(
        const ProfilePaths &paths,
        QString *error);

    bool verifyProfile(
        const ProfilePaths &paths,
        const QString &profileRoot,
        const PersonalStateSnapshot &expected,
        QString *semanticDigest,
        QString *error) const;

    bool writeBackup(
        const ProfilePaths &paths,
        const PersonalStateSnapshot &snapshot,
        QString *error) const;

    std::optional<PersonalStateSnapshot> readBackup(
        const ProfilePaths &paths,
        QString *error) const;

    bool restoreLegacyForRetry(
        const PersonalStateSnapshot &snapshot,
        QString *error);

    bool restoreLegacyAndRollback(
        const ProfilePaths &paths,
        ProfileAdoption *adoption,
        const PersonalStateSnapshot &snapshot,
        QString *error);

    bool activate(
        const ProfilePaths &paths,
        QString *error);

    std::optional<bool> legacyPersonalStateClaimed(
        QString *error) const;

    static QString backupFilePath(
        const ProfilePaths &paths);
    static QString creationIntentPath(
        const ProfilePaths &paths);

    static bool writeCreationIntent(
        const ProfilePaths &paths,
        QString *error);
    static bool clearCreationIntent(
        const ProfilePaths &paths,
        QString *error = nullptr);
    static bool hasCreationIntent(
        const ProfilePaths &paths);

    static bool setError(
        QString *error,
        const QString &message);

    ProfileStoreRuntime *m_profileRuntime = nullptr;
    QString m_appDataRoot;
    QSet<QString> m_quarantinedThisProcess;
};
