#include "TraktAuth.h"

#include "TrackerConnectionStore.h"

#include <limits>
#include <QUuid>

TrackerProviderCapabilities traktCapabilities()
{
    return TrackerProviderCapability::ReadHistory
        | TrackerProviderCapability::ReadProgress
        | TrackerProviderCapability::WriteProgress
        | TrackerProviderCapability::WriteCompletion
        | TrackerProviderCapability::Scrobble;
}

TraktAuthError traktAuthErrorForTransport(TraktTransportError error)
{
    switch (error) {
    case TraktTransportError::None: return TraktAuthError::None;
    case TraktTransportError::AccessDenied: return TraktAuthError::AccessDenied;
    case TraktTransportError::Expired: return TraktAuthError::Expired;
    case TraktTransportError::RateLimited:
    case TraktTransportError::SlowDown: return TraktAuthError::RateLimited;
    case TraktTransportError::NetworkFailure: return TraktAuthError::Transport;
    default: return TraktAuthError::TokenRejected;
    }
}

TraktAuthSession::TraktAuthSession(TraktAuthBinding binding, TraktAuthConfiguration configuration,
                                   TrackerCredentialVault *vault, TraktAuthTransport *transport,
                                   TrackerClock *clock, TrackerConnectionStore *connections)
    : m_binding(std::move(binding)), m_configuration(std::move(configuration)),
      m_vault(vault), m_transport(transport), m_clock(clock), m_connections(connections),
      m_callbackLifetime(std::make_shared<CallbackLifetime>())
{
}

TraktAuthSession::~TraktAuthSession()
{
    std::lock_guard<std::recursive_mutex> lock(m_callbackLifetime->mutex);
    m_callbackLifetime->alive = false;
    clearTransient();
}

bool traktConfigurationIsValid(const TraktAuthConfiguration &configuration)
{
    const QUrl &url = configuration.brokerBaseUrl;
    return !configuration.clientId.trimmed().isEmpty()
        && !configuration.clientId.contains(QLatin1Char('\r'))
        && !configuration.clientId.contains(QLatin1Char('\n'))
        && url.isValid() && url.scheme() == QLatin1String("https")
        && !url.host().isEmpty() && url.userInfo().isEmpty()
        && !url.hasQuery() && !url.hasFragment();
}

bool traktAccountUuidIsCanonical(const QString &accountId)
{
    const QUuid parsed(accountId);
    return !parsed.isNull() && accountId == parsed.toString(QUuid::WithoutBraces).toLower();
}

bool TraktAuthSession::configurationValid() const
{
    return m_vault && m_vault->isAvailable() && m_transport && m_clock
        && !m_binding.profileId.isEmpty() && traktConfigurationIsValid(m_configuration);
}

bool TraktAuthSession::begin()
{
    if (m_snapshot.phase == TraktAuthPhase::RequestingCode
        || m_snapshot.phase == TraktAuthPhase::AwaitingApproval
        || m_snapshot.phase == TraktAuthPhase::ExchangingToken
        || m_snapshot.phase == TraktAuthPhase::VerifyingIdentity) {
        return false;
    }
    if (!configurationValid()) { attention(TraktAuthError::Configuration); return false; }
    clearTransient();
    ++m_generation;
    m_snapshot = {};
    m_snapshot.phase = TraktAuthPhase::RequestingCode;
    const quint64 generation = m_generation;
    const auto life = m_callbackLifetime;
    m_transport->requestDeviceCode(m_configuration, [this, life, generation](const TraktDeviceCodeResponse &r) {
        std::lock_guard<std::recursive_mutex> lock(life->mutex);
        if (life->alive) completeDevice(generation, r);
    });
    return true;
}

void TraktAuthSession::completeDevice(quint64 generation, const TraktDeviceCodeResponse &response)
{
    if (generation != m_generation || m_snapshot.phase != TraktAuthPhase::RequestingCode) return;
    if (response.error != TraktTransportError::None || response.deviceCode.isEmpty()
        || response.userCode.trimmed().isEmpty() || !response.verificationUrl.isValid()
        || response.verificationUrl.scheme() != QLatin1String("https")
        || (response.verificationUrl.host() != QLatin1String("trakt.tv")
            && response.verificationUrl.host() != QLatin1String("auth.trakt.tv"))
        || !response.verificationUrl.userInfo().isEmpty()
        || response.expiresInMs <= 0 || response.expiresInMs > 24LL * 60 * 60 * 1000
        || response.pollIntervalMs <= 0 || response.pollIntervalMs > response.expiresInMs
        || m_clock->nowMs() > std::numeric_limits<qint64>::max() - response.expiresInMs) {
        attention(traktAuthErrorForTransport(response.error));
        return;
    }
    m_deviceCode = response.deviceCode;
    m_pollIntervalMs = response.pollIntervalMs;
    m_snapshot.phase = TraktAuthPhase::AwaitingApproval;
    m_snapshot.error = TraktAuthError::None;
    m_snapshot.userCode = response.userCode.trimmed();
    m_snapshot.verificationUrl = response.verificationUrl;
    m_snapshot.expiresAtMs = m_clock->nowMs() + response.expiresInMs;
    m_snapshot.nextPollAtMs = m_clock->nowMs() + m_pollIntervalMs;
}

bool TraktAuthSession::poll()
{
    expireIfDue();
    if (m_snapshot.phase != TraktAuthPhase::AwaitingApproval || m_deviceCode.isEmpty()) return false;
    const qint64 now = m_clock->nowMs();
    if (now < m_snapshot.nextPollAtMs) return false;
    m_snapshot.phase = TraktAuthPhase::ExchangingToken;
    m_snapshot.nextPollAtMs = now + m_pollIntervalMs;
    const quint64 generation = m_generation;
    const auto life = m_callbackLifetime;
    const QByteArray code = m_deviceCode;
    m_transport->pollDeviceToken(m_configuration, code, [this, life, generation](const TraktTokenResponse &r) {
        std::lock_guard<std::recursive_mutex> lock(life->mutex);
        if (life->alive) completeToken(generation, r);
    });
    return true;
}

void TraktAuthSession::completeToken(quint64 generation, const TraktTokenResponse &response)
{
    expireIfDue();
    if (generation != m_generation || m_snapshot.phase != TraktAuthPhase::ExchangingToken) return;
    if (response.error == TraktTransportError::AuthorizationPending) {
        m_snapshot.phase = TraktAuthPhase::AwaitingApproval;
        return;
    }
    if (response.error == TraktTransportError::SlowDown || response.error == TraktTransportError::RateLimited) {
        m_pollIntervalMs = qMin(m_pollIntervalMs + 5000, 24LL * 60 * 60 * 1000);
        m_snapshot.nextPollAtMs = m_clock->nowMs() + m_pollIntervalMs;
        m_snapshot.phase = TraktAuthPhase::AwaitingApproval;
        return;
    }
    if (response.error != TraktTransportError::None || response.accessToken.isEmpty()
        || response.refreshToken.isEmpty() || response.accessExpiresInMs <= 0) {
        attention(traktAuthErrorForTransport(response.error));
        return;
    }
    m_snapshot.phase = TraktAuthPhase::VerifyingIdentity;
    const auto life = m_callbackLifetime;
    m_transport->fetchStableAccountId(m_configuration, response.accessToken,
        [this, life, generation, response](const TraktIdentityResponse &identity) {
            std::lock_guard<std::recursive_mutex> lock(life->mutex);
            if (life->alive) completeIdentity(generation, response, identity);
        });
}

void TraktAuthSession::completeIdentity(quint64 generation, const TraktTokenResponse &token,
                                        const TraktIdentityResponse &response)
{
    expireIfDue();
    if (generation != m_generation || m_snapshot.phase != TraktAuthPhase::VerifyingIdentity) return;
    if (response.error != TraktTransportError::None || !traktAccountUuidIsCanonical(response.remoteAccountId)) {
        attention(!traktAccountUuidIsCanonical(response.remoteAccountId) ? TraktAuthError::MissingStableAccount
                                                     : traktAuthErrorForTransport(response.error));
        return;
    }
    if (m_connections) {
        const auto existing = m_connections->connection(TrackerProviderId::Trakt);
        if (existing && existing->state != TrackerConnectionState::Disconnected
            && existing->remoteAccountId != response.remoteAccountId) {
            attention(TraktAuthError::AccountChangeRequired);
            return;
        }
    }
    const qint64 createdAt = token.createdAtMs > 0 ? token.createdAtMs : m_clock->nowMs();
    if (createdAt < 0 || token.accessExpiresInMs > std::numeric_limits<qint64>::max() - createdAt
        || createdAt + token.accessExpiresInMs <= m_clock->nowMs()) {
        attention(TraktAuthError::TokenRejected); return;
    }
    TrackerCredential credential;
    credential.slot = {m_binding.profileId, TrackerProviderId::Trakt, response.remoteAccountId};
    credential.accessToken = token.accessToken;
    credential.refreshToken = token.refreshToken;
    credential.accessTokenExpiresAtMs = createdAt + token.accessExpiresInMs;
    credential.refreshTokenExpiresAtMs = std::numeric_limits<qint64>::max();
    credential.grantedScopes = {};
    if (!m_vault->saveAndVerify(credential)) {
        attention(TraktAuthError::CredentialStore);
        return;
    }
    const auto verified = m_vault->loadForProfile(m_binding.profileId, TrackerProviderId::Trakt);
    if (!verified || verified->slot.profileId != credential.slot.profileId
        || verified->slot.providerId != TrackerProviderId::Trakt
        || verified->slot.remoteAccountId != credential.slot.remoteAccountId
        || verified->accessToken != credential.accessToken || verified->refreshToken != credential.refreshToken
        || verified->accessTokenExpiresAtMs != credential.accessTokenExpiresAtMs
        || verified->refreshTokenExpiresAtMs != credential.refreshTokenExpiresAtMs
        || verified->grantedScopes != credential.grantedScopes) {
        attention(TraktAuthError::CredentialStore); return;
    }
    m_snapshot.phase = TraktAuthPhase::CredentialReady;
    m_snapshot.error = TraktAuthError::None;
    m_snapshot.remoteAccountId = response.remoteAccountId;
    clearTransient();
}

void TraktAuthSession::cancel()
{
    ++m_generation;
    clearTransient();
    m_snapshot = {};
    m_snapshot.phase = TraktAuthPhase::Cancelled;
}

void TraktAuthSession::expireIfDue()
{
    if ((m_snapshot.phase == TraktAuthPhase::AwaitingApproval || m_snapshot.phase == TraktAuthPhase::ExchangingToken
         || m_snapshot.phase == TraktAuthPhase::VerifyingIdentity)
        && m_snapshot.expiresAtMs > 0 && m_clock->nowMs() >= m_snapshot.expiresAtMs) {
        ++m_generation;
        clearTransient();
        m_snapshot.phase = TraktAuthPhase::TimedOut;
        m_snapshot.error = TraktAuthError::Expired;
    }
}

void TraktAuthSession::invalidateProfile(TraktAuthBinding replacement)
{
    ++m_generation;
    clearTransient();
    m_binding = std::move(replacement);
    m_snapshot = {};
}

TraktAuthSnapshot TraktAuthSession::snapshot() const
{
    return m_snapshot;
}

void TraktAuthSession::attention(TraktAuthError error)
{
    clearTransient();
    m_snapshot.phase = TraktAuthPhase::NeedsAttention;
    m_snapshot.error = error == TraktAuthError::None ? TraktAuthError::TokenRejected : error;
}

void TraktAuthSession::clearTransient()
{
    m_deviceCode.fill('\0');
    m_deviceCode.clear();
}

std::optional<TraktAuthConfiguration> traktProductionConfiguration()
{
#if defined(COLOSSEUM_TRAKT_CLIENT_ID) && defined(COLOSSEUM_TRAKT_TOKEN_BROKER_URL)
    TraktAuthConfiguration configuration;
    configuration.clientId = QStringLiteral(COLOSSEUM_TRAKT_CLIENT_ID).trimmed();
    configuration.brokerBaseUrl = QUrl(QStringLiteral(COLOSSEUM_TRAKT_TOKEN_BROKER_URL));
    configuration.appName = QStringLiteral("Colosseum");
#ifdef COLOSSEUM_VERSION
    configuration.appVersion = QStringLiteral(COLOSSEUM_VERSION);
#else
    configuration.appVersion = QStringLiteral("dev");
#endif
    if (!traktConfigurationIsValid(configuration)) return std::nullopt;
    return configuration;
#else
    return std::nullopt;
#endif
}
