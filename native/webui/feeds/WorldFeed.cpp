#include "WorldFeed.h"
#include "FeedValue.h"

#include "../../engine/BiblioCatalogStore.h"
#include "../../engine/ComicsCatalog.h"
#include "../../engine/ImdbCatalog.h"
#include "../../engine/MalCatalog.h"

#include <QHash>
#include <QStringList>
#include <QUuid>

namespace {

QVariantList items(const QVariantList &rows, const QString &world, const QString &kind)
{
    QVariantList out;
    for (const QVariant &value : rows) {
        QVariantMap row = value.toMap();
        // BiblioCatalogStore::page() returns canonicalId/coverUrl and a rating
        // object; BiblioExplorePage.qml:_normalizeHouseItem() translates those
        // before rendering. Keep the same identity and visual fields here.
        if (world == QLatin1String("Biblio")) {
            if (!row.contains(QStringLiteral("id")))
                row.insert(QStringLiteral("id"), row.value(QStringLiteral("canonicalId")));
            if (!row.contains(QStringLiteral("source")))
                row.insert(QStringLiteral("source"), QStringLiteral("biblio"));
            if (!row.contains(QStringLiteral("subtitle")))
                row.insert(QStringLiteral("subtitle"), row.value(QStringLiteral("author")));
            const QVariant rating = row.value(QStringLiteral("rating"));
            if (rating.canConvert<QVariantMap>())
                row.insert(QStringLiteral("rating"), rating.toMap().value(QStringLiteral("average")));
        }
        out.append(WebFeedValue::item(row, world, kind));
    }
    return out;
}

QVariantList pageItems(const QVariantMap &page, const QString &world,
                       const QString &kind)
{
    return items(page.value(QStringLiteral("items")).toList(), world, kind);
}

void append(QVariantList &out, const QString &world, const QString &tab,
            const QString &name, const QString &title, const QVariantList &rows,
            const QString &layout = QStringLiteral("rail"))
{
    out.append(WebFeedValue::section(world.toLower() + QLatin1Char('.') + tab
                                     + QLatin1Char('.') + name, out.size(), title,
                                     layout, rows, rows.isEmpty() ? QStringLiteral("empty")
                                                                : QStringLiteral("ready")));
}

QVariantList imdbRows(ImdbCatalog &catalogue, const QString &type,
                      double ratingMin, int votesMin, int limit)
{
    if (!catalogue.ready()) return {};
    return catalogue.titleCatalog({{QStringLiteral("type"), type},
                                   {QStringLiteral("order"), QStringLiteral("rating")},
                                   {QStringLiteral("ratingMin"), ratingMin},
                                   {QStringLiteral("votesMin"), votesMin},
                                   {QStringLiteral("excludeAnime"), true}}, 0, limit);
}

} // namespace

bool WorldFeed::validTab(const QString &world, const QString &tab)
{
    static const QHash<QString, QStringList> tabs{
        {QStringLiteral("Theatre"), {QStringLiteral("discover"), QStringLiteral("movies"),
                                     QStringLiteral("shows"), QStringLiteral("anime"),
                                     QStringLiteral("library")}},
        {QStringLiteral("Tankoban"), {QStringLiteral("discover"), QStringLiteral("manga"),
                                      QStringLiteral("comics"), QStringLiteral("library")}},
        {QStringLiteral("Biblio"), {QStringLiteral("discover"), QStringLiteral("explore"),
                                    QStringLiteral("library")}}
    };
    return tabs.value(world).contains(tab);
}

QVariantList WorldFeed::build(const QString &world, const QString &tab,
                              const Paths &paths, const QVariantList &collection,
                              bool showExplicit)
{
    QVariantList out;
    if (!validTab(world, tab))
        return {WebFeedValue::section(QStringLiteral("world.error"), 0,
                                      QStringLiteral("Unavailable"), QStringLiteral("list"),
                                      {}, QStringLiteral("error"))};

    // The CollectionStore copy is taken on the GUI thread. This library tab
    // moves the collection view from TheatreWorld.qml:library, TankobanWorld.qml:
    // library, and BiblioWorld.qml:library, without querying any SQLite owner.
    if (tab == QLatin1String("library")) {
        append(out, world, tab, QStringLiteral("saved"), QStringLiteral("Library"),
               items(collection, world, QString()), QStringLiteral("grid"));
        return out;
    }

    if (world == QLatin1String("Theatre")) {
        // Moved from TheatreWorld.qml:182 loadCatalog and its catalogue tab
        // selection. A worker-owned ImdbCatalog/MalCatalog gives each request
        // its own SQLite connection; GUI-owned QSqlDatabase is never crossed.
        ImdbCatalog imdb(paths.imdb, nullptr,
                         QStringLiteral("web_world_imdb_")
                             + QUuid::createUuid().toString(QUuid::WithoutBraces));
        MalCatalog mal(paths.mal, nullptr,
                       QStringLiteral("web_world_mal_")
                           + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (tab == QLatin1String("discover") || tab == QLatin1String("movies")) {
            append(out, world, tab, QStringLiteral("movies"), QStringLiteral("Top Movies"),
                   items(imdbRows(imdb, QStringLiteral("movie"), 8.0, 200000,
                                  tab == QLatin1String("discover") ? 12 : 24),
                         world, QStringLiteral("movie")));
        }
        if (tab == QLatin1String("discover") || tab == QLatin1String("shows")) {
            append(out, world, tab, QStringLiteral("shows"), QStringLiteral("Top Shows"),
                   items(imdbRows(imdb, QStringLiteral("series"), 8.2, 100000,
                                  tab == QLatin1String("discover") ? 12 : 24),
                         world, QStringLiteral("series")));
        }
        if (tab == QLatin1String("discover") || tab == QLatin1String("anime")) {
            QVariantList anime;
            if (mal.ready())
                anime = mal.animeCatalog({{QStringLiteral("order"), QStringLiteral("score")},
                                          {QStringLiteral("voteFloor"), 5000}}, 0,
                                         tab == QLatin1String("discover") ? 12 : 24);
            append(out, world, tab, QStringLiteral("anime"), QStringLiteral("Top Anime"),
                   items(anime, world, QStringLiteral("anime")));
        }
        // The complete Next Up unit resolver is the Theatre pilot (Step 4).
        // This state is explicit rather than silently displaying a wrong unit.
        if (tab == QLatin1String("discover"))
            out.append(WebFeedValue::section(QStringLiteral("theatre.discover.nextUp"),
                        out.size(), QStringLiteral("Next Up"), QStringLiteral("rail"),
                        {}, QStringLiteral("loading")));
        return out;
    }

    if (world == QLatin1String("Tankoban")) {
        // Moved from TankobanWorld.qml:99 initializeComicCatalogue and
        // TankobanDiscoverApi.js. Both catalogues live on this worker thread.
        MalCatalog mal(paths.mal, nullptr,
                       QStringLiteral("web_world_mal_")
                           + QUuid::createUuid().toString(QUuid::WithoutBraces));
        ComicsCatalog comics(paths.comics);
        auto manga = [&mal, showExplicit](const QString &catalogue) {
            return mal.ready() ? pageItems(mal.discoverPage(catalogue, {}, {},
                                                             showExplicit, 0, 24),
                                           QStringLiteral("Tankoban"), QStringLiteral("manga"))
                               : QVariantList{};
        };
        auto comic = [&comics, showExplicit](const QString &catalogue) {
            return comics.ready() ? pageItems(comics.discoverPage(catalogue, {}, {},
                                                                   showExplicit, 0, 24),
                                              QStringLiteral("Tankoban"), QStringLiteral("comic"))
                                  : QVariantList{};
        };
        if (tab == QLatin1String("discover") || tab == QLatin1String("manga")) {
            append(out, world, tab, QStringLiteral("mangaPopular"), QStringLiteral("Popular Manga"), manga(QStringLiteral("popular")));
            if (tab == QLatin1String("manga")) {
                append(out, world, tab, QStringLiteral("mangaTrending"), QStringLiteral("Trending"), manga(QStringLiteral("trending")));
                append(out, world, tab, QStringLiteral("mangaTop"), QStringLiteral("Top Rated"), manga(QStringLiteral("top-rated")));
                append(out, world, tab, QStringLiteral("mangaNew"), QStringLiteral("New Releases"), manga(QStringLiteral("new-releases")));
            }
        }
        if (tab == QLatin1String("discover") || tab == QLatin1String("comics")) {
            append(out, world, tab, QStringLiteral("comicsPopular"), QStringLiteral("Popular Comics"), comic(QStringLiteral("popular")));
            if (tab == QLatin1String("comics")) {
                append(out, world, tab, QStringLiteral("comicsRecent"), QStringLiteral("Recently Available"), comic(QStringLiteral("recently-available")));
                append(out, world, tab, QStringLiteral("comicsComplete"), QStringLiteral("Complete Runs"), comic(QStringLiteral("complete-runs")));
                append(out, world, tab, QStringLiteral("comicsStocked"), QStringLiteral("Most Stocked"), comic(QStringLiteral("most-stocked")));
            }
        }
        if (tab == QLatin1String("discover"))
            out.append(WebFeedValue::section(QStringLiteral("tankoban.discover.nextUp"),
                        out.size(), QStringLiteral("Next Up"), QStringLiteral("rail"),
                        {}, QStringLiteral("loading")));
        return out;
    }

    // Biblio's catalogue owns its own GUI-thread connection in the app. Open a
    // separate BiblioCatalogStore on this worker for the selected tab only.
    // Source: BiblioWorld.qml:56 _refreshFeaturedFromNative,
    // BiblioDiscoverPage.qml and BiblioExplorePage.qml catalogue rows.
    BiblioCatalogStore biblio;
    if (!biblio.open(paths.biblio))
        return {WebFeedValue::section(QStringLiteral("biblio.%1.catalogue").arg(tab),
                    0, QStringLiteral("Books"), QStringLiteral("grid"), {},
                    QStringLiteral("error"))};
    if (tab == QLatin1String("discover")) {
        append(out, world, tab, QStringLiteral("popular"), QStringLiteral("Popular"),
               pageItems(biblio.page(QStringLiteral("popular"), {}, {},
                                     showExplicit, 0, 24), world, QStringLiteral("book")),
               QStringLiteral("grid"));
    } else {
        for (const QString &catalogue : {QStringLiteral("popular"),
                                         QStringLiteral("top-rated"),
                                         QStringLiteral("new-releases"),
                                         QStringLiteral("trending")}) {
            QString title = catalogue;
            title.replace(QLatin1Char('-'), QLatin1Char(' '));
            append(out, world, tab, catalogue, title,
                   pageItems(biblio.page(catalogue, {}, {}, showExplicit, 0, 18),
                             world, QStringLiteral("book")));
        }
    }
    return out;
}
