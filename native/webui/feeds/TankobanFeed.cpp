#include "ActionRegistry.h"
#include "ContinueFeed.h"
#include "FeedChoice.h"
#include "FeedHttp.h"
#include "FeedRegistry.h"
#include "FeedValue.h"
#include "../ColosseumWebBridge.h"

#include "../../CollectionStore.h"
#include "../../ProgressStore.h"
#include "../../engine/ComicsCatalog.h"
#include "../../engine/MalCatalog.h"
#include "../../engine/MangaDownloader.h"
#include "../../engine/MangaTankobanService.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSharedPointer>
#include <QTimer>
#include <QStringList>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {

constexpr int kPersonalLimit = 12;
constexpr int kShelfLimit = 20;
const QString kRemoveAction = QStringLiteral("world.tankoban.collection.remove");

QVariantMap selectedChoice(const QString &key, const QString &label,
                           const QVariantMap &viewPatch, bool selected,
                           const QString &sublabel = {})
{
    QVariantMap choice = WebFeedChoice::choice(
        key, label, {{QStringLiteral("view"), viewPatch}}, sublabel);
    if (selected)
        choice.insert(QStringLiteral("selected"), true);
    return choice;
}

QVariantMap choiceSection(const QString &id, int index, const QString &title,
                          const QVariantList &choices, const QString &layout = QStringLiteral("chips"))
{
    return WebFeedChoice::section(id, index, title, layout, choices,
        choices.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
}

QVariantMap catalogueRoute(const QString &medium, const QString &catalogue,
                           const QString &axis, const QString &key,
                           bool showExplicit)
{
    return WebFeedChoice::route(
        QStringLiteral("Tankoban"), QStringLiteral("catalogue"),
        {{QStringLiteral("medium"), medium},
         {QStringLiteral("catalogue"), catalogue},
         {QStringLiteral("axis"), axis},
         {QStringLiteral("key"), key}},
        {}, showExplicit, 24);
}

QVariantMap genreRoute(const QString &medium, const QString &name,
                       bool showExplicit)
{
    return WebFeedChoice::route(
        QStringLiteral("Tankoban"), QStringLiteral("genre"),
        {{QStringLiteral("medium"), medium},
         {QStringLiteral("name"), name}},
        {}, showExplicit, 24);
}

QVariantMap genreIndexRoute(const QString &medium, bool showExplicit)
{
    return WebFeedChoice::route(
        QStringLiteral("Tankoban"), QStringLiteral("genreIndex"),
        {{QStringLiteral("medium"), medium}},
        {}, showExplicit, 24);
}

void setSeeAll(QVariantMap &section, const QVariantMap &route)
{
    if (!route.isEmpty())
        section.insert(QStringLiteral("seeAll"),
                       QVariantMap{{QStringLiteral("route"), route}});
}

QString normalizedTitle(QString title)
{
    return title.toLower().simplified();
}

QString mangaMalId(const QVariantMap &row)
{
    QString mal = row.value(QStringLiteral("mal_id"),
                            row.value(QStringLiteral("malId"))).toString();
    if (mal.isEmpty()) {
        const QString id = row.value(QStringLiteral("id")).toString();
        if (id.startsWith(QLatin1String("mal:")))
            mal = id.mid(4);
    }
    return mal;
}

QVariantMap mangaItem(QVariantMap row)
{
    const QString mal = mangaMalId(row);
    if (!mal.isEmpty()) {
        row.insert(QStringLiteral("id"), QStringLiteral("mal:") + mal);
        row.insert(QStringLiteral("mal_id"), mal);
    }
    return WebFeedValue::item(row, QStringLiteral("Tankoban"),
                              QStringLiteral("manga"));
}

QVariantMap comicItem(QVariantMap row)
{
    QString locg = row.value(QStringLiteral("locgId")).toString();
    if (locg.startsWith(QLatin1String("locg:")))
        locg = locg.mid(5);
    if (!locg.isEmpty()) {
        row.insert(QStringLiteral("locgId"), locg);
        row.insert(QStringLiteral("id"), QStringLiteral("locg:") + locg);
    } else {
        QString gcd = row.value(QStringLiteral("gcdId")).toString();
        if (gcd.startsWith(QLatin1String("gcd:")))
            gcd = gcd.mid(4);
        if (!gcd.isEmpty()) {
            row.insert(QStringLiteral("gcdId"), gcd);
            row.insert(QStringLiteral("id"), QStringLiteral("gcd:") + gcd);
        }
    }
    return WebFeedValue::item(row, QStringLiteral("Tankoban"),
                              QStringLiteral("comic"));
}

QVariantList mapRows(const QVariantList &rows, const QString &kind, int limit)
{
    QVariantList out;
    for (const QVariant &value : rows) {
        if (out.size() >= limit)
            break;
        const QVariantMap item = kind == QLatin1String("comic")
            ? comicItem(value.toMap()) : mangaItem(value.toMap());
        if (!item.value(QStringLiteral("title")).toString().isEmpty())
            out.append(item);
    }
    return out;
}

QVariantMap staticManga(const QString &malId, const QString &title,
                        const QString &cover, const QString &backdrop = {},
                        const QString &subtitle = {})
{
    QVariantMap row{{QStringLiteral("id"), QStringLiteral("mal:") + malId},
                    {QStringLiteral("mal_id"), malId},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("cover"), cover},
                    {QStringLiteral("backdrop"), backdrop},
                    {QStringLiteral("subtitle"), subtitle}};
    return mangaItem(row);
}

QVariantMap staticComic(const QString &title, const QString &cover,
                        const QString &backdrop = {}, const QString &subtitle = {})
{
    // TankobanWorld.qml:67-80 routes title-only western cards through the
    // GetComics/curated resolver. A gc: title is the existing native identity.
    QVariantMap row{{QStringLiteral("id"), QStringLiteral("gc:") + title},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("cover"), cover},
                    {QStringLiteral("backdrop"), backdrop},
                    {QStringLiteral("subtitle"), subtitle}};
    return WebFeedValue::item(row, QStringLiteral("Tankoban"),
                              QStringLiteral("comic"));
}

QVariantList featuredItems()
{
    // Moved from Catalog.js:17-21 and TankobanWorld.qml:171-176.
    return {
        staticManga(QStringLiteral("13"), QStringLiteral("One Piece"), {},
            QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg"),
            QStringLiteral("Rubber-bodied Monkey D. Luffy and his Straw Hat crew sail the seas hunting the legendary treasure that crowns the next King of the Pirates.")),
        staticComic(QStringLiteral("Saga"),
            QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Publication4/v4/23/03/9c/23039c5b-155e-0ae4-36b3-d3407b07420c/AUG120491.jpg/2000x2000bb.jpg"),
            {}, QStringLiteral("Alana and Marko, lovers from opposite sides of a galactic war, flee both armies to raise their newborn daughter in hiding.")),
        staticManga(QStringLiteral("2"), QStringLiteral("Berserk"), {},
            QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/banner/30002-3TuoSMl20fUX.jpg"),
            QStringLiteral("Guts, a lone swordsman in a brutal dark-fantasy world, hunts the demons who damned his comrades.")),
        staticComic(QStringLiteral("Invincible"),
            QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Publication124/v4/7c/73/8d/7c738d92-dea3-7901-7ef8-962db5966470/Invincible_Compendium01.jpg/2000x2000bb.jpg"),
            {}, QStringLiteral("Mark Grayson inherits superpowers from his father, then learns the truth about his mission."))
    };
}

QVariantMap resolveMangaTitle(MalCatalog &mal, const QString &title,
                              const QString &cover)
{
    // TankobanMangaTab.qml:30-43 uses Catalog.topManga. The old list had
    // title-only identity; resolve it once on this worker so the web Item key
    // obeys CONTRACT §3.3's Tankoban:mal:<id> mapping.
    const QVariantList matches = mal.matchByTitle(title, 0, QStringLiteral("manga"));
    if (matches.size() != 1)
        return {};
    QVariantMap row = matches.first().toMap();
    if (!cover.isEmpty())
        row.insert(QStringLiteral("cover"), cover);
    return mangaItem(row);
}

QVariantList topMangaItems(MalCatalog &mal)
{
    // Moved from Catalog.js:27-41 and TankobanMangaTab.qml:30-43.
    const QStringList titles{
        QStringLiteral("One Piece"), QStringLiteral("Berserk"),
        QStringLiteral("Vinland Saga"), QStringLiteral("Vagabond"),
        QStringLiteral("Chainsaw Man"), QStringLiteral("Monster"),
        QStringLiteral("Slam Dunk"), QStringLiteral("Solo Leveling"),
        QStringLiteral("20th Century Boys"), QStringLiteral("Jujutsu Kaisen")
    };
    const QStringList covers{
        QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30013-BeslEMqiPhlk.jpg"),
        QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30002-Cul4OeN7bYtn.jpg"),
        QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30642-0mjRDkf4THpo.jpg"),
        QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30656-9mW113O7rDnA.png"),
        QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx105778-euxXZEIfDY2u.png"),
        QStringLiteral("https://uploads.mangadex.org/covers/d9e30523-9d65-469e-92a2-302995770950/a397b3d3-d7b3-413f-8d6a-f2b136a4b4e2.jpg.512.jpg"),
        QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30051-5KJyPlO7z5F4.png"),
        QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx105398-b673Vt5ZSuz3.jpg"),
        QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30003-E84fwIh22LAQ.jpg"),
        QStringLiteral("https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx101517-H3TdM3g5ZUe9.jpg")
    };
    QVariantList out;
    for (int i = 0; i < titles.size(); ++i) {
        const QVariantMap item = resolveMangaTitle(mal, titles.at(i), covers.at(i));
        if (!item.isEmpty())
            out.append(item);
    }
    return out;
}

QVariantList mangaGenreChoices(bool showExplicit)
{
    // Moved from Catalog.js:57-68 and TankobanMangaTab.qml:46-55.
    struct Genre { const char *name; int count; const char *cover; };
    const Genre genres[] = {
        {"Shounen", 1240, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/nx30011-9yUF1dXWgDOx.jpg"},
        {"Seinen", 680, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30656-9mW113O7rDnA.png"},
        {"Romance", 920, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30102-MJuQ0e0k2CgU.png"},
        {"Action", 1510, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx53390-1RsuABC34P9D.jpg"},
        {"Comedy", 1100, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30044-vPmoz3vTPvZs.jpg"},
        {"Drama", 870, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx85135-11OOnyaqV71k.png"},
        {"Shoujo", 540, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30030-zBHKa3yHdtnM.png"},
        {"Josei", 210, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx30028-VJqBC1ar6AxE.png"},
        {"Isekai", 430, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx165131-R4pedWdbZiAW.jpg"},
        {"School", 360, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx69883-zDt4DUXkQS5N.png"},
        {"Magic", 290, "https://s4.anilist.co/file/anilistcdn/media/manga/cover/medium/bx118586-CXKgWikBFQgS.jpg"}
    };
    QVariantList out;
    for (const Genre &genre : genres) {
        const QString name = QString::fromLatin1(genre.name);
        out.append(WebFeedChoice::choice(
            QStringLiteral("tankoban:manga:genre:") + name.toLower(), name,
            {{QStringLiteral("route"),
              genreRoute(QStringLiteral("manga"), name, showExplicit)}},
            QString::number(genre.count), QString::fromLatin1(genre.cover)));
    }
    return out;
}

QStringList mangaCatalogues()
{
    // TankobanDiscoverApi.js:23-31.
    return {QStringLiteral("trending"), QStringLiteral("popular"),
            QStringLiteral("top-rated"), QStringLiteral("new-releases")};
}

QStringList comicCatalogues()
{
    // TankobanDiscoverApi.js:32-40.
    return {QStringLiteral("popular"), QStringLiteral("new-releases"),
            QStringLiteral("recently-available"), QStringLiteral("complete-runs"),
            QStringLiteral("near-complete"), QStringLiteral("most-stocked"),
            QStringLiteral("community-collections"), QStringLiteral("all")};
}

QString catalogueTitle(const QString &key)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("trending"), QStringLiteral("Trending")},
        {QStringLiteral("popular"), QStringLiteral("Popular")},
        {QStringLiteral("top-rated"), QStringLiteral("Top Rated")},
        {QStringLiteral("new-releases"), QStringLiteral("New Releases")},
        {QStringLiteral("recently-available"), QStringLiteral("Recently Available")},
        {QStringLiteral("complete-runs"), QStringLiteral("Complete Runs")},
        {QStringLiteral("near-complete"), QStringLiteral("Near Complete")},
        {QStringLiteral("most-stocked"), QStringLiteral("Most Stocked")},
        {QStringLiteral("community-collections"), QStringLiteral("Community Collections")},
        {QStringLiteral("all"), QStringLiteral("All Series")}
    };
    return names.value(key, key);
}

QString axisForGroup(const QString &type, const QString &group)
{
    const QString g = group.toLower();
    if (type == QLatin1String("manga")) {
        if (g == QLatin1String("genres") || g == QLatin1String("genre"))
            return QStringLiteral("genre");
        if (g == QLatin1String("demographics") || g == QLatin1String("demographic"))
            return QStringLiteral("demographic");
    } else {
        if (g == QLatin1String("genres") || g == QLatin1String("genre"))
            return QStringLiteral("genre");
        if (g == QLatin1String("publishers") || g == QLatin1String("publisher"))
            return QStringLiteral("publisher");
        if (g == QLatin1String("formats") || g == QLatin1String("format"))
            return QStringLiteral("format");
        if (g == QLatin1String("availability"))
            return QStringLiteral("availability");
    }
    return {};
}

QString displayGroup(const QString &axis)
{
    if (axis == QLatin1String("genre")) return QStringLiteral("Genres");
    if (axis == QLatin1String("demographic")) return QStringLiteral("Demographics");
    if (axis == QLatin1String("publisher")) return QStringLiteral("Publishers");
    if (axis == QLatin1String("format")) return QStringLiteral("Formats");
    if (axis == QLatin1String("availability")) return QStringLiteral("Availability");
    return {};
}

QVariantList discoverFacets(const QString &type, MalCatalog &mal,
                            ComicsCatalog &comics, bool showExplicit,
                            const QString &axis)
{
    if (axis.isEmpty())
        return {};
    return type == QLatin1String("manga")
        ? mal.discoverFilters(axis, showExplicit)
        : comics.discoverFilters(axis, showExplicit);
}

bool facetExists(const QVariantList &facets, const QString &key)
{
    const QString wanted = key.toLower().trimmed();
    for (const QVariant &value : facets) {
        const QVariantMap row = value.toMap();
        const QString candidate = row.value(
            QStringLiteral("key"), row.value(QStringLiteral("value")))
            .toString().toLower().trimmed();
        if (candidate == wanted)
            return true;
    }
    return false;
}

QVariantList filterChoices(const QString &type, MalCatalog &mal,
                           ComicsCatalog &comics, bool showExplicit,
                           const QString &activeGroup,
                           const QString &activeKey)
{
    const QStringList axes = type == QLatin1String("manga")
        ? QStringList{QStringLiteral("genre"), QStringLiteral("demographic")}
        : QStringList{QStringLiteral("genre"), QStringLiteral("publisher"),
                      QStringLiteral("format"), QStringLiteral("availability")};
    QVariantList out;
    QVariantMap clear = selectedChoice(
        QStringLiteral("tankoban:filter:all"), QStringLiteral("All"),
        {{QStringLiteral("filterGroup"), QString()},
         {QStringLiteral("filterKey"), QString()}},
        activeKey.isEmpty());
    out.append(clear);
    for (const QString &axis : axes) {
        const QString group = displayGroup(axis);
        const QVariantList facets = discoverFacets(type, mal, comics,
                                                   showExplicit, axis);
        for (const QVariant &value : facets) {
            const QVariantMap row = value.toMap();
            const QString key = row.value(
                QStringLiteral("key"), row.value(QStringLiteral("value")))
                .toString().toLower().trimmed();
            const QString label = row.value(
                QStringLiteral("label"), row.value(QStringLiteral("value")))
                .toString();
            if (key.isEmpty() || label.isEmpty())
                continue;
            out.append(selectedChoice(
                QStringLiteral("tankoban:filter:") + axis + QLatin1Char(':') + key,
                group + QStringLiteral(" · ") + label,
                {{QStringLiteral("filterGroup"), group},
                 {QStringLiteral("filterKey"), key}},
                activeGroup == group && activeKey == key,
                row.value(QStringLiteral("count")).toString()));
        }
    }
    return out;
}

QVariantList catalogueChoices(const QString &type, const QString &selected)
{
    const QStringList ids = type == QLatin1String("manga")
        ? mangaCatalogues() : comicCatalogues();
    QVariantList out;
    for (const QString &id : ids) {
        out.append(selectedChoice(
            QStringLiteral("tankoban:catalogue:") + id,
            catalogueTitle(id),
            {{QStringLiteral("catalogue"), id},
             {QStringLiteral("filterGroup"), QString()},
             {QStringLiteral("filterKey"), QString()}},
            selected == id, QStringLiteral("Tankoban built-in catalogue")));
    }
    return out;
}

QVariantList typeChoices(const QString &selected)
{
    return {
        selectedChoice(
            QStringLiteral("tankoban:type:manga"), QStringLiteral("Manga"),
            {{QStringLiteral("type"), QStringLiteral("manga")},
             {QStringLiteral("catalogue"), QStringLiteral("popular")},
             {QStringLiteral("filterGroup"), QString()},
             {QStringLiteral("filterKey"), QString()}},
            selected == QLatin1String("manga")),
        selectedChoice(
            QStringLiteral("tankoban:type:comics"), QStringLiteral("Comics"),
            {{QStringLiteral("type"), QStringLiteral("comics")},
             {QStringLiteral("catalogue"), QStringLiteral("popular")},
             {QStringLiteral("filterGroup"), QString()},
             {QStringLiteral("filterKey"), QString()}},
            selected == QLatin1String("comics"))
    };
}

QVariantList buildDiscover(const FeedContext &ctx, MalCatalog &mal,
                           ComicsCatalog &comics)
{
    // Moved from TankobanDiscoverApi.js:23-40,171-309,350-508.
    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
    QString type = view.value(QStringLiteral("type"),
                              QStringLiteral("manga")).toString();
    if (type != QLatin1String("manga") && type != QLatin1String("comics"))
        type = QStringLiteral("manga");

    const QStringList validCatalogues = type == QLatin1String("manga")
        ? mangaCatalogues() : comicCatalogues();
    QString catalogue = view.value(QStringLiteral("catalogue"),
                                   QStringLiteral("popular")).toString();
    if (!validCatalogues.contains(catalogue))
        catalogue = QStringLiteral("popular");

    QString group = view.value(QStringLiteral("filterGroup")).toString();
    QString key = view.value(QStringLiteral("filterKey")).toString()
        .toLower().trimmed();
    QString axis = axisForGroup(type, group);
    if (axis.isEmpty()) {
        group.clear();
        key.clear();
    } else {
        const QVariantList facets = discoverFacets(
            type, mal, comics, ctx.showExplicit, axis);
        if (!key.isEmpty() && !facetExists(facets, key))
            key.clear();
        if (key.isEmpty()) {
            group.clear();
            axis.clear();
        }
    }

    QVariantList out;
    out.append(choiceSection(
        QStringLiteral("tankoban.discover.types"), out.size(), {},
        typeChoices(type)));
    out.append(choiceSection(
        QStringLiteral("tankoban.discover.catalogues"), out.size(), {},
        catalogueChoices(type, catalogue)));
    out.append(choiceSection(
        QStringLiteral("tankoban.discover.filters"), out.size(),
        QStringLiteral("Filter"),
        filterChoices(type, mal, comics, ctx.showExplicit, group, key)));

    const QVariantMap page = type == QLatin1String("manga")
        ? mal.discoverPage(catalogue, axis, key, ctx.showExplicit, 0, 24)
        : comics.discoverPage(catalogue, axis, key, ctx.showExplicit, 0, 24);
    const QString publicKind = type == QLatin1String("manga")
        ? QStringLiteral("manga") : QStringLiteral("comic");
    const QVariantList items = mapRows(
        page.value(QStringLiteral("items")).toList(), publicKind, 24);
    QVariantMap wall = WebFeedValue::section(
        QStringLiteral("tankoban.discover.wall"), out.size(),
        catalogueTitle(catalogue), QStringLiteral("grid"), items,
        items.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    wall.insert(QStringLiteral("emptyTitle"), QStringLiteral("Nothing here yet"));
    wall.insert(QStringLiteral("emptyText"),
        key.isEmpty() ? QStringLiteral("This catalogue answered with nothing.")
                      : QStringLiteral("No series match this filter."));
    setSeeAll(wall, catalogueRoute(publicKind, catalogue, axis, key,
                                    ctx.showExplicit));
    out.append(wall);
    return out;
}

QVariantList collectionLane(const FeedContext &ctx, MalCatalog *mal,
                            const QString &type)
{
    QVariantList out;
    for (const QVariant &value : ctx.collection) {
        QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("type")).toString() != type)
            continue;
        const QString libraryId = entry.value(QStringLiteral("id")).toString();
        entry.insert(QStringLiteral("libraryId"), libraryId);
        QVariantMap item;
        if (type == QLatin1String("manga") && mal) {
            if (libraryId.startsWith(QLatin1String("mal:"))) {
                entry.insert(QStringLiteral("mal_id"), libraryId.mid(4));
            } else {
                const QVariantList matches = mal->matchByTitle(
                    entry.value(QStringLiteral("title")).toString(), 0,
                    QStringLiteral("manga"));
                if (matches.size() == 1) {
                    const QString malId = matches.first().toMap()
                        .value(QStringLiteral("mal_id")).toString();
                    if (!malId.isEmpty()) {
                        entry.insert(QStringLiteral("id"),
                                     QStringLiteral("mal:") + malId);
                        entry.insert(QStringLiteral("mal_id"), malId);
                    }
                }
            }
            if (!entry.value(QStringLiteral("id")).toString()
                     .startsWith(QLatin1String("mal:")))
                continue;
            item = mangaItem(entry);
        } else {
            const QString id = entry.value(QStringLiteral("id")).toString();
            if (!id.startsWith(QLatin1String("gc:"))
                && !id.startsWith(QLatin1String("gcd:"))
                && !id.startsWith(QLatin1String("locg:"))
                && entry.value(QStringLiteral("gcdId")).toString().isEmpty()
                && entry.value(QStringLiteral("locgId")).toString().isEmpty())
                continue;
            item = comicItem(entry);
        }
        item.insert(QStringLiteral("primary"), QStringLiteral("details"));
        out.append(item);
    }
    return out;
}

QVariantList buildManga(const FeedContext &ctx, MalCatalog &mal)
{
    // Moved from TankobanMangaTab.qml:21-55.
    QVariantList out;
    QVariantMap saved = WebFeedValue::section(
        QStringLiteral("tankoban.manga.collection"), out.size(),
        QStringLiteral("Your Collection"), QStringLiteral("continue"),
        collectionLane(ctx, &mal, QStringLiteral("manga")));
    out.append(saved);

    QVariantMap top = WebFeedValue::section(
        QStringLiteral("tankoban.manga.top"), out.size(),
        QStringLiteral("Top in Tankoban — Manga"), QStringLiteral("rail"),
        topMangaItems(mal));
    setSeeAll(top, catalogueRoute(QStringLiteral("manga"),
                                  QStringLiteral("popular"), {}, {},
                                  ctx.showExplicit));
    out.append(top);

    QVariantMap genres = choiceSection(
        QStringLiteral("tankoban.manga.genres"), out.size(),
        QStringLiteral("Explore by Genre — Manga"),
        mangaGenreChoices(ctx.showExplicit), QStringLiteral("tiles"));
    setSeeAll(genres, genreIndexRoute(QStringLiteral("manga"),
                                      ctx.showExplicit));
    out.append(genres);
    return out;
}

QVariantList comicGenreChoices(ComicsCatalog &comics, bool showExplicit)
{
    // Moved from TankobanWorld.qml:152-164 and TankobanComicsTab.qml:89-99.
    QVariantList out;
    for (const QVariant &value : comics.curatedGenreShelves(8)) {
        const QVariantMap row = value.toMap();
        const QString name = row.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        const QVariantList covers = row.value(QStringLiteral("covers")).toList();
        const QString art = covers.isEmpty() ? QString() : covers.first().toString();
        out.append(WebFeedChoice::choice(
            QStringLiteral("tankoban:comic:genre:") + name.toLower(), name,
            {{QStringLiteral("route"),
              genreRoute(QStringLiteral("comic"), name, showExplicit)}},
            row.value(QStringLiteral("count")).toString(), art));
    }
    return out;
}

QVariantList buildComics(const FeedContext &ctx, ComicsCatalog &comics)
{
    // Moved from TankobanWorld.qml:99-165 initializeComicCatalogue() and
    // TankobanComicsTab.qml:28-99. This worker owns its SQLite connection.
    QVariantList out;
    out.append(WebFeedValue::section(
        QStringLiteral("tankoban.comics.collection"), out.size(),
        QStringLiteral("Your Collection"), QStringLiteral("continue"),
        collectionLane(ctx, nullptr, QStringLiteral("comic"))));

    QVariantMap top = WebFeedValue::section(
        QStringLiteral("tankoban.comics.top"), out.size(),
        QStringLiteral("Top in Tankoban — Comics"), QStringLiteral("rail"),
        mapRows(comics.curatedRanked(), QStringLiteral("comic"), 10));
    setSeeAll(top, catalogueRoute(QStringLiteral("comic"),
                                  QStringLiteral("popular"), {}, {},
                                  ctx.showExplicit));
    out.append(top);

    struct DiscoverShelf {
        const char *label;
        const char *catalogue;
        const char *axis;
        const char *key;
    };
    const DiscoverShelf discoverShelves[] = {
        {"Recently Available", "recently-available", "", ""},
        {"Complete Runs", "complete-runs", "", ""},
        {"Near Complete", "near-complete", "", ""},
        {"Omnibuses", "popular", "format", "omnibus"},
        {"Deluxe Editions", "popular", "format", "deluxe"},
        {"Graphic Novels", "popular", "format", "graphic novel"},
        {"Community Collections", "community-collections", "", ""}
    };
    for (const DiscoverShelf &spec : discoverShelves) {
        const QString catalogue = QString::fromLatin1(spec.catalogue);
        const QString axis = QString::fromLatin1(spec.axis);
        const QString key = QString::fromLatin1(spec.key);
        const QVariantMap page = comics.discoverPage(
            catalogue, axis, key, ctx.showExplicit, 0, kShelfLimit);
        QVariantMap section = WebFeedValue::section(
            QStringLiteral("tankoban.comics.catalogue.") + catalogue
                + QLatin1Char(':') + (key.isEmpty() ? QStringLiteral("all") : key),
            out.size(), QString::fromLatin1(spec.label), QStringLiteral("rail"),
            mapRows(page.value(QStringLiteral("items")).toList(),
                    QStringLiteral("comic"), kShelfLimit));
        setSeeAll(section, catalogueRoute(
            QStringLiteral("comic"), catalogue, axis, key, ctx.showExplicit));
        if (!section.value(QStringLiteral("items")).toList().isEmpty())
            out.append(section);
    }

    struct Shelf {
        const char *label;
        const char *kind;
        const char *arg;
    };
    const Shelf shelves[] = {
        {"Most Stocked", "stocked", ""},
        {"Marvel", "publisher", "Marvel"},
        {"DC", "publisher", "DC"},
        {"Image", "publisher", "Image"},
        {"The 2020s", "decade", "2020"},
        {"The 2010s", "decade", "2010"},
        {"Deep Shelves", "deep", ""},
        {"Fan-Made Shelf", "fanmade", ""}
    };
    for (const Shelf &spec : shelves) {
        const QString kind = QString::fromLatin1(spec.kind);
        const QString arg = QString::fromLatin1(spec.arg);
        QVariantMap section = WebFeedValue::section(
            QStringLiteral("tankoban.comics.shelf.") + kind
                + QLatin1Char(':') + arg.toLower(),
            out.size(), QString::fromLatin1(spec.label), QStringLiteral("rail"),
            mapRows(comics.shelf(kind, arg, kShelfLimit),
                    QStringLiteral("comic"), kShelfLimit));
        if (kind == QLatin1String("stocked")) {
            setSeeAll(section, catalogueRoute(
                QStringLiteral("comic"), QStringLiteral("most-stocked"),
                {}, {}, ctx.showExplicit));
        } else if (kind == QLatin1String("publisher")) {
            setSeeAll(section, catalogueRoute(
                QStringLiteral("comic"), QStringLiteral("popular"),
                QStringLiteral("publisher"), arg.toLower(), ctx.showExplicit));
        }
        if (!section.value(QStringLiteral("items")).toList().isEmpty())
            out.append(section);
    }

    QVariantMap genres = choiceSection(
        QStringLiteral("tankoban.comics.genres"), out.size(),
        QStringLiteral("Explore Comics"),
        comicGenreChoices(comics, ctx.showExplicit), QStringLiteral("tiles"));
    setSeeAll(genres, genreIndexRoute(QStringLiteral("comic"),
                                      ctx.showExplicit));
    out.append(genres);
    return out;
}

double unitNumber(const QString &label)
{
    // Moved from NextUp.js:113-116.
    static const QRegularExpression number(
        QStringLiteral("(\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch match = number.match(label);
    return match.hasMatch()
        ? match.captured(1).toDouble()
        : std::numeric_limits<double>::quiet_NaN();
}

QVariantMap nextUnit(const QString &finishedLabel, const QVariantList &units,
                     bool readyOnly)
{
    // Moved from NextUp.js:122-139.
    const double from = unitNumber(finishedLabel);
    if (std::isnan(from))
        return {};
    QVariantMap best;
    double bestNumber = std::numeric_limits<double>::infinity();
    for (const QVariant &value : units) {
        const QVariantMap unit = value.toMap();
        if (readyOnly && unit.value(QStringLiteral("state")).toString()
            != QLatin1String("ready"))
            continue;
        const QVariant numberValue = unit.value(QStringLiteral("number"));
        const double number = numberValue.isValid()
            ? numberValue.toDouble()
            : unitNumber(unit.value(QStringLiteral("label")).toString());
        if (std::isnan(number) || number <= from || number >= bestNumber)
            continue;
        best = unit;
        bestNumber = number;
    }
    if (best.isEmpty())
        return {};
    best.insert(QStringLiteral("_number"), bestNumber);
    return best;
}

QVariantList recentOfKind(const QVariantList &recent, const QString &kind,
                          int limit)
{
    QVariantList out;
    for (const QVariant &value : recent) {
        if (value.toMap().value(QStringLiteral("kind")).toString() != kind)
            continue;
        out.append(value);
        if (out.size() >= limit)
            break;
    }
    return out;
}

QVariantList nextUpItems(const FeedContext &ctx)
{
    // Moved from TankobanWorld.qml:34-64 nextUpRows() and
    // NextUp.js:95-157. Western comics stay excluded because their catalogue
    // is not linear.
    QVariantList candidates;
    QSet<QString> seen;
    const QVariantList manga = recentOfKind(
        ctx.recent, QStringLiteral("manga"), 24);
    const QVariantList tankoban = recentOfKind(
        ctx.recent, QStringLiteral("tankoban"), 24);
    for (const QVariantList &list : {manga, tankoban}) {
        for (const QVariant &value : list) {
            const QVariantMap row = value.toMap();
            const QString id = row.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || seen.contains(id))
                continue;
            seen.insert(id);
            if (row.value(QStringLiteral("watched")).toBool()
                || row.value(QStringLiteral("progress")).toDouble() >= 0.90)
                candidates.append(row);
        }
    }

    const QVariantList downloaded =
        ctx.nativeSnapshot.value(QStringLiteral("downloadedChapters")).toList();
    const QVariantMap volumesBySeries =
        ctx.nativeSnapshot.value(QStringLiteral("volumesBySeries")).toMap();

    QVariantList out;
    for (const QVariant &candidateValue : candidates) {
        if (out.size() >= kPersonalLimit)
            break;
        const QVariantMap candidate = candidateValue.toMap();
        const QString kind = candidate.value(QStringLiteral("kind")).toString();
        const QString seriesId = candidate.value(QStringLiteral("id")).toString();
        QVariantMap next;
        bool downloadedUnit = false;

        if (kind == QLatin1String("tankoban")) {
            QVariantList volumes = volumesBySeries.value(seriesId).toList();
            for (QVariant &value : volumes) {
                QVariantMap volume = value.toMap();
                if (!volume.contains(QStringLiteral("label")))
                    volume.insert(QStringLiteral("label"),
                                  QStringLiteral("Vol. ")
                                      + volume.value(QStringLiteral("number")).toString());
                value = volume;
            }
            next = nextUnit(candidate.value(QStringLiteral("sub")).toString(),
                            volumes, true);
            downloadedUnit = !next.isEmpty();
            if (next.isEmpty())
                next = nextUnit(candidate.value(QStringLiteral("sub")).toString(),
                                volumes, false);
        } else {
            QVariantList mine;
            for (const QVariant &value : downloaded) {
                const QVariantMap row = value.toMap();
                if (!row.value(QStringLiteral("missing")).toBool()
                    && row.value(QStringLiteral("seriesId")).toString() == seriesId)
                    mine.append(row);
            }
            next = nextUnit(candidate.value(QStringLiteral("sub")).toString(),
                            mine, false);
            downloadedUnit = !next.isEmpty();
            if (next.isEmpty()
                && !std::isnan(unitNumber(candidate.value(
                    QStringLiteral("sub")).toString()))) {
                next = {{QStringLiteral("id"), QString()},
                        {QStringLiteral("label"), QStringLiteral("Next chapter")}};
            }
        }
        if (next.isEmpty())
            continue;

        const QString unitId = next.value(QStringLiteral("id")).toString();
        const QString label = next.value(
            QStringLiteral("label"),
            kind == QLatin1String("tankoban")
                ? QStringLiteral("Vol. ")
                    + next.value(QStringLiteral("number")).toString()
                : QStringLiteral("Next chapter")).toString();

        QVariantMap row = candidate;
        row.insert(QStringLiteral("seriesId"), seriesId);
        row.insert(QStringLiteral("progress"), 0.0);
        row.insert(QStringLiteral("subtitle"),
                   label + (downloadedUnit
                       ? QString()
                       : QStringLiteral(" · not downloaded")));
        row.insert(QStringLiteral("resume"), QVariantMap{
            {QStringLiteral("chapterId"), unitId},
            {QStringLiteral("unitId"), unitId},
            {QStringLiteral("downloaded"), downloadedUnit}});
        QVariantMap item = WebFeedValue::item(
            row, QStringLiteral("Tankoban"), QStringLiteral("manga"), true);
        item.insert(QStringLiteral("primary"),
                    downloadedUnit && !unitId.isEmpty()
                        ? QStringLiteral("resume")
                        : QStringLiteral("details"));
        item.insert(QStringLiteral("badge"),
                    downloadedUnit ? QStringLiteral("Next Up")
                                   : QStringLiteral("Not downloaded"));
        out.append(item);
    }
    return out;
}

QVariantMap matchedProgress(const QVariantMap &entry,
                            const QVariantList &recent,
                            const QString &kind)
{
    const QString id = entry.value(QStringLiteral("id")).toString();
    const QString title = normalizedTitle(
        entry.value(QStringLiteral("title")).toString());
    QVariantMap fallback;
    for (const QVariant &value : recent) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("kind")).toString() != kind)
            continue;
        if (row.value(QStringLiteral("id")).toString() == id)
            return row;
        if (fallback.isEmpty()
            && normalizedTitle(row.value(QStringLiteral("title")).toString())
                == title)
            fallback = row;
    }
    return fallback;
}

QVariantMap collectionBase(QVariantMap entry, MalCatalog &mal)
{
    const QString libraryId = entry.value(QStringLiteral("id")).toString();
    entry.insert(QStringLiteral("libraryId"), libraryId);
    if (entry.value(QStringLiteral("type")).toString()
        == QLatin1String("manga")) {
        if (libraryId.startsWith(QLatin1String("mal:"))) {
            entry.insert(QStringLiteral("mal_id"), libraryId.mid(4));
        } else {
            const QVariantList matches = mal.matchByTitle(
                entry.value(QStringLiteral("title")).toString(), 0,
                QStringLiteral("manga"));
            if (matches.size() == 1) {
                const QString malId = matches.first().toMap()
                    .value(QStringLiteral("mal_id")).toString();
                if (!malId.isEmpty()) {
                    entry.insert(QStringLiteral("id"),
                                 QStringLiteral("mal:") + malId);
                    entry.insert(QStringLiteral("mal_id"), malId);
                }
            }
        }
        // CONTRACT §3.3 has no Tankoban:source:* manga identity. A stale
        // title-keyed Collection row is shown only after it recovers one
        // unambiguous MAL identity; otherwise omitting it is safer than
        // inventing a non-canonical key that cannot round-trip through open().
        if (!entry.value(QStringLiteral("id")).toString()
                 .startsWith(QLatin1String("mal:")))
            return {};
        return mangaItem(entry);
    }

    const QString comicId = entry.value(QStringLiteral("id")).toString();
    const bool canonicalComic = comicId.startsWith(QLatin1String("gc:"))
        || comicId.startsWith(QLatin1String("gcd:"))
        || comicId.startsWith(QLatin1String("locg:"))
        || !entry.value(QStringLiteral("gcdId")).toString().isEmpty()
        || !entry.value(QStringLiteral("locgId")).toString().isEmpty();
    if (!canonicalComic)
        return {};
    return comicItem(entry);
}

QVariantList itemMenu()
{
    return {QVariantMap{
        {QStringLiteral("key"), QStringLiteral("remove")},
        {QStringLiteral("label"), QStringLiteral("Remove from Library")},
        {QStringLiteral("warn"), true},
        {QStringLiteral("target"),
         QVariantMap{{QStringLiteral("act"), kRemoveAction}}}
    }};
}

struct LibraryRow {
    QVariantMap item;
    QString matchedId;
    QString collectionId;
    QString title;
    qint64 addedAt = 0;
    qint64 lastRead = 0;
    bool inProgress = false;
    bool downloaded = false;
    bool canonicalMatch = false;
};

QList<LibraryRow> libraryRows(const FeedContext &ctx, MalCatalog &mal)
{
    // Moved from TankobanLibraryApi.js:35-205. Manga uses the volume
    // Progress lane only (the current post-migration rule); comics stay in the
    // comic lane. Exact identity beats normalized-title fallback.
    QList<LibraryRow> rows;
    QSet<QString> downloaded;
    for (const QString &id : ctx.downloadedIds)
        downloaded.insert(id);
    for (const QVariant &value : ctx.collection) {
        const QVariantMap entry = value.toMap();
        const QString collectionId =
            entry.value(QStringLiteral("id")).toString();
        const QString type = entry.value(QStringLiteral("type")).toString()
            == QLatin1String("comic")
            ? QStringLiteral("comic") : QStringLiteral("manga");
        const QString progressKind = type == QLatin1String("comic")
            ? QStringLiteral("comic") : QStringLiteral("tankoban");
        const QVariantMap progress = matchedProgress(
            entry, ctx.recent, progressKind);
        QVariantMap identityEntry = entry;
        // A legacy title-keyed manga row may already have canonical Progress.
        // Prefer that native durable identity before consulting the catalogue.
        if (type == QLatin1String("manga")) {
            const QString progressId = progress.value(QStringLiteral("id")).toString();
            if (progressId.startsWith(QLatin1String("mal:"))) {
                identityEntry.insert(QStringLiteral("id"), progressId);
                identityEntry.insert(QStringLiteral("mal_id"), progressId.mid(4));
            }
        }
        QVariantMap base = collectionBase(identityEntry, mal);
        if (base.isEmpty())
            continue;
        QVariantMap item = base;

        LibraryRow row;
        row.collectionId = collectionId;
        row.title = normalizedTitle(
            entry.value(QStringLiteral("title")).toString());
        row.addedAt = entry.value(QStringLiteral("addedAt")).toLongLong();

        if (!progress.isEmpty()) {
            QVariantMap resume = progress;
            resume.insert(QStringLiteral("libraryId"), collectionId);
            QVariantMap projected = WebFeedValue::item(
                resume, QStringLiteral("Tankoban"), type, true);
            projected.insert(QStringLiteral("key"),
                             base.value(QStringLiteral("key")));
            projected.insert(QStringLiteral("title"),
                             base.value(QStringLiteral("title")));
            if (base.contains(QStringLiteral("cover")))
                projected.insert(QStringLiteral("cover"),
                                 base.value(QStringLiteral("cover")));
            projected.insert(QStringLiteral("primary"),
                             QStringLiteral("resume"));
            row.matchedId =
                progress.value(QStringLiteral("id")).toString();
            row.canonicalMatch = row.matchedId == collectionId;
            row.lastRead =
                progress.value(QStringLiteral("updatedAt")).toLongLong();
            row.inProgress = true;

            if (progressKind != QLatin1String("tankoban")) {
                const QString chapterId = progress.value(
                    QStringLiteral("resume")).toMap()
                    .value(QStringLiteral("chapterId")).toString();
                row.downloaded = downloaded.contains(chapterId);
            }
            projected.insert(QStringLiteral("badge"),
                             row.downloaded
                                 ? QStringLiteral("Downloaded")
                                 : QStringLiteral("In progress"));
            item = projected;
        } else {
            item.insert(QStringLiteral("primary"),
                        QStringLiteral("details"));
        }
        item.insert(QStringLiteral("menu"), itemMenu());
        row.item = item;
        rows.append(row);
    }

    // TankobanLibraryApi.js:156-187 duplicate suppression. If a canonical
    // collection row and a legacy title-keyed row resolve to the same progress
    // record, keep the canonical row.
    QHash<QString, int> claimed;
    QList<LibraryRow> deduped;
    for (const LibraryRow &row : rows) {
        if (row.matchedId.isEmpty()) {
            deduped.append(row);
            continue;
        }
        if (!claimed.contains(row.matchedId)) {
            claimed.insert(row.matchedId, deduped.size());
            deduped.append(row);
            continue;
        }
        const int index = claimed.value(row.matchedId);
        if (row.canonicalMatch && !deduped.at(index).canonicalMatch)
            deduped[index] = row;
    }
    return deduped;
}

QVariantList libraryControls(const QList<LibraryRow> &rows,
                             const QVariantMap &view)
{
    const QString filter = view.value(QStringLiteral("filter")).toString();
    const QString sort = view.value(
        QStringLiteral("sort"), QStringLiteral("lastRead")).toString();
    int inProgress = 0;
    int downloaded = 0;
    for (const LibraryRow &row : rows) {
        if (row.inProgress) ++inProgress;
        if (row.downloaded) ++downloaded;
    }

    QVariantList sections;
    sections.append(choiceSection(
        QStringLiteral("tankoban.library.filters"), sections.size(), {},
        {selectedChoice(
             QStringLiteral("tankoban:library:all"), QStringLiteral("All"),
             {{QStringLiteral("filter"), QString()}}, filter.isEmpty(),
             QString::number(rows.size())),
         selectedChoice(
             QStringLiteral("tankoban:library:progress"),
             QStringLiteral("In Progress"),
             {{QStringLiteral("filter"), QStringLiteral("inProgress")}},
             filter == QLatin1String("inProgress"),
             QString::number(inProgress)),
         selectedChoice(
             QStringLiteral("tankoban:library:downloaded"),
             QStringLiteral("Downloaded"),
             {{QStringLiteral("filter"), QStringLiteral("downloaded")}},
             filter == QLatin1String("downloaded"),
             QString::number(downloaded))}));
    sections.append(choiceSection(
        QStringLiteral("tankoban.library.sort"), sections.size(), {},
        {selectedChoice(
             QStringLiteral("tankoban:library:last-read"),
             QStringLiteral("Last Read"),
             {{QStringLiteral("sort"), QStringLiteral("lastRead")}},
             sort == QLatin1String("lastRead")),
         selectedChoice(
             QStringLiteral("tankoban:library:added"),
             QStringLiteral("Recently Added"),
             {{QStringLiteral("sort"), QStringLiteral("added")}},
             sort == QLatin1String("added")),
         selectedChoice(
             QStringLiteral("tankoban:library:az"),
             QStringLiteral("A–Z"),
             {{QStringLiteral("sort"), QStringLiteral("az")}},
             sort == QLatin1String("az"))}));
    return sections;
}

QVariantList buildLibrary(const FeedContext &ctx, MalCatalog &mal)
{
    // CollectionStore::items() is copied on the GUI thread by the bridge.
    // TankobanLibraryTab.qml:36-46,77-101 and TankobanLibraryApi.js:221-266
    // move here as native query/filter/sort projection.
    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
    QString filter = view.value(QStringLiteral("filter")).toString();
    if (!QStringList{QString(), QStringLiteral("inProgress"),
                     QStringLiteral("downloaded")}.contains(filter))
        filter.clear();
    QString sort = view.value(
        QStringLiteral("sort"), QStringLiteral("lastRead")).toString();
    if (!QStringList{QStringLiteral("lastRead"), QStringLiteral("added"),
                     QStringLiteral("az")}.contains(sort))
        sort = QStringLiteral("lastRead");
    const QString query = view.value(QStringLiteral("query"))
        .toString().trimmed().toLower();

    QList<LibraryRow> all = libraryRows(ctx, mal);
    QVariantList out = libraryControls(all, view);

    QList<LibraryRow> visible;
    for (const LibraryRow &row : all) {
        if (filter == QLatin1String("inProgress") && !row.inProgress)
            continue;
        if (filter == QLatin1String("downloaded") && !row.downloaded)
            continue;
        if (!query.isEmpty()
            && !row.title.contains(query))
            continue;
        visible.append(row);
    }

    std::stable_sort(visible.begin(), visible.end(),
        [&sort](const LibraryRow &a, const LibraryRow &b) {
            if (sort == QLatin1String("added")) {
                if (a.addedAt != b.addedAt)
                    return a.addedAt > b.addedAt;
                if (a.title != b.title)
                    return a.title < b.title;
                return a.collectionId < b.collectionId;
            }
            if (sort == QLatin1String("az")) {
                if (a.title != b.title)
                    return a.title < b.title;
                return a.collectionId < b.collectionId;
            }
            if (a.lastRead != b.lastRead)
                return a.lastRead > b.lastRead;
            if (a.addedAt != b.addedAt)
                return a.addedAt > b.addedAt;
            return a.title < b.title;
        });

    QVariantList items;
    for (const LibraryRow &row : visible)
        items.append(row.item);
    QVariantMap section = WebFeedValue::section(
        QStringLiteral("tankoban.library.saved"), out.size(),
        QStringLiteral("Library"), QStringLiteral("grid"), items,
        items.isEmpty() ? QStringLiteral("empty")
                        : QStringLiteral("ready"));
    section.insert(QStringLiteral("emptyTitle"),
                   all.isEmpty() ? QStringLiteral("Your library is empty")
                                 : QStringLiteral("No matches"));
    section.insert(QStringLiteral("emptyText"),
                   all.isEmpty()
                       ? QStringLiteral("Save a manga or comic series — it lands here.")
                       : QStringLiteral("Try a different filter or search."));
    out.append(section);
    return out;
}

void capturePersonalState(ColosseumWebBridge &bridge, FeedContext &ctx)
{
    // CONTRACT §5.5: GUI-owned downloader/volume services are touched only
    // here on the GUI thread. The worker sees immutable QVariant snapshots.
    if (auto *downloads = qobject_cast<MangaDownloader *>(
            bridge.service(QStringLiteral("Downloads")))) {
        const QVariantList chapters = downloads->downloadedChapters();
        ctx.nativeSnapshot.insert(QStringLiteral("downloadedChapters"),
                                  chapters);
        for (const QVariant &value : chapters) {
            const QVariantMap row = value.toMap();
            if (!row.value(QStringLiteral("missing")).toBool())
                ctx.downloadedIds.append(
                    row.value(QStringLiteral("id")).toString());
        }
    }
    if (auto *volumes = qobject_cast<MangaTankobanService *>(
            bridge.service(QStringLiteral("TankobanVolumes")))) {
        QVariantMap bySeries;
        for (const QVariant &value : recentOfKind(
                 ctx.recent, QStringLiteral("tankoban"), 24)) {
            const QString id = value.toMap()
                .value(QStringLiteral("id")).toString();
            if (!id.isEmpty() && !bySeries.contains(id))
                bySeries.insert(id, volumes->volumesForSeries(id));
        }
        ctx.nativeSnapshot.insert(QStringLiteral("volumesBySeries"),
                                  bySeries);
    }
}

QVariantList initial(const QVariantMap &params)
{
    const QString tab = params.value(QStringLiteral("tab")).toString();
    return {
        WebFeedValue::section(
            QStringLiteral("tankoban.chrome.featured"), 0,
            QStringLiteral("Featured in Tankoban"), QStringLiteral("hero"),
            {}, QStringLiteral("loading")),
        WebFeedValue::section(
            QStringLiteral("tankoban.chrome.nextUp"), 1,
            QStringLiteral("Next Up"), QStringLiteral("continue"),
            {}, QStringLiteral("loading")),
        WebFeedValue::section(
            QStringLiteral("tankoban.") + tab + QStringLiteral(".loading"), 2,
            QStringLiteral("Tankoban"), QStringLiteral("grid"),
            {}, QStringLiteral("loading"))
    };
}

bool validView(const QString &tab, const QVariantMap &view)
{
    for (auto it = view.cbegin(); it != view.cend(); ++it) {
        if (it.value().metaType().id() != QMetaType::QString)
            return false;
        const QString key = it.key();
        if (tab == QLatin1String("discover")) {
            if (!QStringList{QStringLiteral("type"),
                             QStringLiteral("catalogue"),
                             QStringLiteral("filterGroup"),
                             QStringLiteral("filterKey")}.contains(key))
                return false;
        } else if (tab == QLatin1String("library")) {
            if (!QStringList{QStringLiteral("filter"),
                             QStringLiteral("sort"),
                             QStringLiteral("query")}.contains(key))
                return false;
        } else {
            return false;
        }
    }
    return true;
}

bool valid(const QVariantMap &params)
{
    if (params.value(QStringLiteral("world")).toString()
        != QLatin1String("Tankoban"))
        return false;
    const QString tab = params.value(QStringLiteral("tab")).toString();
    if (!QStringList{QStringLiteral("discover"), QStringLiteral("manga"),
                     QStringLiteral("comics"), QStringLiteral("library")}
             .contains(tab))
        return false;
    if (!params.contains(QStringLiteral("view")))
        return true;
    if (!params.value(QStringLiteral("view")).canConvert<QVariantMap>())
        return false;
    return validView(tab, params.value(QStringLiteral("view")).toMap());
}

QVariantList build(const FeedContext &ctx)
{
    const QString tab = ctx.params.value(QStringLiteral("tab")).toString();
    QVariantList out;

    // TankobanWorld.qml:171-197. These two personal sections are above the
    // tab bar on every tab; Continue Reading itself reuses the dedicated
    // the Continue feed in the web surface, exactly as Theatre does.
    out.append(WebFeedValue::section(
        QStringLiteral("tankoban.chrome.featured"), out.size(),
        QStringLiteral("Featured in Tankoban"), QStringLiteral("hero"),
        featuredItems()));
    const QVariantList nextUp = nextUpItems(ctx);
    out.append(WebFeedValue::section(
        QStringLiteral("tankoban.chrome.nextUp"), out.size(),
        QStringLiteral("Next Up"), QStringLiteral("continue"), nextUp,
        nextUp.isEmpty() ? QStringLiteral("empty")
                         : QStringLiteral("ready")));

    QVariantList body;
    if (tab == QLatin1String("discover")) {
        MalCatalog mal(ctx.paths.mal, nullptr,
            QStringLiteral("web_tankoban_discover_mal_")
                + QUuid::createUuid().toString(QUuid::WithoutBraces));
        ComicsCatalog comics(ctx.paths.comics);
        // TankobanDiscoverPage.qml:82-105 gates readiness by the CURRENT type.
        // A first-run Comics vault fetch must not blank an already-ready Manga wall,
        // and a missing MAL database must not suppress a ready Comics catalogue.
        const QString type = ctx.params.value(QStringLiteral("view")).toMap()
            .value(QStringLiteral("type"), QStringLiteral("manga")).toString();
        if ((type == QLatin1String("comics") && comics.ready())
            || (type != QLatin1String("comics") && mal.ready()))
            body = buildDiscover(ctx, mal, comics);
    } else if (tab == QLatin1String("manga")) {
        MalCatalog mal(ctx.paths.mal, nullptr,
            QStringLiteral("web_tankoban_manga_mal_")
                + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (mal.ready())
            body = buildManga(ctx, mal);
    } else if (tab == QLatin1String("comics")) {
        ComicsCatalog comics(ctx.paths.comics);
        if (comics.ready())
            body = buildComics(ctx, comics);
    } else if (tab == QLatin1String("library")) {
        MalCatalog mal(ctx.paths.mal, nullptr,
            QStringLiteral("web_tankoban_library_mal_")
                + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (mal.ready())
            body = buildLibrary(ctx, mal);
    }

    if (body.isEmpty()) {
        QVariantMap error = WebFeedValue::section(
            QStringLiteral("tankoban.") + tab
                + QStringLiteral(".unavailable"),
            0, QStringLiteral("Tankoban"), QStringLiteral("grid"),
            {}, QStringLiteral("error"));
        error.insert(QStringLiteral("error"),
                     QStringLiteral("Tankoban catalogue data is unavailable."));
        body.append(error);
    }
    for (QVariant value : body) {
        QVariantMap section = value.toMap();
        section.insert(QStringLiteral("index"), out.size());
        out.append(section);
    }
    return out;
}

QVariantList enrich(const FeedContext &ctx)
{
    // TankobanDiscoverApi.js:350-427. The bundled SQLite wall paints in the
    // first worker pass; this second worker pass performs the non-blocking
    // Jikan refresh and only enriches the same MAL identities in place.
    if (ctx.params.value(QStringLiteral("tab")).toString()
            != QLatin1String("discover"))
        return ctx.baseSections;
    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
    const QString type = view.value(
        QStringLiteral("type"), QStringLiteral("manga")).toString();
    if (type != QLatin1String("manga")
        || !view.value(QStringLiteral("filterKey")).toString().isEmpty())
        return ctx.baseSections;

    QString catalogue = view.value(
        QStringLiteral("catalogue"), QStringLiteral("popular")).toString();
    if (!mangaCatalogues().contains(catalogue))
        catalogue = QStringLiteral("popular");

    QUrl url(QStringLiteral("https://api.jikan.moe/v4/top/manga"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("manga"));
    query.addQueryItem(QStringLiteral("order_by"),
        catalogue == QLatin1String("top-rated")
            ? QStringLiteral("score") : QStringLiteral("members"));
    query.addQueryItem(QStringLiteral("sort"), QStringLiteral("desc"));
    query.addQueryItem(QStringLiteral("sfw"),
        ctx.showExplicit ? QStringLiteral("false")
                         : QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
    url.setQuery(query);
    const WebFeedHttp::Reply reply = WebFeedHttp::request(url);
    if (!reply.ok || !reply.json.isObject())
        return ctx.baseSections;

    QHash<QString, QVariantMap> live;
    for (const QJsonValue &value : reply.json.object()
             .value(QStringLiteral("data")).toArray()) {
        const QVariantMap row = value.toObject().toVariantMap();
        const QVariantMap item = mangaItem(row);
        if (!item.value(QStringLiteral("key")).toString().isEmpty())
            live.insert(item.value(QStringLiteral("key")).toString(), item);
    }
    if (live.isEmpty())
        return ctx.baseSections;

    QVariantList updated = ctx.baseSections;
    for (QVariant &value : updated) {
        QVariantMap section = value.toMap();
        if (section.value(QStringLiteral("id")).toString()
            != QLatin1String("tankoban.discover.wall"))
            continue;
        QVariantList items = section.value(QStringLiteral("items")).toList();
        for (QVariant &itemValue : items) {
            QVariantMap item = itemValue.toMap();
            const QVariantMap fresh = live.value(
                item.value(QStringLiteral("key")).toString());
            if (fresh.isEmpty())
                continue;
            for (const QString &field : {
                     QStringLiteral("title"), QStringLiteral("cover"),
                     QStringLiteral("year"), QStringLiteral("rating")}) {
                if (fresh.contains(field)
                    && !fresh.value(field).toString().isEmpty())
                    item.insert(field, fresh.value(field));
            }
            itemValue = item;
        }
        section.insert(QStringLiteral("items"), items);
        value = section;
        break;
    }
    return updated;
}

QMetaObject::Connection bindProgress(QObject *owner, QObject *receiver,
                                     std::function<void()> refresh)
{
    auto *progress = qobject_cast<ProgressStore *>(owner);
    if (!progress)
        return {};
    // CONTRACT §3.2: progress-driven feed events are coalesced per
    // subscription to at most one refresh per second. recordSilent() already
    // suppresses the 5-second playback tick; this also folds lifecycle bursts.
    const auto pending = QSharedPointer<bool>::create(false);
    return QObject::connect(progress, &ProgressStore::changed, receiver,
        [receiver, refresh, pending] {
            if (*pending)
                return;
            *pending = true;
            QTimer::singleShot(1000, receiver, [refresh, pending] {
                *pending = false;
                refresh();
            });
        });
}

QMetaObject::Connection bindDownloads(QObject *owner, QObject *receiver,
                                      std::function<void()> refresh)
{
    auto *downloads = qobject_cast<MangaDownloader *>(owner);
    return downloads
        ? QObject::connect(downloads, &MangaDownloader::finished, receiver,
                           [refresh](const QString &) { refresh(); })
        : QMetaObject::Connection{};
}

QMetaObject::Connection bindDownloadRemoved(QObject *owner, QObject *receiver,
                                            std::function<void()> refresh)
{
    auto *downloads = qobject_cast<MangaDownloader *>(owner);
    return downloads
        ? QObject::connect(downloads, &MangaDownloader::removed, receiver,
                           [refresh](const QString &) { refresh(); })
        : QMetaObject::Connection{};
}

QMetaObject::Connection bindVolumes(QObject *owner, QObject *receiver,
                                    std::function<void()> refresh)
{
    auto *volumes = qobject_cast<MangaTankobanService *>(owner);
    return volumes
        ? QObject::connect(volumes, &MangaTankobanService::volumesChanged,
                           receiver,
                           [refresh](const QString &) { refresh(); })
        : QMetaObject::Connection{};
}

QMetaObject::Connection bindVolumeFinished(QObject *owner, QObject *receiver,
                                           std::function<void()> refresh)
{
    auto *volumes = qobject_cast<MangaTankobanService *>(owner);
    return volumes
        ? QObject::connect(volumes, &MangaTankobanService::finished,
                           receiver,
                           [refresh](const QString &) { refresh(); })
        : QMetaObject::Connection{};
}

const bool feedRegistered = [] {
    FeedRegistry::Entry entry;
    entry.name = QStringLiteral("world");
    entry.selector = QStringLiteral("Tankoban");
    entry.valid = &valid;
    entry.initial = &initial;
    entry.build = &build;
    entry.needsProgress = true;
    entry.needsCollection = true;
    entry.needsExtensions = true;
    entry.enrich = &enrich;
    entry.capture = &capturePersonalState;
    entry.ownerSignals = {
        {QStringLiteral("Progress"), &bindProgress},
        {QStringLiteral("Downloads"), &bindDownloads},
        {QStringLiteral("Downloads"), &bindDownloadRemoved},
        {QStringLiteral("TankobanVolumes"), &bindVolumes},
        {QStringLiteral("TankobanVolumes"), &bindVolumeFinished}
    };
    return FeedRegistry::add(entry);
}();

const bool removeRegistered = ActionRegistry::add({
    kRemoveAction,
    [](const QVariantMap &payload) {
        const QVariantMap item = payload.value(QStringLiteral("item")).toMap();
        return item.value(QStringLiteral("world")).toString()
            == QLatin1String("Tankoban")
            && !item.value(QStringLiteral("ref")).toMap().isEmpty();
    },
    [](ColosseumWebBridge &bridge, const QVariantMap &payload,
       ActionRegistry::Completion done) {
        auto *collection = qobject_cast<CollectionStore *>(
            bridge.service(QStringLiteral("Collection")));
        const QVariantMap ref = payload.value(
            QStringLiteral("item")).toMap()
            .value(QStringLiteral("ref")).toMap();
        QString id = ref.value(QStringLiteral("libraryId")).toString();
        if (id.isEmpty())
            id = ref.value(QStringLiteral("id")).toString();
        if (!collection || id.isEmpty()) {
            done({{QStringLiteral("ok"), false},
                  {QStringLiteral("error"),
                   QStringLiteral("Collection item is unavailable.")}});
            return;
        }
        const bool ok = collection->remove(QStringLiteral("tankoban"), id);
        done(ok
            ? QVariantMap{{QStringLiteral("ok"), true}}
            : QVariantMap{{QStringLiteral("ok"), false},
                          {QStringLiteral("error"),
                           QStringLiteral("Collection could not be changed.")}});
    }});

} // namespace
