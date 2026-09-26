#include "PorticoDiscoveryService.h"

#include "PorticoCanonicalizer.h"
#include "PorticoTrendModel.h"

PorticoDiscoveryService::PorticoDiscoveryService(QObject *parent)
    : QObject(parent)
{
    m_filter.setSourceModel(m_broker.shelfModel());
    connect(&m_broker, &PorticoTrendBroker::regionChanged,
            this, &PorticoDiscoveryService::regionChanged);
    connect(&m_broker, &PorticoTrendBroker::sourceReady, this,
            [this](const QString &feedId, const QVariantMap &shelf) {
        bumpRevision();
        emit sourceReady(feedId, shelf);
    });
    connect(&m_broker, &PorticoTrendBroker::sourceFailed, this,
            [this](const QString &feedId, const QString &message) {
        bumpRevision();
        emit sourceFailed(feedId, message);
    });
    connect(&m_filter, &QAbstractItemModel::rowsInserted,
            this, &PorticoDiscoveryService::bumpRevision);
    connect(&m_filter, &QAbstractItemModel::rowsRemoved,
            this, &PorticoDiscoveryService::bumpRevision);
    connect(&m_filter, &QAbstractItemModel::modelReset,
            this, &PorticoDiscoveryService::bumpRevision);
    connect(&m_filter, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &) {
        bumpRevision();
    });
}

void PorticoDiscoveryService::setLens(const QString &lens)
{
    const QString before = m_filter.lens();
    m_filter.setLens(lens);
    if (before != m_filter.lens()) {
        bumpRevision();
        emit lensChanged();
    }
}

void PorticoDiscoveryService::setActiveApps(const QStringList &apps)
{
    if (apps == m_activeApps)
        return;
    m_activeApps = apps;
    emit activeAppsChanged();
}

void PorticoDiscoveryService::setRegion(const QString &region)
{
    m_broker.setRegion(region);
}

void PorticoDiscoveryService::configureFeed(
    const QString &feedId, const QString &sourceId, const QVariantMap &config)
{
    m_broker.configureFeed(feedId, sourceId, config);
    bumpRevision();
}
void PorticoDiscoveryService::configureSource(
    const QString &sourceId, const QVariantMap &config)
{
    m_broker.configureSource(sourceId, config);
    bumpRevision();
}

void PorticoDiscoveryService::refreshAll()
{
    m_broker.refreshAll();
}

void PorticoDiscoveryService::refreshFeed(const QString &feedId)
{
    m_broker.refreshFeed(feedId);
}

void PorticoDiscoveryService::refreshSource(const QString &sourceId)
{
    m_broker.refreshSource(sourceId);
}

QVariantList PorticoDiscoveryService::shelves() const
{
    QVariantList result;
    const auto roles = m_filter.roleNames();
    for (int row = 0; row < m_filter.rowCount(); ++row) {
        const QModelIndex idx = m_filter.index(row, 0);
        QVariantMap shelf;
        for (auto it = roles.cbegin(); it != roles.cend(); ++it)
            shelf.insert(QString::fromUtf8(it.value()), idx.data(it.key()));
        result.push_back(shelf);
    }
    return result;
}
QVariantList PorticoDiscoveryService::destinationsFor(const QVariantMap &item) const
{
    return m_destinations.destinationsFor(
        PorticoTrend::itemFromVariantMap(item), m_activeApps);
}

QString PorticoDiscoveryService::canonicalKeyFor(const QVariantMap &item) const
{
    return PorticoCanonicalizer::canonicalKey(
        PorticoTrend::itemFromVariantMap(item));
}

QVariantMap PorticoDiscoveryService::shelf(const QString &feedId) const
{
    return m_broker.shelf(feedId);
}

void PorticoDiscoveryService::bumpRevision()
{
    ++m_revision;
    emit revisionChanged();
}
