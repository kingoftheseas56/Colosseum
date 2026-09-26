#include "FeedRegistry.h"
#include "FeedChoice.h"
#include "FeedHttp.h"
#include "FeedValue.h"
#include "SearchFeed.h"
#include "ActionRegistry.h"
#include "../ColosseumWebBridge.h"

#include "../../engine/ComicsCatalog.h"
#include "../../engine/ImdbCatalog.h"
#include "../../engine/MalCatalog.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QFutureWatcher>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrentRun>

namespace {
bool valid(const QVariantMap &params)
{
    return QStringList{QStringLiteral("all"), QStringLiteral("Tankoban"),
                       QStringLiteral("Biblio"), QStringLiteral("Theatre")}
               .contains(params.value(QStringLiteral("scope")).toString())
        && params.value(QStringLiteral("query")).toString().size() <= 200;
}

QStringList genreNames(const QString &scope)
{
    if (scope == QLatin1String("Biblio"))
        return {QStringLiteral("Fiction & Literature"), QStringLiteral("Mysteries & Thrillers"),
                QStringLiteral("Sci-Fi & Fantasy"), QStringLiteral("Romance"),
                QStringLiteral("History"), QStringLiteral("Young Adult")};
    if (scope == QLatin1String("Theatre"))
        return {QStringLiteral("Action"), QStringLiteral("Adventure"), QStringLiteral("Animation"),
                QStringLiteral("Comedy"), QStringLiteral("Crime"), QStringLiteral("Documentary"),
                QStringLiteral("Drama"), QStringLiteral("Fantasy"), QStringLiteral("Horror"),
                QStringLiteral("Mystery"), QStringLiteral("Romance"), QStringLiteral("Sci-Fi")};
    // WorldSearch.js:272-279; all uses the Tankoban recommendations.
    return {QStringLiteral("Action"), QStringLiteral("Adventure"), QStringLiteral("Comedy"),
            QStringLiteral("Drama"), QStringLiteral("Fantasy"), QStringLiteral("Horror"),
            QStringLiteral("Mystery"), QStringLiteral("Psychological"), QStringLiteral("Romance"),
            QStringLiteral("Sci-Fi"), QStringLiteral("Sports"), QStringLiteral("Supernatural")};
}

QVariantList idle(const FeedContext &ctx)
{
    // SearchSurface.qml:401-496: recent query chips, genre chips, surprise door.
    const QString scope = ctx.params.value(QStringLiteral("scope")).toString();
    QVariantList result;
    QVariantList recent;
    for (const QString &query : ctx.history)
        recent.append(WebFeedChoice::choice(QStringLiteral("search:recent:") + scope + QLatin1Char(':') + query,
            query, {{QStringLiteral("query"), query}}, {}, {}, true));
    if (!recent.isEmpty())
        result.append(WebFeedChoice::section(QStringLiteral("search.recent"), 0,
            QStringLiteral("Recent searches"), QStringLiteral("chips"), recent));

    QVariantList genreChoices;
    const QString world = scope == QLatin1String("all") ? QStringLiteral("Tankoban") : scope;
    const QString medium = world == QLatin1String("Theatre") ? QStringLiteral("movie")
                         : world == QLatin1String("Biblio") ? QStringLiteral("book")
                         : QStringLiteral("manga");
    const QStringList names = genreNames(scope);
    const QStringList biblioIds{QStringLiteral("9031"), QStringLiteral("9032"),
        QStringLiteral("9020"), QStringLiteral("9003"), QStringLiteral("9015"), QStringLiteral("11165")};
    for (int i = 0; i < names.size(); ++i) {
        QVariantMap facet{{QStringLiteral("medium"), medium}, {QStringLiteral("name"), names.at(i)}};
        if (world == QLatin1String("Biblio")) facet.insert(QStringLiteral("key"), biblioIds.at(i));
        genreChoices.append(WebFeedChoice::choice(
            QStringLiteral("search:genre:") + world + QLatin1Char(':') + names.at(i), names.at(i),
            {{QStringLiteral("route"), WebFeedChoice::route(world, QStringLiteral("genre"), facet,
                QStringLiteral("popular"), ctx.showExplicit, 24)}}));
    }
    result.append(WebFeedChoice::section(QStringLiteral("search.genres"), result.size(),
        QStringLiteral("Try a genre"), QStringLiteral("chips"), genreChoices));
    result.append(WebFeedChoice::section(QStringLiteral("search.surprise"), result.size(),
        QStringLiteral("Surprise me"), QStringLiteral("chips"),
        {WebFeedChoice::choice(QStringLiteral("search:surprise"), QStringLiteral("Surprise me"),
            {{QStringLiteral("act"), QStringLiteral("search.surprise")}})}));
    return result;
}

QVariantMap theatreItem(const QJsonObject &meta, const QString &kind)
{
    const QString id = meta.value(QStringLiteral("id")).toString();
    QVariantMap row{{QStringLiteral("tt"), id},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("type"), kind},
                    {QStringLiteral("title"), meta.value(QStringLiteral("name")).toString()},
                    {QStringLiteral("cover"), meta.value(QStringLiteral("poster")).toString()},
                    {QStringLiteral("backdrop"), meta.value(QStringLiteral("background")).toString()}};
    const QVariantMap item = WebFeedValue::item(row, QStringLiteral("Theatre"), kind);
    return item;
}

QVariantList theatre(const QString &query, bool *failed)
{
    QVariantList items;
    for (const QString &kind : {QStringLiteral("movie"), QStringLiteral("series")}) {
        const QUrl url(QStringLiteral("https://v3-cinemeta.strem.io/catalog/%1/top/search=%2.json")
            .arg(kind, QString::fromUtf8(QUrl::toPercentEncoding(query))));
        const auto reply = WebFeedHttp::request(url);
        if (!reply.ok) { *failed = true; continue; }
        int count = 0;
        for (const QJsonValue &value : reply.json.object().value(QStringLiteral("metas")).toArray()) {
            if (count++ >= 16) break;
            const QVariantMap item = theatreItem(value.toObject(), kind);
            if (!item.value(QStringLiteral("title")).toString().isEmpty()) items.append(item);
        }
    }
    return items;
}

QVariantList manga(const QString &query, bool *failed)
{
    // WorldSearch.js:170-217: AniList SEARCH_MATCH lane. Request identity as well as art.
    const QJsonObject body{{QStringLiteral("query"),
        QStringLiteral("query($s:String){Page(perPage:24){media(search:$s,type:MANGA,sort:SEARCH_MATCH){id idMal title{romaji english} coverImage{large} format}}}")},
        {QStringLiteral("variables"), QJsonObject{{QStringLiteral("s"), query}}}};
    const auto reply = WebFeedHttp::request(QUrl(QStringLiteral("https://graphql.anilist.co")),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    if (!reply.ok) { *failed = true; return {}; }
    QVariantList items;
    const QJsonArray media = reply.json.object().value(QStringLiteral("data")).toObject()
        .value(QStringLiteral("Page")).toObject().value(QStringLiteral("media")).toArray();
    for (const QJsonValue &value : media) {
        const QJsonObject m = value.toObject();
        const QJsonObject title = m.value(QStringLiteral("title")).toObject();
        QVariantMap row{{QStringLiteral("id"), QString::number(m.value(QStringLiteral("id")).toInt())},
                        {QStringLiteral("source"), QStringLiteral("anilist")},
                        {QStringLiteral("title"), title.value(QStringLiteral("english")).toString().isEmpty()
                            ? title.value(QStringLiteral("romaji")).toString()
                            : title.value(QStringLiteral("english")).toString()},
                        {QStringLiteral("cover"), m.value(QStringLiteral("coverImage")).toObject().value(QStringLiteral("large")).toString()},
                        {QStringLiteral("subtitle"), m.value(QStringLiteral("format")).toString()}};
        if (m.value(QStringLiteral("idMal")).toInt() > 0)
            row.insert(QStringLiteral("mal_id"), m.value(QStringLiteral("idMal")).toInt());
        items.append(WebFeedValue::item(row, QStringLiteral("Tankoban"), QStringLiteral("manga")));
    }
    return items;
}

QVariantList comics(const FeedContext &ctx, const QString &query)
{
    ComicsCatalog db(ctx.paths.comics);
    QVariantList items;
    for (const QVariant &value : db.search(query, 30)) {
        const QVariantMap row = value.toMap();
        items.append(WebFeedValue::item(row, QStringLiteral("Tankoban"), QStringLiteral("comic")));
    }
    return items;
}

QVariantList books(const QString &query, const QString &media, bool *failed)
{
    // BiblioApi.js:247-253 and 315-321: Apple ebook and audiobook search lanes.
    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery args;
    args.addQueryItem(QStringLiteral("media"), media);
    args.addQueryItem(QStringLiteral("limit"), QStringLiteral("24"));
    args.addQueryItem(QStringLiteral("term"), query);
    url.setQuery(args);
    const auto reply = WebFeedHttp::request(url);
    if (!reply.ok) { *failed = true; return {}; }
    QVariantList items;
    for (const QJsonValue &value : reply.json.object().value(QStringLiteral("results")).toArray()) {
        const QJsonObject r = value.toObject();
        const qlonglong numericId = r.value(QStringLiteral("trackId")).toVariant().toLongLong() > 0
            ? r.value(QStringLiteral("trackId")).toVariant().toLongLong()
            : r.value(QStringLiteral("collectionId")).toVariant().toLongLong();
        if (numericId <= 0) continue;
        const QString id = QString::number(numericId);
        const QString title = media == QLatin1String("audiobook")
            ? r.value(QStringLiteral("collectionName")).toString()
            : r.value(QStringLiteral("trackName")).toString();
        const QString cover = r.value(QStringLiteral("artworkUrl100")).toString()
            .replace(QStringLiteral("100x100"), QStringLiteral("600x600"));
        const QVariantMap row{{QStringLiteral("id"), id}, {QStringLiteral("source"), QStringLiteral("apple")},
            {QStringLiteral("title"), title}, {QStringLiteral("subtitle"), r.value(QStringLiteral("artistName")).toString()},
            {QStringLiteral("cover"), cover}};
        if (!title.isEmpty()) items.append(WebFeedValue::item(row, QStringLiteral("Biblio"),
            media == QLatin1String("audiobook") ? QStringLiteral("audiobook") : QStringLiteral("book")));
    }
    return items;
}

QVariantList build(const FeedContext &ctx)
{
    const QString scope = ctx.params.value(QStringLiteral("scope")).toString();
    const QString query = ctx.params.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty()) return idle(ctx);
    if (query.size() < 2) return {WebFeedValue::section(QStringLiteral("search.results"), 0,
        QStringLiteral("Search"), QStringLiteral("grid"), {}, QStringLiteral("ready"))};
    QVariantList sections;
    if (scope == QLatin1String("all") || scope == QLatin1String("Theatre")) {
        // An indexed local first section keeps query feedback immediate while
        // WorldSearch.js:109-166 Cinemeta lanes resolve on the second pass.
        ImdbCatalog imdb(ctx.paths.imdb, nullptr, QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVariantList items;
        for (const QVariant &value : imdb.search(query, 20)) {
            QVariantMap row = value.toMap();
            const QString type = row.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("tvSeries") || type == QLatin1String("mini"))
                row.insert(QStringLiteral("type"), QStringLiteral("series"));
            items.append(WebFeedValue::item(row, QStringLiteral("Theatre")));
        }
        sections.append(WebFeedValue::section(QStringLiteral("search.theatre"), sections.size(),
            QStringLiteral("Movies & Shows"), QStringLiteral("grid"), items,
            items.isEmpty() ? QStringLiteral("loading") : QStringLiteral("ready")));
    }
    if (scope == QLatin1String("all") || scope == QLatin1String("Tankoban")) {
        QVariantList items = comics(ctx, query);
        sections.append(WebFeedValue::section(QStringLiteral("search.tankoban"), sections.size(),
            QStringLiteral("Manga & Comics"), QStringLiteral("grid"), items,
            items.isEmpty() ? QStringLiteral("loading") : QStringLiteral("ready")));
    }
    if (scope == QLatin1String("all") || scope == QLatin1String("Biblio")) {
        sections.append(WebFeedValue::section(QStringLiteral("search.biblio"), sections.size(),
            QStringLiteral("Books & Audiobooks"), QStringLiteral("grid"), {}, QStringLiteral("loading")));
    }
    return sections;
}

QVariantList enrich(const FeedContext &ctx)
{
    const QString query = ctx.params.value(QStringLiteral("query")).toString().trimmed();
    if (query.size() < 2) return ctx.baseSections;
    QVariantList sections = ctx.baseSections;
    for (int i = 0; i < sections.size(); ++i) {
        QVariantMap section = sections.at(i).toMap();
        const QString id = section.value(QStringLiteral("id")).toString();
        QVariantList items;
        bool failed = false;
        if (id == QLatin1String("search.theatre")) {
            items = theatre(query, &failed);
            if (items.isEmpty()) items = section.value(QStringLiteral("items")).toList();
        } else if (id == QLatin1String("search.tankoban")) {
            items = manga(query, &failed);
            items.append(section.value(QStringLiteral("items")).toList());
        } else if (id == QLatin1String("search.biblio")) {
            items = books(query, QStringLiteral("ebook"), &failed);
            items.append(books(query, QStringLiteral("audiobook"), &failed));
        } else continue;
        section.insert(QStringLiteral("items"), items);
        section.insert(QStringLiteral("state"), failed && items.isEmpty() ? QStringLiteral("error")
            : items.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
        sections[i] = section;
    }
    return sections;
}

const bool registered = FeedRegistry::add({QStringLiteral("search"), {}, valid,
    [](const QVariantMap &params) -> QVariantList {
        if (params.value(QStringLiteral("query")).toString().trimmed().isEmpty()) return {};
        return {WebFeedValue::section(QStringLiteral("search.results"), 0,
            QStringLiteral("Search"), QStringLiteral("grid"), {}, QStringLiteral("loading"))};
    }, build, false, false, false, true, enrich});

const bool surpriseRegistered = ActionRegistry::add({QStringLiteral("search.surprise"),
    [](const QVariantMap &payload) {
        return QStringList{QStringLiteral("all"), QStringLiteral("Tankoban"),
                           QStringLiteral("Biblio"), QStringLiteral("Theatre")}
            .contains(payload.value(QStringLiteral("scope")).toString());
    },
    [](ColosseumWebBridge &bridge, const QVariantMap &payload, ActionRegistry::Completion done) {
        const QString scope = payload.value(QStringLiteral("scope")).toString();
        const WorldFeed::Paths paths = bridge.feedPaths();
        const bool showExplicit = bridge.showExplicit();
        auto *pick = new QFutureWatcher<QVariantMap>(&bridge);
        QObject::connect(pick, &QFutureWatcher<QVariantMap>::finished, &bridge,
            [pick, &bridge, done] {
                const QVariantMap item = pick->result();
                pick->deleteLater();
                if (item.isEmpty()) {
                    done({{QStringLiteral("ok"), false},
                          {QStringLiteral("error"), QStringLiteral("No surprise title is available.")}});
                    return;
                }
                auto *opened = new QFutureWatcher<QVariantMap>(&bridge);
                QObject::connect(opened, &QFutureWatcher<QVariantMap>::finished, &bridge,
                    [opened, done] {
                        const QVariantMap result = opened->result();
                        opened->deleteLater();
                        done(result);
                    });
                opened->setFuture(bridge.act(QStringLiteral("open"),
                    {{QStringLiteral("item"), item},
                     {QStringLiteral("intent"), QStringLiteral("details")}}));
            });
        pick->setFuture(QtConcurrent::run([scope, paths, showExplicit] {
            return SearchFeed::surpriseItem(scope, paths, showExplicit);
        }));
    }});
} // namespace

QVariantMap SearchFeed::surpriseItem(const QString &scope,
                                     const WorldFeed::Paths &paths,
                                     bool showExplicit)
{
    // SearchSurface.qml:177-187 and WorldSearch.js:365-374: choose a random
    // genre, then one of its first twenty actual titles. Call only on a worker.
    QString world = scope;
    if (world == QLatin1String("all")) {
        const QStringList worlds{QStringLiteral("Tankoban"), QStringLiteral("Theatre"),
                                 QStringLiteral("Biblio")};
        world = worlds.at(QRandomGenerator::global()->bounded(worlds.size()));
    }
    const QStringList names = genreNames(world);
    if (names.isEmpty()) return {};
    const QString genre = names.at(QRandomGenerator::global()->bounded(names.size()));
    QVariantList items;
    if (world == QLatin1String("Tankoban")) {
        MalCatalog mal(paths.mal, nullptr, QUuid::createUuid().toString(QUuid::WithoutBraces));
        for (const QVariant &value : mal.genreEntries(QStringLiteral("manga"), genre,
                                                       QStringLiteral("members"), 20))
            items.append(WebFeedValue::item(value.toMap(), world, QStringLiteral("manga")));
    } else if (world == QLatin1String("Theatre")) {
        ImdbCatalog imdb(paths.imdb, nullptr, QUuid::createUuid().toString(QUuid::WithoutBraces));
        for (const QVariant &value : imdb.titleCatalog({{QStringLiteral("type"), QStringLiteral("movie")},
                    {QStringLiteral("genre"), genre}, {QStringLiteral("order"), QStringLiteral("votes")}}, 0, 20))
            items.append(WebFeedValue::item(value.toMap(), world, QStringLiteral("movie")));
    } else if (world == QLatin1String("Biblio")) {
        // BiblioApi.js:121-129 uses this chart as the book discovery source.
        const QStringList ids{QStringLiteral("9031"), QStringLiteral("9032"),
            QStringLiteral("9020"), QStringLiteral("9003"), QStringLiteral("9015"), QStringLiteral("11165")};
        const int index = names.indexOf(genre);
        if (index >= 0 && index < ids.size()) {
            const auto reply = WebFeedHttp::request(QUrl(QStringLiteral("https://itunes.apple.com/us/rss/topebooks/limit=20/genre=%1/json")
                .arg(ids.at(index))));
            for (const QJsonValue &value : reply.json.object().value(QStringLiteral("feed")).toObject()
                                              .value(QStringLiteral("entry")).toArray()) {
                const QJsonObject row = value.toObject();
                const QString id = row.value(QStringLiteral("id")).toObject().value(QStringLiteral("attributes"))
                    .toObject().value(QStringLiteral("im:id")).toString();
                const QString title = row.value(QStringLiteral("im:name")).toObject().value(QStringLiteral("label")).toString();
                if (!id.isEmpty() && !title.isEmpty())
                    items.append(WebFeedValue::item({{QStringLiteral("id"), id},
                        {QStringLiteral("source"), QStringLiteral("apple")}, {QStringLiteral("title"), title}},
                        world, QStringLiteral("book")));
            }
        }
    }
    Q_UNUSED(showExplicit);
    if (items.isEmpty()) return {};
    return items.at(QRandomGenerator::global()->bounded(qMin(20, items.size()))).toMap();
}
