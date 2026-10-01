#include "MalHttpTransport.h"

#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <utility>

namespace {

constexpr qsizetype kMaximumResponseBytes = 64 * 1024;
constexpr qsizetype kMaximumTokenBytes = 16 * 1024;
constexpr qint64 kMaximumAccessLifetimeMs = 35LL * 24 * 60 * 60 * 1000;

QUrl apiUrl(const MalAuthConfiguration &configuration,
            const QString &path,
            const QUrlQuery &query = {})
{
    if (!path.startsWith(QLatin1Char('/')) || path.contains(QLatin1String("..")))
        return {};
    QUrl url = configuration.apiEndpoint;
    QString base = url.path();
    if (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    url.setPath(base + path);
    url.setQuery(query);
    return url;
}

qint64 retryAfterMs(const QNetworkReply *reply)
{
    bool ok = false;
    const qint64 seconds =
        reply->rawHeader("Retry-After").trimmed().toLongLong(&ok);
    return ok && seconds > 0 && seconds < 24 * 60 * 60
        ? seconds * 1000 : 0;
}

MalTransportError transportError(int statusCode,
                                 QNetworkReply::NetworkError networkError)
{
    if (statusCode == 401)
        return MalTransportError::AuthenticationRequired;
    if (statusCode == 403)
        return MalTransportError::AccessDenied;
    if (statusCode == 429)
        return MalTransportError::RateLimited;
    if (networkError != QNetworkReply::NoError
        && statusCode == 0) {
        return MalTransportError::NetworkFailure;
    }
    if (statusCode >= 200 && statusCode < 300)
        return MalTransportError::None;
    return MalTransportError::ProtocolFailure;
}

void configureCommonHeaders(QNetworkRequest *request,
                            const MalAuthConfiguration &configuration)
{
    request->setRawHeader("Accept", "application/json");
    request->setRawHeader(
        "User-Agent",
        (configuration.appName + QLatin1Char('/')
         + configuration.appVersion).toUtf8());
    request->setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);
}

QJsonObject tokenBrokerObject(const QUrlQuery &form)
{
    QJsonObject object;
    const auto items = form.queryItems(QUrl::FullyDecoded);
    for (const auto &item : items) {
        if (item.first == QLatin1String("client_id"))
            continue;
        object.insert(item.first, item.second);
    }
    return object;
}

QString jsonStringId(const QJsonValue &value)
{
    if (value.isDouble()) {
        const qint64 id = static_cast<qint64>(value.toDouble());
        return id > 0 ? QString::number(id) : QString();
    }
    const QString id = value.toString().trimmed();
    bool ok = false;
    const qulonglong number = id.toULongLong(&ok);
    return ok && number > 0 ? QString::number(number) : QString();
}

} // namespace

MalHttpTransport::MalHttpTransport(QObject *parent)
    : QObject(parent)
{}

void MalHttpTransport::exchangeAuthorizationCode(
    const MalAuthConfiguration &configuration,
    const QByteArray &authorizationCode,
    const QByteArray &codeVerifier,
    TokenCompletion completion)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), configuration.clientId);
    form.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"),
                      QString::fromLatin1(authorizationCode));
    form.addQueryItem(QStringLiteral("redirect_uri"),
                      configuration.redirectUri.toString(QUrl::FullyEncoded));
    form.addQueryItem(QStringLiteral("code_verifier"),
                      QString::fromLatin1(codeVerifier));
    tokenRequest(configuration, form, std::move(completion));
}

void MalHttpTransport::refreshToken(
    const MalAuthConfiguration &configuration,
    const QByteArray &refreshTokenValue,
    TokenCompletion completion)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), configuration.clientId);
    form.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"),
                      QString::fromLatin1(refreshTokenValue));
    tokenRequest(configuration, form, std::move(completion));
}

void MalHttpTransport::tokenRequest(
    const MalAuthConfiguration &configuration,
    const QUrlQuery &form,
    TokenCompletion completion)
{
    if (!malConfigurationIsValid(configuration)) {
        if (completion)
            completion({});
        return;
    }

    QNetworkRequest request(configuration.usesBroker()
                                ? configuration.tokenBrokerEndpoint
                                : configuration.tokenEndpoint);
    configureCommonHeaders(&request, configuration);

    QByteArray body;
    if (configuration.usesBroker()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
        body = QJsonDocument(tokenBrokerObject(form))
                   .toJson(QJsonDocument::Compact);
    } else {
        request.setHeader(
            QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/x-www-form-urlencoded"));
        // Official MAL docs allow a client with no secret to authenticate
        // through HTTP Basic with an empty password. A confidential secret
        // is never embedded in Colosseum; such registrations use the broker.
        request.setRawHeader(
            "Authorization",
            "Basic " + (configuration.clientId + QLatin1Char(':'))
                            .toUtf8().toBase64());
        body = form.query(QUrl::FullyEncoded).toUtf8();
    }

    QNetworkReply *reply = m_network.post(request, body);
    QObject::connect(reply, &QNetworkReply::finished, this,
        [reply, completion = std::move(completion)]() mutable {
            MalTokenResult result;
            result.statusCode = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray payload = reply->readAll();
            if (payload.size() > kMaximumResponseBytes) {
                result.error = MalTransportError::PayloadTooLarge;
            } else {
                result.error = transportError(result.statusCode,
                                              reply->error());
                if (result.error == MalTransportError::None) {
                    QJsonParseError parseError;
                    const QJsonDocument document =
                        QJsonDocument::fromJson(payload, &parseError);
                    const QJsonObject object = document.object();
                    const QByteArray access =
                        object.value(QStringLiteral("access_token"))
                            .toString().toUtf8();
                    const QByteArray refresh =
                        object.value(QStringLiteral("refresh_token"))
                            .toString().toUtf8();
                    const qint64 seconds =
                        static_cast<qint64>(
                            object.value(QStringLiteral("expires_in"))
                                .toDouble(0));
                    if (parseError.error != QJsonParseError::NoError
                        || access.isEmpty() || refresh.isEmpty()
                        || access.size() > kMaximumTokenBytes
                        || refresh.size() > kMaximumTokenBytes
                        || seconds <= 0
                        || seconds > kMaximumAccessLifetimeMs / 1000) {
                        result.error =
                            MalTransportError::ProtocolFailure;
                    } else {
                        result.accessToken = access;
                        result.refreshToken = refresh;
                        result.accessExpiresInMs = seconds * 1000;
                    }
                }
            }
            reply->deleteLater();
            if (completion)
                completion(result);
        });
}

void MalHttpTransport::fetchIdentity(
    const MalAuthConfiguration &configuration,
    const QByteArray &accessToken,
    IdentityCompletion completion)
{
    get(configuration, accessToken, QStringLiteral("/users/@me"), {},
        [completion = std::move(completion)](
            const MalApiResult &api) mutable {
            MalIdentityResult result;
            result.error = api.error;
            result.statusCode = api.statusCode;
            if (api.succeeded() && api.document.isObject()) {
                const QJsonObject object = api.document.object();
                result.remoteAccountId =
                    jsonStringId(object.value(QStringLiteral("id")));
                result.userName =
                    object.value(QStringLiteral("name"))
                        .toString().simplified().left(128);
                if (result.remoteAccountId.isEmpty())
                    result.error = MalTransportError::ProtocolFailure;
            }
            if (completion)
                completion(result);
        });
}

void MalHttpTransport::get(
    const MalAuthConfiguration &configuration,
    const QByteArray &accessToken,
    const QString &path,
    const QUrlQuery &query,
    ApiCompletion completion)
{
    apiRequest(configuration, accessToken, QByteArrayLiteral("GET"),
               path, query, {}, std::move(completion));
}

void MalHttpTransport::patchForm(
    const MalAuthConfiguration &configuration,
    const QByteArray &accessToken,
    const QString &path,
    const QUrlQuery &form,
    ApiCompletion completion)
{
    apiRequest(configuration, accessToken, QByteArrayLiteral("PATCH"),
               path, {}, form, std::move(completion));
}

void MalHttpTransport::apiRequest(
    const MalAuthConfiguration &configuration,
    const QByteArray &accessToken,
    const QByteArray &method,
    const QString &path,
    const QUrlQuery &query,
    const QUrlQuery &form,
    ApiCompletion completion)
{
    const QUrl url = apiUrl(configuration, path, query);
    if (!url.isValid() || accessToken.isEmpty()
        || accessToken.size() > kMaximumTokenBytes) {
        if (completion)
            completion({});
        return;
    }

    QNetworkRequest request(url);
    configureCommonHeaders(&request, configuration);
    request.setRawHeader("Authorization",
                         "Bearer " + accessToken);
    QNetworkReply *reply = nullptr;
    if (method == QByteArrayLiteral("GET")) {
        reply = m_network.get(request);
    } else {
        request.setHeader(
            QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/x-www-form-urlencoded"));
        reply = m_network.sendCustomRequest(
            request, method,
            form.query(QUrl::FullyEncoded).toUtf8());
    }

    QObject::connect(reply, &QNetworkReply::finished, this,
        [reply, completion = std::move(completion)]() mutable {
            MalApiResult result;
            result.statusCode = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            result.retryAfterMs = retryAfterMs(reply);
            const QByteArray payload = reply->readAll();
            if (payload.size() > kMaximumResponseBytes) {
                result.payloadTooLarge = true;
                result.error = MalTransportError::PayloadTooLarge;
            } else {
                result.error = transportError(result.statusCode,
                                              reply->error());
                result.networkFailure =
                    result.error == MalTransportError::NetworkFailure;
                if (!payload.isEmpty()) {
                    QJsonParseError parseError;
                    result.document =
                        QJsonDocument::fromJson(payload, &parseError);
                    if (parseError.error != QJsonParseError::NoError
                        && result.error == MalTransportError::None) {
                        result.error =
                            MalTransportError::ProtocolFailure;
                    }
                }
            }
            reply->deleteLater();
            if (completion)
                completion(result);
        });
}
