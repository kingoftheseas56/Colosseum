#include <QtTest>

#include "engine/ComicsCatalog.h"
#include "engine/ImdbCatalog.h"
#include "engine/MalCatalog.h"
#include "engine/VaultIdentifier.h"
#include "engine/VaultIndex.h"
#include "engine/VaultKit.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

namespace {

bool execSql(QSqlQuery& query, const QString& sql)
{
    query.prepare(sql);
    return query.exec();
}

// Vault ux uplift S16 — the remake fixture: TWO Dune rows, 1984 and 2021, sharing the
// normalized title (the exact shape that renders the cleaned title "Dune" ambiguous, and the
// kind/table shape ImdbCatalog::matchByTitle expects — the tst_vault_imdb_match schema).
bool buildImdbDuneFixture(const QString& path)
{
    const QString connection = QStringLiteral("vault_identifier_imdb_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery q(db);
            ok = execSql(q, QStringLiteral(
                "CREATE TABLE title ("
                "tt TEXT PRIMARY KEY, type TEXT, title TEXT, "
                "norm_title TEXT NOT NULL, year INTEGER, endYear INTEGER, runtimeMin INTEGER, "
                "genres TEXT, rating REAL, votes INTEGER, episodes INTEGER, "
                "origLang TEXT, isAnime INTEGER)"));
            if (ok) {
                ok = execSql(q, QStringLiteral(
                    "INSERT INTO title VALUES "
                    "('tt0000001','movie','Dune','dune',1984,0,137,'[\"Sci-Fi\"]',7.1,6000,0,'en',0),"
                    "('tt0000002','movie','Dune','dune',2021,0,155,'[\"Sci-Fi\",\"Adventure\"]',8.1,30000,0,'en',0),"
                    "('tt0062622','movie','2001: A Space Odyssey','%1',1968,0,149,'[\"Sci-Fi\"]',8.3,30000,0,'en',0)")
                    .arg(VaultKit::normalizedTitle(QStringLiteral("2001: A Space Odyssey"))));
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

bool buildMalFixture(const QString& path, bool ambiguous)
{
    const QString connection = QStringLiteral("vault_identifier_mal_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery q(db);
            ok = execSql(q, QStringLiteral(
                "CREATE TABLE anime ("
                "mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT, "
                "norm_title TEXT NOT NULL, norm_title_english TEXT NOT NULL, "
                "type TEXT, score REAL, year INTEGER, cover TEXT, synopsis TEXT, "
                "credits TEXT, tags TEXT, episodes INTEGER)"));
            if (ok) {
                // norm_title is 'matrix', NOT 'the matrix' — VaultKit::normalizedTitle() strips a
                // leading "the"/"a"/"an" article (VaultIdentifier's own lookup path), so a fixture
                // row storing the un-stripped article would silently never match ANY real lookup
                // title and both rows would look like zero candidates, not two (a pre-existing
                // vacuous-fixture gap fixed here so the ambiguity path is genuinely exercised).
                const QString rows = ambiguous
                    ? QStringLiteral(
                        "INSERT INTO anime VALUES "
                        "(1,'The Matrix','','matrix','', 'TV',8.0,1999,'cover-1','synopsis-1','[]','[]',1),"
                        "(2,'The Matrix','','matrix','', 'TV',7.0,2003,'cover-2','synopsis-2','[]','[]',1)")
                    : QStringLiteral(
                        "INSERT INTO anime VALUES "
                        "(3,'Cowboy Bebop','','cowboy bebop','', 'TV',8.8,1998,'cover-3','synopsis-3','[]','[]',26)");
                ok = execSql(q, rows);
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

VaultIndex::FileRow fixtureRow(const QString& id, const QString& title)
{
    VaultIndex::FileRow row;
    row.id = id;
    row.rootPath = QStringLiteral("D:/vault");
    row.subtreePath = QStringLiteral("D:/vault/") + title;
    row.groupKey = row.subtreePath;
    row.groupTitle = title;
    row.kind = QStringLiteral("comic");
    row.path = row.subtreePath + QStringLiteral("/Vol 1.cbz");
    row.displayTitle = QStringLiteral("Vol 1");
    row.realName = QStringLiteral("Vol 1.cbz");
    row.size = 100;
    row.mtimeMs = 1;
    return row;
}

} // namespace

class VaultIdentifierTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void unambiguousMatchAdopts();
    void twoCandidatesStayUnidentified();
    void manualChosenCandidateWinsAndPreservesProgress();
    void incoherentGroupTitlesAreRefused();
    void coherentSeriesAllowsDifferentDisplayTitles();
    void absentDatabaseNoOpsHonestly();
    void autoPassIdentifiesOnlyCertainEligibleGroups();
    void unidentifyPreservesProgressAndFileId();
    void reshelveChangesKindWithoutChangingFileId();
    // ── browse-face execution plan, Slice 2: durable identityState ──
    void identityStateRecordsAdoptedAmbiguousNoneAndSuppressed();
    // ── Vault ux uplift S16: the census's parsed year disambiguates remakes ──
    void duneRemakeYearAdoptsTheRightRow();
    void yearlessDuneStaysHonestlyAmbiguous();
    void numericTitleKeepsItsLeadingYearAndMatchesReleaseYear();
    void bookMatchRequiresActualEmbeddedTitleInsteadOfFilenameFallback();
    void conflictingBookTitlesOrUnprovenGroupMembersRefuseAutoMatch();
    void manualPdfMatchSurvivesAutoPassAndExplicitSuppressionSemantics();
    void manualFlatShowTypePersistsWithoutCatalogueAndRejectsUnknownType();

private:
    QTemporaryDir m_dir;
    QString m_malPath;
    QString m_ambiguousMalPath;
    QString m_imdbDunePath;
};

void VaultIdentifierTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_malPath = m_dir.filePath(QStringLiteral("mal.sqlite"));
    m_ambiguousMalPath = m_dir.filePath(QStringLiteral("mal-ambiguous.sqlite"));
    QVERIFY(buildMalFixture(m_malPath, false));
    QVERIFY(buildMalFixture(m_ambiguousMalPath, true));
    m_imdbDunePath = m_dir.filePath(QStringLiteral("imdb-dune.sqlite"));
    QVERIFY(buildImdbDuneFixture(m_imdbDunePath));
}

// Vault ux uplift S16 — the cleaner's year find wins the catalog's own year filter. The group
// title is the ALREADY-CLEANED "Dune" (the raw folder "Dune (2021)" was stripped by
// cleanMediaFolderTitle; row.parsedYear = 2021 is the find it never exposed). Two catalogue
// Dunes exist (1984 + 2021); without the year both are candidates and the gate abstains.
void VaultIdentifierTest::duneRemakeYearAdoptsTheRightRow()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    auto row = fixtureRow(QStringLiteral("vault:dune-remake"), QStringLiteral("Dune"));
    row.kind = QStringLiteral("video");
    row.parsedYear = 2021; // the census captured the year the cleaner stripped
    QVERIFY(index.publish({row}));
    ImdbCatalog imdb(m_imdbDunePath);
    QVERIFY(imdb.ready());
    VaultIdentifier identifier(&index, nullptr, nullptr, &imdb);

    const VaultIdentifier::Match match = identifier.matchGroup(row.groupKey);
    QVERIFY2(match.adopted, "the year filter must narrow exactly the one Dune");
    QCOMPARE(match.source, QStringLiteral("IMDB"));
    QCOMPARE(match.sourceId, QStringLiteral("imdb:tt0000002")); // the 2021 Dune
    QCOMPARE(match.year, 2021);
    QCOMPARE(match.type, QStringLiteral("movie"));
    // Phase-4 G1: the adopted IMDb facts ride the match AND the group's rows (applyGroup).
    QCOMPARE(match.rating, 8.1);
    QCOMPARE(match.genres, QStringLiteral("Sci-Fi · Adventure"));

    // The persistence seam: applyGroup writes the facts onto every row of the group.
    QVERIFY(identifier.applyGroup(row.groupKey, match));
    const QList<VaultIndex::FileRow> adopted = index.rowsForGroup(row.groupKey);
    QCOMPARE(adopted.size(), 1);
    QCOMPARE(adopted.first().identityRating, 8.1);
    QCOMPARE(adopted.first().identityGenres, QStringLiteral("Sci-Fi · Adventure"));
    QCOMPARE(adopted.first().identityType, QStringLiteral("movie"));

    // Un-identify clears them (no stale billboard on a filename-honest tile).
    QVERIFY(identifier.unidentifyGroup(row.groupKey));
    const QList<VaultIndex::FileRow> cleared = index.rowsForGroup(row.groupKey);
    QCOMPARE(cleared.first().identityRating, 0.0);
    QVERIFY(cleared.first().identityGenres.isEmpty());
    QVERIFY(cleared.first().identityType.isEmpty());
}

void VaultIdentifierTest::numericTitleKeepsItsLeadingYearAndMatchesReleaseYear()
{
    QTemporaryDir tmp; VaultIndex index(tmp.filePath(QStringLiteral("numeric-title.sqlite"))); QVERIFY(index.isOpen());
    ImdbCatalog imdb(m_imdbDunePath); QVERIFY(imdb.ready());
    VaultIdentifier identifier(&index, nullptr, nullptr, &imdb);
    const QString raw = QStringLiteral("2001 A Space Odyssey (1968)");
    auto row = fixtureRow(QStringLiteral("vault:odyssey"), VaultKit::cleanMediaFolderTitle(raw));
    row.kind = QStringLiteral("video"); row.parsedYear = VaultKit::parsedTitleYear(raw);
    QCOMPARE(row.groupTitle, QStringLiteral("2001 A Space Odyssey")); QCOMPARE(row.parsedYear, 1968);
    QVERIFY(index.upsert(row));
    auto match = identifier.matchGroup(row.groupKey);
    QVERIFY(match.adopted); QCOMPARE(match.sourceId, QStringLiteral("imdb:tt0062622")); QCOMPARE(match.year, 1968);
    // Older/unprocessed rows use the same qualified parser, without dropping the title's 2001.
    row.groupTitle = raw; row.parsedYear = 0; QVERIFY(index.upsert(row));
    match = identifier.matchGroup(row.groupKey);
    QVERIFY(match.adopted); QCOMPARE(match.sourceId, QStringLiteral("imdb:tt0062622")); QCOMPARE(match.year, 1968);
}

void VaultIdentifierTest::manualFlatShowTypePersistsWithoutCatalogueAndRejectsUnknownType()
{
    QTemporaryDir tmp; VaultIndex index(tmp.filePath(QStringLiteral("manual-type.sqlite"))); QVERIFY(index.isOpen());
    auto row = fixtureRow(QStringLiteral("vault:flat"), QStringLiteral("Flat show"));
    row.kind = QStringLiteral("video"); row.progressed = true; QVERIFY(index.upsert(row));
    VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    VaultIdentifier::Match choice; choice.adopted = true; choice.source = QStringLiteral("IMDB");
    choice.sourceId = QStringLiteral("imdb:tt123"); choice.title = QStringLiteral("Flat show");
    choice.world = QStringLiteral("Theatre"); choice.type = QStringLiteral(" SERIES ");
    QVERIFY(identifier.identifyGroupWith(row.groupKey, choice));
    auto saved = index.rowsForGroup(row.groupKey).first(); QCOMPARE(saved.identityType, QStringLiteral("series"));
    QCOMPARE(saved.id, row.id); QVERIFY(saved.progressed); QCOMPARE(identifier.autoIdentifyExisting(), 0);
    QVERIFY(identifier.unidentifyGroup(row.groupKey)); saved = index.rowsForGroup(row.groupKey).first();
    QVERIFY(saved.identityType.isEmpty()); QVERIFY(saved.identitySuppressed); QCOMPARE(saved.id, row.id); QVERIFY(saved.progressed);
    choice.type = QStringLiteral("mini"); QVERIFY(identifier.identifyGroupWith(row.groupKey, choice));
    QCOMPARE(index.rowsForGroup(row.groupKey).first().identityType, QStringLiteral("mini"));
    choice.type = QStringLiteral("episode"); QVERIFY(identifier.identifyGroupWith(row.groupKey, choice));
    QVERIFY(index.rowsForGroup(row.groupKey).first().identityType.isEmpty());
}

// The negative, the gate's own law: without a year the cleaned title "Dune" matches BOTH
// catalogue rows and the conservative gate stays honestly ambiguous (never a guess).
void VaultIdentifierTest::yearlessDuneStaysHonestlyAmbiguous()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    auto row = fixtureRow(QStringLiteral("vault:dune-plain"), QStringLiteral("Dune"));
    row.kind = QStringLiteral("video");
    row.parsedYear = 0; // no year in the raw folder name
    QVERIFY(index.publish({row}));
    ImdbCatalog imdb(m_imdbDunePath);
    QVERIFY(imdb.ready());
    VaultIdentifier identifier(&index, nullptr, nullptr, &imdb);

    const VaultIdentifier::Match match = identifier.matchGroup(row.groupKey);
    QVERIFY(!match.adopted);
    QCOMPARE(match.candidateCount, 2);
}

void VaultIdentifierTest::bookMatchRequiresActualEmbeddedTitleInsteadOfFilenameFallback()
{
    QTemporaryDir tmp; VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite"))); QVERIFY(index.isOpen());
    auto row = fixtureRow(QStringLiteral("vault:book"), QStringLiteral("Release filename"));
    row.kind = QStringLiteral("book"); row.metadataSource = QStringLiteral("EPUB");
    row.displayTitle = QStringLiteral("Release filename"); row.progressed = true;
    QVERIFY(index.publish({row})); VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    QVERIFY(!identifier.matchGroup(row.groupKey).adopted); QCOMPARE(identifier.autoIdentifyExisting(), 0);
    row.embeddedTitle = QStringLiteral("Actual embedded title"); QVERIFY(index.upsert(row));
    const auto match = identifier.matchGroup(row.groupKey); QVERIFY(match.adopted);
    QCOMPARE(match.title, row.embeddedTitle); QCOMPARE(identifier.autoIdentifyExisting(), 1);
    const auto adopted = index.rowsForGroup(row.groupKey).first();
    QCOMPARE(adopted.identityTitle, row.embeddedTitle); QCOMPARE(adopted.id, row.id); QVERIFY(adopted.progressed);
}

void VaultIdentifierTest::conflictingBookTitlesOrUnprovenGroupMembersRefuseAutoMatch()
{
    QTemporaryDir tmp; VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite"))); QVERIFY(index.isOpen());
    auto first = fixtureRow(QStringLiteral("vault:first"), QStringLiteral("Books"));
    first.kind = QStringLiteral("book"); first.metadataSource = QStringLiteral("EPUB"); first.embeddedTitle = QStringLiteral("First book");
    auto second = first; second.id = QStringLiteral("vault:second"); second.path += QStringLiteral(".second");
    second.embeddedTitle = QStringLiteral("Different book");
    QVERIFY(index.publish({first, second})); VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    QVERIFY(!identifier.matchGroup(first.groupKey).adopted); QCOMPARE(identifier.autoIdentifyExisting(), 0);
    second.embeddedTitle = QStringLiteral(" first book "); QVERIFY(index.upsert(second));
    QVERIFY(identifier.matchGroup(first.groupKey).adopted);
    second.metadataSource.clear(); QVERIFY(index.upsert(second));
    QVERIFY(!identifier.matchGroup(first.groupKey).adopted);
    second.metadataSource = QStringLiteral("EPUB"); second.embeddedTitle.clear(); QVERIFY(index.upsert(second));
    QVERIFY(!identifier.matchGroup(first.groupKey).adopted);
}

void VaultIdentifierTest::manualPdfMatchSurvivesAutoPassAndExplicitSuppressionSemantics()
{
    QTemporaryDir tmp; VaultIndex index(tmp.filePath(QStringLiteral("index.sqlite"))); QVERIFY(index.isOpen());
    auto row = fixtureRow(QStringLiteral("vault:manual-book"), QStringLiteral("Books"));
    row.kind = QStringLiteral("book"); row.format = QStringLiteral("pdf"); row.realName = QStringLiteral("manual.pdf");
    row.path = row.subtreePath + QStringLiteral("/manual.pdf"); row.progressed = true; row.identitySuppressed = true;
    QVERIFY(index.publish({row})); VaultIdentifier identifier(&index, nullptr, nullptr, nullptr);
    VaultIdentifier::Match chosen; chosen.adopted = true; chosen.source = QStringLiteral("EPUB");
    chosen.sourceId = QStringLiteral("book:manual"); chosen.title = QStringLiteral("Deliberate manual title"); chosen.world = QStringLiteral("Biblio");
    QVERIFY(identifier.identifyGroupWith(row.groupKey, chosen)); QCOMPARE(identifier.autoIdentifyExisting(), 0);
    auto current = index.rowsForGroup(row.groupKey).first();
    QCOMPARE(current.id, row.id); QVERIFY(current.progressed); QVERIFY(!current.identitySuppressed);
    QCOMPARE(current.identityId, chosen.sourceId); QCOMPARE(current.identityTitle, chosen.title); QVERIFY(current.embeddedTitle.isEmpty());
    QVERIFY(identifier.unidentifyGroup(row.groupKey)); QCOMPARE(identifier.autoIdentifyExisting(), 0);
    current = index.rowsForGroup(row.groupKey).first(); QVERIFY(current.identitySuppressed); QVERIFY(current.identityId.isEmpty());
    QCOMPARE(current.id, row.id); QVERIFY(current.progressed);
    QVERIFY(identifier.identifyGroupWith(row.groupKey, chosen));
    current = index.rowsForGroup(row.groupKey).first(); QVERIFY(!current.identitySuppressed);
    QCOMPARE(current.identityTitle, chosen.title); QCOMPARE(current.id, row.id); QVERIFY(current.progressed);
}

void VaultIdentifierTest::unambiguousMatchAdopts()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    const auto row = fixtureRow(QStringLiteral("vault:cowboy"), QStringLiteral("Cowboy Bebop"));
    QVERIFY(index.publish({row}));
    MalCatalog mal(m_malPath);
    QVERIFY(mal.ready());
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);

    const VaultIdentifier::Match match = identifier.matchGroup(row.groupKey);
    QVERIFY(match.adopted);
    QCOMPARE(match.source, QStringLiteral("MAL"));
    QCOMPARE(match.sourceId, QStringLiteral("mal:3"));
    QCOMPARE(match.title, QStringLiteral("Cowboy Bebop"));
    QCOMPARE(match.synopsis, QStringLiteral("synopsis-3"));
}

void VaultIdentifierTest::twoCandidatesStayUnidentified()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    const auto row = fixtureRow(QStringLiteral("vault:matrix"), QStringLiteral("The Matrix"));
    QVERIFY(index.publish({row}));
    MalCatalog mal(m_ambiguousMalPath);
    QVERIFY(mal.ready());
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);

    const VaultIdentifier::Match match = identifier.matchGroup(row.groupKey);
    QVERIFY(!match.adopted);
    QVERIFY(match.source.isEmpty());
}

void VaultIdentifierTest::manualChosenCandidateWinsAndPreservesProgress()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    auto row = fixtureRow(QStringLiteral("vault:manual"), QStringLiteral("The Matrix"));
    row.progressed = true;
    QVERIFY(index.publish({row}));

    MalCatalog mal(m_ambiguousMalPath);
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);
    VaultIdentifier::Match chosen;
    chosen.adopted = true;
    chosen.source = QStringLiteral("MAL");
    chosen.sourceId = QStringLiteral("mal:2");
    chosen.title = QStringLiteral("Matrix (2003)");
    chosen.synopsis = QStringLiteral("chosen B");
    chosen.world = QStringLiteral("Tankoban");
    chosen.year = 2003;

    QVERIFY(identifier.identifyGroupWith(row.groupKey, chosen));
    const auto rows = index.rowsForGroup(row.groupKey);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().identityId, QStringLiteral("mal:2"));
    QCOMPARE(rows.first().identityTitle, QStringLiteral("Matrix (2003)"));
    QCOMPARE(rows.first().identitySynopsis, QStringLiteral("chosen B"));
    QCOMPARE(rows.first().progressed, true);
    QCOMPARE(rows.first().id, row.id);
}

void VaultIdentifierTest::incoherentGroupTitlesAreRefused()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    auto first = fixtureRow(QStringLiteral("vault:fused-1"), QStringLiteral("Cowboy Bebop"));
    auto second = fixtureRow(QStringLiteral("vault:fused-2"), QStringLiteral("The Matrix"));
    second.groupKey = first.groupKey;
    second.subtreePath = first.subtreePath;
    second.path = first.subtreePath + QStringLiteral("/The Matrix.mp4");
    second.displayTitle = QStringLiteral("The Matrix");
    QVERIFY(index.publish({first, second}));

    MalCatalog mal(m_malPath);
    QVERIFY(mal.ready());
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);

    const VaultIdentifier::Match match = identifier.matchGroup(first.groupKey);
    QVERIFY(!match.adopted);
}

void VaultIdentifierTest::coherentSeriesAllowsDifferentDisplayTitles()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    auto first = fixtureRow(QStringLiteral("vault:series-1"), QStringLiteral("Cowboy Bebop"));
    auto second = fixtureRow(QStringLiteral("vault:series-2"), QStringLiteral("Cowboy Bebop"));
    auto third = fixtureRow(QStringLiteral("vault:series-3"), QStringLiteral("Cowboy Bebop"));
    second.displayTitle = QStringLiteral("Vol 2");
    third.displayTitle = QStringLiteral("Chapter 45-53");
    QVERIFY(index.publish({first, second, third}));

    MalCatalog mal(m_malPath);
    QVERIFY(mal.ready());
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);

    const VaultIdentifier::Match match = identifier.matchGroup(first.groupKey);
    QVERIFY(match.adopted);
    QCOMPARE(match.title, QStringLiteral("Cowboy Bebop"));
}

void VaultIdentifierTest::absentDatabaseNoOpsHonestly()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    const auto row = fixtureRow(QStringLiteral("vault:missing"), QStringLiteral("Cowboy Bebop"));
    QVERIFY(index.publish({row}));
    MalCatalog mal(vaultDir.filePath(QStringLiteral("missing.sqlite")));
    QVERIFY(!mal.ready());
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);

    const VaultIdentifier::Match match = identifier.matchGroup(row.groupKey);
    QVERIFY(!match.adopted);
    QVERIFY(match.title.isEmpty());
}

void VaultIdentifierTest::autoPassIdentifiesOnlyCertainEligibleGroups()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    auto certain = fixtureRow(QStringLiteral("vault:auto-certain"), QStringLiteral("Cowboy Bebop"));
    certain.progressed = true;
    auto ambiguous = fixtureRow(QStringLiteral("vault:auto-ambiguous"), QStringLiteral("The Matrix"));
    auto suppressed = fixtureRow(QStringLiteral("vault:auto-suppressed"), QStringLiteral("Cowboy Bebop"));
    suppressed.groupKey += QStringLiteral("/suppressed");
    suppressed.identitySuppressed = true;
    auto away = fixtureRow(QStringLiteral("vault:auto-away"), QStringLiteral("Cowboy Bebop"));
    away.groupKey += QStringLiteral("/away");
    away.away = true;
    auto errored = fixtureRow(QStringLiteral("vault:auto-error"), QStringLiteral("Cowboy Bebop"));
    errored.groupKey += QStringLiteral("/error");
    errored.errorState = QStringLiteral("corrupt");
    QVERIFY(index.publish({certain, ambiguous, suppressed, away, errored}));

    MalCatalog mal(m_malPath);
    QVERIFY(mal.ready());
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);
    QCOMPARE(identifier.autoIdentifyExisting(), 1);

    const auto certainRows = index.rowsForGroup(certain.groupKey);
    QCOMPARE(certainRows.size(), 1);
    QCOMPARE(certainRows.first().identitySource, QStringLiteral("MAL"));
    QCOMPARE(certainRows.first().id, certain.id);
    QCOMPARE(certainRows.first().progressed, true);
    QVERIFY(index.rowsForGroup(ambiguous.groupKey).first().identitySource.isEmpty());
    QVERIFY(index.rowsForGroup(suppressed.groupKey).first().identitySource.isEmpty());
    QVERIFY(index.rowsForGroup(away.groupKey).first().identitySource.isEmpty());
    QVERIFY(index.rowsForGroup(errored.groupKey).first().identitySource.isEmpty());
}

void VaultIdentifierTest::unidentifyPreservesProgressAndFileId()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    auto row = fixtureRow(QStringLiteral("vault:roundtrip"), QStringLiteral("Cowboy Bebop"));
    row.progressed = true;
    QVERIFY(index.publish({row}));
    MalCatalog mal(m_malPath);
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);
    const auto match = identifier.matchGroup(row.groupKey);
    QVERIFY(match.adopted);
    QVERIFY(identifier.applyGroup(row.groupKey, match));
    QVERIFY(identifier.unidentifyGroup(row.groupKey));

    const auto rows = index.rowsForRoot(QStringLiteral("D:/vault"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().id, row.id);
    QCOMPARE(rows.first().progressed, true);
    QVERIFY(rows.first().identitySource.isEmpty());
    QCOMPARE(rows.first().displayTitle, QStringLiteral("Vol 1"));
}

void VaultIdentifierTest::reshelveChangesKindWithoutChangingFileId()
{
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());
    auto row = fixtureRow(QStringLiteral("vault:reshelve"), QStringLiteral("Cowboy Bebop"));
    row.progressed = true;
    QVERIFY(index.publish({row}));
    MalCatalog mal(m_malPath);
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);

    QVERIFY(identifier.reshelveGroup(row.groupKey, QStringLiteral("book")));
    const auto rows = index.rowsForRoot(QStringLiteral("D:/vault"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().id, row.id);
    QCOMPARE(rows.first().kind, QStringLiteral("book"));
    QCOMPARE(rows.first().progressed, true);
}

void VaultIdentifierTest::identityStateRecordsAdoptedAmbiguousNoneAndSuppressed()
{
    // Slice 2: matchGroup()'s candidateCount + VaultIdentifier::recordAmbiguous() make
    // "Vault isn't sure" a durable, projectable fact instead of it hiding inside "not
    // identified yet" — one-candidate adopts, two-candidate records ambiguous(2) and stays
    // unidentified, zero-candidate never writes anything (stays "none"), and Un-identify
    // records "suppressed". Pre-change baseline: ambiguous groups were indistinguishable from
    // unscanned ones in every projection — this proves the column now tells them apart.
    QTemporaryDir vaultDir;
    QVERIFY(vaultDir.isValid());
    VaultIndex index(vaultDir.filePath(QStringLiteral("index.sqlite")));
    QVERIFY(index.isOpen());

    auto adopted = fixtureRow(QStringLiteral("vault:state-adopted"), QStringLiteral("Cowboy Bebop"));

    auto ambiguous = fixtureRow(QStringLiteral("vault:state-ambiguous"), QStringLiteral("The Matrix"));
    ambiguous.groupKey += QStringLiteral("/ambiguous");
    ambiguous.subtreePath = ambiguous.groupKey;
    ambiguous.path = ambiguous.subtreePath + QStringLiteral("/Vol 1.cbz");

    auto none = fixtureRow(QStringLiteral("vault:state-none"),
                           QStringLiteral("Totally Unknown Franchise XYZ"));
    none.groupKey += QStringLiteral("/none");
    none.subtreePath = none.groupKey;
    none.path = none.subtreePath + QStringLiteral("/Vol 1.cbz");

    QVERIFY(index.publish({adopted, ambiguous, none}));

    // one candidate -> adopted, durably (identityState survives past the reversible Match).
    MalCatalog mal(m_malPath); // has exactly "Cowboy Bebop"; neither "The Matrix" nor the XYZ title
    QVERIFY(mal.ready());
    VaultIdentifier identifier(&index, nullptr, &mal, nullptr);
    const VaultIdentifier::Match adoptedMatch = identifier.matchGroup(adopted.groupKey);
    QVERIFY(adoptedMatch.adopted);
    QVERIFY(identifier.applyGroup(adopted.groupKey, adoptedMatch));
    QCOMPARE(index.rowsForGroup(adopted.groupKey).first().identityState, QStringLiteral("adopted"));

    // zero candidates -> "none": matchGroup reports it honestly and nothing is ever written.
    const VaultIdentifier::Match noneMatch = identifier.matchGroup(none.groupKey);
    QVERIFY(!noneMatch.adopted);
    QCOMPARE(noneMatch.candidateCount, 0);
    QVERIFY(!identifier.recordAmbiguous(none.groupKey, noneMatch.candidateCount));
    QVERIFY(index.rowsForGroup(none.groupKey).first().identityState.isEmpty());

    // two candidates -> ambiguous(2), recorded, NEVER adopted.
    MalCatalog ambiguousMal(m_ambiguousMalPath);
    QVERIFY(ambiguousMal.ready());
    VaultIdentifier ambiguousIdentifier(&index, nullptr, &ambiguousMal, nullptr);
    const VaultIdentifier::Match ambiguousMatch = ambiguousIdentifier.matchGroup(ambiguous.groupKey);
    QVERIFY(!ambiguousMatch.adopted);
    QCOMPARE(ambiguousMatch.candidateCount, 2);
    QVERIFY(ambiguousIdentifier.recordAmbiguous(ambiguous.groupKey, ambiguousMatch.candidateCount));
    const VaultIndex::FileRow ambiguousRow = index.rowsForGroup(ambiguous.groupKey).first();
    QCOMPARE(ambiguousRow.identityState, QStringLiteral("ambiguous"));
    QCOMPARE(ambiguousRow.identityCandidateCount, 2);
    QVERIFY(ambiguousRow.identityId.isEmpty()); // still filename-honest, never silently merged

    // suppression round-trip: Un-identify clears identityId but records "suppressed" (never
    // reverts to "ambiguous"/"none" — the user's explicit choice is itself the durable fact).
    QVERIFY(identifier.unidentifyGroup(adopted.groupKey));
    QCOMPARE(index.rowsForGroup(adopted.groupKey).first().identityState, QStringLiteral("suppressed"));
}

QTEST_MAIN(VaultIdentifierTest)
#include "tst_vault_identifier.moc"
