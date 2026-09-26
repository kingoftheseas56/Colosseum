#pragma once

#include "PorticoTrendTypes.h"

#include <QStringList>
#include <QVariantList>

class PorticoDestinationResolver
{
public:
    QVariantList destinationsFor(const PorticoTrend::Item &item,
                                 const QStringList &activeApps = {}) const;
    static QString mediumForKind(const QString &kind);
};
