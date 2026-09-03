#include "EngineFsControlPlane.h"
#include "QtEngineFsTimerScheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QtTest>

#include <algorithm>
#include <limits>

using namespace Colosseum::Server::EngineFs;

namespace {

class FakeScheduler final : public IEngineFsTimerScheduler
{
public:
    TimerId schedule(std::chrono::milliseconds delay, std::function<void()> callback) override
    {
        const auto id = ++nextId_;
        tasks_.push_back({id, nowMs_ + delay.count(), std::move(callback), false});
        return id;
    }

    void cancel(TimerId id) override
    {
        for (auto& task : tasks_)
            if (task.id == id)
                task.cancelled = true;
    }

    void advance(std::chrono::milliseconds amount)
    {
        const qint64 target = nowMs_ + amount.count();
        for (;;) {
            int chosen = -1;
            qint64 due = std::numeric_limits<qint64>::max();
            for (int i = 0; i < tasks_.size(); ++i) {
                const auto& task = tasks_.at(i);
                if (!task.cancelled && task.dueMs <= target && task.dueMs < due) {
                    chosen = i;
                    due = task.dueMs;
                }
            }
            if (chosen < 0)
                break;
            nowMs_ = due;
            auto callback = std::move(tasks_[chosen].callback);
            tasks_[chosen].cancelled = true;
            callback();
        }
        nowMs_ = target;
    }

private:
    struct Task { TimerId id; qint64 dueMs; std::function<void()> callback; bool cancelled; };
    QVector<Task> tasks_;
    TimerId nextId_ = 0;
    qint64 nowMs_ = 0;
};
class FakeBackend final : public IEngineFsBackend
{
public:
    void resumeSwarm() override { ++resumeCount; snapshot.swarmPaused = false; }
    void pauseSwarm() override { ++pauseCount; snapshot.swarmPaused = true; }

    void destroy(std::function<void()> done) override
    {
        ++destroyCount;
        if (destroySynchronously)
            done();
        else
            pendingDestroy = std::move(done);
    }

    void whenReady(std::function<void(const QVariant&)> callback) override
    {
        if (ready)
            callback(readyPayload);
        else
            readyCallbacks.push_back(std::move(callback));
    }

    void setCallbacks(EngineFsBackendCallbacks value) override { callbacks = std::move(value); }
    EngineFsBackendSnapshot statisticsSnapshot() const override { return snapshot; }

    void completeReady(const QVariant& payload = {})
    {
        ready = true;
        readyPayload = payload;
        auto callbacksToRun = std::move(readyCallbacks);
        readyCallbacks.clear();
        for (const auto& callback : callbacksToRun)
            callback(readyPayload);
    }

    void completeDestroy()
    {
        auto done = std::move(pendingDestroy);
        if (done)
            done();
    }

    void emitError(const QString& message) { if (callbacks.onError) callbacks.onError(message); }
    void emitInvalidPiece(int piece) { if (callbacks.onInvalidPiece) callbacks.onInvalidPiece(piece); }

    EngineFsBackendSnapshot snapshot;
    EngineFsBackendCallbacks callbacks;
    QVector<std::function<void(const QVariant&)>> readyCallbacks;
    QVariant readyPayload;
    std::function<void()> pendingDestroy;
    bool ready = false;
    bool destroySynchronously = true;
    int resumeCount = 0;
    int pauseCount = 0;
    int destroyCount = 0;
};

class FakeFactory final : public IEngineFsBackendFactory
{
public:
    std::shared_ptr<IEngineFsBackend> create(const QString& canonicalHash,
                                             const QJsonObject&) override
    {
        ++createCount;
        auto backend = std::make_shared<FakeBackend>();
        backend->ready = readyByDefault;
        backends.insert(canonicalHash, backend);
        return backend;
    }

    std::shared_ptr<FakeBackend> at(const QString& hash) const
    {
        return backends.value(canonicalInfoHash(hash));
    }

    QHash<QString, std::shared_ptr<FakeBackend>> backends;
    bool readyByDefault = false;
    int createCount = 0;
};

EngineFsBackendSnapshot snapshotFromOracle(const QJsonObject& expected)
{
    EngineFsBackendSnapshot snapshot;
    snapshot.metadataReady = expected.contains(QStringLiteral("files"));
    if (expected.contains(QStringLiteral("name")))
        snapshot.torrentName = expected.value(QStringLiteral("name")).toString();
    snapshot.queued = expected.value(QStringLiteral("queued")).toInt();
    snapshot.uniquePeers = expected.value(QStringLiteral("unique")).toInt();
    snapshot.connectionTries = expected.value(QStringLiteral("connectionTries")).toInt();
    snapshot.swarmPaused = expected.value(QStringLiteral("swarmPaused")).toBool();
    snapshot.swarmConnections = expected.value(QStringLiteral("swarmConnections")).toInt();
    snapshot.swarmSize = expected.value(QStringLiteral("swarmSize")).toInt();
    for (const auto& value : expected.value(QStringLiteral("selections")).toArray())
        snapshot.selections.push_back(value.toObject());
    snapshot.downloaded = static_cast<qint64>(expected.value(QStringLiteral("downloaded")).toDouble());
    snapshot.uploaded = static_cast<qint64>(expected.value(QStringLiteral("uploaded")).toDouble());
    snapshot.downloadSpeed = expected.value(QStringLiteral("downloadSpeed")).toDouble();
    snapshot.uploadSpeed = expected.value(QStringLiteral("uploadSpeed")).toDouble();
    if (expected.contains(QStringLiteral("sources")))
        snapshot.peerSearchSources = expected.value(QStringLiteral("sources")).toArray();
    if (expected.contains(QStringLiteral("peerSearchRunning")))
        snapshot.peerSearchRunning = expected.value(QStringLiteral("peerSearchRunning")).toBool();

    for (const auto& value : expected.value(QStringLiteral("files")).toArray()) {
        QJsonObject object = value.toObject();
        EngineFsFileSnapshot file;
        file.name = object.take(QStringLiteral("name")).toString();
        file.length = static_cast<qint64>(object.take(QStringLiteral("length")).toDouble());
        file.offset = static_cast<qint64>(object.take(QStringLiteral("offset")).toDouble());
        file.path = object.take(QStringLiteral("path")).toString();
        file.extra = object;
        snapshot.files.push_back(file);
    }

    for (const auto& value : expected.value(QStringLiteral("wires")).toArray()) {
        const QJsonObject object = value.toObject();
        EngineFsWireSnapshot wire;
        wire.peerChoking = false;
        wire.requests = object.value(QStringLiteral("requests")).toInt();
        wire.address = object.value(QStringLiteral("address")).toString();
        wire.amInterested = object.value(QStringLiteral("amInterested")).toBool();
        wire.isSeeder = object.value(QStringLiteral("isSeeder")).toBool();
        wire.downSpeed = object.value(QStringLiteral("downSpeed")).toDouble();
        wire.upSpeed = object.value(QStringLiteral("upSpeed")).toDouble();
        snapshot.wires.push_back(wire);
    }
    while (snapshot.wires.size() < expected.value(QStringLiteral("peers")).toInt())
        snapshot.wires.push_back(EngineFsWireSnapshot{});
    snapshot.pieceLength = 262144;
    return snapshot;
}

QStringList eventNames(const QVector<EngineFsEvent>& events)
{
    QStringList names;
    for (const auto& event : events)
        names.push_back(event.name);
    return names;
}

EngineFsBackendSnapshot populatedSnapshot()
{
    EngineFsBackendSnapshot snapshot;
    snapshot.metadataReady = true;
    snapshot.torrentName = QStringLiteral("Sintel");
    snapshot.pieceLength = 4;

    EngineFsFileSnapshot file;
    file.length = 4;
    file.name = QStringLiteral("Sintel.mp4");
    file.offset = 3;
    file.path = QStringLiteral("Sintel\\Sintel.mp4");
    file.extra.insert(QStringLiteral("__cacheEvents"), true);
    snapshot.files.push_back(file);
    snapshot.availablePieces.insert(0);

    snapshot.selections.push_back(QJsonObject{{QStringLiteral("from"), 0},
                                               {QStringLiteral("to"), 3},
                                               {QStringLiteral("offset"), 1},
                                               {QStringLiteral("priority"), 0}});

    EngineFsWireSnapshot fast;
    fast.peerChoking = false;
    fast.requests = 2;
    fast.address = QStringLiteral("10.0.0.1:1");
    fast.amInterested = true;
    fast.downSpeed = 101.5;
    fast.upSpeed = 7.0;
    snapshot.wires.push_back(fast);

    EngineFsWireSnapshot choked;
    choked.peerChoking = true;
    choked.address = QStringLiteral("10.0.0.2:2");
    snapshot.wires.push_back(choked);

    EngineFsWireSnapshot seeder;
    seeder.peerChoking = false;
    seeder.address = QStringLiteral("10.0.0.3:3");
    seeder.isSeeder = true;
    snapshot.wires.push_back(seeder);

    snapshot.queued = 7;
    snapshot.uniquePeers = 9;
    snapshot.connectionTries = 11;
    snapshot.swarmPaused = true;
    snapshot.swarmConnections = 13;
    snapshot.swarmSize = 17;
    snapshot.downloaded = 19;
    snapshot.uploaded = 23;
    snapshot.downloadSpeed = 29.5;
    snapshot.uploadSpeed = 999.0;
    snapshot.peerSearchSources = QJsonArray{
        QJsonObject{{QStringLiteral("url"), QStringLiteral("dht:test")}}};
    snapshot.peerSearchRunning = true;
    return snapshot;
}

QJsonObject oracleResponseJson(const QString& recordName)
{
    const QDir sourceDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    QFile file(sourceDir.filePath(QStringLiteral("../../docs/research/aqueduct-wave0/oracle/golden/live.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& value : root.value(QStringLiteral("records")).toArray()) {
        const auto record = value.toObject();
        if (record.value(QStringLiteral("name")).toString() == recordName)
            return record.value(QStringLiteral("response")).toObject().value(QStringLiteral("json")).toObject();
    }
    return {};
}

} // namespace

class EngineFsControlPlaneTest : public QObject
{
    Q_OBJECT

private slots:
    void canonicalizesInfoHash();
    void registryCreateReuseRemoveAndFanout();
    void keepConcurrencyEvictsOnlyUnselectedOldEngines();
    void activityTimersPauseAndDestroyInSourceOrder();
    void statisticsMatchEngineFsSemantics();
    void oracleStatsSnapshotsMatchWave0();
    void qtTimerSchedulerFiresAndCancels();
};

void EngineFsControlPlaneTest::canonicalizesInfoHash()
{
    QCOMPARE(canonicalInfoHash(QStringLiteral("08ADA5A7A6183AAE1E09D831DF6748D566095A10")),
             QStringLiteral("08ada5a7a6183aae1e09d831df6748d566095a10"));
}

void EngineFsControlPlaneTest::registryCreateReuseRemoveAndFanout()
{
    FakeFactory factory;
    FakeScheduler scheduler;
    QVector<EngineFsEvent> events;
    EngineFsControlPlane plane(factory, scheduler, [&](const EngineFsEvent& event) {
        events.push_back(event);
    });

    const QString upper = QStringLiteral("08ADA5A7A6183AAE1E09D831DF6748D566095A10");
    const QString hash = canonicalInfoHash(upper);
    int readyCallbacks = 0;
    plane.createEngine(upper, QJsonObject{{QStringLiteral("marker"), 1}},
                       [&](const std::shared_ptr<IEngineFsBackend>&) { ++readyCallbacks; });

    QCOMPARE(factory.createCount, 1);
    QVERIFY(plane.exists(upper));
    QCOMPARE(plane.list(), QStringList{hash});
    auto backend = factory.at(hash);
    QCOMPARE(backend->resumeCount, 1);
    QCOMPARE(eventNames(events),
             QStringList({QStringLiteral("engine-create"),
                          QStringLiteral("engine-create:") + hash + QStringLiteral(":[object Object]"),
                          QStringLiteral("engine-created"),
                          QStringLiteral("engine-created:") + hash}));

    backend->completeReady(QVariant::fromValue(QJsonObject{{QStringLiteral("name"), QStringLiteral("Sintel")}}));
    QCOMPARE(readyCallbacks, 1);
    QCOMPARE(eventNames(events).sliced(4),
             QStringList({QStringLiteral("engine-ready:") + hash, QStringLiteral("engine-ready")}));

    events.clear();
    plane.createEngine(hash, QJsonObject{{QStringLiteral("marker"), 2}},
                       [&](const std::shared_ptr<IEngineFsBackend>&) { ++readyCallbacks; });
    QCOMPARE(factory.createCount, 1);
    QCOMPARE(backend->resumeCount, 2);
    QCOMPARE(readyCallbacks, 2);
    QCOMPARE(plane.statistics(hash).toObject().value(QStringLiteral("opts")).toObject()
                 .value(QStringLiteral("marker")).toInt(), 2);

    events.clear();
    backend->emitError(QStringLiteral("boom"));
    backend->emitInvalidPiece(42);
    QCOMPARE(eventNames(events),
             QStringList({QStringLiteral("engine-error:") + hash,
                          QStringLiteral("engine-error"),
                          QStringLiteral("engine-invalid-piece:") + hash,
                          QStringLiteral("engine-invalid-piece")}));

    events.clear();
    bool removed = false;
    plane.removeEngine(upper, [&] { removed = true; });
    QVERIFY(removed);
    QCOMPARE(backend->destroyCount, 1);
    QVERIFY(!plane.exists(hash));
    QCOMPARE(eventNames(events),
             QStringList({QStringLiteral("engine-destroyed"),
                          QStringLiteral("engine-destroyed:") + hash}));
}

void EngineFsControlPlaneTest::keepConcurrencyEvictsOnlyUnselectedOldEngines()
{
    FakeFactory factory;
    factory.readyByDefault = true;
    FakeScheduler scheduler;
    EngineFsControlPlane plane(factory, scheduler);

    plane.createEngine(QStringLiteral("A"), {});
    plane.createEngine(QStringLiteral("B"), {});
    plane.createEngine(QStringLiteral("C"), {});
    auto a = factory.at(QStringLiteral("a"));
    auto b = factory.at(QStringLiteral("b"));
    auto c = factory.at(QStringLiteral("c"));
    b->snapshot.selections.push_back(QJsonObject{{QStringLiteral("from"), 1}});

    bool done = false;
    plane.keepConcurrency(QStringLiteral("D"), 2, [&] { done = true; });

    QVERIFY(done);
    QVERIFY(!plane.exists(QStringLiteral("a")));
    QVERIFY(plane.exists(QStringLiteral("b")));
    QVERIFY(!plane.exists(QStringLiteral("c")));
    QCOMPARE(plane.list(), QStringList{QStringLiteral("b")});
    QCOMPARE(a->destroyCount, 1);
    QCOMPARE(b->destroyCount, 0);
    QCOMPARE(c->destroyCount, 1);
}

void EngineFsControlPlaneTest::activityTimersPauseAndDestroyInSourceOrder()
{
    FakeFactory factory;
    factory.readyByDefault = true;
    FakeScheduler scheduler;
    QVector<EngineFsEvent> events;
    EngineFsControlPlane plane(factory, scheduler, [&](const EngineFsEvent& event) {
        events.push_back(event);
    });

    const QString hash = QStringLiteral("abc123");
    plane.createEngine(hash, {});
    auto backend = factory.at(hash);
    events.clear();
    plane.noteStreamOpen(hash, 5);
    QCOMPARE(eventNames(events),
             QStringList({QStringLiteral("stream-open"),
                          QStringLiteral("stream-active"),
                          QStringLiteral("stream-active:abc123:5"),
                          QStringLiteral("engine-active"),
                          QStringLiteral("engine-active:abc123")}));

    events.clear();
    plane.noteStreamClose(hash, 5);
    scheduler.advance(std::chrono::seconds(10));
    plane.noteStreamOpen(hash, 5);
    scheduler.advance(std::chrono::seconds(200));
    QCOMPARE(eventNames(events), QStringList({QStringLiteral("stream-close"),
                                              QStringLiteral("stream-open")}));
    QVERIFY(plane.exists(hash));

    events.clear();
    plane.noteStreamClose(hash, 5);
    QCOMPARE(eventNames(events), QStringList{QStringLiteral("stream-close")});
    events.clear();
    scheduler.advance(std::chrono::milliseconds(19999));
    QVERIFY(events.isEmpty());
    scheduler.advance(std::chrono::milliseconds(1));
    QCOMPARE(eventNames(events),
             QStringList({QStringLiteral("stream-inactive"),
                          QStringLiteral("stream-inactive:abc123:5")}));
    QVERIFY(plane.exists(hash));

    events.clear();
    scheduler.advance(std::chrono::seconds(99));
    QVERIFY(events.isEmpty());
    scheduler.advance(std::chrono::seconds(1));
    QCOMPARE(eventNames(events),
             QStringList({QStringLiteral("engine-inactive"),
                          QStringLiteral("engine-destroyed"),
                          QStringLiteral("engine-destroyed:abc123"),
                          QStringLiteral("engine-inactive:abc123")}));
    QVERIFY(!plane.exists(hash));
    QCOMPARE(backend->destroyCount, 1);

    events.clear();
    plane.createEngine(hash, {});
    auto idleBackend = factory.at(hash);
    events.clear();
    plane.noteStreamCreated(hash, 5);
    plane.noteStreamCached(hash, 5);
    scheduler.advance(std::chrono::seconds(10));
    plane.noteStreamCreated(hash, 5);
    scheduler.advance(std::chrono::seconds(20));
    QVERIFY(events.isEmpty());
    QCOMPARE(idleBackend->pauseCount, 0);

    plane.noteStreamCached(hash, 5);
    scheduler.advance(std::chrono::seconds(20));
    QCOMPARE(eventNames(events),
             QStringList({QStringLiteral("engine-idle"),
                          QStringLiteral("engine-idle:abc123")}));
    QCOMPARE(idleBackend->pauseCount, 1);
    QVERIFY(plane.exists(hash));
}

void EngineFsControlPlaneTest::statisticsMatchEngineFsSemantics()
{
    FakeFactory factory;
    factory.readyByDefault = true;
    FakeScheduler scheduler;
    EngineFsControlPlane plane(factory, scheduler);
    const QString hash = QStringLiteral("feedbeef");
    plane.createEngine(hash, QJsonObject{{QStringLiteral("marker"), 2}});
    auto backend = factory.at(hash);
    backend->snapshot = populatedSnapshot();

    const QJsonObject stats = plane.statistics(hash).toObject();
    QCOMPARE(stats.value(QStringLiteral("infoHash")).toString(), hash);
    QCOMPARE(stats.value(QStringLiteral("name")).toString(), QStringLiteral("Sintel"));
    QCOMPARE(stats.value(QStringLiteral("peers")).toInt(), 3);
    QCOMPARE(stats.value(QStringLiteral("unchoked")).toInt(), 2);
    QCOMPARE(stats.value(QStringLiteral("queued")).toInt(), 7);
    QCOMPARE(stats.value(QStringLiteral("unique")).toInt(), 9);
    QCOMPARE(stats.value(QStringLiteral("connectionTries")).toInt(), 11);
    QCOMPARE(stats.value(QStringLiteral("swarmPaused")).toBool(), true);
    QCOMPARE(stats.value(QStringLiteral("swarmConnections")).toInt(), 13);
    QCOMPARE(stats.value(QStringLiteral("swarmSize")).toInt(), 17);
    QCOMPARE(stats.value(QStringLiteral("downloaded")).toInteger(), 19);
    QCOMPARE(stats.value(QStringLiteral("uploaded")).toInteger(), 23);
    QCOMPARE(stats.value(QStringLiteral("downloadSpeed")).toDouble(), 29.5);
    QCOMPARE(stats.value(QStringLiteral("uploadSpeed")).toDouble(), 29.5);
    QCOMPARE(stats.value(QStringLiteral("peerSearchRunning")).toBool(), true);
    QCOMPARE(stats.value(QStringLiteral("wires")).toArray().size(), 2);
    QCOMPARE(stats.value(QStringLiteral("sources")).toArray().size(), 1);
    QCOMPARE(stats.value(QStringLiteral("opts")).toObject().value(QStringLiteral("marker")).toInt(), 2);
    QVERIFY(stats.value(QStringLiteral("files")).toArray().at(0).toObject()
                .value(QStringLiteral("__cacheEvents")).toBool());

    const QJsonObject fileStats = plane.statistics(hash, 0).toObject();
    QVERIFY(fileStats.value(QStringLiteral("wires")).isNull());
    QCOMPARE(fileStats.value(QStringLiteral("streamLen")).toInteger(), 4);
    QCOMPARE(fileStats.value(QStringLiteral("streamName")).toString(), QStringLiteral("Sintel.mp4"));
    QCOMPARE(fileStats.value(QStringLiteral("streamProgress")).toDouble(), 1.0);

    const QJsonObject invalidFileStats = plane.statistics(hash, 99).toObject();
    QVERIFY(invalidFileStats.value(QStringLiteral("wires")).isNull());
    QVERIFY(!invalidFileStats.contains(QStringLiteral("streamLen")));

    QVERIFY(plane.statistics(QStringLiteral("missing")).isNull());
    QCOMPARE(plane.statisticsAll().keys(), QStringList{hash});

    backend->snapshot.metadataReady = false;
    const QJsonObject preMetadata = plane.statistics(hash).toObject();
    QVERIFY(!preMetadata.contains(QStringLiteral("name")));
    QVERIFY(!preMetadata.contains(QStringLiteral("files")));
}

void EngineFsControlPlaneTest::oracleStatsSnapshotsMatchWave0()
{
    const QString hash = QStringLiteral("08ada5a7a6183aae1e09d831df6748d566095a10");
    const QJsonObject expectedEngine = oracleResponseJson(QStringLiteral("engine-create"));
    QVERIFY2(!expectedEngine.isEmpty(), "Wave 0 live engine oracle must be readable");

    FakeFactory factory;
    factory.readyByDefault = true;
    FakeScheduler scheduler;
    EngineFsControlPlane plane(factory, scheduler);
    plane.createEngine(hash, expectedEngine.value(QStringLiteral("opts")).toObject());
    factory.at(hash)->snapshot = snapshotFromOracle(expectedEngine);
    QCOMPARE(plane.statistics(hash).toObject(), expectedEngine);

    const QJsonObject expectedFile = oracleResponseJson(QStringLiteral("stats-early-0"));
    QVERIFY2(!expectedFile.isEmpty(), "Wave 0 live file oracle must be readable");
    factory.at(hash)->snapshot = snapshotFromOracle(expectedFile);
    QCOMPARE(plane.statistics(hash, 5).toObject(), expectedFile);

    plane.removeEngine(hash);
    QVERIFY(plane.statistics(hash).isNull());
}

void EngineFsControlPlaneTest::qtTimerSchedulerFiresAndCancels()
{
    QtEngineFsTimerScheduler scheduler;
    int fired = 0;
    scheduler.schedule(std::chrono::milliseconds(0), [&] { ++fired; });
    QTRY_COMPARE_WITH_TIMEOUT(fired, 1, 1000);

    const TimerId cancelled = scheduler.schedule(std::chrono::milliseconds(1), [&] { ++fired; });
    scheduler.cancel(cancelled);
    QTest::qWait(20);
    QCOMPARE(fired, 1);
}


QTEST_GUILESS_MAIN(EngineFsControlPlaneTest)
#include "tst_enginefs_control_plane.moc"
