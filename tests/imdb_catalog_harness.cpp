// ImdbCatalog — allowlisted, bound, paged movies/shows query contract (spec 2026-08-02).
// Builds a temp SQLite fixture with the baked schema, opens it read-only through
// ImdbCatalog, and proves: type/order filters, rating/vote bands, year windows, runtime,
// genre join, lang/notLang, excludeAnime, offset paging without overlap, the limit clamp,
// strict allowlisting, bound values (injection inert), and titleFacts batch lookup.
#include "engine/ImdbCatalog.h"

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
void require(bool c, const char* m) {
    if (!c) { std::cerr << "FAIL: " << m << '\n'; std::exit(1); }
}
QStringList ttsOf(const QVariantList& rows) {
    QStringList out;
    for (const QVariant& v : rows) out << v.toMap().value("tt").toString();
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString dbPath = QDir::temp().filePath("imdb_catalog_fixture.db");
    QFile::remove(dbPath);
    {
        QSqlDatabase w = QSqlDatabase::addDatabase("QSQLITE", "writer");
        w.setDatabaseName(dbPath);
        require(w.open(), "fixture opens");
        QSqlQuery q(w);
        require(q.exec("CREATE TABLE title (tt TEXT PRIMARY KEY, type TEXT, title TEXT,"
                       " year INTEGER, endYear INTEGER, runtimeMin INTEGER, genres TEXT,"
                       " rating REAL, votes INTEGER, episodes INTEGER,"
                       " origLang TEXT, isAnime INTEGER)"), "title table");
        require(q.exec("CREATE TABLE genre (tt TEXT, genre TEXT)"), "genre table");
        auto ins = [&](const char* tt, const char* type, const char* title, int year,
                       int runtime, double rating, int votes, int episodes,
                       const char* lang, int anime, const char* genresJson) {
            QSqlQuery i(w);
            i.prepare("INSERT INTO title VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
            i.addBindValue(tt); i.addBindValue(type); i.addBindValue(title);
            i.addBindValue(year); i.addBindValue(0); i.addBindValue(runtime);
            i.addBindValue(genresJson); i.addBindValue(rating); i.addBindValue(votes);
            i.addBindValue(episodes); i.addBindValue(lang); i.addBindValue(anime);
            require(i.exec(), "insert title");
        };
        auto tag = [&](const char* tt, const char* g) {
            QSqlQuery i(w);
            i.prepare("INSERT INTO genre VALUES (?,?)");
            i.addBindValue(tt); i.addBindValue(g);
            require(i.exec(), "insert genre");
        };
        //   tt      type      title            year rt   rating votes    eps lang anime genres
        ins("tt1",  "movie",  "Famous Classic", 1994, 142, 9.3, 3000000,  0, "en", 0, "[\"Drama\"]");
        ins("tt2",  "movie",  "Quiet Gem",      2011, 101, 7.9,   45000,  0, "en", 0, "[\"Drama\"]");
        ins("tt3",  "movie",  "Old Cult",       1988,  96, 7.6,   80000,  0, "en", 0, "[\"Horror\"]");
        ins("tt4",  "movie",  "French Film",    2001, 122, 8.3,  790000,  0, "fr", 0, "[\"Comedy\"]");
        ins("tt5",  "movie",  "Anime Film",     2001, 125, 8.6,  900000,  0, "ja", 1, "[\"Animation\"]");
        ins("tt6",  "movie",  "Western Toon",   2010,  90, 7.8,  300000,  0, "en", 0, "[\"Animation\"]");
        ins("tt7",  "series", "Great Series",   2008,   0, 9.5, 2300000, 62, "en", 0, "[\"Crime\",\"Drama\"]");
        ins("tt8",  "series", "Anime Series",   2013,   0, 9.1,  600000, 90, "ja", 1, "[\"Animation\"]");
        ins("tt9",  "mini",   "True Mini",      2019,   0, 9.3,  900000,  5, "en", 0, "[\"Drama\"]");
        ins("tt10", "series", "Long Runner",    1989,   0, 8.7,  450000, 750, "en", 0, "[\"Comedy\"]");
        ins("tt11", "movie",  "Korean Film",    2019, 132, 8.5,  950000,  0, "ko", 0, "[\"Thriller\"]");
        ins("tt12", "movie",  "No Lang",        2015, 110, 8.0,   20000,  0, "",   0, "[\"Drama\"]");
        for (int i = 0; i < 120; ++i)
            ins(("tt9" + QString::number(100 + i)).toUtf8().constData(), "movie",
                "Filler", 2005, 100, 6.0, 1500 + i, 0, "en", 0, "[]");
        tag("tt3", "Horror"); tag("tt4", "Comedy"); tag("tt5", "Animation");
        tag("tt6", "Animation"); tag("tt8", "Animation");
        w.close();
    }
    QSqlDatabase::removeDatabase("writer");

    ImdbCatalog cat(dbPath);
    require(cat.ready(), "opens read-only");

    { // type + rating/vote floor + order
        QVariantMap q{{"type","movie"},{"ratingMin",8.0},{"votesMin",200000},{"order","rating"}};
        const auto r = cat.titleCatalog(q, 0, 24);
        require(ttsOf(r).first() == "tt1", "top rated first is the famous classic");
        require(!ttsOf(r).contains("tt2"), "below vote floor excluded");
    }
    { // vote band (hidden gems shape) excludes the famous title
        QVariantMap q{{"type","movie"},{"ratingMin",7.4},{"votesMin",10000},{"votesMax",100000},{"order","rating"}};
        const auto tts = ttsOf(cat.titleCatalog(q, 0, 24));
        require(tts.contains("tt2") && tts.contains("tt3") && !tts.contains("tt1"),
                "vote band keeps gems, bans the blockbuster");
    }
    { // year window + excludeAnime
        QVariantMap q{{"type","movie"},{"yearTo",1999},{"order","votes"},{"excludeAnime",true}};
        const auto tts = ttsOf(cat.titleCatalog(q, 0, 24));
        require(tts.contains("tt3") && tts.contains("tt1") && !tts.contains("tt5"), "pre-2000 sans anime");
    }
    { // genre join + excludeAnime: western toon in, anime film out
        QVariantMap q{{"type","movie"},{"genre","Animation"},{"excludeAnime",true},{"order","votes"}};
        require(ttsOf(cat.titleCatalog(q, 0, 24)) == QStringList{"tt6"}, "animation minus anime");
    }
    { // notGenre (list): live-action shelves drop animation-tagged rows the anime flag missed
        QVariantMap q{{"type","movie"},{"notGenre", QVariantList{"Animation"}},{"order","votes"}};
        const auto tts = ttsOf(cat.titleCatalog(q, 0, 24));
        require(!tts.contains("tt5") && !tts.contains("tt6"), "notGenre excludes animation-tagged titles");
        require(tts.contains("tt1"), "notGenre keeps non-animation titles");
    }
    { // lang + notLang
        QVariantMap fr{{"type","movie"},{"lang","fr"},{"order","rating"}};
        require(ttsOf(cat.titleCatalog(fr, 0, 24)) == QStringList{"tt4"}, "lang=fr");
        QVariantMap intl{{"type","movie"},{"notLang","en"},{"order","votes"},{"excludeAnime",true}};
        const auto tts = ttsOf(cat.titleCatalog(intl, 0, 24));
        require(tts.contains("tt4") && tts.contains("tt11") && !tts.contains("tt12") && !tts.contains("tt1"),
                "international = known non-en only");
    }
    { // series includes minis; mini exact; episodes order + floor
        QVariantMap s{{"type","series"},{"order","rating"},{"votesMin",100000},{"excludeAnime",true}};
        const auto tts = ttsOf(cat.titleCatalog(s, 0, 24));
        require(tts.contains("tt7") && tts.contains("tt9") && !tts.contains("tt8"),
                "series = tvSeries+mini, anime excluded");
        QVariantMap m{{"type","mini"},{"order","votes"}};
        require(ttsOf(cat.titleCatalog(m, 0, 24)) == QStringList{"tt9"}, "mini exact");
        QVariantMap lr{{"type","series"},{"episodesMin",100},{"order","episodes"}};
        require(ttsOf(cat.titleCatalog(lr, 0, 24)) == QStringList{"tt10"}, "long-running by episodes");
    }
    { // runtime
        QVariantMap q{{"type","movie"},{"runtimeMax",120},{"votesMin",10000},{"order","votes"},{"excludeAnime",true}};
        const auto tts = ttsOf(cat.titleCatalog(q, 0, 24));
        require(tts.contains("tt2") && !tts.contains("tt1"), "runtime cap");
    }
    { // paging: no overlap; clamp
        QVariantMap q{{"type","movie"},{"order","votes"}};
        const auto a = ttsOf(cat.titleCatalog(q, 0, 3));
        const auto b = ttsOf(cat.titleCatalog(q, 3, 3));
        for (const auto& t : a) require(!b.contains(t), "offset pages disjoint");
        require(cat.titleCatalog(q, 0, 99999).size() == 100, "limit clamps to 100");
    }
    { // allowlist + binding
        QVariantMap bogus{{"type","movie"},{"surprise","x"}};
        require(cat.titleCatalog(bogus, 0, 24).isEmpty(), "unknown key -> empty");
        QVariantMap badOrder{{"type","movie"},{"order","sideways"}};
        require(cat.titleCatalog(badOrder, 0, 24).isEmpty(), "unknown order -> empty");
        QVariantMap evil{{"type","movie"},{"genre","x'); DROP TABLE title;--"}};
        require(cat.titleCatalog(evil, 0, 24).isEmpty(), "injection matches nothing");
        QVariantMap still{{"type","movie"},{"order","votes"}};
        require(!cat.titleCatalog(still, 0, 24).isEmpty(), "table intact after injection");
    }
    { // titleFacts batch
        const auto f = cat.titleFacts({"tt1", "tt8", "ttMISSING"});
        require(f.contains("tt1") && f.value("tt1").toMap().value("votes").toInt() == 3000000, "facts votes");
        require(f.value("tt8").toMap().value("isAnime").toBool(), "facts isAnime");
        require(!f.contains("ttMISSING"), "missing id absent");
    }

    QFile::remove(dbPath);
    std::cout << "PASS ImdbCatalog allowlisted paged query contract\n";
    return 0;
}
