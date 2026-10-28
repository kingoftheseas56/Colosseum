#include "server1/discovery/DhtLookup.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
using server1::discovery::DhtLookup;
QJsonObject receipt;
const QString hash = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
void expect(bool ok, const char *message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
bool until(const std::function<bool()> &predicate, int ms = 1200) {
    QElapsedTimer elapsed; elapsed.start();
    do { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); if (predicate()) return true; QThread::msleep(1); } while (elapsed.elapsed() < ms);
    return predicate();
}
void settle() { until([] { return false; }, 35); }
void bind(QUdpSocket &socket) { socket.setProxy(QNetworkProxy::NoProxy); expect(socket.bind(QHostAddress::LocalHost, 0), "bind fixture"); }
QByteArray b(const QByteArray &data) { return QByteArray::number(data.size()) + ':' + data; }
QByteArray field(const QByteArray &data, const QByteArray &key, int size) {
    const auto prefix = b(key) + QByteArray::number(size) + ':';
    const auto at = data.indexOf(prefix); expect(at >= 0, "response/query field exists"); return data.mid(at + prefix.size(), size);
}
QByteArray tx(const QByteArray &data) { return field(data, "t", 2); }
QByteArray id(const QByteArray &data) { return field(data, "id", 20); }
QByteArray peer(quint16 port) { QByteArray p = QByteArray::fromHex("7f000001"); p.append(char(port >> 8)); p.append(char(port)); return p; }
QByteArray reply(const QByteArray &transaction, const QByteArray &node, const QByteArray &extra = {}) {
    return "d1:rd2:id" + b(node) + extra + "e1:t" + b(transaction) + "1:y1:re";
}
void send(QUdpSocket &socket, const QNetworkDatagram &request, const QByteArray &data) {
    expect(socket.writeDatagram(data, request.senderAddress(), request.senderPort()) == data.size(), "send fixture response");
}
QNetworkDatagram bootstrap(DhtLookup &, QUdpSocket &router) {
    expect(until([&] { return router.hasPendingDatagrams(); }), "constructor starts find_node bootstrap");
    const auto initial = router.receiveDatagram();
    expect(initial.data().contains("1:q9:find_node"), "initial bootstrap uses find_node");
    receipt["bootstrapMethod"] = QStringLiteral("find_node");
    expect(field(initial.data(), "target", 20) == id(initial.data()), "bootstrap targets persistent own id");
    send(router, initial, reply(tx(initial.data()), QByteArray(20, 'a'))); settle(); return initial;
}
QByteArray query(const QByteArray &transaction, const QByteArray &method, const QByteArray &arguments) {
    return "d1:ad2:id20:" + QByteArray(20, 'z') + arguments + "e1:q" + b(method) + "1:t" + b(transaction) + "1:y1:qe";
}
QNetworkDatagram request(QUdpSocket &client, const QNetworkDatagram &server, const QByteArray &body) {
    expect(client.writeDatagram(body, server.senderAddress(), server.senderPort()) == body.size(), "send inbound query");
    expect(until([&] { return client.hasPendingDatagrams(); }), "inbound query receives a response");
    return client.receiveDatagram();
}

void persistentAndOverlap() {
    QUdpSocket router; bind(router);
    DhtLookup service(hash, {{QStringLiteral("127.0.0.1"), router.localPort()}});
    int peers = 0; service.onPeer = [&](std::string) { ++peers; };
    const auto initial = bootstrap(service, router);
    const auto one = service.startLookup();
    expect(one != 0, "startLookup returns nonzero abort handle");
    expect(until([&] { return router.hasPendingDatagrams(); }), "first lookup uses known routing node");
    const auto first = router.receiveDatagram();
    const auto two = service.startLookup();
    expect(two != one, "overlapping lookup has distinct abort handle");
    expect(until([&] { return router.hasPendingDatagrams(); }), "overlapping lookup sends request");
    const auto second = router.receiveDatagram();
    expect(first.senderPort() == initial.senderPort() && second.senderPort() == initial.senderPort(), "source keeps its UDP socket");
    expect(id(first.data()) == id(initial.data()) && id(second.data()) == id(initial.data()), "source keeps its node identity");
    service.abortLookup(one);
    send(router, first, reply(tx(first.data()), QByteArray(20, 'a'), "6:valuesl" + b(peer(6000)) + "e"));
    send(router, second, reply(tx(second.data()), QByteArray(20, 'a'), "6:valuesl" + b(peer(6001)) + "e"));
    expect(until([&] { return peers == 1 && !service.active(); }), "aborting old lookup does not cancel newer lookup");
    receipt["overlapAbortPeers"] = peers;
    receipt["persistentNode"] = id(first.data()) == id(second.data());
    service.close();
    expect(service.startLookup() == 0, "closed service rejects later starts");
}

void inboundPeerService() {
    QUdpSocket router, client; bind(router); bind(client);
    DhtLookup service(hash, {{QStringLiteral("127.0.0.1"), router.localPort()}});
    const auto initial = bootstrap(service, router);
    const auto ping = request(client, initial, query("p1", "ping", {}));
    expect(tx(ping.data()) == "p1" && id(ping.data()) == id(initial.data()), "ping preserves transaction and service identity");
    const auto found = request(client, initial, query("f1", "find_node", "6:target20:" + QByteArray(20, 'a')));
    expect(found.data().contains("5:nodes"), "find_node returns routing contacts");
    const auto get = request(client, initial, query("g1", "get_peers", "9:info_hash20:" + QByteArray::fromHex(hash.toLatin1())));
    const auto token = field(get.data(), "token", 20);
    const auto rejected = request(client, initial, query("a0", "announce_peer", "9:info_hash20:" + QByteArray::fromHex(hash.toLatin1()) + "4:porti6000e5:token20:" + QByteArray(20, 'x')));
    expect(rejected.data().contains("1:y1:e") && rejected.data().contains("bad token"), "announce with invalid token is rejected");
    const auto announce = request(client, initial, query("a1", "announce_peer", "9:info_hash20:" + QByteArray::fromHex(hash.toLatin1()) + "4:porti6000e5:token" + b(token)));
    expect(announce.data().contains("1:y1:r"), "valid announced peer is accepted");
    const auto cached = request(client, initial, query("g2", "get_peers", "9:info_hash20:" + QByteArray::fromHex(hash.toLatin1())));
    expect(cached.data().contains("6:valuesl" + b(peer(6000))), "get_peers serves cached announced peer");
    std::vector<std::string> delivered; service.onPeer = [&](std::string p) { delivered.push_back(std::move(p)); };
    const auto lookup = service.startLookup(); service.abortLookup(lookup);
    expect(until([&] { return !delivered.empty(); }), "lookup emits cached local peer on next turn, as oracle");
    expect(delivered.front() == "127.0.0.1:6000", "cached lookup peer is decoded");
    receipt["cachedPeerAfterAbort"] = int(delivered.size());
    service.close();
}

void tokenRotationAndPeerSampling() {
    QUdpSocket router, client; bind(router); bind(client);
    DhtLookup service(hash, {{QStringLiteral("127.0.0.1"), router.localPort()}});
    const auto initial = bootstrap(service, router);
    const auto target = QByteArray::fromHex(hash.toLatin1());
    const auto first = request(client, initial, query("g1", "get_peers", "9:info_hash" + b(target)));
    const auto token = field(first.data(), "token", 20);
    QTimer *rotation = nullptr;
    for (auto *timer : service.findChildren<QTimer *>()) if (timer->interval() == 300000) rotation = timer;
    expect(rotation, "service rotates token secrets at pinned five-minute interval");
    expect(QMetaObject::invokeMethod(rotation, "timeout", Qt::DirectConnection), "advance secret rotation fixture");
    const auto accepted = request(client, initial, query("a1", "announce_peer", "9:info_hash" + b(target) + "4:porti6000e5:token" + b(token)));
    expect(accepted.data().contains("1:y1:r"), "previous secret's token remains valid after one rotation");
    receipt["tokenFirstRotationAccepted"] = accepted.data().contains("1:y1:r");
    expect(QMetaObject::invokeMethod(rotation, "timeout", Qt::DirectConnection), "advance second secret rotation fixture");
    const auto rejected = request(client, initial, query("a2", "announce_peer", "9:info_hash" + b(target) + "4:porti6000e5:token" + b(token)));
    expect(rejected.data().contains("bad token"), "token expires after two rotations");
    receipt["tokenSecondRotationRejected"] = rejected.data().contains("bad token");
    const auto fresh = request(client, initial, query("g2", "get_peers", "9:info_hash" + b(target)));
    const auto newToken = field(fresh.data(), "token", 20);
    for (int port = 6001; port <= 6104; ++port) {
        const auto stored = request(client, initial, query("a3", "announce_peer", "9:info_hash" + b(target)
            + "4:porti" + QByteArray::number(port) + "e5:token" + b(newToken)));
        expect(stored.data().contains("1:y1:r"), "sample fixture adds unique peer");
    }
    const auto sample = request(client, initial, query("g3", "get_peers", "9:info_hash" + b(target)));
    const auto list = sample.data().indexOf("6:valuesl");
    expect(list >= 0, "peer sample is returned as compact values");
    const auto end = list + 9 + 100 * 8;
    expect(sample.data().mid(end, 2) == "ee", "peer cache samples at most one hundred peers");
    std::set<QByteArray> unique;
    for (int i = 0; i < 100; ++i) {
        expect(sample.data().mid(list + 9 + i * 8, 2) == "6:", "each sampled peer is compact IPv4");
        unique.insert(sample.data().mid(list + 11 + i * 8, 6));
    }
    expect(unique.size() == 100, "random sampling does not repeat peers");
    receipt["peerSampleSize"] = int((end - list - 9) / 8);
    receipt["peerSampleUnique"] = int(unique.size());
    const auto missing = request(client, initial, query("e1", "get_peers", {}));
    expect(missing.data().contains("`get_peers` missing required `a.info_hash` field"), "missing required query field follows oracle error");
}

void sourceResponseCorrelation() {
    QUdpSocket router, learned, alternate; bind(router); bind(learned); bind(alternate);
    DhtLookup service(hash, {{QStringLiteral("127.0.0.1"), router.localPort()}});
    const auto initial = bootstrap(service, router);
    int delivered = 0; service.onPeer = [&](std::string) { ++delivered; };
    send(learned, initial, reply("zz", QByteArray(20, 'u'), "6:valuesl" + b(peer(7000)) + "e"));
    settle();
    expect(delivered == 0, "unsolicited response does not emit peer values");
    (void) service.startLookup();
    expect(until([&] { return learned.hasPendingDatagrams(); }), "oracle learns routing id from unsolicited response");
    receipt["unexpectedResponseLearnsNode"] = learned.hasPendingDatagrams();
    expect(until([&] { return router.hasPendingDatagrams(); }), "known bootstrap remains in routing table");
    const auto queryToRouter = router.receiveDatagram();
    send(alternate, queryToRouter, reply(tx(queryToRouter.data()), QByteArray(20, 'a'), "6:valuesl" + b(peer(7001)) + "e"));
    expect(until([&] { return delivered == 1; }), "pinned KRPC correlates transaction and host, not source port");
    receipt["sameHostDifferentPortEmits"] = delivered;
    const auto queryToLearned = learned.receiveDatagram();
    send(learned, queryToLearned, "d1:rd6:valuesl" + b(peer(7002)) + "ee1:t" + b(tx(queryToLearned.data())) + "1:y1:re");
    expect(until([&] { return delivered == 2; }), "expected peer reply may omit id, matching pinned callback path");
    receipt["missingNodeIdReplyEmits"] = delivered;
    service.close();
}

void preciseTimeoutTicks() {
    QUdpSocket router; bind(router);
    DhtLookup service(hash, {{QStringLiteral("127.0.0.1"), router.localPort()}});
    bootstrap(service, router);
    const auto handle = service.startLookup();
    expect(handle && until([&] { return router.hasPendingDatagrams(); }), "timeout test starts lookup");
    QTimer *tick = nullptr;
    for (auto *timer : service.findChildren<QTimer *>()) if (timer->interval() == 500) tick = timer;
    expect(tick, "network timeout scans at pinned 500ms cadence");
    QJsonArray ticks;
    for (int i = 0; i < 4; ++i) {
        expect(QMetaObject::invokeMethod(tick, "timeout", Qt::DirectConnection), "advance request timeout tick");
        expect(service.active(), "request remains live while TTL4 counts down to zero");
        ticks.push_back(service.active());
    }
    expect(QMetaObject::invokeMethod(tick, "timeout", Qt::DirectConnection), "advance expiry tick");
    expect(!service.active(), "fifth timeout tick expires TTL-zero request");
    ticks.push_back(service.active()); receipt["timeoutActiveAfterTicks"] = ticks;
}

void routingEvictionRetry() {
    QUdpSocket router, contactClient, inspector; bind(router); bind(contactClient); bind(inspector);
    DhtLookup service(hash, {{QStringLiteral("127.0.0.1"), router.localPort()}});
    expect(until([&] { return router.hasPendingDatagrams(); }), "routing fixture receives bootstrap");
    const auto initial = router.receiveDatagram();
    const auto ownId = id(initial.data());
    auto nearId = ownId; nearId[19] = char(quint8(nearId[19]) ^ 1);
    send(router, initial, reply(tx(initial.data()), nearId)); settle();
    std::vector<QByteArray> ids;
    for (int i = 0; i < 21; ++i) {
        auto node = ownId;
        node[0] = char(quint8(node[0]) ^ 0x80);
        node[18] = char(i >> 8); node[19] = char(i);
        ids.push_back(node);
        auto ping = query("p1", "ping", {});
        ping.replace(QByteArray(20, 'z'), node);
        const auto pong = request(contactClient, initial, ping);
        expect(pong.data().contains("1:y1:r"), "routing fixture peer ping accepted");
    }
    expect(until([&] { return contactClient.hasPendingDatagrams(); }), "full far bucket probes old contact");
    const auto first = contactClient.receiveDatagram();
    expect(first.data().contains("1:q4:ping"), "bucket capacity invokes ping eviction check");
    QTimer *tick = nullptr;
    for (auto *timer : service.findChildren<QTimer *>()) if (timer->interval() == 500) tick = timer;
    expect(tick, "routing fixture finds timeout tick");
    for (int i = 0; i < 5; ++i) QMetaObject::invokeMethod(tick, "timeout", Qt::DirectConnection);
    expect(until([&] { return contactClient.hasPendingDatagrams(); }), "failed old-node ping receives one retry");
    const auto retry = contactClient.receiveDatagram();
    send(contactClient, retry, reply(tx(retry.data()), ids[9])); settle();
    auto inspectId = ownId; inspectId[19] = char(quint8(inspectId[19]) ^ 2);
    auto find = query("f1", "find_node", "6:target" + b(ids[9]));
    find.replace(QByteArray(20, 'z'), inspectId);
    const auto nodes = request(inspector, initial, find);
    const bool oldRemoved = !nodes.data().contains(ids[9]);
    const bool newPresent = nodes.data().contains(ids[20]);
    expect(oldRemoved && newPresent, "pinned outer-error capture evicts first-failed node even after successful retry");
    receipt["routingEvictsAfterSuccessfulRetry"] = oldRemoved;
    receipt["routingKeepsNewCandidate"] = newPresent;
    service.close();
}

void cacheCapacityEviction() {
    QUdpSocket router, client; bind(router); bind(client);
    DhtLookup service(hash, {{QStringLiteral("127.0.0.1"), router.localPort()}});
    const auto initial = bootstrap(service, router);
    const auto tokenReply = request(client, initial, query("g0", "get_peers", "9:info_hash" + b(QByteArray(20, 'h'))));
    const auto token = field(tokenReply.data(), "token", 20);
    const auto valueFor = [](int i) { return QByteArray("value-") + QByteArray::number(i); };
    const auto valueKey = [&](int i) { return QCryptographicHash::hash(b(valueFor(i)), QCryptographicHash::Sha1); };
    const auto getValue = [&](int i) {
        return request(client, initial, query("g1", "get", "6:target" + b(valueKey(i)))).data();
    };
    for (int i = 0; i < 1000; ++i) request(client, initial, query("p1", "put", "5:token" + b(token) + "1:v" + b(valueFor(i))));
    expect(getValue(0).contains("1:v" + b(valueFor(0))), "value LRU fixture touches first key");
    request(client, initial, query("p1", "put", "5:token" + b(token) + "1:v" + b(valueFor(1000))));
    receipt["valueLruTouchedSurvives"] = getValue(0).contains("1:v" + b(valueFor(0)));
    receipt["valueLruOldestEvicted"] = !getValue(1).contains("1:v");
    receipt["valueLruNewestPresent"] = getValue(1000).contains("1:v" + b(valueFor(1000)));
    expect(receipt["valueLruTouchedSurvives"].toBool() && receipt["valueLruOldestEvicted"].toBool()
        && receipt["valueLruNewestPresent"].toBool(), "immutable values obey capacity1000 and access LRU");
    const auto peerKey = [](int i) {
        QByteArray key(20, '\0'); key[16] = char(i >> 24); key[17] = char(i >> 16); key[18] = char(i >> 8); key[19] = char(i); return key;
    };
    const auto getPeers = [&](int i) {
        return request(client, initial, query("g2", "get_peers", "9:info_hash" + b(peerKey(i)))).data();
    };
    for (int i = 0; i < 10000; ++i) request(client, initial, query("a1", "announce_peer", "9:info_hash" + b(peerKey(i)) + "4:porti6000e5:token" + b(token)));
    expect(getPeers(0).contains("6:values"), "peer LRU fixture touches first info hash");
    request(client, initial, query("a1", "announce_peer", "9:info_hash" + b(peerKey(10000)) + "4:porti6000e5:token" + b(token)));
    receipt["peerLruTouchedSurvives"] = getPeers(0).contains("6:values");
    receipt["peerLruOldestEvicted"] = !getPeers(1).contains("6:values");
    receipt["peerLruNewestPresent"] = getPeers(10000).contains("6:values");
    expect(receipt["peerLruTouchedSurvives"].toBool() && receipt["peerLruOldestEvicted"].toBool()
        && receipt["peerLruNewestPresent"].toBool(), "peer cache obeys capacity10000 and info-hash LRU");
}

void immutableValueService() {
    QUdpSocket router, client; bind(router); bind(client);
    DhtLookup service(hash, {{QStringLiteral("127.0.0.1"), router.localPort()}});
    const auto initial = bootstrap(service, router);
    const auto value = QByteArray("payload");
    const auto target = QCryptographicHash::hash(b(value), QCryptographicHash::Sha1);
    const auto before = request(client, initial, query("g1", "get", "6:target" + b(target)));
    const auto token = field(before.data(), "token", 20);
    const auto put = request(client, initial, query("u1", "put", "5:token" + b(token) + "1:v" + b(value)));
    expect(put.data().contains("1:y1:r"), "BEP44 immutable put accepted with token");
    const auto after = request(client, initial, query("g2", "get", "6:target" + b(target)));
    expect(after.data().contains("1:v" + b(value)), "BEP44 immutable get returns cached value");
    receipt["immutableValue"] = QString::fromUtf8(field(after.data(), "v", 7));
    const auto mutablePut = request(client, initial, query("m1", "put", "1:k32:" + QByteArray(32, 'k') + "3:sig64:" + QByteArray(64, 's') + "5:token" + b(token) + "1:v" + b(value)));
    expect(mutablePut.data().contains("verification not supported"), "default oracle rejects mutable data without verifier");
    receipt["mutableVerificationUnsupported"] = mutablePut.data().contains("verification not supported");
}
}
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    persistentAndOverlap(); inboundPeerService(); immutableValueService(); tokenRotationAndPeerSampling(); preciseTimeoutTicks(); sourceResponseCorrelation(); routingEvictionRetry(); cacheCapacityEviction();
    if (argc == 2 && std::string(argv[1]) == "--differential")
        std::cout << QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData() << '\n';
    else std::cout << "DHT persistent service contracts PASS\n";
}
