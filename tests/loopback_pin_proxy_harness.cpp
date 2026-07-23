// loopback_pin_proxy_harness.cpp — proves the CONNECT tunnel: client → proxy →
// (pinned IPv4) upstream, bytes relayed both ways. Event-loop driven; a QTimer
// failsafe quits so a hang can't wedge CI.
#include "../native/net/LoopbackPinProxy.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <cstdio>

static int fails = 0;
#define CHECK(c,l) do{ if(!(c)){ ++fails; std::printf("FAIL: %s\n", l);} }while(0)

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // fake upstream: echo "PONG" when it receives "PING"
    QTcpServer upstream;
    upstream.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0);
    const quint16 upPort = upstream.serverPort();
    QObject::connect(&upstream, &QTcpServer::newConnection, [&]{
        QTcpSocket* s = upstream.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, [s]{
            if (s->readAll().contains("PING")) s->write("PONG");
        });
    });

    // proxy maps our fake host → 127.0.0.1 (stands in for the pinned IPv4)
    QHash<QString,QString> pins; pins.insert(QStringLiteral("fake.metahub"), QStringLiteral("127.0.0.1"));
    LoopbackPinProxy proxy(pins);
    CHECK(proxy.start(), "proxy started");
    const quint16 pxPort = proxy.port();

    // client speaks HTTP CONNECT to the proxy, then sends PING through the tunnel
    QTcpSocket client;
    QByteArray got;
    bool established = false;
    QObject::connect(&client, &QTcpSocket::readyRead, [&]{
        got += client.readAll();
        if (!established && got.contains("200")) {
            established = true; got.clear(); client.write("PING");
        } else if (established && got.contains("PONG")) {
            qApp->quit();
        }
    });
    QObject::connect(&client, &QTcpSocket::connected, [&]{
        client.write("CONNECT fake.metahub:" + QByteArray::number(upPort) + " HTTP/1.1\r\n\r\n");
    });
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), pxPort);

    QTimer::singleShot(4000, [&]{ ++fails; std::printf("FAIL: tunnel timed out\n"); qApp->quit(); });
    app.exec();

    CHECK(established, "CONNECT got 200 Established");
    std::printf(fails ? "FAILS: %d\n" : "loopback_pin_proxy_harness: ALL PASS\n", fails);
    return fails;
}
