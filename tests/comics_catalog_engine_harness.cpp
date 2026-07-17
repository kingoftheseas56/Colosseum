// ComicsCatalog engine harness — fixture SQLite db in a temp dir, exit code = verdict.
// Run from native/build-msvc so the deployed sqldrivers/qsqlite.dll is found.
#include "engine/ComicsCatalog.h"
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cstdio>

static int fail(const char* m) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; }

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString dbPath = dir.filePath("cat.db");
    {
        auto d = QSqlDatabase::addDatabase("QSQLITE", "fixture");
        d.setDatabaseName(dbPath);
        if (!d.open()) return fail("fixture open");
        QSqlQuery q(d);
        q.exec("create table series(gcd_id integer primary key, title text, year int, year_ended int, issue_count int, publisher text, cover text, synopsis text)");
        q.exec("create table download(post_id integer primary key, series_id int, title text, link text, date text, kind text, method text, fan_made int, year_start int)");
        q.exec("create table series_stats(series_id integer primary key, downloads int, kinds text, latest_post text)");
        q.exec("insert into series values (1,'Saga',2012,0,72,'Image','https://c/s.jpg','Two soldiers')");
        q.exec("insert into series values (2,'Batman',1940,2011,713,'DC','','')");
        q.exec("insert into series values (3,'Batman',2016,0,150,'DC','','')");
        q.exec("insert into download values (10,1,'Saga #55 (2022)','https://g/10/','2022-01-09','single','run_span',0,2022)");
        q.exec("insert into download values (11,1,'Saga Vol. 4 (TPB)','https://g/11/','2014-05-01','collection','exact_run_year',0,2014)");
        q.exec("insert into series_stats values (1,2,'collection,single','2022-01-09')");
        q.exec("insert into series_stats values (2,5,'single','2011-01-01')");
        q.exec("insert into series_stats values (3,1,'bundle','2020-01-01')");
        d.close();
    }
    QSqlDatabase::removeDatabase("fixture");

    ComicsCatalog cat(dbPath);
    if (!cat.ready()) return fail("ready() on a good db");
    const QVariantMap s = cat.series(1);
    if (s.value("title").toString() != "Saga") return fail("series title");
    if (s.value("downloads").toInt() != 2) return fail("series_stats join");
    if (s.value("cover").toString() != "https://c/s.jpg") return fail("cover");
    if (!cat.series(999).isEmpty()) return fail("missing series must be {}");
    const QVariantList dls = cat.downloadsFor(1);
    if (dls.size() != 2) return fail("downloads count");
    if (dls[0].toMap().value("postId").toInt() != 10) return fail("downloads date DESC (2022 first)");
    if (dls[1].toMap().value("kind").toString() != "collection") return fail("kind field");
    const QVariantList hits = cat.search("bat", 10);
    if (hits.size() != 2) return fail("search LIKE count");
    if (hits[0].toMap().value("gcdId").toInt() != 2) return fail("same class -> downloads DESC (1940 run, 5 dls, first)");
    if (cat.search("saga", 10).size() != 1) return fail("search exact");
    if (!cat.search("zz%_zz", 10).isEmpty()) return fail("LIKE metachars must be escaped");
    const QVariantList ex = cat.exactMatches("BATMAN");
    if (ex.size() != 2) return fail("exactMatches case-insensitive both runs");
    if (cat.exactMatches("bat").size() != 0) return fail("exactMatches is exact, not prefix");
    ComicsCatalog missing(dir.filePath("nope.db"));
    if (missing.ready()) return fail("missing db must not be ready");
    if (!missing.search("x", 5).isEmpty() || !missing.series(1).isEmpty() || !missing.downloadsFor(1).isEmpty())
        return fail("not-ready must return empty, never crash");
    std::fprintf(stdout, "COMICS-CATALOG-ENGINE OK\n");
    return 0;
}
