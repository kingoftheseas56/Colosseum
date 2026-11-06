#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QUuid>
#include "player/downloadstore.h"

using Policy = DownloadStore::RecoveryPolicy;
class AndroidDownloadRecovery : public QObject {
    Q_OBJECT
    QTemporaryDir payload;
    QString appName;
    QString base() const { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/videos"; }
    QString output() const { return payload.path()+"/video.mp4"; }
    const QString hash = QString(40, 'a');
    QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
    void seed(bool metadata = true, QString state = "paused", QString pinnedHash = {}) {
        QDir().mkpath(base()); QFile part(output()+".part"); QVERIFY(part.open(QIODevice::WriteOnly)); part.write("abc"); part.close();
        QJsonObject request{{"id","test"},{"streamId","tt1"},{"infoHash",hash},{"fileIdx",2}};
        QJsonObject row{{"request",request},{"state",state},{"outputPath",output()}};
        if (metadata) { row.insert("partialInfoHash",pinnedHash.isEmpty()?hash:pinnedHash); row.insert("partialFileIdx",2); }
        QFile queue(base()+"/queue.json"); QVERIFY(queue.open(QIODevice::WriteOnly)); queue.write(QJsonDocument(QJsonArray{row}).toJson());
    }
    QVariantMap row(DownloadStore &s) { const auto js=s.jobs(); return js.isEmpty()?QVariantMap{}:js.first().toMap(); }
    void request(DownloadStore &store, const QString &resolvedHash, int file, QByteArray response, QByteArray &received) {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server,&QTcpServer::newConnection,&server,[&]{
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,&server,[&,socket]{
                received+=socket->readAll();
                if (!received.contains("\r\n\r\n")) return;
                socket->write(response); socket->disconnectFromHost();
            });
        });
        store.resumeJob("test");
        QCOMPARE(row(store).value("state").toString(),QString("resolving"));
        store.feedTorrentUrl("test",QString("http://127.0.0.1:%1/video").arg(server.serverPort()),resolvedHash,file);
        QTRY_VERIFY_WITH_TIMEOUT(store.hasVideo("test") || row(store).value("state")=="failed",3000);
    }
private slots:
    void init() {
        QVERIFY(payload.isValid()); QStandardPaths::setTestModeEnabled(true);
        appName="ColosseumTask2Recovery-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QCoreApplication::setApplicationName(appName);
        QFile::remove(output()); QFile::remove(output()+".part");
    }
    void cleanup() {
        const auto path=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(path.contains(appName)); QVERIFY(QDir(path).removeRecursively());
    }
    void pinnedResume_data() {
        QTest::addColumn<bool>("metadata"); QTest::addColumn<bool>("sameHash"); QTest::addColumn<int>("file");
        QTest::addColumn<bool>("rangeValid"); QTest::addColumn<bool>("expectRange");
        QTest::newRow("matching")<<true<<true<<2<<true<<true;
        QTest::newRow("old-queue-infers-exact-pin")<<false<<true<<2<<true<<true;
        QTest::newRow("changed-hash-truncates")<<true<<false<<2<<true<<false;
        QTest::newRow("changed-file-truncates")<<true<<true<<3<<true<<false;
        QTest::newRow("mismatched-range-rejected")<<true<<true<<2<<false<<true;
    }
    void pinnedResume() {
        QFETCH(bool,metadata);QFETCH(bool,sameHash);QFETCH(int,file);QFETCH(bool,rangeValid);QFETCH(bool,expectRange);
        seed(metadata); DownloadStore store(nullptr,Policy::AndroidPinnedTorrent); QByteArray received;
        const QByteArray response=expectRange
            ? QByteArray("HTTP/1.1 206 Partial Content\r\nContent-Length: 3\r\nContent-Range: bytes ")+(rangeValid?"3-5/6":"0-2/6")+"\r\nConnection: close\r\n\r\ndef"
            : QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\nxyz");
        request(store,sameHash?hash:QString(40,'b'),file,response,received);
        QCOMPARE(received.contains("Range: bytes=3-"),expectRange);
        if (!rangeValid) { QCOMPARE(row(store).value("state").toString(),QString("failed")); QVERIFY(!store.hasVideo("test")); QVERIFY(!QFile::exists(output()+".part")); }
        else { QCOMPARE(read(output()),expectRange?QByteArray("abcdef"):QByteArray("xyz")); QVERIFY(store.hasVideo("test")); }
    }
    void legacyDefaultDoesNotRecoverPinnedPartial() {
        seed(); DownloadStore store; store.setForeground(false); // must remain legacy, not lifecycle-paused
        QByteArray received; request(store,hash,2,"HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\nxyz",received);
        QVERIFY(!received.contains("Range:")); QCOMPARE(read(output()),QByteArray("xyz"));
        QFile q(base()+"/queue.json"); QVERIFY(q.open(QIODevice::ReadOnly)); QVERIFY(!q.readAll().contains("partialInfoHash"));
    }
    void invalidOldPinNeverAppends() {
        seed(false); QFile q(base()+"/queue.json"); QVERIFY(q.open(QIODevice::ReadOnly)); auto rows=QJsonDocument::fromJson(q.readAll()).array();q.close();
        auto entry=rows.first().toObject(); auto req=entry.value("request").toObject();req.insert("fileIdx",-1);entry.insert("request",req);rows[0]=entry;
        QVERIFY(q.open(QIODevice::WriteOnly));q.write(QJsonDocument(rows).toJson());q.close();
        DownloadStore store(nullptr,Policy::AndroidPinnedTorrent);QByteArray received;
        request(store,hash,2,"HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\nxyz",received);
        QVERIFY(!received.contains("Range:"));QCOMPARE(read(output()),QByteArray("xyz"));
    }
    void transientPrefixSurvivesRetryAndRestart() {
        seed();
        { DownloadStore store(nullptr,Policy::AndroidPinnedTorrent);QByteArray received;
          request(store,hash,2,"HTTP/1.1 206 Partial Content\r\nContent-Length: 10\r\nContent-Range: bytes 3-12/13\r\nConnection: close\r\n\r\ndef",received);
          QCOMPARE(row(store).value("state").toString(),QString("failed")); QCOMPARE(read(output()+".part"),QByteArray("abcdef"));
        }
        { DownloadStore store(nullptr,Policy::AndroidPinnedTorrent);store.retryJob("test");
          QCOMPARE(row(store).value("state").toString(),QString("resolving"));
          store.pauseJob("test");QByteArray received;
          request(store,hash,2,"HTTP/1.1 206 Partial Content\r\nContent-Length: 3\r\nContent-Range: bytes 6-8/9\r\nConnection: close\r\n\r\nghi",received);
          QVERIFY(received.contains("Range: bytes=6-"));QCOMPARE(read(output()),QByteArray("abcdefghi"));
        }
    }
    void lifecycleLeaseAndManualPause() {
        seed(); DownloadStore store(nullptr,Policy::AndroidPinnedTorrent);QSignalSpy resets(&store,&DownloadStore::resolutionReset);
        store.setForeground(false);QCOMPARE(row(store).value("state").toString(),QString("paused"));
        store.setBackgroundExecutionAllowed(true); // manual paused row must stay paused
        QCOMPARE(row(store).value("state").toString(),QString("paused"));
        store.resumeJob("test");QCOMPARE(row(store).value("state").toString(),QString("resolving"));
        store.setBackgroundExecutionAllowed(false);QCOMPARE(row(store).value("state").toString(),QString("paused"));
        store.feedTorrentUrl("test","http://127.0.0.1:1/stale",hash,2);QCOMPARE(row(store).value("state").toString(),QString("paused"));
        store.setBackgroundExecutionAllowed(true);QCOMPARE(row(store).value("state").toString(),QString("resolving"));
        store.pauseJob("test");store.setForeground(true);store.setBackgroundExecutionAllowed(false);
        QCOMPARE(row(store).value("state").toString(),QString("paused"));QCOMPARE(resets.count(),2);
    }
    void failedCancellationKeepsCanonicalJob_data() {
        QTest::addColumn<bool>("android");QTest::newRow("legacy")<<false;QTest::newRow("android")<<true;
    }
    void failedCancellationKeepsCanonicalJob() {
        QFETCH(bool,android);seed();QFile::remove(output()+".part");QVERIFY(QDir().mkpath(output()+".part/occupied"));
        DownloadStore store(nullptr,android?Policy::AndroidPinnedTorrent:Policy::Legacy);store.cancelJob("test");
        QCOMPARE(row(store).value("state").toString(),QString("failed"));
        QVERIFY(row(store).value("error").toString().contains("could not delete"));QVERIFY(QFileInfo::exists(output()+".part"));
        QDir(output()+".part").removeRecursively();
    }
    void missingPartialStartsCleanly() {
        seed();QVERIFY(QFile::remove(output()+".part"));
        DownloadStore store(nullptr,Policy::AndroidPinnedTorrent);QByteArray received;
        request(store,hash,2,"HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\nxyz",received);
        QVERIFY(!received.contains("Range:"));QCOMPARE(read(output()),QByteArray("xyz"));
    }
    void lifecyclePauseRestartRequiresExplicitResume() {
        seed(true,"resolving");
        { DownloadStore store(nullptr,Policy::AndroidPinnedTorrent);store.setForeground(false);
          QCOMPARE(row(store).value("state").toString(),QString("paused")); }
        { DownloadStore store(nullptr,Policy::AndroidPinnedTorrent);QCoreApplication::processEvents();
          QCOMPARE(row(store).value("state").toString(),QString("paused"));
          store.resumeJob("test");QCOMPARE(row(store).value("state").toString(),QString("resolving")); }
    }
    void legacyTransportFailureStillDiscardsPartial() {
        seed();DownloadStore store;QByteArray received;
        request(store,hash,2,"HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nxyz",received);
        QCOMPARE(row(store).value("state").toString(),QString("failed"));QVERIFY(!QFile::exists(output()+".part"));
    }
    void localFileAvailabilityIsHonest() {
        DownloadStore store(nullptr,Policy::AndroidPinnedTorrent);
        QVERIFY(!store.localFileAvailable(output()));QFile f(output());QVERIFY(f.open(QIODevice::WriteOnly));f.write("video");f.close();
        QVERIFY(store.localFileAvailable(output()));QVERIFY(store.localFileAvailable(QUrl::fromLocalFile(output()).toString()));
        QVERIFY(!store.localFileAvailable("https://example.invalid/video"));QVERIFY(!store.localFileAvailable(payload.path()));
    }
};
QTEST_GUILESS_MAIN(AndroidDownloadRecovery)
#include "tst_android_download_recovery.moc"
