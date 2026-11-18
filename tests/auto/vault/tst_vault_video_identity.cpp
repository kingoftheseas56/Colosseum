#include "engine/ImdbCatalog.h"
#include "engine/VaultIdentifier.h"
#include "engine/VaultKit.h"
#include "engine/VaultScanner.h"
#include "engine/VaultWatcher.h"
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
QString catalog(const QString &path, bool aliases = false)
{
    const QString name = "video_fixture_writer";
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        if (!db.open())
            return {};
        QSqlQuery q(db);
        if (!q.exec("CREATE TABLE title(tt TEXT PRIMARY KEY,type TEXT,title TEXT,norm_title "
                    "TEXT,year INTEGER,endYear INTEGER,runtimeMin INTEGER,genres TEXT,rating "
                    "REAL,votes INTEGER,episodes INTEGER,origLang TEXT,isAnime INTEGER)"))
            return {};
        const QStringList records = {"tt1|movie|Casino Royale|casino royale|2006",
                                     "tt2|movie|Dune|dune|1984",
                                     "tt3|movie|Dune|dune|2021",
                                     "tt4|movie|Clone Show|clone show|2008",
                                     "tt5|series|Clone Show|clone show|2008",
                                     "tt6|movie|Logan|logan|2017",
                                     "tt7|series|A Programme|programme|2005",
                                     "tt8|series|A Programme|programme|2020"};
        for (const auto &record : records)
        {
            auto v = record.split('|');
            q.prepare("INSERT INTO title VALUES(?,?,?,?,?,0,120,'[]',8,5000,0,'en',0)");
            for (const auto &f : v)
                q.addBindValue(f);
            if (!q.exec())
                return {};
        }
        if (aliases)
        {
            if (!q.exec("CREATE TABLE title_alias(tt TEXT,norm_title TEXT,title TEXT,region "
                        "TEXT,language TEXT,source TEXT)"))
                return {};
            if (!q.exec("INSERT INTO title_alias VALUES('tt7','alternate programme','Alternate "
                        "Programme','GB','en','title.akas.tsv'),('tt7','programme','A "
                        "Programme','GB','en','title.akas.tsv'),('tt7','alternate "
                        "programme','Alternate "
                        "Programme','US','en','title.akas.tsv'),('tt8','alternate "
                        "programme','Alternate Programme','US','en','title.akas.tsv')"))
                return {};
        }
        if (!q.exec("INSERT INTO title VALUES"
                    "('tt0388629','series','One Piece','one piece',1999,0,24,'[]',8,5000,1193,'ja',1),"
                    "('tt11737520','series','One Piece','one piece',2023,0,50,'[]',8,5000,17,'en',0)"))
            return {};
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return path;
}
VaultIndex::FileRow row(const QString &path, const QString &title, int year = 0)
{
    VaultIndex::FileRow r;
    r.id = "vault:" + path;
    r.kind = "video";
    r.path = path;
    r.realName = QFileInfo(path).fileName();
    r.groupTitle = title;
    r.groupKey = r.subtreePath = QFileInfo(path).absolutePath();
    r.rootPath = "D:/Videos";
    r.parsedYear = year;
    r.size = 100;
    r.mtimeMs = 1;
    return r;
}
QString touch(const QString &root, const QString &rel)
{
    auto p = root + '/' + rel;
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("fixture");
    return p;
}

// Release-noise / subtitle / numeral fixture (2026-11-16). tt ids are readable on purpose.
QString noiseCatalog(const QString &path)
{
    const QString name = "video_noise_fixture_writer";
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        if (!db.open())
            return {};
        QSqlQuery q(db);
        if (!q.exec("CREATE TABLE title(tt TEXT PRIMARY KEY,type TEXT,title TEXT,norm_title "
                    "TEXT,year INTEGER,endYear INTEGER,runtimeMin INTEGER,genres TEXT,rating "
                    "REAL,votes INTEGER,episodes INTEGER,origLang TEXT,isAnime INTEGER)"))
            return {};
        const QList<QStringList> records = {
            {"ttRogueOne", "movie", "Rogue One: A Star Wars Story", "rogue one a star wars story", "2016"},
            {"ttStrangelove", "movie", "Dr. Strangelove or: How I Learned to Stop Worrying and Love the Bomb",
             "dr strangelove or how i learned to stop worrying and love the bomb", "1964"},
            {"ttGodfather3", "movie", "The Godfather Part III", "godfather part iii", "1990"},
            {"ttInfinityWar", "movie", "Avengers: Infinity War", "avengers infinity war", "2018"},
            {"ttReadyPlayerOne", "movie", "Ready Player One", "ready player one", "2018"},
            {"ttHomecoming", "movie", "Spider-Man: Homecoming", "spider man homecoming", "2017"},
            {"ttMononoke", "movie", "Princess Mononoke", "princess mononoke", "1997"},
            {"ttSummerWars", "movie", "Summer Wars", "summer wars", "2009"},
            {"ttLionKing", "movie", "The Lion King", "lion king", "1994"},
            {"ttKillBillWBA", "movie", "Kill Bill: The Whole Bloody Affair", "kill bill the whole bloody affair", "2004"},
            {"ttKillBill1", "movie", "Kill Bill: Vol. 1", "kill bill vol 1", "2003"},
            {"ttKillBill2", "movie", "Kill Bill: Vol. 2", "kill bill vol 2", "2004"},
            {"ttOz", "movie", "The Wizard of Oz", "wizard of oz", "1939"},
            {"ttTerminator2", "movie", "Terminator 2: Judgment Day", "terminator 2 judgment day", "1991"},
            {"ttBladeRunner1982", "movie", "Blade Runner", "blade runner", "1982"},
            {"ttWonderWoman2017", "movie", "Wonder Woman", "wonder woman", "2017"},
            {"ttIronMan2008", "movie", "Iron Man", "iron man", "2008"},
            {"ttIronMan1994", "series", "Iron Man", "iron man", "1994"},
            {"ttBatman1989", "movie", "Batman", "batman", "1989"},
            {"ttBatmanPhantasm", "movie", "Batman: Mask of the Phantasm", "batman mask of the phantasm", "1993"},
            {"ttFooAlpha", "movie", "Foo Bar: Alpha", "foo bar alpha", "2001"},
            {"ttFooBeta", "movie", "Foo Bar: Beta", "foo bar beta", "2001"},
            {"ttInglourious", "movie", "Inglourious Basterds", "inglourious basterds", "2009"},
            {"ttGetOut", "movie", "Get Out", "get out", "2017"},
            {"ttInception", "movie", "Inception", "inception", "2010"},
            {"ttPhantomMenace", "movie", "Star Wars: Episode I - The Phantom Menace",
             "star wars episode i the phantom menace", "1999"},
            {"ttSaga2", "movie", "Fixture Saga 2", "fixture saga 2", "2005"},
            {"ttSagaIV", "movie", "Fixture Saga IV", "fixture saga iv", "2006"},
            {"ttExtendedFamily", "movie", "The Extended Family", "extended family", "2008"},
            {"ttXMen1992", "series", "X-Men", "x men", "1992"},
            {"ttXMen2000", "movie", "X-Men", "x men", "2000"},
            {"ttShazam", "movie", "Superman/Shazam!: The Return of Black Adam", "superman shazam the return of black adam", "2010"},
            {"ttAllStar", "movie", "All Star Superman", "all star superman", "2011"},
            {"ttTDKR2", "movie", "Batman: The Dark Knight Returns, Part 2", "batman the dark knight returns part 2", "2013"},
            {"ttMoviesGeneric", "movie", "Movies", "movies", "2010"}};
        for (const auto &v : records)
        {
            q.prepare("INSERT INTO title VALUES(?,?,?,?,?,0,120,'[]',8,5000,0,'en',0)");
            for (const auto &f : v)
                q.addBindValue(f);
            if (!q.exec())
                return {};
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return path;
}

struct Identified
{
    bool adopted = false;
    QString sourceId;
    int candidates = 0;
    QString groupTitle;
    int parsedYear = 0;
};

// Runs the real scanner grouping for one file path, then the real identifier against the fixture.
Identified identifyPath(const QString &dbPath, const QString &path)
{
    QTemporaryDir d;
    VaultIndex index(d.filePath("index.sqlite"));
    VaultIndex::FileRow r;
    r.id = "vault:" + path;
    r.kind = "video";
    r.path = path;
    r.realName = QFileInfo(path).fileName();
    r.subtreePath = QFileInfo(path).absolutePath();
    r.rootPath = "D:/Videos";
    r.size = 100;
    r.mtimeMs = 1;
    VaultScanner::assignVideoGroup(r, false, false, {path});
    Identified out;
    out.groupTitle = r.groupTitle;
    out.parsedYear = r.parsedYear;
    if (!index.publish({r}))
        return out;
    ImdbCatalog c(dbPath);
    VaultIdentifier id(&index, nullptr, nullptr, &c);
    const auto m = id.matchGroup(r.groupKey);
    out.adopted = m.adopted;
    out.sourceId = m.sourceId;
    out.candidates = m.candidateCount;
    return out;
}

QVariantMap fakeWorkerRow(QSqlQuery &q)
{
    QVariantMap m;
    m["tt"] = q.value(0).toString();
    m["type"] = q.value(1).toString();
    m["title"] = q.value(2).toString();
    m["year"] = q.value(3).toInt();
    return m;
}
// Emulates server/content-api catalogue-semantic.ts: matchByTitle = exact norm_title (+year),
// search = norm_title LIKE key% with a limit. Records every call.
QVariant fakeWorker(QSqlDatabase &db, QStringList &calls, const QString &method, const QVariantList &args)
{
    calls << method + ":" + args.value(0).toString();
    QVariantList out;
    QSqlQuery q(db);
    if (method == "matchByTitle")
    {
        q.prepare("SELECT tt,type,title,year FROM title WHERE norm_title=? AND (?=0 OR year=?)");
        q.addBindValue(args.value(0));
        q.addBindValue(args.value(1));
        q.addBindValue(args.value(1));
    }
    else
    {
        q.prepare("SELECT tt,type,title,year FROM title WHERE norm_title LIKE ? ORDER BY CASE WHEN norm_title=? THEN 0 ELSE 1 END,year DESC LIMIT ?");
        q.addBindValue(args.value(0).toString() + "%");
        q.addBindValue(args.value(0));
        q.addBindValue(args.value(1));
    }
    if (q.exec())
        while (q.next())
            out << fakeWorkerRow(q);
    return out;
}
} // namespace
class VideoIdentityTest : public QObject
{
    Q_OBJECT
  private slots:
    void absolute_episode_disambiguates_only_when_one_series_can_contain_it()
    {
        QTemporaryDir d;
        QVERIFY(d.isValid());
        const auto db = catalog(d.filePath("catalogue.sqlite"));
        QVERIFY(!db.isEmpty());
        const auto identifiable = identifyPath(db, d.filePath("[SubsPlease] One Piece - 1179 (1080p) [A02F35FE].mkv"));
        QVERIFY(identifiable.adopted);
        QCOMPARE(identifiable.sourceId, QStringLiteral("imdb:tt0388629"));
        for (const auto &episode : {QStringLiteral("012"), QStringLiteral("1200")}) {
            const auto ambiguous = identifyPath(db, d.filePath("[SubsPlease] One Piece - " + episode + " (1080p).mkv"));
            QVERIFY(!ambiguous.adopted);
            QCOMPARE(ambiguous.candidates, 2);
        }
    }

    void recordedProfileGenreReplay()
    {
        const auto directory=qEnvironmentVariable("VAULT_GENRE_REPLAY");
        if (directory.isEmpty()) QSKIP("Optional fresh-profile recorded Cinemeta replay");
        QFile file(directory + "/metas.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto metas=QJsonDocument::fromJson(file.readAll()).object();
        VaultIndex index(directory + "/replay.sqlite");
        VaultIdentifier id(&index,nullptr,nullptr,nullptr);
        int requests=0;
        QSet<QString> seen;
        id.setVideoGenreLookup([&](const QString& tt,const QString&,auto done) {
            QVERIFY(!seen.contains(tt)); seen.insert(tt); ++requests;
            done(metas.value(tt).toObject().toVariantMap());
        });
        id.backfillVideoGenres();
        const int first=requests;
        id.backfillVideoGenres();
        QCOMPARE(requests,first);
        QVERIFY(requests <= 30);
    }
    void manualMetaGenres_data()
    {
        QTest::addColumn<QString>("type");
        QTest::newRow("series") << "series";
        QTest::newRow("movie") << "movie";
    }
    void manualMetaGenres()
    {
        QFETCH(QString, type);
        QTemporaryDir d;
        VaultIndex index(d.filePath("index.sqlite"));
        auto a = row("D:/Videos/Test/S01E01.mkv", "Test");
        auto b = a; b.id += "2"; b.path += "2";
        QVERIFY(index.publish({a,b}));
        VaultIdentifier id(&index,nullptr,nullptr,nullptr);
        VaultIdentifier::Match match;
        match.adopted=true; match.source="IMDB"; match.sourceId="imdb:tt0141842";
        match.title="Test"; match.type=type; match.world="Theatre";
        match.genres=VaultIdentifier::videoGenres(QVariantList{"Crime","Drama"});
        QVERIFY(id.identifyGroupWith(a.groupKey,match));
        for (const auto& file : index.rowsForKind("video")) {
            QCOMPARE(file.identityGenres, "Crime · Drama");
            QCOMPARE(file.identityState,"manual");
        }
    }
    void genreBackfillOnceAndStaleReply()
    {
        QTemporaryDir d;
        VaultIndex index(d.filePath("index.sqlite"));
        auto a = row("D:/Videos/Test/S01E01.mkv", "Test");
        a.identityId="imdb:tt0141842"; a.identityType="series";
        a.identitySource="IMDB"; a.identityTitle="Test"; a.identityState="manual";
        auto b=a; b.id+="2"; b.groupKey+="2";
        auto movie=a; movie.id="movie"; movie.identityId="imdb:tt1234567";
        movie.identityType="movie"; movie.identityGenres="Comedy";
        QVERIFY(index.publish({a,b,movie}));
        VaultIdentifier id(&index,nullptr,nullptr,nullptr);
        int requests=0;
        VaultIdentifier::VideoGenreReply reply;
        id.setVideoGenreLookup([&](const QString& tt,const QString& type,auto done) {
            ++requests; QCOMPARE(tt,"tt0141842"); QCOMPARE(type,"series"); reply=done;
        });
        id.backfillVideoGenres(); id.backfillVideoGenres();
        QCOMPARE(requests,1);
        reply({{"id","tt0141842"},{"genres",QVariantList{"Crime","Drama"}}});
        id.backfillVideoGenres(); QCOMPARE(requests,1);
        for (const auto& file:index.rowsForKind("video")) {
            QCOMPARE(file.identityId,file.id=="movie" ? movie.identityId : a.identityId);
            QCOMPARE(file.identityGenres,file.id=="movie" ? QString("Comedy") : QString("Crime · Drama"));
            QCOMPARE(file.identityState,"manual");
        }
        // A late response after an explicit unmatch cannot restore the identity.
        QVERIFY(id.unidentifyGroup(b.groupKey));
        reply({{"id","tt0141842"},{"genres",QVariantList{"Thriller"}}});
        QVERIFY(index.rowsForGroup(b.groupKey).first().identityId.isEmpty());
        QVERIFY(index.rowsForGroup(b.groupKey).first().identityGenres.isEmpty());
    }
    void genreFailedRequestNotRepeated()
    {
        QTemporaryDir d;
        VaultIndex index(d.filePath("index.sqlite"));
        auto a=row("D:/Videos/Test/S01E01.mkv","Test");
        a.identityId="imdb:tt0141842"; a.identityType="series";
        QVERIFY(index.publish({a}));
        VaultIdentifier id(&index,nullptr,nullptr,nullptr);
        int requests=0;
        id.setVideoGenreLookup([&](const auto&,const auto&,auto done) { ++requests; done({}); });
        id.backfillVideoGenres(); id.backfillVideoGenres();
        QCOMPARE(requests,1);
        QVERIFY(index.rowsForKind("video").first().identityGenres.isEmpty());
    }
    void releaseTitleAndYear_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("title");
        QTest::addColumn<int>("year");
        QTest::newRow("stereo") << "Blade.Runner.2049.2017.3D.HSBS.BluRay.x264"
                                << "Blade Runner 2049" << 2017;
        QTest::newRow("bracket-stereo") << "Aquaman (2018) [3D] [HSBS]" << "Aquaman" << 2018;
        QTest::newRow("half-sbs") << "A.Film.2016.3D.Half-SBS.BRRip.x264" << "A Film" << 2016;
        QTest::newRow("max-is-title")
            << "Mad.Max.Fury.Road.2015.1080p.BluRay.x264" << "Mad Max Fury Road" << 2015;
        QTest::newRow("max-provider-qualified")
            << "Mad.Max.1979.MAX.WEB-DL.1080p" << "Mad Max" << 1979;
        QTest::newRow("max-title-before-source") << "Mad.Max.WEB-DL.1080p" << "Mad Max" << 0;
        QTest::newRow("numeric-word") << "Blade Runner 2049" << "Blade Runner 2049" << 0;
        QTest::newRow("future-title") << "Horizon 2077" << "Horizon 2077" << 0;
        QTest::newRow("numeric-film") << "1917 (2019)" << "1917" << 2019;
        QTest::newRow("decimal-film") << "2.0 (2018)" << "2 0" << 2018;
    }
    void releaseTitleAndYear()
    {
        QFETCH(QString, raw);
        QFETCH(QString, title);
        QFETCH(int, year);
        QCOMPARE(VaultKit::cleanMediaFolderTitle(raw), title);
        QCOMPARE(VaultKit::parsedTitleYear(raw), year);
    }
    void filenameTitleWinsOverFranchiseFolder()
    {
        QTemporaryDir d;
        auto db = catalog(d.filePath("catalog.sqlite"));
        VaultIndex index(d.filePath("index.sqlite"));
        auto r = row("D:/Videos/Franchise Casino Royale (2006)/Casino.Royale.2006.1080p.BluRay.mp4",
                     "Casino Royale", 2006);
        QVERIFY(index.publish({r}));
        ImdbCatalog c(db);
        VaultIdentifier id(&index, nullptr, nullptr, &c);
        auto m = id.matchGroup(r.groupKey);
        QVERIFY(m.adopted);
        QCOMPARE(m.sourceId, QString("imdb:tt1"));
    }
    void conflictingQualifiedYearsAbstain()
    {
        QTemporaryDir d;
        auto db = catalog(d.filePath("catalog.sqlite"));
        VaultIndex index(d.filePath("index.sqlite"));
        auto r = row("D:/Videos/Dune (1984)/Dune.2021.1080p.BluRay.mp4", "Dune", 2021);
        QVERIFY(index.publish({r}));
        ImdbCatalog c(db);
        VaultIdentifier id(&index, nullptr, nullptr, &c);
        QVERIFY(!id.matchGroup(r.groupKey).adopted);
    }
    void episodeEvidenceFiltersMovieCandidate()
    {
        QTemporaryDir d;
        auto db = catalog(d.filePath("catalog.sqlite"));
        VaultIndex index(d.filePath("index.sqlite"));
        auto r = row("D:/Videos/Clone Show/Season 1/Clone Show S01E01.mkv", "Clone Show");
        QVERIFY(index.publish({r}));
        ImdbCatalog c(db);
        VaultIdentifier id(&index, nullptr, nullptr, &c);
        auto m = id.matchGroup(r.groupKey);
        QVERIFY(m.adopted);
        QCOMPARE(m.sourceId, QString("imdb:tt5"));
    }
    void numericFilmAndSamplesStayIndependent()
    {
        QTemporaryDir d;
        touch(d.path(), "Movies/1917 (2019).mkv");
        touch(d.path(), "Movies/21 (2008).mkv");
        touch(d.path(), "Logan (2017)/Logan.2017.1080p.BluRay.mkv");
        touch(d.path(), "Logan (2017)/sample.avi");
        auto scan = VaultScanner::buildScan(d.path(), {}, 1, {});
        QCOMPARE(scan.rows.size(), 4);
        QString main, sample;
        for (const auto &r : scan.rows)
        {
            if (r.realName.startsWith("1917"))
                QCOMPARE(r.groupTitle, QString("1917"));
            if (r.realName.startsWith("21"))
                QCOMPARE(r.groupTitle, QString("21"));
            if (r.realName.startsWith("Logan"))
                main = r.groupKey;
            if (r.realName == "sample.avi")
                sample = r.groupKey;
        }
        QVERIFY(main != sample);
    }
    void contextualEpisodeFamiliesGroupWithoutGuessingMovies()
    {
        QTemporaryDir d;
        for (int n = 1; n <= 26; ++n)
            touch(d.path(),
                  QString("TV Shows/Quiet Show S1/%1 - Title %1.mkv").arg(n, 2, 10, QChar('0')));
        for (int n = 2; n <= 10; ++n)
            touch(d.path(),
                  QString("TV Shows/Another Show/%1 - Title %1.mkv").arg(n, 2, 10, QChar('0')));
        for (int n = 1; n <= 24; ++n)
            touch(d.path(),
                  QString("TV Shows/Comedy - Season 1/Comedy - Season 2/Episode %1 - %2.mp4")
                      .arg(n)
                      .arg(n == 21 ? "94 Meetings" : "Title"));
        for (int season = 1; season <= 2; ++season)
            for (int n = 1; n <= 6; ++n)
                touch(d.path(),
                      QString("TV Shows/Office (UK) Series 1 + 2/Office - %1.%2 - Title.avi")
                          .arg(season)
                          .arg(n));
        for (int n = 1; n <= 12; ++n)
            touch(d.path(), QString("TV Shows/Motion Story - Motion Comic 1-12/Motion Story Motion "
                                    "Comic - Chapter %1.avi")
                                .arg(n));
        auto scan = VaultScanner::buildScan(d.path(), {}, 1, {});
        QCOMPARE(scan.rows.size(), 83);
        QSet<QString> groups;
        for (const auto &r : scan.rows)
            groups.insert(r.groupKey);
        QCOMPARE(groups.size(), 5);
        for (const auto &group : groups)
        {
            const auto level = VaultKit::planBrowseLevel(group);
            QVERIFY(!level.isEmpty());
            for (const auto &node : level)
                QCOMPARE(node.nodeType, VaultKit::BrowseNodeType::Episode);
        }
        QStringList family;
        for (const auto &r : scan.rows)
            if (r.path.contains("/Comedy - Season 2/"))
                family.append(r.path);
        for (const auto &file : family)
        {
            auto context = VaultKit::videoEpisodeContext(file, family);
            QCOMPARE(context.number.season, 2);
            if (file.contains("Episode 21 -"))
                QCOMPARE(context.number.episode, 21);
        }
        touch(d.path(), "Movies/Collection/01 - First Film.mkv");
        touch(d.path(), "Movies/Collection/02 - Second Film.mkv");
        touch(d.path(), "Movies/Fan Edits/Episode 1 - A Film - Fan Edit.mp4");
        touch(d.path(), "Movies/Fan Edits/Episode 2 - Another Film - Fan Edit.mp4");
        touch(d.path(), "Movies/Star Wars Episode 5 - The Empire Strikes Back (1980).mp4");
        touch(d.path(), "Movies/2.0 (2018).mkv");
        scan = VaultScanner::buildScan(d.path(), {}, 2, {});
        QSet<QString> filmGroups;
        for (const auto &r : scan.rows)
            if (r.path.contains("/Movies/"))
                filmGroups.insert(r.groupKey);
        QCOMPARE(filmGroups.size(), 6);
    }
    void inheritedShowTitleDropsSeasonAndReleaseGroup()
    {
        QTemporaryDir d;
        touch(d.path(), "TV Shows/[Release] A Programme Season 1 [1080p]/S01E01 - Title.mkv");
        auto scan = VaultScanner::buildScan(d.path(), {}, 1, {});
        QCOMPARE(scan.rows.size(), 1);
        QCOMPARE(scan.rows.first().groupTitle, QString("A Programme"));
    }
    void aliasesDeduplicateCanonicalIdsAndKeepAmbiguity()
    {
        QTemporaryDir d;
        auto db = catalog(d.filePath("catalog.sqlite"), true);
        ImdbCatalog c(db);
        auto one = c.matchByTitle("Alternate Programme", 2005);
        QCOMPARE(one.size(), 1);
        QCOMPARE(one.first().toMap().value("tt").toString(), QString("tt7"));
        QCOMPARE(c.matchByTitle("Alternate Programme").size(), 2);
        QCOMPARE(c.matchByTitle("A Programme", 2005).size(), 1);
        QCOMPARE(c.matchByTitle("Alternate Programme", 1999).size(), 0);
    }
    void lateLocalCatalogueRetriesEligibleGroup()
    {
        QTemporaryDir d;
        auto db = catalog(d.filePath("catalog.sqlite"));
        VaultIndex index(d.filePath("index.sqlite"));
        auto r = row("D:/Videos/Casino Royale (2006)/Casino.Royale.2006.1080p.BluRay.mp4",
                     "Casino Royale", 2006);
        QVERIFY(index.publish({r}));
        ImdbCatalog c("");
        VaultIdentifier id(&index, nullptr, nullptr, &c);
        QVERIFY(!id.matchGroup(r.groupKey).adopted);
        QVERIFY(c.reopen(db));
        QTRY_COMPARE(index.rowsForGroup(r.groupKey).first().identityId, QString("imdb:tt1"));
    }

    void conflictingGroupYearsAndUnavailableMembersAbstain()
    {
        QTemporaryDir d;
        auto db = catalog(d.filePath("catalog.sqlite"));
        VaultIndex index(d.filePath("index.sqlite"));
        auto first = row("D:/Videos/Dune/Dune.1984.1080p.mkv", "Dune", 1984);
        auto second = row("D:/Videos/Dune/Dune.2021.1080p.mkv", "Dune", 2021);
        QVERIFY(index.publish({first, second}));
        ImdbCatalog c(db);
        VaultIdentifier id(&index, nullptr, nullptr, &c);
        QVERIFY(!id.matchGroup(first.groupKey).adopted);
        second = first;
        second.id = "second";
        second.path = "D:/Videos/Dune/copy.mkv";
        second.realName = "copy.mkv";
        second.away = true;
        QVERIFY(index.publish({first, second}));
        QVERIFY(!id.matchGroup(first.groupKey).adopted);
    }

    void watcherContextArrivalsKeepFileIdAndProgress()
    {
        QTemporaryDir d;
        const auto root = d.path() + "/library";
        const auto first = touch(root, "TV Shows/Quiet Show Season 1/01 - First.mkv");
        VaultIndex index(d.filePath("index.sqlite"));
        VaultIdentity identity(d.path());
        VaultScanner scanner(&index, &identity);
        auto generation = scanner.nextGeneration();
        scanner.applyPublish({VaultScanner::buildScan(root, {}, generation, {})}, generation);
        auto old = index.rowsForPath(first).first();
        old.progressed = true;
        QVERIFY(index.upsert(old));
        const auto second = touch(root, "TV Shows/Quiet Show Season 1/02 - Second.mkv");
        VaultWatcher watcher(&index, &identity, nullptr);
        QVERIFY(watcher.processRoot(root, {}, {}).landedCount > 0);
        auto now = index.rowsForPath(first).first();
        QCOMPARE(now.id, old.id);
        QVERIFY(now.progressed);
        QCOMPARE(now.groupKey, index.rowsForPath(second).first().groupKey);
        QCOMPARE(now.groupTitle, QString("Quiet Show"));
        QCOMPARE(watcher.processRoot(root, {}, {}).landedCount, 0);
    }

    void sampleCannotAcquireMainFeatureIdentity()
    {
        QTemporaryDir d;
        const auto main = touch(d.path(), "Logan (2017)/Logan.2017.1080p.BluRay.mkv");
        const auto sample = touch(d.path(), "Logan (2017)/sample.avi");
        auto scan = VaultScanner::buildScan(d.path(), {}, 1, {});
        for (auto &r : scan.rows)
            r.id = "vault:" + r.path;
        VaultIndex index(d.filePath("index.sqlite"));
        QVERIFY(index.publish(scan.rows));
        auto db = catalog(d.filePath("catalog.sqlite"));
        ImdbCatalog c(db);
        VaultIdentifier id(&index, nullptr, nullptr, &c);
        QCOMPARE(id.autoIdentifyExisting(), 1);
        QCOMPARE(index.rowsForPath(main).first().identityId, QString("imdb:tt6"));
        QVERIFY(index.rowsForPath(sample).first().identityId.isEmpty());
    }

    void numberedCollectionInsideTvContainerIsNotAShow()
    {
        QStringList files = {"D:/TV Shows/Collection/01 - A Film.mkv",
                             "D:/TV Shows/Collection/02 - Another Film.mkv"};
        for (const auto &file : files)
            QVERIFY(!VaultKit::videoEpisodeContext(file, files).number.matched);
    }

    void explicitSamplesArePhysicalExtrasNotAdditionalFeatures()
    {
        QTemporaryDir d;
        const auto main = touch(d.path(), "A Film (2017)/A.Film.2017.1080p.BluRay.mkv");
        touch(d.path(), "A Film (2017)/sample.avi");
        touch(d.path(), "A Film (2017)/Sample/A.Film.2017.720p.Sample.mkv");
        auto facts = VaultKit::describeFilmFolder(QFileInfo(main).absolutePath(), main);
        QCOMPARE(facts.extras.size(), 2);
        auto nodes = VaultKit::planBrowseLevel(d.path());
        QCOMPARE(nodes.size(), 1);
        QCOMPARE(nodes.first().nodeType, VaultKit::BrowseNodeType::Film);
        QCOMPARE(nodes.first().mediaCount, 1);
    }

    // ---- 2026-11-16: release noise, exact subtitle, numeral equivalence --------------------
    void releaseNoiseCleaning_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("title");
        QTest::addColumn<int>("year");
        QTest::newRow("sanet-underscores")
            << "www.SANET.ST____Avengers.Infinity.War.2018.3D.1080p.BluRay.x264-iM@X"
            << "Avengers Infinity War" << 2018;
        QTest::newRow("sanet-dash")
            << "www.SANET.ST  -  Ready.Player.One.2018.3D.1080p.BluRay.x264-iM@X"
            << "Ready Player One" << 2018;
        QTest::newRow("domain-dash-prefix")
            << "Crazy4TV.com - Spider-Man.Homecoming.2017.1080p.BluRay.x265-HETeam"
            << "Spider-Man Homecoming" << 2017;
        QTest::newRow("bracket-domain-prefix")
            << "[TorrentCounter.to] Ready Player One 2018 1080p BluRay x264" << "Ready Player One" << 2018;
        QTest::newRow("dual-audio-language-list")
            << "Princess.Mononoke.1080p.DualAudio.5.1.English.Japanese.BDrip.[KoTuWa]"
            << "Princess Mononoke" << 0;
        QTest::newRow("fansub-group-and-triple-audio")
            << "[AnimeRG] Summer Wars [Triple-Audio] [1080p] [x265] [pseudo]" << "Summer Wars" << 0;
        QTest::newRow("glued-resolution-and-size")
            << "The Lion King 3D (1994) 1080pBrRip HSBS x264 - 1.3GB - YIFY" << "The Lion King" << 1994;
        QTest::newRow("size-after-year")
            << "Rogue One 2016 720p BRRip 950 MB - iExTV" << "Rogue One" << 2016;
        QTest::newRow("group-token-after-quality")
            << "KILL.BILL.The.Whole.Bloody.Affair.720p.BluRay.x264.anoXmous"
            << "KILL BILL The Whole Bloody Affair" << 0;
        QTest::newRow("anniversary-edition")
            << "The.Wizard.Of.Oz.1939.75th.Anniversary.Edition.1080p.BluRay.x264.anoXmous"
            << "The Wizard Of Oz" << 1939;
        QTest::newRow("dc-after-year")
            << "Terminator.2.Judgment.Day.1991.DC.1080p.BRrip.x264.GAZ.YIFY"
            << "Terminator 2 Judgment Day" << 1991;
        QTest::newRow("remastered-extended-after-year")
            << "Terminator.2.Judgment.Day.1991.REMASTERED.EXTENDED.1080p.BluRay.10bit.HEVC.6CH-MkvCage.ws"
            << "Terminator 2 Judgment Day" << 1991;
        QTest::newRow("despecialized")
            << "Return.Of.The.Jedi.1983.Despecialized.720p.x264.AC3.5.1" << "Return Of The Jedi" << 1983;
        QTest::newRow("criterion")
            << "Dr.Strangelove.1964.Criterion.1080p.BluRay.x264.EAC3-SARTRE" << "Dr Strangelove" << 1964;
        QTest::newRow("uncensored")
            << "The.Godfather-A.Novel.for.Television.1977.UNCENSORED.EXTENDED.720p.HDTV.x264-BATV"
            << "The Godfather-A Novel for Television" << 1977;
        // Real title words survive: the edition words are only noise in the release suffix.
        QTest::newRow("extended-family-is-a-title") << "The Extended Family (2008)" << "The Extended Family" << 2008;
        QTest::newRow("directors-cut-is-a-title") << "The Director's Cut (2012)" << "The Director's Cut" << 2012;
        QTest::newRow("justice-league-dark")
            << "Justice.League.Dark.2017.HDRip.XviD.AC3-EVO" << "Justice League Dark" << 2017;
        QTest::newRow("rec-bracket-title") << "[REC] 2007 1080p BluRay x264" << "REC" << 2007;
    }
    void releaseNoiseCleaning()
    {
        QFETCH(QString, raw);
        QFETCH(QString, title);
        QFETCH(int, year);
        QCOMPARE(VaultKit::cleanMediaFolderTitle(raw), title);
        QCOMPARE(VaultKit::parsedTitleYear(raw), year);
    }

    void formerlyUnmatchedFilesAdoptTheRightTitle_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("tt");
        const QString v = "D:/Videos/";
        auto add = [&](const char *name, const QString &rel, const char *tt)
        { QTest::newRow(name) << v + rel << QString(tt); };
        add("rogue-one-release", "Adventure/Rogue One 2016 720p BRRip 950 MB - iExTV/Rogue One 2016 720p BRRip 950 MB - iExTV.mp4", "imdb:ttRogueOne");
        add("strangelove-anoxmous", "Dr.Strangelove.1964.720p.BluRay.x264.anoXmous/Dr.Strangelove.1964.720p.BluRay.x264.anoXmous_.mp4", "imdb:ttStrangelove");
        add("strangelove-criterion", "Dr.Strangelove.1964.Criterion.1080p.BluRay.x264.EAC3-SARTRE/Dr.Strangelove.1964.Criterion.1080p.BluRay.x264.EAC3-SARTRE.mkv", "imdb:ttStrangelove");
        add("godfather-part-3-release", "The Godfather Part 3 (1990) [1080p]/The.Godfather.Part.3.1990.1080p.BrRip.x264.BOKUTOX.YIFY.mp4", "imdb:ttGodfather3");
        add("sanet-infinity-war", "Avengers.Infinity.War.2018.3D.1080p.BluRay.x264-iM@X/www.SANET.ST____Avengers.Infinity.War.2018.3D.1080p.BluRay.x264-iM@X.mkv", "imdb:ttInfinityWar");
        add("sanet-ready-player-one", "Ready.Player.One.2018.3D.1080p.BluRay.x264-iM@X/www.SANET.ST  -  Ready.Player.One.2018.3D.1080p.BluRay.x264-iM@X.mkv", "imdb:ttReadyPlayerOne");
        add("crazy4tv-homecoming", "Spider-Man.Homecoming.2017.1080p.6CH.BluRay.x265-HETeam/Crazy4TV.com - Spider-Man.Homecoming.2017.1080p.BluRay.x265-HETeam.mkv", "imdb:ttHomecoming");
        add("torrentcounter-prefix", "[TorrentCounter.to] Ready Player One 2018 1080p BluRay x264.mkv", "imdb:ttReadyPlayerOne");
        add("princess-mononoke", "Princess.Mononoke.1080p.DualAudio.5.1.English.Japanese.6ch.hime.OST.Extras.BRrip.BDrip.[KoTuWa]/Princess.Mononoke.1080p.DualAudio.5.1.English.Japanese.BDrip.[KoTuWa].mkv", "imdb:ttMononoke");
        add("summer-wars-animerg", "Summer Wars (2009) [MULTI-AUDIO] [1080p] [BD] [x265] [pseudo]/[AnimeRG] Summer Wars [Triple-Audio] [1080p] [x265] [pseudo].mkv", "imdb:ttSummerWars");
        add("lion-king-3d", "The Lion King (1994) [1080p] [3D] [HSBS]/The Lion King 3D (1994) 1080pBrRip HSBS x264 - 1.3GB - YIFY.mp4", "imdb:ttLionKing");
        add("kill-bill-affair", "Tarantino/KILL.BILL.The.Whole.Bloody.Affair.720p.BluRay.x264.anoXmous.mp4", "imdb:ttKillBillWBA");
        add("wizard-of-oz-anniversary", "The.Wizard.Of.Oz.1939.75th.Anniversary.Edition.1080p.BluRay.x264.anoXmous/The.Wizard.Of.Oz.1939.75th.Anniversary.Edition.1080p.BluRay.x264.anoXmous_.mp4", "imdb:ttOz");
        add("terminator-2-dc", "Terminator 2 Judgment Day DC (1991) [1080p]/Terminator.2.Judgment.Day.1991.DC.1080p.BRrip.x264.GAZ.YIFY.mp4", "imdb:ttTerminator2");
        add("terminator-2-remastered", "Sci-Fi/Terminator.2.Judgment.Day.1991.REMASTERED.EXTENDED.1080p.BluRay.10bit.HEVC.6CH-MkvCage.ws.mkv", "imdb:ttTerminator2");
        add("batman-exact-beats-subtitle-rows", "Batman (1989).mp4", "imdb:ttBatman1989");
        add("roman-catalogue-arabic-file", "Fixture Saga 4 (2006).mp4", "imdb:ttSagaIV");
        add("arabic-catalogue-roman-file", "Fixture Saga II (2005).mp4", "imdb:ttSaga2");
        add("extended-family-keeps-its-word", "The Extended Family (2008).mp4", "imdb:ttExtendedFamily");
    }
    void formerlyUnmatchedFilesAdoptTheRightTitle()
    {
        QFETCH(QString, path);
        QFETCH(QString, tt);
        QTemporaryDir d;
        const auto db = noiseCatalog(d.filePath("catalog.sqlite"));
        QVERIFY(!db.isEmpty());
        const auto r = identifyPath(db, path);
        QVERIFY2(r.adopted, qPrintable(QString("title='%1' year=%2 candidates=%3")
                                           .arg(r.groupTitle).arg(r.parsedYear).arg(r.candidates)));
        QCOMPARE(r.sourceId, tt);
    }

    void mustStayUnmatched_data()
    {
        QTest::addColumn<QString>("path");
        const QString v = "D:/Videos/";
        auto add = [&](const char *name, const QString &rel)
        { QTest::newRow(name) << v + rel; };
        add("blade-runner-final-cut-year-disagrees", "Blade.Runner (1997)/Blade.Runner.Final.Cut.1997.720p.BluRay.x264.YIFY.mp4");
        add("wonder-woman-2009-year-disagrees", "Wonder Woman Individual/Wonder Woman (2009).mp4");
        add("iron-man-no-year-is-ambiguous", "MCU/Iron Man Series/Iron Man 1.mp4");
        add("inglorious-typo-is-not-fuzzy-matched", "Inglorious.Basterds.2009.720p.BDRip.XviD.AC3-RARBG/Inglorious.Basterds.2009.720p.BRRip.XviD.AC3-RARBG.avi");
        add("etrg-promo", "Get.Out.2017.HDRip.XViD-ETRG/ETRG.mp4");
        add("rarbg-promo", "Inception.2010.1080p.BluRay.H264.AAC-RARBG/RARBG.com.mp4");
        add("fan-edit", "Star Wars Prequels - The Anti Cheese Edits/Epidsode 1 - Phantom Menace - Anti-Cheese Edit.mp4");
        add("camera-clip", "Family PenDrive/phone/DCIM/Camera/VID_20150920_212519.mp4");
        add("whatsapp-clip", "Family PenDrive/phone/WhatsApp/WhatsApp Video/VID-20150826-WA0001.mp4");
        add("pendrive-talk", "Family PenDrive/phone/Karma and Memory  Sadhguru-cao.mp4");
        add("pendrive-song", "Family PenDrive/phone/Neelimabbu Yesudas.wmv-cao.mp4");
        // Prefix rule guards: year-less queries and mismatched or doubled years never adopt.
        add("subtitle-prefix-needs-a-year", "Rogue One.mp4");
        add("subtitle-prefix-year-must-agree", "Rogue One 2017.mp4");
        add("subtitle-prefix-two-qualifying-rows", "Foo Bar 2001.mp4");
        add("subtitle-prefix-not-mid-title", "Phantom Menace 1999.mp4");
        add("subtitle-prefix-not-word-fragment", "Rogue 2016.mp4");
    }
    void mustStayUnmatched()
    {
        QFETCH(QString, path);
        QTemporaryDir d;
        const auto db = noiseCatalog(d.filePath("catalog.sqlite"));
        QVERIFY(!db.isEmpty());
        const auto r = identifyPath(db, path);
        QVERIFY2(!r.adopted, qPrintable(QString("adopted %1 (title='%2')").arg(r.sourceId, r.groupTitle)));
    }

    void subtitlePrefixIsExactAndYearBound()
    {
        QTemporaryDir d;
        const auto db = noiseCatalog(d.filePath("catalog.sqlite"));
        ImdbCatalog c(db);
        // The exact query wins whenever it has a candidate; the subtitle rule only runs on zero.
        QCOMPARE(c.matchByTitle("Batman", 1989).size(), 1);
        QCOMPARE(c.matchByTitle("Batman", 1989).first().toMap().value("tt").toString(), QString("ttBatman1989"));
        QCOMPARE(c.matchByTitle("Batman", 1993).size(), 1); // the one "Batman: ..." row of that year
        QCOMPARE(c.matchByTitle("Batman", 1990).size(), 0);
        QCOMPARE(c.matchByTitle("Batman").size(), 1);        // exact only; the prefix rule never fires without a year
        QCOMPARE(c.matchByTitle("Rogue One").size(), 0);
        QCOMPARE(c.matchByTitle("Rogue One", 2016).size(), 1);
        QCOMPARE(c.matchByTitle("Foo Bar", 2001).size(), 0); // two qualifying rows: abstain
        QCOMPARE(c.matchByTitle("Foo", 2001).size(), 0);     // "foo bar" is not the part before the colon
        QCOMPARE(c.matchByTitle("Dr Strangelove", 1964).size(), 1);
    }
    void numeralEquivalenceIsOneToTenOnly()
    {
        QTemporaryDir d;
        const auto db = noiseCatalog(d.filePath("catalog.sqlite"));
        ImdbCatalog c(db);
        QCOMPARE(c.matchByTitle("The Godfather Part 3", 1990).size(), 1);
        QCOMPARE(c.matchByTitle("The Godfather Part III", 1990).size(), 1);
        QCOMPARE(c.matchByTitle("Godfather Part 3", 1990).size(), 1);
        QCOMPARE(c.matchByTitle("Godfather Part 3", 1991).size(), 0);
        QCOMPARE(c.matchByTitle("Fixture Saga IV", 2006).size(), 1);
        QCOMPARE(c.matchByTitle("Fixture Saga 4", 2006).size(), 1);
        QCOMPARE(c.matchByTitle("Fixture Saga 11", 2006).size(), 0);
    }

    // ---- the content-API (remote) branch runs the same three steps as the local one -------------
    void remoteBranchRunsTheSameThreeSteps()
    {
        QTemporaryDir d;
        const auto path = noiseCatalog(d.filePath("catalog.sqlite"));
        QStringList calls;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "fake_worker_db");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            ImdbCatalog c{QString()}; // no local db: matchByTitle takes the remote branch
            c.setRemoteReaderForTest([&](const QString &m, const QVariantList &a)
                                     { return fakeWorker(db, calls, m, a); });
            auto tt = [](const QVariantList &rows)
            { return rows.size() == 1 ? rows.first().toMap().value("tt").toString() : QString("n=%1").arg(rows.size()); };

            // exact hit: one call, nothing else
            QCOMPARE(tt(c.matchByTitle("Batman", 1989)), QString("ttBatman1989"));
            QCOMPARE(calls, QStringList({"matchByTitle:batman"}));

            // numeral variant, no prefix search needed
            calls.clear();
            QCOMPARE(tt(c.matchByTitle("The Godfather Part 3", 1990)), QString("ttGodfather3"));
            QCOMPARE(calls, QStringList({"matchByTitle:godfather part 3", "matchByTitle:godfather part iii"}));

            // exact subtitle through the existing search method, year filter and head check in C++
            calls.clear();
            QCOMPARE(tt(c.matchByTitle("Rogue One", 2016)), QString("ttRogueOne"));
            QCOMPARE(calls, QStringList({"matchByTitle:rogue one", "search:rogue one"}));
            QCOMPARE(tt(c.matchByTitle("Dr Strangelove", 1964)), QString("ttStrangelove"));

            // never without a year, never for a wrong year, never for two qualifying titles
            calls.clear();
            QCOMPARE(c.matchByTitle("Rogue One").size(), 0);
            QCOMPARE(calls, QStringList({"matchByTitle:rogue one"}));
            QCOMPARE(c.matchByTitle("Rogue One", 2017).size(), 0);
            QCOMPARE(c.matchByTitle("Foo Bar", 2001).size(), 0);
            QCOMPARE(c.matchByTitle("Phantom Menace", 1999).size(), 0); // mid-title is not a subtitle head

            // a prefix answer that fills the limit is not provable: zero
            c.setRemoteReaderForTest([&](const QString &m, const QVariantList &a)
                                     {
                                         if (m != "search")
                                             return fakeWorker(db, calls, m, a);
                                         QVariantList flood;
                                         for (int i = 0; i < a.value(1).toInt(); ++i)
                                             flood << QVariantMap{{"tt", QString("ttF%1").arg(i)}, {"title", i == 0 ? "Rogue One: A Star Wars Story" : "Rogue One Filler"}, {"year", 2016}};
                                         return QVariant(flood);
                                     });
            QCOMPARE(c.matchByTitle("Rogue One", 2016).size(), 0);
            db.close();
        }
        QSqlDatabase::removeDatabase("fake_worker_db");
    }

    // ---- slice 2 (2026-11-16): show descriptors, folder-title fallback, release noise, extras, reprobe ----
    void slice2NoiseCleaning_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("title");
        QTest::addColumn<int>("year");
        QTest::newRow("year-range-folder") << "X-Men The Animated Series (1992 - 1997)" << "X-Men The Animated Series" << 1992;
        QTest::newRow("open-year-range") << "Some Show (2008-)" << "Some Show" << 2008;
        QTest::newRow("new-proper-hdcam-hq")
            << "Avengers Infinity War 2018 NEW PROPER HD-CAM X264 HQ-CPG" << "Avengers Infinity War" << 2018;
        QTest::newRow("hdts-after-year") << "Some Film 2015 HDTS x264" << "Some Film" << 2015;
        QTest::newRow("cam-after-year") << "Some Film 2015 CAM XviD" << "Some Film" << 2015;
        // title words survive when they are not in release position after the year
        QTest::newRow("new-in-title") << "The New Frontier (2008)" << "The New Frontier" << 2008;
        QTest::newRow("proper-in-title") << "The Proper Gentleman 2010 DVDRip" << "The Proper Gentleman" << 2010;
        QTest::newRow("cam-is-a-title") << "Cam (2018)" << "Cam" << 2018;
        QTest::newRow("ts-is-a-title") << "TS Madison (2016)" << "TS Madison" << 2016;
    }
    void slice2NoiseCleaning()
    {
        QFETCH(QString, raw);
        QFETCH(QString, title);
        QFETCH(int, year);
        QCOMPARE(VaultKit::cleanMediaFolderTitle(raw), title);
        QCOMPARE(VaultKit::parsedTitleYear(raw), year);
    }

    void slice2AdoptionsAndGuards_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QStringList>("others");
        QTest::addColumn<QString>("tt"); // empty = must stay unmatched
        const QString v = "D:/Videos/";
        auto row = [&](const char *name, const QString &rel, const QStringList &others, const QString &tt)
        {
            QStringList o;
            for (const auto &x : others)
                o << v + x;
            QTest::newRow(name) << v + rel << o << tt;
        };
        // show folders
        row("xmen-year-range-and-descriptor",
            "TV Shows/Cartoons/X-Men The Animated Series (1992 - 1997)/S01E01 - Night of the Sentinels (Part 1).mkv", {}, "imdb:ttXMen1992");
        row("show-descriptor-needs-year", "TV Shows/Cartoons/X-Men The Animated Series/S01E01 - Night.mkv", {}, "");
        // folder-title fallback: single feature, folder carries the year
        row("folder-black-adam",
            "Superman.Shazam.The.Return.of.Black.Adam.2010.720p.BluRay.x264-aAF/aaf-sstroba.720p.mkv", {}, "imdb:ttShazam");
        row("folder-all-star",
            "All.Star.Superman.2011.720p.BRRip.XviD.AC3.SANTi/santi-allstarsuperman.brrip.xvid.avi", {}, "imdb:ttAllStar");
        row("folder-dark-knight-part-2",
            "Batman The Dark Knight Returns Part 2 (2013)/Batman.The.Dark.Knight.Part.2.2013.720p.BRrip.x264.GAZ.YIFY.mp4", {}, "imdb:ttTDKR2");
        row("folder-with-sample-beside",
            "All.Star.Superman.2011.720p.BRRip.XviD.AC3.SANTi/santi-allstarsuperman.brrip.xvid.avi",
            {"All.Star.Superman.2011.720p.BRRip.XviD.AC3.SANTi/sample.avi", "All.Star.Superman.2011.720p.BRRip.XviD.AC3.SANTi/Extras/Making Of.mkv"}, "imdb:ttAllStar");
        // guards
        row("folder-two-different-films",
            "All.Star.Superman.2011.720p.BRRip.XviD.AC3.SANTi/santi-allstarsuperman.brrip.xvid.avi",
            {"All.Star.Superman.2011.720p.BRRip.XviD.AC3.SANTi/Other.Unrelated.Picture.avi"}, "");
        row("folder-without-year", "All.Star.Superman.720p.BRRip.XviD.AC3.SANTi/santi-allstarsuperman.brrip.xvid.avi", {}, "");
        row("folder-year-disagrees-with-filename",
            "All.Star.Superman.2011.720p.BRRip.XviD.AC3.SANTi/santi-allstarsuperman.2012.brrip.xvid.avi", {}, "");
        row("generic-folder", "Movies 2010/aaf-xyz.720p.mkv", {}, "");
        row("etrg-promo-never-takes-folder-identity", "Get.Out.2017.HDRip.XViD-ETRG/ETRG.mp4", {}, "");
        row("rarbg-promo-never-takes-folder-identity", "Inception.2010.1080p.BluRay.H264.AAC-RARBG/RARBG.com.mp4", {}, "");
        row("sample-never-takes-folder-identity", "All.Star.Superman.2011.720p.BRRip.XviD.AC3.SANTi/sample.avi", {}, "");
    }
    void slice2AdoptionsAndGuards()
    {
        QFETCH(QString, path);
        QFETCH(QStringList, others);
        QFETCH(QString, tt);
        QTemporaryDir d;
        const auto db = noiseCatalog(d.filePath("catalog.sqlite"));
        QVERIFY(!db.isEmpty());
        QStringList all = others;
        all.prepend(path);
        VaultIndex index(d.filePath("index.sqlite"));
        QList<VaultIndex::FileRow> rows;
        const bool mixed = VaultScanner::hasMixedVideoTitles(all);
        QString targetGroup;
        for (const auto &p : all)
        {
            VaultIndex::FileRow r;
            r.id = "vault:" + p;
            r.kind = "video";
            r.path = p;
            r.realName = QFileInfo(p).fileName();
            r.subtreePath = QFileInfo(p).absolutePath();
            r.rootPath = "D:/Videos";
            r.size = 100;
            r.mtimeMs = 1;
            VaultScanner::assignVideoGroup(r, false, mixed, all);
            if (p == path)
                targetGroup = r.groupKey;
            rows.append(r);
        }
        QVERIFY(index.publish(rows));
        ImdbCatalog c(db);
        VaultIdentifier id(&index, nullptr, nullptr, &c);
        const auto m = id.matchGroup(targetGroup);
        if (tt.isEmpty())
            QVERIFY2(!m.adopted, qPrintable("adopted " + m.sourceId));
        else
        {
            QVERIFY2(m.adopted, qPrintable(QString("candidates=%1").arg(m.candidateCount)));
            QCOMPARE(m.sourceId, tt);
        }
    }

    void slice2PhysicalExtrasParents()
    {
        using R = VaultIndex::FileRow;
        auto mk = [](const QString &path, const QString &identity = QString())
        {
            R r;
            r.id = "vault:" + path;
            r.kind = "video";
            r.path = path;
            r.identityId = identity;
            r.identityTitle = identity == "imdb:ttSop" ? "The Sopranos"
                : identity == "imdb:ttStrangelove" ? "Dr. Strangelove or: How I Learned to Stop Worrying and Love the Bomb"
                : identity == "imdb:ttParks" ? "Parks and Recreation" : identity;
            return r;
        };
        const QString sop = "D:/TV/The Sopranos - The Complete Series (Season 1, 2, 3, 4, 5 & 6) + Extras";
        const QString film = "D:/Movies/Dr.Strangelove.1964.Criterion.1080p.BluRay.x264.EAC3-SARTRE";
        const auto rows = QList<R>{
            mk(sop + "/Season 1/S01E01.mkv", "imdb:ttSop"), mk(sop + "/Season 2/S02E01.mkv", "imdb:ttSop"),
            mk(sop + "/The Complete Series Bonus + Extras/Inside The Sopranos.mp4"),
            mk(sop + "/The Complete Series Bonus + Extras/Sopranos Tour.mp4"),
            mk(film + "/Dr.Strangelove.1964.mkv", "imdb:ttStrangelove"),
            mk(film + "/Extras/Inside Dr. Strangelove.mkv"),
            mk(film + "/Behind the Scenes/Making Of.mkv"),
            // none of these may be filed:
            mk("D:/Loose/Extras/Orphan Extra.mkv"),                       // no identified ancestor
            mk(film + "/Another Feature.mkv"),                            // not in an extras folder
            mk("D:/Two/Extras/Bonus.mkv"), mk("D:/Two/A/Film A.mkv", "imdb:ttA"), mk("D:/Two/B/Film B.mkv", "imdb:ttB"),
            mk(film + "/Extras/Identified Extra.mkv", "imdb:ttOther"),      // already identified
            // a collection folder holding another show: not that show's extras
            mk("D:/Sitcoms/Parks and Recreation/S01E01.mkv", "imdb:ttParks"),
            mk("D:/Sitcoms/The Office (UK) Series 1 + 2 Christmas Specials And Extras/The Office - 1.1.avi"),
            // samples keep their own behaviour
            mk(film + "/Sample/Sample.mkv")};
        const auto parents = VaultIdentifier::physicalExtrasParents(rows);
        QCOMPARE(parents.value("vault:" + sop + "/The Complete Series Bonus + Extras/Inside The Sopranos.mp4"), QString("imdb:ttSop"));
        QCOMPARE(parents.value("vault:" + sop + "/The Complete Series Bonus + Extras/Sopranos Tour.mp4"), QString("imdb:ttSop"));
        QCOMPARE(parents.value("vault:" + film + "/Extras/Inside Dr. Strangelove.mkv"), QString("imdb:ttStrangelove"));
        QCOMPARE(parents.value("vault:" + film + "/Behind the Scenes/Making Of.mkv"), QString("imdb:ttStrangelove"));
        QCOMPARE(parents.size(), 4);
    }

    void slice2RejectedRowsAreReprobedOncePerSession()
    {
        QTemporaryDir d;
        VaultIndex index(d.filePath("index.sqlite"));
        auto make = [](const QString &id, const QString &verdict)
        {
            VaultIndex::FileRow r;
            r.id = id;
            r.kind = "video";
            r.path = "D:/Videos/" + id + ".mkv";
            r.realName = id + ".mkv";
            r.groupKey = r.subtreePath = "D:/Videos";
            r.groupTitle = id;
            r.rootPath = "D:/Videos";
            r.size = 100;
            r.mtimeMs = 1;
            r.admissionVerdict = verdict;
            if (!verdict.isEmpty() && verdict != "Admitted")
            {
                r.admissionDetail = "no audio or video data played";
                r.errorState = "rejected";
                r.errorDetail = r.admissionDetail;
            }
            return r;
        };
        const QString rejected = "vault:reprobe-rejected-fixture", admitted = "vault:reprobe-admitted-fixture";
        QVERIFY(index.publish({make(rejected, "RejectedError"), make(admitted, "Admitted")}));
        // a rescan: fresh rows carry no verdict; size and mtime are unchanged
        QVERIFY(index.publish({make(rejected, QString()), make(admitted, QString())}));
        QCOMPARE(index.rowsForPath("D:/Videos/" + rejected + ".mkv").first().admissionVerdict, QString()); // re-probe pending
        QVERIFY(index.rowsForPath("D:/Videos/" + rejected + ".mkv").first().errorState.isEmpty());
        QCOMPARE(index.rowsForPath("D:/Videos/" + admitted + ".mkv").first().admissionVerdict, QString("Admitted")); // carried
        // the enricher probes again and the file is still bad: recorded as before
        QVERIFY(index.upsertMany({make(rejected, "RejectedError")}));
        // a second rescan in the same session leaves it alone (no probing loop)
        QVERIFY(index.publish({make(rejected, QString()), make(admitted, QString())}));
        const auto again = index.rowsForPath("D:/Videos/" + rejected + ".mkv").first();
        QCOMPARE(again.admissionVerdict, QString("RejectedError"));
        QCOMPARE(again.errorState, QString("rejected"));
        // a changed file is unaffected by this rule (size/mtime change already drops the carry)
    }
};
QTEST_GUILESS_MAIN(VideoIdentityTest)
#include "tst_vault_video_identity.moc"
