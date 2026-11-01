#pragma once

#include "AniListHttpTransport.h"
#include "TrackerCredentialVault.h"
#include "account/ProfilePaths.h"

#include <QObject>

#include <functional>
#include <memory>

// AniList tokens last about a year and there is no refresh token. A 401 is
// therefore surfaced as "reauthorization required" and never retried: the
// caller keeps its queued work and asks the user to reconnect.
class AniListApiClient final : public QObject
{
    Q_OBJECT

public:
    using Completion = std::function<void(const AniListApiResult &)>;

    AniListApiClient(const ProfilePaths &profile,
                     const AniListAuthConfiguration &configuration,
                     QObject *parent = nullptr);
    AniListApiClient(const ProfilePaths &profile,
                     const AniListAuthConfiguration &configuration,
                     TrackerCredentialVault *vault,
                     AniListHttpTransport *transport,
                     QObject *parent = nullptr);

    bool available() const;

    void post(const QJsonObject &body, Completion completion);

    // AniList documents no provider-side revocation endpoint for this flow.
    // Disconnect is a local lifecycle operation; the user can separately
    // revoke the app in AniList settings.
    bool disconnectThenForget(const std::function<bool()> &disconnect);

private:
    ProfilePaths m_profile;
    AniListAuthConfiguration m_configuration;
    std::unique_ptr<TrackerCredentialVault> m_defaultVault;
    std::unique_ptr<AniListHttpTransport> m_defaultTransport;
    TrackerCredentialVault *m_vault = nullptr;
    AniListHttpTransport *m_transport = nullptr;
};
