#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace WebFeedValue {

inline QVariantMap jsonMap(const QVariantMap &value)
{
    return QJsonObject::fromVariantMap(value).toVariantMap();
}

inline QVariantMap item(const QVariantMap &row, const QString &world,
                        const QString &fallbackKind = QString(), bool isContinue = false)
{
    const QString storedKind = row.value(QStringLiteral("kind")).toString();
    QString kind = fallbackKind.isEmpty() ? storedKind : fallbackKind;
    const QString id = row.value(QStringLiteral("id")).toString();
    const QString tt = row.value(QStringLiteral("tt")).toString();
    const QString mal = row.value(QStringLiteral("mal_id"), row.value(QStringLiteral("malId"))).toString();
    const QString gcd = row.value(QStringLiteral("gcdId")).toString();
    const QString locg = row.value(QStringLiteral("locgId")).toString();
    const QString source = row.value(QStringLiteral("source")).toString();
    const QString type = row.value(QStringLiteral("type")).toString();
    if (kind == QLatin1String("video"))
        kind = type == QLatin1String("series") ? QStringLiteral("series") : QStringLiteral("movie");
    else if (kind == QLatin1String("comics"))
        kind = QStringLiteral("comic");
    else if (kind == QLatin1String("tankoban"))
        kind = QStringLiteral("manga");
    else if (kind.isEmpty())
        kind = world == QLatin1String("Biblio") ? QStringLiteral("book")
             : world == QLatin1String("Tankoban") ? QStringLiteral("manga")
             : type == QLatin1String("series") ? QStringLiteral("series") : QStringLiteral("movie");

    QString identity;
    if (!tt.isEmpty() || id.startsWith(QLatin1String("tt")))
        identity = QStringLiteral("imdb:") + (tt.isEmpty() ? id : tt);
    else if (!mal.isEmpty())
        identity = QStringLiteral("mal:") + mal;
    else if (!locg.isEmpty())
        identity = QStringLiteral("locg:") + locg;
    else if (!gcd.isEmpty())
        identity = QStringLiteral("gcd:") + gcd;
    else if (id.startsWith(QLatin1String("gc:")) || id.startsWith(QLatin1String("gcd:"))
             || id.startsWith(QLatin1String("locg:")) || id.startsWith(QLatin1String("mal:")))
        identity = id;
    else
        identity = (source.isEmpty() ? QStringLiteral("source") : source) + QLatin1Char(':') + id;

    QString group = id;
    if (storedKind == QLatin1String("video") && id.count(QLatin1Char(':')) >= 2) {
        const QStringList parts = id.split(QLatin1Char(':'));
        group = id.startsWith(QLatin1String("tt")) ? parts.value(0)
              : parts.value(0) + QLatin1Char(':') + parts.value(1);
    }
    const QString groupKey = storedKind + QLatin1Char(':') + group;
    QVariantMap ref;
    for (const QString &field : {QStringLiteral("kind"), QStringLiteral("id"),
                                 QStringLiteral("libraryId"), QStringLiteral("tt"),
                                 QStringLiteral("mal_id"), QStringLiteral("malId"),
                                 QStringLiteral("gcdId"), QStringLiteral("locgId"),
                                 QStringLiteral("seriesId"), QStringLiteral("type"),
                                 QStringLiteral("source"), QStringLiteral("resume"),
                                 QStringLiteral("unitId"), QStringLiteral("chapterId"),
                                 QStringLiteral("episodeId")}) {
        if (row.contains(field))
            ref.insert(field, row.value(field));
    }
    if (isContinue)
        ref.insert(QStringLiteral("continueGroupKey"), groupKey);

    QVariantMap out{
        {QStringLiteral("key"), isContinue ? QStringLiteral("continue:") + groupKey
                                          : world + QLatin1Char(':') + identity},
        {QStringLiteral("world"), world},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("title"), row.value(QStringLiteral("title"), row.value(QStringLiteral("title_english"))).toString()},
        {QStringLiteral("ref"), jsonMap(ref)}
    };
    QString cover = row.value(QStringLiteral("cover"), row.value(QStringLiteral("coverUrl"))).toString();
    if (cover.isEmpty())
        cover = row.value(QStringLiteral("images")).toMap().value(QStringLiteral("jpg")).toMap()
                    .value(QStringLiteral("large_image_url")).toString();
    if (cover.isEmpty() && identity.startsWith(QLatin1String("imdb:")))
        cover = QStringLiteral("https://images.metahub.space/poster/small/%1/img")
                    .arg(identity.mid(5));
    if (!cover.isEmpty()) out.insert(QStringLiteral("cover"), cover);
    const QString backdrop = row.value(QStringLiteral("backdrop")).toString();
    if (!backdrop.isEmpty()) out.insert(QStringLiteral("backdrop"), backdrop);
    const QString subtitle = row.value(QStringLiteral("subtitle")).toString();
    if (!subtitle.isEmpty()) out.insert(QStringLiteral("subtitle"), subtitle);
    if (row.value(QStringLiteral("year")).toInt() > 0)
        out.insert(QStringLiteral("year"), row.value(QStringLiteral("year")).toInt());
    const QVariant rating = row.value(QStringLiteral("rating"), row.value(QStringLiteral("score")));
    if (rating.isValid() && !rating.isNull()) out.insert(QStringLiteral("rating"), rating.toDouble());
    const QVariant progress = row.value(QStringLiteral("progress"));
    if (progress.isValid() && !progress.isNull())
        out.insert(QStringLiteral("progress"), qBound(0.0, progress.toDouble(), 1.0));
    return out;
}

inline QVariantMap section(const QString &id, int index, const QString &title,
                           const QString &layout, const QVariantList &items,
                           const QString &state = QStringLiteral("ready"), bool hasMore = false)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("index"), index},
            {QStringLiteral("title"), title}, {QStringLiteral("layout"), layout},
            {QStringLiteral("state"), state}, {QStringLiteral("items"), items},
            {QStringLiteral("hasMore"), hasMore}};
}

} // namespace WebFeedValue
