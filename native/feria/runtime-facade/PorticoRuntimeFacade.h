#pragma once

#include "PorticoContentStore.h"
#include "PorticoDiscoveryService.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class PorticoRuntimeFacade final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* shelfModel READ shelfModel CONSTANT)
    Q_PROPERTY(QString lens READ lens WRITE setLens NOTIFY lensChanged)
    Q_PROPERTY(QStringList activeApps READ activeApps WRITE setActiveApps NOTIFY activeAppsChanged)
    Q_PROPERTY(QString region READ region WRITE setRegion NOTIFY regionChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit PorticoRuntimeFacade(QObject *parent = nullptr);

    QAbstractItemModel *shelfModel() { return m_discovery.shelfModel(); }
    QString lens() const { return m_discovery.lens(); }
    QStringList activeApps() const { return m_discovery.activeApps(); }
    QString region() const { return m_discovery.region(); }
    int revision() const { return m_discovery.revision(); }

    PorticoDiscoveryService *discoveryService() { return &m_discovery; }
    PorticoContentStore *contentStore() { return &m_store; }

    void setLens(const QString &lens);
    void setActiveApps(const QStringList &apps);
    void setRegion(const QString &region);

    Q_INVOKABLE void configureFeed(const QString &feedId, const QString &sourceId,
                                   const QVariantMap &config = {});
    Q_INVOKABLE void configureSource(const QString &sourceId,
                                     const QVariantMap &config = {});
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshFeed(const QString &feedId);
    Q_INVOKABLE void refreshSource(const QString &sourceId);

    Q_INVOKABLE QVariantList shelves() const;
    Q_INVOKABLE QVariantMap shelf(const QString &feedId) const;
    Q_INVOKABLE QVariantMap title(const QString &canonicalKey) const;
    Q_INVOKABLE QStringList search(const QString &query, int limit = 40) const;
    Q_INVOKABLE QVariantList destinationsFor(const QVariantMap &item) const;
    Q_INVOKABLE QString canonicalKeyFor(const QVariantMap &item) const;

signals:
    void lensChanged();
    void activeAppsChanged();
    void regionChanged();
    void revisionChanged();
    void sourceReady(const QString &feedId, const QVariantMap &shelf);
    void sourceFailed(const QString &feedId, const QString &message);

private:
    static QVariantMap normalizedItem(const QVariantMap &item);

    PorticoDiscoveryService m_discovery;
    PorticoContentStore m_store;
};
