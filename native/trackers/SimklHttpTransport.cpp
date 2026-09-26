#include "SimklHttpTransport.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <limits>

namespace {

constexpr qsizetype kMaximumResponseBytes = 128 * 1024;
constexpr qint64 kRefreshLifetimeMs = 180LL * 24 * 60 * 60 * 1000;

QString userAgent(const SimklAuthConfiguration &configuration)
{
    return configuration.appName + QLatin1Char('/') + configuration.appVersion;
}

QUrl apiUrl(QUrl url, const SimklAuthConfiguration &configuration)
{
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("client_id"), configuration.clientId);
    query.addQueryItem(QStringLiteral("app-name"), configuration.appName);
    query.addQueryItem(QStringLiteral("app-version"), configuration.appVersion);
    url.setQuery(query);
    return url;
}

QJsonObject parseObject(const QByteArray &payload, bool *ok)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    const bool valid = error.error == QJsonParseError::NoError && document.isObject();
    if (ok)
        *ok = valid;
    return valid ? document.object() : QJsonObject{};
}

qint64 secondsToMs(const QJsonValue &value)
{
    if (!value.isDouble())
        return 0;
    const double seconds = value.toDouble();
    if (seconds <= 0 || seconds > static_cast<double>(std::numeric_limits<qint64>::max() / 1000))
        return 0;
    return static_cast<qint64>(seconds * 1000.0);
}

QStringList scopes(const QString &value)
{
    return value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

SimklTransportError transportError(int status,
                                   const QByteArray &payload,
                                   bool networkFailure,
                                   bool tooLarge)
{
    if (tooLarge)
        return SimklTransportError::PayloadTooLarge;
    if (status == 429)
        return SimklTransportError::RateLimited;
    bool parsed = false;
    const QString error = parseObject(payload, &parsed)
                              .value(QStringLiteral("error")).toString();
    if (error == QLatin1String("authorization_pending")
        || error == QLatin1String("slow_down"))
        return SimklTransportError::AuthorizationPending;
    if (error == QLatin1String("access_denied"))
        return SimklTransportError::AccessDenied;
    if (error == QLatin1String("expired_token")
        || error == QLatin1String("invalid_grant"))
        return SimklTransportError::Expired;
    if (status == 401 || error == QLatin1String("user_token_failed"))
        return SimklTransportError::Revoked;
    if (networkFailure || status <= 0 || status >= 500)
        return SimklTransportError::NetworkFailure;
    return SimklTransportError::ProtocolFailure;
}

qint64 retryAfterMs(QNetworkReply *reply)
{
    bool ok = false;
    const qint64 seconds = reply->rawHeader("Retry-After").trimmed().toLongLong(&ok);
    return ok && seconds > 0 && seconds <= std::numeric_limits<qint64>::max() / 1000
        ? seconds * 1000 : 0;
}

} // namespace

SimklHttpTransport::SimklHttpTransport(QObject *parent)
    : QObject(parent)
{}

void SimklHttpTransport::exchangeAuthorizationCode(
    const SimklAuthorizationCodeRequest &request,
    SimklTokenCompletion completion)
{
    postForm(request.configuration.tokenEndpoint,
             {{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
              {QStringLiteral("client_id"), request.configuration.clientId},
              {QStringLiteral("code"), request.authorizationCode},
              {QStringLiteral("redirect_uri"), request.configuration.redirectUri.toString()},
              {QStringLiteral("code_verifier"), QString::fromLatin1(request.codeVerifier)}},
             userAgent(request.configuration),
             [configuration = request.configuration,
              completion = std::move(completion)](int status, const QByteArray &payload,
                                                   qint64 retryAfter, bool failed,
                                                   bool tooLarge) mutable {
        bool ok = false;
        const QJsonObject object = parseObject(payload, &ok);
        if (status < 200 || status >= 300 || failed || !ok) {
            completion({transportError(status, payload, failed, tooLarge), {}, {}, 0, 0, {}, retryAfter});
            return;
        }
        completion({SimklTransportError::None,
                    object.value(QStringLiteral("access_token")).toString().toUtf8(),
                    object.value(QStringLiteral("refresh_token")).toString().toUtf8(),
                    secondsToMs(object.value(QStringLiteral("expires_in"))),
                    kRefreshLifetimeMs,
                    scopes(object.value(QStringLiteral("scope")).toString()), 0});
    });
}

void SimklHttpTransport::requestDevicePin(
    const SimklDevicePinRequest &request,
    SimklDevicePinCompletion completion)
{
    postForm(request.configuration.deviceEndpoint,
             {{QStringLiteral("client_id"), request.configuration.clientId},
              {QStringLiteral("scope"), request.configuration.requiredScopes.join(QLatin1Char(' '))}},
             userAgent(request.configuration),
             [completion = std::move(completion)](int status, const QByteArray &payload,
                                                  qint64 retryAfter, bool failed,
                                                  bool tooLarge) mutable {
        bool ok = false;
        const QJsonObject object = parseObject(payload, &ok);
        if (status < 200 || status >= 300 || failed || !ok) {
            completion({transportError(status, payload, failed, tooLarge), {}, {}, {}, {}, 0, 0,
                        retryAfter});
            return;
        }
        completion({SimklTransportError::None,
                    object.value(QStringLiteral("device_code")).toString().toUtf8(),
                    object.value(QStringLiteral("user_code")).toString(),
                    QUrl(object.value(QStringLiteral("verification_uri")).toString()),
                    QUrl(object.value(QStringLiteral("verification_uri_complete")).toString()),
                    secondsToMs(object.value(QStringLiteral("expires_in"))),
                    secondsToMs(object.value(QStringLiteral("interval"))), 0});
    });
}

void SimklHttpTransport::pollDevicePin(
    const SimklDevicePinPollRequest &request,
    SimklTokenCompletion completion)
{
    postForm(request.configuration.tokenEndpoint,
             {{QStringLiteral("grant_type"),
               QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")},
              {QStringLiteral("client_id"), request.configuration.clientId},
              {QStringLiteral("device_code"), QString::fromUtf8(request.deviceCode)}},
             userAgent(request.configuration),
             [completion = std::move(completion)](int status, const QByteArray &payload,
                                                  qint64 retryAfter, bool failed,
                                                  bool tooLarge) mutable {
        bool ok = false;
        const QJsonObject object = parseObject(payload, &ok);
        if (status < 200 || status >= 300 || failed || !ok) {
            const QString error = object.value(QStringLiteral("error")).toString();
            const qint64 pendingDelay = error == QLatin1String("slow_down") ? 10000 : retryAfter;
            completion({transportError(status, payload, failed, tooLarge), {}, {}, 0, 0, {}, pendingDelay});
            return;
        }
        completion({SimklTransportError::None,
                    object.value(QStringLiteral("access_token")).toString().toUtf8(),
                    object.value(QStringLiteral("refresh_token")).toString().toUtf8(),
                    secondsToMs(object.value(QStringLiteral("expires_in"))),
                    kRefreshLifetimeMs,
                    scopes(object.value(QStringLiteral("scope")).toString()), 0});
    });
}

void SimklHttpTransport::fetchStableAccountId(
    const SimklIdentityRequest &request,
    SimklIdentityCompletion completion)
{
    getJson(apiUrl(request.configuration.identityEndpoint, request.configuration),
            request.accessToken, userAgent(request.configuration),
            [completion = std::move(completion)](int status, const QByteArray &payload,
                                                 qint64 retryAfter, bool failed,
                                                 bool tooLarge) mutable {
        bool ok = false;
        const QJsonObject object = parseObject(payload, &ok);
        const QJsonValue id = object.value(QStringLiteral("account")).toObject()
                                  .value(QStringLiteral("id"));
        const QString accountId = id.isDouble()
            ? QString::number(static_cast<qint64>(id.toDouble())) : id.toString();
        if (status < 200 || status >= 300 || failed || !ok || accountId.isEmpty()) {
            completion({transportError(status, payload, failed, tooLarge), {}, retryAfter});
            return;
        }
        completion({SimklTransportError::None, accountId, 0});
    });
}

void SimklHttpTransport::postForm(const QUrl &url,
                                  const QList<QPair<QString, QString>> &fields,
                                  const QString &agent,
                                  HttpCompletion completion)
{
    QUrlQuery form;
    for (const auto &field : fields)
        form.addQueryItem(field.first, field.second);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("User-Agent", agent.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network.post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    auto buffer = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [reply, buffer, tooLarge] {
        if (*tooLarge)
            return;
        buffer->append(reply->readAll());
        if (buffer->size() > kMaximumResponseBytes) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, buffer, tooLarge, completion = std::move(completion)]() mutable {
        finishReply(reply, buffer, tooLarge, std::move(completion));
    });
}

void SimklHttpTransport::getJson(const QUrl &url,
                                 const QByteArray &accessToken,
                                 const QString &agent,
                                 HttpCompletion completion)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + accessToken);
    request.setRawHeader("User-Agent", agent.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network.get(request);
    auto buffer = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [reply, buffer, tooLarge] {
        if (*tooLarge)
            return;
        buffer->append(reply->readAll());
        if (buffer->size() > kMaximumResponseBytes) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, buffer, tooLarge, completion = std::move(completion)]() mutable {
        finishReply(reply, buffer, tooLarge, std::move(completion));
    });
}

void SimklHttpTransport::finishReply(QNetworkReply *reply,
                                     const std::shared_ptr<QByteArray> &buffer,
                                     const std::shared_ptr<bool> &tooLarge,
                                     HttpCompletion completion)
{
    if (!reply)
        return;
    buffer->append(reply->readAll());
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool redirected = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
    const bool failed = redirected
        || (reply->error() != QNetworkReply::NoError && status == 0);
    const qint64 retry = retryAfterMs(reply);
    const QByteArray payload = *tooLarge ? QByteArray() : *buffer;
    reply->deleteLater();
    completion(status, payload, retry, failed, *tooLarge);
}
