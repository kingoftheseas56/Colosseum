#include "PorticoTrendModel.h"

PorticoTrendModel::PorticoTrendModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PorticoTrendModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant PorticoTrendModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const auto &entry = m_entries.at(index.row());
    const auto &shelf = entry.shelf;
    switch (role) {
    case ShelfIdRole: return shelf.id;
    case TitleRole: return shelf.title;
    case SourceIdRole: return shelf.sourceId;
    case SourceLabelRole: return shelf.sourceLabel;
    case MediumRole: return shelf.medium;
    case RegionRole: return shelf.region;
    case PeriodRole: return shelf.period;
    case CanonicalUrlRole: return shelf.canonicalUrl;
    case StabilityRole: return shelf.stability;
    case ItemsRole: return shelf.toVariantMap().value(QStringLiteral("items"));
    case LoadingRole: return entry.loading;
    case ErrorRole: return entry.error;
    case FetchedAtRole: return shelf.fetchedAt;
    case StaleRole: return shelf.stale;
    case FallbackReasonRole: return shelf.fallbackReason;
    case ProviderIdRole: return shelf.providerId;
    case RankedRole: return shelf.ranked;
    case PriorityRole: return shelf.priority;
    default: return {};
    }
}

QHash<int, QByteArray> PorticoTrendModel::roleNames() const
{
    return {
        {ShelfIdRole, "shelfId"}, {TitleRole, "title"},
        {SourceIdRole, "sourceId"}, {SourceLabelRole, "sourceLabel"},
        {MediumRole, "medium"}, {RegionRole, "region"},
        {PeriodRole, "period"}, {CanonicalUrlRole, "canonicalUrl"},
        {StabilityRole, "stability"}, {ItemsRole, "items"},
        {LoadingRole, "loading"}, {ErrorRole, "error"},
        {FetchedAtRole, "fetchedAt"}, {StaleRole, "stale"},
        {FallbackReasonRole, "fallbackReason"},
        {ProviderIdRole, "providerId"}, {RankedRole, "ranked"},
        {PriorityRole, "priority"}
    };
}
int PorticoTrendModel::indexOf(const QString &feedId) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).shelf.id == feedId)
            return i;
    return -1;
}

void PorticoTrendModel::setLoading(const QString &feedId, const QString &sourceId,
                                   const QString &title, const QString &sourceLabel,
                                   const QString &medium, const QString &stability)
{
    int row = indexOf(feedId);
    if (row < 0) {
        beginInsertRows({}, m_entries.size(), m_entries.size());
        Entry entry;
        entry.shelf.id = feedId;
        entry.shelf.sourceId = sourceId;
        entry.shelf.title = title;
        entry.shelf.sourceLabel = sourceLabel;
        entry.shelf.medium = medium;
        entry.shelf.stability = stability;
        entry.loading = true;
        m_entries.push_back(entry);
        endInsertRows();
        return;
    }
    m_entries[row].loading = true;
    m_entries[row].error.clear();
    emit dataChanged(index(row), index(row), {LoadingRole, ErrorRole});
}

void PorticoTrendModel::setShelf(const PorticoTrend::Shelf &shelf)
{
    const int row = indexOf(shelf.id);
    if (row < 0) {
        beginInsertRows({}, m_entries.size(), m_entries.size());
        m_entries.push_back({shelf, false, {}});
        endInsertRows();
        return;
    }
    m_entries[row].shelf = shelf;
    m_entries[row].loading = false;
    m_entries[row].error.clear();
    emit dataChanged(index(row), index(row));
}

void PorticoTrendModel::setError(const QString &feedId, const QString &message)
{
    const int row = indexOf(feedId);
    if (row < 0)
        return;
    m_entries[row].loading = false;
    m_entries[row].error = message;
    emit dataChanged(index(row), index(row), {LoadingRole, ErrorRole});
}

QVariantMap PorticoTrendModel::shelf(const QString &feedId) const
{
    const int row = indexOf(feedId);
    if (row < 0)
        return {};
    auto map = m_entries.at(row).shelf.toVariantMap();
    map.insert(QStringLiteral("loading"), m_entries.at(row).loading);
    map.insert(QStringLiteral("error"), m_entries.at(row).error);
    return map;
}
