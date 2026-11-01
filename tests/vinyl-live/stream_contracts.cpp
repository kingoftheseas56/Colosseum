#include "player/streamserver.h"
#include "player/StreamRuntimeIntegrity.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QTest>

class tst_stream_server_scoped : public QObject {
    Q_OBJECT
private slots:
    void runtimeIntegrityApplyIsAtomicAndIdempotent() {
        const QByteArray factory = QByteArrayLiteral("module.exports = function(link, opts) {");
        const QByteArray receive = QByteArrayLiteral("var ready = !p.set(reservation, block);");
        const QByteArray invalid = QByteArrayLiteral("else for (var j = ver.start; j != ver.end; j++) engine.emit(\"invalid-piece\", j),");
        const QByteArray connected = QByteArrayLiteral("onwire = function(wire) {");
        const QByteArray prewarm = QByteArrayLiteral("sel = engine.select(offsetPiece, endPiece, !1);");
        const QByteArray original = factory + '\n' + receive + '\n' + invalid + '\n' + connected + '\n' + prewarm;

        QByteArray unknownAnchor = original;
        unknownAnchor.replace(receive, QByteArrayLiteral("var ready = !p.set(unrecognized, block);"));
        const QByteArray rejectedBytes = unknownAnchor;
        QVERIFY(!StreamRuntimeIntegrity::apply(unknownAnchor));
        QCOMPARE(unknownAnchor, rejectedBytes); // no earlier anchor was partially patched

        QByteArray patched = original;
        QVERIFY(StreamRuntimeIntegrity::apply(patched));
        QVERIFY(patched.contains(StreamRuntimeIntegrity::helpers));
        QVERIFY(patched.contains("colosseumRecordPieceSupplier(p, reservation, wire); var ready"));
        QVERIFY(patched.contains("colosseumRejectCorruptSupplier(engine, p, ver)"));
        QVERIFY(patched.contains("colosseumRejectKnownCorruptPeer(engine, wire)"));
        QVERIFY(patched.contains("if (!sel || engine.selection.indexOf(sel) < 0)"));
        const QByteArray oncePatched = patched;
        QVERIFY(StreamRuntimeIntegrity::apply(patched));
        QCOMPARE(patched, oncePatched);
    }

    void scopedManifestRetriesAfterListenerRecovery() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const quint16 servicePort = server.serverPort();
        int creates = 0;
        const QByteArray body = QByteArrayLiteral("{\"files\":[{\"name\":\"song.flac\",\"length\":1234}]}");
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                    const auto request = socket->readAll();
                    if (!request.contains("HTTP/1.1")) return;
                    if (request.contains("/create")) ++creates;
                    const QByteArray responseBody = request.contains("/create") ? body : QByteArrayLiteral("{}");
                    socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "
                                   + QByteArray::number(responseBody.size()) + "\r\n\r\n" + responseBody);
                    socket->disconnectFromHost();
                });
            }
        });

        StreamServer stream(servicePort, nullptr);
        QSignalSpy manifests(&stream, &StreamServer::torrentManifestReady);
        QSignalSpy errors(&stream, &StreamServer::torrentRequestError);
        bool listenerRestored = false;
        bool cancelOnRecovery = false;
        connect(&stream, &StreamServer::readyChanged, &stream, [&]() {
            if (!stream.ready() && !server.isListening()) {
                listenerRestored = server.listen(QHostAddress::LocalHost, servicePort);
                if (cancelOnRecovery)
                    stream.cancelRequest(QStringLiteral("cancelled-during-recovery"));
            }
        });

        const QString hash(40, QLatin1Char('a'));
        stream.requestManifest(QStringLiteral("initial"), hash);
        QTRY_COMPARE(manifests.size(), 1);
        QCOMPARE(creates, 1);

        // Retire the adopted listener after readiness. The next scoped /create
        // gets ConnectionRefusedError; recovery re-adopts the same service port.
        server.close();
        stream.requestManifest(QStringLiteral("retry"), hash);
        QTRY_COMPARE_WITH_TIMEOUT(manifests.size(), 2, 5000);
        QCOMPARE(manifests.at(1).at(0).toString(), QStringLiteral("retry"));
        QCOMPARE(errors.size(), 0);
        QCOMPARE(creates, 2);
        QVERIFY(listenerRestored);
        QVERIFY(stream.ready());

        // Cancellation after the old port is retired must keep the recovery
        // completion from resurrecting this token in flushPending().
        listenerRestored = false;
        cancelOnRecovery = true;
        const int beforeCancelledRetry = creates;
        server.close();
        stream.requestManifest(QStringLiteral("cancelled-during-recovery"), hash);
        QTRY_VERIFY_WITH_TIMEOUT(listenerRestored && stream.ready(), 5000);
        QCOMPARE(manifests.size(), 2);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(creates, beforeCancelledRetry);
        QVERIFY(listenerRestored);
    }

    void successfulScopedManifestResetsRecoveryBudget() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const quint16 servicePort = server.serverPort();
        int creates = 0;
        int settingsPosts = 0;
        const QByteArray manifestBody = QByteArrayLiteral("{\"files\":[{\"name\":\"song.flac\",\"length\":1234}]}");
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                    const auto request = socket->readAll();
                    if (!request.contains("HTTP/1.1")) return;
                    const bool settingsPost = request.startsWith("POST /settings ");
                    const bool create = request.contains("/create");
                    if (settingsPost) ++settingsPosts;
                    if (create) ++creates;
                    const QByteArray body = create ? manifestBody : QByteArrayLiteral("{}");
                    const QByteArray status = settingsPost
                        ? QByteArrayLiteral("HTTP/1.1 500 Internal Server Error\r\n")
                        : QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
                    socket->write(status + "Connection: close\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });

        StreamServer stream(servicePort, nullptr);
        QSignalSpy manifests(&stream, &StreamServer::torrentManifestReady);
        QSignalSpy errors(&stream, &StreamServer::torrentRequestError);
        int listenerRecoveries = 0;
        connect(&stream, &StreamServer::readyChanged, &stream, [&]() {
            if (!stream.ready() && !server.isListening()) {
                ++listenerRecoveries;
                server.listen(QHostAddress::LocalHost, servicePort);
            }
        });

        const QString hash(40, QLatin1Char('a'));
        stream.requestManifest(QStringLiteral("initial"), hash);
        QTRY_COMPARE_WITH_TIMEOUT(manifests.size(), 1, 5000);
        QCOMPARE(settingsPosts, 1); // best-effort tuning failed with HTTP 500

        // First refused create triggers one restart. The recovered listener
        // serves another valid scoped manifest despite /settings failing.
        server.close();
        stream.requestManifest(QStringLiteral("after-first-refusal"), hash);
        QTRY_COMPARE_WITH_TIMEOUT(manifests.size(), 2, 5000);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(listenerRecoveries, 1);
        QCOMPARE(settingsPosts, 2);

        // A second, separate refusal must be treated as a new failure because
        // the scoped /create path proved the recovered listener healthy.
        server.close();
        stream.requestManifest(QStringLiteral("after-second-refusal"), hash);
        QTRY_VERIFY_WITH_TIMEOUT(manifests.size() == 3 || errors.size() > 0, 5000);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(manifests.size(), 3);
        QCOMPARE(listenerRecoveries, 2);
        QCOMPARE(settingsPosts, 3);
        QCOMPARE(creates, 3);
    }

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
