#include "PorticoShelfFilterModel.h"

#include "PorticoShelfPolicy.h"
#include "PorticoTrendModel.h"

PorticoShelfFilterModel::PorticoShelfFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    sort(0);
}

void PorticoShelfFilterModel::setLens(const QString &lens)
{
    const QString next = lens.trimmed().toLower();
    if (next == m_lens)
        return;
    m_lens = next.isEmpty() ? QStringLiteral("all") : next;
    invalidateFilter();
    emit lensChanged();
}

void PorticoShelfFilterModel::setEnabledSources(const QStringList &sources)
{
    if (sources == m_enabledSources)
        return;
    m_enabledSources = sources;
    invalidateFilter();
    emit enabledSourcesChanged();
}

bool PorticoShelfFilterModel::filterAcceptsRow(
    int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    PorticoTrend::Shelf shelf;
    shelf.sourceId = idx.data(PorticoTrendModel::SourceIdRole).toString();
    shelf.medium = idx.data(PorticoTrendModel::MediumRole).toString();
    return PorticoShelfPolicy::accepts(shelf, m_lens, m_enabledSources);
}

bool PorticoShelfFilterModel::lessThan(
    const QModelIndex &left, const QModelIndex &right) const
{
    PorticoTrend::Shelf lhs;
    lhs.sourceId = left.data(PorticoTrendModel::SourceIdRole).toString();
    lhs.priority = left.data(PorticoTrendModel::PriorityRole).toInt();
    PorticoTrend::Shelf rhs;
    rhs.sourceId = right.data(PorticoTrendModel::SourceIdRole).toString();
    rhs.priority = right.data(PorticoTrendModel::PriorityRole).toInt();
    return PorticoShelfPolicy::priority(lhs) < PorticoShelfPolicy::priority(rhs);
}
