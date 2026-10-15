#include <QtTest>

#include "engine/ImdbCatalog.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

namespace {

bool execSql(QSqlQuery& query, const QString& sql)
{
    query.prepare(sql);
    return query.exec();
}

bool buildFixture(const QString& path, bool withNormalizedColumn)
{
    const QString connection = QStringLiteral("vault_imdb_fixture_writer");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (!db.open())
            return false;
        QSqlQuery q(db);
        const QString normColumn = withNormalizedColumn
            ? QStringLiteral("norm_title TEXT NOT NULL, ")
            : QString();
        if (!execSql(q, QStringLiteral(
                "CREATE TABLE title ("
                "tt TEXT PRIMARY KEY, type TEXT, title TEXT, %1"
                "year INTEGER, endYear INTEGER, runtimeMin INTEGER, genres TEXT, "
                "rating REAL, votes INTEGER, episodes INTEGER, origLang TEXT, isAnime INTEGER)"
                ).arg(normColumn)))
            return false;

        const QString insertSql = withNormalizedColumn
            ? QStringLiteral(
                "INSERT INTO title VALUES "
                "('tt0000001','movie','The Matrix','matrix',1999,0,136,'[\"Action\",\"Sci-Fi\"]',8.7,9000,0,'en',0),"
                "('tt0000002','movie','Matrix','matrix',2003,0,120,'[\"Action\"]',7.1,4000,0,'en',0),"
                "('tt0000003','series','Cowboy Bebop','cowboy bebop',1998,1999,24,'[\"Animation\"]',8.9,12000,26,'ja',1)"
              )
            : QStringLiteral(
                "INSERT INTO title VALUES "
                "('tt0000099','movie','The Matrix',1999,0,136,'[]',8.7,9000,0,'en',0)"
              );
        if (!execSql(q, insertSql))
            return false;
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return true;
}

QString idAt(const QVariantList& rows, int index)
{
    return rows.at(index).toMap().value(QStringLiteral("tt")).toString();
}

} // namespace

class VaultImdbMatchTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void normalizedPunctuationMatches();
    void prefixSearchRanksCandidates();
    void twoCandidatesStayAmbiguous();
    void yearNarrowsAmbiguity();
    void synopsisIsHonestAndFactsArePresent();
    void legacySchemaDoesNotMatch();
    void missingDatabaseIsSafe();

private:
    QTemporaryDir m_dir;
    QString m_dbPath;
};

void VaultImdbMatchTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_dbPath = m_dir.filePath(QStringLiteral("imdb.sqlite"));
    QVERIFY(buildFixture(m_dbPath, true));
}

void VaultImdbMatchTest::normalizedPunctuationMatches()
{
    ImdbCatalog catalog(m_dbPath);
    QVERIFY(catalog.ready());
    const QVariantList rows = catalog.matchByTitle(QStringLiteral("Cowboy.Bebop"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(idAt(rows, 0), QStringLiteral("tt0000003"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("isAnime")).toBool(), true);
}

void VaultImdbMatchTest::prefixSearchRanksCandidates()
{
    ImdbCatalog catalog(m_dbPath);
    QVERIFY(catalog.ready());
    const QVariantList rows = catalog.search(QStringLiteral("matrix"), 10);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(idAt(rows, 0), QStringLiteral("tt0000002"));
    QCOMPARE(idAt(rows, 1), QStringLiteral("tt0000001"));
}

void VaultImdbMatchTest::twoCandidatesStayAmbiguous()
{
    ImdbCatalog catalog(m_dbPath);
    const QVariantList rows = catalog.matchByTitle(QStringLiteral("The Matrix"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(idAt(rows, 0), QStringLiteral("tt0000002"));
    QCOMPARE(idAt(rows, 1), QStringLiteral("tt0000001"));
}

void VaultImdbMatchTest::yearNarrowsAmbiguity()
{
    ImdbCatalog catalog(m_dbPath);
    const QVariantList rows = catalog.matchByTitle(QStringLiteral("The.Matrix"), 1999);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(idAt(rows, 0), QStringLiteral("tt0000001"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("rating")).toDouble(), 8.7);
}

void VaultImdbMatchTest::synopsisIsHonestAndFactsArePresent()
{
    ImdbCatalog catalog(m_dbPath);
    const QVariantMap row = catalog.matchByTitle(QStringLiteral("The Matrix"), 1999)
                                .at(0).toMap();
    QVERIFY(row.contains(QStringLiteral("synopsis")));
    QVERIFY(row.value(QStringLiteral("synopsis")).toString().isEmpty());
    QCOMPARE(row.value(QStringLiteral("votes")).toInt(), 9000);
    QCOMPARE(row.value(QStringLiteral("type")).toString(), QStringLiteral("movie"));
}

void VaultImdbMatchTest::legacySchemaDoesNotMatch()
{
    const QString legacyPath = m_dir.filePath(QStringLiteral("legacy.sqlite"));
    QVERIFY(buildFixture(legacyPath, false));
    ImdbCatalog catalog(legacyPath);
    QVERIFY(catalog.ready());
    QVERIFY(catalog.matchByTitle(QStringLiteral("The Matrix")).isEmpty());
}

void VaultImdbMatchTest::missingDatabaseIsSafe()
{
    ImdbCatalog catalog(m_dir.filePath(QStringLiteral("missing.sqlite")));
    QVERIFY(!catalog.ready());
    QVERIFY(catalog.matchByTitle(QStringLiteral("The Matrix")).isEmpty());
}

QTEST_MAIN(VaultImdbMatchTest)
#include "tst_vault_imdb_match.moc"
