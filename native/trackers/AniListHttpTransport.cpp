#include "AniListHttpTransport.h"

#include <QJsonObject>
#include <QNetworkRequest>

#include <utility>

namespace {

constexpr qsizetype kMaximumResponseBytes = 64 * 1024;
constexpr qsizetype kMaximumRequestBytes = 64 * 1024;
constexpr qsizetype kMaximumTokenBytes = 16 * 1024;
constexpr int kTransferTimeoutMs = 30 * 1000;

qint64 parseRetryAfterMs(const QByteArray &header)
{
    bool ok = false;
    const qint64 seconds = header.trimmed().toLongLong(&ok);
    return ok && seconds > 0 && seconds < 24 * 60 * 60
        ? seconds * 1000 : 0;
}

} // namespace

AniListApiResult aniListResultFromResponse(
    int statusCode,
    QNetworkReply::NetworkError networkError,
    const QByteArray &payload,
    const QByteArray &retryAfterHeader)
{
    AniListApiResult result;
    result.statusCode = statusCode;
    if (payload.size() > kMaximumResponseBytes) {
        result.payloadTooLarge = true;
        result.error = AniListTransportError::PayloadTooLarge;
        return result;
    }

    // Redirects are never followed: the origin is fixed.
    if (statusCode >= 300 && statusCode < 400) {
        result.error = AniListTransportError::RedirectRejected;
        return result;
    }
    if (statusCode == 0) {
        if (networkError == QNetworkReply::TimeoutError
            || networkError == QNetworkReply::OperationCanceledError) {
            result.error = AniListTransportError::Timeout;
        } else {
            result.error = AniListTransportError::NetworkFailure;
        }
        result.networkFailure = true;
        return result;
    }

    if (!payload.isEmpty()) {
        QJsonParseError parseError;
        result.document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError)
            result.document = QJsonDocument();
    }

    if (statusCode == 429) {
        result.error = AniListTransportError::RateLimited;
        result.retryAfterMs = parseRetryAfterMs(retryAfterHeader);
        return result;
    }
    // AniList reports a bad or expired bearer token as 401, or as 400 with an
    // "Invalid token" GraphQL error. Either way the user must reauthorize.
    if (statusCode == 401
        || ((statusCode == 400 || (statusCode >= 200 && statusCode < 300))
            && aniListResponseIsInvalidToken(result.document))) {
        result.error = AniListTransportError::AuthenticationRequired;
        return result;
    }
    if (statusCode >= 500) {
        result.error = AniListTransportError::ServerFailure;
        return result;
    }
    if (statusCode >= 200 && statusCode < 300) {
        result.error = result.document.isObject()
            ? AniListTransportError::None
            : AniListTransportError::ProtocolFailure;
        return result;
    }
    result.error = AniListTransportError::ProtocolFailure;
    return result;
}

AniListHttpTransport::AniListHttpTransport(QObject *parent)
    : QObject(parent)
{}

void AniListHttpTransport::post(
    const AniListAuthConfiguration &configuration,
    const QByteArray &accessToken,
    const QJsonObject &body,
    ApiCompletion completion)
{
    const QByteArray payload =
        QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (!aniListConfigurationIsValid(configuration)
        || accessToken.isEmpty() || accessToken.size() > kMaximumTokenBytes
        || body.isEmpty() || payload.size() > kMaximumRequestBytes) {
        if (completion)
            completion({});
        return;
    }

    QNetworkRequest request(configuration.graphqlEndpoint);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", "Bearer " + accessToken);
    request.setRawHeader(
        "User-Agent",
        (configuration.appName + QLatin1Char('/')
         + configuration.appVersion).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_network.post(request, payload);
    QObject::connect(reply, &QNetworkReply::finished, this,
        [reply, completion = std::move(completion)]() mutable {
            const int statusCode = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray response = reply->readAll();
            const AniListApiResult result = aniListResultFromResponse(
                statusCode, reply->error(), response,
                reply->rawHeader("Retry-After"));
            reply->deleteLater();
            if (completion)
                completion(result);
        });
}
