#include "LocalDownloadsProjection.h"

#include "TankoyomiIdentity.h"

#include <QHash>
#include <QStringList>

#include <algorithm>

namespace {

bool isGenericChapterLabel(const QString &label)
{
    const QString normalized = label.trimmed().toLower();
    return normalized.isEmpty()
        || normalized == QLatin1String("chapter")
        || normalized == QStringLiteral("capítulo")
        || normalized == QLatin1String("capitulo")
        || normalized == QLatin1String("chapitre");
}

QString countLabel(int count, const QString &singular, const QString &plural)
{
    return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? singular : plural);
}

int artPriority(const QVariantMap &item)
{
    const QString kind = LocalDownloadsProjection::itemKind(item);
    if (kind == QLatin1String("volume")) return 30;
    if (kind == QLatin1String("chapter") || kind == QLatin1String("issue")) return 20;
    return 10;
}

QString unitText(const QVariantMap &series)
{
    QStringList parts;
    const int chapters = series.value(QStringLiteral("chapterCount")).toInt();
    const int volumes = series.value(QStringLiteral("volumeCount")).toInt();
    const int issues = series.value(QStringLiteral("issueCount")).toInt();
    const int editions = series.value(QStringLiteral("editionCount")).toInt();
    const int episodes = series.value(QStringLiteral("episodeCount")).toInt();
    const int films = series.value(QStringLiteral("filmCount")).toInt();

    if (chapters > 0) parts.append(countLabel(chapters, QStringLiteral("chapter"), QStringLiteral("chapters")));
    if (volumes > 0) parts.append(countLabel(volumes, QStringLiteral("volume"), QStringLiteral("volumes")));
    if (issues > 0) parts.append(countLabel(issues, QStringLiteral("issue"), QStringLiteral("issues")));
    if (editions > 0) parts.append(countLabel(editions, QStringLiteral("edition"), QStringLiteral("editions")));
    if (episodes > 0) parts.append(countLabel(episodes, QStringLiteral("episode"), QStringLiteral("episodes")));
    if (films > 0) parts.append(countLabel(films, QStringLiteral("film"), QStringLiteral("films")));

    if (!parts.isEmpty()) return parts.join(QStringLiteral(" · "));
    const int items = series.value(QStringLiteral("itemCount")).toInt();
    return countLabel(items, QStringLiteral("item"), QStringLiteral("items"));
}

} // namespace
QString LocalDownloadsProjection::itemKind(const QVariantMap &item)
{
    const QString explicitKind = item.value(QStringLiteral("itemKind")).toString().trimmed().toLower();
    if (!explicitKind.isEmpty()) return explicitKind;

    const QString id = item.value(QStringLiteral("id")).toString();
    const QString kind = item.value(QStringLiteral("kind")).toString().trimmed().toLower();
    const QString world = item.value(QStringLiteral("world")).toString().trimmed().toLower();

    if (id.startsWith(QStringLiteral("tankoban:"), Qt::CaseInsensitive)
        && id.contains(QStringLiteral(":volume:"), Qt::CaseInsensitive)) {
        return QStringLiteral("volume");
    }
    if (TankoyomiIdentity::isQualifiedChapter(id)) return QStringLiteral("chapter");
    if (kind == QLatin1String("manga")) return QStringLiteral("chapter");
    if (kind == QLatin1String("comic")) return QStringLiteral("issue");
    if (kind == QLatin1String("book")) return QStringLiteral("edition");
    if (kind == QLatin1String("episode")) return QStringLiteral("episode");
    if (kind == QLatin1String("movie") || kind == QLatin1String("film"))
        return QStringLiteral("film");
    if (world == QLatin1String("theatre")) {
        if (item.value(QStringLiteral("season")).toInt() > 0
            || item.value(QStringLiteral("episode")).toInt() > 0) {
            return QStringLiteral("episode");
        }
        return QStringLiteral("film");
    }
    return QStringLiteral("item");
}

QString LocalDownloadsProjection::chapterLabel(const QString &chapterId,
                                               const QString &storedLabel)
{
    const QString stored = storedLabel.trimmed();
    if (!isGenericChapterLabel(stored)) return stored;

    const auto parsed = TankoyomiIdentity::parseChapter(chapterId);
    if (parsed) {
        for (const QString &key : {QStringLiteral("label"), QStringLiteral("title"),
                                   QStringLiteral("name")}) {
            const QString candidate = parsed->chapter.value(key).toString().trimmed();
            if (!candidate.isEmpty() && !isGenericChapterLabel(candidate)) return candidate;
        }
        const QVariant number = parsed->chapter.value(QStringLiteral("number"));
        if (number.isValid() && !number.isNull()) {
            const QString value = number.toString().trimmed();
            if (!value.isEmpty()) return QStringLiteral("Chapter %1").arg(value);
        }
    }
    return stored.isEmpty() ? QStringLiteral("Chapter") : stored;
}

bool LocalDownloadsProjection::canRedownload(const QVariantMap &item)
{
    const QString world = item.value(QStringLiteral("world")).toString().trimmed().toLower();
    if (world == QLatin1String("theatre") || world == QLatin1String("biblio")) return true;
    return world == QLatin1String("tankoban") && itemKind(item) == QLatin1String("chapter");
}
QVariantList LocalDownloadsProjection::aggregateSeries(const QVariantList &items,
                                                       const QString &world)
{
    QHash<QString, QVariantMap> aggregates;
    QStringList order;

    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString key = item.value(QStringLiteral("seriesKey")).toString();
        if (key.isEmpty()) continue;

        auto it = aggregates.find(key);
        if (it == aggregates.end()) {
            order.append(key);
            QString title = item.value(QStringLiteral("seriesTitle")).toString();
            if (title.isEmpty()) title = item.value(QStringLiteral("title")).toString();
            it = aggregates.insert(key, QVariantMap{
                {QStringLiteral("key"), key},
                {QStringLiteral("world"), world},
                {QStringLiteral("title"), title},
                {QStringLiteral("kind"), item.value(QStringLiteral("kind"))},
                {QStringLiteral("itemCount"), 0},
                {QStringLiteral("chapterCount"), 0},
                {QStringLiteral("volumeCount"), 0},
                {QStringLiteral("issueCount"), 0},
                {QStringLiteral("editionCount"), 0},
                {QStringLiteral("episodeCount"), 0},
                {QStringLiteral("filmCount"), 0},
                {QStringLiteral("bytes"), 0.0},
                {QStringLiteral("updatedAt"), 0.0},
                {QStringLiteral("art"), QString()},
                {QStringLiteral("artPriority"), -1}
            });
        }

        QVariantMap &series = it.value();
        series[QStringLiteral("itemCount")] =
            series.value(QStringLiteral("itemCount")).toInt() + 1;
        series[QStringLiteral("bytes")] =
            series.value(QStringLiteral("bytes")).toDouble()
            + item.value(QStringLiteral("bytes")).toDouble();
        series[QStringLiteral("updatedAt")] =
            qMax(series.value(QStringLiteral("updatedAt")).toDouble(),
                 item.value(QStringLiteral("addedAt")).toDouble());

        const QString itemArt = item.value(QStringLiteral("art")).toString();
        const int priority = artPriority(item);
        if (!itemArt.isEmpty()
            && priority > series.value(QStringLiteral("artPriority")).toInt()) {
            series[QStringLiteral("art")] = itemArt;
            series[QStringLiteral("artPriority")] = priority;
        }

        const QString subtype = itemKind(item);
        const QString countKey = subtype == QLatin1String("chapter")
            ? QStringLiteral("chapterCount")
            : subtype == QLatin1String("volume") ? QStringLiteral("volumeCount")
            : subtype == QLatin1String("issue") ? QStringLiteral("issueCount")
            : subtype == QLatin1String("edition") ? QStringLiteral("editionCount")
            : subtype == QLatin1String("episode") ? QStringLiteral("episodeCount")
            : subtype == QLatin1String("film") ? QStringLiteral("filmCount")
            : QString();
        if (!countKey.isEmpty())
            series[countKey] = series.value(countKey).toInt() + 1;
    }

    QVariantList result;
    result.reserve(order.size());
    for (const QString &key : order) {
        QVariantMap series = aggregates.value(key);
        series.remove(QStringLiteral("artPriority"));
        series.insert(QStringLiteral("unitText"), unitText(series));
        result.append(series);
    }
    std::sort(result.begin(), result.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("updatedAt")).toDouble()
            > right.toMap().value(QStringLiteral("updatedAt")).toDouble();
    });
    return result;
}
