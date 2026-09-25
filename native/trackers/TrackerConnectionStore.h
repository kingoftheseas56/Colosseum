#pragma once

#include "TrackerTypes.h"

#include "account/ProfilePaths.h"

#include <QString>

#include <optional>

class TrackerConnectionStore final
{
public:
    enum class ExternalAccountClaim {
        Unclaimed,
        Claimed,
        Indeterminate
    };

    explicit TrackerConnectionStore(const ProfilePaths &profile);

    static QString storagePath(const ProfilePaths &profile);

    bool healthy(QString *error = nullptr) const;
    bool refresh(QString *error = nullptr);
    QString persistenceError() const;
    QList<TrackerConnection> connections() const;
    std::optional<TrackerConnection> connection(TrackerProviderId providerId) const;
    quint64 nextConnectionGeneration(TrackerProviderId providerId) const;
    bool upsert(const TrackerConnection &connection, QString *error = nullptr);
    // Persist the destination's transfer reservation before the source is
    // disconnected. The shared claims lock makes this an exclusive handoff.
    bool beginTransferFrom(const ProfilePaths &sourceProfile,
                           const TrackerConnection &transferPending,
                           QString *error = nullptr);
    bool setDisconnected(TrackerProviderId providerId,
                        const QString &expectedRemoteAccountId,
                        quint64 expectedGeneration,
                        QString *error = nullptr);
    bool restoreConnected(TrackerProviderId providerId,
                          const QString &expectedRemoteAccountId,
                          quint64 expectedGeneration,
                          QString *error = nullptr);
    ExternalAccountClaim externalAccountClaim(
        TrackerProviderId providerId,
        const QString &remoteAccountId,
        QString *claimingProfileId = nullptr,
        QString *error = nullptr) const;

private:
    bool load();
    bool refreshFromDisk(QString *error);
    bool persist(const QList<TrackerConnection> &candidate, QString *error) const;
    ExternalAccountClaim externalAccountClaimUnlocked(
        TrackerProviderId providerId,
        const QString &remoteAccountId,
        QString *claimingProfileId,
        QString *error) const;

    ProfilePaths m_profile;
    QString m_path;
    QList<TrackerConnection> m_connections;
    bool m_healthy = true;
    QString m_persistenceError;
};
