#include <QtTest>

#include "engine/ComicsCatalog.h"
#include "engine/ImdbCatalog.h"
#include "engine/MalCatalog.h"
#include "engine/VaultIdentifier.h"
#include "engine/VaultIndex.h"

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
                const QString rows = ambiguous
                    ? QStringLiteral(
                        "INSERT INTO anime VALUES "
                        "(1,'The Matrix','','the matrix','', 'TV',8.0,1999,'cover-1','synopsis-1','[]','[]',1),"
                        "(2,'The Matrix','','the matrix','', 'TV',7.0,2003,'cover-2','synopsis-2','[]','[]',1)")
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
    void absentDatabaseNoOpsHonestly();
    void unidentifyPreservesProgressAndFileId();
    void reshelveChangesKindWithoutChangingFileId();

private:
    QTemporaryDir m_dir;
    QString m_malPath;
    QString m_ambiguousMalPath;
};

void VaultIdentifierTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_malPath = m_dir.filePath(QStringLiteral("mal.sqlite"));
    m_ambiguousMalPath = m_dir.filePath(QStringLiteral("mal-ambiguous.sqlite"));
    QVERIFY(buildMalFixture(m_malPath, false));
    QVERIFY(buildMalFixture(m_ambiguousMalPath, true));
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

QTEST_MAIN(VaultIdentifierTest)
#include "tst_vault_identifier.moc"
