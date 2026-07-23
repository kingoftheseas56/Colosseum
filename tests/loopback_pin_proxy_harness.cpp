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

    // A large blob that spans many TCP segments — on "PING" the upstream sends it
    // ALL then IMMEDIATELY closes. The old eager-delete teardown truncated the tail
    // (unflushed client-write bytes discarded); the drain teardown must deliver every
    // byte. This is the regression guard for "Unsupported image format" (2026-07-24).
    const QByteArray BLOB(262144, 'A');

    // fake upstream: on "PING", write the blob then disconnect right away
    QTcpServer upstream;
    upstream.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0);
    const quint16 upPort = upstream.serverPort();
    QObject::connect(&upstream, &QTcpServer::newConnection, [&]{
        QTcpSocket* s = upstream.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, [s, &BLOB]{
            if (s->readAll().contains("PING")) { s->write(BLOB); s->disconnectFromHost(); }
        });
    });

    // proxy maps our fake host → 127.0.0.1 (stands in for the pinned IPv4)
    QHash<QString,QString> pins; pins.insert(QStringLiteral("fake.metahub"), QStringLiteral("127.0.0.1"));
    LoopbackPinProxy proxy(pins);
    CHECK(proxy.start(), "proxy started");
    const quint16 pxPort = proxy.port();

    // client: CONNECT, then PING, then accumulate the whole blob through the tunnel
    QTcpSocket client;
    QByteArray hdr, payload;
    bool established = false;
    QObject::connect(&client, &QTcpSocket::readyRead, [&]{
        if (!established) {
            hdr += client.readAll();
            const int end = hdr.indexOf("\r\n\r\n");
            if (hdr.contains("200") && end >= 0) {
                established = true;
                payload = hdr.mid(end + 4);   // any blob bytes that rode in after the 200
                client.write("PING");
            }
            return;
        }
        payload += client.readAll();
        if (payload.size() >= BLOB.size()) qApp->quit();
    });
    QObject::connect(&client, &QTcpSocket::connected, [&]{
        client.write("CONNECT fake.metahub:" + QByteArray::number(upPort) + " HTTP/1.1\r\n\r\n");
    });
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), pxPort);

    QTimer::singleShot(6000, [&]{ ++fails; std::printf("FAIL: tunnel timed out\n"); qApp->quit(); });
    app.exec();

    CHECK(established, "CONNECT got 200 Established");
    CHECK(payload.size() == BLOB.size(), "full blob delivered (no truncation on close)");
    CHECK(payload == BLOB, "blob bytes intact");
    std::printf(fails ? "FAILS: %d\n" : "loopback_pin_proxy_harness: ALL PASS\n", fails);
    return fails;
}
