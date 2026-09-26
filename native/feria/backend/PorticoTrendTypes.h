#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PorticoTrend {

struct Query {
    QString region = QStringLiteral("US");
    QString language = QStringLiteral("en");
    QString category;
    int limit = 20;
};

struct Item {
    QString id;
    QString title;
    QString creator;
    QString kind;
    QString imageUrl;
    QString canonicalUrl;
    QString sourceId;
    QString canonicalKey;
    QStringList sourceIds;
    int rank = 0;
    int year = 0;
    QStringList genres;
    QVariantMap externalIds;
    QVariantMap extra;

    QVariantMap toVariantMap() const
    {
        return {
            {QStringLiteral("id"), id},
            {QStringLiteral("title"), title},
            {QStringLiteral("creator"), creator},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("imageUrl"), imageUrl},
            {QStringLiteral("canonicalUrl"), canonicalUrl},
            {QStringLiteral("sourceId"), sourceId},
            {QStringLiteral("canonicalKey"), canonicalKey},
            {QStringLiteral("sourceIds"), sourceIds},
            {QStringLiteral("rank"), rank},
            {QStringLiteral("year"), year},
            {QStringLiteral("genres"), genres},
            {QStringLiteral("externalIds"), externalIds},
            {QStringLiteral("extra"), extra},
        };
    }
};

inline Item itemFromVariantMap(const QVariantMap &map)
{
    Item item;
    item.id = map.value(QStringLiteral("id")).toString();
    item.title = map.value(QStringLiteral("title")).toString();
    item.creator = map.value(QStringLiteral("creator")).toString();
    item.kind = map.value(QStringLiteral("kind")).toString();
    item.imageUrl = map.value(QStringLiteral("imageUrl")).toString();
    item.canonicalUrl = map.value(QStringLiteral("canonicalUrl")).toString();
    item.sourceId = map.value(QStringLiteral("sourceId")).toString();
    item.canonicalKey = map.value(QStringLiteral("canonicalKey")).toString();
    item.sourceIds = map.value(QStringLiteral("sourceIds")).toStringList();
    item.rank = map.value(QStringLiteral("rank")).toInt();
    item.year = map.value(QStringLiteral("year")).toInt();
    item.genres = map.value(QStringLiteral("genres")).toStringList();
    item.externalIds = map.value(QStringLiteral("externalIds")).toMap();
    item.extra = map.value(QStringLiteral("extra")).toMap();
    return item;
}

struct Shelf {
    QString id;
    QString title;
    QString sourceId;
    QString sourceLabel;
    QString medium;
    QString region;
    QString period;
    QString canonicalUrl;
    QString stability;
    QString providerId;
    bool ranked = false;
    int priority = 0;
    QList<Item> items;
    QDateTime fetchedAt = QDateTime::currentDateTimeUtc();
    bool stale = false;
    QString fallbackReason;

    QVariantMap toVariantMap() const
    {
        QVariantList rows;
        rows.reserve(items.size());
        for (const auto &item : items)
            rows.push_back(item.toVariantMap());
        return {
            {QStringLiteral("id"), id},
            {QStringLiteral("title"), title},
            {QStringLiteral("sourceId"), sourceId},
            {QStringLiteral("sourceLabel"), sourceLabel},
            {QStringLiteral("medium"), medium},
            {QStringLiteral("region"), region},
            {QStringLiteral("period"), period},
            {QStringLiteral("canonicalUrl"), canonicalUrl},
            {QStringLiteral("stability"), stability},
            {QStringLiteral("providerId"), providerId},
            {QStringLiteral("ranked"), ranked},
            {QStringLiteral("priority"), priority},
            {QStringLiteral("items"), rows},
            {QStringLiteral("fetchedAt"), fetchedAt},
            {QStringLiteral("stale"), stale},
            {QStringLiteral("fallbackReason"), fallbackReason},
        };
    }
};

inline Shelf shelfFromVariantMap(const QVariantMap &map)
{
    Shelf shelf;
    shelf.id = map.value(QStringLiteral("id")).toString();
    shelf.title = map.value(QStringLiteral("title")).toString();
    shelf.sourceId = map.value(QStringLiteral("sourceId")).toString();
    shelf.sourceLabel = map.value(QStringLiteral("sourceLabel")).toString();
    shelf.medium = map.value(QStringLiteral("medium")).toString();
    shelf.region = map.value(QStringLiteral("region")).toString();
    shelf.period = map.value(QStringLiteral("period")).toString();
    shelf.canonicalUrl = map.value(QStringLiteral("canonicalUrl")).toString();
    shelf.stability = map.value(QStringLiteral("stability")).toString();
    shelf.providerId = map.value(QStringLiteral("providerId")).toString();
    shelf.ranked = map.value(QStringLiteral("ranked")).toBool();
    shelf.priority = map.value(QStringLiteral("priority")).toInt();
    const QVariant fetched = map.value(QStringLiteral("fetchedAt"));
    shelf.fetchedAt = fetched.canConvert<QDateTime>()
        ? fetched.toDateTime()
        : QDateTime::fromString(fetched.toString(), Qt::ISODateWithMs);
    if (!shelf.fetchedAt.isValid())
        shelf.fetchedAt = QDateTime::currentDateTimeUtc();
    shelf.stale = map.value(QStringLiteral("stale")).toBool();
    shelf.fallbackReason = map.value(QStringLiteral("fallbackReason")).toString();
    for (const auto &value : map.value(QStringLiteral("items")).toList())
        shelf.items.push_back(itemFromVariantMap(value.toMap()));
    return shelf;
}

} // namespace PorticoTrend

Q_DECLARE_METATYPE(PorticoTrend::Shelf)
Q_DECLARE_METATYPE(PorticoTrend::Item)
