#pragma once

#include "PorticoTrendTypes.h"

#include <QStringList>

class PorticoShelfPolicy
{
public:
    static bool accepts(const PorticoTrend::Shelf &shelf,
                        const QString &lens,
                        const QStringList &enabledSources = {});
    static int priority(const PorticoTrend::Shelf &shelf);
};
