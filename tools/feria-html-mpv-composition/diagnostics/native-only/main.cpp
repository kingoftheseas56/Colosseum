// Diagnostic baseline only: no WebEngine headers, initialization, linkage or view.
#include "mpvitem.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QDir>
#include <QFile>
#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <cmath>
#include <algorithm>

class Baseline final : public QObject {
    Q_OBJECT
public:
    QString fixtures, evidence;
    bool keepOpen = false;
    QQuickWindow *window = nullptr;
    MpvItem *player = nullptr;
    int fixtureIndex = 0, stage = 0;
    bool loaded = false, passed = true;
    QTimer tick;
    QElapsedTimer clock;
    QImage first;
    QJsonArray frames;
    Baseline() {
        clock.start(); tick.setInterval(70);
        connect(&tick, &QTimer::timeout, this, [this] {
            if (!player) return;
            if (stage == 1 && loaded && player->decodedWidth()>0) { stage=2; player->seekExact(1); }
            else if (stage == 2 && std::abs(player->position()-1)<.2 && !player->coreSeeking()) {
                stage=3; QTimer::singleShot(450,this,[this]{ capture(1); stage=4; player->seekExact(3); });
            } else if (stage == 4 && std::abs(player->position()-3)<.2 && !player->coreSeeking()) {
                stage=5; QTimer::singleShot(450,this,[this]{ capture(3); finish(); });
            }
        });
    }
    Q_INVOKABLE void bind(QObject *object) {
        player=qobject_cast<MpvItem *>(object);
        player->setPropertyBlocking("ao","null");
        player->setPropertyBlocking("hwdec","no");
        player->setPropertyBlocking("keep-open","no");
        connect(player,&MpvItem::fileLoaded,this,[this]{loaded=true;});
        connect(player,&MpvItem::ready,this,[this]{ if(stage==0){ qInfo()<<"NATIVE RENDERER READY"; load(); tick.start(); } },Qt::QueuedConnection);
    }
    void load() {
        loaded=false; stage=1; first=QImage();
        const QString name=fixtureIndex==0?"h264-aac.mp4":"ffv1-pcm.mkv";
        player->setPause(true); player->loadFile(QDir(fixtures).absoluteFilePath(name));
        qInfo()<<"NATIVE LOAD"<<name;
    }
    void capture(int position) {
        const QString name=QString(fixtureIndex==0?"h264-":"ffv1-")+QString::number(position);
        const QImage image=window->grabWindow().convertToFormat(QImage::Format_RGB32);
        const bool saved=!image.isNull()&&image.save(evidence+'/'+name+".png");
        const QRect region(image.width()*.35,image.height()*.24,image.width()*.30,image.height()*.32);
        const QImage crop=image.copy(region);
        double sums[3]={}, squares[3]={}; QSet<quint32> colors;
        for(int y=0;y<crop.height();++y) for(int x=0;x<crop.width();++x) {
            const QRgb p=crop.pixel(x,y); const int c[3]={qRed(p),qGreen(p),qBlue(p)};
            for(int k=0;k<3;++k){sums[k]+=c[k];squares[k]+=double(c[k])*c[k];}
            colors.insert(p & 0x00ffffff);
        }
        const double count=std::max(1,crop.width()*crop.height()); QJsonArray deviations; double maximum=0;
        for(int k=0;k<3;++k) {const double v=std::sqrt(std::max(0.,squares[k]/count-std::pow(sums[k]/count,2))); deviations.append(v);maximum=std::max(maximum,v);}
        const bool varied=maximum>18&&colors.size()>20;
        double difference=0;
        if(position==1) first=crop;
        else if(first.size()==crop.size()) {
            for(int y=0;y<crop.height();++y)for(int x=0;x<crop.width();++x){const QRgb a=first.pixel(x,y),b=crop.pixel(x,y);difference+=(std::abs(qRed(a)-qRed(b))+std::abs(qGreen(a)-qGreen(b))+std::abs(qBlue(a)-qBlue(b)))/3.;}
            difference/=count;
        }
        const bool distinct=position==1||difference>3;
        passed &= saved&&varied&&distinct;
        frames.append(QJsonObject{{"name",name},{"saved",saved},{"position",player->position()}, {"duration",player->duration()}, {"decodedWidth",player->decodedWidth()}, {"decodedHeight",player->decodedHeight()}, {"paused",player->pause()}, {"stddev",deviations}, {"distinctRgbColors",colors.size()}, {"variedPicture",varied}, {"meanDifferenceFromFirst",difference}, {"distinctFromFirst",distinct}, {"exposed",window->isExposed()}, {"graphicsApi",int(window->rendererInterface()->graphicsApi())}, {"videoCodec",QJsonValue::fromVariant(player->mpvProperty("video-codec"))}, {"audioCodec",QJsonValue::fromVariant(player->mpvProperty("audio-codec"))}});
        qInfo()<<"NATIVE CAPTURE"<<name<<"varied"<<varied<<"difference"<<difference;
        write();
    }
    void write() {
        QDir().mkpath(evidence); QFile f(evidence+"/native-only.json");
        if(f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(QJsonObject{{"passed",passed},{"compileQt",QT_VERSION_STR},{"runtimeQt",qVersion()},{"frames",frames},{"elapsedMs",clock.elapsed()},{"webEngineLinked",false},{"audioOutput","null"}}).toJson());
    }
    void finish() {tick.stop();stage=6;write();qInfo()<<"NATIVE BASELINE COMPLETE"<<passed;if(!keepOpen)QTimer::singleShot(200,qApp,[this]{qApp->exit(passed?0:2);});}
};
int main(int argc,char **argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc,argv);
    QCommandLineParser cli;cli.addHelpOption();
    cli.addOptions({QCommandLineOption("fixtures","Fixture directory","directory"),QCommandLineOption("evidence","Evidence directory","directory"),QCommandLineOption("keep-open","Keep final image on the desktop"),QCommandLineOption("fixture","Single fixture ID: h264 or ffv1","id","h264")});cli.process(app);
    if(!cli.isSet("fixtures")||!cli.isSet("evidence"))return 64;
    if(cli.value("fixture")!="h264"&&cli.value("fixture")!="ffv1")return 64;
    Baseline baseline;baseline.fixtureIndex=cli.value("fixture")=="ffv1"?1:0;baseline.fixtures=cli.value("fixtures");baseline.evidence=cli.value("evidence");baseline.keepOpen=cli.isSet("keep-open");QDir().mkpath(baseline.evidence);
    qmlRegisterType<MpvItem>("Baseline.Player",1,0,"MpvItem");
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("baseline",&baseline);engine.load(QUrl("qrc:/Main.qml"));if(engine.rootObjects().isEmpty())return 1;
    baseline.window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    qInfo()<<"NATIVE ONLY Qt compile"<<QT_VERSION_STR<<"runtime"<<qVersion();
    QTimer::singleShot(30000,&baseline,[&baseline]{if(baseline.stage!=6){baseline.passed=false;baseline.finish();}});
    return app.exec();
}
#include "main.moc"
