#pragma once

#include "PorticoDestinationResolver.h"
#include "PorticoShelfFilterModel.h"
#include "PorticoTrendBroker.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class PorticoDiscoveryService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* shelfModel READ shelfModel CONSTANT)
    Q_PROPERTY(QString lens READ lens WRITE setLens NOTIFY lensChanged)
    Q_PROPERTY(QStringList activeApps READ activeApps WRITE setActiveApps NOTIFY activeAppsChanged)
    Q_PROPERTY(QString region READ region WRITE setRegion NOTIFY regionChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit PorticoDiscoveryService(QObject *parent = nullptr);

    QAbstractItemModel *shelfModel() { return &m_filter; }
    QString lens() const { return m_filter.lens(); }
    QStringList activeApps() const { return m_activeApps; }
    QString region() const { return m_broker.region(); }
    int revision() const { return m_revision; }

    void setLens(const QString &lens);
    void setActiveApps(const QStringList &apps);
    void setRegion(const QString &region);
    void configureFeed(const QString &feedId, const QString &sourceId,
                       const QVariantMap &config = {});
    void configureSource(const QString &sourceId, const QVariantMap &config);

    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void refreshFeed(const QString &feedId);
    Q_INVOKABLE void refreshSource(const QString &sourceId);
    Q_INVOKABLE QVariantList shelves() const;
    Q_INVOKABLE QVariantList destinationsFor(const QVariantMap &item) const;
    Q_INVOKABLE QString canonicalKeyFor(const QVariantMap &item) const;
    Q_INVOKABLE QVariantMap shelf(const QString &feedId) const;

signals:
    void lensChanged();
    void activeAppsChanged();
    void regionChanged();
    void revisionChanged();
    void sourceReady(const QString &sourceId, const QVariantMap &shelf);
    void sourceFailed(const QString &sourceId, const QString &message);

private:
    void bumpRevision();

    PorticoTrendBroker m_broker;
    PorticoShelfFilterModel m_filter;
    PorticoDestinationResolver m_destinations;
    QStringList m_activeApps;
    int m_revision = 0;
};
