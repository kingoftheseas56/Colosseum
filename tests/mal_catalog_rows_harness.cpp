// MalCatalog::animeCatalog — allowlisted, bound, paged offline anime query contract
// (Theatre Deep Catalogue, Task 2). Builds a temporary SQLite fixture with the exact
// baked schema, opens it through MalCatalog (read-only), and proves: order/status/type/
// year/members-band/voteFloor filtering, offset paging with no overlap, the limit clamp
// to 100, strict key + order allowlisting (unknown -> empty), and that tag values are
// BOUND (a SQL-injection tag cannot drop the table).
//
// House convention: require() prints "FAIL: <msg>" and exits 1 (Release-safe); one PASS
// line on success. No argv fixtures — the DB is built in the temp dir and removed after.
#include "engine/MalCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
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

// Every returned row's year lies within [from, to].
bool allYearsBetween(const QVariantList& rows, int from, int to)
{
    for (const QVariant& v : rows) {
        const int y = v.toMap().value(QStringLiteral("year")).toInt();
        if (y < from || y > to) return false;
    }
    return !rows.isEmpty();
}

QStringList idsOf(const QVariantList& rows)
{
    QStringList out;
    for (const QVariant& v : rows)
        out << QString::number(v.toMap().value(QStringLiteral("mal_id")).toInt());
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString dbPath = QDir::temp().filePath(QStringLiteral("mal_catalog_rows_fixture.db"));
    QFile::remove(dbPath);

    // ── Build the fixture with a separate writable connection, then release it fully
    //    so MalCatalog can open the same file read-only. ──
    {
        QSqlDatabase w = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("fixture_writer"));
        w.setDatabaseName(dbPath);
        require(w.open(), "fixture db opens for writing");
        QSqlQuery q(w);
        require(q.exec(QStringLiteral(
            "CREATE TABLE anime (mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT, "
            "type TEXT, score REAL, scored_by INTEGER, members INTEGER, status TEXT, "
            "episodes INTEGER, year INTEGER, cover TEXT, synopsis TEXT, credits TEXT, tags TEXT)")),
            "create anime table");
        require(q.exec(QStringLiteral(
            "CREATE TABLE tag (medium TEXT, tag TEXT, mal_id INTEGER)")), "create tag table");

        auto insertAnime = [&](int id, const QString& title, const QString& type, QVariant score,
                               int scoredBy, int members, const QString& status, int year) {
            QSqlQuery ins(w);
            ins.prepare(QStringLiteral(
                "INSERT INTO anime (mal_id,title,title_english,type,score,scored_by,members,status,"
                "episodes,year,cover,synopsis,credits,tags) "
                "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
            ins.addBindValue(id);
            ins.addBindValue(title);
            ins.addBindValue(title);            // title_english
            ins.addBindValue(type);
            ins.addBindValue(score);            // may be null QVariant
            ins.addBindValue(scoredBy);
            ins.addBindValue(members);
            ins.addBindValue(status);
            ins.addBindValue(12);               // episodes
            ins.addBindValue(year);
            ins.addBindValue(QStringLiteral("http://cover/") + QString::number(id));
            ins.addBindValue(QStringLiteral("synopsis"));
            ins.addBindValue(QStringLiteral("[]"));   // credits JSON
            ins.addBindValue(QStringLiteral("[]"));   // tags JSON
            require(ins.exec(), "insert anime row");
        };
        auto insertTag = [&](const QString& tag, int id) {
            QSqlQuery ins(w);
            ins.prepare(QStringLiteral("INSERT INTO tag (medium,tag,mal_id) VALUES ('anime',?,?)"));
            ins.addBindValue(tag);
            ins.addBindValue(id);
            require(ins.exec(), "insert tag row");
        };

        //          id  title       type     score            votes    members   status              year
        insertAnime(1,  "Air TV A",  "TV",    QVariant(8.5),   90000,   500000,   "Currently Airing",  2015);
        insertAnime(2,  "Air TV B",  "TV",    QVariant(8.9),   200000,  900000,   "Currently Airing",  2012);
        insertAnime(3,  "Fin TV C",  "TV",    QVariant(9.1),   800000,  2000000,  "Finished Airing",   2009);
        insertAnime(4,  "Up TV D",   "TV",    QVariant(),      0,       3000,     "Not yet aired",     2027);
        insertAnime(5,  "Movie E",   "Movie", QVariant(8.7),   150000,  700000,   "Finished Airing",   2016);
        insertAnime(6,  "Movie F",   "Movie", QVariant(9.3),   400000,  1200000,  "Finished Airing",   2001);
        insertAnime(7,  "Gem G",     "TV",    QVariant(8.2),   30000,   90000,    "Finished Airing",   2018);
        insertAnime(8,  "Thin H",    "TV",    QVariant(9.6),   120,     4000,     "Finished Airing",   2020);
        insertAnime(9,  "Old I",     "TV",    QVariant(8.0),   60000,   300000,   "Finished Airing",   1998);
        insertAnime(10, "Decade J",  "TV",    QVariant(7.8),   40000,   250000,   "Finished Airing",   2013);

        insertTag("Action", 1);
        insertTag("Action", 3);
        insertTag("Mecha", 3);
        insertTag("Romance", 7);

        // 120 filler rows so the 100-row clamp is provable (all low members/year/votes so
        // they never disturb the named-row assertions).
        for (int i = 1; i <= 120; ++i)
            insertAnime(1000 + i, QStringLiteral("Filler ") + QString::number(i),
                        "TV", QVariant(6.0), 100, 100 + i, "Finished Airing", 2005);

        w.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("fixture_writer"));

    MalCatalog catalog(dbPath);
    require(catalog.ready(), "MalCatalog opens the fixture read-only");

    // ── order=score + voteFloor: only titles above the vote floor, ranked by score ──
    {
        QVariantMap top{{"order", "score"}, {"voteFloor", 5000}};
        const QVariantList r = catalog.animeCatalog(top, 0, 2);
        require(r.size() == 2, "score+voteFloor top respects the limit");
        require(r.first().toMap().value("mal_id").toInt() == 6, "highest-scored above floor is first");
        require(r.at(1).toMap().value("mal_id").toInt() == 3, "second highest-scored above floor is second");
    }

    // ── status filter ──
    {
        QVariantMap airing{{"status", "Currently Airing"}, {"order", "members"}};
        const QVariantList r = catalog.animeCatalog(airing, 0, 24);
        require(!r.isEmpty(), "airing query returns rows");
        require(r.first().toMap().value("status").toString() == QStringLiteral("Currently Airing"),
                "airing query is status-filtered");
        require(r.first().toMap().value("mal_id").toInt() == 2, "airing ordered by members desc");
    }

    // ── type filter ──
    {
        QVariantMap movies{{"type", "Movie"}, {"order", "score"}};
        const QVariantList r = catalog.animeCatalog(movies, 0, 24);
        require(!r.isEmpty(), "movie query returns rows");
        require(r.first().toMap().value("type").toString() == QStringLiteral("Movie"),
                "movie query is type-filtered");
        require(r.first().toMap().value("mal_id").toInt() == 6, "movies ordered by score desc");
    }

    // ── decade window: every row inside [2010, 2019] ──
    {
        QVariantMap decade{{"yearFrom", 2010}, {"yearTo", 2019}, {"order", "members"}};
        const QVariantList r = catalog.animeCatalog(decade, 0, 24);
        require(allYearsBetween(r, 2010, 2019), "decade window keeps only in-range years");
    }

    // ── members band + voteFloor (Hidden Gems shape) ──
    {
        QVariantMap gems{{"membersMin", 20000}, {"membersMax", 150000},
                         {"voteFloor", 2000}, {"order", "score"}};
        const QVariantList r = catalog.animeCatalog(gems, 0, 24);
        require(r.size() == 1 && r.first().toMap().value("mal_id").toInt() == 7,
                "members band + voteFloor isolates the mid-popularity title");
    }

    // ── tag filter (bound) ──
    {
        QVariantMap actionQ{{"tag", "Action"}, {"order", "members"}};
        const QVariantList r = catalog.animeCatalog(actionQ, 0, 24);
        require(idsOf(r) == (QStringList{"3", "1"}), "tag query returns tagged rows by members desc");
        QVariantMap mechaQ{{"tag", "Mecha"}, {"order", "members"}};
        require(catalog.animeCatalog(mechaQ, 0, 24).size() == 1, "single-tagged row isolated");
    }

    // ── offset paging: adjacent windows do not overlap ──
    {
        QVariantMap q{{"order", "members"}};
        const QStringList page0 = idsOf(catalog.animeCatalog(q, 0, 3));
        const QStringList page1 = idsOf(catalog.animeCatalog(q, 3, 3));
        require(page0.size() == 3 && page1.size() == 3, "both offset pages fill");
        for (const QString& id : page0)
            require(!page1.contains(id), "offset paging produces no overlap");
    }

    // ── limit clamps to 100 even when a larger limit is requested ──
    {
        QVariantMap q{{"order", "members"}};
        require(catalog.animeCatalog(q, 0, 99999).size() == 100, "limit clamps to 100");
    }

    // ── strict allowlist: an unknown key or unknown order value returns empty ──
    {
        QVariantMap bogusKey{{"order", "members"}, {"bogus", "x"}};
        require(catalog.animeCatalog(bogusKey, 0, 24).isEmpty(), "unknown query key returns empty");
        QVariantMap bogusOrder{{"order", "sideways"}};
        require(catalog.animeCatalog(bogusOrder, 0, 24).isEmpty(), "unknown order value returns empty");
    }

    // ── tag values are BOUND, never interpolated: an injection tag matches nothing and
    //    leaves the table intact for a follow-up query. ──
    {
        QVariantMap evil{{"tag", "Action'); DROP TABLE anime;--"}, {"order", "members"}};
        require(catalog.animeCatalog(evil, 0, 24).isEmpty(), "injection tag matches nothing");
        QVariantMap actionQ{{"tag", "Action"}, {"order", "members"}};
        require(catalog.animeCatalog(actionQ, 0, 24).size() == 2,
                "table survived the injection attempt (values are bound)");
    }

    QFile::remove(dbPath);
    std::cout << "PASS MalCatalog::animeCatalog allowlisted paged query contract\n";
    return 0;
}
