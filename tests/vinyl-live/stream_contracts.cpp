#include "player/streamserver.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QTest>

class tst_stream_server_scoped : public QObject {
    Q_OBJECT
private slots:
    void scopedManifestSelectionAndCancellation() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        bool hold = false;
        int creates = 0;
        QByteArray lastCreate;
        QByteArray manifestBody=QByteArrayLiteral("{\"files\":[{\"name\":\"cover.jpg\",\"length\":100},{\"name\":\"song.flac\",\"path\":\"album/song.flac\",\"length\":1234}]}");
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                    const auto request = socket->readAll();
                    if (!request.contains("HTTP/1.1")) return;
                    if (request.contains("/create")) { ++creates;lastCreate=request; if (hold) return; }
                    const QByteArray body = request.contains("/create")
                        ? manifestBody
                        : QByteArrayLiteral("{}");
                    socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
        StreamServer stream(server.serverPort(), nullptr);
        QSignalSpy manifest(&stream, &StreamServer::torrentManifestReady);
        QSignalSpy scoped(&stream, &StreamServer::torrentStreamReady);
        QSignalSpy errors(&stream, &StreamServer::torrentRequestError);
        QSignalSpy legacy(&stream, &StreamServer::streamReady);
        QSignalSpy fetch(&stream, &StreamServer::fetchReady);
        QSignalSpy globalErrors(&stream, &StreamServer::streamError);
        const QString hash(40, QLatin1Char('a'));
        stream.requestManifest("manifest", hash);
        QTRY_COMPARE(manifest.size(), 1);
        const auto files = manifest.at(0).at(2).toList();
        QCOMPARE(files.size(), 2);
        QCOMPARE(files.at(1).toMap().value("fileIdx").toInt(), 1);
        QCOMPARE(files.at(1).toMap().value("path").toString(), QString("album/song.flac"));
        stream.requestFile("manifest", hash, 1);
        QTRY_COMPARE(scoped.size(), 1);
        QCOMPARE(scoped.at(0).at(1).toString(), QString("http://127.0.0.1:%1/").arg(server.serverPort()) + hash + "/1");
        stream.requestFile("manifest", hash, 2);
        QTRY_COMPARE(errors.size(), 1);
        hold = true;
        const int before = creates;
        stream.requestManifest("cancelled", hash);
        QTRY_COMPARE(creates, before + 1);
        stream.cancelRequest("cancelled");
        QTest::qWait(1100);
        QCOMPARE(creates, before + 1);
        QCOMPARE(manifest.size(), 1);
        QCOMPARE(errors.size(), 1);
        hold = false;
        stream.requestManifest("cancelled", hash);
        QTRY_COMPARE(manifest.size(), 2);
        QCOMPARE(legacy.size(), 0);
        QCOMPARE(fetch.size(), 0);
        QCOMPARE(globalErrors.size(), 0);
        stream.play(hash, 0);
        stream.prefetch(hash, 1);
        QTRY_COMPARE(legacy.size(), 1);
        QTRY_COMPARE(fetch.size(), 1);
        QCOMPARE(scoped.size(), 1);
        stream.cancelRequest("manifest");
        stream.requestFile("manifest",hash,1);QTRY_COMPARE(errors.size(),2);
        stream.requestManifest("invalid-hash","not-a-hash");QTRY_COMPARE(errors.size(),3);
        QCOMPARE(manifest.size(),2);
        manifestBody="{\"infoHash\":\""+QByteArray(40,'b')+"\",\"files\":[{\"name\":\"song.flac\",\"length\":1234}]}";
        stream.requestManifest("wrong-identity",hash);QTRY_COMPARE(errors.size(),4);
        manifestBody=QByteArrayLiteral("{\"files\":[{\"path\":\"../song.flac\",\"length\":1234}]}");
        stream.requestManifest("unsafe-path",hash);QTRY_COMPARE(errors.size(),5);
        manifestBody=QByteArrayLiteral("{\"files\":[{\"path\":\"album/song.flac\",\"length\":1234}]}");
        stream.requestMagnetManifest("with-magnet",hash,"magnet:?xt=urn:btih:"+hash+"&tr=udp%3A%2F%2Ftracker.example%3A80");
        QTRY_COMPARE(manifest.size(),3);QVERIFY(lastCreate.contains("tracker.example"));
        QCOMPARE(globalErrors.size(),0);QCOMPARE(legacy.size(),1);QCOMPARE(fetch.size(),1);
    }
};
QTEST_GUILESS_MAIN(tst_stream_server_scoped)
#include "stream_contracts.moc"
