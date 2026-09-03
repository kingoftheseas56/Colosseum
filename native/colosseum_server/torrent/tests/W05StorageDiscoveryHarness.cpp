#include "DiscoverySession.h"
#include "Storage.h"

#include <QCryptographicHash>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace colosseum::server::torrent;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QByteArray sha1Hex(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex();
}
StorageLayout crossFileLayout()
{
    StorageLayout layout;
    layout.pieceLength = 4;
    layout.verificationLength = 8;
    layout.pieceCount = 3;
    layout.files = {{0, 5}, {5, 5}};
    return layout;
}

void testNormalStore()
{
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory available");

    QSet<int> have{0, 1, 2};
    StorageHooks hooks;
    hooks.hasPiece = [&have](int piece) { return have.contains(piece); };

    NormalPieceStore store(temp.path(), crossFileLayout(), hooks);
    store.write(0, QByteArray("ABCD"));
    store.write(1, QByteArray("EFGH"));
    store.write(2, QByteArray("IJ"));

    const QVector<QByteArray> hashes{sha1Hex("ABCDEFGH"), sha1Hex("IJ")};
    const auto firstVerification = store.verify(0, hashes);
    require(firstVerification && firstVerification->success,
            "normal store verifies a real 8-byte piece from two virtual pieces");
    require(firstVerification->start == 0 && firstVerification->end == 2,
            "verification returns the virtual-piece span");
    const auto tailVerification = store.verify(2, hashes);
    require(tailVerification && tailVerification->success,
            "normal store verifies the final partial verification group");

    const QString secondPath = temp.filePath(QStringLiteral("redirected-file-1"));
    store.setDestination(1, secondPath);
    store.commit(0, 2);
    require(store.read(0) == QByteArray("ABCD"), "piece zero reads back from disk");
    require(store.read(1) == QByteArray("EFGH"), "cross-file piece reads back byte-for-byte");
    require(store.read(2) == QByteArray("IJ"), "tail piece uses the upstream remainder length");

    QFile first(temp.filePath(QStringLiteral("0")));
    require(first.open(QIODevice::ReadOnly), "default destination file exists");
    require(first.readAll() == QByteArray("ABCDE"), "first file receives its exact overlap");
    QFile second(secondPath);
    require(second.open(QIODevice::ReadOnly), "redirected destination file exists");
    require(second.readAll() == QByteArray("FGHIJ"), "second file receives its exact overlap");
}
void testNormalStoreMemoryLru()
{
    QTemporaryDir temp;
    QSet<int> have{0, 1};
    StorageLayout layout;
    layout.pieceLength = 4;
    layout.verificationLength = 4;
    layout.pieceCount = 2;
    layout.files = {{0, 8}};
    StorageHooks hooks;
    hooks.hasPiece = [&have](int piece) { return have.contains(piece); };

    NormalPieceStore store(temp.path(), layout, hooks, 4);
    store.write(0, QByteArray("ABCD"));
    store.commit(0, 0);
    require(store.memoryBufferSize() == 4, "committed cached piece remains until LRU pressure");
    store.write(1, QByteArray("EFGH"));
    require(store.memoryBufferSize() == 4, "oldest free cached piece is reclaimed under pressure");
    require(store.read(0) == QByteArray("ABCD"), "evicted memory piece remains available on disk");
}

void testCircularUncommittedProtection()
{
    QTemporaryDir temp;
    QSet<int> have{0, 1};
    QVector<int> reset;
    StorageHooks hooks;
    hooks.hasPiece = [&have](int piece) { return have.contains(piece); };
    hooks.resetPiece = [&reset](int piece) { reset.push_back(piece); };
    CircularPieceStore store(temp.path(), crossFileLayout(), hooks,
                             CircularStoreOptions{CircularStoreType::Memory, 4});
    store.write(0, QByteArray("ABCD"), 10);
    bool fullWasReported = false;
    try { store.write(1, QByteArray("EFGH"), 20); }
    catch (const std::runtime_error&) { fullWasReported = true; }
    require(fullWasReported, "an uncommitted circular piece is never an eviction victim");
    require(reset.isEmpty(), "uncommitted circular piece is not reset");
}

void testCircularStoreProtection()
{
    QTemporaryDir temp;
    QSet<int> have{0, 1, 2};
    QSet<int> selected{0};
    QSet<int> locked{1};
    QVector<int> reset;
    StorageHooks hooks;
    hooks.hasPiece = [&have](int piece) { return have.contains(piece); };
    hooks.isSelected = [&selected](int piece) { return selected.contains(piece); };
    hooks.isLocked = [&locked](int piece) { return locked.contains(piece); };
    hooks.resetPiece = [&reset, &have](int piece) { reset.push_back(piece); have.remove(piece); };

    CircularPieceStore store(temp.path(), crossFileLayout(), hooks,
                             CircularStoreOptions{CircularStoreType::Memory, 8});
    store.write(0, QByteArray("ABCD"), 100);
    store.commit(0, 0);
    store.write(1, QByteArray("EFGH"), 200);
    store.commit(1, 1);

    bool fullWasReported = false;
    try {
        store.write(2, QByteArray("IJ"), 300);
    } catch (const std::runtime_error&) {
        fullWasReported = true;
    }
    require(fullWasReported, "selected and locked pieces make a full circular store unfreeable");
    require(reset.isEmpty(), "protected pieces are never reset during failed eviction");

    selected.clear();
    store.write(2, QByteArray("IJ"), 300);
    require(reset == QVector<int>{0}, "oldest unprotected piece is reset before slot reuse");
    require(store.read(2) == QByteArray("IJ"), "replacement piece is readable");
    require(store.read(1) == QByteArray("EFGH"), "locked neighbor survives eviction");

    QTemporaryDir fsTemp;
    StorageLayout onePiece;
    onePiece.pieceLength = 4;
    onePiece.verificationLength = 4;
    onePiece.pieceCount = 1;
    onePiece.files = {{0, 4}};
    QSet<int> fsHave{0};
    StorageHooks fsHooks;
    fsHooks.hasPiece = [&fsHave](int piece) { return fsHave.contains(piece); };
    CircularPieceStore fsStore(fsTemp.path(), onePiece, fsHooks,
                               CircularStoreOptions{CircularStoreType::FileSystem, 4});
    fsStore.write(0, QByteArray("ABCD"), 10);
    require(fsStore.verify(0, {sha1Hex("ABCD")})->success,
            "circular store verifies bytes before commit");
    fsStore.commit(0, 0);
    require(fsStore.read(0) == QByteArray("ABCD"), "filesystem circular slot reads committed bytes");
    require(QFile::exists(fsTemp.filePath(QStringLiteral("pieces/0"))),
            "filesystem circular commit persists the piece file");
}
void testPeerDiscoveryPolicy()
{
    QStringList admitted;
    PeerDiscoveryCoordinator discovery(
        {QStringLiteral("tracker:https://tracker.test/announce"), QStringLiteral("dht:abc")},
        PeerSearchOptions{40, 150}, 0,
        [&admitted](const QString& address) { admitted.push_back(address); });

    require(discovery.isRunning(), "peer search starts immediately");
    auto stats = discovery.stats();
    require(stats.size() == 2, "tracker and DHT sources are retained");
    require(stats[0].numRequests == 1 && stats[1].numRequests == 0,
            "tracker starts immediately while DHT observes its 1500ms delay");

    discovery.advanceTo(1500);
    stats = discovery.stats();
    require(stats[1].numRequests == 1, "DHT lookup starts after 1500ms");

    discovery.reportPeer(0, QStringLiteral("1.2.3.4:6881"));
    discovery.reportPeer(1, QStringLiteral("1.2.3.4:6881"));
    stats = discovery.stats();
    require(stats[0].numFound == 1 && stats[0].numFoundUniq == 1,
            "first source gets the global unique-peer credit");
    require(stats[1].numFound == 1 && stats[1].numFoundUniq == 0,
            "second source counts the duplicate but not as globally unique");
    require(admitted.size() == 2, "every source peer event is forwarded to swarm admission");

    discovery.onSwarmState(151, false, 2000);
    require(!discovery.isRunning(), "queued peers above product max pause discovery");
    discovery.onSwarmState(39, false, 3000);
    require(discovery.isRunning(), "queued peers below min restart discovery");
    require(discovery.stats()[0].numRequests == 2, "restart immediately re-announces tracker");
    discovery.advanceTo(4500);
    require(discovery.stats()[1].numRequests == 2, "restart schedules a fresh delayed DHT lookup");

    discovery.advanceTo(30000);
    require(discovery.stats()[0].numRequests == 3, "running search refreshes tracker every 30 seconds");
    discovery.onSwarmState(100, true, 31000);
    require(!discovery.isRunning(), "swarm pause always pauses peer discovery");

    const QStringList defaults = PeerDiscoveryCoordinator::productDefaultSources(QStringLiteral("deadbeef"));
    require(defaults.size() == 21, "product default source set is 20 trackers plus DHT");
    require(defaults.last() == QStringLiteral("dht:deadbeef"), "product defaults append the torrent DHT source");
    const QStringList torrentSources = PeerDiscoveryCoordinator::sourcesForTorrent(
        {QStringLiteral("udp://torrent.example:80/announce")}, QStringLiteral("deadbeef"), defaults);
    require(torrentSources == QStringList{QStringLiteral("tracker:udp://torrent.example:80/announce"),
                                         QStringLiteral("dht:deadbeef")},
            "torrent announce URLs replace configured sources and retain DHT");
    require(PeerDiscoveryCoordinator::sourcesForTorrent({}, QStringLiteral("deadbeef"), defaults) == defaults,
            "configured product sources are used when torrent metadata has no announces");
    discovery.close();
}

void testSessionTimeoutsAndCap()
{
    const SwarmSessionSettings settings = SwarmSessionSettings::stremioProductDefaults();
    SwarmSessionPolicy connectTimeout(settings, 0);
    const QString connectPeer = QStringLiteral("10.1.0.1:6881");
    require(connectTimeout.addPeer(connectPeer), "connect-timeout peer admitted");
    require(connectTimeout.beginNextConnection() == connectPeer, "connect-timeout attempt starts");
    connectTimeout.advanceTo(2999);
    require(connectTimeout.hasPeer(connectPeer), "connect attempt survives before 3000ms timeout");
    connectTimeout.advanceTo(3000);
    require(!connectTimeout.hasPeer(connectPeer), "unestablished peer is removed at connect timeout");
    require(connectTimeout.takeTimedOutPeers() == QStringList{connectPeer},
            "connect timeout is observable to the transport adapter");

    SwarmSessionPolicy handshakeTimeout(settings, 0);
    const QString handshakePeer = QStringLiteral("10.1.0.2:6882");
    handshakeTimeout.addPeer(handshakePeer);
    require(handshakeTimeout.beginNextConnection() == handshakePeer, "handshake-timeout attempt starts");
    handshakeTimeout.onTransportConnected(handshakePeer);
    handshakeTimeout.advanceTo(19999);
    require(handshakeTimeout.hasPeer(handshakePeer), "connected peer survives before handshake timeout");
    handshakeTimeout.advanceTo(20000);
    require(!handshakeTimeout.hasPeer(handshakePeer), "peer is removed when handshake never arrives");

    SwarmSessionSettings oneConnection = settings;
    oneConnection.maxConnections = 1;
    SwarmSessionPolicy capped(oneConnection, 0);
    capped.addPeer(QStringLiteral("10.1.0.3:6883"));
    capped.addPeer(QStringLiteral("10.1.0.4:6884"));
    const auto first = capped.beginNextConnection();
    require(first.has_value(), "first capped connection starts");
    require(!capped.beginNextConnection().has_value(), "connection cap blocks a second in-flight attempt");
    capped.onConnectionClosed(*first, 0);
    require(capped.beginNextConnection().has_value(), "capacity reopens after connection teardown");
}

void testSwarmSessionPolicy()
{
    const SwarmSessionSettings settings = SwarmSessionSettings::stremioProductDefaults();
    require(settings.maxConnections == 55, "settings path supplies 55 max connections");
    require(settings.handshakeTimeoutMs == 20000, "handshake timeout matches product settings");
    require(settings.requestTimeoutMs == 4000, "request timeout matches product settings");
    require(settings.connectTimeoutMs == 3000, "p2p-swarm connect timeout remains 3000ms");
    require(settings.reconnectWaitMs == std::array<qint64, 3>{4000, 8000, 12000},
            "reconnect ladder matches p2p-swarm");

    SwarmSessionPolicy session(settings, 0);
    session.pause();
    require(session.addPeer(QStringLiteral("10.0.0.1:6881")), "valid peer enters session queue");
    require(session.addPeer(QStringLiteral("10.0.0.2:6882")), "second valid peer enters session queue");
    require(!session.addPeer(QStringLiteral("10.0.0.3:0")), "invalid port is rejected");
    require(!session.addPeer(QStringLiteral("10.0.0.1:6881")), "duplicate peer is rejected");
    session.setPriority(QStringLiteral("10.0.0.2:6882"), 2);
    require(!session.beginNextConnection().has_value(), "paused swarm starts no connection");

    session.resume();
    const auto first = session.beginNextConnection();
    require(first && *first == QStringLiteral("10.0.0.2:6882"),
            "highest numeric priority drains first");
    require(session.tries() == 1 && session.connectionCount() == 1,
            "connection attempt increments tries and active connection count");
    session.onHandshakeSucceeded(*first);
    session.setPeerChoking(*first, false);
    require(session.unchokedPeerCount() == 1, "unchoked peer accounting follows wire state");

    session.recordDownload(1000, 0);
    session.recordUpload(250, 0);
    require(session.downloadedBytes() == 1000 && session.uploadedBytes() == 250,
            "session byte counters accumulate wire traffic");
    require(session.downloadSpeed(0) > 0.0, "download speedometer observes traffic");
    require(session.downloadSpeed(6000) == 0.0, "five-second speed window decays to zero");

    session.onConnectionClosed(*first, 100);
    require(session.retryDueMs(*first) == 4100, "first reconnect waits four seconds");
    session.advanceTo(4099);
    require(session.queuedCount() == 1, "unrelated queued peer remains while retry waits");
    session.advanceTo(4100);
    session.setPriority(QStringLiteral("10.0.0.2:6882"), 3);
    const auto retryOne = session.beginNextConnection();
    require(retryOne && *retryOne == *first, "first retry becomes eligible at its due time");
    session.onConnectionClosed(*retryOne, 4200);
    require(session.retryDueMs(*retryOne) == 12200, "second reconnect waits eight seconds");
    session.advanceTo(12200);
    const auto retryTwo = session.beginNextConnection();
    require(retryTwo && *retryTwo == *first, "second retry re-enters the queue");
    session.onConnectionClosed(*retryTwo, 12300);
    require(session.retryDueMs(*retryTwo) == 24300, "third reconnect waits twelve seconds");
    session.advanceTo(24300);
    const auto retryThree = session.beginNextConnection();
    require(retryThree && *retryThree == *first, "third retry re-enters the queue");
    session.onConnectionClosed(*retryThree, 24400);
    require(!session.hasPeer(*retryThree), "peer is removed after reconnect ladder is exhausted");

    session.pause();
    require(session.isPaused(), "pause state is explicit");
    session.resume();
    require(!session.isPaused(), "resume state is explicit");
}
} // namespace

int main()
{
    testNormalStore();
    testNormalStoreMemoryLru();
    testCircularUncommittedProtection();
    testCircularStoreProtection();
    testPeerDiscoveryPolicy();
    testSessionTimeoutsAndCap();
    testSwarmSessionPolicy();
    std::cout << "w05_storage_discovery_harness PASS\n";
    return 0;
}
