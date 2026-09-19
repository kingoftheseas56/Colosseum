#include "StremioCodec.h"

#include <QHostAddress>
#include <QJsonValue>
#include <QUrlQuery>

namespace {
constexpr qsizetype kMaximumCallbackBytes = 16 * 1024;
constexpr qsizetype kMaximumCredentialBytes = 4096;
constexpr qsizetype kMaximumIdentityText = 256;

bool safeIdentityText(const QString &value) {
    return !value.isEmpty()
        && value.size() <= kMaximumIdentityText
        && value.trimmed() == value
        && !value.contains(QChar::ReplacementCharacter);
}

bool safeCredential(const QByteArray &value) {
    if (value.isEmpty() || value.size() > kMaximumCredentialBytes)
        return false;
    for (const char byte : value) {
        if (static_cast<unsigned char>(byte) < 0x21
            || static_cast<unsigned char>(byte) == 0x7f) {
            return false;
        }
    }
    return true;
}
}

StremioLoopbackCallback StremioCodec::decodeLoopbackCallback(
    const QByteArray &request,
    const QString &expectedPath) {
    StremioLoopbackCallback result;
    if (request.isEmpty() || request.size() > kMaximumCallbackBytes
        || expectedPath.isEmpty() || !expectedPath.startsWith(QLatin1Char('/'))) {
        result.error = QStringLiteral("The callback request is invalid.");
        return result;
    }

    const qsizetype lineEnd = request.indexOf("\r\n");
    if (lineEnd <= 0) {
        result.error = QStringLiteral("The callback request line is invalid.");
        return result;
    }
    const QList<QByteArray> parts = request.left(lineEnd).split(' ');
    if (parts.size() != 3 || parts.at(0) != "GET" || !parts.at(2).startsWith("HTTP/")) {
        result.error = QStringLiteral("The callback method is invalid.");
        return result;
    }

    const QUrl callback = QUrl::fromEncoded(
        QByteArrayLiteral("http://127.0.0.1") + parts.at(1));
    if (!callback.isValid() || callback.path(QUrl::FullyEncoded) != expectedPath) {
        result.error = QStringLiteral("The callback correlation path does not match.");
        return result;
    }

    const QUrlQuery query(callback);
    const QList<QString> keys = query.allQueryItemValues(QStringLiteral("key"));
    const QList<QString> authKeys = query.allQueryItemValues(QStringLiteral("authKey"));
    if (keys.size() + authKeys.size() != 1) {
        result.error = QStringLiteral("The callback credential is ambiguous.");
        return result;
    }

    const QString credential = keys.isEmpty() ? authKeys.first() : keys.first();
    const QByteArray bytes = credential.toUtf8();
    if (!safeCredential(bytes)) {
        result.error = QStringLiteral("The callback credential is invalid.");
        return result;
    }

    result.accepted = true;
    result.authKey = bytes;
    return result;
}

bool StremioCodec::isProductionEndpoint(const QUrl &endpoint) {
    if (!endpoint.isValid() || endpoint.scheme() != QStringLiteral("https")
        || endpoint.host().toLower() != QStringLiteral("api.strem.io")
        || endpoint.path(QUrl::FullyEncoded) != QStringLiteral("/api")
        || !endpoint.userInfo().isEmpty() || endpoint.hasQuery() || endpoint.hasFragment()
        || (endpoint.port() != -1 && endpoint.port() != 443)) {
        return false;
    }
    return true;
}

bool StremioCodec::isTaggedLoopbackEndpoint(const QUrl &endpoint) {
    if (qEnvironmentVariableIsEmpty("COLOSSEUM_APPDATA_TAG")
        || !endpoint.isValid() || endpoint.scheme() != QStringLiteral("http")) {
        return false;
    }
    return endpoint.host() == QStringLiteral("127.0.0.1")
        && endpoint.port() > 0;
}

QUrl StremioCodec::browserLoginUrl(const QUrl &callback) {
    QUrl result(QStringLiteral("https://www.stremio.com/login"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("appName"), QStringLiteral("Colosseum"));
    query.addQueryItem(QStringLiteral("appCallback"), callback.toString(QUrl::FullyEncoded));
    result.setQuery(query);
    return result;
}

bool StremioCodec::decodeGetUserResult(
    const QJsonObject &response,
    StremioAccountIdentity *identity,
    QString *error) {
    if (!identity) {
        if (error)
            *error = QStringLiteral("A Stremio identity output is required.");
        return false;
    }
    const QJsonValue result = response.value(QStringLiteral("result"));
    if (!result.isObject()) {
        if (error)
            *error = QStringLiteral("The Stremio identity response is malformed.");
        return false;
    }
    const QJsonObject object = result.toObject();
    const QString accountId = object.value(QStringLiteral("_id")).toString().trimmed();
    QString displayName = object.value(QStringLiteral("fullname")).toString().trimmed();
    if (displayName.isEmpty())
        displayName = object.value(QStringLiteral("email")).toString().trimmed();
    if (!safeIdentityText(accountId) || !safeIdentityText(displayName)) {
        if (error)
            *error = QStringLiteral("The Stremio identity response is invalid.");
        return false;
    }
    identity->accountId = accountId;
    identity->displayName = displayName;
    return true;
}
