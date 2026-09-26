#include "PorticoCanonicalizer.h"

#include <QChar>
#include <QHash>
#include <QRegularExpression>

QString PorticoCanonicalizer::normalizedText(QString value)
{
    value = value.normalized(QString::NormalizationForm_D).toCaseFolded();
    QString out;
    out.reserve(value.size());
    bool pendingSpace = false;
    for (const QChar ch : value) {
        if (ch.category() == QChar::Mark_NonSpacing)
            continue;
        if (ch.isLetterOrNumber()) {
            if (pendingSpace && !out.isEmpty())
                out += QLatin1Char(' ');
            out += ch;
            pendingSpace = false;
        } else {
            pendingSpace = true;
        }
    }
    return out.trimmed();
}

QString PorticoCanonicalizer::kindGroup(const QString &kind)
{
    const QString k = kind.toLower();
    if (k == QStringLiteral("webtoon"))
        return QStringLiteral("comic");
    if (k == QStringLiteral("music-video"))
        return QStringLiteral("song");
    return k;
}
QString PorticoCanonicalizer::sharedExternalKey(const PorticoTrend::Item &item)
{
    static const QStringList sharedIds = {
        QStringLiteral("imdb"),
        QStringLiteral("tmdb"),
        QStringLiteral("isbn"),
        QStringLiteral("isrc"),
        QStringLiteral("upc"),
        QStringLiteral("mal")
    };
    for (const QString &key : sharedIds) {
        const QString value = item.externalIds.value(key).toString().trimmed();
        if (!value.isEmpty())
            return key + QLatin1Char(':') + value.toCaseFolded();
    }
    return {};
}

QString PorticoCanonicalizer::canonicalKey(const PorticoTrend::Item &item)
{
    const QString group = kindGroup(item.kind);
    const QString external = sharedExternalKey(item);
    if (!external.isEmpty())
        return group + QLatin1Char(':') + external;

    QStringList parts{
        group,
        normalizedText(item.title),
        normalizedText(item.creator)
    };
    if (item.year > 0)
        parts.push_back(QString::number(item.year));
    return parts.join(QLatin1Char('|'));
}

PorticoTrend::Item PorticoCanonicalizer::decorate(PorticoTrend::Item item)
{
    item.canonicalKey = canonicalKey(item);
    if (item.sourceIds.isEmpty() && !item.sourceId.isEmpty())
        item.sourceIds.push_back(item.sourceId);
    return item;
}
PorticoTrend::Shelf PorticoCanonicalizer::decorate(PorticoTrend::Shelf shelf)
{
    for (auto &item : shelf.items)
        item = decorate(item);
    return shelf;
}

QList<PorticoTrend::Item> PorticoCanonicalizer::merge(
    const QList<PorticoTrend::Item> &items)
{
    QList<PorticoTrend::Item> merged;
    QHash<QString, int> positions;
    for (auto item : items) {
        item = decorate(item);
        const auto found = positions.constFind(item.canonicalKey);
        if (found == positions.cend()) {
            positions.insert(item.canonicalKey, merged.size());
            merged.push_back(item);
            continue;
        }

        auto &existing = merged[*found];
        for (const QString &source : item.sourceIds)
            if (!existing.sourceIds.contains(source))
                existing.sourceIds.push_back(source);
        for (auto it = item.externalIds.cbegin(); it != item.externalIds.cend(); ++it)
            if (!existing.externalIds.contains(it.key()))
                existing.externalIds.insert(it.key(), it.value());
        if (existing.imageUrl.isEmpty())
            existing.imageUrl = item.imageUrl;
        if (existing.canonicalUrl.isEmpty())
            existing.canonicalUrl = item.canonicalUrl;
        if (existing.creator.isEmpty())
            existing.creator = item.creator;
        if (existing.year <= 0)
            existing.year = item.year;
        if (item.rank > 0 && (existing.rank <= 0 || item.rank < existing.rank))
            existing.rank = item.rank;
    }
    return merged;
}
