// Arc64 W1: real downloader/index/pairing code with offline byte transport only.
// Negative control: omit infoHash serialization; releaseRoundTrip must fail.
#include <QtTest>
#include "engine/AudiobookDownloader.h"
#include "AudioPairingStore.h"
#include "player/streamserver.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <cstring>

// Link-time transport seam, matching the downloader tests' existing stub pattern.
StreamServer::StreamServer(QObject *parent) : QObject(parent) {}
StreamServer::~StreamServer() = default;
void StreamServer::play(const QString &, int) {}
void StreamServer::prefetch(const QString &hash, int index) {
    QTimer::singleShot(0,this,[this,hash,index] { emit fetchReady(streamUrl(hash,index),hash,index); });
}
QString StreamServer::streamUrl(const QString &hash, int index) const { return "http://fixture/"+hash+"/"+QString::number(index); }
void StreamServer::warmUp() {}
void StreamServer::watchStats(const QString &,int) {}
void StreamServer::unwatchStats() {}

class MemoryReply : public QNetworkReply {
    QByteArray m_body;
    qint64 m_offset=0;
    bool m_aborted=false;
public:
    MemoryReply(const QNetworkRequest &request, const QByteArray &body, QObject *parent) : QNetworkReply(parent),m_body(body) {
        setRequest(request);setUrl(request.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);
        open(QIODevice::ReadOnly);setHeader(QNetworkRequest::ContentLengthHeader,body.size());
        QTimer::singleShot(0,this,[this] { if(m_aborted)return;emit readyRead();setFinished(true);emit finished(); });
    }
    void abort() override { m_aborted=true;setError(OperationCanceledError,"cancelled");setFinished(true);emit finished(); }
    qint64 bytesAvailable() const override { return m_body.size()-m_offset+QNetworkReply::bytesAvailable(); }
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char *data,qint64 max) override {
        const auto n=qMin(max,qint64(m_body.size())-m_offset);if(n<=0)return -1;
        std::memcpy(data,m_body.constData()+m_offset,size_t(n));m_offset+=n;return n;
    }
};
class OfflineNetwork : public QNetworkAccessManager {
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override {
        const QByteArray body=request.url().path().endsWith("/create")
            ? QByteArray("{\"files\":[{\"name\":\"chapter.mp3\",\"length\":7}]}") : QByteArray("fixture");
        return new MemoryReply(request,body,this);
    }
};
class tst_audiobook_index : public QObject {
    Q_OBJECT
    const QString pk="quiet river|ada north";
    const QString hash=QString(40,'a');
    QVariantMap metadata() const { return {{"releaseSlug","quiet-m4b"},{"format","M4B"},{"sizeLabel","412 MB"}}; }
private slots:
    void releaseRoundTrip() {
        QTemporaryDir dir;QVERIFY(dir.isValid());OfflineNetwork net;StreamServer stream;
        { AudiobookDownloader dl(&net,&stream,nullptr,dir.path());QSignalSpy finished(&dl,&AudiobookDownloader::finished);
          dl.downloadAudiobook(pk,hash,"Quiet River","Ada North","book","book.epub",metadata());
          QTRY_COMPARE(finished.size(),1);QVERIFY(dl.isDownloaded(pk)); }
        AudiobookDownloader restored(&net,&stream,nullptr,dir.path());const auto e=restored.entryFor(pk);
        QCOMPARE(e.value("infoHash").toString(),hash);QCOMPARE(e.value("releaseSlug").toString(),QString("quiet-m4b"));
        QCOMPARE(e.value("format").toString(),QString("M4B"));QCOMPARE(e.value("sizeLabel").toString(),QString("412 MB"));
        QVERIFY(restored.entryFor("unknown").isEmpty());
    }
    void legacyIndex() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QDir().mkpath(dir.filePath("audiobooks/legacy"));
        const auto track=dir.filePath("audiobooks/legacy/track.mp3");QFile f(track);QVERIFY(f.open(QIODevice::WriteOnly));f.write("fixture");f.close();
        QJsonObject entry{{"dir",dir.filePath("audiobooks/legacy")},{"files",QJsonArray{track}},{"title","Quiet River"},{"author","Ada North"}};
        QFile index(dir.filePath("audiobooks/index.json"));QVERIFY(index.open(QIODevice::WriteOnly));index.write(QJsonDocument(QJsonObject{{pk,entry}}).toJson());index.close();
        OfflineNetwork net;AudiobookDownloader dl(&net,nullptr,nullptr,dir.path());QVERIFY(dl.isDownloaded(pk));
        QCOMPARE(dl.localFiles(pk),QStringList{track});QVERIFY(dl.entryFor(pk).value("releaseSlug").toString().isEmpty());
    }
    void replacementAndPairing() {
        QTemporaryDir dir;QVERIFY(dir.isValid());OfflineNetwork net;StreamServer stream;AudioPairingStore pairing(dir.filePath("pairs.ini"));
        AudiobookDownloader dl(&net,&stream,nullptr,dir.path());dl.setPairing(&pairing);QSignalSpy finished(&dl,&AudiobookDownloader::finished);
        dl.downloadAudiobook(pk,hash,"Quiet River","Ada North","book","book.epub",metadata());QTRY_COMPARE(finished.size(),1);
        const auto marker=dl.localAudiobook(pk)+"/old-only.txt";QFile file(marker);QVERIFY(file.open(QIODevice::WriteOnly));file.write("old");file.close();
        dl.replaceAudiobook(pk,QString(40,'b'),"Quiet River","Ada North","book","book.epub",{{"releaseSlug","new"}});
        QVERIFY(!QFile::exists(marker));QTRY_COMPARE(finished.size(),2);QCOMPARE(dl.downloadedAudiobooks().size(),1);
        QCOMPARE(pairing.allPairings().size(),1);QCOMPARE(pairing.getPairing("book").value("pairKey").toString(),pk);
        QCOMPARE(dl.entryFor(pk).value("releaseSlug").toString(),QString("new"));
    }
    void cancelMidDownload() {
        QTemporaryDir dir;QVERIFY(dir.isValid());OfflineNetwork net;StreamServer stream;
        AudiobookDownloader dl(&net,&stream,nullptr,dir.path());QSignalSpy finished(&dl,&AudiobookDownloader::finished);
        dl.downloadAudiobook(pk,hash,"Quiet River","Ada North");dl.cancelDownload(pk);
        QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();
        QVERIFY(!dl.isDownloaded(pk));QVERIFY(dl.entryFor(pk).isEmpty());QCOMPARE(dl.activeCount(),0);QCOMPARE(finished.size(),0);
    }
    void invalidReplacementPreservesDownload() {
        QTemporaryDir dir;QVERIFY(dir.isValid());OfflineNetwork net;StreamServer stream;
        AudiobookDownloader dl(&net,&stream,nullptr,dir.path());QSignalSpy finished(&dl,&AudiobookDownloader::finished);
        dl.downloadAudiobook(pk,hash,"Quiet River","Ada North");QTRY_COMPARE(finished.size(),1);
        const auto files=dl.localFiles(pk);dl.replaceAudiobook(pk,"bad","Quiet River","Ada North");QVERIFY(dl.isDownloaded(pk));QCOMPARE(dl.localFiles(pk),files);
    }
};
QTEST_GUILESS_MAIN(tst_audiobook_index)
#include "tst_audiobook_index.moc"
