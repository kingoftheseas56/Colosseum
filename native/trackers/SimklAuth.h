#pragma once

#include "TrackerRuntime.h"

#include <QUrl>

#include <functional>
#include <memory>
#include <mutex>

class TrackerRandomSource
{
public:
    virtual ~TrackerRandomSource() = default;
    virtual QByteArray bytes(qsizetype count) = 0;
};

class TrackerConnectionStore;

class TrackerSystemBrowser
{
public:
    virtual ~TrackerSystemBrowser() = default;
    virtual bool open(const QUrl &url) = 0;
};

class DefaultTrackerSystemBrowser final : public TrackerSystemBrowser
{
public:
    bool open(const QUrl &url) override;
};

enum class SimklTransportError : quint8 {
    None,
    AuthorizationPending,
    AccessDenied,
    Expired,
    Revoked,
    RateLimited,
    PayloadTooLarge,
    NetworkFailure,
    ProtocolFailure
};
Q_DECLARE_METATYPE(SimklTransportError)

enum class SimklAuthPhase : quint8 {
    Idle,
    AwaitingBrowserCallback,
    AwaitingDeviceApproval,
    ExchangingToken,
    VerifyingIdentity,
    CredentialReady,
    Connected,
    Cancelled,
    TimedOut,
    NeedsAttention
};

enum class SimklAuthError : quint8 {
    None,
    Configuration,
    BrowserUnavailable,
    CallbackRejected,
    AccessDenied,
    Expired,
    Revoked,
    RateLimited,
    PayloadTooLarge,
    Transport,
    TokenRejected,
    MissingPermission,
    MissingStableAccount,
    AccountChangeRequired,
    CredentialStore,
    CredentialRecoveryFailed
};

struct SimklAuthConfiguration {
    QString clientId;
    QUrl authorizationEndpoint;
    QUrl tokenEndpoint;
    QUrl deviceEndpoint;
    QUrl identityEndpoint;
    QUrl redirectUri;
    QString appName;
    QString appVersion;
    QStringList requiredScopes;
};

struct SimklAuthorizationCodeRequest {
    SimklAuthConfiguration configuration;
    QString authorizationCode;
    QByteArray codeVerifier;
};

struct SimklDevicePinRequest {
    SimklAuthConfiguration configuration;
};

struct SimklDevicePinPollRequest {
    SimklAuthConfiguration configuration;
    QByteArray deviceCode;
};

struct SimklIdentityRequest {
    SimklAuthConfiguration configuration;
    QByteArray accessToken;
};

struct SimklTokenResponse {
    SimklTransportError error = SimklTransportError::ProtocolFailure;
    QByteArray accessToken;
    QByteArray refreshToken;
    qint64 accessExpiresInMs = 0;
    qint64 refreshExpiresInMs = 0;
    QStringList grantedScopes;
    qint64 retryAfterMs = 0;
};

struct SimklDevicePinResponse {
    SimklTransportError error = SimklTransportError::ProtocolFailure;
    QByteArray deviceCode;
    QString userCode;
    QUrl verificationUri;
    QUrl verificationUriComplete;
    qint64 expiresInMs = 0;
    qint64 pollIntervalMs = 0;
    qint64 retryAfterMs = 0;
};

struct SimklIdentityResponse {
    SimklTransportError error = SimklTransportError::ProtocolFailure;
    QString remoteAccountId;
    qint64 retryAfterMs = 0;
};

using SimklTokenCompletion = std::function<void(const SimklTokenResponse &)>;
using SimklDevicePinCompletion = std::function<void(const SimklDevicePinResponse &)>;
using SimklIdentityCompletion = std::function<void(const SimklIdentityResponse &)>;

class SimklAuthTransport
{
public:
    virtual ~SimklAuthTransport() = default;
    virtual void exchangeAuthorizationCode(
        const SimklAuthorizationCodeRequest &request,
        SimklTokenCompletion completion) = 0;
    virtual void requestDevicePin(
        const SimklDevicePinRequest &request,
        SimklDevicePinCompletion completion) = 0;
    virtual void pollDevicePin(
        const SimklDevicePinPollRequest &request,
        SimklTokenCompletion completion) = 0;
    virtual void fetchStableAccountId(
        const SimklIdentityRequest &request,
        SimklIdentityCompletion completion) = 0;
};

struct SimklAuthBinding {
    QString profileId;
    quint64 profileGeneration = 0;
};

struct SimklAuthSnapshot {
    SimklAuthPhase phase = SimklAuthPhase::Idle;
    SimklAuthError error = SimklAuthError::None;
    QString remoteAccountId;
    QString userCode;
    QUrl verificationUri;
    qint64 expiresAtMs = 0;
    qint64 retryAfterMs = 0;

    static QStringList sanitizedFieldNames();
};

class SimklAuthSession final
{
public:
    SimklAuthSession(SimklAuthBinding binding,
                     SimklAuthConfiguration configuration,
                     TrackerCredentialVault *vault,
                     TrackerSystemBrowser *browser,
                     SimklAuthTransport *transport,
                     TrackerRandomSource *random,
                     TrackerClock *clock,
                     TrackerConnectionStore *connections = nullptr);
    ~SimklAuthSession();

    SimklAuthSession(const SimklAuthSession &) = delete;
    SimklAuthSession &operator=(const SimklAuthSession &) = delete;
    SimklAuthSession(SimklAuthSession &&) = delete;
    SimklAuthSession &operator=(SimklAuthSession &&) = delete;

    bool beginBrowserAuthorization();
    bool acceptBrowserCallback(const QString &state, const QString &authorizationCode);
    bool beginDevicePinAuthorization();
    bool pollDevicePin();
    void cancel();
    void invalidateProfile(SimklAuthBinding replacement);
    void expireIfDue();
    SimklAuthSnapshot snapshot() const;

private:
    struct CallbackLifetime {
        std::recursive_mutex mutex;
        bool alive = true;
    };

    void completeToken(quint64 generation, const SimklTokenResponse &response);
    void completeIdentity(quint64 generation,
                          const SimklTokenResponse &token,
                          const SimklIdentityResponse &response);
    void completeDevicePin(quint64 generation, const SimklDevicePinResponse &response);
    void becomeAttention(SimklAuthError error, qint64 retryAfterMs = 0);
    void clearTransientSecrets();
    bool mayBegin() const;

    SimklAuthBinding m_binding;
    SimklAuthConfiguration m_configuration;
    TrackerCredentialVault *m_vault = nullptr;
    TrackerSystemBrowser *m_browser = nullptr;
    SimklAuthTransport *m_transport = nullptr;
    TrackerRandomSource *m_random = nullptr;
    TrackerClock *m_clock = nullptr;
    TrackerConnectionStore *m_connections = nullptr;
    SimklAuthSnapshot m_snapshot;
    QByteArray m_state;
    QByteArray m_codeVerifier;
    QByteArray m_deviceCode;
    quint64 m_generation = 0;
    qint64 m_deadlineMs = 0;
    qint64 m_nextDevicePollAtMs = 0;
    std::shared_ptr<CallbackLifetime> m_callbackLifetime;
};

class SimklIdentityProvider final : public TrackerIdentityProvider
{
public:
    SimklIdentityProvider(SimklAuthConfiguration configuration,
                          TrackerCredentialVault *vault,
                          SimklAuthTransport *transport);
    ~SimklIdentityProvider();

    SimklIdentityProvider(const SimklIdentityProvider &) = delete;
    SimklIdentityProvider &operator=(const SimklIdentityProvider &) = delete;
    SimklIdentityProvider(SimklIdentityProvider &&) = delete;
    SimklIdentityProvider &operator=(SimklIdentityProvider &&) = delete;

    TrackerProviderDescriptor descriptor() const override;
    void requestStableAccountId(
        const TrackerConnectionAttempt &attempt,
        StableAccountCompletion completion) override;

private:
    struct CallbackLifetime {
        std::mutex mutex;
        TrackerCredentialVault *vault = nullptr;
        bool alive = true;
    };

    SimklAuthConfiguration m_configuration;
    TrackerCredentialVault *m_vault = nullptr;
    SimklAuthTransport *m_transport = nullptr;
    std::shared_ptr<CallbackLifetime> m_callbackLifetime;
};

SimklAuthError simklAuthErrorForTransport(SimklTransportError error);
std::optional<SimklAuthConfiguration> simklProductionConfiguration();
