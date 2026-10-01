#pragma once

#include "MalProtocol.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrlQuery>

#include <functional>

enum class MalTransportError : quint8 {
    None,
    AuthenticationRequired,
    AccessDenied,
    RateLimited,
    PayloadTooLarge,
    NetworkFailure,
    ProtocolFailure
};

struct MalTokenResult {
    MalTransportError error = MalTransportError::ProtocolFailure;
    QByteArray accessToken;
    QByteArray refreshToken;
    qint64 accessExpiresInMs = 0;
    int statusCode = 0;

    bool succeeded() const
    {
        return error == MalTransportError::None
            && !accessToken.isEmpty() && !refreshToken.isEmpty()
            && accessExpiresInMs > 0;
    }
};

struct MalIdentityResult {
    MalTransportError error = MalTransportError::ProtocolFailure;
    QString remoteAccountId;
    QString userName;
    int statusCode = 0;

    bool succeeded() const
    {
        return error == MalTransportError::None
            && !remoteAccountId.isEmpty();
    }
};

struct MalApiResult {
    MalTransportError error = MalTransportError::ProtocolFailure;
    int statusCode = 0;
    QJsonDocument document;
    qint64 retryAfterMs = 0;
    bool networkFailure = false;
    bool payloadTooLarge = false;

    bool succeeded() const
    {
        return error == MalTransportError::None
            && statusCode >= 200 && statusCode < 300;
    }
};

class MalHttpTransport : public QObject
{
    Q_OBJECT

public:
    using TokenCompletion = std::function<void(const MalTokenResult &)>;
    using IdentityCompletion = std::function<void(const MalIdentityResult &)>;
    using ApiCompletion = std::function<void(const MalApiResult &)>;

    explicit MalHttpTransport(QObject *parent = nullptr);
    ~MalHttpTransport() override = default;

    virtual void exchangeAuthorizationCode(
        const MalAuthConfiguration &configuration,
        const QByteArray &authorizationCode,
        const QByteArray &codeVerifier,
        TokenCompletion completion);

    virtual void refreshToken(
        const MalAuthConfiguration &configuration,
        const QByteArray &refreshToken,
        TokenCompletion completion);

    virtual void fetchIdentity(
        const MalAuthConfiguration &configuration,
        const QByteArray &accessToken,
        IdentityCompletion completion);

    virtual void get(const MalAuthConfiguration &configuration,
             const QByteArray &accessToken,
             const QString &path,
             const QUrlQuery &query,
             ApiCompletion completion);

    virtual void patchForm(const MalAuthConfiguration &configuration,
                 const QByteArray &accessToken,
                 const QString &path,
                 const QUrlQuery &form,
                 ApiCompletion completion);

private:
    void tokenRequest(const MalAuthConfiguration &configuration,
                      const QUrlQuery &form,
                      TokenCompletion completion);
    void apiRequest(const MalAuthConfiguration &configuration,
                    const QByteArray &accessToken,
                    const QByteArray &method,
                    const QString &path,
                    const QUrlQuery &query,
                    const QUrlQuery &form,
                    ApiCompletion completion);

    QNetworkAccessManager m_network;
};
