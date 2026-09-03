#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfilePaths.h"

#include <QString>

#include <optional>

class ProfileAdoption {
public:
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
        State state = State::Preparing;
        QString sourceSemanticDigest;
        QString targetSemanticDigest;
        QString legacyBackupSemanticDigest;
        QString legacyBackupRoot;
        QString stagingRoot;
        QString finalRoot;

        // Activity-ledger digests (CPP-PORT-CONTRACT §17), parallel to the
        // three digests above but tracked separately: the activity.sqlite
        // file rides inside stagingRoot/finalRoot and is promoted by the same
        // directory rename, so these are auxiliary verification facts, not a
        // second state machine. Empty string is the deliberate "no legacy
        // activity ledger to migrate" sentinel on both sides — a fresh
        // installation predating this feature has nothing to digest, and
        // that must compare equal, not fail verification.
        QString activitySourceDigest;
        QString activityTargetDigest;
        QString activityLegacyBackupDigest;
    };

    static std::optional<ProfileAdoption> begin(const ProfilePaths &paths,
                                                const QString &sourceSemanticDigest,
                                                QString *error = nullptr);
    static std::optional<ProfileAdoption> open(const ProfilePaths &paths,
                                               QString *error = nullptr);

    Snapshot snapshot() const;
    State state() const;

    bool markTargetVerified(const QString &targetSemanticDigest,
                            QString *error = nullptr);

    // Parallel activity-ledger verification (CPP-PORT-CONTRACT §17), called
    // alongside markTargetVerified() (either order) while still Preparing —
    // it does NOT itself transition state, only records/persists the two
    // digests. Both empty is valid ("no activity data to migrate"); a
    // non-empty pair must match exactly. This is a separate method rather
    // than extra markTargetVerified() parameters so existing two-argument
    // call sites (targetDigest, error) keep compiling unchanged.
    bool markActivityTargetVerified(const QString &activitySourceDigest,
                                    const QString &activityTargetDigest,
                                    QString *error = nullptr);

    bool promote(QString *error = nullptr);
    bool markLegacyQuarantined(const QString &backupSemanticDigest,
                               QString *error = nullptr);

    // Parallel activity-ledger quarantine verification, called alongside
    // markLegacyQuarantined() while Promoted — same non-breaking-overload
    // reasoning as markActivityTargetVerified() above.
    bool markActivityLegacyQuarantined(const QString &activityLegacyBackupDigest,
                                       QString *error = nullptr);

    bool commit(QString *error = nullptr);

    bool rollbackBeforeLegacyQuarantine(QString *error = nullptr);
    bool rollbackAfterLegacyRestore(
        const QString &restoredSemanticDigest,
        QString *error = nullptr);

    static QString stateName(State state);

private:
    ProfileAdoption(const ProfilePaths &paths, const Snapshot &snapshot);

    static std::optional<Snapshot> readSnapshot(const ProfilePaths &paths,
                                                QString *error);
    bool writeSnapshot(QString *error) const;
    bool reconcileInterruptedPromotion(QString *error);
    bool ensureManagedPath(const QString &path, QString *error) const;
    static bool removeManagedTree(const ProfilePaths &paths,
                                  const QString &path,
                                  QString *error);
    static bool setError(QString *error, const QString &message);

    ProfilePaths m_paths;
    Snapshot m_snapshot;
};
