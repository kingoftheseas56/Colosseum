// tst_tankoban_catalog — Catalogue-independence Slice 1. Proves TankobanCatalog:
// missing-db honest emptiness, series count/count_basis round-trip, volume-number
// synthesis from a known count, numeric-aware ordering (2 before 10), baked
// cover/name overlay, unknown-malId emptiness, and count_basis passthrough
// ("mal" vs "bookwalker"). SQLite in a QTemporaryDir per run (no committed
// .sqlite, house rule); the qsqlite driver resolves because the target lands in
// build-msvc/ beside the app-deployed plugin (ledger deploy note). GUILESS.

#include "engine/TankobanCatalog.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

namespace {

// Builds a fresh tankoban_catalog.db-shaped SQLite file at `path` with the
// production schema (native/engine/TankobanCatalog.h contract / the builder
// script's output shape), via its OWN throwaway connection — never the
// TankobanCatalog-under-test's connection.
bool buildFixtureDb(const QString& path)
{
    const QString connection =
        QStringLiteral("tst_tankoban_fixture_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery q(db);
            ok = q.exec(QStringLiteral(
                    "CREATE TABLE series (mal_id INTEGER PRIMARY KEY, "
                    "volume_count INTEGER NOT NULL, count_basis TEXT NOT NULL)"))
                 && q.exec(QStringLiteral(
                    "CREATE TABLE volumes (mal_id INTEGER NOT NULL, number TEXT NOT NULL, "
                    "cover_url TEXT NOT NULL DEFAULT '', name TEXT NOT NULL DEFAULT '')"));
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

bool insertSeries(const QString& path, int malId, int volumeCount, const QString& countBasis)
{
    const QString connection =
        QStringLiteral("tst_tankoban_seed_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT INTO series (mal_id, volume_count, count_basis) VALUES (?, ?, ?)"));
            q.addBindValue(malId);
            q.addBindValue(volumeCount);
            q.addBindValue(countBasis);
            ok = q.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

bool insertVolume(const QString& path, int malId, const QString& number,
                   const QString& coverUrl, const QString& name)
{
    const QString connection =
        QStringLiteral("tst_tankoban_seed_vol_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT INTO volumes (mal_id, number, cover_url, name) VALUES (?, ?, ?, ?)"));
            q.addBindValue(malId);
            q.addBindValue(number);
            // A default-constructed QString() is isNull() — Qt's SQLite driver
            // binds that as SQL NULL, which the NOT NULL columns below reject
            // (DEFAULT '' only applies when a column is OMITTED from the INSERT,
            // never when NULL is bound explicitly). Coerce to empty-but-non-null
            // so the fixture matches the production schema's own contract.
            q.addBindValue(coverUrl.isNull() ? QStringLiteral("") : coverUrl);
            q.addBindValue(name.isNull() ? QStringLiteral("") : name);
            ok = q.exec();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

} // namespace

class tst_tankoban_catalog : public QObject
{
    Q_OBJECT

private slots:
    void missing_db_is_honest_empty();
    void count_round_trip();
    void volumes_synthesized_from_count();
    void numeric_order_two_before_ten();
    void cover_and_name_attach();
    void unknown_malid_empty();
    void count_basis_passthrough();
};

void tst_tankoban_catalog::missing_db_is_honest_empty()
{
    TankobanCatalog cat(QStringLiteral("this/path/does/not/exist/tankoban_catalog.db"));
    QVERIFY(!cat.ready());
    QVERIFY(cat.seriesInfo(2).isEmpty());
    QVERIFY(cat.volumes(2).isEmpty());
}

void tst_tankoban_catalog::count_round_trip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("tankoban_catalog.db"));
    QVERIFY(buildFixtureDb(path));
    QVERIFY(insertSeries(path, 2, 41, QStringLiteral("mal")));

    TankobanCatalog cat(path);
    QVERIFY(cat.ready());
    const QVariantMap info = cat.seriesInfo(2);
    QCOMPARE(info.value(QStringLiteral("volumeCount")).toInt(), 41);
    QCOMPARE(info.value(QStringLiteral("countBasis")).toString(), QStringLiteral("mal"));
}

void tst_tankoban_catalog::volumes_synthesized_from_count()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("tankoban_catalog.db"));
    QVERIFY(buildFixtureDb(path));
    // volume_count 5, no baked volume rows — the seam synthesizes "1".."5".
    QVERIFY(insertSeries(path, 13, 5, QStringLiteral("mal")));

    TankobanCatalog cat(path);
    const QVariantList vols = cat.volumes(13);
    QCOMPARE(vols.size(), 5);
    for (int i = 0; i < 5; ++i) {
        const QVariantMap m = vols.at(i).toMap();
        QCOMPARE(m.value(QStringLiteral("number")).toString(), QString::number(i + 1));
        QVERIFY(m.value(QStringLiteral("cover")).toString().isEmpty());
        QVERIFY(m.value(QStringLiteral("name")).toString().isEmpty());
    }
}

void tst_tankoban_catalog::numeric_order_two_before_ten()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("tankoban_catalog.db"));
    QVERIFY(buildFixtureDb(path));
    // An ongoing series (count unknown) whose harvest rows arrived out of
    // TEXT-lexicographic order — "10" would sort before "2" without the
    // numeric-aware key. Proves the overlay-only ordering path directly.
    QVERIFY(insertSeries(path, 99, 0, QStringLiteral("mal")));
    QVERIFY(insertVolume(path, 99, QStringLiteral("10"), QStringLiteral("cover10.jpg"), QString()));
    QVERIFY(insertVolume(path, 99, QStringLiteral("2"), QStringLiteral("cover2.jpg"), QString()));
    QVERIFY(insertVolume(path, 99, QStringLiteral("1"), QStringLiteral("cover1.jpg"), QString()));

    TankobanCatalog cat(path);
    const QVariantList vols = cat.volumes(99);
    QCOMPARE(vols.size(), 3);
    QCOMPARE(vols.at(0).toMap().value(QStringLiteral("number")).toString(), QStringLiteral("1"));
    QCOMPARE(vols.at(1).toMap().value(QStringLiteral("number")).toString(), QStringLiteral("2"));
    QCOMPARE(vols.at(2).toMap().value(QStringLiteral("number")).toString(), QStringLiteral("10"));
}

void tst_tankoban_catalog::cover_and_name_attach()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("tankoban_catalog.db"));
    QVERIFY(buildFixtureDb(path));
    QVERIFY(insertSeries(path, 7, 3, QStringLiteral("bookwalker")));
    // Only volume 2 carries a baked cover/name; 1 and 3 stay synthesized-bare.
    QVERIFY(insertVolume(path, 7, QStringLiteral("2"), QStringLiteral("https://cdn/vol2.jpg"),
                         QStringLiteral("Volume 2: The Reckoning")));

    TankobanCatalog cat(path);
    const QVariantList vols = cat.volumes(7);
    QCOMPARE(vols.size(), 3);
    QCOMPARE(vols.at(0).toMap().value(QStringLiteral("cover")).toString(), QString());
    QCOMPARE(vols.at(1).toMap().value(QStringLiteral("cover")).toString(),
             QStringLiteral("https://cdn/vol2.jpg"));
    QCOMPARE(vols.at(1).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Volume 2: The Reckoning"));
    QCOMPARE(vols.at(2).toMap().value(QStringLiteral("cover")).toString(), QString());
}

void tst_tankoban_catalog::unknown_malid_empty()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("tankoban_catalog.db"));
    QVERIFY(buildFixtureDb(path));
    QVERIFY(insertSeries(path, 2, 41, QStringLiteral("mal")));

    TankobanCatalog cat(path);
    QVERIFY(cat.ready()); // the db itself is fine — only this id is absent
    QVERIFY(cat.seriesInfo(999999).isEmpty());
    QVERIFY(cat.volumes(999999).isEmpty());
}

void tst_tankoban_catalog::count_basis_passthrough()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("tankoban_catalog.db"));
    QVERIFY(buildFixtureDb(path));
    QVERIFY(insertSeries(path, 1, 0, QStringLiteral("mal")));         // ongoing, MAL-only
    QVERIFY(insertSeries(path, 2, 88, QStringLiteral("bookwalker"))); // harvest-supplied count

    TankobanCatalog cat(path);
    QCOMPARE(cat.seriesInfo(1).value(QStringLiteral("countBasis")).toString(),
             QStringLiteral("mal"));
    QCOMPARE(cat.seriesInfo(1).value(QStringLiteral("volumeCount")).toInt(), 0);
    QCOMPARE(cat.seriesInfo(2).value(QStringLiteral("countBasis")).toString(),
             QStringLiteral("bookwalker"));
    QCOMPARE(cat.seriesInfo(2).value(QStringLiteral("volumeCount")).toInt(), 88);
}

QTEST_GUILESS_MAIN(tst_tankoban_catalog)
#include "tst_tankoban_catalog.moc"
