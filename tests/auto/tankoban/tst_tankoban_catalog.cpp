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
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QTextStream>
#include <QScopeGuard>

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
    QJsonObject responseOverride;
    bool compact = false;
    QString sourceHash = QString(64, 'a');
    QUrl endpoint() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    ContentFixture() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto* socket = nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                const auto request = socket->readAll();
                if (!request.contains("\r\n\r\n")) return;
                if(request.startsWith("GET /catalogue/")){socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();return;}
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
                    body = oversized ? QByteArray(512 * 1024 + 100, 'x') : QJsonDocument(responseOverride.isEmpty() ? QJsonObject{
                        {"apiVersion", 1}, {"ready", ready}, {"sourceHash", sourceHash},
                        {"seriesInfo", info}, {"volumes", rows}} : responseOverride)
                        .toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
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
    void remote_cache_bounded_and_revision_preserved();
    void durable_cache_separate_process_restart();
    void durable_cache_expiry_and_failed_refresh();
    void durable_cache_rejects_invalid_input_data();
    void durable_cache_rejects_invalid_input();
    void durable_cache_retains_good_file_on_invalid_refresh();
    void durable_cache_source_change_preserves_other_series();
    void durable_cache_preserves_local_authority();
    void durable_cache_bounds_bytes_and_entries();
    void durable_cache_failed_atomic_write_retries();
    void durable_cache_corrupt_reload_preserves_memory();
    void durable_cache_preserves_unknown_identity();
    void durable_cache_accepts_maximum_valid_response();
    void durable_cache_retains_new_fetch_after_clock_skew();
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

void tst_tankoban_catalog::remote_cache_bounded_and_revision_preserved()
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
    // A source revision describes the fetched entry; a refresh of one series
    // must not erase unrelated last-known-good facts.
    cached = 0;
    for (int id = 1; id <= 33; ++id) if (cat.hasSeries(id)) ++cached;
    QCOMPARE(cached, 31);
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

namespace {
QJsonObject cacheDocument(const QUrl& endpoint)
{
    return {{"schemaVersion", 1}, {"apiVersion", 1}, {"endpoint", endpoint.toString(QUrl::FullyEncoded)},
            {"entries", QJsonArray{QJsonObject{
                {"malId", 13}, {"sourceHash", QString(64, 'a')},
                {"fetchedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"seriesInfo", QJsonObject{{"volumeCount", 1}, {"countBasis", "mal"}}},
                {"volumes", QJsonArray{QJsonObject{{"number", "01"}, {"cover", "https://fixture.invalid/one"}, {"name", "One"}}}}}}}};
}
QByteArray readBytes(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}
QJsonObject readCache(const QString& path)
{
    return QJsonDocument::fromJson(readBytes(path)).object();
}
int cacheChild(QCoreApplication& app)
{
    const auto args = app.arguments();
    if (args.size() != 5) return 20;
    const bool seed = args.at(2) == QStringLiteral("seed");
    QNetworkAccessManager nam;
    TankobanCatalog cat(QStringLiteral("intentionally-missing-catalogue.db"));
    cat.configureRemote(seed ? &nam : nullptr, QUrl(args.at(4)), args.at(3));
    bool complete = false;
    bool failed = false;
    QEventLoop loop;
    QObject::connect(&cat, &TankobanCatalog::seriesChanged, &loop, [&](int) { complete = true; loop.quit(); });
    QObject::connect(&cat, &TankobanCatalog::seriesFailed, &loop, [&](int, const QString&) { failed = true; loop.quit(); });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    cat.requestSeries(13);
    if (!complete && !failed) loop.exec();
    const QJsonObject output{{"ready", cat.ready()}, {"hasSeries", cat.hasSeries(13)},
        {"info", QJsonObject::fromVariantMap(cat.seriesInfo(13))},
        {"volumes", QJsonArray::fromVariantList(cat.volumes(13))}};
    QTextStream(stdout) << QJsonDocument(output).toJson(QJsonDocument::Compact) << '\n';
    return complete && !failed && !cat.ready() && cat.hasSeries(13) ? 0 : 21;
}
} // namespace

void tst_tankoban_catalog::durable_cache_separate_process_restart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("catalog-vault/tankoban_remote_v1.json");
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const auto endpoint = server.endpoint();
    const QJsonObject info{{"volumeCount", 3}, {"countBasis", "bookwalker"}};
    const QJsonArray rows{QJsonObject{{"number", "01"}, {"cover", "https://fixture.invalid/one.jpg"}, {"name", "Volume One"}},
        QJsonObject{{"number", "2.5"}, {"cover", ""}, {"name", "Extra α"}},
        QJsonObject{{"number", "Special"}, {"cover", "https://fixture.invalid/special"}, {"name", ""}}};
    auto injectedInfo = info;
    injectedInfo.insert("token", "DO-NOT-PERSIST-INFO");
    auto injectedRows = rows;
    auto first = injectedRows.first().toObject();
    first.insert("token", "DO-NOT-PERSIST-ROW");
    injectedRows[0] = first;
    server.responseOverride = {{"apiVersion", 1}, {"ready", true}, {"sourceHash", QString(64, 'a')},
        {"seriesInfo", injectedInfo}, {"volumes", injectedRows}, {"token", "DO-NOT-PERSIST-ROOT"}};
    QProcess seed;
    seed.start(QCoreApplication::applicationFilePath(), {"--cache-child", "seed", path, endpoint.toString()});
    QTRY_COMPARE_WITH_TIMEOUT(seed.state(), QProcess::NotRunning, 10000);
    QCOMPARE(seed.exitStatus(), QProcess::NormalExit);
    QCOMPARE(seed.exitCode(), 0);
    QCOMPARE(server.guests, 1);
    QCOMPARE(server.reads, 1);
    const auto saved = readBytes(path);
    QVERIFY(!saved.isEmpty());
    QVERIFY(!saved.contains("fixture-lease"));
    QVERIFY(!saved.contains("DO-NOT-PERSIST"));
    QVERIFY(!saved.contains("token"));
    const auto root = readCache(path);
    QCOMPARE(root.value("schemaVersion").toInt(), 1);
    QCOMPARE(root.value("apiVersion").toInt(), 1);
    const auto entry = root.value("entries").toArray().first().toObject();
    QCOMPARE(entry.value("malId").toInt(), 13);
    QCOMPARE(entry.value("sourceHash").toString(), QString(64, 'a'));
    QVERIFY(QDateTime::fromString(entry.value("fetchedAt").toString(), Qt::ISODateWithMs).isValid());
    server.close();
    for (bool stale : {false, true}) {
        if (stale) {
            auto expiredRoot = root;
            auto expiredEntry = entry;
            expiredEntry.insert("fetchedAt", QDateTime::currentDateTimeUtc().addSecs(-301).toString(Qt::ISODateWithMs));
            expiredRoot.insert("entries", QJsonArray{expiredEntry});
            QVERIFY(writeBytes(path, QJsonDocument(expiredRoot).toJson()));
        }
        QProcess offline;
        offline.start(QCoreApplication::applicationFilePath(), {"--cache-child", "offline", path, endpoint.toString()});
        QTRY_COMPARE_WITH_TIMEOUT(offline.state(), QProcess::NotRunning, 10000);
        QCOMPARE(offline.exitStatus(), QProcess::NormalExit);
        QCOMPARE(offline.exitCode(), 0);
        const auto result = QJsonDocument::fromJson(offline.readAllStandardOutput()).object();
        QCOMPARE(result.value("info").toObject(), info);
        QCOMPARE(result.value("volumes").toArray(), rows);
        QCOMPARE(result.value("ready"), QJsonValue(false));
        QCOMPARE(result.value("hasSeries"), QJsonValue(true));
    }
}

void tst_tankoban_catalog::durable_cache_expiry_and_failed_refresh()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    auto cached = cacheDocument(server.endpoint());
    auto entry = cached.value("entries").toArray().first().toObject();
    entry.insert("fetchedAt", QDateTime::currentDateTimeUtc().addSecs(-301).toString(Qt::ISODateWithMs));
    cached.insert("entries", QJsonArray{entry});
    const auto original = QJsonDocument(cached).toJson();
    QVERIFY(writeBytes(path, original));
    QNetworkAccessManager nam;
    TankobanCatalog cat("missing-catalogue.db");
    cat.configureRemote(&nam, server.endpoint(), path);
    QVERIFY(cat.hasSeries(13));
    const auto oldRows = cat.volumes(13);
    QSignalSpy failed(&cat, &TankobanCatalog::seriesFailed);
    QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
    server.status = 503;
    cat.requestSeries(13);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(server.reads, 1);
    QCOMPARE(cat.volumes(13), oldRows);
    QCOMPARE(readBytes(path), original);
    server.status = 200;
    cat.requestSeries(13);
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(server.reads, 2);
    QCOMPARE(cat.volumes(13).size(), 2);
    cat.requestSeries(13);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(server.reads, 2); // TTL starts at the successful fetch, not the failed one.
    cat.requestSeries(13, true);
    QTRY_COMPARE(changed.count(), 3);
    QCOMPARE(server.reads, 3);
    const auto refreshed = readCache(path).value("entries").toArray().first().toObject();
    QVERIFY(QDateTime::fromString(refreshed.value("fetchedAt").toString(), Qt::ISODateWithMs)
        .secsTo(QDateTime::currentDateTimeUtc()) < 5);
}

void tst_tankoban_catalog::durable_cache_rejects_invalid_input_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto* fault : {"partial", "schema", "api", "endpoint", "malId", "duplicateId", "hash", "timestamp",
             "futureTimestamp", "count", "duplicateVolume", "numberType", "oversize", "tooManyEntries", "nameSize"})
        QTest::newRow(fault) << QString::fromLatin1(fault);
}

void tst_tankoban_catalog::durable_cache_rejects_invalid_input()
{
    QFETCH(QString, fault);
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    const QUrl endpoint("https://catalogue.fixture.invalid");
    auto root = cacheDocument(endpoint);
    auto entry = root.value("entries").toArray().first().toObject();
    if (fault == "schema") root.insert("schemaVersion", 2);
    if (fault == "api") root.insert("apiVersion", 1.5);
    if (fault == "endpoint") root.insert("endpoint", "https://other.fixture.invalid");
    if (fault == "malId") entry.insert("malId", 13.5);
    if (fault == "hash") entry.insert("sourceHash", "not-a-revision");
    if (fault == "timestamp") entry.insert("fetchedAt", "not-a-date");
    if (fault == "futureTimestamp") entry.insert("fetchedAt", QDateTime::currentDateTimeUtc().addDays(1).toString(Qt::ISODateWithMs));
    if (fault == "count") entry.insert("seriesInfo", QJsonObject{{"volumeCount", 2.5}, {"countBasis", "mal"}});
    if (fault == "duplicateVolume") { auto rows = entry.value("volumes").toArray(); rows.append(rows.first()); entry.insert("volumes", rows); }
    if (fault == "numberType" || fault == "nameSize") {
        auto row = entry.value("volumes").toArray().first().toObject();
        if (fault == "numberType") row.insert("number", 1);
        else row.insert("name", QString(4097, 'x'));
        entry.insert("volumes", QJsonArray{row});
    }
    QJsonArray entries{entry};
    if (fault == "duplicateId") entries.append(entry);
    if (fault == "tooManyEntries") for (int id = 14; id <= 45; ++id) { auto extra = entry; extra.insert("malId", id); entries.append(extra); }
    root.insert("entries", entries);
    QByteArray bytes = QJsonDocument(root).toJson();
    if (fault == "partial") bytes.chop(20);
    if (fault == "oversize") bytes = QByteArray(4 * 1024 * 1024 + 1, ' ');
    QVERIFY(writeBytes(path, bytes));
    TankobanCatalog cat("missing-catalogue.db");
    cat.configureRemote(nullptr, endpoint, path);
    QVERIFY(!cat.hasSeries(13));
    QVERIFY(!cat.ready());
    QCOMPARE(readBytes(path), bytes); // Startup rejection never overwrites evidence or another good copy.
}

void tst_tankoban_catalog::durable_cache_retains_good_file_on_invalid_refresh()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QVERIFY(writeBytes(path, QJsonDocument(cacheDocument(server.endpoint())).toJson()));
    const auto original = readBytes(path);
    QNetworkAccessManager nam;
    TankobanCatalog cat("missing-catalogue.db");
    cat.configureRemote(&nam, server.endpoint(), path);
    QSignalSpy failed(&cat, &TankobanCatalog::seriesFailed);
    const auto oldRows = cat.volumes(13);
    server.responseOverride = {{"apiVersion", 1}, {"ready", true}, {"sourceHash", QString(64, 'b')},
        {"seriesInfo", QJsonObject{{"volumeCount", 1}, {"countBasis", "mal"}}},
        {"volumes", QJsonArray{QJsonObject{{"number", "1"}, {"cover", ""}, {"name", 42}}}}};
    cat.requestSeries(13, true);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(cat.volumes(13), oldRows);
    QCOMPARE(readBytes(path), original);
    server.oversized = true;
    cat.requestSeries(13, true);
    QTRY_COMPARE(failed.count(), 2);
    QCOMPARE(cat.volumes(13), oldRows);
    QCOMPARE(readBytes(path), original);
}

void tst_tankoban_catalog::durable_cache_source_change_preserves_other_series()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager nam;
    {
        TankobanCatalog cat("missing-catalogue.db");
        cat.configureRemote(&nam, server.endpoint(), path);
        QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
        cat.requestSeries(13);
        QTRY_COMPARE(changed.count(), 1);
        server.sourceHash = QString(64, 'b');
        cat.requestSeries(14);
        QTRY_COMPARE(changed.count(), 2);
        QVERIFY(cat.hasSeries(13));
        QVERIFY(cat.hasSeries(14));
    }
    TankobanCatalog restored("missing-catalogue.db");
    restored.configureRemote(nullptr, server.endpoint(), path);
    QVERIFY(restored.hasSeries(13));
    QVERIFY(restored.hasSeries(14));
    const auto entries = readCache(path).value("entries").toArray();
    QCOMPARE(entries.size(), 2);
    QSet<QString> hashes;
    for (const auto item : entries) hashes.insert(item.toObject().value("sourceHash").toString());
    QCOMPARE(hashes, (QSet<QString>{QString(64, 'a'), QString(64, 'b')}));
}

void tst_tankoban_catalog::durable_cache_preserves_local_authority()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    const auto db = dir.filePath("tankoban.db");
    const QUrl endpoint("https://catalogue.fixture.invalid");
    QVERIFY(writeBytes(path, QJsonDocument(cacheDocument(endpoint)).toJson()));
    QVERIFY(buildFixtureDb(db));
    QVERIFY(insertSeries(db, 13, 5, "bookwalker"));
    TankobanCatalog cat(db);
    cat.configureRemote(nullptr, endpoint, path);
    QCOMPARE(cat.seriesInfo(13).value("volumeCount").toInt(), 5);
    QCOMPARE(cat.volumes(13).size(), 5);
    QVERIFY(cat.ready());
    cat.closeForSwap();
    QCOMPARE(cat.seriesInfo(13).value("volumeCount").toInt(), 1);
    QCOMPARE(cat.volumes(13).first().toMap().value("number").toString(), QString("01"));
    QVERIFY(!cat.ready());
    QVERIFY(cat.reopen(db));
    QCOMPARE(cat.volumes(13).size(), 5);
}

void tst_tankoban_catalog::durable_cache_bounds_bytes_and_entries()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager nam;
    {
        TankobanCatalog cat("missing-catalogue.db");
        cat.configureRemote(&nam, server.endpoint(), path);
        QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
        for (int id = 1; id <= 33; ++id) {
            cat.requestSeries(id);
            QTRY_COMPARE(changed.count(), id);
        }
        QCOMPARE(readCache(path).value("entries").toArray().size(), 32);
        QVERIFY(!cat.hasSeries(1)); // Deterministic oldest-success eviction, not QHash order.
        QVERIFY(cat.hasSeries(33));
        QJsonArray rows;
        for (int n = 1; n <= 30; ++n) rows.append(QJsonObject{{"number", QString::number(n)}, {"cover", QString(6000, 'x')}, {"name", ""}});
        server.responseOverride = {{"apiVersion", 1}, {"ready", true}, {"sourceHash", QString(64, 'a')},
            {"seriesInfo", QJsonObject{{"volumeCount", 30}, {"countBasis", "mal"}}}, {"volumes", rows}};
        for (int id = 34; id <= 66; ++id) {
            cat.requestSeries(id);
            QTRY_COMPARE(changed.count(), id);
            QVERIFY(QFileInfo(path).size() <= 4 * 1024 * 1024);
        }
        const auto stored = readCache(path).value("entries").toArray();
        QVERIFY(stored.size() < 32);
        QVERIFY(!stored.isEmpty());
        QVERIFY(cat.hasSeries(66));
    }
    TankobanCatalog restored("missing-catalogue.db");
    restored.configureRemote(nullptr, server.endpoint(), path);
    QVERIFY(restored.hasSeries(66));
    QCOMPARE(restored.volumes(66).size(), 30);
}

void tst_tankoban_catalog::durable_cache_failed_atomic_write_retries()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QVERIFY(writeBytes(path, QJsonDocument(cacheDocument(server.endpoint())).toJson()));
    const auto original = readBytes(path);
    QNetworkAccessManager nam;
    TankobanCatalog cat("missing-catalogue.db");
    cat.configureRemote(&nam, server.endpoint(), path);
    QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
#ifdef Q_OS_UNIX
    const auto originalPermissions = QFile::permissions(dir.path());
    auto restorePermissions = qScopeGuard([&] { QFile::setPermissions(dir.path(), originalPermissions); });
    QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QFile probe(dir.filePath("permission-probe"));
    if (probe.open(QIODevice::WriteOnly)) QSKIP("Filesystem permits writes despite directory permissions");
    QTest::ignoreMessage(QtWarningMsg, "Tankoban catalogue cache could not be saved atomically");
    cat.requestSeries(13, true);
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(cat.volumes(13).size(), 2); // Valid new facts remain available in memory.
    QCOMPARE(readBytes(path), original); // No truncation or direct-write fallback.
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"cache.json"});
    cat.configureRemote(&nam, server.endpoint(), path);
    QCOMPARE(cat.volumes(13).size(), 2); // Older valid disk data cannot roll memory back.
    QVERIFY(QFile::setPermissions(dir.path(), originalPermissions));
#else
    // A directory occupying the destination deterministically fails QSaveFile
    // on platforms where directory mode bits do not enforce write denial.
    QVERIFY(QFile::rename(path, dir.filePath("previous.json")));
    QVERIFY(QDir().mkdir(path));
    QTest::ignoreMessage(QtWarningMsg, "Tankoban catalogue cache could not be saved atomically");
    cat.requestSeries(13, true);
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(cat.volumes(13).size(), 2);
    QCOMPARE(readBytes(dir.filePath("previous.json")), original);
    QVERIFY(QDir().rmdir(path));
#endif
    cat.requestSeries(13, true);
    QTRY_COMPARE(changed.count(), 2);
    QVERIFY(readBytes(path) != original);
    const auto entry = readCache(path).value("entries").toArray().first().toObject();
    QCOMPARE(entry.value("volumes").toArray().size(), 2);
}

void tst_tankoban_catalog::durable_cache_corrupt_reload_preserves_memory()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    const QUrl endpoint("https://catalogue.fixture.invalid");
    QVERIFY(writeBytes(path, QJsonDocument(cacheDocument(endpoint)).toJson()));
    TankobanCatalog cat("missing-catalogue.db");
    cat.configureRemote(nullptr, endpoint, path);
    const auto rows = cat.volumes(13);
    QVERIFY(!rows.isEmpty());
    QVERIFY(writeBytes(path, "{\"schemaVersion\":"));
    cat.configureRemote(nullptr, endpoint, path);
    QCOMPARE(cat.volumes(13), rows);
    cat.configureRemote(nullptr, QUrl("https://different.fixture.invalid"), path);
    QVERIFY(!cat.hasSeries(13)); // Same file never crosses endpoint identity.
}

void tst_tankoban_catalog::durable_cache_preserves_unknown_identity()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const auto endpoint = server.endpoint();
    server.unknown = true;
    QNetworkAccessManager nam;
    {
        TankobanCatalog cat("missing-catalogue.db");
        cat.configureRemote(&nam, server.endpoint(), path);
        QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
        cat.requestSeries(99);
        QTRY_COMPARE(changed.count(), 1);
        QVERIFY(!cat.hasSeries(99));
        server.unknown = false;
        server.responseOverride = {{"apiVersion", 1}, {"ready", true}, {"sourceHash", QString(64, 'a')},
            {"seriesInfo", QJsonObject{{"volumeCount", 0}, {"countBasis", "mal"}}}, {"volumes", QJsonArray{}}};
        cat.requestSeries(2);
        QTRY_COMPARE(changed.count(), 2);
        QVERIFY(cat.hasSeries(2));
    }
    server.close();
    TankobanCatalog restored("missing-catalogue.db");
    restored.configureRemote(nullptr, endpoint, path);
    QVERIFY(!restored.hasSeries(99));
    QVERIFY(restored.seriesInfo(99).isEmpty());
    QVERIFY(restored.volumes(99).isEmpty());
    QVERIFY(restored.hasSeries(2));
    QVERIFY(restored.volumes(2).isEmpty());
    QVERIFY(!restored.ready());
}

void tst_tankoban_catalog::durable_cache_accepts_maximum_valid_response()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QJsonArray rows;
    for (int n = 1; n <= 65; ++n) rows.append(QJsonObject{{"number", QString::number(n)}, {"cover", QString(8000, 'x')}, {"name", ""}});
    server.compact = true;
    server.responseOverride = {{"apiVersion", 1}, {"ready", true}, {"sourceHash", QString(64, 'a')},
        {"seriesInfo", QJsonObject{{"volumeCount", 65}, {"countBasis", "mal"}}}, {"volumes", rows}};
    const int padding = 512 * 1024 - QJsonDocument(server.responseOverride).toJson(QJsonDocument::Compact).size();
    QVERIFY(padding > 0 && padding <= 4096);
    auto last = rows.last().toObject();
    last.insert("name", QString(padding, 'x'));
    rows[rows.size() - 1] = last;
    server.responseOverride.insert("volumes", rows);
    QCOMPARE(QJsonDocument(server.responseOverride).toJson(QJsonDocument::Compact).size(), 512 * 1024);
    QNetworkAccessManager nam;
    {
        TankobanCatalog cat("missing-catalogue.db");
        cat.configureRemote(&nam, server.endpoint(), path);
        QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
        cat.requestSeries(13);
        QTRY_COMPARE(changed.count(), 1);
    }
    TankobanCatalog restored("missing-catalogue.db");
    restored.configureRemote(nullptr, server.endpoint(), path);
    QCOMPARE(restored.volumes(13).size(), 65);
}

void tst_tankoban_catalog::durable_cache_retains_new_fetch_after_clock_skew()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("cache.json");
    ContentFixture server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    auto root = cacheDocument(server.endpoint());
    auto entry = root.value("entries").toArray().first().toObject();
    entry.insert("fetchedAt", QDateTime::currentDateTimeUtc().addSecs(100).toString(Qt::ISODateWithMs));
    QJsonArray entries;
    for (int id = 1; id <= 32; ++id) { entry.insert("malId", id); entries.append(entry); }
    root.insert("entries", entries);
    QVERIFY(writeBytes(path, QJsonDocument(root).toJson()));
    QNetworkAccessManager nam;
    TankobanCatalog cat("missing-catalogue.db");
    cat.configureRemote(&nam, server.endpoint(), path);
    QSignalSpy changed(&cat, &TankobanCatalog::seriesChanged);
    cat.requestSeries(100);
    QTRY_COMPARE(changed.count(), 1);
    QVERIFY(cat.hasSeries(100));
    QCOMPARE(readCache(path).value("entries").toArray().size(), 32);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--cache-child"))) return cacheChild(app);
    tst_tankoban_catalog test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_tankoban_catalog.moc"
