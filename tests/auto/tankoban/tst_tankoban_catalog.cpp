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
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

namespace {

class ContentFixture : public QTcpServer {
public:
    int guests = 0;
    int reads = 0;
    int status = 200;
    int delay = 0;
    bool ready = true;
    bool unknown = false;
    bool oversized = false;
    bool conflictOnce = false;
    QString sourceHash = QString(64, 'a');
    QUrl endpoint() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    ContentFixture() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto* socket = nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                const auto request = socket->readAll();
                if (!request.contains("\r\n\r\n")) return;
                QByteArray body;
                int responseStatus = 200;
                const bool guest = request.startsWith("POST /v1/guest/session ");
                if (guest) {
                    ++guests;
                    body = QJsonDocument(QJsonObject{{"token", "fixture-lease"},
                        {"expiresAt", QDateTime::currentDateTimeUtc().addSecs(300).toString(Qt::ISODate)}}).toJson();
                } else {
                    ++reads;
                    if (!request.contains("Authorization: Bearer fixture-lease")) responseStatus = 401;
                    else responseStatus = conflictOnce && reads == 1 ? 409 : status;
                    QJsonObject info;
                    QJsonArray rows;
                    if (!unknown) {
                        info = {{"volumeCount", 2}, {"countBasis", "mal"}};
                        rows = {QJsonObject{{"number", "1"}, {"cover", "cover1"}, {"name", "One"}},
                                QJsonObject{{"number", "2"}, {"cover", ""}, {"name", ""}}};
                    }
                    body = oversized ? QByteArray(512 * 1024 + 100, 'x') : QJsonDocument(QJsonObject{
                        {"apiVersion", 1}, {"ready", ready}, {"sourceHash", sourceHash},
                        {"seriesInfo", info}, {"volumes", rows}}).toJson();
                }
                QPointer<QTcpSocket> safe(socket);
                QTimer::singleShot(guest ? 0 : delay, socket, [safe, body, responseStatus] {
                    if (!safe) return;
                    safe->write("HTTP/1.1 " + QByteArray::number(responseStatus) + " Fixture\r\nContent-Type: application/json\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    safe->disconnectFromHost();
                });
            });
        });
    }
};

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
    void remote_is_disabled_by_default();
    void remote_result_cached_offline_without_global_readiness();
    void remote_rejects_not_ready_unknown_and_oversize();
    void remote_cancel_ignores_late_reply();
    void remote_revision_conflict_renews_once();
    void remote_preserves_local_authority();
    void remote_cache_bounded_and_revision_invalidated();
    void actual_worker_contract();
    void missing_db_is_honest_empty();
    void count_round_trip();
    void volumes_synthesized_from_count();
    void numeric_order_two_before_ten();
    void cover_and_name_attach();
    void unknown_malid_empty();
    void count_basis_passthrough();
};

void tst_tankoban_catalog::remote_is_disabled_by_default()
{
    TankobanCatalog cat(QStringLiteral("missing-catalogue.db"));
    QSignalSpy failed(&cat, &TankobanCatalog::seriesFailed);
    cat.requestSeries(13);
    QCOMPARE(failed.count(), 1);
    QVERIFY(!cat.hasSeries(13));
    QVERIFY(!cat.ready());
    QCOMPARE(cat.revision(), 0);
}

void tst_tankoban_catalog::remote_result_cached_offline_without_global_readiness()
{
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager nam;
    TankobanCatalog cat(QStringLiteral("missing-catalogue.db"));
    cat.configureRemote(&nam, server.endpoint());
    QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
    cat.requestSeries(13);
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(cat.seriesInfo(13).value("volumeCount").toInt(), 2);
    QCOMPARE(cat.volumes(13).size(), 2);
    QCOMPARE(cat.revision(), 1);
    QVERIFY(cat.hasSeries(13));
    QVERIFY(!cat.ready());
    server.close();
    cat.requestSeries(13);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(server.guests, 1);
    QCOMPARE(server.reads, 1);
    QSignalSpy failed(&cat, &TankobanCatalog::seriesFailed);
    cat.requestSeries(13, true);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(cat.volumes(13).size(), 2); // last-good facts survive failed revalidation
}

void tst_tankoban_catalog::actual_worker_contract()
{
    const auto endpoint = qEnvironmentVariable("COLOSSEUM_CONTENT_TEST_URL");
    if (endpoint.isEmpty()) QSKIP("Actual Worker fixture not configured");
    const int malId = qEnvironmentVariableIntValue("COLOSSEUM_CONTENT_TEST_MAL_ID");
    QVERIFY(malId > 0);
    QNetworkAccessManager nam;
    TankobanCatalog cat(QStringLiteral("missing-catalogue.db"));
    cat.configureRemote(&nam, QUrl(endpoint));
    QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
    QSignalSpy failed(&cat, &TankobanCatalog::seriesFailed);
    cat.requestSeries(malId);
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() || failed.count(), 20000);
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(cat.seriesInfo(malId).value("volumeCount").toInt(),
             qEnvironmentVariableIntValue("COLOSSEUM_CONTENT_TEST_VOLUME_COUNT"));
    QVERIFY(!cat.ready());
    QVERIFY(cat.hasSeries(malId));
    const auto rows = cat.volumes(malId);
    QCOMPARE(rows.size(), 4);
    QCOMPARE(rows.at(0).toMap().value("number").toString(), QStringLiteral("1"));
    QCOMPARE(rows.at(1).toMap().value("number").toString(), QStringLiteral("2"));
    QCOMPARE(rows.at(2).toMap().value("number").toString(), QStringLiteral("3"));
    QCOMPARE(rows.at(3).toMap().value("number").toString(), QStringLiteral("10"));
    QCOMPARE(rows.at(1).toMap().value("cover").toString(), QStringLiteral("https://fixture.invalid/two"));
    QCOMPARE(rows.at(1).toMap().value("name").toString(), QStringLiteral("Two"));
    cat.requestSeries(99);
    QTRY_COMPARE(changed.count(), 2);
    QVERIFY(cat.seriesInfo(99).isEmpty());
    QVERIFY(cat.volumes(99).isEmpty());
}

void tst_tankoban_catalog::remote_rejects_not_ready_unknown_and_oversize()
{
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager nam;
    TankobanCatalog cat(QStringLiteral("missing-catalogue.db"));
    cat.configureRemote(&nam, server.endpoint());
    QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
    QSignalSpy failed(&cat, &TankobanCatalog::seriesFailed);
    server.ready = false;
    cat.requestSeries(13);
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(!cat.hasSeries(13));
    QCOMPARE(cat.revision(), 0);
    server.ready = true;
    server.unknown = true;
    cat.requestSeries(99);
    QTRY_COMPARE(changed.count(), 1);
    QVERIFY(!cat.hasSeries(99));
    QVERIFY(cat.seriesInfo(99).isEmpty());
    QVERIFY(cat.volumes(99).isEmpty());
    server.unknown = false;
    server.oversized = true;
    cat.requestSeries(14);
    QTRY_COMPARE(failed.count(), 2);
    QVERIFY(!cat.hasSeries(14));
    QCOMPARE(cat.revision(), 1);
}

void tst_tankoban_catalog::remote_cancel_ignores_late_reply()
{
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.delay = 150;
    QNetworkAccessManager nam;
    TankobanCatalog cat(QStringLiteral("missing-catalogue.db"));
    cat.configureRemote(&nam, server.endpoint());
    QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
    cat.requestSeries(13);
    QTRY_COMPARE(server.reads, 1);
    cat.cancelRequest();
    QTest::qWait(250);
    QCOMPARE(changed.count(), 0);
    QVERIFY(!cat.hasSeries(13));
    QCOMPARE(cat.revision(), 0);
}

void tst_tankoban_catalog::remote_revision_conflict_renews_once()
{
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager nam;
    TankobanCatalog cat(QStringLiteral("missing-catalogue.db"));
    cat.configureRemote(&nam, server.endpoint());
    QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
    QSignalSpy failed(&cat, &TankobanCatalog::seriesFailed);
    cat.requestSeries(13);
    QTRY_COMPARE(changed.count(), 1);
    server.status = 409;
    cat.requestSeries(14);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(server.guests, 2);
    QCOMPARE(server.reads, 3);
    QVERIFY(!cat.hasSeries(14));
    QVERIFY(cat.hasSeries(13));
}

void tst_tankoban_catalog::remote_preserves_local_authority()
{
    QTemporaryDir tmp;
    const auto path = tmp.filePath(QStringLiteral("tankoban.db"));
    QVERIFY(buildFixtureDb(path));
    QVERIFY(insertSeries(path, 13, 5, QStringLiteral("bookwalker")));
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager nam;
    TankobanCatalog cat(path);
    cat.configureRemote(&nam, server.endpoint());
    cat.requestSeries(13);
    QCOMPARE(server.guests, 0);
    QCOMPARE(cat.volumes(13).size(), 5);
    QCOMPARE(cat.seriesInfo(13).value("countBasis").toString(), QStringLiteral("bookwalker"));
}

void tst_tankoban_catalog::remote_cache_bounded_and_revision_invalidated()
{
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager nam;
    TankobanCatalog cat(QStringLiteral("missing-catalogue.db"));
    cat.configureRemote(&nam, server.endpoint());
    QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
    for (int id = 1; id <= 33; ++id) {
        cat.requestSeries(id);
        QTRY_COMPARE(changed.count(), id);
    }
    int cached = 0;
    for (int id = 1; id <= 33; ++id) if (cat.hasSeries(id)) ++cached;
    QCOMPARE(cached, 32);
    server.sourceHash = QString(64, 'b');
    cat.requestSeries(34);
    QTRY_COMPARE(changed.count(), 34);
    QVERIFY(cat.hasSeries(34));
    for (int id = 1; id <= 33; ++id) QVERIFY(!cat.hasSeries(id));
}

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
