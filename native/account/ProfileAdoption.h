#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfilePaths.h"

#include <QString>

#include <optional>

class ProfileAdoption {
public:
    enum class Operation {
        FirstAccount,
        LocalAttachment
    };

    enum class State {
        Preparing,
        TargetVerified,
        Promoted,
        LegacyQuarantined,
        RetryPending,
        Committed
    };

    struct Snapshot {
        QString accountId;
        Operation operation = Operation::FirstAccount;
        State state = State::Preparing;
        QString sourceSemanticDigest;
        QString targetSemanticDigest;
        QString legacyBackupSemanticDigest;
        QString legacyBackupRoot;
        QString stagingRoot;
        QString finalRoot;

        // Existing-account attachment keeps the previous account profile in a
        // same-parent rollback directory until the verified merged profile is
        // committed. The local source remains untouched throughout.
        QString previousTargetSemanticDigest;
        QString previousTargetActivityDigest;
        QString replacementBackupRoot;

        // Activity-ledger digests (CPP-PORT-CONTRACT §17), parallel to the
        // personal-state digests. First-account adoption requires source and
        // target to match exactly because it is a byte-preserving migration.
        // Local attachment records a merged target digest instead.
        QString activitySourceDigest;
        QString activityTargetDigest;
        QString activityLegacyBackupDigest;
    };

    static std::optional<ProfileAdoption> begin(const ProfilePaths &paths,
                                                const QString &sourceSemanticDigest,
                                                QString *error = nullptr);
    static std::optional<ProfileAdoption> open(const ProfilePaths &paths,
                                               QString *error = nullptr);

    static std::optional<ProfileAdoption> beginLocalAttachment(
        const ProfilePaths &paths,
        const QString &sourceSemanticDigest,
        const QString &previousTargetSemanticDigest,
        const QString &previousTargetActivityDigest,
        QString *error = nullptr);
    static std::optional<ProfileAdoption> openLocalAttachment(
        const ProfilePaths &paths,
        QString *error = nullptr);

    Snapshot snapshot() const;
    Operation operation() const;
    State state() const;

    bool markTargetVerified(const QString &targetSemanticDigest,
                            QString *error = nullptr);

    bool markActivityTargetVerified(const QString &activitySourceDigest,
                                    const QString &activityTargetDigest,
                                    QString *error = nullptr);

    bool promote(QString *error = nullptr);
    bool markLegacyQuarantined(const QString &backupSemanticDigest,
                               QString *error = nullptr);
    bool markActivityLegacyQuarantined(const QString &activityLegacyBackupDigest,
                                       QString *error = nullptr);

    bool commit(QString *error = nullptr);
    bool commitLocalAttachment(QString *error = nullptr);
    bool cleanupLocalAttachment(QString *error = nullptr);

    bool rollbackBeforeLegacyQuarantine(QString *error = nullptr);
    bool rollbackAfterLegacyRestore(
        const QString &restoredSemanticDigest,
        QString *error = nullptr);
    bool rollbackLocalAttachment(QString *error = nullptr);

    static QString stateName(State state);

private:
    ProfileAdoption(const ProfilePaths &paths,
                    const Snapshot &snapshot,
                    const QString &journalPath);

    static std::optional<Snapshot> readSnapshot(
        const ProfilePaths &paths,
        const QString &journalPath,
        Operation expectedOperation,
        QString *error);
    bool writeSnapshot(QString *error) const;
    bool reconcileInterruptedPromotion(QString *error);
    bool reconcileInterruptedLocalAttachment(QString *error);
    bool ensureManagedPath(const QString &path, QString *error) const;
    static bool removeManagedTree(const ProfilePaths &paths,
                                  const QString &path,
                                  QString *error);
    static bool setError(QString *error, const QString &message);

    ProfilePaths m_paths;
    Snapshot m_snapshot;
    QString m_journalPath;
};
