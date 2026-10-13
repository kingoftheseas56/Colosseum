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
        if (!server.listen(QHostAddress::LocalHost, 11470))
            QSKIP("External Stremio service owns test endpoint");
        bool hold = false;
        int creates = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                    const auto request = socket->readAll();
                    if (!request.contains("HTTP/1.1")) return;
                    if (request.contains("/create")) { ++creates; if (hold) return; }
                    const QByteArray body = request.contains("/create")
                        ? QByteArrayLiteral("{\"files\":[{\"name\":\"cover.jpg\",\"length\":100},{\"name\":\"song.flac\",\"path\":\"album/song.flac\",\"length\":1234}]}")
                        : QByteArrayLiteral("{}");
                    socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
        StreamServer stream;
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
        stream.requestFile("selected", hash, 1);
        QTRY_COMPARE(scoped.size(), 1);
        QCOMPARE(scoped.at(0).at(1).toString(), QString("http://127.0.0.1:11470/") + hash + "/1");
        stream.requestFile("invalid", hash, 2);
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
    }
};
QTEST_GUILESS_MAIN(tst_stream_server_scoped)
#include "stream_contracts.moc"
