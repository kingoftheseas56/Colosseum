#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/ProfilePaths.h"
#include "account/RatingsReviewsStore.h"
#include "account/RatingsReviewsSyncAdapter.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncProtocol.h"
#include "account/SyncStateStore.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <optional>
#include <utility>

namespace {
constexpr auto kAccount = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDeviceA = "11111111-1111-4111-8111-111111111111";
constexpr auto kDeviceB = "22222222-2222-4222-8222-222222222222";

class PullFixtureTransport final : public AccountTransport {
public:
    using AccountTransport::AccountTransport;

    QList<SyncWireMutation> pullMutations;
    int sendCount = 0;
    int pushCount = 0;
    bool servedPull = false;

    void send(quint64 requestId, const AccountTransportRequest &request) override {
        ++sendCount;
        AccountTransportReply reply;
        reply.statusCode = 200;

        if (request.path.startsWith(QStringLiteral("/v1/sync/pull"))) {
            QJsonArray entries;
            if (!servedPull) {
                quint64 seq = 1;
                for (const SyncWireMutation &mutation : std::as_const(pullMutations)) {
                    entries.append(QJsonObject{
                        {QStringLiteral("server_seq"), QString::number(seq++)},
                        {QStringLiteral("won"), true},
                        {QStringLiteral("mutation"), syncWireMutationToJson(mutation)}});
                }
                servedPull = true;
            }
            reply.body.insert(QStringLiteral("server_time_ms"), QStringLiteral("10000"));
            reply.body.insert(QStringLiteral("entries"), entries);
            reply.body.insert(QStringLiteral("has_more"), false);
        } else if (request.path == QLatin1String("/v1/sync/push")) {
            ++pushCount;
            QJsonArray results;
            for (const QJsonValue &value : request.body.value(QStringLiteral("mutations")).toArray()) {
                const QJsonObject mutation = value.toObject();
                results.append(QJsonObject{
                    {QStringLiteral("mutation_id"), mutation.value(QStringLiteral("mutation_id"))},
                    {QStringLiteral("accepted"), true},
                    {QStringLiteral("server_seq"), QStringLiteral("100")},
                    {QStringLiteral("won"), true}});
            }
            reply.body.insert(QStringLiteral("server_time_ms"), QStringLiteral("10000"));
            reply.body.insert(QStringLiteral("results"), results);
        } else {
            reply.body.insert(QStringLiteral("server_time_ms"), QStringLiteral("10000"));
            reply.body.insert(QStringLiteral("entries"), QJsonArray());
            reply.body.insert(QStringLiteral("has_more"), false);
        }

        QTimer::singleShot(0, this, [this, requestId, reply]() {
            emit finished(requestId, reply);
        });
    }
};
ProfilePaths accountProfile(QTemporaryDir *temp) {
    const auto profile = ProfilePaths::account(
        QString::fromLatin1(kAccount), temp->path());
    if (!profile.has_value())
        qFatal("invalid account fixture");
    if (!QDir().mkpath(profile->profileRoot()))
        qFatal("could not create account fixture");
    return *profile;
}

RatingsReviewsStore::Identity identity(const QString &mediaId = QStringLiteral("fixture-series")) {
    return RatingsReviewsStore::Identity{
        QStringLiteral("theatre"),
        QStringLiteral("series"),
        mediaId};
}

SyncWireMutation remoteDelete(
    const QString &key,
    qint64 deletedAtMs,
    quint64 counter = 0) {
    SyncWireMutation mutation;
    mutation.mutationId = QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc");
    mutation.deviceId = QString::fromLatin1(kDeviceB);
    mutation.category = QStringLiteral("ratings_reviews");
    mutation.recordKey = key;
    mutation.schemaVersion = 1;
    mutation.hlc = SyncWireHlc{9000, counter, mutation.deviceId};
    mutation.operation = SyncWireOperation::Delete;
    mutation.deletedAtMs = deletedAtMs;
    return mutation;
}
}

class tst_ratings_reviews_sync final : public QObject {
    Q_OBJECT

private slots:
    void wholeRecordExportAndFirewallBoundary();
    void explicitDeletePersistsExactEventTimeThroughOutboxRestart();
    void remoteDeleteAppliesExactTimestampWithoutEcho();
    void twoReplicaPutDeleteConverges();
};
void tst_ratings_reviews_sync::wholeRecordExportAndFirewallBoundary() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    qint64 now = 1000;
    RatingsReviewsStore store(
        QDir(temp.path()).filePath(QStringLiteral("ratings-reviews.json")),
        [&now]() { return now; });
    RatingsReviewsSyncAdapter adapter(&store);
    SyncAdapterRegistry registry;
    QVERIFY(registry.registerAdapter(&adapter));
    QVERIFY(!adapter.missingRecordsAreDeletes());
    QVERIFY(!adapter.remoteApplyEmitsLocalMutation());

    RatingsReviewsStore::CommitResult commit;
    QString error;
    QVERIFY2(store.saveLocal(
        identity(),
        std::optional<double>{8.5},
        std::optional<QString>{QStringLiteral("C:\\Notes\\review.txt")},
        true,
        &commit,
        &error), qPrintable(error));

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError registryError;
    QVERIFY2(registry.exportSnapshot(
        QStringLiteral("ratings_reviews"), &snapshot, &registryError),
        qPrintable(registryError.detail));
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(snapshot.tombstones.size(), 0);
    QCOMPARE(snapshot.tombstoneEventMs.size(), 0);
    const SyncAdapterRecord exported = snapshot.records.constFirst();
    QCOMPARE(exported.recordKey, commit.recordKey);
    QCOMPARE(exported.localOrderMs, qint64(1000));
    const QJsonObject payload = exported.payload.toObject();
    QCOMPARE(payload.value(QStringLiteral("review")).toString(),
             QStringLiteral("C:\\Notes\\review.txt"));
    QCOMPARE(payload.value(QStringLiteral("rating")).toDouble(), 8.5);
    QCOMPARE(payload.value(QStringLiteral("spoiler")).toBool(), true);
    QVERIFY(!payload.contains(QStringLiteral("provider_id")));
    QVERIFY(!payload.contains(QStringLiteral("access_token")));
    QVERIFY(!payload.contains(QStringLiteral("receipt_id")));
    QVERIFY(!payload.contains(QStringLiteral("publish")));

    now = 2000;
    QVERIFY2(store.saveLocal(
        identity(QStringLiteral("/home/me/opinion")),
        std::optional<double>{7.0},
        std::nullopt,
        false,
        nullptr,
        &error), qPrintable(error));
    QVERIFY(!registry.exportSnapshot(
        QStringLiteral("ratings_reviews"), &snapshot, &registryError));
    QCOMPARE(registryError.code, QStringLiteral("filesystem_path_value"));
}
void tst_ratings_reviews_sync::explicitDeletePersistsExactEventTimeThroughOutboxRestart() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile = accountProfile(&temp);
    qint64 now = 3000;

    RatingsReviewsStore store(profile.ratingsReviewsPath(), [&now]() { return now; });
    RatingsReviewsSyncAdapter adapter(&store);
    SyncAdapterRegistry registry;
    QVERIFY(registry.registerAdapter(&adapter));
    PullFixtureTransport transport;
    AccountClient client(&transport);
    client.setAccessToken(QByteArrayLiteral("fixture-access"));
    SyncEngine engine(&client, &registry, [&now]() { return now; });
    engine.setAutomaticSchedulingEnabled(false);
    engine.setNetworkEnabled(false);

    QString error;
    QVERIFY2(engine.start(profile, QString::fromLatin1(kDeviceA), &error), qPrintable(error));
    QVERIFY2(store.saveLocal(
        identity(), 9.0, QStringLiteral("durable"), false, nullptr, &error),
        qPrintable(error));
    QTRY_COMPARE(engine.pendingOutboxCount(), 1);

    now = 4000;
    QVERIFY2(store.saveLocal(
        identity(), 9.5, QStringLiteral("latest"), false, nullptr, &error),
        qPrintable(error));
    QTRY_COMPARE(engine.pendingOutboxCount(), 1);

    now = 4321;
    QVERIFY2(store.saveLocal(
        identity(), std::nullopt, std::nullopt, false, nullptr, &error),
        qPrintable(error));
    QTRY_COMPARE(engine.pendingOutboxCount(), 1);

    const auto manifest = engine.attachmentManifest(&error);
    QCOMPARE(manifest.size(), 1);
    QVERIFY(manifest.constFirst().mutation.deletedAtMs.has_value());
    QCOMPARE(*manifest.constFirst().mutation.deletedAtMs, qint64(4321));
    QVERIFY2(engine.stopPreservingOutbox(&error), qPrintable(error));

    SyncStateStore stateStore;
    const auto reopened = stateStore.load(profile.syncStatePath(), &error);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QCOMPARE(reopened->outbox.size(), 1);
    QVERIFY(reopened->outbox.constFirst().deletedAtMs.has_value());
    QCOMPARE(*reopened->outbox.constFirst().deletedAtMs, qint64(4321));
}

void tst_ratings_reviews_sync::remoteDeleteAppliesExactTimestampWithoutEcho() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile = accountProfile(&temp);
    qint64 now = 5000;

    RatingsReviewsStore store(profile.ratingsReviewsPath(), [&now]() { return now; });
    RatingsReviewsSyncAdapter adapter(&store);
    SyncAdapterRegistry registry;
    QVERIFY(registry.registerAdapter(&adapter));
    PullFixtureTransport transport;
    const QString key = RatingsReviewsStore::recordKeyForIdentity(identity());
    transport.pullMutations.append(remoteDelete(key, 7777));
    AccountClient client(&transport);
    client.setAccessToken(QByteArrayLiteral("fixture-access"));
    SyncEngine engine(&client, &registry, [&now]() { return now; });
    engine.setAutomaticSchedulingEnabled(false);
    engine.setNetworkEnabled(false);

    QSignalSpy localDirty(&store, &RatingsReviewsStore::syncDirty);
    QSignalSpy remoteApplied(&store, &RatingsReviewsStore::remoteApplied);
    QString error;
    QVERIFY2(engine.start(profile, QString::fromLatin1(kDeviceA), &error), qPrintable(error));
    engine.setNetworkEnabled(true);

    QTRY_COMPARE(remoteApplied.count(), 1);
    QCOMPARE(localDirty.count(), 0);
    const QJsonObject tombstones = store.tombstonesJson();
    QVERIFY(tombstones.contains(key));
    QCOMPARE(
        tombstones.value(key).toObject().value(QStringLiteral("deleted_at_ms")).toInteger(),
        qint64(7777));
    QCOMPARE(engine.pendingOutboxCount(), 0);
    QCOMPARE(transport.pushCount, 0);
}

void tst_ratings_reviews_sync::twoReplicaPutDeleteConverges() {
    QTemporaryDir leftTemp;
    QTemporaryDir rightTemp;
    QVERIFY(leftTemp.isValid());
    QVERIFY(rightTemp.isValid());
    qint64 leftNow = 6000;
    qint64 rightNow = 7000;
    RatingsReviewsStore left(
        QDir(leftTemp.path()).filePath(QStringLiteral("rr.json")),
        [&leftNow]() { return leftNow; });
    RatingsReviewsStore right(
        QDir(rightTemp.path()).filePath(QStringLiteral("rr.json")),
        [&rightNow]() { return rightNow; });
    RatingsReviewsSyncAdapter leftAdapter(&left);
    RatingsReviewsSyncAdapter rightAdapter(&right);
    SyncAdapterRegistry leftRegistry;
    SyncAdapterRegistry rightRegistry;
    QVERIFY(leftRegistry.registerAdapter(&leftAdapter));
    QVERIFY(rightRegistry.registerAdapter(&rightAdapter));

    QString error;
    RatingsReviewsStore::CommitResult commit;
    QVERIFY2(left.saveLocal(
        identity(), 10.0, QStringLiteral("../ending"), false, &commit, &error),
        qPrintable(error));

    SyncAdapterSnapshot leftSnapshot;
    SyncAdapterRegistryError registryError;
    QVERIFY(leftRegistry.exportSnapshot(
        QStringLiteral("ratings_reviews"), &leftSnapshot, &registryError));
    QCOMPARE(leftSnapshot.records.size(), 1);
    const QJsonObject payload = leftSnapshot.records.constFirst().payload.toObject();

    SyncAdapterMutation put;
    put.categoryId = QStringLiteral("ratings_reviews");
    put.recordKey = commit.recordKey;
    put.schemaVersion = 1;
    put.operation = SyncWireOperation::Put;
    put.payload = payload;
    QVERIFY2(rightRegistry.applyRemote(put, &registryError), qPrintable(registryError.detail));
    QCOMPARE(right.recordsJson(), left.recordsJson());

    leftNow = 8000;
    QVERIFY2(left.saveLocal(identity(), std::nullopt, std::nullopt, false, nullptr, &error),
             qPrintable(error));
    QVERIFY(leftRegistry.exportSnapshot(
        QStringLiteral("ratings_reviews"), &leftSnapshot, &registryError));
    QCOMPARE(leftSnapshot.tombstones.size(), 1);
    QVERIFY(leftSnapshot.tombstoneEventMs.contains(commit.recordKey));
    QCOMPARE(leftSnapshot.tombstoneEventMs.value(commit.recordKey), qint64(8000));

    SyncAdapterMutation del;
    del.categoryId = QStringLiteral("ratings_reviews");
    del.recordKey = commit.recordKey;
    del.schemaVersion = 1;
    del.operation = SyncWireOperation::Delete;
    del.deletedAtMs = leftSnapshot.tombstoneEventMs.value(commit.recordKey);
    QVERIFY2(rightRegistry.applyRemote(del, &registryError), qPrintable(registryError.detail));
    QCOMPARE(right.recordsJson(), left.recordsJson());
    QCOMPARE(right.tombstonesJson(), left.tombstonesJson());
}

QTEST_GUILESS_MAIN(tst_ratings_reviews_sync)
#include "tst_ratings_reviews_sync.moc"
