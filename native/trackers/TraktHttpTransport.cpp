#include "TraktHttpTransport.h"

#include "TraktCodec.h"

#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <limits>

namespace {
constexpr qint64 kMaximumBodyBytes = 4LL * 1024LL * 1024LL;
qint64 secondsToMs(const QJsonValue &value)
{
    const qint64 seconds = value.toInteger();
    return seconds > 0 && seconds <= std::numeric_limits<qint64>::max() / 1000 ? seconds * 1000 : 0;
}

TraktTransportError errorForStatus(int status)
{
    switch (status) {
    case 400: return TraktTransportError::AuthorizationPending;
    case 404: return TraktTransportError::InvalidCode;
    case 409: return TraktTransportError::AlreadyUsed;
    case 410: return TraktTransportError::Expired;
    case 418: return TraktTransportError::AccessDenied;
    case 429: return TraktTransportError::SlowDown;
    case 401:
    case 403: return TraktTransportError::Revoked;
    default: return TraktTransportError::ProtocolFailure;
    }
}
}

TraktHttpTransport::TraktHttpTransport(QObject *parent) : QObject(parent) {}

QList<QPair<QByteArray,QByteArray>> TraktHttpTransport::traktHeaders(
    const TraktAuthConfiguration &configuration, const QByteArray &accessToken)
{
    QList<QPair<QByteArray,QByteArray>> headers{
        {QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")},
        {QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")},
        {QByteArrayLiteral("trakt-api-version"), QByteArrayLiteral("2")},
        {QByteArrayLiteral("trakt-api-key"), configuration.clientId.toUtf8()},
        {QByteArrayLiteral("User-Agent"), (configuration.appName + QLatin1Char('/') + configuration.appVersion).toUtf8()}
    };
    if (!accessToken.isEmpty())
        headers.append({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + accessToken});
    return headers;
}

QUrl TraktHttpTransport::brokerEndpoint(const TraktAuthConfiguration &configuration, const QString &path)
{
    QUrl url = configuration.brokerBaseUrl;
    QString basePath = url.path();
    if (basePath.endsWith(QLatin1Char('/'))) basePath.chop(1);
    url.setPath(basePath + path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void TraktHttpTransport::postJson(const QUrl &url, const QJsonObject &body,
                                  const QList<QPair<QByteArray,QByteArray>> &headers,
                                  JsonCompletion completion)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    for (const auto &header : headers) request.setRawHeader(header.first, header.second);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const auto buffer = std::make_shared<QByteArray>();
    const auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, buffer, tooLarge] {
        if (*tooLarge) return;
        buffer->append(reply->readAll());
        if (buffer->size() > kMaximumBodyBytes) { *tooLarge = true; reply->abort(); }
    });
    finishReply(reply, buffer, tooLarge, std::move(completion));
}

void TraktHttpTransport::getJson(const QUrl &url,
                                 const QList<QPair<QByteArray,QByteArray>> &headers,
                                 JsonCompletion completion)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    for (const auto &header : headers) request.setRawHeader(header.first, header.second);
    QNetworkReply *reply = m_network.get(request);
    const auto buffer = std::make_shared<QByteArray>();
    const auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, buffer, tooLarge] {
        if (*tooLarge) return;
        buffer->append(reply->readAll());
        if (buffer->size() > kMaximumBodyBytes) { *tooLarge = true; reply->abort(); }
    });
    finishReply(reply, buffer, tooLarge, std::move(completion));
}

void TraktHttpTransport::finishReply(QNetworkReply *reply,
                                     const std::shared_ptr<QByteArray> &buffer,
                                     const std::shared_ptr<bool> &tooLarge,
                                     JsonCompletion completion)
{
    connect(reply, &QNetworkReply::finished, this, [reply, buffer, tooLarge, completion = std::move(completion)] {
        QByteArray body = *buffer;
        body += reply->readAll();
        JsonResult result;
        result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.networkFailure = reply->error() != QNetworkReply::NoError
            && (result.statusCode == 0 || (result.statusCode >= 200 && result.statusCode < 300));
        result.payloadTooLarge = *tooLarge || body.size() > kMaximumBodyBytes;
        if (!result.payloadTooLarge && !body.isEmpty()) {
            QJsonParseError error;
            result.document = QJsonDocument::fromJson(body, &error);
            if (error.error != QJsonParseError::NoError) result.document = {};
        }
        reply->deleteLater();
        completion(result);
    });
}

void TraktHttpTransport::requestDeviceCode(const TraktAuthConfiguration &configuration,
                                           DeviceCompletion completion)
{
    postJson(QUrl(QStringLiteral("https://auth.trakt.tv/oauth/device/code")),
             {{QStringLiteral("client_id"), configuration.clientId}},
             traktHeaders(configuration),
             [completion = std::move(completion)](const JsonResult &result) {
        TraktDeviceCodeResponse response;
        if (result.networkFailure) response.error = TraktTransportError::NetworkFailure;
        else if (result.statusCode != 200) response.error = errorForStatus(result.statusCode);
        else if (!result.document.isObject()) response.error = TraktTransportError::ProtocolFailure;
        else {
            const QJsonObject object = result.document.object();
            response.deviceCode = object.value(QStringLiteral("device_code")).toString().toUtf8();
            response.userCode = object.value(QStringLiteral("user_code")).toString();
            response.verificationUrl = QUrl(object.value(QStringLiteral("verification_url")).toString());
            response.expiresInMs = secondsToMs(object.value(QStringLiteral("expires_in")));
            response.pollIntervalMs = secondsToMs(object.value(QStringLiteral("interval")));
            response.error = response.deviceCode.isEmpty() || response.userCode.isEmpty()
                || !response.verificationUrl.isValid() || response.expiresInMs <= 0 || response.pollIntervalMs <= 0
                ? TraktTransportError::ProtocolFailure : TraktTransportError::None;
        }
        completion(response);
    });
}

void TraktHttpTransport::pollDeviceToken(const TraktAuthConfiguration &configuration,
                                         const QByteArray &deviceCode, TokenCompletion completion)
{
    postJson(brokerEndpoint(configuration, QStringLiteral("/v1/trakt/device-token")),
             {{QStringLiteral("code"), QString::fromUtf8(deviceCode)}},
             {{QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")},
              {QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")}},
             [completion = std::move(completion)](const JsonResult &result) {
        TraktTokenResponse response;
        if (result.networkFailure) response.error = TraktTransportError::NetworkFailure;
        else if (result.statusCode != 200) response.error = errorForStatus(result.statusCode);
        else if (!result.document.isObject()) response.error = TraktTransportError::ProtocolFailure;
        else {
            const QJsonObject object = result.document.object();
            response.accessToken = object.value(QStringLiteral("access_token")).toString().toUtf8();
            response.refreshToken = object.value(QStringLiteral("refresh_token")).toString().toUtf8();
            response.accessExpiresInMs = secondsToMs(object.value(QStringLiteral("expires_in")));
            response.createdAtMs = secondsToMs(object.value(QStringLiteral("created_at")));
            response.error = response.accessToken.isEmpty() || response.refreshToken.isEmpty()
                || response.accessExpiresInMs <= 0 ? TraktTransportError::ProtocolFailure : TraktTransportError::None;
        }
        completion(response);
    });
}

void TraktHttpTransport::fetchStableAccountId(const TraktAuthConfiguration &configuration,
                                              const QByteArray &accessToken,
                                              IdentityCompletion completion)
{
    getJson(QUrl(QStringLiteral("https://api.trakt.tv/users/settings")),
            traktHeaders(configuration, accessToken),
            [completion = std::move(completion)](const JsonResult &result) {
        TraktIdentityResponse response;
        if (result.networkFailure) response.error = TraktTransportError::NetworkFailure;
        else if (result.statusCode == 401 || result.statusCode == 403) response.error = TraktTransportError::Revoked;
        else if (result.statusCode == 429) response.error = TraktTransportError::RateLimited;
        else if (result.statusCode != 200 || !result.document.isObject()) response.error = TraktTransportError::ProtocolFailure;
        else {
            response.remoteAccountId = traktSettingsUuid(result.document);
            response.error = response.remoteAccountId.isEmpty() ? TraktTransportError::ProtocolFailure : TraktTransportError::None;
        }
        completion(response);
    });
}
