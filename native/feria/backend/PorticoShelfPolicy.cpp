#include "PorticoShelfPolicy.h"

#include <QHash>

bool PorticoShelfPolicy::accepts(const PorticoTrend::Shelf &shelf,
                                 const QString &lens,
                                 const QStringList &enabledSources)
{
    const QString normalizedLens = lens.trimmed().toLower();
    if (!enabledSources.isEmpty() && !enabledSources.contains(shelf.sourceId))
        return false;
    if (normalizedLens.isEmpty() || normalizedLens == QStringLiteral("all"))
        return true;
    return shelf.medium == normalizedLens;
}

int PorticoShelfPolicy::priority(const PorticoTrend::Shelf &shelf)
{
    if (shelf.priority != 0)
        return shelf.priority;
    static const QHash<QString, int> order = {
        {QStringLiteral("stremio"), 100},
        {QStringLiteral("spotify"), 200},
        {QStringLiteral("youtube"), 210},
        {QStringLiteral("applemusic"), 220},
        {QStringLiteral("openlibrary"), 300},
        {QStringLiteral("anilist"), 310},
        {QStringLiteral("webtoon"), 320},
        {QStringLiteral("globalcomix"), 330}
    };
    return order.value(shelf.sourceId, 1000);
}
