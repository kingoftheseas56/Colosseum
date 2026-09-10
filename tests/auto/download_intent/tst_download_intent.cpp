// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/DownloadIntentStore.h"
#include "account/DownloadIntentSyncAdapter.h"
#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/ProfilePaths.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncProtocol.h"
#include "account/SyncStateStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {

constexpr auto kAccount = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDevice = "11111111-1111-4111-8111-111111111111";

class DownloadFixtureTransport final : public AccountTransport {
    Q_OBJECT
public:
    explicit DownloadFixtureTransport(QObject *parent = nullptr)
        : AccountTransport(parent) {}

    int requestCount() const { return m_requestCount; }
    QList<SyncWireMutation> pushed() const { return m_pushed; }

    void send(quint64 requestId, const AccountTransportRequest &request) override {
        ++m_requestCount;
        AccountTransportReply reply;
        reply.statusCode = 200;
        reply.body.insert(QStringLiteral("server_time_ms"), QStringLiteral("2000000"));
        if (request.method == QStringLiteral("POST")
            && request.path == QStringLiteral("/v1/sync/push")) {
            QJsonArray results;
            for (const QJsonValue &value : request.body.value(QStringLiteral("mutations")).toArray()) {
                const auto mutation = value.isObject()
                    ? syncWireMutationFromJson(value.toObject())
                    : std::nullopt;
                if (!mutation.has_value())
                    continue;
                m_pushed.append(*mutation);
                QJsonObject result;
                result.insert(QStringLiteral("mutation_id"), mutation->mutationId);
                result.insert(QStringLiteral("accepted"), true);
                result.insert(QStringLiteral("server_seq"), QString::number(++m_serverSequence));
                result.insert(QStringLiteral("won"), true);
                results.append(result);
            }
            reply.body.insert(QStringLiteral("results"), results);
        } else if (request.method == QStringLiteral("GET")
                   && request.path.startsWith(QStringLiteral("/v1/sync/pull?after="))) {
            reply.body.insert(QStringLiteral("entries"), QJsonArray());
            reply.body.insert(QStringLiteral("has_more"), false);
        } else {
            reply.statusCode = 404;
            reply.errorCode = QStringLiteral("fixture_route_missing");
        }
        emit finished(requestId, reply);
    }

private:
    int m_requestCount = 0;
    quint64 m_serverSequence = 0;
    QList<SyncWireMutation> m_pushed;
};

}

class tst_download_intent : public QObject {
    Q_OBJECT

private slots:
    void remoteIntentStoresLogicalIdentityWithoutAPath();
    void localDownloadRowsExportWithoutLocalOnlyFields();
    void cancellationPersistsAndBlocksStaleLocalRows();
    void freshProviderReappearanceRearmsAfterObservedAbsence();
    void repeatedCancellationDisarmsFreshIntentObservation();
    void deactivationRetainsProviderForReactivation();
    void cancellationBeforeEngineStartEmitsDeleteAndSurvivesRestart();
};

void tst_download_intent::remoteIntentStoresLogicalIdentityWithoutAPath() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    DownloadIntentStore store;
    QString error;
    QVERIFY2(store.activate(*profile, &error), qPrintable(error));
    QVERIFY2(store.applyRemote(
        QStringLiteral("theatre/movie-42"),
        SyncWireOperation::Put,
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("movie-42")},
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("Fixture Movie")}},
        1,
        &error), qPrintable(error));

    const QVariantList rows = store.records();
    QCOMPARE(rows.size(), 1);
    const QVariantMap row = rows.first().toMap();
    QCOMPARE(row.value(QStringLiteral("id")).toString(), QStringLiteral("movie-42"));
    QVERIFY(!row.contains(QStringLiteral("path")));
    QVERIFY(!row.contains(QStringLiteral("url")));
}

void tst_download_intent::localDownloadRowsExportWithoutLocalOnlyFields() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    DownloadIntentStore store;
    store.setLocalRecordProvider([] {
        return QVariantList{
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("movie-42")},
                {QStringLiteral("world"), QStringLiteral("theatre")},
                {QStringLiteral("kind"), QStringLiteral("movie")},
                {QStringLiteral("title"), QStringLiteral("Fixture Movie")},
                {QStringLiteral("path"), QStringLiteral("C:/Users/test/movie.mkv")},
                {QStringLiteral("url"), QStringLiteral("https://source.test/movie.mkv")},
                {QStringLiteral("headers"), QStringLiteral("secret")},
                {QStringLiteral("bytes"), 1234}}};
    });

    QString error;
    QVERIFY2(store.activate(*profile, &error), qPrintable(error));
    QVERIFY2(store.refreshFromLocal(&error), qPrintable(error));

    SyncAdapterExport snapshot;
    DownloadIntentSyncAdapter adapter(&store);
    QVERIFY2(adapter.exportSnapshot(&snapshot, &error), qPrintable(error));
    QCOMPARE(snapshot.records.size(), 1);
    QVERIFY(snapshot.records.first().payload.isObject());
    const QJsonObject payload = snapshot.records.first().payload.toObject();
    QVERIFY(!payload.contains(QStringLiteral("path")));
    QVERIFY(!payload.contains(QStringLiteral("url")));
    QVERIFY(!payload.contains(QStringLiteral("headers")));
    QVERIFY(!payload.contains(QStringLiteral("bytes")));
}

void tst_download_intent::cancellationPersistsAndBlocksStaleLocalRows() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    const QVariantMap row{
        {QStringLiteral("id"), QStringLiteral("movie-42")},
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Fixture Movie")}};

    DownloadIntentStore first;
    first.setLocalRecordProvider([row] { return QVariantList{row}; });
    QString error;
    QVERIFY2(first.activate(*profile, &error), qPrintable(error));
    QCOMPARE(first.records().size(), 1);
    QVERIFY2(first.cancel(QStringLiteral("theatre/movie-42"), &error), qPrintable(error));
    QCOMPARE(first.records().size(), 0);

    SyncAdapterExport cancelled;
    QVERIFY2(first.exportSnapshot(&cancelled, &error), qPrintable(error));
    QCOMPARE(cancelled.records.size(), 0);
    QCOMPARE(cancelled.tombstones, QList<QString>{QStringLiteral("theatre/movie-42")});

    // A provider row left over from a local queue must not resurrect a
    // durable cancellation after a refresh or process restart.
    QVERIFY2(first.refreshFromLocal(&error), qPrintable(error));
    QCOMPARE(first.records().size(), 0);

    DownloadIntentStore restarted;
    restarted.setLocalRecordProvider([row] { return QVariantList{row}; });
    QVERIFY2(restarted.activate(*profile, &error), qPrintable(error));
    QCOMPARE(restarted.records().size(), 0);
    QVERIFY2(restarted.remember(row, &error), qPrintable(error));
    QCOMPARE(restarted.records().size(), 1);
    SyncAdapterExport restored;
    QVERIFY2(restarted.exportSnapshot(&restored, &error), qPrintable(error));
    QVERIFY(restored.tombstones.isEmpty());
}

void tst_download_intent::freshProviderReappearanceRearmsAfterObservedAbsence() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QString::fromLatin1(kAccount),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    const QVariantMap row{
        {QStringLiteral("id"), QStringLiteral("movie-42")},
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Fixture Movie")}};
    QVariantList localRows{row};
    DownloadIntentStore store;
    store.setLocalRecordProvider([&localRows] { return localRows; });
    QString error;
    QVERIFY2(store.activate(*profile, &error), qPrintable(error));
    QVERIFY2(store.cancel(QStringLiteral("theatre/movie-42"), &error),
             qPrintable(error));

    // The stale provider row remains blocked immediately after cancellation.
    QVERIFY2(store.refreshFromLocal(&error), qPrintable(error));
    QCOMPARE(store.records().size(), 0);

    // A real physical cancellation removes the provider row first. Persist
    // that observation, then accept a later source-start reappearance.
    localRows.clear();
    QVERIFY2(store.refreshFromLocal(&error), qPrintable(error));
    QCOMPARE(store.records().size(), 0);
    QFile persisted(QDir(profile->profileRoot()).filePath(
        QStringLiteral("download-intents.json")));
    QVERIFY(persisted.open(QIODevice::ReadOnly));
    QVERIFY(!persisted.readAll().contains("provider_absent"));
    persisted.close();
    localRows = QVariantList{row};

    // The fresh intent is durable and survives a process restart without
    // needing to call the explicit redownload helper.
    DownloadIntentStore restarted;
    restarted.setLocalRecordProvider([&localRows] { return localRows; });
    QVERIFY2(restarted.activate(*profile, &error), qPrintable(error));
    // The absence observation is process-local. A restart must keep a stale
    // provider row blocked until this process observes absence again.
    QCOMPARE(restarted.records().size(), 0);
    localRows.clear();
    QVERIFY2(restarted.refreshFromLocal(&error), qPrintable(error));
    localRows = QVariantList{row};
    QVERIFY2(restarted.refreshFromLocal(&error), qPrintable(error));
    QCOMPARE(restarted.records().size(), 1);
    QCOMPARE(restarted.records().first().toMap(), row);

    SyncAdapterExport snapshot;
    QVERIFY2(restarted.exportSnapshot(&snapshot, &error), qPrintable(error));
    QVERIFY(snapshot.tombstones.isEmpty());
}

void tst_download_intent::repeatedCancellationDisarmsFreshIntentObservation() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QString::fromLatin1(kAccount),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    const QVariantMap row{
        {QStringLiteral("id"), QStringLiteral("movie-42")},
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Fixture Movie")}};
    QVariantList localRows{row};
    DownloadIntentStore store;
    store.setLocalRecordProvider([&localRows] { return localRows; });
    QString error;
    QVERIFY2(store.activate(*profile, &error), qPrintable(error));
    QVERIFY2(store.cancel(QStringLiteral("theatre/movie-42"), &error),
             qPrintable(error));
    localRows.clear();
    QVERIFY2(store.refreshFromLocal(&error), qPrintable(error));

    // A fresh source start was observed, then a newer DELETE arrives before
    // that start is accepted. The stale row must remain cancelled.
    QVERIFY2(store.cancel(QStringLiteral("theatre/movie-42"), &error),
             qPrintable(error));
    localRows = QVariantList{row};
    QVERIFY2(store.refreshFromLocal(&error), qPrintable(error));
    QCOMPARE(store.records().size(), 0);

    DownloadIntentStore restarted;
    restarted.setLocalRecordProvider([&localRows] { return localRows; });
    QVERIFY2(restarted.activate(*profile, &error), qPrintable(error));
    QCOMPARE(restarted.records().size(), 0);
}

void tst_download_intent::deactivationRetainsProviderForReactivation() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QString::fromLatin1(kAccount),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    const QVariantMap row{
        {QStringLiteral("id"), QStringLiteral("movie-42")},
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Fixture Movie")}};
    int providerCalls = 0;
    DownloadIntentStore store;
    store.setLocalRecordProvider([&providerCalls, &row] {
        ++providerCalls;
        return QVariantList{row};
    });
    QString error;
    QVERIFY2(store.activate(*profile, &error), qPrintable(error));
    QCOMPARE(store.records().size(), 1);
    const int callsBeforeDeactivate = providerCalls;

    store.deactivate();
    QVERIFY(!store.active());
    QVERIFY2(store.activate(*profile, &error), qPrintable(error));
    QVERIFY(providerCalls > callsBeforeDeactivate);
    QCOMPARE(store.records().size(), 1);
}

void tst_download_intent::cancellationBeforeEngineStartEmitsDeleteAndSurvivesRestart() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QString::fromLatin1(kAccount),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    const QVariantMap row{
        {QStringLiteral("id"), QStringLiteral("movie-42")},
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Fixture Movie")}};

    QString error;
    DownloadFixtureTransport transport;
    {
        DownloadIntentStore store;
        store.setLocalRecordProvider([row] { return QVariantList{row}; });
        QVERIFY2(store.activate(*profile, &error), qPrintable(error));
        QVERIFY2(store.refreshFromLocal(&error), qPrintable(error));
        QVERIFY2(store.cancel(QStringLiteral("theatre/movie-42"), &error), qPrintable(error));
        QCOMPARE(store.records().size(), 0);

        AccountClient client(&transport);
        client.setAccessToken(QByteArrayLiteral("fixture-access"));
        DownloadIntentSyncAdapter adapter(&store);
        SyncAdapterRegistry registry;
        SyncAdapterRegistryError registryError;
        QVERIFY2(registry.registerAdapter(&adapter, &registryError),
                 qPrintable(registryError.detail));
        SyncEngine engine(&client, &registry, [] { return qint64(2000000); });
        engine.setAutomaticSchedulingEnabled(false);
        engine.setNetworkEnabled(false);
        QVERIFY2(engine.start(*profile, QString::fromLatin1(kDevice), &error), qPrintable(error));

        // The cancellation predates the first mirror checkpoint. Starting
        // the engine must still place its tombstone on the wire as a DELETE.
        engine.setNetworkEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(transport.pushed().size() >= 1, 5000);
        const QList<SyncWireMutation> pushed = transport.pushed();
        const auto deleteIt = std::find_if(
            pushed.cbegin(), pushed.cend(), [](const SyncWireMutation &mutation) {
                return mutation.category == QStringLiteral("desired_download_intent")
                    && mutation.recordKey == QStringLiteral("theatre/movie-42")
                    && mutation.operation == SyncWireOperation::Delete;
            });
        QVERIFY(deleteIt != pushed.cend());
        QVERIFY2(engine.stopPreservingOutbox(&error), qPrintable(error));
    }

    DownloadIntentStore restarted;
    restarted.setLocalRecordProvider([row] { return QVariantList{row}; });
    QVERIFY2(restarted.activate(*profile, &error), qPrintable(error));
    QVERIFY2(restarted.refreshFromLocal(&error), qPrintable(error));
    QCOMPARE(restarted.records().size(), 0);
    SyncAdapterExport snapshot;
    DownloadIntentSyncAdapter restartedAdapter(&restarted);
    QVERIFY2(restartedAdapter.exportSnapshot(&snapshot, &error), qPrintable(error));
    QCOMPARE(snapshot.tombstones, QList<QString>{QStringLiteral("theatre/movie-42")});

    // Restarting the engine with the acknowledged DELETE and durable
    // tombstone must not create another mutation for the same intent.
    {
        AccountClient client(&transport);
        client.setAccessToken(QByteArrayLiteral("fixture-access"));
        SyncAdapterRegistry registry;
        SyncAdapterRegistryError registryError;
        QVERIFY2(registry.registerAdapter(&restartedAdapter, &registryError),
                 qPrintable(registryError.detail));
        SyncEngine engine(&client, &registry, [] { return qint64(2000000); });
        engine.setAutomaticSchedulingEnabled(false);
        engine.setNetworkEnabled(false);
        QVERIFY2(engine.start(*profile, QString::fromLatin1(kDevice), &error), qPrintable(error));
        engine.setNetworkEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state() == SyncEngine::State::Idle, 5000);
        QCOMPARE(transport.pushed().size(), 1);
        QVERIFY2(engine.stopPreservingOutbox(&error), qPrintable(error));
    }
}

QTEST_MAIN(tst_download_intent)
#include "tst_download_intent.moc"
