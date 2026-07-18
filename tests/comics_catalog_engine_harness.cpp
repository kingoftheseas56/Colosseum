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
        q.exec("insert into series values (4,'Hulk',2020,0,10,'Marvel','','')");
        q.exec("insert into series values (5,'Hulkverines',2019,2019,6,'Marvel','','')");
        q.exec("insert into series values (6,'The Immortal Hulk',2018,2021,50,'Marvel','','')");
        q.exec("insert into series_stats values (4,3,'single','2020-01-01')");
        q.exec("insert into series_stats values (5,10,'bundle','2019-01-01')");
        q.exec("insert into series_stats values (6,100,'collection','2021-01-01')");
        // shelf() fixtures: fan_made row + a decade/publisher spread layered onto series 1-6
        q.exec("insert into download values (12,4,'Hulk Fan Omnibus','https://g/12/','2021-06-01','collection','fan',1,2021)");
        q.exec("insert into series values (7,'Spawn',2010,0,20,'Image','https://c/spawn.jpg','')");
        q.exec("insert into series_stats values (7,20,'single','2015-01-01')");

        // curated catalog fixtures (locg_id-keyed)
        q.exec("create table curated_series(locg_id text primary key, rank int, title text, norm_title text, year int, slug text, publisher text, cover text, synopsis text)");
        q.exec("create table curated_edition(id integer primary key autoincrement, locg_id text, title text, display_title text, format text, collects text, isbn text, pages int, published text, chid text, cover text, available int, getcomics_post text, creators text, description text)");
        q.exec("create table curated_genre(locg_id text, genre text)");
        q.exec("insert into curated_series values ('locg1',1,'Invincible','invincible',2003,'invincible','Image','https://c/inv.jpg','A boy gains powers')");
        q.exec("insert into curated_series values ('locg2',2,'Saga','saga',2012,'saga','Image','https://c/saga2.jpg','Two soldiers')");
        q.exec("insert into curated_series values ('locg3',3,'Undiscovered','undiscovered',2015,'undiscovered','Indie','','no downloadable editions yet')");
        q.exec("insert into curated_edition values (1,'locg1','Invincible Vol. 1','Invincible Volume 1','tpb','#1-4','111',112,'2004-01-01','chid1','https://c/inv1.jpg',1,'https://gc/inv1/','Kirkman','collects the first arc')");
        q.exec("insert into curated_edition values (2,'locg1','Invincible Vol. 2','Invincible Volume 2','tpb','#5-8','222',112,'2005-01-01','chid2','https://c/inv2.jpg',0,'','Kirkman','collects the second arc')");
        q.exec("insert into curated_edition values (3,'locg2','Saga Vol. 1','Saga Volume 1','tpb','#1-6','333',160,'2013-01-01','chid3','https://c/sagaed1.jpg',1,'https://gc/saga1/','Vaughan','collects the first arc')");
        q.exec("insert into curated_edition values (4,'locg3','Undiscovered Vol. 1','Undiscovered Volume 1','tpb','#1-4','444',100,'2016-01-01','chid4','',0,'','Nobody','not yet available')");
        q.exec("insert into curated_genre values ('locg1','superhero')");
        q.exec("insert into curated_genre values ('locg1','action')");
        q.exec("insert into curated_genre values ('locg2','action')");
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
    const QVariantList rank = cat.search("hulk", 10);
    if (rank.size() != 3) return fail("hulk search should hit exactly 3");
    if (rank[0].toMap().value("gcdId").toInt() != 4) return fail("exact 'Hulk' must rank first despite fewest downloads");
    if (rank[1].toMap().value("gcdId").toInt() != 5) return fail("prefix 'Hulkverines' must rank second");
    if (rank[2].toMap().value("gcdId").toInt() != 6) return fail("contains-only 'Immortal Hulk' ranks last despite most downloads");
    const QVariantList ex = cat.exactMatches("BATMAN");
    if (ex.size() != 2) return fail("exactMatches case-insensitive both runs");
    if (cat.exactMatches("bat").size() != 0) return fail("exactMatches is exact, not prefix");

    // --- curated catalog ---
    if (!cat.curatedReady()) return fail("curatedReady on a db with curated_series rows");
    const QVariantList ranked = cat.curatedRanked();
    if (ranked.size() != 3) return fail("curatedRanked count");
    if (ranked[0].toMap().value("locgId").toString() != "locg1") return fail("curatedRanked rank ASC (locg1 first)");
    if (ranked[1].toMap().value("locgId").toString() != "locg2") return fail("curatedRanked rank ASC (locg2 second)");
    if (ranked[2].toMap().value("locgId").toString() != "locg3") return fail("curatedRanked rank ASC (locg3 third)");
    {
        const QString g1 = ranked[0].toMap().value("genres").toString();
        if (g1 != "superhero,action") return fail("curatedRanked genres group_concat order");
    }
    if (ranked[1].toMap().value("genres").toString() != "action") return fail("curatedRanked single genre");
    if (ranked[2].toMap().value("genres").toString() != "") return fail("curatedRanked empty genres string when no genre rows");

    const QVariantMap inv = cat.curatedSeries("locg1");
    if (inv.value("title").toString() != "Invincible") return fail("curatedSeries title");
    if (inv.value("synopsis").toString() != "A boy gains powers") return fail("curatedSeries synopsis");
    const QVariantList invEd = inv.value("editions").toList();
    if (invEd.size() != 2) return fail("curatedSeries editions count");
    if (invEd[0].toMap().value("chid").toString() != "chid1") return fail("curatedSeries editions id ASC (chid1 first)");
    if (invEd[0].toMap().value("available").toBool() != true) return fail("curatedSeries edition available true");
    if (invEd[1].toMap().value("available").toBool() != false) return fail("curatedSeries edition available false");
    if (invEd[1].toMap().value("displayTitle").toString() != "Invincible Volume 2") return fail("curatedSeries edition displayTitle");
    if (!cat.curatedSeries("nope").isEmpty()) return fail("curatedSeries missing locgId must be {}");

    const QVariantMap byNormHit = cat.curatedByNorm("saga");
    if (byNormHit.value("locgId").toString() != "locg2") return fail("curatedByNorm hit");
    if (!cat.curatedByNorm("nonexistent-title").isEmpty()) return fail("curatedByNorm miss must be {}");

    const QVariantList shelves = cat.curatedGenreShelves(5);
    if (shelves.size() != 2) return fail("curatedGenreShelves genre count");
    if (shelves[0].toMap().value("name").toString() != "action") return fail("curatedGenreShelves order (count DESC: action=2 first)");
    if (shelves[0].toMap().value("count").toInt() != 2) return fail("curatedGenreShelves action count=2");
    {
        const QVariantList covers = shelves[0].toMap().value("covers").toList();
        if (covers.size() != 2) return fail("curatedGenreShelves action covers count");
        if (covers[0].toString() != "https://c/inv.jpg") return fail("curatedGenreShelves covers rank order (locg1 first)");
    }
    if (shelves[1].toMap().value("name").toString() != "superhero") return fail("curatedGenreShelves order (count DESC then name ASC)");

    if (!cat.curatedHasDownloadable("locg1")) return fail("curatedHasDownloadable true (chid1 available+post)");
    if (!cat.curatedHasDownloadable("locg2")) return fail("curatedHasDownloadable true for locg2 too (chid3 available+post)");
    if (cat.curatedHasDownloadable("locg3")) return fail("curatedHasDownloadable false — locg3's only edition is unavailable/no post");
    if (cat.curatedHasDownloadable("nope")) return fail("curatedHasDownloadable false on missing locgId");

    // --- shelf (browse-landing) ---
    const QVariantList stocked = cat.shelf("stocked", "", 10);
    if (stocked.isEmpty() || stocked[0].toMap().value("gcdId").toInt() != 6) return fail("shelf stocked downloads DESC (Immortal Hulk=100 first)");
    const QVariantList marvel = cat.shelf("publisher", "Marvel", 10);
    if (marvel.size() != 3) return fail("shelf publisher count (Marvel: gcd 4,5,6)");
    if (marvel[0].toMap().value("gcdId").toInt() != 6) return fail("shelf publisher downloads DESC");
    const QVariantList decade2010 = cat.shelf("decade", "2010", 10);
    // years 2010-2019: Saga(1,2012) Batman-2016(3,2016) Hulkverines(5,2019) Immortal Hulk(6,2018) Spawn(7,2010)
    if (decade2010.size() != 5) return fail("shelf decade 2010 count");
    if (decade2010[0].toMap().value("gcdId").toInt() != 6) return fail("shelf decade downloads DESC (Immortal Hulk=100 first)");
    if (decade2010[1].toMap().value("gcdId").toInt() != 7) return fail("shelf decade downloads DESC (Spawn=20 second)");
    const QVariantList deep = cat.shelf("deep", "", 10);
    if (deep.size() != 3) return fail("shelf deep >=10 count (Hulkverines=10, Spawn=20, Immortal Hulk=100)");
    if (deep[0].toMap().value("gcdId").toInt() != 6) return fail("shelf deep downloads DESC (Immortal Hulk=100 first)");
    if (deep[1].toMap().value("gcdId").toInt() != 7) return fail("shelf deep downloads DESC (Spawn=20 second)");
    if (deep[2].toMap().value("gcdId").toInt() != 5) return fail("shelf deep downloads DESC (Hulkverines=10 third)");
    const QVariantList fanmade = cat.shelf("fanmade", "", 10);
    if (fanmade.size() != 1 || fanmade[0].toMap().value("gcdId").toInt() != 4) return fail("shelf fanmade (only Hulk has a fan_made download)");
    if (!cat.shelf("bogus", "", 10).isEmpty()) return fail("shelf unknown kind must be empty");

    ComicsCatalog missing(dir.filePath("nope.db"));
    if (missing.ready()) return fail("missing db must not be ready");
    if (!missing.search("x", 5).isEmpty() || !missing.series(1).isEmpty()
        || !missing.downloadsFor(1).isEmpty() || !missing.exactMatches("x").isEmpty())
        return fail("not-ready must return empty, never crash");
    if (missing.curatedReady()) return fail("not-ready curatedReady must be false");
    if (!missing.curatedRanked().isEmpty() || !missing.curatedSeries("locg1").isEmpty()
        || !missing.curatedByNorm("saga").isEmpty() || !missing.curatedGenreShelves(5).isEmpty()
        || missing.curatedHasDownloadable("locg1") || !missing.shelf("stocked", "", 10).isEmpty())
        return fail("not-ready curated/shelf accessors must return empty/false, never crash");
    std::fprintf(stdout, "COMICS-CATALOG-ENGINE OK\n");
    return 0;
}
