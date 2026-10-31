#pragma once

#include "AniListProtocol.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

#include <functional>

enum class AniListTransportError : quint8 {
    None,
    AuthenticationRequired,
    RateLimited,
    PayloadTooLarge,
    NetworkFailure,
    Timeout,
    ServerFailure,
    RedirectRejected,
    ProtocolFailure
};

struct AniListApiResult {
    AniListTransportError error = AniListTransportError::ProtocolFailure;
    int statusCode = 0;
    QJsonDocument document;
    qint64 retryAfterMs = 0;
    bool networkFailure = false;
    bool payloadTooLarge = false;

    bool succeeded() const
    {
        return error == AniListTransportError::None
            && statusCode >= 200 && statusCode < 300;
    }
};

// Pure response classification, shared by the live transport and its tests.
// `retryAfterHeader` is the raw Retry-After value (delta-seconds only).
AniListApiResult aniListResultFromResponse(
    int statusCode,
    QNetworkReply::NetworkError networkError,
    const QByteArray &payload,
    const QByteArray &retryAfterHeader);

class AniListHttpTransport : public QObject
{
    Q_OBJECT

public:
    using ApiCompletion = std::function<void(const AniListApiResult &)>;

    explicit AniListHttpTransport(QObject *parent = nullptr);
    ~AniListHttpTransport() override = default;

    // POSTs one GraphQL body (`query` + `variables`) to the fixed endpoint.
    virtual void post(const AniListAuthConfiguration &configuration,
                      const QByteArray &accessToken,
                      const QJsonObject &body,
                      ApiCompletion completion);

private:
    QNetworkAccessManager m_network;
};
