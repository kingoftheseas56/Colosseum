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
        q.exec("create table download_mirror(post_id integer, url text, host text, label text)");
        q.exec("insert into download_mirror values (10,'https://datanodes.to/f/a','datanodes.to','MIRROR DOWNLOAD')");
        q.exec("insert into download_mirror values (10,'https://1024terabox.com/s/b','1024terabox.com','TERABOX')");
        q.exec("insert into series values (7,'Spawn',2010,0,20,'Image','https://c/spawn.jpg','')");
        q.exec("insert into series_stats values (7,20,'single','2015-01-01')");
        // escape-pin fixture: literal-underscore title that an UNESCAPED prefix LIKE
        // would let 'Batman' wildcard past (see the bat_a assertion below)
        q.exec("insert into series values (8,'Bat_an',2005,0,5,'Indie','','')");
        q.exec("insert into series_stats values (8,1,'single','2005-01-01')");

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
    // mirror doors (spec 2026-07-18): downloadsFor rows carry their harvested mirrors
    const QVariantList m10 = dls[0].toMap().value("mirrors").toList();
    if (m10.size() != 2) return fail("post 10 must carry 2 mirror doors");
    if (m10[0].toMap().value("host").toString() != QStringLiteral("datanodes.to")
        && m10[1].toMap().value("host").toString() != QStringLiteral("datanodes.to"))
        return fail("mirror host missing");
    if (!dls[1].toMap().value("mirrors").toList().isEmpty())
        return fail("unharvested post must carry an EMPTY mirrors list");
    // graceful fallback (spec 2026-07-18): a db WITHOUT download_mirror must still
    // return download rows, each with an EMPTY mirrors list — never throw/empty-out.
    {
        QTemporaryDir dir2;
        const QString db2 = dir2.filePath("nomirror.db");
        {
            auto d = QSqlDatabase::addDatabase("QSQLITE", "nomirror");
            d.setDatabaseName(db2);
            d.open();
            QSqlQuery q(d);
            q.exec("create table series(gcd_id integer primary key, title text, year int, year_ended int, issue_count int, publisher text, cover text, synopsis text)");
            q.exec("create table download(post_id integer primary key, series_id int, title text, link text, date text, kind text, method text, fan_made int, year_start int)");
            q.exec("create table series_stats(series_id integer primary key, downloads int, kinds text, latest_post text)");
            q.exec("insert into series values (1,'Saga',2012,0,72,'Image','','')");
            q.exec("insert into download values (10,1,'Saga #1','l','2022-01-09','single','run_span',0,2022)");
            d.close();
        }
        QSqlDatabase::removeDatabase("nomirror");
        ComicsCatalog nomirror(db2);
        const QVariantList dl = nomirror.downloadsFor(1);
        if (dl.size() != 1) return fail("fallback: downloadsFor must still return rows without download_mirror");
        const QVariantMap row0 = dl[0].toMap();
        if (!row0.contains(QStringLiteral("mirrors")))
            return fail("fallback: row must carry a mirrors key even when the table is absent");
        if (!row0.value(QStringLiteral("mirrors")).toList().isEmpty())
            return fail("fallback: row's mirrors list must be EMPTY when the table is absent");
    }
    const QVariantList hits = cat.search("bat", 10);
    if (hits.size() != 3) return fail("search LIKE count (Batman x2 + Bat_an)");
    if (hits[0].toMap().value("gcdId").toInt() != 2) return fail("same class -> downloads DESC (1940 run, 5 dls, first)");
    if (cat.search("saga", 10).size() != 1) return fail("search exact");
    if (!cat.search("zz%_zz", 10).isEmpty()) return fail("metachar query must not wildcard-match");
    // prefix-tier escape is load-bearing: escaped, only the literal 'Bat_an' row is
    // tier 1 for "bat_a"; unescaped, 'Batman' wildcards into tier 1 and wins on downloads.
    // ("bat_an" itself can't discriminate — Bat_an is an EXACT match, tier 0 either way.)
    const QVariantList metaPrefix = cat.search("bat_a", 10);
    if (metaPrefix.size() != 3) return fail("bat_a tokens must match Batman runs + Bat_an");
    if (metaPrefix[0].toMap().value("title").toString() != QStringLiteral("Bat_an"))
        return fail("escaped prefix must rank literal 'Bat_an' first");
    const QVariantList rank = cat.search("hulk", 10);
    if (rank.size() != 3) return fail("hulk search should hit exactly 3");
    if (rank[0].toMap().value("gcdId").toInt() != 4) return fail("exact 'Hulk' must rank first despite fewest downloads");
    if (rank[1].toMap().value("gcdId").toInt() != 5) return fail("prefix 'Hulkverines' must rank second");
    if (rank[2].toMap().value("gcdId").toInt() != 6) return fail("contains-only 'Immortal Hulk' ranks last despite most downloads");
    // --- word-based matching (spec 2026-07-18): every typed word must appear in
    // the title, ANY order; phrase tiers still rank first. ---
    const QVariantList anyOrder = cat.search("hulk immortal", 10);
    if (anyOrder.size() != 1) return fail("order-free words must find 'The Immortal Hulk'");
    if (anyOrder[0].toMap().value("gcdId").toInt() != 6) return fail("order-free hit wrong row");
    const QVariantList inOrder = cat.search("immortal hulk", 10);
    if (inOrder.size() != 1 || inOrder[0].toMap().value("gcdId").toInt() != 6)
        return fail("in-order words must find 'The Immortal Hulk'");
    const QVariantList phrasePrefix = cat.search("the immortal", 10);
    if (phrasePrefix.size() != 1) return fail("phrase-prefix multi-word must hit");
    if (!cat.search("saga hulk", 10).isEmpty()) return fail("words that never co-occur must return empty");
    if (!cat.search("%", 10).isEmpty()) return fail("punctuation-only query must return empty");
    const QVariantList twoBat = cat.search("batman", 10);
    if (twoBat.size() != 2) return fail("single word still matches both Batman runs");
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

    // ========================================================================
    // Tankoban Discover — discovery filters + house ranking (spec 2026-08-01).
    // Built on a SEPARATE fixture db so every curated/shelf assertion above stays
    // byte-identical and green; this db is engineered for the ranking edge cases.
    // (Ground-truth per Hemanth: the curated comics catalogue carries NO explicit/
    // adult classification — no column, no maturity genre — so includeExplicit
    // gates NOTHING; the "mainstream-visibility" fixture below proves a horror /
    // Mature-Readers title is never spuriously hidden either way.)
    // ========================================================================
    {
        QTemporaryDir dir3;
        const QString db3 = dir3.filePath("disco.db");
        {
            auto d = QSqlDatabase::addDatabase("QSQLITE", "disco");
            d.setDatabaseName(db3);
            if (!d.open()) return fail("disco fixture open");
            QSqlQuery q(d);
            // the constructor's readiness probe reads `series`; an empty one makes m_ok true.
            q.exec("create table series(gcd_id integer primary key, title text, year int, year_ended int, issue_count int, publisher text, cover text, synopsis text)");
            q.exec("create table curated_series(locg_id text primary key, rank int, title text, norm_title text, year int, slug text, publisher text, cover text, synopsis text)");
            q.exec("create table curated_edition(id integer primary key autoincrement, locg_id text, title text, display_title text, format text, collects text, isbn text, pages int, published text, chid text, cover text, available int, getcomics_post text, creators text, description text)");
            q.exec("create table curated_genre(locg_id text, genre text)");
            // series — inserted deliberately NOT in publication-date order, so New
            // Releases ordering can only come from real edition years, never rowid.
            //  sv: BEST rank (1) but 0 available          -> availability-boost loser
            //  af: rank 2, fully available, recent         -> "Available Favorite" leads
            //  nr: NO rank (redistribution + metadata cap), fully available
            //  fr: newest editions (2025)                  -> New Releases first
            //  ds: 8 editions                              -> Most Stocked first
            //  mr: Mature-Readers/horror title             -> mainstream-visibility
            //  a1/a2: same norm_title 'alpha', diff year   -> All norm-then-year tie
            //  zz: worst rank, last-inserted (rowid trap)
            q.exec("insert into curated_series values ('sv',1,'Stellar Vault','stellar vault',2000,'stellar-vault','DC','https://c/sv.jpg','a vault of stars')");
            q.exec("insert into curated_series values ('af',2,'Available Favorite','available favorite',2020,'available-favorite','Image','https://c/af.jpg','beloved and fully in stock')");
            q.exec("insert into curated_series values ('nr',NULL,'Nameless Rank','nameless rank',2010,'nameless-rank','Indie','https://c/nr.jpg','')");
            q.exec("insert into curated_series values ('fr',20,'Fresh Off Press','fresh off press',2024,'fresh-off-press','Marvel','https://c/fr.jpg','hot new release')");
            q.exec("insert into curated_series values ('ds',15,'Deep Stacks','deep stacks',2011,'deep-stacks','DC','https://c/ds.jpg','deeply stocked run')");
            q.exec("insert into curated_series values ('mr',30,'Mature Mayhem','mature mayhem',2016,'mature-mayhem','Black Mask','https://c/mr.jpg','mature readers horror and violence')");
            q.exec("insert into curated_series values ('a1',50,'Alpha','alpha',2000,'alpha-1','','https://c/a1.jpg','alpha one')");
            q.exec("insert into curated_series values ('a2',60,'Alpha','alpha',2010,'alpha-2','Image','https://c/a2.jpg','alpha two')");
            q.exec("insert into curated_series values ('zz',100,'Zeta Filler','zeta filler',2005,'zeta-filler','','https://c/zz.jpg','filler tail')");
            // editions — published year drives recency/New-Releases; available+post
            // drives the availability fraction; row count drives Most-Stocked depth.
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('sv','2000',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('sv','2003',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('af','2021',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('af','2022',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('af','2023',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('af','2024',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('nr','2010',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('fr','2024',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('fr','2025',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('ds','2008',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('ds','2009',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('ds','2010',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('ds','2011',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('ds','2012',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('ds','2008',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('ds','2009',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('ds','2010',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('mr','2016',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('mr','2018',1,'p')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('mr','2020',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('a1','2000',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('a2','2010',0,'')");
            q.exec("insert into curated_edition (locg_id,published,available,getcomics_post) values ('zz','2005',0,'')");
            q.exec("insert into curated_genre values ('sv','Superhero')");
            q.exec("insert into curated_genre values ('af','Superhero')");
            q.exec("insert into curated_genre values ('af','Action & Adventure')");
            q.exec("insert into curated_genre values ('nr','Horror')");
            q.exec("insert into curated_genre values ('fr','Fantasy')");
            q.exec("insert into curated_genre values ('ds','Crime & Mystery')");
            q.exec("insert into curated_genre values ('mr','Horror')");
            q.exec("insert into curated_genre values ('a1','Drama')");
            q.exec("insert into curated_genre values ('a2','Drama')");
            q.exec("insert into curated_genre values ('zz','Humor')");
            d.close();
        }
        QSqlDatabase::removeDatabase("disco");
        ComicsCatalog disco(db3);
        if (!disco.curatedReady()) return fail("disco fixture curatedReady");

        auto idxOf = [](const QVariantList& L, const QString& id) -> int {
            for (int i = 0; i < L.size(); ++i)
                if (L[i].toMap().value("locgId").toString() == id) return i;
            return -1;
        };

        // --- discoverFilters: genre facet, count DESC then label ASC ---
        const QVariantList gf = disco.discoverFilters("genre", true);
        if (gf.size() != 7) return fail("discoverFilters genre facet count");
        if (gf[0].toMap().value("key").toString() != "Drama" || gf[0].toMap().value("count").toInt() != 2)
            return fail("genre facet count DESC then label ASC: Drama(2) first");
        if (gf[1].toMap().value("key").toString() != "Horror" || gf[1].toMap().value("count").toInt() != 2)
            return fail("genre facet: Horror(2) second");
        if (gf[2].toMap().value("key").toString() != "Superhero" || gf[2].toMap().value("count").toInt() != 2)
            return fail("genre facet: Superhero(2) third");
        if (gf[0].toMap().value("label").toString() != "Drama") return fail("genre facet label == key");
        if (gf[3].toMap().value("key").toString() != "Action & Adventure")
            return fail("genre facet count=1 tier ordered label ASC");
        // includeExplicit is a NO-OP for comics: identical facets either way.
        if (disco.discoverFilters("genre", false).size() != gf.size())
            return fail("includeExplicit must be a no-op for genre facets");

        // --- discoverFilters: publisher facet, blank publisher EXCLUDED ---
        const QVariantList pf = disco.discoverFilters("publisher", true);
        if (pf.size() != 5) return fail("publisher facet count (blank publisher excluded)");
        if (pf[0].toMap().value("key").toString() != "DC" || pf[0].toMap().value("count").toInt() != 2)
            return fail("publisher facet DC(2) first");
        if (pf[1].toMap().value("key").toString() != "Image" || pf[1].toMap().value("count").toInt() != 2)
            return fail("publisher facet Image(2) second");
        for (const QVariant& v : pf)
            if (v.toMap().value("key").toString().isEmpty()) return fail("blank publisher must not be a facet");
        if (!disco.discoverFilters("bogus", true).isEmpty()) return fail("unknown axis -> empty facets");
        if (!disco.discoverFilters("", true).isEmpty()) return fail("empty axis -> empty facets");

        // --- discoverPage popular: house ranking ---
        const QVariantMap popMap = disco.discoverPage("popular", "", "", true, 0, 100);
        if (popMap.value("freshness").toString() != "bundled") return fail("discoverPage freshness bundled");
        const QVariantList pop = popMap.value("items").toList();
        if (pop.size() != 9) return fail("popular full page size");
        // availability BOOST: the fully-available 'af' (rank 2) leads and outranks the
        // better-ranked but unavailable 'sv' (rank 1).
        if (pop[0].toMap().value("locgId").toString() != "af") return fail("popular: Available Favorite must lead (availability boost)");
        if (idxOf(pop, "af") >= idxOf(pop, "sv")) return fail("popular: available 'af' must outrank better-ranked-but-unavailable 'sv'");
        // no-rank redistribution: 'nr' is not slammed to worst, and metadata is capped.
        if (idxOf(pop, "nr") >= idxOf(pop, "sv")) return fail("popular: no-rank 'nr' must not be floored to worst");
        {
            const int i = idxOf(pop, "nr");
            const QVariantMap comps = pop[i].toMap().value("houseComponents").toMap();
            if (comps.size() != 4) return fail("houseComponents must carry 4 keys");
            if (comps.value("metadata").toDouble() > 0.10 + 1e-9)
                return fail("no-rank redistribution: metadata contribution must stay <= 0.10");
            if (comps.value("popularity").toDouble() != 0.0)
                return fail("no-rank row: popularity component must be 0 (rank absent)");
        }
        // row shape + houseComponents sum to houseScore
        {
            const QVariantMap r = pop[0].toMap();
            if (r.value("title").toString() != "Available Favorite") return fail("row title");
            if (r.value("year").toInt() != 2020) return fail("row year");
            if (r.value("publisher").toString() != "Image") return fail("row publisher");
            if (r.value("cover").toString() != "https://c/af.jpg") return fail("row cover");
            if (!r.value("genres").toString().contains("Superhero")) return fail("row genres");
            if (r.value("availability").toBool() != true) return fail("row availability bool (af available)");
            if (r.value("explicit").toBool() != false) return fail("row explicit:false always");
            const QVariantMap c = r.value("houseComponents").toMap();
            const double sum = c.value("popularity").toDouble() + c.value("availability").toDouble()
                             + c.value("recency").toDouble() + c.value("metadata").toDouble();
            if (qAbs(sum - r.value("houseScore").toDouble()) > 1e-9) return fail("houseComponents must sum to houseScore");
            if (r.value("houseScore").toDouble() <= 0.0 || r.value("houseScore").toDouble() > 1.0)
                return fail("houseScore in (0,1]");
        }
        if (pop[idxOf(pop, "sv")].toMap().value("availability").toBool() != false)
            return fail("row availability bool (sv unavailable)");

        // --- New Releases: by real publication year, NOT rowid ---
        const QVariantList nrl = disco.discoverPage("new-releases", "", "", true, 0, 100).value("items").toList();
        if (nrl.size() != 9) return fail("new-releases size");
        if (nrl[0].toMap().value("locgId").toString() != "fr") return fail("new-releases: newest editions (fr,2025) first");
        if (nrl[1].toMap().value("locgId").toString() != "af") return fail("new-releases: 2024 second");
        if (nrl[0].toMap().value("locgId").toString() == "zz") return fail("new-releases must not be rowid order (zz inserted last)");

        // --- Most Stocked: by edition depth, house rank tie-break ---
        const QVariantList ms = disco.discoverPage("most-stocked", "", "", true, 0, 100).value("items").toList();
        if (ms[0].toMap().value("locgId").toString() != "ds") return fail("most-stocked: deepest (ds, 8 editions) first");
        if (idxOf(ms, "fr") >= idxOf(ms, "sv")) return fail("most-stocked: equal-depth tie broken by house rank (fr before sv)");

        // --- All: alphabetical by normalized title then start year ---
        const QVariantList all = disco.discoverPage("all", "", "", true, 0, 100).value("items").toList();
        if (all[0].toMap().value("locgId").toString() != "a1") return fail("all: norm 'alpha' + year 2000 first");
        if (all[1].toMap().value("locgId").toString() != "a2") return fail("all: same norm broken by year (2010 second)");
        if (all[2].toMap().value("locgId").toString() != "af") return fail("all: 'available favorite' third");

        // --- pagination: stable, no dup/skip across pages ---
        const QVariantMap pg0 = disco.discoverPage("popular", "", "", true, 0, 3);
        const QVariantMap pg1 = disco.discoverPage("popular", "", "", true, 3, 3);
        const QVariantMap pg2 = disco.discoverPage("popular", "", "", true, 6, 3);
        const QVariantList l0 = pg0.value("items").toList(), l1 = pg1.value("items").toList(), l2 = pg2.value("items").toList();
        if (l0.size() != 3 || l1.size() != 3 || l2.size() != 3) return fail("pagination page sizes");
        if (pg0.value("nextOffset").toInt() != 3) return fail("pagination nextOffset");
        if (pg0.value("exhausted").toBool()) return fail("pagination page0 not exhausted");
        if (!pg2.value("exhausted").toBool()) return fail("pagination last page exhausted");
        for (int i = 0; i < 3; ++i) {
            if (l0[i].toMap().value("locgId") != pop[i].toMap().value("locgId")) return fail("page0 must match full[0..2]");
            if (l1[i].toMap().value("locgId") != pop[i + 3].toMap().value("locgId")) return fail("page1 must match full[3..5]");
        }

        // --- facet-scoped ranking ---
        const QVariantList horror = disco.discoverPage("popular", "genre", "Horror", true, 0, 100).value("items").toList();
        if (horror.size() != 2) return fail("popular+genre=Horror scopes to the 2 horror titles");
        if (idxOf(horror, "nr") < 0 || idxOf(horror, "mr") < 0) return fail("horror facet must contain nr and mr");
        const QVariantList dc = disco.discoverPage("popular", "publisher", "DC", true, 0, 100).value("items").toList();
        if (dc.size() != 2) return fail("popular+publisher=DC scopes to the 2 DC titles");
        if (dc[0].toMap().value("locgId").toString() != "sv") return fail("DC scope: rank-1 sv leads");

        // --- includeExplicit is a documented NO-OP; horror/Mature title stays VISIBLE ---
        const QVariantList popF = disco.discoverPage("popular", "", "", false, 0, 100).value("items").toList();
        if (popF.size() != pop.size()) return fail("includeExplicit must be a no-op for comics (same result set)");
        if (idxOf(pop, "mr") < 0 || idxOf(popF, "mr") < 0)
            return fail("mainstream-visibility: Mature/horror title must stay visible either way");

        // --- bound/allowlisted: injection matches nothing, table survives ---
        const QVariantList inj = disco.discoverPage("popular", "genre", "Action'); DROP TABLE curated_series;--", true, 0, 100).value("items").toList();
        if (!inj.isEmpty()) return fail("injection filterKey must match nothing (bound, not concatenated)");
        if (!disco.curatedReady()) return fail("injection must NOT drop curated_series (table survives)");
        if (!disco.discoverPage("bogus", "", "", true, 0, 10).value("items").toList().isEmpty())
            return fail("unknown catalogId -> empty items");
        if (disco.discoverPage("bogus", "", "", true, 0, 10).value("freshness").toString() != "bundled")
            return fail("unknown catalogId still returns a well-formed bundled map");
        if (!disco.discoverPage("popular", "bogus", "x", true, 0, 10).value("items").toList().isEmpty())
            return fail("unknown filterAxis -> empty items");

        // --- offset/limit clamping ---
        const QVariantList clampLim = disco.discoverPage("popular", "", "", true, 0, 0).value("items").toList();
        if (clampLim.size() != 1 || clampLim[0].toMap().value("locgId").toString() != "af")
            return fail("limit 0 clamps to 1");
        const QVariantList clampOff = disco.discoverPage("popular", "", "", true, -5, 3).value("items").toList();
        if (clampOff.size() != 3 || clampOff[0].toMap().value("locgId").toString() != "af")
            return fail("negative offset clamps to 0");
    }

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
    if (!missing.discoverFilters("genre", true).isEmpty()
        || !missing.discoverPage("popular", "", "", true, 0, 10).value("items").toList().isEmpty())
        return fail("not-ready discovery accessors must return empty, never crash");
    std::fprintf(stdout, "COMICS-CATALOG-ENGINE OK\n");
    return 0;
}
