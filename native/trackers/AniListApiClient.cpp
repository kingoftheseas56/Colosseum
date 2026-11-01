#include "AniListApiClient.h"

#include <QDateTime>
#include <QPointer>

#include <utility>

namespace {

AniListApiResult authFailure(AniListTransportError error)
{
    AniListApiResult result;
    result.error = error;
    result.statusCode =
        error == AniListTransportError::AuthenticationRequired ? 401 : 0;
    return result;
}

} // namespace

AniListApiClient::AniListApiClient(
    const ProfilePaths &profile,
    const AniListAuthConfiguration &configuration,
    QObject *parent)
    : AniListApiClient(profile, configuration, nullptr, nullptr, parent)
{}

AniListApiClient::AniListApiClient(
    const ProfilePaths &profile,
    const AniListAuthConfiguration &configuration,
    TrackerCredentialVault *vault,
    AniListHttpTransport *transport,
    QObject *parent)
    : QObject(parent),
      m_profile(profile),
      m_configuration(configuration),
      m_defaultVault(vault ? nullptr
                           : std::make_unique<WindowsTrackerCredentialVault>()),
      m_defaultTransport(transport ? nullptr
                                   : std::make_unique<AniListHttpTransport>()),
      m_vault(vault ? vault : m_defaultVault.get()),
      m_transport(transport ? transport : m_defaultTransport.get())
{
    setObjectName(QStringLiteral("aniListApiClient"));
}

bool AniListApiClient::available() const
{
    return aniListConfigurationIsValid(m_configuration)
        && m_vault && m_vault->isAvailable()
        && m_transport && !m_profile.profileId().isEmpty();
}

bool AniListApiClient::disconnectThenForget(
    const std::function<bool()> &disconnect)
{
    return disconnect ? disconnect() : false;
}

void AniListApiClient::post(const QJsonObject &body, Completion completion)
{
    if (!available()) {
        completion(authFailure(AniListTransportError::ProtocolFailure));
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto current = m_vault->loadForProfile(
        m_profile.profileId(), TrackerProviderId::AniList);
    if (!current
        || current->slot.providerId != TrackerProviderId::AniList
        || current->slot.profileId != m_profile.profileId()
        || current->accessToken.isEmpty()
        || (current->accessTokenExpiresAtMs > 0
            && current->accessTokenExpiresAtMs <= now)) {
        // Missing, empty, or expired: reauthorization, with no network call.
        completion(authFailure(AniListTransportError::AuthenticationRequired));
        return;
    }

    QPointer<AniListApiClient> self(this);
    m_transport->post(
        m_configuration, current->accessToken, body,
        [self, completion = std::move(completion)](
            const AniListApiResult &result) {
            if (!self)
                return;
            // A 401 is final: there is no refresh token to retry with.
            completion(result);
        });
}
