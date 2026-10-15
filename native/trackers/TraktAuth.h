#pragma once

#include "TrackerCredentialVault.h"
#include "TrackerRuntime.h"

#include <QUrl>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>

class TrackerConnectionStore;

enum class TraktTransportError : quint8 {
    None, AuthorizationPending, SlowDown, AccessDenied, Expired, InvalidCode,
    AlreadyUsed, RateLimited, NetworkFailure, ProtocolFailure, Revoked
};

enum class TraktAuthPhase : quint8 {
    Idle, RequestingCode, AwaitingApproval, ExchangingToken, VerifyingIdentity,
    CredentialReady, Cancelled, TimedOut, NeedsAttention
};

enum class TraktAuthError : quint8 {
    None, Configuration, AccessDenied, Expired, RateLimited, Transport,
    TokenRejected, MissingStableAccount, CredentialStore, AccountChangeRequired
};

struct TraktAuthConfiguration {
    QString clientId;
    QUrl brokerBaseUrl;
    QString appName;
    QString appVersion;
};

struct TraktDeviceCodeResponse {
    TraktTransportError error = TraktTransportError::ProtocolFailure;
    QByteArray deviceCode;
    QString userCode;
    QUrl verificationUrl;
    qint64 expiresInMs = 0;
    qint64 pollIntervalMs = 0;
};

struct TraktTokenResponse {
    TraktTransportError error = TraktTransportError::ProtocolFailure;
    QByteArray accessToken;
    QByteArray refreshToken;
    qint64 accessExpiresInMs = 0;
    qint64 createdAtMs = 0;
};

struct TraktIdentityResponse {
    TraktTransportError error = TraktTransportError::ProtocolFailure;
    QString remoteAccountId;
};

class TraktAuthTransport {
public:
    using DeviceCompletion = std::function<void(const TraktDeviceCodeResponse &)>;
    using TokenCompletion = std::function<void(const TraktTokenResponse &)>;
    using IdentityCompletion = std::function<void(const TraktIdentityResponse &)>;
    virtual ~TraktAuthTransport() = default;
    virtual void requestDeviceCode(const TraktAuthConfiguration &, DeviceCompletion) = 0;
    virtual void pollDeviceToken(const TraktAuthConfiguration &, const QByteArray &, TokenCompletion) = 0;
    virtual void fetchStableAccountId(const TraktAuthConfiguration &, const QByteArray &, IdentityCompletion) = 0;
};

struct TraktAuthBinding {
    QString profileId;
    quint64 profileGeneration = 0;
};

struct TraktAuthSnapshot {
    TraktAuthPhase phase = TraktAuthPhase::Idle;
    TraktAuthError error = TraktAuthError::None;
    QString remoteAccountId;
    QString userCode;
    QUrl verificationUrl;
    qint64 expiresAtMs = 0;
    qint64 nextPollAtMs = 0;
};

class TraktAuthSession final {
public:
    TraktAuthSession(TraktAuthBinding binding, TraktAuthConfiguration configuration,
                     TrackerCredentialVault *vault, TraktAuthTransport *transport,
                     TrackerClock *clock, TrackerConnectionStore *connections = nullptr);
    ~TraktAuthSession();
    bool begin();
    bool poll();
    void cancel();
    void expireIfDue();
    void invalidateProfile(TraktAuthBinding replacement);
    TraktAuthSnapshot snapshot() const;

private:
    struct CallbackLifetime { std::recursive_mutex mutex; bool alive = true; };
    void completeDevice(quint64 generation, const TraktDeviceCodeResponse &response);
    void completeToken(quint64 generation, const TraktTokenResponse &response);
    void completeIdentity(quint64 generation, const TraktTokenResponse &token, const TraktIdentityResponse &response);
    void attention(TraktAuthError error);
    bool configurationValid() const;
    void clearTransient();

    TraktAuthBinding m_binding;
    TraktAuthConfiguration m_configuration;
    TrackerCredentialVault *m_vault = nullptr;
    TraktAuthTransport *m_transport = nullptr;
    TrackerClock *m_clock = nullptr;
    TrackerConnectionStore *m_connections = nullptr;
    TraktAuthSnapshot m_snapshot;
    QByteArray m_deviceCode;
    quint64 m_generation = 0;
    qint64 m_pollIntervalMs = 0;
    std::shared_ptr<CallbackLifetime> m_callbackLifetime;
};

TrackerProviderCapabilities traktCapabilities();
TraktAuthError traktAuthErrorForTransport(TraktTransportError error);
std::optional<TraktAuthConfiguration> traktProductionConfiguration();

bool traktConfigurationIsValid(const TraktAuthConfiguration &configuration);
bool traktAccountUuidIsCanonical(const QString &accountId);
