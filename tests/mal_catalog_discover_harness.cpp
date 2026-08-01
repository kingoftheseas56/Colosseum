// MalCatalog::discoverPage / discoverFilters — paged, allowlisted, fully-bound
// offline MANGA discovery contract (Tankoban Discover, Task 4). Builds a temporary
// SQLite fixture with the exact baked discover schema (manga + the axis-aware
// classification tables), opens it through MalCatalog (read-only), and proves:
//   - Popular order (members DESC, deterministic tie-break)
//   - Top Rated Bayesian weighting: a low-vote 9.9 title never outranks the
//     broadly-established titles
//   - New Releases order (start_date newest; future / invalid dates rejected)
//   - genre + demographic filtering, bound (an injection value matches nothing
//     and cannot drop the table)
//   - offset paging with no overlap; the [1,100] limit clamp; exhausted/nextOffset
//   - includeExplicit gating on both rows and filter facets
//   - Trending's explicit fallbackCatalog:"popular" (no comparable snapshots yet)
//
// House convention (mirrors mal_catalog_rows_harness): require() prints
// "FAIL: <msg>" and exits 1 (Release-safe); the single stdout marker
// MAL_CATALOG_DISCOVER_OK + exit 0 is the only green signal.
// Run from native/build-msvc so the deployed sqldrivers/qsqlite.dll resolves.
#include "engine/MalCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QList<int> idsOf(const QVariantList& rows)
{
    QList<int> out;
    for (const QVariant& v : rows)
        out << v.toMap().value(QStringLiteral("mal_id")).toInt();
    return out;
}

// index of mal_id in the row list, or -1
int posOf(const QVariantList& rows, int malId)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).toMap().value(QStringLiteral("mal_id")).toInt() == malId)
            return i;
    return -1;
}

bool containsId(const QVariantList& rows, int malId) { return posOf(rows, malId) >= 0; }

// find a facet by its "value" in a discoverFilters() list; returns count or -1
int facetCount(const QVariantList& facets, const QString& value)
{
    for (const QVariant& v : facets) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("value")).toString() == value)
            return m.value(QStringLiteral("count")).toInt();
    }
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString dbPath = QDir::temp().filePath(QStringLiteral("mal_catalog_discover_fixture.db"));
    QFile::remove(dbPath);

    // ── Build the fixture with a separate writable connection, then release it
    //    fully so MalCatalog can open the same file read-only. ──
    {
        QSqlDatabase w = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("discover_fixture_writer"));
        w.setDatabaseName(dbPath);
        require(w.open(), "fixture db opens for writing");
        QSqlQuery q(w);
        // manga with the new discover columns (explicit / start_date / favorites)
        require(q.exec(QStringLiteral(
            "CREATE TABLE manga (mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT, "
            "type TEXT, score REAL, scored_by INTEGER, members INTEGER, status TEXT, "
            "volumes INTEGER, chapters INTEGER, year INTEGER, cover TEXT, synopsis TEXT, "
            "credits TEXT, tags TEXT, "
            "explicit INTEGER NOT NULL DEFAULT 0, start_date TEXT NOT NULL DEFAULT '', "
            "favorites INTEGER NOT NULL DEFAULT 0)")), "create manga table");
        // legacy flattened tag tables (present in every baked db; discover ignores them)
        require(q.exec(QStringLiteral(
            "CREATE TABLE tag (medium TEXT, tag TEXT, mal_id INTEGER)")), "create tag table");
        require(q.exec(QStringLiteral(
            "CREATE TABLE tag_count (medium TEXT, tag TEXT, total INTEGER, "
            "PRIMARY KEY (medium, tag))")), "create tag_count table");
        // axis-aware classification tables
        require(q.exec(QStringLiteral(
            "CREATE TABLE classification (medium TEXT, axis TEXT, value TEXT, mal_id INTEGER)")),
            "create classification table");
        require(q.exec(QStringLiteral(
            "CREATE TABLE classification_count (medium TEXT, axis TEXT, value TEXT, total INTEGER, "
            "PRIMARY KEY (medium, axis, value))")), "create classification_count table");

        auto insertManga = [&](int id, const QString& title, const QString& type, QVariant score,
                               int scoredBy, int members, const QString& startDate, int favorites,
                               int explicit_, const QString& tagsJson) {
            QSqlQuery ins(w);
            ins.prepare(QStringLiteral(
                "INSERT INTO manga (mal_id,title,title_english,type,score,scored_by,members,status,"
                "volumes,chapters,year,cover,synopsis,credits,tags,explicit,start_date,favorites) "
                "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
            ins.addBindValue(id);
            ins.addBindValue(title);
            ins.addBindValue(title);                 // title_english
            ins.addBindValue(type);
            ins.addBindValue(score);                 // may be a null QVariant
            ins.addBindValue(scoredBy);
            ins.addBindValue(members);
            ins.addBindValue(QStringLiteral("Finished"));
            ins.addBindValue(0);                     // volumes
            ins.addBindValue(0);                     // chapters
            ins.addBindValue(startDate.left(4).toInt());   // year derived from start_date
            ins.addBindValue(QStringLiteral("http://cover/") + QString::number(id));
            ins.addBindValue(QStringLiteral("synopsis"));
            ins.addBindValue(QStringLiteral("[]"));  // credits JSON
            ins.addBindValue(tagsJson);              // tags JSON (flattened classifications)
            ins.addBindValue(explicit_);
            ins.addBindValue(startDate);
            ins.addBindValue(favorites);
            require(ins.exec(), "insert manga row");
        };
        auto insertClass = [&](const QString& axis, const QString& value, int id) {
            QSqlQuery ins(w);
            ins.prepare(QStringLiteral(
                "INSERT INTO classification (medium,axis,value,mal_id) VALUES ('manga',?,?,?)"));
            ins.addBindValue(axis);
            ins.addBindValue(value);
            ins.addBindValue(id);
            require(ins.exec(), "insert classification row");
        };

        //           id  title            type      score          votes   members  start_date     favs    expl  tags
        insertManga(1, "Berserk",        "Manga",  QVariant(9.4), 300000, 600000,  "1989-08-25",  100000, 0, "[\"Action\",\"Fantasy\",\"Seinen\"]");
        insertManga(2, "Solo Leveling",  "Manhwa", QVariant(8.7), 150000, 500000,  "2018-03-04",   50000, 0, "[\"Action\",\"Fantasy\"]");
        insertManga(3, "LowVote Nine9",  "Manga",  QVariant(9.9),    120,   4000,  "2020-01-01",     200, 0, "[\"Action\"]");
        insertManga(4, "Explicit Title", "Manga",  QVariant(8.0),  50000, 200000,  "2015-06-01",    3000, 1, "[\"Ecchi\",\"Hentai\",\"Seinen\"]");
        insertManga(5, "Established",     "Manga",  QVariant(8.9), 200000, 400000,  "2009-01-01",   40000, 0, "[\"Action\",\"Shounen\"]");
        insertManga(6, "Newest",         "Manga",  QVariant(7.5),  10000,  50000,  "2024-05-01",    1000, 0, "[\"Comedy\"]");
        insertManga(7, "Future Book",    "Manga",  QVariant(8.0),   5000,  10000,  "2099-01-01",     500, 0, "[\"Action\"]");
        insertManga(8, "Bad Date",       "Manga",  QVariant(7.0),   3000,   8000,  "2021-00-00",     400, 0, "[\"Drama\"]");

        insertClass("genre", "Action",   1); insertClass("genre", "Fantasy", 1); insertClass("demographic", "Seinen",  1);
        insertClass("genre", "Action",   2); insertClass("genre", "Fantasy", 2);
        insertClass("genre", "Action",   3);
        insertClass("genre", "Ecchi",    4); insertClass("genre", "Hentai",  4); insertClass("demographic", "Seinen",  4);
        insertClass("genre", "Action",   5); insertClass("demographic", "Shounen", 5);
        insertClass("genre", "Comedy",   6);
        insertClass("genre", "Action",   7);
        insertClass("genre", "Drama",    8);

        // 120 filler rows so the [1,100] limit clamp is provable. All low members /
        // votes / past-but-old dates so they never disturb the named-row assertions.
        for (int i = 1; i <= 120; ++i) {
            insertManga(1000 + i, QStringLiteral("Filler ") + QString::number(i), "Manga",
                        QVariant(6.0), 100, 100 + i, "2005-01-01", 0, 0, "[\"Filler\"]");
            insertClass("genre", "Filler", 1000 + i);
        }

        // classification_count mirrors the per-row classification totals (bake fills it
        // over the whole loaded set; the fixture states it directly).
        auto insertCount = [&](const QString& axis, const QString& value, int total) {
            QSqlQuery ins(w);
            ins.prepare(QStringLiteral(
                "INSERT INTO classification_count (medium,axis,value,total) VALUES ('manga',?,?,?)"));
            ins.addBindValue(axis); ins.addBindValue(value); ins.addBindValue(total);
            require(ins.exec(), "insert classification_count row");
        };
        insertCount("genre", "Action", 5);  insertCount("genre", "Fantasy", 2);
        insertCount("genre", "Ecchi", 1);   insertCount("genre", "Hentai", 1);
        insertCount("genre", "Comedy", 1);  insertCount("genre", "Drama", 1);
        insertCount("genre", "Filler", 120);
        insertCount("demographic", "Seinen", 2); insertCount("demographic", "Shounen", 1);

        w.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("discover_fixture_writer"));

    MalCatalog cat(dbPath);
    require(cat.ready(), "MalCatalog opens the discover fixture read-only");

    // ── Popular: members DESC ───────────────────────────────────────────────
    {
        const QVariantMap page = cat.discoverPage("popular", "", "", true, 0, 3);
        const QVariantList items = page.value("items").toList();
        require(items.size() == 3, "popular page fills to the limit");
        require(idsOf(items) == (QList<int>{1, 2, 5}), "popular order is members DESC (Berserk, Solo, Established)");
        require(page.value("nextOffset").toInt() == 3, "popular nextOffset = offset + rows");
        require(page.value("exhausted").toBool() == false, "popular first page is not exhausted");
        require(page.value("freshness").toString() == QStringLiteral("bundled"), "freshness is bundled");
        require(page.value("fallbackCatalog").toString().isEmpty(), "popular has no fallbackCatalog");
        // row shape: availability false + explicit is a real bool + classifications present
        const QVariantMap r0 = items.first().toMap();
        require(r0.value("availability").toBool() == false, "every row carries availability:false");
        require(r0.contains("explicit"), "row carries an explicit flag");
        require(r0.value("classifications").toList().contains(QVariant(QStringLiteral("Action"))),
                "row carries its classifications list");
        require(r0.value("start_date").toString() == QStringLiteral("1989-08-25"), "row carries start_date");
        require(r0.value("favorites").toInt() == 100000, "row carries favorites");
    }

    // ── Top Rated: Bayesian — the low-vote 9.9 never beats established titles ─
    {
        const QVariantMap page = cat.discoverPage("top-rated", "", "", true, 0, 10);
        const QVariantList items = page.value("items").toList();
        require(!items.isEmpty(), "top-rated returns rows");
        require(idsOf(items).first() == 1, "top-rated is topped by Berserk, not the 9.9 low-vote");
        const int lowVote = posOf(items, 3);
        require(lowVote > 0, "the 9.9 low-vote title is present but not first");
        require(posOf(items, 1) < lowVote, "established Berserk outranks the low-vote 9.9");
        require(posOf(items, 5) >= 0 && posOf(items, 5) < lowVote, "established title outranks the low-vote 9.9");
        require(posOf(items, 2) >= 0 && posOf(items, 2) < lowVote, "established Solo outranks the low-vote 9.9");
    }

    // ── New Releases: start_date DESC, future + invalid dates rejected ───────
    {
        const QVariantMap page = cat.discoverPage("new-releases", "", "", true, 0, 100);
        const QVariantList items = page.value("items").toList();
        require(!items.isEmpty(), "new-releases returns rows");
        require(idsOf(items).first() == 6, "new-releases newest first (2024)");
        require(!containsId(items, 7), "future-dated title is rejected (2099)");
        require(!containsId(items, 8), "invalid-dated title is rejected (2021-00-00)");
        // relative recency ordering among the named past dates
        require(posOf(items, 3) < posOf(items, 2), "2020 newer than 2018");
        require(posOf(items, 2) < posOf(items, 5), "2018 newer than 2009");
    }

    // ── genre filter (bound), members DESC within the facet ──────────────────
    {
        const QVariantMap page = cat.discoverPage("popular", "genre", "Action", true, 0, 100);
        const QVariantList items = page.value("items").toList();
        require(idsOf(items) == (QList<int>{1, 2, 5, 7, 3}), "Action genre facet, members DESC");
        require(!containsId(items, 4), "non-Action explicit title absent from Action facet");
        require(!containsId(items, 6), "non-Action title absent from Action facet");
    }

    // ── demographic filter + includeExplicit interaction ─────────────────────
    {
        const QVariantList incl = cat.discoverPage("popular", "demographic", "Seinen", true, 0, 100)
                                     .value("items").toList();
        require(idsOf(incl) == (QList<int>{1, 4}), "Seinen facet includes the explicit title when allowed");
        const QVariantList excl = cat.discoverPage("popular", "demographic", "Seinen", false, 0, 100)
                                     .value("items").toList();
        require(idsOf(excl) == (QList<int>{1}), "Seinen facet drops the explicit title when hidden");
    }

    // ── includeExplicit gating on the unfiltered catalog ─────────────────────
    {
        const QVariantList incl = cat.discoverPage("popular", "", "", true, 0, 100).value("items").toList();
        require(containsId(incl, 4), "explicit title present when includeExplicit=true");
        const QVariantList excl = cat.discoverPage("popular", "", "", false, 0, 100).value("items").toList();
        require(!containsId(excl, 4), "explicit title hidden when includeExplicit=false");
    }

    // ── offset paging: adjacent windows do not overlap ───────────────────────
    {
        const QVariantList p0 = cat.discoverPage("popular", "", "", true, 0, 3).value("items").toList();
        const QVariantList p1 = cat.discoverPage("popular", "", "", true, 3, 3).value("items").toList();
        require(p0.size() == 3 && p1.size() == 3, "both offset pages fill");
        for (int id : idsOf(p0))
            require(!containsId(p1, id), "offset paging produces no overlap");
    }

    // ── limit clamps to 100; a small facet reports exhausted ─────────────────
    {
        const QVariantMap big = cat.discoverPage("popular", "", "", true, 0, 99999);
        require(big.value("items").toList().size() == 100, "limit clamps to 100");
        require(big.value("nextOffset").toInt() == 100, "clamped nextOffset = 0 + 100");
        require(big.value("exhausted").toBool() == false, "a full 100-row page is not exhausted");

        const QVariantMap tiny = cat.discoverPage("popular", "demographic", "Shounen", true, 0, 10);
        require(tiny.value("items").toList().size() == 1, "Shounen facet has one title");
        require(tiny.value("exhausted").toBool() == true, "a facet smaller than the limit is exhausted");
        require(tiny.value("nextOffset").toInt() == 1, "tiny nextOffset = 0 + 1");
    }

    // ── Trending: no snapshots yet → Popular order + fallbackCatalog:"popular" ─
    {
        const QVariantMap page = cat.discoverPage("trending", "", "", true, 0, 5);
        const QVariantList items = page.value("items").toList();
        require(idsOf(items).first() == 1, "trending falls back to popular order");
        require(page.value("fallbackCatalog").toString() == QStringLiteral("popular"),
                "trending sets fallbackCatalog:popular");
        require(page.value("freshness").toString() == QStringLiteral("bundled"), "trending freshness bundled");
    }

    // ── allowlist: unknown catalogId / axis → empty items, never a crash ─────
    {
        require(cat.discoverPage("bogus", "", "", true, 0, 10).value("items").toList().isEmpty(),
                "unknown catalogId yields no items");
        require(cat.discoverPage("popular", "bogus", "x", true, 0, 10).value("items").toList().isEmpty(),
                "unknown filter axis yields no items");
    }

    // ── binding: an injection filter value matches nothing and cannot drop the table ─
    {
        const QVariantList evil = cat.discoverPage(
            "popular", "genre", "Action'); DROP TABLE manga;--", true, 0, 10).value("items").toList();
        require(evil.isEmpty(), "injection filter value matches nothing");
        require(cat.discoverPage("popular", "genre", "Action", true, 0, 100).value("items").toList().size() == 5,
                "the table survived the injection attempt (values are bound)");
    }

    // ── discoverFilters: facets per axis, includeExplicit prunes explicit-only facets ─
    {
        const QVariantList gAll = cat.discoverFilters("genre", true);
        require(facetCount(gAll, "Action") == 5, "genre facet Action counts all 5");
        require(facetCount(gAll, "Hentai") == 1, "genre facet Hentai present when explicit allowed");

        const QVariantList gSfw = cat.discoverFilters("genre", false);
        require(facetCount(gSfw, "Action") == 5, "Action facet unchanged (none explicit)");
        require(facetCount(gSfw, "Hentai") == -1, "explicit-only Hentai facet pruned when hidden");

        const QVariantList dAll = cat.discoverFilters("demographic", true);
        require(facetCount(dAll, "Seinen") == 2, "Seinen counts both titles when explicit allowed");
        const QVariantList dSfw = cat.discoverFilters("demographic", false);
        require(facetCount(dSfw, "Seinen") == 1, "Seinen count drops the explicit title when hidden");

        require(cat.discoverFilters("bogus", true).isEmpty(), "unknown filter axis yields no facets");
        require(cat.discoverFilters("", true).isEmpty(), "empty filter axis yields no facets");
    }

    // ── not-ready seam: a missing db never crashes, returns empties ───────────
    {
        MalCatalog missing(QDir::temp().filePath(QStringLiteral("mal_catalog_discover_nope.db")));
        require(!missing.ready(), "missing db is not ready");
        require(missing.discoverPage("popular", "", "", true, 0, 10).value("items").toList().isEmpty(),
                "not-ready discoverPage returns empty items");
        require(missing.discoverFilters("genre", true).isEmpty(),
                "not-ready discoverFilters returns empty");
    }

    QFile::remove(dbPath);
    std::cout << "MAL_CATALOG_DISCOVER_OK\n";
    return 0;
}
