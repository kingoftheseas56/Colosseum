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
    bool promote(QString *error = nullptr);
    bool markLegacyQuarantined(const QString &backupSemanticDigest,
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
