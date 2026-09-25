#include "SimklAuth.h"

#include "TrackerConnectionStore.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QRandomGenerator>
#include <QUrlQuery>

#include <algorithm>
#include <limits>

namespace {

constexpr qsizetype kStateBytes = 32;
constexpr qsizetype kVerifierBytes = 64;
constexpr qsizetype kMaximumAuthorizationCodeLength = 2048;
constexpr qsizetype kMaximumTokenBytes = 16 * 1024;
constexpr qsizetype kMaximumDeviceCodeBytes = 2048;
constexpr qsizetype kMaximumUserCodeLength = 64;
constexpr qint64 kBrowserTimeoutMs = 5 * 60 * 1000;
constexpr qint64 kMaximumAccessTokenLifetimeMs = 8 * 24 * 60 * 60 * 1000LL;
constexpr qint64 kMaximumRefreshTokenLifetimeMs = 181 * 24 * 60 * 60 * 1000LL;

bool hasOnlyDigits(const QString &value)
{
    if (value.isEmpty() || value.size() > 64)
        return false;
    for (const QChar character : value) {
        if (!character.isDigit())
            return false;
    }
    return value != QLatin1String("0");
}

bool isHttpsEndpoint(const QUrl &url)
{
    return url.isValid() && url.scheme() == QLatin1String("https") && !url.host().isEmpty();
}

bool isLoopbackRedirect(const QUrl &url)
{
    if (!url.isValid() || url.scheme() != QLatin1String("http"))
        return false;
    const QString host = url.host().toLower();
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost")
        || host == QLatin1String("::1");
}

bool isSimklPinUri(const QUrl &url)
{
    return url.isValid() && url.scheme() == QLatin1String("https")
        && url.host().compare(QLatin1String("simkl.com"), Qt::CaseInsensitive) == 0
        && (url.port() == -1 || url.port() == 443)
        && url.path() == QLatin1String("/pin")
        && url.fragment().isEmpty() && url.userName().isEmpty() && url.password().isEmpty();
}

bool validConfiguration(const SimklAuthConfiguration &configuration)
{
    if (configuration.clientId.isEmpty() || configuration.clientId.size() > 256
        || configuration.clientId != configuration.clientId.trimmed()
        || configuration.appName.isEmpty() || configuration.appName.size() > 64
        || configuration.appVersion.isEmpty() || configuration.appVersion.size() > 64
        || !isHttpsEndpoint(configuration.authorizationEndpoint)
        || !isHttpsEndpoint(configuration.tokenEndpoint)
        || !isHttpsEndpoint(configuration.deviceEndpoint)
        || !isHttpsEndpoint(configuration.identityEndpoint)
        || !isLoopbackRedirect(configuration.redirectUri)
        || configuration.requiredScopes.isEmpty()
        || configuration.requiredScopes.size() > 2) {
        return false;
    }
    for (const QString &scope : configuration.requiredScopes) {
        if (scope != QLatin1String("media:read") && scope != QLatin1String("media:write"))
            return false;
    }
    return true;
}

QByteArray randomBytes(TrackerRandomSource *random, qsizetype count)
{
    if (random)
        return random->bytes(count);
    QByteArray result;
    result.resize(count);
    for (qsizetype index = 0; index < count; ++index)
        result[index] = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    return result;
}

QByteArray randomBytesExactly(TrackerRandomSource *random, qsizetype count)
{
    const QByteArray result = randomBytes(random, count);
    return result.size() == count ? result : QByteArray();
}

QByteArray base64Url(const QByteArray &value)
{
    return value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

bool secureEquals(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size())
        return false;
    uchar difference = 0;
    for (qsizetype index = 0; index < left.size(); ++index)
        difference |= static_cast<uchar>(left.at(index)) ^ static_cast<uchar>(right.at(index));
    return difference == 0;
}

bool tokenResponseIsUsable(const SimklTokenResponse &response,
                           const SimklAuthConfiguration &configuration)
{
    if (response.accessToken.isEmpty() || response.refreshToken.isEmpty()
        || response.accessToken.size() > kMaximumTokenBytes
        || response.refreshToken.size() > kMaximumTokenBytes
        || response.accessExpiresInMs <= 0
        || response.accessExpiresInMs > kMaximumAccessTokenLifetimeMs
        || response.refreshExpiresInMs <= response.accessExpiresInMs
        || response.refreshExpiresInMs > kMaximumRefreshTokenLifetimeMs) {
        return false;
    }
    for (const QString &required : configuration.requiredScopes) {
        if (!response.grantedScopes.contains(required))
            return false;
    }
    return true;
}

bool checkedAdd(qint64 left, qint64 right, qint64 *result)
{
    if (right <= 0 || left > std::numeric_limits<qint64>::max() - right)
        return false;
    *result = left + right;
    return true;
}

} // namespace

bool DefaultTrackerSystemBrowser::open(const QUrl &url)
{
    return QDesktopServices::openUrl(url);
}

QStringList SimklAuthSnapshot::sanitizedFieldNames()
{
    return {
        QStringLiteral("phase"),
        QStringLiteral("error"),
        QStringLiteral("remoteAccountId"),
        QStringLiteral("userCode"),
        QStringLiteral("verificationUri"),
        QStringLiteral("expiresAtMs"),
        QStringLiteral("retryAfterMs")};
}

SimklAuthError simklAuthErrorForTransport(SimklTransportError error)
{
    switch (error) {
    case SimklTransportError::None:
    case SimklTransportError::AuthorizationPending:
        return SimklAuthError::None;
    case SimklTransportError::AccessDenied:
        return SimklAuthError::AccessDenied;
    case SimklTransportError::Expired:
        return SimklAuthError::Expired;
    case SimklTransportError::Revoked:
        return SimklAuthError::Revoked;
    case SimklTransportError::RateLimited:
        return SimklAuthError::RateLimited;
    case SimklTransportError::PayloadTooLarge:
        return SimklAuthError::PayloadTooLarge;
    case SimklTransportError::NetworkFailure:
        return SimklAuthError::Transport;
    case SimklTransportError::ProtocolFailure:
        return SimklAuthError::TokenRejected;
    }
    return SimklAuthError::TokenRejected;
}

SimklAuthSession::SimklAuthSession(SimklAuthBinding binding,
                                   SimklAuthConfiguration configuration,
                                   TrackerCredentialVault *vault,
                                   TrackerSystemBrowser *browser,
                                   SimklAuthTransport *transport,
                                   TrackerRandomSource *random,
                                   TrackerClock *clock,
                                   TrackerConnectionStore *connections)
    : m_binding(std::move(binding)),
      m_configuration(std::move(configuration)),
      m_vault(vault),
      m_browser(browser),
      m_transport(transport),
      m_random(random),
      m_clock(clock),
      m_connections(connections),
      m_callbackLifetime(std::make_shared<CallbackLifetime>())
{
}

SimklAuthSession::~SimklAuthSession()
{
    const std::lock_guard<std::recursive_mutex> lock(m_callbackLifetime->mutex);
    m_callbackLifetime->alive = false;
    clearTransientSecrets();
}

bool SimklAuthSession::beginBrowserAuthorization()
{
    if (!mayBegin()) {
        becomeAttention(SimklAuthError::Configuration);
        return false;
    }
    ++m_generation;
    clearTransientSecrets();
    m_snapshot = {};
    m_state = base64Url(randomBytesExactly(m_random, kStateBytes));
    m_codeVerifier = base64Url(randomBytesExactly(m_random, kVerifierBytes));
    if (m_state.isEmpty() || m_codeVerifier.isEmpty()) {
        becomeAttention(SimklAuthError::Configuration);
        return false;
    }

    QUrl authorizationUrl = m_configuration.authorizationEndpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), m_configuration.clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), m_configuration.redirectUri.toString());
    query.addQueryItem(QStringLiteral("scope"), m_configuration.requiredScopes.join(QLatin1Char(' ')));
    query.addQueryItem(QStringLiteral("state"), QString::fromLatin1(m_state));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(base64Url(
        QCryptographicHash::hash(m_codeVerifier, QCryptographicHash::Sha256))));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    authorizationUrl.setQuery(query);

    if (!checkedAdd(m_clock->nowMs(), kBrowserTimeoutMs, &m_deadlineMs)) {
        becomeAttention(SimklAuthError::Configuration);
        return false;
    }

    if (!m_browser->open(authorizationUrl)) {
        becomeAttention(SimklAuthError::BrowserUnavailable);
        return false;
    }
    m_snapshot.phase = SimklAuthPhase::AwaitingBrowserCallback;
    m_snapshot.expiresAtMs = m_deadlineMs;
    return true;
}

bool SimklAuthSession::acceptBrowserCallback(
    const QString &state,
    const QString &authorizationCode)
{
    if (m_snapshot.phase != SimklAuthPhase::AwaitingBrowserCallback
        || authorizationCode.isEmpty()
        || authorizationCode != authorizationCode.trimmed()
        || authorizationCode.size() > kMaximumAuthorizationCodeLength
        || !secureEquals(state.toUtf8(), m_state)) {
        return false;
    }
    m_snapshot.phase = SimklAuthPhase::ExchangingToken;
    m_snapshot.expiresAtMs = 0;
    const quint64 generation = m_generation;
    const std::weak_ptr<CallbackLifetime> lifetime = m_callbackLifetime;
    m_transport->exchangeAuthorizationCode(
        {m_configuration, authorizationCode, m_codeVerifier},
        [this, generation, lifetime](const SimklTokenResponse &response) {
            const std::shared_ptr<CallbackLifetime> guarded = lifetime.lock();
            if (!guarded)
                return;
            const std::lock_guard<std::recursive_mutex> lock(guarded->mutex);
            if (!guarded->alive)
                return;
            completeToken(generation, response);
        });
    return true;
}

bool SimklAuthSession::beginDevicePinAuthorization()
{
    if (!mayBegin()) {
        becomeAttention(SimklAuthError::Configuration);
        return false;
    }
    ++m_generation;
    clearTransientSecrets();
    m_snapshot = {};
    const quint64 generation = m_generation;
    m_snapshot.phase = SimklAuthPhase::ExchangingToken;
    const std::weak_ptr<CallbackLifetime> lifetime = m_callbackLifetime;
    m_transport->requestDevicePin(
        {m_configuration},
        [this, generation, lifetime](const SimklDevicePinResponse &response) {
            const std::shared_ptr<CallbackLifetime> guarded = lifetime.lock();
            if (!guarded)
                return;
            const std::lock_guard<std::recursive_mutex> lock(guarded->mutex);
            if (!guarded->alive)
                return;
            completeDevicePin(generation, response);
        });
    return true;
}

bool SimklAuthSession::pollDevicePin()
{
    expireIfDue();
    if (m_snapshot.phase != SimklAuthPhase::AwaitingDeviceApproval || m_deviceCode.isEmpty()
        || m_clock->nowMs() < m_nextDevicePollAtMs)
        return false;
    m_snapshot.phase = SimklAuthPhase::ExchangingToken;
    const quint64 generation = m_generation;
    qint64 nextPollAt = 0;
    if (!checkedAdd(m_clock->nowMs(), m_snapshot.retryAfterMs, &nextPollAt)) {
        becomeAttention(SimklAuthError::TokenRejected);
        return false;
    }
    m_nextDevicePollAtMs = nextPollAt;
    const std::weak_ptr<CallbackLifetime> lifetime = m_callbackLifetime;
    m_transport->pollDevicePin(
        {m_configuration, m_deviceCode},
        [this, generation, lifetime](const SimklTokenResponse &response) {
            const std::shared_ptr<CallbackLifetime> guarded = lifetime.lock();
            if (!guarded)
                return;
            const std::lock_guard<std::recursive_mutex> lock(guarded->mutex);
            if (!guarded->alive)
                return;
            if (generation == m_generation
                && response.error == SimklTransportError::AuthorizationPending) {
                m_snapshot.phase = SimklAuthPhase::AwaitingDeviceApproval;
                return;
            }
            completeToken(generation, response);
        });
    return true;
}

void SimklAuthSession::cancel()
{
    ++m_generation;
    clearTransientSecrets();
    m_snapshot.phase = SimklAuthPhase::Cancelled;
    m_snapshot.error = SimklAuthError::None;
    m_snapshot.remoteAccountId.clear();
    m_snapshot.userCode.clear();
    m_snapshot.verificationUri.clear();
    m_snapshot.expiresAtMs = 0;
    m_snapshot.retryAfterMs = 0;
}

void SimklAuthSession::invalidateProfile(SimklAuthBinding replacement)
{
    m_binding = std::move(replacement);
    cancel();
}

void SimklAuthSession::expireIfDue()
{
    if ((m_snapshot.phase == SimklAuthPhase::AwaitingBrowserCallback
         || m_snapshot.phase == SimklAuthPhase::AwaitingDeviceApproval)
        && m_deadlineMs > 0 && m_clock->nowMs() >= m_deadlineMs) {
        ++m_generation;
        clearTransientSecrets();
        m_snapshot.phase = SimklAuthPhase::TimedOut;
        m_snapshot.error = SimklAuthError::Expired;
        m_snapshot.userCode.clear();
        m_snapshot.verificationUri.clear();
        m_snapshot.expiresAtMs = 0;
    }
}

SimklAuthSnapshot SimklAuthSession::snapshot() const
{
    return m_snapshot;
}

void SimklAuthSession::completeToken(quint64 generation, const SimklTokenResponse &response)
{
    if (generation != m_generation || m_snapshot.phase != SimklAuthPhase::ExchangingToken)
        return;
    if (response.error != SimklTransportError::None) {
        becomeAttention(simklAuthErrorForTransport(response.error), response.retryAfterMs);
        return;
    }
    if (!tokenResponseIsUsable(response, m_configuration)) {
        const bool scopesMissing = !response.accessToken.isEmpty()
            && !response.refreshToken.isEmpty()
            && response.accessExpiresInMs > 0 && response.refreshExpiresInMs > 0;
        becomeAttention(scopesMissing ? SimklAuthError::MissingPermission
                                      : SimklAuthError::TokenRejected);
        return;
    }
    m_snapshot.phase = SimklAuthPhase::VerifyingIdentity;
    m_transport->fetchStableAccountId(
        {m_configuration, response.accessToken},
        [this, generation, response, lifetime = std::weak_ptr<CallbackLifetime>(m_callbackLifetime)](
            const SimklIdentityResponse &identity) {
            const std::shared_ptr<CallbackLifetime> guarded = lifetime.lock();
            if (!guarded)
                return;
            const std::lock_guard<std::recursive_mutex> lock(guarded->mutex);
            if (!guarded->alive)
                return;
            completeIdentity(generation, response, identity);
        });
}

void SimklAuthSession::completeIdentity(
    quint64 generation,
    const SimklTokenResponse &token,
    const SimklIdentityResponse &response)
{
    if (generation != m_generation || m_snapshot.phase != SimklAuthPhase::VerifyingIdentity)
        return;
    if (response.error != SimklTransportError::None) {
        becomeAttention(simklAuthErrorForTransport(response.error), response.retryAfterMs);
        return;
    }
    const QString stableId = response.remoteAccountId.trimmed();
    if (stableId != response.remoteAccountId || !hasOnlyDigits(stableId)) {
        becomeAttention(SimklAuthError::MissingStableAccount);
        return;
    }
    if (m_connections) {
        QString connectionError;
        if (!m_connections->refresh(&connectionError)) {
            becomeAttention(SimklAuthError::CredentialStore);
            return;
        }
        const auto binding = m_connections->connection(TrackerProviderId::Simkl);
        if (binding
            && (binding->state == TrackerConnectionState::Connected
                || binding->state == TrackerConnectionState::TransferPending)
            && binding->remoteAccountId != stableId) {
            becomeAttention(SimklAuthError::AccountChangeRequired);
            return;
        }
    }
    qint64 accessExpiresAtMs = 0;
    qint64 refreshExpiresAtMs = 0;
    if (!checkedAdd(m_clock->nowMs(), token.accessExpiresInMs, &accessExpiresAtMs)
        || !checkedAdd(m_clock->nowMs(), token.refreshExpiresInMs, &refreshExpiresAtMs)) {
        becomeAttention(SimklAuthError::TokenRejected);
        return;
    }
    const TrackerCredential credential{
        {m_binding.profileId, TrackerProviderId::Simkl, stableId},
        token.accessToken,
        token.refreshToken,
        accessExpiresAtMs,
        refreshExpiresAtMs,
        token.grantedScopes};
    const auto existing = m_vault->loadForProfile(
        m_binding.profileId, TrackerProviderId::Simkl);
    if (existing && existing->slot.remoteAccountId != stableId) {
        becomeAttention(SimklAuthError::AccountChangeRequired);
        return;
    }
    const auto restorePreviousCredential = [&]() {
        if (!existing)
            return m_vault->clearForProfile(m_binding.profileId, TrackerProviderId::Simkl);
        const auto matchesExisting = [&]() {
            const auto restored = m_vault->loadForProfile(
                m_binding.profileId, TrackerProviderId::Simkl);
            return restored && restored->slot.profileId == existing->slot.profileId
                && restored->slot.providerId == existing->slot.providerId
                && restored->slot.remoteAccountId == existing->slot.remoteAccountId
                && restored->accessToken == existing->accessToken
                && restored->refreshToken == existing->refreshToken
                && restored->accessTokenExpiresAtMs == existing->accessTokenExpiresAtMs
                && restored->refreshTokenExpiresAtMs == existing->refreshTokenExpiresAtMs
                && restored->grantedScopes == existing->grantedScopes;
        };
        if (matchesExisting())
            return true;
        return m_vault->saveAndVerify(*existing) && matchesExisting();
    };
    if (!m_vault->saveAndVerify(credential)) {
        becomeAttention(restorePreviousCredential()
                            ? SimklAuthError::CredentialStore
                            : SimklAuthError::CredentialRecoveryFailed);
        return;
    }
    const auto readback = m_vault->loadForProfile(m_binding.profileId, TrackerProviderId::Simkl);
    if (!readback || readback->slot.remoteAccountId != credential.slot.remoteAccountId
        || readback->accessToken != credential.accessToken
        || readback->refreshToken != credential.refreshToken) {
        becomeAttention(restorePreviousCredential()
                            ? SimklAuthError::CredentialStore
                            : SimklAuthError::CredentialRecoveryFailed);
        return;
    }
    clearTransientSecrets();
    // TrackerConnectionRuntime still has to atomically persist the profile
    // binding and external-account claim. Only that owner can publish Connected.
    m_snapshot.phase = SimklAuthPhase::CredentialReady;
    m_snapshot.error = SimklAuthError::None;
    m_snapshot.remoteAccountId = stableId;
    m_snapshot.userCode.clear();
    m_snapshot.expiresAtMs = accessExpiresAtMs;
    m_snapshot.retryAfterMs = 0;
}

void SimklAuthSession::completeDevicePin(
    quint64 generation,
    const SimklDevicePinResponse &response)
{
    if (generation != m_generation || m_snapshot.phase != SimklAuthPhase::ExchangingToken)
        return;
    if (response.error != SimklTransportError::None) {
        becomeAttention(simklAuthErrorForTransport(response.error), response.retryAfterMs);
        return;
    }
    const QString code = response.userCode.trimmed();
    qint64 deadline = 0;
    if (response.deviceCode.isEmpty() || response.deviceCode.size() > kMaximumDeviceCodeBytes
        || code.isEmpty() || code != response.userCode || code.size() > kMaximumUserCodeLength
        || !isSimklPinUri(response.verificationUri)
        || !isSimklPinUri(response.verificationUriComplete)
        || response.expiresInMs <= 0 || response.pollIntervalMs <= 0
        || response.pollIntervalMs > response.expiresInMs
        || !checkedAdd(m_clock->nowMs(), response.expiresInMs, &deadline)) {
        becomeAttention(SimklAuthError::TokenRejected);
        return;
    }
    m_deviceCode = response.deviceCode;
    m_deadlineMs = deadline;
    m_snapshot.phase = SimklAuthPhase::AwaitingDeviceApproval;
    m_snapshot.error = SimklAuthError::None;
    m_snapshot.userCode = code;
    m_snapshot.verificationUri = response.verificationUri;
    m_snapshot.expiresAtMs = deadline;
    m_snapshot.retryAfterMs = qMax<qint64>(0, response.pollIntervalMs);
    if (!checkedAdd(m_clock->nowMs(), m_snapshot.retryAfterMs, &m_nextDevicePollAtMs)) {
        becomeAttention(SimklAuthError::TokenRejected);
    }
}

void SimklAuthSession::becomeAttention(SimklAuthError error, qint64 retryAfterMs)
{
    clearTransientSecrets();
    m_snapshot.phase = SimklAuthPhase::NeedsAttention;
    m_snapshot.error = error;
    m_snapshot.remoteAccountId.clear();
    m_snapshot.userCode.clear();
    m_snapshot.verificationUri.clear();
    m_snapshot.expiresAtMs = 0;
    m_snapshot.retryAfterMs = qMax<qint64>(0, retryAfterMs);
}

void SimklAuthSession::clearTransientSecrets()
{
    m_state.fill('\0');
    m_state.clear();
    m_codeVerifier.fill('\0');
    m_codeVerifier.clear();
    m_deviceCode.fill('\0');
    m_deviceCode.clear();
    m_deadlineMs = 0;
    m_nextDevicePollAtMs = 0;
}

bool SimklAuthSession::mayBegin() const
{
    return m_vault && m_vault->isAvailable() && m_browser && m_transport && m_clock
        && validConfiguration(m_configuration) && !m_binding.profileId.isEmpty();
}

SimklIdentityProvider::SimklIdentityProvider(
    SimklAuthConfiguration configuration,
    TrackerCredentialVault *vault,
    SimklAuthTransport *transport)
    : m_configuration(std::move(configuration)),
      m_vault(vault),
      m_transport(transport),
      m_callbackLifetime(std::make_shared<CallbackLifetime>())
{
    m_callbackLifetime->vault = vault;
}

SimklIdentityProvider::~SimklIdentityProvider()
{
    const std::lock_guard<std::mutex> lock(m_callbackLifetime->mutex);
    m_callbackLifetime->alive = false;
    m_callbackLifetime->vault = nullptr;
}

TrackerProviderDescriptor SimklIdentityProvider::descriptor() const
{
    return {
        TrackerProviderId::Simkl,
        trackerProviderDisplayName(TrackerProviderId::Simkl),
        TrackerProviderCapability::ReadHistory
            | TrackerProviderCapability::ReadProgress
            | TrackerProviderCapability::WriteProgress
            | TrackerProviderCapability::WriteCompletion
            | TrackerProviderCapability::Scrobble,
        m_vault && m_vault->isAvailable() && m_transport && validConfiguration(m_configuration)};
}

void SimklIdentityProvider::requestStableAccountId(
    const TrackerConnectionAttempt &attempt,
    StableAccountCompletion completion)
{
    if (!completion || !m_vault || !m_transport || !validConfiguration(m_configuration)
        || attempt.providerId != TrackerProviderId::Simkl) {
        if (completion)
            completion({});
        return;
    }
    const auto credential = m_vault->loadForProfile(attempt.profileId, TrackerProviderId::Simkl);
    if (!credential || credential->slot.remoteAccountId.isEmpty()) {
        completion({});
        return;
    }
    const QString expectedStableId = credential->slot.remoteAccountId;
    const QByteArray expectedAccessToken = credential->accessToken;
    const std::weak_ptr<CallbackLifetime> lifetime = m_callbackLifetime;
    m_transport->fetchStableAccountId(
        {m_configuration, credential->accessToken},
        [completion = std::move(completion), expectedStableId, expectedAccessToken,
         lifetime, profileId = attempt.profileId](const SimklIdentityResponse &response) {
            const std::shared_ptr<CallbackLifetime> guarded = lifetime.lock();
            if (!guarded)
                return;
            const std::lock_guard<std::mutex> lock(guarded->mutex);
            if (!guarded->alive || !guarded->vault)
                return;
            if (response.error == SimklTransportError::Expired
                || response.error == SimklTransportError::Revoked) {
                const auto current = guarded->vault->loadForProfile(
                    profileId, TrackerProviderId::Simkl);
                if (current && current->slot.remoteAccountId == expectedStableId
                    && current->accessToken == expectedAccessToken) {
                    guarded->vault->clearForProfile(profileId, TrackerProviderId::Simkl);
                }
            }
            if (response.error != SimklTransportError::None
                || response.remoteAccountId != expectedStableId
                || !hasOnlyDigits(response.remoteAccountId)) {
                completion({});
                return;
            }
            completion({response.remoteAccountId});
        });
}

std::optional<SimklAuthConfiguration> simklProductionConfiguration()
{
    // This source tree contains no registered SIMKL desktop client receipt.
    // No environment variable or test configuration may turn production on.
    return std::nullopt;
}
