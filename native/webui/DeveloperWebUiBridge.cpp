#include "DeveloperWebUiBridge.h"

#include "../CollectionStore.h"
#include "../ProgressStore.h"
#include "../engine/BiblioCatalog.h"
#include "../engine/ComicsCatalog.h"
#include "../engine/ExtensionsStore.h"
#include "../engine/ImdbCatalog.h"
#include "../engine/MalCatalog.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace {

QVariantList mapItems(const QVariantMap &page)
{
    return page.value(QStringLiteral("items")).toList();
}

QString exploreTitle(const QString &catalogId)
{
    static const QHash<QString, QString> titles{
        {QStringLiteral("popular"), QStringLiteral("Popular")},
        {QStringLiteral("top-rated"), QStringLiteral("Top Rated")},
        {QStringLiteral("new-releases"), QStringLiteral("New Releases")},
        {QStringLiteral("trending"), QStringLiteral("Trending")},
        {QStringLiteral("most-read"), QStringLiteral("Most Read")},
        {QStringLiteral("classics"), QStringLiteral("Classics")}
    };
    return titles.value(catalogId, catalogId);
}

QVariantMap queryMap(std::initializer_list<std::pair<QString, QVariant>> entries)
{
    QVariantMap out;
    for (const auto &entry : entries)
        out.insert(entry.first, entry.second);
    return out;
}

QVariantList mergeRecent(std::initializer_list<QVariantList> groups, int limit)
{
    QVariantList out;
    for (const QVariantList &group : groups)
        out.append(group);
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("updatedAt")).toLongLong()
             > b.toMap().value(QStringLiteral("updatedAt")).toLongLong();
    });
    if (limit > 0 && out.size() > limit)
        out = out.mid(0, limit);
    return out;
}

} // namespace

DeveloperWebUiBridge::DeveloperWebUiBridge(
    MalCatalog *malCatalog,
    ComicsCatalog *comicsCatalog,
    BiblioCatalog *biblioCatalog,
    ImdbCatalog *imdbCatalog,
    ExtensionsStore *extensions,
    QObject *parent)
    : QObject(parent),
      m_malCatalog(malCatalog),
      m_comicsCatalog(comicsCatalog),
      m_biblioCatalog(biblioCatalog),
      m_imdbCatalog(imdbCatalog),
      m_extensions(extensions)
{
    setObjectName(QStringLiteral("developerWebUiBridge"));

    if (m_malCatalog)
        connect(m_malCatalog, &MalCatalog::readyChanged,
                this, &DeveloperWebUiBridge::bump);
    if (m_comicsCatalog)
        connect(m_comicsCatalog, &ComicsCatalog::readyChanged,
                this, &DeveloperWebUiBridge::bump);
    if (m_imdbCatalog)
        connect(m_imdbCatalog, &ImdbCatalog::readyChanged,
                this, &DeveloperWebUiBridge::bump);
    if (m_biblioCatalog) {
        connect(m_biblioCatalog, &BiblioCatalog::readyChanged,
                this, &DeveloperWebUiBridge::bump);
        connect(m_biblioCatalog, &BiblioCatalog::revisionChanged,
                this, &DeveloperWebUiBridge::bump);
    }
    if (m_extensions)
        connect(m_extensions, &ExtensionsStore::changed,
                this, &DeveloperWebUiBridge::bump);
}

QString DeveloperWebUiBridge::resourceUrl() const
{
    return QStringLiteral("qrc:///developer-webui/index.html");
}

void DeveloperWebUiBridge::setWallpaper(const QString &wallpaper)
{
    if (m_wallpaper == wallpaper)
        return;
    m_wallpaper = wallpaper;
    emit wallpaperChanged();
    if (m_clientReady) {
        emit patchReady(QVariantMap{
            {QStringLiteral("wallpaper"), m_wallpaper}
        });
    }
}
void DeveloperWebUiBridge::bindPersonalStores(
    ProgressStore *progress, CollectionStore *collection)
{
    if (m_progressChanged)
        disconnect(m_progressChanged);
    if (m_collectionChanged)
        disconnect(m_collectionChanged);

    m_progress = progress;
    m_collection = collection;

    if (m_progress) {
        m_progressChanged = connect(
            m_progress, &ProgressStore::changed,
            this, &DeveloperWebUiBridge::bump);
    }
    if (m_collection) {
        m_collectionChanged = connect(
            m_collection, &CollectionStore::changed,
            this, &DeveloperWebUiBridge::bump);
    }
}

QVariantList DeveloperWebUiBridge::recent(
    const QString &kind, int limit) const
{
    return m_progress ? m_progress->recent(kind, limit) : QVariantList{};
}

QVariantList DeveloperWebUiBridge::collection(
    const QString &world) const
{
    return m_collection ? m_collection->items(world) : QVariantList{};
}

QVariantMap DeveloperWebUiBridge::normalizeRow(
    const QVariantMap &row, const QString &world, const QString &kind)
{
    QVariantMap out = row;
    if (!world.isEmpty())
        out.insert(QStringLiteral("world"), world);

    QString id = out.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = out.value(QStringLiteral("tt")).toString();
    if (id.isEmpty())
        id = out.value(QStringLiteral("mal_id")).toString();
    if (id.isEmpty())
        id = out.value(QStringLiteral("locgId")).toString();
    if (id.isEmpty())
        id = out.value(QStringLiteral("gcdId")).toString();
    if (!id.isEmpty())
        out.insert(QStringLiteral("id"), id);

    if (!kind.isEmpty() && out.value(QStringLiteral("kind")).toString().isEmpty())
        out.insert(QStringLiteral("kind"), kind);
    if (out.value(QStringLiteral("title")).toString().isEmpty()) {
        const QString titleEnglish =
            out.value(QStringLiteral("title_english")).toString();
        if (!titleEnglish.isEmpty())
            out.insert(QStringLiteral("title"), titleEnglish);
    }

    QString cover = out.value(QStringLiteral("cover")).toString();
    if (cover.isEmpty())
        cover = out.value(QStringLiteral("coverUrl")).toString();
    if (cover.isEmpty()) {
        const QVariantMap images = out.value(QStringLiteral("images")).toMap();
        const QVariantMap jpg = images.value(QStringLiteral("jpg")).toMap();
        cover = jpg.value(QStringLiteral("large_image_url")).toString();
        if (cover.isEmpty())
            cover = jpg.value(QStringLiteral("image_url")).toString();
    }
    const QString tt = out.value(QStringLiteral("tt")).toString();
    if (cover.isEmpty() && tt.startsWith(QStringLiteral("tt"))) {
        cover = QStringLiteral("https://images.metahub.space/poster/small/%1/img")
                    .arg(tt);
    }
    if (!cover.isEmpty())
        out.insert(QStringLiteral("cover"), cover);

    if (out.value(QStringLiteral("type")).toString().isEmpty()
        && !kind.isEmpty()) {
        out.insert(QStringLiteral("type"), kind);
    }
    return out;
}

QVariantList DeveloperWebUiBridge::normalizeRows(
    const QVariantList &rows, const QString &world, const QString &kind)
{
    QVariantList out;
    out.reserve(rows.size());
    for (const QVariant &value : rows)
        out.append(normalizeRow(value.toMap(), world, kind));
    return out;
}

QVariantMap DeveloperWebUiBridge::section(
    const QString &title, const QVariantList &items,
    const QString &layout, bool seeAll, const QVariantMap &pin)
{
    QVariantMap out{
        {QStringLiteral("title"), title},
        {QStringLiteral("layout"), layout},
        {QStringLiteral("items"), items}
    };
    if (seeAll)
        out.insert(QStringLiteral("seeAll"), true);
    if (!pin.isEmpty())
        out.insert(QStringLiteral("pin"), pin);
    return out;
}

QVariantMap DeveloperWebUiBridge::page(
    const QString &world, const QString &catalog,
    int offset, int limit) const
{
    QVariantMap raw;
    if (world == QLatin1String("manga") && m_malCatalog && m_malCatalog->ready()) {
        raw = m_malCatalog->discoverPage(
            catalog, QString(), QString(), false, offset, limit);
        raw.insert(QStringLiteral("items"),
                   normalizeRows(mapItems(raw), QStringLiteral("Tankoban"),
                                 QStringLiteral("manga")));
    } else if (world == QLatin1String("comics")
               && m_comicsCatalog && m_comicsCatalog->ready()) {
        raw = m_comicsCatalog->discoverPage(
            catalog, QString(), QString(), false, offset, limit);
        raw.insert(QStringLiteral("items"),
                   normalizeRows(mapItems(raw), QStringLiteral("Tankoban"),
                                 QStringLiteral("comics")));
    } else if (world == QLatin1String("biblio")
               && m_biblioCatalog && m_biblioCatalog->ready()) {
        raw = m_biblioCatalog->discoverPage(
            catalog, QString(), QString(), false, offset, limit);
        raw.insert(QStringLiteral("items"),
                   normalizeRows(mapItems(raw), QStringLiteral("Biblio"),
                                 QStringLiteral("book")));
    }
    return raw;
}
QVariantList DeveloperWebUiBridge::theatreRows(
    const QString &tab, int limit) const
{
    if (!m_imdbCatalog || !m_imdbCatalog->ready())
        return {};

    QVariantMap query;
    QString kind;
    if (tab == QLatin1String("shows")) {
        query = queryMap({
            {QStringLiteral("type"), QStringLiteral("series")},
            {QStringLiteral("order"), QStringLiteral("rating")},
            {QStringLiteral("ratingMin"), 8.2},
            {QStringLiteral("votesMin"), 100000},
            {QStringLiteral("excludeAnime"), true}
        });
        kind = QStringLiteral("series");
    } else {
        query = queryMap({
            {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("order"), QStringLiteral("rating")},
            {QStringLiteral("ratingMin"), 8.0},
            {QStringLiteral("votesMin"), 200000},
            {QStringLiteral("excludeAnime"), true}
        });
        kind = QStringLiteral("movie");
    }
    return normalizeRows(
        m_imdbCatalog->titleCatalog(query, 0, limit),
        QStringLiteral("Theatre"), kind);
}

QVariantList DeveloperWebUiBridge::theatreAnimeRows(int limit) const
{
    if (!m_malCatalog || !m_malCatalog->ready())
        return {};

    const QVariantMap query{
        {QStringLiteral("order"), QStringLiteral("score")},
        {QStringLiteral("voteFloor"), 5000}
    };
    return normalizeRows(
        m_malCatalog->animeCatalog(query, 0, limit),
        QStringLiteral("Theatre"), QStringLiteral("anime"));
}

QVariantList DeveloperWebUiBridge::biblioExploreSections(int limit) const
{
    QVariantList sections;
    if (!m_biblioCatalog || !m_biblioCatalog->ready())
        return sections;

    const QVariantList rows = m_biblioCatalog->exploreRows(limit, false);
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString catalogId =
            row.value(QStringLiteral("catalogId")).toString();
        sections.append(section(
            exploreTitle(catalogId),
            normalizeRows(row.value(QStringLiteral("items")).toList(),
                          QStringLiteral("Biblio"), QStringLiteral("book")),
            QStringLiteral("rail"), true,
            QVariantMap{{QStringLiteral("catalogId"), catalogId}}));
    }
    return sections;
}
QVariantList DeveloperWebUiBridge::universeRows() const
{
    QVariantList out;
    if (!m_extensions)
        return out;

    for (const QVariant &value : m_extensions->installed()) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("enabled")).toBool() != true)
            continue;
        const QVariantMap manifest =
            entry.value(QStringLiteral("manifest")).toMap();
        bool universe = false;
        for (const QVariant &resource : manifest.value(
                 QStringLiteral("resources")).toList()) {
            if (resource.toString() == QLatin1String("universe")) {
                universe = true;
                break;
            }
            if (resource.toMap().value(QStringLiteral("name")).toString()
                == QLatin1String("universe")) {
                universe = true;
                break;
            }
        }
        if (!universe)
            continue;

        out.append(QVariantMap{
            {QStringLiteral("extensionId"),
             entry.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"),
             manifest.value(QStringLiteral("name")).toString()},
            {QStringLiteral("banner"),
             manifest.value(QStringLiteral("background")).toString()},
            {QStringLiteral("logo"),
             manifest.value(QStringLiteral("logo")).toString()}
        });
    }
    return out;
}

QVariantMap DeveloperWebUiBridge::homeSnapshot() const
{
    const QVariantList manga =
        mapItems(page(QStringLiteral("manga"), QStringLiteral("popular"), 0, 5));
    const QVariantList comics =
        mapItems(page(QStringLiteral("comics"), QStringLiteral("popular"), 0, 5));
    const QVariantList biblio =
        mapItems(page(QStringLiteral("biblio"), QStringLiteral("popular"), 0, 10));

    QVariantList theatre;
    theatre.append(theatreRows(QStringLiteral("movies"), 3));
    theatre.append(theatreRows(QStringLiteral("shows"), 3));
    theatre.append(theatreAnimeRows(3));

    return QVariantMap{
        {QStringLiteral("continue"), recent(QString(), 12)},
        {QStringLiteral("tankoban"),
         QVariantMap{{QStringLiteral("manga"), manga},
                     {QStringLiteral("comics"), comics}}},
        {QStringLiteral("theatre"),
         QVariantMap{{QStringLiteral("items"), theatre}}},
        {QStringLiteral("biblio"),
         QVariantMap{{QStringLiteral("chart"), biblio}}}
    };
}
QVariantMap DeveloperWebUiBridge::tankobanSnapshot(
    const QString &tab) const
{
    const QVariantList mangaPopular =
        mapItems(page(QStringLiteral("manga"), QStringLiteral("popular"), 0, 24));
    const QVariantList comicsPopular =
        mapItems(page(QStringLiteral("comics"), QStringLiteral("popular"), 0, 24));

    QVariantMap tabs;
    tabs.insert(QStringLiteral("discover"),
        QVariantMap{{QStringLiteral("sections"), QVariantList{
            section(QStringLiteral("Popular Manga"), mangaPopular),
            section(QStringLiteral("Popular Comics"), comicsPopular)
        }}});

    const QVariantList mangaSections{
        section(QStringLiteral("Trending"),
                mapItems(page(QStringLiteral("manga"),
                              QStringLiteral("trending"), 0, 24))),
        section(QStringLiteral("Top Rated"),
                mapItems(page(QStringLiteral("manga"),
                              QStringLiteral("top-rated"), 0, 24))),
        section(QStringLiteral("New Releases"),
                mapItems(page(QStringLiteral("manga"),
                              QStringLiteral("new-releases"), 0, 24))),
        section(QStringLiteral("Popular"), mangaPopular)
    };
    tabs.insert(QStringLiteral("manga"),
                QVariantMap{{QStringLiteral("sections"), mangaSections}});

    const QVariantList comicsSections{
        section(QStringLiteral("Recently Available"),
                mapItems(page(QStringLiteral("comics"),
                              QStringLiteral("recently-available"), 0, 24))),
        section(QStringLiteral("Complete Runs"),
                mapItems(page(QStringLiteral("comics"),
                              QStringLiteral("complete-runs"), 0, 24))),
        section(QStringLiteral("Most Stocked"),
                mapItems(page(QStringLiteral("comics"),
                              QStringLiteral("most-stocked"), 0, 24))),
        section(QStringLiteral("Popular"), comicsPopular)
    };
    tabs.insert(QStringLiteral("comics"),
                QVariantMap{{QStringLiteral("sections"), comicsSections}});
    tabs.insert(QStringLiteral("library"),
                QVariantMap{{QStringLiteral("items"),
                             normalizeRows(collection(QStringLiteral("tankoban")),
                                           QStringLiteral("Tankoban"))}});

    QVariantList featured = mangaPopular.mid(0, 2);
    featured.append(comicsPopular.mid(0, 2));

    return QVariantMap{
        {QStringLiteral("activeTab"),
         isKnownTab(QStringLiteral("Tankoban"), tab)
             ? tab : QStringLiteral("discover")},
        {QStringLiteral("featured"), featured},
        {QStringLiteral("nextUp"), QVariantList{}},
        {QStringLiteral("continue"),
         mergeRecent({recent(QStringLiteral("manga"), 12),
                      recent(QStringLiteral("tankoban"), 12),
                      recent(QStringLiteral("comic"), 12)}, 12)},
        {QStringLiteral("tabs"), tabs}
    };
}
QVariantMap DeveloperWebUiBridge::biblioSnapshot(
    const QString &tab) const
{
    const QVariantList popular =
        mapItems(page(QStringLiteral("biblio"), QStringLiteral("popular"), 0, 24));

    QVariantMap tabs;
    tabs.insert(QStringLiteral("discover"),
                QVariantMap{{QStringLiteral("items"), popular}});
    tabs.insert(QStringLiteral("explore"),
                QVariantMap{{QStringLiteral("sections"),
                             biblioExploreSections(18)}});
    tabs.insert(QStringLiteral("library"),
                QVariantMap{{QStringLiteral("items"),
                             normalizeRows(collection(QStringLiteral("biblio")),
                                           QStringLiteral("Biblio"),
                                           QStringLiteral("book"))}});

    return QVariantMap{
        {QStringLiteral("activeTab"),
         isKnownTab(QStringLiteral("Biblio"), tab)
             ? tab : QStringLiteral("discover")},
        {QStringLiteral("featured"), popular.mid(0, 4)},
        {QStringLiteral("nextUp"), QVariantList{}},
        {QStringLiteral("continue"), recent(QStringLiteral("book"), 12)},
        {QStringLiteral("tabs"), tabs}
    };
}

QVariantMap DeveloperWebUiBridge::theatreSnapshot(
    const QString &tab) const
{
    const QVariantList movies = theatreRows(QStringLiteral("movies"), 24);
    const QVariantList shows = theatreRows(QStringLiteral("shows"), 24);
    const QVariantList anime = theatreAnimeRows(24);

    QVariantList featured = movies.mid(0, 2);
    featured.append(shows.mid(0, 2));
    featured.append(anime.mid(0, 2));

    QVariantMap tabs;
    tabs.insert(QStringLiteral("discover"),
        QVariantMap{{QStringLiteral("sections"), QVariantList{
            section(QStringLiteral("Top Movies"), movies.mid(0, 12)),
            section(QStringLiteral("Top Shows"), shows.mid(0, 12)),
            section(QStringLiteral("Top Anime"), anime.mid(0, 12))
        }}});
    tabs.insert(QStringLiteral("movies"),
        QVariantMap{{QStringLiteral("sections"), QVariantList{
            section(QStringLiteral("Top Rated"), movies)
        }}});
    tabs.insert(QStringLiteral("shows"),
        QVariantMap{{QStringLiteral("sections"), QVariantList{
            section(QStringLiteral("Top Rated"), shows)
        }}});
    tabs.insert(QStringLiteral("anime"),
        QVariantMap{{QStringLiteral("sections"), QVariantList{
            section(QStringLiteral("Top Rated"), anime)
        }}});
    tabs.insert(QStringLiteral("library"),
        QVariantMap{{QStringLiteral("items"),
                     normalizeRows(collection(QStringLiteral("theatre")),
                                   QStringLiteral("Theatre"))}});

    return QVariantMap{
        {QStringLiteral("activeTab"),
         isKnownTab(QStringLiteral("Theatre"), tab)
             ? tab : QStringLiteral("discover")},
        {QStringLiteral("featured"), featured},
        {QStringLiteral("nextUp"), QVariantList{}},
        {QStringLiteral("continue"), recent(QStringLiteral("video"), 12)},
        {QStringLiteral("tabs"), tabs}
    };
}
QString DeveloperWebUiBridge::canonicalSurface(const QString &surface)
{
    if (surface.compare(QStringLiteral("Tankoban"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Tankoban");
    if (surface.compare(QStringLiteral("Biblio"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Biblio");
    if (surface.compare(QStringLiteral("Theatre"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Theatre");
    return QStringLiteral("Home");
}

bool DeveloperWebUiBridge::isKnownWorld(const QString &world)
{
    return world == QLatin1String("Tankoban")
        || world == QLatin1String("Biblio")
        || world == QLatin1String("Theatre");
}

bool DeveloperWebUiBridge::isKnownTab(
    const QString &world, const QString &tab)
{
    static const QHash<QString, QSet<QString>> tabs{
        {QStringLiteral("Tankoban"),
         {QStringLiteral("discover"), QStringLiteral("manga"),
          QStringLiteral("comics"), QStringLiteral("library")}},
        {QStringLiteral("Biblio"),
         {QStringLiteral("discover"), QStringLiteral("explore"),
          QStringLiteral("library")}},
        {QStringLiteral("Theatre"),
         {QStringLiteral("discover"), QStringLiteral("movies"),
          QStringLiteral("shows"), QStringLiteral("anime"),
          QStringLiteral("library")}}
    };
    return tabs.value(world).contains(tab);
}

QVariantMap DeveloperWebUiBridge::snapshot(
    const QString &surface, const QString &tab) const
{
    const QString canonical = canonicalSurface(surface);
    QVariantMap out{
        {QStringLiteral("surface"), canonical},
        {QStringLiteral("revision"), m_revision},
        {QStringLiteral("wallpaper"), m_wallpaper}
    };

    if (canonical == QLatin1String("Home")) {
        out.insert(QStringLiteral("universes"), universeRows());
        out.insert(QStringLiteral("home"), homeSnapshot());
        return out;
    }

    QVariantMap worlds;
    if (canonical == QLatin1String("Tankoban"))
        worlds.insert(canonical, tankobanSnapshot(tab));
    else if (canonical == QLatin1String("Biblio"))
        worlds.insert(canonical, biblioSnapshot(tab));
    else
        worlds.insert(canonical, theatreSnapshot(tab));
    out.insert(QStringLiteral("worlds"), worlds);
    return out;
}

void DeveloperWebUiBridge::requestSnapshot(
    const QString &surface, const QString &tab)
{
    const QString canonical = canonicalSurface(surface);
    if (canonical != QLatin1String("Home")
        && !tab.isEmpty() && !isKnownTab(canonical, tab)) {
        emit invalidAction(QStringLiteral("Unknown WebUI tab '%1' for %2.")
                               .arg(tab, canonical));
        return;
    }
    m_activeSurface = canonical;
    m_activeTab = tab;
    emit snapshotReady(snapshot(canonical, tab));
}
void DeveloperWebUiBridge::clientReady()
{
    m_clientReady = true;
    requestSnapshot(QStringLiteral("Home"), QString());
}

QVariantMap DeveloperWebUiBridge::actionItem(const QVariantMap &action)
{
    const QVariantMap item = action.value(QStringLiteral("item")).toMap();
    if (!item.isEmpty())
        return item;

    QVariantMap fallback;
    for (const QString &key : {
             QStringLiteral("id"), QStringLiteral("title"),
             QStringLiteral("kind")}) {
        if (action.contains(key))
            fallback.insert(key, action.value(key));
    }
    return fallback;
}

void DeveloperWebUiBridge::emitNativeSearch(
    const QString &surface, const QString &query)
{
    QVariantList results;
    const QString trimmed = query.trimmed();
    if (trimmed.size() < 2) {
        emit patchReady(QVariantMap{
            {QStringLiteral("search"),
             QVariantMap{{QStringLiteral("loading"), false},
                         {QStringLiteral("results"), results}}}
        });
        return;
    }

    if (surface == QLatin1String("Tankoban")) {
        if (m_malCatalog && m_malCatalog->ready())
            results.append(normalizeRows(
                m_malCatalog->search(trimmed, 18, QStringLiteral("manga")),
                QStringLiteral("Tankoban"), QStringLiteral("manga")));
        if (m_comicsCatalog && m_comicsCatalog->ready())
            results.append(normalizeRows(
                m_comicsCatalog->search(trimmed, 18),
                QStringLiteral("Tankoban"), QStringLiteral("comics")));
    } else if (surface == QLatin1String("Theatre")) {
        if (m_imdbCatalog && m_imdbCatalog->ready())
            results.append(normalizeRows(
                m_imdbCatalog->search(trimmed, 24),
                QStringLiteral("Theatre")));
        if (m_malCatalog && m_malCatalog->ready())
            results.append(normalizeRows(
                m_malCatalog->search(trimmed, 12, QStringLiteral("anime")),
                QStringLiteral("Theatre"), QStringLiteral("anime")));
    } else if (surface == QLatin1String("Biblio")) {
        const QVariantList sections = biblioExploreSections(40);
        const QString needle = trimmed.toCaseFolded();
        for (const QVariant &sectionValue : sections) {
            const QVariantList items =
                sectionValue.toMap().value(QStringLiteral("items")).toList();
            for (const QVariant &itemValue : items) {
                const QVariantMap item = itemValue.toMap();
                if (item.value(QStringLiteral("title")).toString()
                        .toCaseFolded().contains(needle)) {
                    results.append(item);
                    if (results.size() >= 30)
                        break;
                }
            }
            if (results.size() >= 30)
                break;
        }
    }

    emit patchReady(QVariantMap{
        {QStringLiteral("search"),
         QVariantMap{{QStringLiteral("loading"), false},
                     {QStringLiteral("results"), results}}}
    });
}
void DeveloperWebUiBridge::postAction(const QVariantMap &action)
{
    const QString type = action.value(QStringLiteral("type")).toString().trimmed();
    if (type.isEmpty()) {
        emit invalidAction(QStringLiteral("WebUI action is missing a type."));
        return;
    }

    if (type == QLatin1String("webui-ready")) {
        clientReady();
        return;
    }
    if (type == QLatin1String("request-snapshot")) {
        requestSnapshot(action.value(QStringLiteral("surface")).toString(),
                        action.value(QStringLiteral("tab")).toString());
        return;
    }
    if (type == QLatin1String("home")) {
        m_activeSurface = QStringLiteral("Home");
        m_activeTab.clear();
        emit homeRequested();
        return;
    }

    const QString world =
        canonicalSurface(action.value(QStringLiteral("world")).toString());
    if (type == QLatin1String("open-world")) {
        if (!isKnownWorld(world)) {
            emit invalidAction(QStringLiteral("WebUI requested an unknown world."));
            return;
        }
        m_activeSurface = world;
        emit openWorldRequested(world);
        return;
    }
    if (type == QLatin1String("world-tab")) {
        const QString tab = action.value(QStringLiteral("tab")).toString();
        if (!isKnownWorld(world) || !isKnownTab(world, tab)) {
            emit invalidAction(QStringLiteral("WebUI requested an invalid world tab."));
            return;
        }
        m_activeSurface = world;
        m_activeTab = tab;
        emit worldTabRequested(world, tab);
        requestSnapshot(world, tab);
        return;
    }

    const QVariantMap item = actionItem(action);
    if (type == QLatin1String("open-item")) {
        if (!isKnownWorld(world) || item.isEmpty()) {
            emit invalidAction(QStringLiteral("WebUI open-item is missing valid identity."));
            return;
        }
        emit openItemRequested(
            world, item,
            action.value(QStringLiteral("intent")).toString());
        return;
    }
    if (type == QLatin1String("resume")) {
        emit resumeRequested(world, item);
        return;
    }
    if (type == QLatin1String("continue-details")) {
        emit continueDetailsRequested(world, item);
        return;
    }
    if (type == QLatin1String("next-up")) {
        emit nextUpRequested(world, item);
        return;
    }
    if (type == QLatin1String("continue-see-all")) {
        emit continueSeeAllRequested(world);
        return;
    }
    if (type == QLatin1String("see-all")) {
        emit seeAllRequested(
            world,
            action.value(QStringLiteral("tab")).toString(),
            action.value(QStringLiteral("pin")).toMap());
        return;
    }
    if (type == QLatin1String("open-universe")) {
        emit openUniverseRequested(
            action.value(QStringLiteral("extensionId")).toString(),
            action.value(QStringLiteral("name")).toString(),
            item);
        return;
    }
    if (type == QLatin1String("open-universe-hall")) {
        emit openUniverseHallRequested();
        return;
    }
    if (type == QLatin1String("open-vault")) {
        emit openVaultRequested();
        return;
    }
    if (type == QLatin1String("open-genre")) {
        emit openGenreRequested(
            world,
            action.value(QStringLiteral("genre")).toString());
        return;
    }
    if (type == QLatin1String("search-open")) {
        emit searchOpened();
        return;
    }
    if (type == QLatin1String("search-close")) {
        emit searchClosed();
        return;
    }
    if (type == QLatin1String("search-query")) {
        const QString query = action.value(QStringLiteral("query")).toString();
        emit searchRequested(canonicalSurface(
                                 action.value(QStringLiteral("surface")).toString()),
                             query);
        emitNativeSearch(canonicalSurface(
                             action.value(QStringLiteral("surface")).toString()),
                         query);
        return;
    }
    if (type == QLatin1String("trackers")) {
        emit trackersRequested();
        return;
    }
    if (type == QLatin1String("wallpaper")) {
        emit wallpaperRequested();
        return;
    }
    if (type == QLatin1String("account")) {
        emit accountRequested();
        return;
    }
    if (type == QLatin1String("window-minimize")) {
        emit windowMinimizeRequested();
        return;
    }
    if (type == QLatin1String("window-toggle-fullscreen")) {
        emit windowToggleFullscreenRequested();
        return;
    }
    if (type == QLatin1String("window-close")) {
        emit windowCloseRequested();
        return;
    }

    emit invalidAction(
        QStringLiteral("Unsupported WebUI action '%1'.").arg(type));
}

void DeveloperWebUiBridge::bump()
{
    ++m_revision;
    emit revisionChanged();
    if (m_clientReady)
        emit patchReady(snapshot(m_activeSurface, m_activeTab));
}
