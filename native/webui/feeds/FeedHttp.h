#pragma once

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace WebFeedHttp {

struct Reply {
    QJsonDocument json;
    QString error;
    bool ok = false;
};

// Constructed and run inside a feed worker. QNetworkAccessManager and its
// QEventLoop never cross the GUI thread; the registry's request version drops
// results from subscriptions superseded while this transport is outstanding.
inline Reply request(const QUrl &url, const QByteArray &body = {}, int timeoutMs = 7000)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Colosseum/1.0 (native web feed)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nullptr;
    if (body.isEmpty()) {
        reply = manager.get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
        reply = manager.post(request, body);
    }
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    if (!reply->isFinished()) loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        return {{}, QStringLiteral("provider timed out"), false};
    }
    if (reply->error() != QNetworkReply::NoError) {
        return {{}, QStringLiteral("provider unavailable"), false};
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || json.isNull()) {
        return {{}, QStringLiteral("provider returned invalid data"), false};
    }
    return {json, {}, true};
}

} // namespace WebFeedHttp
