#pragma once

#include <QObject>
#include <QHash>
#include <QNetworkAccessManager>
#include <QVariantMap>

class PorticoAvailabilityService final : public QObject
{
    Q_OBJECT
public:
    explicit PorticoAvailabilityService(QObject *parent = nullptr);
    QVariantMap lookup(const QVariantMap &item, const QString &region) const;
    void request(const QVariantMap &item, const QString &region, bool force = false);
    static QVariantMap parse(const QVariantMap &item, const QString &region, const QByteArray &body);
signals:
    void changed();
private:
    static QString key(const QVariantMap &item, const QString &region);
    QNetworkAccessManager m_network;
    QHash<QString, QVariantMap> m_cache;
};
