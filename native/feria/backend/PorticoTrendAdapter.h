#pragma once

#include "PorticoTrendTypes.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

class PorticoTrendAdapter : public QObject
{
    Q_OBJECT
public:
    explicit PorticoTrendAdapter(QNetworkAccessManager *network, QObject *parent = nullptr);

    virtual QString id() const = 0;
    virtual QString sourceLabel() const = 0;
    virtual QString medium() const = 0;
    virtual QString stability() const = 0;
    virtual QString defaultTitle() const = 0;
    virtual bool isConfigured(const QVariantMap &config) const;
    virtual void fetch(const PorticoTrend::Query &query, const QVariantMap &config) = 0;

signals:
    void succeeded(const QString &sourceId, const PorticoTrend::Shelf &shelf);
    void failed(const QString &sourceId, const QString &message);

protected:
    QNetworkReply *get(const QUrl &url, const QVariantMap &headers = {});
    QNetworkReply *postJson(const QUrl &url, const QByteArray &body,
                            const QVariantMap &headers = {});
    bool takeReply(QNetworkReply *reply, QByteArray *body, QString *error) const;

    static QString decodeHtml(QString value);
    static QString firstNonEmpty(const QStringList &values);
    static QStringList jsonStringList(const QJsonValue &value);
    static QJsonArray findFirstArrayByKey(const QJsonValue &value, const QString &key);
    static QString lastThumbnailUrl(const QJsonValue &thumbnailValue);
    static int yearFromIsoDate(const QString &value);

    QNetworkAccessManager *m_network = nullptr;
};
