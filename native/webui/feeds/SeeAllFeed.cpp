#include "FeedRegistry.h"
#include "FeedChoice.h"
#include "FeedHttp.h"
#include "FeedValue.h"
#include "ContinueFeed.h"

#include "../../engine/BiblioCatalogStore.h"
#include "../../engine/ComicsCatalog.h"
#include "../../engine/ImdbCatalog.h"
#include "../../engine/MalCatalog.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QUrlQuery>
#include <QUuid>

#include <functional>

namespace {
QString mediumFor(const QString &world, const QVariantMap &facet)
{
    const QString medium = facet.value(QStringLiteral("medium")).toString();
    if (!medium.isEmpty()) return medium;
    return world == QLatin1String("Biblio") ? QStringLiteral("book")
         : world == QLatin1String("Theatre") ? QStringLiteral("movie")
         : QStringLiteral("manga");
}

bool valid(const QVariantMap &params)
{
    const QVariantMap r = params.value(QStringLiteral("route")).toMap();
    const QString world = r.value(QStringLiteral("world")).toString();
    const QString source = r.value(QStringLiteral("source")).toString();
    const QVariantMap facet = r.value(QStringLiteral("facet")).toMap();
    const QString medium = mediumFor(world, facet);
    if (r.value(QStringLiteral("v")).toInt() != 1 ||
        !QStringList{QStringLiteral("Tankoban"), QStringLiteral("Biblio"),
                     QStringLiteral("Theatre"), QStringLiteral("all")}.contains(world) ||
        !QStringList{QStringLiteral("catalogue"), QStringLiteral("genre"),
                     QStringLiteral("genreIndex"), QStringLiteral("collection"),
                     QStringLiteral("continue")}.contains(source) ||
        !r.contains(QStringLiteral("explicit")) ||
        !r.value(QStringLiteral("explicit")).canConvert<bool>() ||
        r.value(QStringLiteral("pageSize")).toInt() < 1 ||
        r.value(QStringLiteral("pageSize")).toInt() > 100) return false;
    if (source == QLatin1String("continue")) return true;
    if (world == QLatin1String("all")) return false;
    if (world == QLatin1String("Tankoban") &&
        !QStringList{QStringLiteral("manga"), QStringLiteral("comic")}.contains(medium)) return false;
    if (world == QLatin1String("Biblio") && medium != QLatin1String("book")) return false;
    if (world == QLatin1String("Theatre") &&
        !QStringList{QStringLiteral("movie"), QStringLiteral("series"),
                     QStringLiteral("anime")}.contains(medium)) return false;
    return source != QLatin1String("genre") || !facet.value(QStringLiteral("name")).toString().isEmpty();
}

QVariantMap mediaItem(const QVariantMap &row, const QString &world, const QString &medium)
{
    QVariantMap copy = row;
    if (world == QLatin1String("Biblio")) {
        copy.insert(QStringLiteral("id"), row.value(QStringLiteral("canonicalId"), row.value(QStringLiteral("id"))));
        copy.insert(QStringLiteral("source"), row.value(QStringLiteral("source"), QStringLiteral("biblio")));
        copy.insert(QStringLiteral("subtitle"), row.value(QStringLiteral("author")));
        if (row.value(QStringLiteral("rating")).canConvert<QVariantMap>())
            copy.insert(QStringLiteral("rating"), row.value(QStringLiteral("rating")).toMap()
                .value(QStringLiteral("average")));
    }
    if (!copy.contains(QStringLiteral("id")) && copy.contains(QStringLiteral("mal_id")))
        copy.insert(QStringLiteral("id"), copy.value(QStringLiteral("mal_id")));
    return WebFeedValue::item(copy, world, medium);
}

QVariantList mapItems(const QVariantList &rows, const QString &world, const QString &medium, int count)
{
    QVariantList result;
    for (const QVariant &row : rows) {
        if (result.size() >= count) break;
        const QVariantMap item = mediaItem(row.toMap(), world, medium);
        if (!item.value(QStringLiteral("title")).toString().isEmpty()) result.append(item);
    }
    return result;
}

QVariantList gatherRows(int wanted, const std::function<QVariantList(int, int)> &page)
{
    QVariantList rows;
    int offset = 0;
    while (rows.size() <= wanted) {
        const int batch = qMin(100, wanted + 1 - rows.size());
        const QVariantList part = page(offset, batch);
        rows.append(part);
        if (part.size() < batch) break;
        offset += part.size();
    }
    return rows;
}

QVariantList gatherPageItems(int wanted, const std::function<QVariantMap(int, int)> &page)
{
    QVariantList rows;
    int offset = 0;
    while (rows.size() <= wanted) {
        const int batch = qMin(100, wanted + 1 - rows.size());
        const QVariantMap result = page(offset, batch);
        const QVariantList part = result.value(QStringLiteral("items")).toList();
        rows.append(part);
        const int next = result.value(QStringLiteral("nextOffset"), offset + part.size()).toInt();
        if (part.isEmpty() || result.value(QStringLiteral("exhausted"), true).toBool() || next <= offset) break;
        offset = next;
    }
    return rows;
}

QVariantList genres(const FeedContext &ctx, const QString &world, const QString &medium)
{
    QVariantList rows;
    if (medium == QLatin1String("manga") || medium == QLatin1String("anime")) {
        // GenreIndexApi.js and TheatreGenreApi.js: bundled MAL names and counts.
        MalCatalog mal(ctx.paths.mal, nullptr, QUuid::createUuid().toString(QUuid::WithoutBraces));
        rows = mal.genreCounts(medium);
    } else if (medium == QLatin1String("comic")) {
        ComicsCatalog comics(ctx.paths.comics);
        rows = comics.discoverFilters(QStringLiteral("genre"), ctx.showExplicit);
    } else if (medium == QLatin1String("book")) {
        // BiblioGenreApi.js: the Apple Books storefront's ten genre doors.
        const QStringList names{QStringLiteral("Fiction & Literature"), QStringLiteral("Mysteries & Thrillers"),
            QStringLiteral("Sci-Fi & Fantasy"), QStringLiteral("Romance"),
            QStringLiteral("Biographies & Memoirs"), QStringLiteral("History"),
            QStringLiteral("Young Adult"), QStringLiteral("Comics & Graphic Novels"),
            QStringLiteral("Humor"), QStringLiteral("Travel & Adventure")};
        const QStringList ids{QStringLiteral("9031"), QStringLiteral("9032"), QStringLiteral("9020"),
            QStringLiteral("9003"), QStringLiteral("9008"), QStringLiteral("9015"),
            QStringLiteral("11165"), QStringLiteral("9026"), QStringLiteral("9012"), QStringLiteral("9004")};
        for (int i = 0; i < names.size(); ++i)
            rows.append(QVariantMap{{QStringLiteral("name"), names.at(i)}, {QStringLiteral("key"), ids.at(i)}});
    } else {
        // TheatreGenreApi.js: Cinemeta catalogue's genre index.
        const QStringList names = medium == QLatin1String("series")
            ? QStringList{QStringLiteral("Action"), QStringLiteral("Adventure"), QStringLiteral("Animation"),
                          QStringLiteral("Comedy"), QStringLiteral("Crime"), QStringLiteral("Documentary"),
                          QStringLiteral("Drama"), QStringLiteral("Family"), QStringLiteral("Fantasy"),
                          QStringLiteral("Mystery"), QStringLiteral("Reality-TV"), QStringLiteral("Romance"),
                          QStringLiteral("Sci-Fi"), QStringLiteral("Thriller")}
            : QStringList{QStringLiteral("Action"), QStringLiteral("Adventure"), QStringLiteral("Animation"),
                          QStringLiteral("Comedy"), QStringLiteral("Crime"), QStringLiteral("Documentary"),
                          QStringLiteral("Drama"), QStringLiteral("Family"), QStringLiteral("Fantasy"),
                          QStringLiteral("Horror"), QStringLiteral("Mystery"), QStringLiteral("Romance"),
                          QStringLiteral("Sci-Fi"), QStringLiteral("Thriller")};
        for (const QString &name : names) rows.append(QVariantMap{{QStringLiteral("name"), name}});
    }
    QVariantList choices;
    for (const QVariant &entry : rows) {
        const QVariantMap row = entry.toMap();
        const QString name = row.value(QStringLiteral("name"), row.value(QStringLiteral("label"))).toString();
        if (name.isEmpty()) continue;
        if (!ctx.showExplicit && (name == QLatin1String("Erotica") || name == QLatin1String("Hentai"))) continue;
        QVariantMap facet{{QStringLiteral("medium"), medium}, {QStringLiteral("name"), name}};
        if (row.contains(QStringLiteral("key"))) facet.insert(QStringLiteral("key"), row.value(QStringLiteral("key")));
        const QVariantMap route = WebFeedChoice::route(world, QStringLiteral("genre"), facet,
                                                       QStringLiteral("popular"), ctx.showExplicit, 24);
        choices.append(WebFeedChoice::choice(QStringLiteral("genre:") + world + QLatin1Char(':') + medium + QLatin1Char(':') + name,
                                            name, {{QStringLiteral("route"), route}},
                                            row.value(QStringLiteral("count")).toString()));
    }
    return choices;
}

QVariantList genreSections(const FeedContext &ctx, const QString &world, const QString &medium)
{
    const QVariantList all = genres(ctx, world, medium);
    if (medium != QLatin1String("manga") && medium != QLatin1String("anime"))
        return {WebFeedChoice::section(QStringLiteral("seeAll.genres.") + world + QLatin1Char('.') + medium,
            0, QStringLiteral("Genres"), QStringLiteral("tiles"), all)};
    // GenreIndexApi.js:16-23 and TheatreGenreApi.js:26-33: MAL's four
    // groups, with only the sexually explicit bucket gated by preference.
    const QStringList plain{QStringLiteral("Action"), QStringLiteral("Adventure"),
        QStringLiteral("Avant Garde"), QStringLiteral("Award Winning"), QStringLiteral("Boys Love"),
        QStringLiteral("Comedy"), QStringLiteral("Drama"), QStringLiteral("Fantasy"),
        QStringLiteral("Girls Love"), QStringLiteral("Gourmet"), QStringLiteral("Horror"),
        QStringLiteral("Mystery"), QStringLiteral("Romance"), QStringLiteral("Sci-Fi"),
        QStringLiteral("Slice of Life"), QStringLiteral("Sports"),
        QStringLiteral("Supernatural"), QStringLiteral("Suspense")};
    const QStringList demographics{QStringLiteral("Shounen"), QStringLiteral("Shoujo"),
        QStringLiteral("Seinen"), QStringLiteral("Josei"), QStringLiteral("Kids")};
    QVariantList groups[4];
    for (const QVariant &value : all) {
        const QVariantMap choice = value.toMap();
        const QString name = choice.value(QStringLiteral("label")).toString();
        const int group = plain.contains(name) ? 0
            : (name == QLatin1String("Erotica") || name == QLatin1String("Hentai")) ? 1
            : demographics.contains(name) ? 3 : 2;
        groups[group].append(choice);
    }
    const QStringList names{QStringLiteral("Genres"), QStringLiteral("Explicit Genres"),
        QStringLiteral("Themes"), QStringLiteral("Demographics")};
    QVariantList result;
    for (int i = 0; i < 4; ++i) {
        if (groups[i].isEmpty()) continue;
        result.append(WebFeedChoice::section(QStringLiteral("seeAll.genres.") + world
            + QLatin1Char('.') + medium + QLatin1Char('.') + QString::number(i),
            result.size(), names.at(i), QStringLiteral("tiles"), groups[i]));
    }
    return result;
}

QVariantList remoteRows(const QString &medium, const QVariantMap &facet, int count, bool *failed)
{
    if (medium == QLatin1String("book")) {
        const QString id = facet.value(QStringLiteral("key")).toString();
        if (id.isEmpty()) return {};
        const auto reply = WebFeedHttp::request(QUrl(QStringLiteral("https://itunes.apple.com/us/rss/topebooks/limit=100/genre=%1/json").arg(id)));
        if (!reply.ok) { *failed = true; return {}; }
        QVariantList rows;
        const QJsonArray entries = reply.json.object().value(QStringLiteral("feed")).toObject()
                                       .value(QStringLiteral("entry")).toArray();
        for (const QJsonValue &entry : entries) {
            const QJsonObject e = entry.toObject();
            const QString title = e.value(QStringLiteral("im:name")).toObject().value(QStringLiteral("label")).toString();
            const QString bookId = e.value(QStringLiteral("id")).toObject().value(QStringLiteral("attributes")).toObject()
                                       .value(QStringLiteral("im:id")).toString();
            if (bookId.isEmpty() || title.isEmpty()) continue;
            const QJsonArray images = e.value(QStringLiteral("im:image")).toArray();
            rows.append(QVariantMap{{QStringLiteral("id"), bookId}, {QStringLiteral("source"), QStringLiteral("apple")},
                                    {QStringLiteral("title"), title},
                                    {QStringLiteral("author"), e.value(QStringLiteral("im:artist")).toObject().value(QStringLiteral("label")).toString()},
                                    {QStringLiteral("cover"), images.isEmpty() ? QString() : images.last().toObject().value(QStringLiteral("label")).toString()}});
            if (rows.size() > count) break;
        }
        return rows;
    }
    QUrl url(QStringLiteral("https://cinemeta-catalogs.strem.io/top/catalog/%1/top/genre=%2.json")
                 .arg(medium == QLatin1String("series") ? QStringLiteral("series") : QStringLiteral("movie"),
                      QString::fromUtf8(QUrl::toPercentEncoding(facet.value(QStringLiteral("name")).toString()))));
    const auto reply = WebFeedHttp::request(url);
    if (!reply.ok) { *failed = true; return {}; }
    QVariantList rows;
    for (const QJsonValue &value : reply.json.object().value(QStringLiteral("metas")).toArray()) {
        const QJsonObject meta = value.toObject();
        rows.append(QVariantMap{{QStringLiteral("tt"), meta.value(QStringLiteral("id")).toString()},
                                {QStringLiteral("title"), meta.value(QStringLiteral("name")).toString()},
                                {QStringLiteral("cover"), meta.value(QStringLiteral("poster")).toString()},
                                {QStringLiteral("backdrop"), meta.value(QStringLiteral("background")).toString()},
                                {QStringLiteral("type"), medium}});
        if (rows.size() > count) break;
    }
    return rows;
}

QVariantMap theatreQuery(const QString &medium, const QString &rowKey)
{
    // TheatreFeed.cpp:160-337 moves TheatreCatalogRules.js shelf recipes;
    // a See All route carries its rowKey so the same recipe can page in SQL.
    QVariantMap q{{QStringLiteral("type"), medium},
                  {QStringLiteral("order"), QStringLiteral("votes")},
                  {QStringLiteral("excludeAnime"), true}};
    if (rowKey == QLatin1String("top10") || rowKey == QLatin1String("most-popular")) return q;
    if (rowKey == QLatin1String("recently-released") || rowKey == QLatin1String("recently-premiered")) {
        q.insert(QStringLiteral("order"), QStringLiteral("year"));
        q.insert(QStringLiteral("votesMin"), 500);
    } else if (rowKey == QLatin1String("top-rated")) {
        q.insert(QStringLiteral("order"), QStringLiteral("rating"));
        q.insert(QStringLiteral("ratingMin"), medium == QLatin1String("series") ? 8.2 : 8.0);
        q.insert(QStringLiteral("votesMin"), medium == QLatin1String("series") ? 100000 : 200000);
    } else if (rowKey == QLatin1String("hidden-gems")) {
        q.insert(QStringLiteral("order"), QStringLiteral("rating"));
        q.insert(QStringLiteral("ratingMin"), medium == QLatin1String("series") ? 7.5 : 7.4);
        q.insert(QStringLiteral("votesMin"), medium == QLatin1String("series") ? 5000 : 10000);
        q.insert(QStringLiteral("votesMax"), medium == QLatin1String("series") ? 75000 : 100000);
    } else if (rowKey == QLatin1String("cult-classics")) {
        q.insert(QStringLiteral("order"), QStringLiteral("rating"));
        q.insert(QStringLiteral("ratingMin"), medium == QLatin1String("series") ? 7.5 : 7.2);
        q.insert(QStringLiteral("votesMin"), medium == QLatin1String("series") ? 5000 : 10000);
        q.insert(QStringLiteral("votesMax"), medium == QLatin1String("series") ? 150000 : 250000);
        q.insert(QStringLiteral("yearTo"), 1999);
    } else if (rowKey == QLatin1String("under-two-hours")) {
        q.insert(QStringLiteral("runtimeMax"), 120);
        q.insert(QStringLiteral("votesMin"), 5000);
    } else if (rowKey == QLatin1String("long-running-series")) {
        q.insert(QStringLiteral("order"), QStringLiteral("episodes"));
        q.insert(QStringLiteral("episodesMin"), 100);
    } else if (rowKey == QLatin1String("limited-series")) {
        q.insert(QStringLiteral("type"), QStringLiteral("mini"));
        q.insert(QStringLiteral("votesMin"), 5000);
    } else if (rowKey == QLatin1String("international-cinema")) {
        q.insert(QStringLiteral("notLang"), QStringLiteral("en"));
        q.insert(QStringLiteral("votesMin"), 5000);
    } else if (rowKey.endsWith(QLatin1String("-cinema"))) {
        const QString lang = rowKey.startsWith(QLatin1String("japanese")) ? QStringLiteral("ja")
            : rowKey.startsWith(QLatin1String("korean")) ? QStringLiteral("ko") : QStringLiteral("fr");
        q.insert(QStringLiteral("order"), QStringLiteral("rating"));
        q.insert(QStringLiteral("lang"), lang);
        q.insert(QStringLiteral("votesMin"), 5000);
    } else if (rowKey == QLatin1String("korean-drama")) {
        q.insert(QStringLiteral("order"), QStringLiteral("rating"));
        q.insert(QStringLiteral("lang"), QStringLiteral("ko"));
        q.insert(QStringLiteral("votesMin"), 5000);
    } else if (rowKey.endsWith(QLatin1String("s-movies")) && rowKey.left(4).toInt() > 0) {
        const int year = rowKey.left(4).toInt();
        q.insert(QStringLiteral("yearFrom"), year);
        q.insert(QStringLiteral("yearTo"), year + 9);
        q.insert(QStringLiteral("votesMin"), 5000);
    } else {
        const QHash<QString, QString> genres{
            {QStringLiteral("documentary-movies"), QStringLiteral("Documentary")},
            {QStringLiteral("animated-movies"), QStringLiteral("Animation")},
            {QStringLiteral("drama-series"), QStringLiteral("Drama")},
            {QStringLiteral("comedy-series"), QStringLiteral("Comedy")},
            {QStringLiteral("documentary-series"), QStringLiteral("Documentary")},
            {QStringLiteral("animated-series"), QStringLiteral("Animation")}};
        if (genres.contains(rowKey)) {
            q.insert(QStringLiteral("genre"), genres.value(rowKey));
            q.insert(QStringLiteral("votesMin"), 5000);
        }
    }
    return q;
}

QVariantMap animeQuery(const QString &rowKey)
{
    QVariantMap q{{QStringLiteral("order"), QStringLiteral("members")}};
    if (rowKey == QLatin1String("top-rated")) {
        q.insert(QStringLiteral("order"), QStringLiteral("score"));
        q.insert(QStringLiteral("voteFloor"), 5000);
    } else if (rowKey == QLatin1String("airing-now") || rowKey == QLatin1String("top-airing")) {
        q.insert(QStringLiteral("status"), QStringLiteral("Currently Airing"));
        if (rowKey == QLatin1String("top-airing")) q.insert(QStringLiteral("order"), QStringLiteral("score"));
    } else if (rowKey == QLatin1String("upcoming-season")) {
        q.insert(QStringLiteral("status"), QStringLiteral("Not yet aired"));
    } else if (rowKey == QLatin1String("top-series") || rowKey == QLatin1String("top-anime-movies")) {
        q.insert(QStringLiteral("type"), rowKey == QLatin1String("top-series") ? QStringLiteral("TV") : QStringLiteral("Movie"));
        q.insert(QStringLiteral("order"), QStringLiteral("score"));
        q.insert(QStringLiteral("voteFloor"), 5000);
    } else if (rowKey == QLatin1String("hidden-gems")) {
        q.insert(QStringLiteral("order"), QStringLiteral("score"));
        q.insert(QStringLiteral("voteFloor"), 2000);
        q.insert(QStringLiteral("membersMin"), 20000);
        q.insert(QStringLiteral("membersMax"), 150000);
    } else if (rowKey.endsWith(QLatin1String("s-anime")) && rowKey.left(4).toInt() > 0) {
        const int year = rowKey.left(4).toInt();
        q.insert(QStringLiteral("yearFrom"), year);
        q.insert(QStringLiteral("yearTo"), year + 9);
    } else if (rowKey == QLatin1String("1990s-earlier")) {
        q.insert(QStringLiteral("yearTo"), 1999);
    } else {
        const QHash<QString, QString> tags{
            {QStringLiteral("action-and-adventure"), QStringLiteral("Action")},
            {QStringLiteral("science-fiction"), QStringLiteral("Sci-Fi")},
            {QStringLiteral("slice-of-life"), QStringLiteral("Slice of Life")},
            {QStringLiteral("psychological"), QStringLiteral("Psychological")},
            {QStringLiteral("romance"), QStringLiteral("Romance")},
            {QStringLiteral("mecha"), QStringLiteral("Mecha")},
            {QStringLiteral("fantasy"), QStringLiteral("Fantasy")},
            {QStringLiteral("horror"), QStringLiteral("Horror")}};
        if (tags.contains(rowKey)) q.insert(QStringLiteral("tag"), tags.value(rowKey));
    }
    return q;
}

QVariantList build(const FeedContext &ctx)
{
    const QVariantMap r = ctx.params.value(QStringLiteral("route")).toMap();
    const QString world = r.value(QStringLiteral("world")).toString();
    const QString source = r.value(QStringLiteral("source")).toString();
    const QVariantMap facet = r.value(QStringLiteral("facet")).toMap();
    const QString medium = mediumFor(world, facet);
    const int pageSize = r.value(QStringLiteral("pageSize")).toInt();
    const int wanted = qMin(5000, pageSize * qMax(1, (ctx.visibleCount + 23) / 24));
    if (source == QLatin1String("genreIndex"))
        return genreSections(ctx, world, medium);
    if (source == QLatin1String("continue"))
        return ContinueFeed::build(ctx.recent, world, ctx.paths.imdb, wanted);
    if (source == QLatin1String("collection"))
        return {WebFeedValue::section(QStringLiteral("seeAll.collection"), 0, QStringLiteral("Collection"),
                                      QStringLiteral("grid"), mapItems(ctx.collection, world, {}, wanted),
                                      QStringLiteral("ready"), ctx.collection.size() > wanted)};
    if (!ctx.showExplicit && source == QLatin1String("genre")
        && (facet.value(QStringLiteral("name")).toString() == QLatin1String("Erotica")
            || facet.value(QStringLiteral("name")).toString() == QLatin1String("Hentai")))
        return {WebFeedValue::section(QStringLiteral("seeAll.results"), 0,
            QStringLiteral("Unavailable"), QStringLiteral("grid"), {}, QStringLiteral("error"))};

    QVariantList rows;
    bool more = false;
    bool failed = false;
    if (medium == QLatin1String("manga") || medium == QLatin1String("anime")) {
        MalCatalog mal(ctx.paths.mal, nullptr, QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (source == QLatin1String("genre")) {
            rows = mal.genreEntries(medium, facet.value(QStringLiteral("name")).toString(),
                                    r.value(QStringLiteral("sort")).toString() == QLatin1String("rating")
                                        ? QStringLiteral("score") : QStringLiteral("members"), qMin(100, wanted + 1));
            more = rows.size() > wanted;
        } else if (medium == QLatin1String("anime")) {
            const QVariantMap query = animeQuery(facet.value(QStringLiteral("rowKey")).toString());
            rows = gatherRows(wanted, [&mal, &query](int offset, int limit) {
                return mal.animeCatalog(query, offset, limit);
            });
            more = rows.size() > wanted;
        } else {
            rows = gatherPageItems(wanted, [&](int offset, int limit) {
                return mal.discoverPage(facet.value(QStringLiteral("catalogue"), QStringLiteral("popular")).toString(),
                    facet.value(QStringLiteral("axis")).toString(), facet.value(QStringLiteral("key")).toString(),
                    ctx.showExplicit, offset, limit);
            });
            more = rows.size() > wanted;
        }
    } else if (medium == QLatin1String("comic")) {
        ComicsCatalog comics(ctx.paths.comics);
        rows = gatherPageItems(wanted, [&](int offset, int limit) {
            return comics.discoverPage(
                source == QLatin1String("genre") ? QStringLiteral("all")
                  : facet.value(QStringLiteral("catalogue"), QStringLiteral("popular")).toString(),
                source == QLatin1String("genre") ? QStringLiteral("genre") : facet.value(QStringLiteral("axis")).toString(),
                source == QLatin1String("genre") ? facet.value(QStringLiteral("name")).toString() : facet.value(QStringLiteral("key")).toString(),
                ctx.showExplicit, offset, limit);
        });
        more = rows.size() > wanted;
    } else if (medium == QLatin1String("book") && source == QLatin1String("genre")) {
        rows = remoteRows(medium, facet, wanted, &failed);
        more = rows.size() > wanted;
    } else if (medium == QLatin1String("book")) {
        BiblioCatalogStore biblio;
        if (biblio.open(ctx.paths.biblio)) {
            rows = gatherPageItems(wanted, [&](int offset, int limit) {
                return biblio.page(facet.value(QStringLiteral("catalogue"), QStringLiteral("popular")).toString(),
                    facet.value(QStringLiteral("axis")).toString(), facet.value(QStringLiteral("key")).toString(),
                    ctx.showExplicit, offset, limit);
            });
            more = rows.size() > wanted;
        }
    } else if (source == QLatin1String("genre")) {
        rows = remoteRows(medium, facet, wanted, &failed);
        more = rows.size() > wanted;
    } else {
        ImdbCatalog imdb(ctx.paths.imdb, nullptr, QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QVariantMap query = theatreQuery(medium, facet.value(QStringLiteral("rowKey")).toString());
        rows = gatherRows(wanted, [&imdb, &query](int offset, int limit) {
            return imdb.titleCatalog(query, offset, limit);
        });
        more = rows.size() > wanted;
    }
    if (wanted >= 5000 || (source == QLatin1String("genre") && medium == QLatin1String("manga") && wanted >= 100)) more = false;
    return {WebFeedValue::section(QStringLiteral("seeAll.results"), 0,
                                  source == QLatin1String("genre") ? facet.value(QStringLiteral("name")).toString()
                                      : QStringLiteral("See All"), QStringLiteral("grid"),
                                  mapItems(rows, world, medium, wanted),
                                  failed ? QStringLiteral("error")
                                      : rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"), more)};
}

const bool registered = FeedRegistry::add({QStringLiteral("seeAll"), {}, valid,
    [](const QVariantMap &) -> QVariantList {
        return {WebFeedValue::section(QStringLiteral("seeAll.results"), 0,
            QStringLiteral("See All"), QStringLiteral("grid"), {}, QStringLiteral("loading"))};
    }, build, true, true});
} // namespace
