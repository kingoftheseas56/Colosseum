#include "server1/discovery/DhtLookup.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QThread>
#include <QUdpSocket>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using server1::discovery::DhtLookup;
const QString hash = QStringLiteral("0123456789abcdef0123456789abcdef01234567");

void expect(bool ok, const char *message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

bool until(const std::function<bool()> &condition, int timeout = 1200)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (condition()) return true;
        QThread::msleep(1);
    } while (timer.elapsed() < timeout);
    return condition();
}

void drainEvents()
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 30) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
}

void bind(QUdpSocket &socket)
{
    socket.setProxy(QNetworkProxy::NoProxy);
    expect(socket.bind(QHostAddress::LocalHost, 0), "bind local DHT fixture");
}

DhtLookup::Endpoint endpoint(const QUdpSocket &socket)
{
    return {QStringLiteral("127.0.0.1"), socket.localPort()};
}

QByteArray string(const QByteArray &value)
{
    return QByteArray::number(value.size()) + ':' + value;
}

QByteArray transaction(const QByteArray &query)
{
    const auto at = query.indexOf("1:t2:");
    expect(at >= 0, "outgoing KRPC query has a two-byte transaction id");
    return query.mid(at + 5, 2);
}

QByteArray compact(quint16 port, quint8 finalOctet = 9)
{
    QByteArray result;
    result.append(char(192)); result.append(char(0)); result.append(char(2));
    result.append(char(finalOctet)); result.append(char(port >> 8)); result.append(char(port));
    return result;
}

QByteArray contact(const QByteArray &id, const QUdpSocket &socket)
{
    QByteArray result = id;
    result.append(char(127)); result.append(char(0)); result.append(char(0)); result.append(char(1));
    result.append(char(socket.localPort() >> 8)); result.append(char(socket.localPort()));
    return result;
}

QByteArray response(const QByteArray &tx, const QByteArray &id,
                    const QByteArray &fields = {})
{
    return "d1:rd2:id" + string(id) + fields + "e1:t" + string(tx) + "1:y1:re";
}

void send(QUdpSocket &socket, const QNetworkDatagram &request, const QByteArray &body)
{
    expect(socket.writeDatagram(body, request.senderAddress(), request.senderPort()) == body.size(),
           "fixture sends DHT response");
}

void bootstrap(DhtLookup &, QUdpSocket &router, const QByteArray &id = QByteArray(20, 'a'))
{
    expect(until([&] { return router.hasPendingDatagrams(); }), "constructor starts DHT bootstrap");
    const auto request = router.receiveDatagram();
    expect(request.data().contains("1:q9:find_node"), "bootstrap is find_node, not a user lookup");
    send(router, request, response(transaction(request.data()), id));
    drainEvents();
}

void compactPeers()
{
    QUdpSocket router;
    bind(router);
    DhtLookup lookup(hash, {endpoint(router)});
    bootstrap(lookup, router);
    std::vector<std::string> peers;
    lookup.onPeer = [&](std::string peer) { peers.push_back(std::move(peer)); };
    drainEvents();
    expect(!router.hasPendingDatagrams(), "bootstrap does not autonomously start a user lookup");
    lookup.start();
    expect(until([&] { return router.hasPendingDatagrams(); }), "start sends get_peers");
    const auto query = router.receiveDatagram();
    expect(query.data().contains("1:q9:get_peers"), "query is get_peers");
    expect(query.data().contains("9:info_hash20:" + QByteArray::fromHex(hash.toLatin1())),
           "query carries the raw 20-byte info hash");
    expect(query.data().contains("2:id20:"), "query carries a 20-byte node id");
    const auto tx = transaction(query.data());
    const auto fields = "6:valuesl" + string(compact(6881)) + string(compact(6881))
        + string(compact(0)) + "e";
    send(router, query, response(tx, QByteArray(20, 'a'), fields));
    expect(until([&] { return peers.size() == 2; }), "compact peers delivered, zero port ignored");
    expect(peers == std::vector<std::string>{"192.0.2.9:6881", "192.0.2.9:6881"},
           "source preserves duplicate peer events for coordinator statistics");
    send(router, query, response(tx, QByteArray(20, 'a'), fields));
    drainEvents();
    expect(peers.size() == 2, "replayed transaction cannot emit peers");
    expect(!lookup.active(), "completed lookup becomes idle without recurring schedule");
}

void iterativeNodes()
{
    QUdpSocket router, node;
    bind(router); bind(node);
    DhtLookup lookup(hash, {endpoint(router)});
    bootstrap(lookup, router);
    std::vector<std::string> peers;
    lookup.onPeer = [&](std::string peer) { peers.push_back(std::move(peer)); };
    lookup.start();
    expect(until([&] { return router.hasPendingDatagrams(); }), "router receives initial query");
    const auto first = router.receiveDatagram();
    const auto id = QByteArray(20, 'b');
    send(router, first, response(transaction(first.data()), QByteArray(20, 'a'),
                                "5:nodes" + string(contact(id, node))));
    expect(until([&] { return node.hasPendingDatagrams(); }), "lookup follows compact node contacts");
    const auto next = node.receiveDatagram();
    send(node, next, response(transaction(next.data()), id,
                             "6:valuesl" + string(compact(51413)) + "e"));
    expect(until([&] { return peers.size() == 1; }), "iterative lookup emits discovered peers");
    expect(peers.front() == "192.0.2.9:51413", "iterative compact peer decoding");
    drainEvents();
    expect(!router.hasPendingDatagrams() && !node.hasPendingDatagrams(),
           "lookup does not requery visited endpoints");
}

void rejectsUnsolicited()
{
    QUdpSocket router, wrongSender;
    bind(router);
    wrongSender.setProxy(QNetworkProxy::NoProxy);
    expect(wrongSender.bind(QHostAddress(QStringLiteral("127.0.0.2")), 0), "bind other-IP source fixture");
    DhtLookup lookup(hash, {endpoint(router)});
    bootstrap(lookup, router);
    int peers = 0;
    lookup.onPeer = [&](std::string) { ++peers; };
    lookup.start();
    expect(until([&] { return router.hasPendingDatagrams(); }), "transaction test initial query");
    const auto request = router.receiveDatagram();
    auto tx = transaction(request.data());
    auto wrongTx = tx;
    wrongTx[0] = char(quint8(wrongTx[0]) ^ 0x80);
    const auto fields = "6:valuesl" + string(compact(6881)) + "e";
    send(router, request, response(wrongTx, QByteArray(20, 'a'), fields));
    send(wrongSender, request, response(tx, QByteArray(20, 'a'), fields));
    send(router, request, "d1:t" + string(tx) + "1:y1:r1:rd2:id99999999999999999999999999:xee");
    drainEvents();
    expect(peers == 0, "wrong transaction, wrong source address and malformed response never emit peers");
    send(router, request, response(tx, QByteArray(20, 'a'), fields));
    expect(until([&] { return peers == 1; }), "invalid responses cannot consume the valid transaction");
}

void cancelAndRestart()
{
    QUdpSocket router;
    bind(router);
    DhtLookup lookup(hash, {endpoint(router)});
    bootstrap(lookup, router);
    int peers = 0;
    lookup.onPeer = [&](std::string) { ++peers; lookup.cancel(); };
    lookup.start();
    expect(until([&] { return router.hasPendingDatagrams(); }), "cancel test initial query");
    const auto old = router.receiveDatagram();
    lookup.cancel();
    expect(!lookup.active(), "cancel immediately makes lookup inactive");
    const auto fields = "6:valuesl" + string(compact(6881)) + string(compact(6882)) + "e";
    send(router, old, response(transaction(old.data()), QByteArray(20, 'a'), fields));
    drainEvents();
    expect(peers == 0, "cancel suppresses already-in-flight responses");
    lookup.start();
    expect(until([&] { return router.hasPendingDatagrams(); }), "explicit restart is supported");
    const auto fresh = router.receiveDatagram();
    expect(transaction(old.data()) != transaction(fresh.data()), "restart does not reuse stale transaction ids");
    send(router, fresh, response(transaction(fresh.data()), QByteArray(20, 'a'), fields));
    expect(until([&] { return peers == 1; }), "restart receives peers");
    drainEvents();
    expect(peers == 1, "cancel from onPeer suppresses remaining response values");
}

void concurrencyAndErrors()
{
    std::vector<std::unique_ptr<QUdpSocket>> routers;
    std::vector<DhtLookup::Endpoint> endpoints;
    for (int i = 0; i < 12; ++i) {
        routers.push_back(std::make_unique<QUdpSocket>());
        bind(*routers.back()); endpoints.push_back(endpoint(*routers.back()));
    }
    DhtLookup lookup(hash, endpoints);
    const auto count = [&] {
        int total = 0;
        for (const auto &router : routers) total += router->hasPendingDatagrams();
        return total;
    };
    expect(until([&] { return count() == 10; }), "DHT concurrency reaches ten outstanding queries");
    drainEvents();
    expect(count() == 10, "DHT concurrency never exceeds ten outstanding queries");
    const auto first = routers.front()->receiveDatagram();
    send(*routers.front(), first,
         "d1:eli201e7:generice1:t" + string(transaction(first.data())) + "1:y1:ee");
    expect(until([&] { return routers[10]->hasPendingDatagrams(); }),
           "KRPC error releases a request slot for the next endpoint");
    expect(!routers[11]->hasPendingDatagrams(), "one completion permits only one replacement");
    lookup.close();
    drainEvents();
    expect(!routers[11]->hasPendingDatagrams(), "close drops unsent endpoints");
}

void identityAndClosestNodes()
{
    QUdpSocket router;
    bind(router);
    std::vector<std::unique_ptr<QUdpSocket>> candidates;
    QByteArray contacts;
    const auto target = QByteArray::fromHex(hash.toLatin1());
    for (int i = 1; i <= 24; ++i) {
        candidates.push_back(std::make_unique<QUdpSocket>());
        bind(*candidates.back());
        auto id = target;
        id[19] = char(quint8(id[19]) ^ i);
        contacts += contact(id, *candidates.back());
    }
    DhtLookup lookup(hash, {endpoint(router)});
    bootstrap(lookup, router, QByteArray(20, char(0xff)));
    int peers = 0;
    lookup.onPeer = [&](std::string) { ++peers; };
    lookup.start();
    expect(until([&] { return router.hasPendingDatagrams(); }), "closest test bootstrap query");
    const auto request = router.receiveDatagram();
    send(router, request, response(transaction(request.data()), QByteArray(20, char(0xff)),
                                   "5:nodes" + string(contacts)));
    expect(until([&] { return candidates[9]->hasPendingDatagrams(); }), "closest ten queried first");
    expect(!candidates[10]->hasPendingDatagrams(), "next closest waits for concurrency slot");
    for (int i = 0; i < 20; ++i) {
        expect(until([&] { return candidates[i]->hasPendingDatagrams(); }), "closest-20 frontier advances");
        const auto next = candidates[i]->receiveDatagram();
        if (i == 0) {
            send(*candidates[i], next, response(transaction(next.data()), QByteArray(20, 'x'),
                 "6:valuesl" + string(compact(6000)) + "e"));
        } else {
            auto id = target;
            id[19] = char(quint8(id[19]) ^ (i + 1));
            send(*candidates[i], next, response(transaction(next.data()), id));
        }
    }
    expect(until([&] { return !lookup.active(); }), "frontier finishes once closest nodes were queried");
    expect(peers == 0, "a discovered node's mismatching response id cannot emit peers");
    for (int i = 20; i < 24; ++i) {
        expect(!candidates[i]->hasPendingDatagrams(), "nodes outside closest twenty are not queried");
    }
}

void hostnameAndDeletion()
{
    QUdpSocket router;
    bind(router);
    auto lookup = std::make_unique<DhtLookup>(hash,
        std::vector<DhtLookup::Endpoint>{{QStringLiteral("localhost"), router.localPort()}});
    bootstrap(*lookup, router);
    int peers = 0;
    lookup->onPeer = [&](std::string) { ++peers; lookup.reset(); };
    lookup->start();
    expect(until([&] { return router.hasPendingDatagrams(); }), "bootstrap hostname resolves asynchronously");
    const auto request = router.receiveDatagram();
    send(router, request, response(transaction(request.data()), QByteArray(20, 'a'),
        "6:valuesl" + string(compact(6881)) + string(compact(6882)) + "e"));
    expect(until([&] { return peers == 1; }), "peer callback may delete its lookup owner");
    drainEvents();
    expect(!lookup && peers == 1, "destruction suppresses remaining peer events safely");
}

void inputAndTimeout()
{
    const auto defaults = DhtLookup::defaultBootstrapNodes();
    expect(defaults.size() == 3
           && defaults[0].host == QStringLiteral("router.bittorrent.com")
           && defaults[1].host == QStringLiteral("router.utorrent.com")
           && defaults[2].host == QStringLiteral("dht.transmissionbt.com")
           && defaults[0].port == 6881 && defaults[1].port == 6881 && defaults[2].port == 6881,
           "default bootstrap endpoints match the pinned oracle");
    QUdpSocket router;
    bind(router);
    for (const auto &bad : {QStringLiteral("abc"), QString(40, QChar('z')), hash + '0'}) {
        DhtLookup invalid(bad, {endpoint(router)});
        invalid.start();
        drainEvents();
        expect(!invalid.active() && !router.hasPendingDatagrams(), "invalid hash produces no network query");
    }
    DhtLookup empty(hash, std::vector<DhtLookup::Endpoint>{});
    empty.start();
    expect(!empty.active(), "explicit empty bootstrap remains offline");
    DhtLookup timeout(hash, {endpoint(router)});
    bootstrap(timeout, router);
    timeout.start();
    expect(until([&] { return router.hasPendingDatagrams(); }), "timeout fixture gets query");
    expect(until([&] { return !timeout.active(); }, 3000), "unresponsive request expires without retry loop");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    compactPeers(); iterativeNodes(); rejectsUnsolicited(); cancelAndRestart();
    concurrencyAndErrors(); identityAndClosestNodes(); hostnameAndDeletion(); inputAndTimeout();
    std::cout << "DHT controlled UDP contracts PASS\n";
    return 0;
}
