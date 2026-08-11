#include <QtTest>

#include "engine/MalCatalog.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

namespace {

bool execSql(QSqlQuery& query, const QString& sql)
{
    query.prepare(sql);
    return query.exec();
}

bool buildFixture(const QString& path)
{
    const QString connection = QStringLiteral("vault_mal_fixture_writer");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (!db.open())
            return false;
        QSqlQuery q(db);
        if (!execSql(q, QStringLiteral(
                "CREATE TABLE anime ("
                "mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT, "
                "norm_title TEXT NOT NULL, norm_title_english TEXT NOT NULL, "
                "type TEXT, score REAL, year INTEGER, cover TEXT, synopsis TEXT, "
                "credits TEXT, tags TEXT, episodes INTEGER)")))
            return false;
        if (!execSql(q, QStringLiteral(
                "CREATE TABLE manga ("
                "mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT, "
                "norm_title TEXT NOT NULL, norm_title_english TEXT NOT NULL, "
                "type TEXT, score REAL, year INTEGER, cover TEXT, synopsis TEXT, "
                "credits TEXT, tags TEXT, volumes INTEGER, chapters INTEGER)")))
            return false;
        if (!execSql(q, QStringLiteral(
                "INSERT INTO anime VALUES "
                "(1,'The Matrix','','matrix','', 'TV',8.0,1999,'cover-1','synopsis-1','[]','[]',1),"
                "(2,'Matrix','','matrix','', 'Movie',7.0,2003,'cover-2','synopsis-2','[]','[]',1),"
                "(3,'Cowboy Bebop','','cowboy bebop','', 'TV',8.8,1998,'cover-3','synopsis-3','[]','[]',26)")))
            return false;
        if (!execSql(q, QStringLiteral(
                "INSERT INTO manga VALUES "
                "(10,'The Matrix','','matrix','', 'Manga',7.5,2001,'cover-10','synopsis-10','[]','[]',2,20)")))
            return false;
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return true;
}

int idAt(const QVariantList& rows, int index)
{
    return rows.at(index).toMap().value(QStringLiteral("mal_id")).toInt();
}

} // namespace

class VaultMalMatchTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void normalizedPunctuationMatches();
    void twoCandidatesStayAmbiguous();
    void yearNarrowsAmbiguity();
    void mediumNarrowsTables();
    void missingDatabaseIsSafe();

private:
    QTemporaryDir m_dir;
    QString m_dbPath;
};

void VaultMalMatchTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_dbPath = m_dir.filePath(QStringLiteral("mal.sqlite"));
    QVERIFY(buildFixture(m_dbPath));
}

void VaultMalMatchTest::normalizedPunctuationMatches()
{
    MalCatalog catalog(m_dbPath);
    QVERIFY(catalog.ready());
    const QVariantList rows = catalog.matchByTitle(QStringLiteral("Cowboy.Bebop"), 0,
                                                    QStringLiteral("anime"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(idAt(rows, 0), 3);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("medium")).toString(),
             QStringLiteral("anime"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("episodes")).toInt(), 26);
}

void VaultMalMatchTest::twoCandidatesStayAmbiguous()
{
    MalCatalog catalog(m_dbPath);
    const QVariantList rows = catalog.matchByTitle(QStringLiteral("The Matrix"), 0,
                                                    QStringLiteral("anime"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(idAt(rows, 0), 2);
    QCOMPARE(idAt(rows, 1), 1);
}

void VaultMalMatchTest::yearNarrowsAmbiguity()
{
    MalCatalog catalog(m_dbPath);
    const QVariantList rows = catalog.matchByTitle(QStringLiteral("The.Matrix"), 1999,
                                                    QStringLiteral("anime"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(idAt(rows, 0), 1);
}

void VaultMalMatchTest::mediumNarrowsTables()
{
    MalCatalog catalog(m_dbPath);
    const QVariantList rows = catalog.matchByTitle(QStringLiteral("The Matrix"), 0,
                                                    QStringLiteral("manga"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(idAt(rows, 0), 10);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("volumes")).toInt(), 2);
    QVERIFY(catalog.matchByTitle(QStringLiteral("The Matrix"), 0,
                                 QStringLiteral("podcast")).isEmpty());
}

void VaultMalMatchTest::missingDatabaseIsSafe()
{
    MalCatalog catalog(m_dir.filePath(QStringLiteral("missing.sqlite")));
    QVERIFY(!catalog.ready());
    QVERIFY(catalog.matchByTitle(QStringLiteral("The Matrix")).isEmpty());
}

QTEST_MAIN(VaultMalMatchTest)
#include "tst_vault_mal_match.moc"
