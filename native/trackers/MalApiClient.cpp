#include "MalApiClient.h"

#include <QDateTime>
#include <QPointer>

#include <utility>

namespace {

constexpr qint64 kExpirySkewMs = 30 * 1000;
constexpr qint64 kConservativeRefreshLifetimeMs =
    31LL * 24 * 60 * 60 * 1000;

MalApiResult authFailure(MalTransportError error)
{
    MalApiResult result;
    result.error = error;
    result.statusCode =
        error == MalTransportError::AuthenticationRequired ? 401 : 0;
    return result;
}

} // namespace

MalApiClient::MalApiClient(
    const ProfilePaths &profile,
    const MalAuthConfiguration &configuration,
    QObject *parent)
    : MalApiClient(profile, configuration, nullptr, nullptr, parent)
{}

MalApiClient::MalApiClient(
    const ProfilePaths &profile,
    const MalAuthConfiguration &configuration,
    TrackerCredentialVault *vault,
    MalHttpTransport *transport,
    QObject *parent)
    : QObject(parent),
      m_profile(profile),
      m_configuration(configuration),
      m_defaultVault(vault ? nullptr
                           : std::make_unique<WindowsTrackerCredentialVault>()),
      m_defaultTransport(transport ? nullptr
                                   : std::make_unique<MalHttpTransport>()),
      m_vault(vault ? vault : m_defaultVault.get()),
      m_transport(transport ? transport : m_defaultTransport.get())
{
    setObjectName(QStringLiteral("malApiClient"));
}

bool MalApiClient::available() const
{
    return malConfigurationIsValid(m_configuration)
        && m_vault && m_vault->isAvailable()
        && m_transport && !m_profile.profileId().isEmpty();
}

std::optional<TrackerCredential> MalApiClient::rotatedCredential(
    const TrackerCredential &prior,
    const MalTokenResult &token,
    qint64 nowMs)
{
    if (prior.slot.providerId != TrackerProviderId::Mal
        || prior.slot.profileId.isEmpty()
        || prior.slot.remoteAccountId.isEmpty()
        || !token.succeeded() || nowMs <= 0) {
        return std::nullopt;
    }
    TrackerCredential rotated = prior;
    rotated.accessToken = token.accessToken;
    rotated.refreshToken = token.refreshToken;
    rotated.accessTokenExpiresAtMs = nowMs + token.accessExpiresInMs;
    rotated.refreshTokenExpiresAtMs = qMax(
        nowMs + kConservativeRefreshLifetimeMs,
        rotated.accessTokenExpiresAtMs + 60LL * 60 * 1000);
    rotated.grantedScopes = {QStringLiteral("mal:api")};
    if (rotated.refreshTokenExpiresAtMs
        <= rotated.accessTokenExpiresAtMs) {
        return std::nullopt;
    }
    return rotated;
}

bool MalApiClient::disconnectThenForget(
    const std::function<bool()> &disconnect)
{
    return disconnect ? disconnect() : false;
}

void MalApiClient::get(const QString &path,
                       const QUrlQuery &query,
                       Completion completion)
{
    getAttempt(path, query, false, std::move(completion));
}

void MalApiClient::patchForm(const QString &path,
                           const QUrlQuery &form,
                           Completion completion)
{
    putAttempt(path, form, false, std::move(completion));
}

void MalApiClient::withAccessToken(bool forceRefresh,
                                   AccessCompletion completion)
{
    if (!available()) {
        completion(std::nullopt, MalTransportError::ProtocolFailure);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto current = m_vault->loadForProfile(
        m_profile.profileId(), TrackerProviderId::Mal);
    if (!current
        || current->slot.providerId != TrackerProviderId::Mal
        || current->slot.profileId != m_profile.profileId()) {
        completion(std::nullopt,
                   MalTransportError::AuthenticationRequired);
        return;
    }
    if (!forceRefresh
        && current->accessTokenExpiresAtMs > now + kExpirySkewMs
        && !current->accessToken.isEmpty()) {
        completion(current->accessToken, MalTransportError::None);
        return;
    }
    if (current->refreshToken.isEmpty()
        || current->refreshTokenExpiresAtMs <= now) {
        completion(std::nullopt,
                   MalTransportError::AuthenticationRequired);
        return;
    }

    m_refreshWaiters.append(std::move(completion));
    if (m_refreshInFlight)
        return;
    m_refreshInFlight = true;

    const TrackerCredential prior = *current;
    QPointer<MalApiClient> self(this);
    m_transport->refreshToken(
        m_configuration, prior.refreshToken,
        [self, prior](const MalTokenResult &result) {
            if (self)
                self->finishRefresh(result, prior);
        });
}

void MalApiClient::finishRefresh(
    const MalTokenResult &result,
    const TrackerCredential &prior)
{
    const QList<AccessCompletion> waiters =
        std::exchange(m_refreshWaiters, {});
    m_refreshInFlight = false;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto rotated = rotatedCredential(prior, result, now);
    if (!rotated || !m_vault->saveAndVerify(*rotated)) {
        const MalTransportError error =
            result.error == MalTransportError::None
                ? MalTransportError::ProtocolFailure
                : result.error;
        for (const AccessCompletion &waiter : waiters) {
            waiter(std::nullopt, error);
        }
        return;
    }
    for (const AccessCompletion &waiter : waiters)
        waiter(rotated->accessToken, MalTransportError::None);
}

void MalApiClient::getAttempt(
    const QString &path,
    const QUrlQuery &query,
    bool retried,
    Completion completion)
{
    QPointer<MalApiClient> self(this);
    withAccessToken(retried,
        [self, path, query, retried,
         completion = std::move(completion)](
            std::optional<QByteArray> token,
            MalTransportError error) mutable {
        if (!self) return;
        if (!token) {
            completion(authFailure(error));
            return;
        }
        self->m_transport->get(
            self->m_configuration, *token, path, query,
            [self, path, query, retried,
             completion = std::move(completion)](
                const MalApiResult &result) mutable {
            if (!self) return;
            if (!retried
                && result.error
                    == MalTransportError::AuthenticationRequired) {
                self->getAttempt(path, query, true,
                                 std::move(completion));
                return;
            }
            completion(result);
        });
    });
}

void MalApiClient::putAttempt(
    const QString &path,
    const QUrlQuery &form,
    bool retried,
    Completion completion)
{
    QPointer<MalApiClient> self(this);
    withAccessToken(retried,
        [self, path, form, retried,
         completion = std::move(completion)](
            std::optional<QByteArray> token,
            MalTransportError error) mutable {
        if (!self) return;
        if (!token) {
            completion(authFailure(error));
            return;
        }
        self->m_transport->patchForm(
            self->m_configuration, *token, path, form,
            [self, path, form, retried,
             completion = std::move(completion)](
                const MalApiResult &result) mutable {
            if (!self) return;
            if (!retried
                && result.error
                    == MalTransportError::AuthenticationRequired) {
                self->putAttempt(path, form, true,
                                 std::move(completion));
                return;
            }
            completion(result);
        });
    });
}
