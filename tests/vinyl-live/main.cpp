#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDir>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QEventLoop>
#include <cstdio>
#include "ProgressStore.h"
#include "CollectionStore.h"
#include "player/mpvitem.h"
#include "vinyl/VinylProvider.h"
#include "vinyl/VinylTorrentProvider.h"
#include "player/streamserver.h"
#include "torrent/TankorentSearchService.h"
#include "torrent/TorrentIndexer.h"

// Only discovery is injected: manifests, peer transfer, HTTP and mpv remain real.
class SeedIndexer : public TorrentIndexer {
public:
    QString hash;
    explicit SeedIndexer(QString value, QObject *parent) : TorrentIndexer(parent),hash(value) {}
    QString id() const override {return "local-seed";}
    QString displayName() const override {return id();}
    IndexerHealth health() const override {return IndexerHealth::Ok;}
    QDateTime lastSuccess() const override {return {};}
    QString lastError() const override {return {};}
    qint64 lastResponseMs() const override {return 0;}
    void search(const QString &,int,const QString &) override {
        QTimer::singleShot(0,this,[this]{TorrentResult r;r.infoHash=hash;r.seeders=1;emit searchFinished({r});});
    }
};
class SeedSearch : public TankorentSearchService {
public:
    QString hash;
    SeedSearch(QNetworkAccessManager *nam,QString value) : TankorentSearchService(nam),hash(value) {}
protected:
    QList<TorrentIndexer*> buildIndexersFor(const QString &type,const QString &) override {
        return type=="music" ? QList<TorrentIndexer*>{new SeedIndexer(hash,this)} : QList<TorrentIndexer*>{};
    }
};

class ProbeControl : public QObject {
    Q_OBJECT
signals:
    void switchProfile(int index);
    void capture();
};

int main(int argc, char **argv) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        fprintf(stderr, "%s\n", message.toUtf8().constData()); fflush(stderr);
    });
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ColosseumTests");
    app.setApplicationName("VinylLiveHarness");
    const auto args=app.arguments();
    if (args.size()<3) {qCritical("Usage: vinyl_live_harness <isolated-state-dir> <interactive|play|resume>");return 2;}
    const QString state=QDir(args[1]).absolutePath();
    QDir().mkpath(state);
    app.setApplicationName("VinylLiveHarness-"+QFileInfo(state).fileName());
    const bool seeded=args[2]=="torrent-play" || args[2]=="torrent-resume";
    QNetworkAccessManager network;
    StreamServer stream;
    const QString seedHash=qEnvironmentVariable("VINYL_TEST_TORRENT_HASH");
    SeedSearch seedSearch(&network,seedHash);
    VinylTorrentProvider torrents(&network,&stream);
    VinylTorrentProvider seededTorrents(&seedSearch,&stream,nullptr,12000);
    if (seeded) {
        QObject::connect(&stream,&StreamServer::torrentStreamReady,&app,[&](const QString &,const QString &url,const QString &hash,int index){
            qInfo()<<"VINYL_SEED_STREAM"<<url;stream.watchStats(hash,index);
        });
        QObject::connect(&stream,&StreamServer::streamStats,&app,[](const QString &,int,const QVariantMap &stats){
            qInfo()<<"VINYL_SEED_STATS"<<QJsonDocument::fromVariant(stats).toJson(QJsonDocument::Compact);
        });
    }
    if (seeded) {
        QTcpSocket probe;probe.connectToHost(QHostAddress::LocalHost,11470);
        if (probe.waitForConnected(200)) {qCritical("Refusing seeded test against an unrelated :11470 service");return 10;}
        QFile metainfo(qEnvironmentVariable("VINYL_TEST_TORRENT_FILE"));
        if (canonicalizeInfoHash(seedHash).isEmpty() || !metainfo.open(QIODevice::ReadOnly)) return 11;
        const QString torrentHex=QString::fromLatin1(metainfo.readAll().toHex());
        QEventLoop preparation;
        bool prepared=false,requested=false;
        QObject::connect(&stream,&StreamServer::readyChanged,&preparation,[&] {
            if (!stream.ready() || requested) return;
            requested=true;
            QUrl endpoint(stream.streamUrl(seedHash,0));endpoint.setPath("/create");
            QNetworkRequest request(endpoint);request.setTransferTimeout(15000);
            request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
            auto *reply=network.post(request,QJsonDocument(QJsonObject{{"blob",torrentHex}}).toJson());
            QObject::connect(reply,&QNetworkReply::finished,&preparation,[&,reply]{
                prepared=reply->error()==QNetworkReply::NoError;
                if(!prepared) qWarning()<<"Seed registration"<<reply->errorString()<<reply->readAll();
                reply->deleteLater();preparation.quit();
            });
        });
        QTimer::singleShot(20000,&preparation,&QEventLoop::quit);
        stream.warmUp();preparation.exec();
        if (!prepared) {qCritical("Seeded torrent preregistration failed");return 12;}
    }
    VinylProvider provider(args[2].contains("contracts") ? QStringLiteral(VINYL_SOURCE_ROOT "/tests/vinyl-live/fake_provider.py")
                                               : QStringLiteral(VINYL_SOURCE_ROOT "/scripts/vinyl/provider.py"));
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&provider,&VinylProvider::shuttingDown);
    ProgressStore progress(state+"/progress.ini");
    CollectionStore collection(state+"/collection.ini");
    ProgressStore otherProgress(state+"/other-progress.ini");
    CollectionStore otherCollection(state+"/other-collection.ini");
    ProbeControl control;
    qmlRegisterType<MpvItem>("Colosseum.Player",1,0,"MpvItem");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Progress",&progress);
    engine.rootContext()->setContextProperty("Collection",&collection);
    engine.rootContext()->setContextProperty("VinylProvider",&provider);
    if(!args[2].contains("contracts")) engine.rootContext()->setContextProperty("VinylTorrentProvider",seeded ? &seededTorrents : &torrents);
    QObject::connect(&stream,&StreamServer::streamReady,&app,[]{qCritical("VINYL_LEGACY_VIDEO_SIGNAL_LEAK");QCoreApplication::exit(13);});
    QObject::connect(&stream,&StreamServer::fetchReady,&app,[]{qCritical("VINYL_LEGACY_DOWNLOAD_SIGNAL_LEAK");QCoreApplication::exit(14);});
    engine.rootContext()->setContextProperty("VinylLiveEnabled",true);
    engine.rootContext()->setContextProperty("ProbeScenario",args[2]);
    engine.rootContext()->setContextProperty("ProbeControl",&control);
    QObject::connect(&control,&ProbeControl::switchProfile,&engine,[&](int index) {
        emit provider.profileLeaving();
        engine.rootContext()->setContextProperty("Progress",index ? &otherProgress : &progress);
        engine.rootContext()->setContextProperty("Collection",index ? &otherCollection : &collection);
        emit provider.profileEntered();
    });
    engine.load(QUrl::fromLocalFile(QStringLiteral(VINYL_SOURCE_ROOT "/tests/vinyl-live/LiveHarness.qml")));
    if (engine.rootObjects().isEmpty()) return 2;
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(&control,&ProbeControl::capture,&engine,[&] {
        if(window) window->grabWindow().save(state+"/last-frame.png");
    });
    if (args[2]!="interactive") QTimer::singleShot(120000,&app,[]{qCritical("VINYL_PROBE_TIMEOUT");QCoreApplication::exit(3);});
    const int result=app.exec();
    progress.flush();
    return result;
}

#include "main.moc"
