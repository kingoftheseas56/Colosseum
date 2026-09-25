#pragma once

#include "TrackerTypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

struct TrackerCredentialSlot {
    QString profileId;
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
};

// This type never crosses the tracker native boundary. It is deliberately not
// a QVariant/QObject/QML type and has no serialization outside the platform
// credential vault.
struct TrackerCredential {
    TrackerCredentialSlot slot;
    QByteArray accessToken;
    QByteArray refreshToken;
    qint64 accessTokenExpiresAtMs = 0;
    qint64 refreshTokenExpiresAtMs = 0;
    QStringList grantedScopes;
};

bool trackerCredentialIsReusable(const TrackerCredential &credential, qint64 nowMs);

class TrackerCredentialVault
{
public:
    virtual ~TrackerCredentialVault() = default;

    virtual bool isAvailable() const { return false; }
    virtual bool hasReusableCredential(const TrackerCredentialSlot &slot, qint64 nowMs) const = 0;
    virtual bool saveAndVerify(const TrackerCredential &credential)
    {
        Q_UNUSED(credential);
        return false;
    }
    virtual std::optional<TrackerCredential> loadForProfile(
        const QString &profileId,
        TrackerProviderId providerId) const
    {
        Q_UNUSED(profileId);
        Q_UNUSED(providerId);
        return std::nullopt;
    }
    virtual bool clearForProfile(const QString &profileId, TrackerProviderId providerId)
    {
        Q_UNUSED(profileId);
        Q_UNUSED(providerId);
        return false;
    }
};

// The Windows implementation uses an independent, profile/provider-scoped
// Credential Manager namespace. It cannot read, replace, or enumerate account
// or Stremio credentials.
class WindowsTrackerCredentialVault final : public TrackerCredentialVault
{
public:
    bool isAvailable() const override;
    bool hasReusableCredential(const TrackerCredentialSlot &slot, qint64 nowMs) const override;
    bool saveAndVerify(const TrackerCredential &credential) override;
    std::optional<TrackerCredential> loadForProfile(
        const QString &profileId,
        TrackerProviderId providerId) const override;
    bool clearForProfile(const QString &profileId, TrackerProviderId providerId) override;

private:
    static QString targetName(const QString &profileId, TrackerProviderId providerId);
};
