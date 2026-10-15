#pragma once

#include "TraktAuth.h"

#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

class QNetworkReply;

class TraktHttpTransport final : public QObject, public TraktAuthTransport
{
    Q_OBJECT
public:
    explicit TraktHttpTransport(QObject *parent = nullptr);

    void requestDeviceCode(const TraktAuthConfiguration &configuration, DeviceCompletion completion) override;
    void pollDeviceToken(const TraktAuthConfiguration &configuration, const QByteArray &deviceCode, TokenCompletion completion) override;
    void fetchStableAccountId(const TraktAuthConfiguration &configuration, const QByteArray &accessToken, IdentityCompletion completion) override;

private:
    struct JsonResult {
        int statusCode = 0;
        QJsonDocument document;
        bool networkFailure = false;
        bool payloadTooLarge = false;
    };
    using JsonCompletion = std::function<void(const JsonResult &)>;
    void postJson(const QUrl &url, const QJsonObject &body, const QList<QPair<QByteArray,QByteArray>> &headers, JsonCompletion completion);
    void getJson(const QUrl &url, const QList<QPair<QByteArray,QByteArray>> &headers, JsonCompletion completion);
    void finishReply(QNetworkReply *reply, const std::shared_ptr<QByteArray> &buffer, const std::shared_ptr<bool> &tooLarge, JsonCompletion completion);
    static QList<QPair<QByteArray,QByteArray>> traktHeaders(const TraktAuthConfiguration &configuration, const QByteArray &accessToken = {});
    static QUrl brokerEndpoint(const TraktAuthConfiguration &configuration, const QString &path);

    QNetworkAccessManager m_network;
};
