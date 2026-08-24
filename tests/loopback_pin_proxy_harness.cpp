// loopback_pin_proxy_harness.cpp — proves the CONNECT tunnel: client → proxy →
// a live pin from Ipv4PinStore → upstream, with bytes relayed both ways.
#include "../native/net/LoopbackPinProxy.h"
#include "../native/net/Ipv4PinStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

static int fails = 0;
#define CHECK(c,l) do{ if(!(c)){ ++fails; std::printf("FAIL: %s\n", l);} }while(0)

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid(), "temporary root created");

    const QByteArray BLOB(262144, 'A');
    QTcpServer upstream;
    CHECK(upstream.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0), "upstream started");
    const quint16 upPort = upstream.serverPort();
    QObject::connect(&upstream, &QTcpServer::newConnection, [&]{
        QTcpSocket* s = upstream.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, [s, &BLOB]{
            if (s->readAll().contains("PING")) {
                s->write(BLOB);
                s->disconnectFromHost();
            }
        });
    });

    Ipv4PinStore pinStore(QDir(temp.path()).filePath(QStringLiteral("pins.json")),
        [](const QString& host, Ipv4PinStore::LookupDone done) {
            done(host == QStringLiteral("fake.metahub")
                     ? QStringLiteral("127.0.0.1") : QString());
        });
    pinStore.refresh({QStringLiteral("fake.metahub")});
    CHECK(pinStore.pinForHost(QStringLiteral("fake.metahub")) == QStringLiteral("127.0.0.1"),
          "test pin is live before CONNECT");

    LoopbackPinProxy proxy(&pinStore);
    CHECK(proxy.start(), "proxy started");
    const quint16 pxPort = proxy.port();

    QTcpSocket client;
    QByteArray hdr, payload;
    bool established = false;
    QObject::connect(&client, &QTcpSocket::readyRead, [&]{
        if (!established) {
            hdr += client.readAll();
            const int end = hdr.indexOf("\r\n\r\n");
            if (hdr.contains("200") && end >= 0) {
                established = true;
                payload = hdr.mid(end + 4);
                client.write("PING");
            }
            return;
        }
        payload += client.readAll();
        if (payload.size() >= BLOB.size())
            qApp->quit();
    });
    QObject::connect(&client, &QTcpSocket::connected, [&]{
        client.write("CONNECT fake.metahub:" + QByteArray::number(upPort)
                     + " HTTP/1.1\r\n\r\n");
    });
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), pxPort);

    QTimer::singleShot(6000, [&]{
        ++fails;
        std::printf("FAIL: tunnel timed out\n");
        qApp->quit();
    });
    app.exec();

    CHECK(established, "CONNECT got 200 Established");
    CHECK(payload.size() == BLOB.size(), "full blob delivered (no truncation on close)");
    CHECK(payload == BLOB, "blob bytes intact");
    std::printf(fails ? "FAILS: %d\n" : "loopback_pin_proxy_harness: ALL PASS\n", fails);
    return fails;
}
