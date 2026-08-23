// Arc 21 focused ComicsCatalog harness. Reference-tree only.
#include "engine/ComicsCatalog.h"
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariantList>
#include <cstdio>

static int fail(const char* msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static bool hasKey(const QVariantList& rows, const QString& key) {
    for (const QVariant& value : rows)
        if (value.toMap().value(QStringLiteral("key")).toString().compare(key, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

static bool hasLocg(const QVariantList& rows, const QString& id) {
    for (const QVariant& value : rows)
        if (value.toMap().value(QStringLiteral("locgId")).toString() == id)
            return true;
    return false;
}

static bool exec(QSqlQuery& q, const QString& sql) {
    const QStringList statements = sql.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& statement : statements) {
        if (q.exec(statement.trimmed())) continue;
        std::fprintf(stderr, "SQL failed: %s\n", qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return fail("temporary directory");
    const QString path = dir.filePath(QStringLiteral("arc21.db"));

    const QString conn = QStringLiteral("arc21_fixture");
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(path);
    if (!db.open()) return fail("fixture sqlite open");
    QSqlQuery q(db);
    if (!exec(q, QStringLiteral(
        "create table series(gcd_id integer primary key,title text,year int,year_ended int,"
        "issue_count int,publisher text,cover text,synopsis text);"
        "create table series_stats(series_id integer primary key,downloads int,kinds text,latest_post text);"
        "create table download(post_id integer primary key,series_id int,title text,link text,date text,"
        "kind text,method text,fan_made int,year_start int);"
        "create table download_mirror(post_id int,url text,host text,label text);"))) return 1;
    if (!exec(q, QStringLiteral(
        "create table curated_series(locg_id text primary key,rank int,title text,norm_title text,year int,"
        "slug text,publisher text,cover text,synopsis text);"
        "create table curated_genre(locg_id text,genre text);"
        "create table curated_edition(id integer primary key,locg_id text,title text,display_title text,"
        "format text,collects text,isbn text,pages int,published text,chid text,cover text,available int,"
        "getcomics_post text,creators text,description text);"
        "create table curated_source(id integer primary key,curated_edition_id int,post_id int,post_url text,"
        "source_title text,source_date text,source_cover text,source_kind text,source_format text,"
        "attachment_method text,confidence_class text,fan_made int,available int,parsed_series_text text,"
        "formats_json text,qualifiers_json text,parser_version int);"))) return 1;
    if (!exec(q, QStringLiteral(
        "create table curated_series_coverage(locg_id text primary key,gcd_series_id int,"
        "canonical_issue_count int,covered_issue_count int,coverage_ratio real,availability_state text,"
        "source_count int,official_source_count int,community_source_count int,latest_source_date text,"
        "computed_at text,parser_version int);"))) return 1;

    if (!exec(q, QStringLiteral(
        "insert into series values(77,'Saga',2012,2018,54,'Image','saga.jpg','');"
        "insert into series_stats values(77,2,'collection','2026-08-20');"
        "insert into curated_series values('saga',1,'Saga','saga',2012,'saga','Image','saga.jpg','Space opera');"
        "insert into curated_genre values('saga','Science Fiction');"))) return 1;
    if (!exec(q, QStringLiteral(
        "insert into curated_edition values(1,'saga','Saga Deluxe Vol. 1','Saga Deluxe Vol. 1','Deluxe',"
        "'Saga #1-18','9781',500,'2014','900','ed.jpg',1,'https://getcomics.org/saga/','A Writer','');"
        "insert into curated_source values(10,1,100,'https://getcomics.org/saga/','Saga Deluxe','2026-08-20',"
        "'gc.jpg','collection','Deluxe','edition_series_match','strong',0,1,'Saga','[\"Deluxe\"]','[]',1);"
        "insert into curated_source values(11,1,101,'https://getcomics.org/saga-fan/','Saga fan bind','2026-08-21',"
        "'fan.jpg','bundle','Digital','fuzzy_gated','strong',1,1,'Saga','[]','[]',1);"
        "insert into download_mirror values(100,'https://mirror/saga.cbz','DataNodes','Mirror');"
        "insert into curated_series_coverage values('saga',77,54,54,1.0,'complete',2,1,1,"
        "'2026-08-20','2026-08-23',1);"))) return 1;

    if (!exec(q, QStringLiteral(
        "insert into curated_series values('paper',2,'Paper Girls','paper girls',2015,'paper-girls','Image',"
        "'paper.jpg','');"
        "insert into curated_genre values('paper','Science Fiction');"
        "insert into curated_edition values(2,'paper','Paper Girls Vol. 1','Paper Girls Vol. 1','Trade Paperback',"
        "'Paper Girls #1-5','9782',144,'2016','901','paper-ed.jpg',1,'https://getcomics.org/paper/','B Writer','');"
        "insert into curated_source values(12,2,102,'https://getcomics.org/paper/','Paper Girls #1-27','2026-08-19',"
        "'paper-gc.jpg','collection','Trade Paperback','exact_run_year','strong',0,1,'Paper Girls','[]','[]',1);"
        "insert into curated_series_coverage values('paper',88,30,27,0.9,'near_complete',1,1,0,"
        "'2026-08-19','2026-08-23',1);"))) return 1;

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(conn);

    ComicsCatalog catalog(path);
    if (!catalog.ready() || !catalog.curatedReady()) return fail("catalogue ready");
    const QVariantMap saga = catalog.curatedSeries(QStringLiteral("saga"));
    if (saga.value(QStringLiteral("coverage")).toMap()
            .value(QStringLiteral("availabilityState")).toString() != QStringLiteral("complete"))
        return fail("series coverage");
    const QVariantList editions = saga.value(QStringLiteral("editions")).toList();
    if (editions.size() != 1) return fail("edition count");
    const QVariantList sources = editions.first().toMap().value(QStringLiteral("sources")).toList();
    if (sources.size() != 2) return fail("multiple sources retained");
    if (sources.first().toMap().value(QStringLiteral("fanMade")).toBool())
        return fail("official source ordered first");
    if (sources.first().toMap().value(QStringLiteral("mirrors")).toList().size() != 1)
        return fail("source mirrors");

    if (!hasKey(catalog.discoverFilters(QStringLiteral("format"), false), QStringLiteral("Deluxe")))
        return fail("format facet");
    const QVariantList availability = catalog.discoverFilters(QStringLiteral("availability"), false);
    if (!hasKey(availability, QStringLiteral("complete"))
        || !hasKey(availability, QStringLiteral("near_complete"))
        || !hasKey(availability, QStringLiteral("available")))
        return fail("availability facets");

    const auto complete = catalog.discoverPage(QStringLiteral("complete-runs"), QString(), QString(), false, 0, 24);
    if (!hasLocg(complete.value(QStringLiteral("items")).toList(), QStringLiteral("saga")))
        return fail("complete-runs catalogue");
    const auto near = catalog.discoverPage(QStringLiteral("near-complete"), QString(), QString(), false, 0, 24);
    if (!hasLocg(near.value(QStringLiteral("items")).toList(), QStringLiteral("paper")))
        return fail("near-complete catalogue");
    const auto community = catalog.discoverPage(QStringLiteral("community-collections"), QString(), QString(), false, 0, 24);
    if (!hasLocg(community.value(QStringLiteral("items")).toList(), QStringLiteral("saga")))
        return fail("community catalogue");
    const auto format = catalog.discoverPage(QStringLiteral("popular"), QStringLiteral("format"),
                                             QStringLiteral("deluxe"), false, 0, 24);
    if (!hasLocg(format.value(QStringLiteral("items")).toList(), QStringLiteral("saga")))
        return fail("format-filtered page");
    const auto availablePage = catalog.discoverPage(QStringLiteral("popular"), QStringLiteral("availability"),
                                                    QStringLiteral("near_complete"), false, 0, 24);
    if (!hasLocg(availablePage.value(QStringLiteral("items")).toList(), QStringLiteral("paper")))
        return fail("availability-filtered page");

    // --- Gate C: stale-db degradation. A pre-Arc-21 catalogue carries none of the
    // five new tables (curated_source, curated_source_claim, curated_source_issue,
    // canonical_issue, curated_series_coverage) — exactly today's live comics_catalog.db
    // before upstream enrichment runs. Every Arc 21 API must degrade to empty/false
    // rather than erroring, and the pre-existing legacy paths must still work.
    QTemporaryDir staleDir;
    if (!staleDir.isValid()) return fail("stale temporary directory");
    const QString stalePath = staleDir.filePath(QStringLiteral("stale.db"));
    const QString staleConn = QStringLiteral("arc21_stale_fixture");
    {
        QSqlDatabase staleDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), staleConn);
        staleDb.setDatabaseName(stalePath);
        if (!staleDb.open()) return fail("stale fixture sqlite open");
        QSqlQuery sq(staleDb);
        if (!exec(sq, QStringLiteral(
            "create table series(gcd_id integer primary key,title text,year int,year_ended int,"
            "issue_count int,publisher text,cover text,synopsis text);"
            "create table series_stats(series_id integer primary key,downloads int,kinds text,latest_post text);"
            "create table download(post_id integer primary key,series_id int,title text,link text,date text,"
            "kind text,method text,fan_made int,year_start int);"
            "create table download_mirror(post_id int,url text,host text,label text);"
            "create table curated_series(locg_id text primary key,rank int,title text,norm_title text,year int,"
            "slug text,publisher text,cover text,synopsis text);"
            "create table curated_genre(locg_id text,genre text);"
            "create table curated_edition(id integer primary key,locg_id text,title text,display_title text,"
            "format text,collects text,isbn text,pages int,published text,chid text,cover text,available int,"
            "getcomics_post text,creators text,description text);"))) return 1;
        if (!exec(sq, QStringLiteral(
            "insert into curated_series values('old',3,'Old Guard','old guard',2017,'old-guard','Image',"
            "'old.jpg','');"
            "insert into curated_edition values(3,'old','Old Guard Vol. 1','Old Guard Vol. 1','Trade Paperback',"
            "'Old Guard #1-5','9783',120,'2017','902','old-ed.jpg',1,'https://getcomics.org/old-guard/',"
            "'C Writer','');"))) return 1;
        staleDb.close();
        staleDb = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(staleConn);

    ComicsCatalog stale(stalePath);
    if (!stale.ready() || !stale.curatedReady()) return fail("stale catalogue ready");
    const QVariantMap oldSeries = stale.curatedSeries(QStringLiteral("old"));
    if (!oldSeries.value(QStringLiteral("coverage")).toMap().isEmpty())
        return fail("stale db: curatedCoverage must be empty without curated_series_coverage");
    const QVariantList oldEditions = oldSeries.value(QStringLiteral("editions")).toList();
    if (oldEditions.size() != 1) return fail("stale db: edition still readable");
    if (!oldEditions.first().toMap().value(QStringLiteral("sources")).toList().isEmpty())
        return fail("stale db: sources must be empty without curated_source");
    if (!stale.discoverFilters(QStringLiteral("availability"), false).isEmpty())
        return fail("stale db: availability facets must be empty without curated_series_coverage");
    // format facets ARE derived from curated_edition alone (no new table dependency) —
    // Old Guard's Trade Paperback format should still surface even on a stale db.
    if (!hasKey(stale.discoverFilters(QStringLiteral("format"), false), QStringLiteral("Trade Paperback")))
        return fail("stale db: format facet still derives from curated_edition alone");
    const auto staleComplete = stale.discoverPage(QStringLiteral("complete-runs"), QString(), QString(), false, 0, 24);
    if (!staleComplete.value(QStringLiteral("items")).toList().isEmpty())
        return fail("stale db: complete-runs catalogue must degrade to empty without coverage table");
    const auto staleNear = stale.discoverPage(QStringLiteral("near-complete"), QString(), QString(), false, 0, 24);
    if (!staleNear.value(QStringLiteral("items")).toList().isEmpty())
        return fail("stale db: near-complete catalogue must degrade to empty without coverage table");
    const auto staleCommunity = stale.discoverPage(QStringLiteral("community-collections"), QString(), QString(), false, 0, 24);
    if (!staleCommunity.value(QStringLiteral("items")).toList().isEmpty())
        return fail("stale db: community-collections catalogue must degrade to empty without coverage table");
    const auto staleAvailFacet = stale.discoverPage(QStringLiteral("popular"), QStringLiteral("availability"),
                                                     QStringLiteral("complete"), false, 0, 24);
    if (!staleAvailFacet.value(QStringLiteral("items")).toList().isEmpty())
        return fail("stale db: availability-filtered popular page must degrade to empty without coverage table");
    // legacy curatedHasDownloadable still works via the getcomics_post fallback path
    // (no curated_source table -> falls back to the pre-Arc-21 query).
    if (!stale.curatedHasDownloadable(QStringLiteral("old")))
        return fail("stale db: curatedHasDownloadable must still use the legacy fallback path");
    // popular/all/new-releases/most-stocked (the pre-existing catalogs) must still return
    // the row — new-table absence must never break the OLD discover surface.
    const auto stalePopular = stale.discoverPage(QStringLiteral("popular"), QString(), QString(), false, 0, 24);
    if (!hasLocg(stalePopular.value(QStringLiteral("items")).toList(), QStringLiteral("old")))
        return fail("stale db: popular catalogue must still return pre-existing rows");

    std::puts("Arc 21 ComicsCatalog harness: OK");
    return 0;
}
