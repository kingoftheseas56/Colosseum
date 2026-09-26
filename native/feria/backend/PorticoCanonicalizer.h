#pragma once

#include "PorticoTrendTypes.h"

class PorticoCanonicalizer
{
public:
    static QString normalizedText(QString value);
    static QString canonicalKey(const PorticoTrend::Item &item);
    static PorticoTrend::Item decorate(PorticoTrend::Item item);
    static PorticoTrend::Shelf decorate(PorticoTrend::Shelf shelf);
    static QList<PorticoTrend::Item> merge(const QList<PorticoTrend::Item> &items);

private:
    static QString kindGroup(const QString &kind);
    static QString sharedExternalKey(const PorticoTrend::Item &item);
};
