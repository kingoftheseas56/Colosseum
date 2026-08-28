// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountHttpTransport.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

namespace {
constexpr auto kJsonContentType = "application/json";

bool isLoopbackHost(const QString &host) {
    const QString normalized = host.trimmed().toLower();
    return normalized == QLatin1String("localhost")
        || normalized == QLatin1String("127.0.0.1")
        || normalized == QLatin1String("::1");
}
}

AccountHttpTransport::AccountHttpTransport(
    const QUrl &baseUrl,
    QObject *parent)
    : AccountTransport(parent),
      m_baseUrl(baseUrl),
      m_network(this) {
    m_baseUrl.setFragment(QString());
    m_baseUrl.setQuery(QString());

    QString path = m_baseUrl.path();
    if (path.isEmpty())
        path = QStringLiteral("/");
    if (!path.endsWith(QLatin1Char('/')))
        path.append(QLatin1Char('/'));
    m_baseUrl.setPath(path);
}

void AccountHttpTransport::send(
    quint64 requestId,
    const AccountTransportRequest &request) {
    if (!isConfigurationValid()
        || request.path.trimmed().isEmpty()
        || !request.path.startsWith(QLatin1Char('/'))) {
        emitConfigurationError(requestId);
        return;
    }

    const QUrl requestUrl(request.path);
    if (!requestUrl.isValid()
        || !requestUrl.isRelative()
        || requestUrl.hasFragment()) {
        emitConfigurationError(requestId);
        return;
    }

    const QUrl url = m_baseUrl.resolved(requestUrl);
    if (url.host().compare(m_baseUrl.host(), Qt::CaseInsensitive) != 0
        || url.scheme().compare(m_baseUrl.scheme(), Qt::CaseInsensitive) != 0
        || url.port(-1) != m_baseUrl.port(-1)) {
        emitConfigurationError(requestId);
        return;
    }

    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QString::fromLatin1(kJsonContentType));
    networkRequest.setRawHeader(
        QByteArrayLiteral("Cache-Control"),
        QByteArrayLiteral("no-store"));
    networkRequest.setTransferTimeout(
        qMax(1, request.timeoutMs));

    // Business API redirects are not followed. This avoids ever forwarding a
    // bearer token or refresh request to an unexpected redirect target.
    networkRequest.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);

    if (!request.bearerToken.isEmpty()) {
        networkRequest.setRawHeader(
            QByteArrayLiteral("Authorization"),
            QByteArrayLiteral("Bearer ") + request.bearerToken);
    }

    const QByteArray method = request.method.trimmed().toUpper();
    const QByteArray payload = request.body.isEmpty()
        ? QByteArrayLiteral("{}")
        : QJsonDocument(request.body).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = nullptr;
    if (method == QByteArrayLiteral("GET")) {
        reply = m_network.get(networkRequest);
    } else if (method == QByteArrayLiteral("POST")) {
        reply = m_network.post(networkRequest, payload);
    } else if (method == QByteArrayLiteral("PUT")) {
        reply = m_network.put(networkRequest, payload);
    } else if (method == QByteArrayLiteral("DELETE")) {
        reply = m_network.deleteResource(networkRequest);
    } else if (method == QByteArrayLiteral("PATCH")) {
        reply = m_network.sendCustomRequest(
            networkRequest,
            QByteArrayLiteral("PATCH"),
            payload);
    } else {
        emitConfigurationError(requestId);
        return;
    }

    const QPointer<QNetworkReply> guardedReply(reply);
    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, guardedReply, requestId]() {
            if (!guardedReply)
                return;

            const QByteArray payload = guardedReply->readAll();
            const AccountTransportReply decoded = decodeReply(
                guardedReply,
                payload);
            guardedReply->deleteLater();
            emit finished(requestId, decoded);
        });
}

QUrl AccountHttpTransport::baseUrl() const {
    return m_baseUrl;
}

bool AccountHttpTransport::isConfigurationValid() const {
    return isAllowedBaseUrl(m_baseUrl);
}

bool AccountHttpTransport::isAllowedBaseUrl(const QUrl &url) {
    if (!url.isValid()
        || url.host().trimmed().isEmpty()
        || !url.userInfo().isEmpty()) {
        return false;
    }

    const QString scheme = url.scheme().trimmed().toLower();
    if (scheme == QLatin1String("https"))
        return true;

    return scheme == QLatin1String("http")
        && isLoopbackHost(url.host());
}

AccountTransportReply AccountHttpTransport::decodeReply(
    QNetworkReply *reply,
    const QByteArray &payload) const {
    AccountTransportReply result;
    if (!reply) {
        result.networkError = true;
        result.errorCode = QStringLiteral("network_error");
        result.errorMessage = QStringLiteral(
            "The account service could not be reached.");
        return result;
    }

    result.statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        payload,
        &parseError);
    if (parseError.error == QJsonParseError::NoError
        && document.isObject()) {
        result.body = document.object();
    }

    result.networkError = result.statusCode == 0
        && reply->error() != QNetworkReply::NoError;

    if (result.statusCode >= 300 && result.statusCode < 400) {
        result.errorCode = QStringLiteral("redirect_not_allowed");
        result.errorMessage = QStringLiteral(
            "The account service returned an unexpected redirect.");
        return result;
    }

    if (result.statusCode >= 400) {
        const QJsonObject errorObject = result.body
            .value(QStringLiteral("error"))
            .toObject();
        result.errorCode = errorObject
            .value(QStringLiteral("code"))
            .toString();
        result.errorMessage = errorObject
            .value(QStringLiteral("message"))
            .toString();
    } else if (result.networkError) {
        result.errorCode = QStringLiteral("network_error");
        result.errorMessage = QStringLiteral(
            "The account service could not be reached.");
    }

    if (result.statusCode >= 400 && result.errorCode.isEmpty()) {
        result.errorCode = QStringLiteral("service_error");
        result.errorMessage = QStringLiteral(
            "The account request could not be completed.");
    }

    return result;
}

void AccountHttpTransport::emitConfigurationError(quint64 requestId) {
    AccountTransportReply reply;
    reply.errorCode = QStringLiteral("transport_configuration");
    reply.errorMessage = QStringLiteral(
        "The account service configuration is invalid.");

    QTimer::singleShot(
        0,
        this,
        [this, requestId, reply]() {
            emit finished(requestId, reply);
        });
}
