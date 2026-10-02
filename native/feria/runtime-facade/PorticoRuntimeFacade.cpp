#include "PorticoRuntimeFacade.h"

PorticoRuntimeFacade::PorticoRuntimeFacade(QObject *parent)
    : QObject(parent)
{
    connect(&m_discovery, &PorticoDiscoveryService::lensChanged,
            this, &PorticoRuntimeFacade::lensChanged);
    connect(&m_discovery, &PorticoDiscoveryService::activeAppsChanged,
            this, &PorticoRuntimeFacade::activeAppsChanged);
    connect(&m_discovery, &PorticoDiscoveryService::regionChanged,
            this, &PorticoRuntimeFacade::regionChanged);
    connect(&m_discovery, &PorticoDiscoveryService::revisionChanged,
            this, &PorticoRuntimeFacade::revisionChanged);

    connect(&m_discovery, &PorticoDiscoveryService::sourceReady,
            this, [this](const QString &feedId, const QVariantMap &shelf) {
        m_store.ingestShelf(feedId, shelf);
        emit sourceReady(feedId, shelf);
    });
    connect(&m_discovery, &PorticoDiscoveryService::sourceFailed,
            this, &PorticoRuntimeFacade::sourceFailed);
}

void PorticoRuntimeFacade::setLens(const QString &lens)
{
    m_discovery.setLens(lens);
}

void PorticoRuntimeFacade::setActiveApps(const QStringList &apps)
{
    m_discovery.setActiveApps(apps);
}

void PorticoRuntimeFacade::setRegion(const QString &region)
{
    m_discovery.setRegion(region);
}

void PorticoRuntimeFacade::configureFeed(
    const QString &feedId, const QString &sourceId, const QVariantMap &config)
{
    m_discovery.configureFeed(feedId, sourceId, config);
}

void PorticoRuntimeFacade::configureSource(
    const QString &sourceId, const QVariantMap &config)
{
    m_discovery.configureSource(sourceId, config);
}

void PorticoRuntimeFacade::refresh()
{
    m_discovery.refreshAll();
}

void PorticoRuntimeFacade::refreshFeed(const QString &feedId)
{
    m_discovery.refreshFeed(feedId);
}

void PorticoRuntimeFacade::refreshSource(const QString &sourceId)
{
    m_discovery.refreshSource(sourceId);
}

QVariantList PorticoRuntimeFacade::shelves() const
{
    return m_discovery.shelves();
}

QVariantMap PorticoRuntimeFacade::shelf(const QString &feedId) const
{
    return m_discovery.shelf(feedId);
}

QVariantMap PorticoRuntimeFacade::title(const QString &canonicalKey) const
{
    return m_store.title(canonicalKey);
}

QStringList PorticoRuntimeFacade::search(const QString &query, int limit) const
{
    return m_store.search(query, limit);
}

QVariantList PorticoRuntimeFacade::destinationsFor(const QVariantMap &item) const
{
    return m_discovery.destinationsFor(normalizedItem(item));
}

QString PorticoRuntimeFacade::canonicalKeyFor(const QVariantMap &item) const
{
    return m_discovery.canonicalKeyFor(normalizedItem(item));
}

QVariantMap PorticoRuntimeFacade::normalizedItem(const QVariantMap &item)
{
    const QVariantMap trend = item.value(QStringLiteral("_trend")).toMap();
    return trend.isEmpty() ? item : trend;
}
