#include "server1/http/AncillaryRoutes.h"
#include "server1/http/HlsV2Routes.h"
#include "server1/http/HttpContract.h"
#include "server1/media/HlsV2Service.h"
#include "server1/settings/MediaSettings.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
int main(int argc,char **argv){QCoreApplication app(argc,argv);if(argc!=3 && argc!=4)return 2;
    QTemporaryDir dir;server1::settings::SettingsStore settings({dir.path().toStdString(),"4.21.0"});
    server1::media::HlsV2Service hls(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]));
    auto sync=[&]{server1::settings::applyMediaSettings(settings,hls,QThread::idealThreadCount());};sync();
    server1::media::HardwareProfiler profiler(settings,sync);
    auto*router=server1_http_router_create();void*server=nullptr;
    auto port=[&]{return server1_http_server_port(server);};
    if(!server1::http::installAncillaryRoutes(router,profiler,port,argc==4?QString::fromLocal8Bit(argv[3]):QString())||!server1::http::installHlsV2Routes(router,&hls,[&]{return QString("http://127.0.0.1:%1").arg(port());}))return 3;
    server=server1_http_server_create(router,1024*1024);if(!server1_http_server_listen(server,0))return 4;
    if(argc==4) {
    bool probeDone=false;QString probeError;QJsonObject probe;
    hls.probe(QString("http://127.0.0.1:%1/samples/hevc.mkv").arg(port()),false,[&](auto v,auto e){probe=v;probeError=e;probeDone=true;});
    QElapsedTimer probeTimer;probeTimer.start();while(!probeDone && probeTimer.elapsed()<15000){app.processEvents();QThread::msleep(1);}
    if(!probeDone || !probeError.isEmpty() || probe["streams"].toArray().isEmpty()){std::cerr<<"real sample probe failed: "<<probeError.toStdString()<<'\n';return 5;}
    }
    bool done=false;QJsonValue result;
    profiler.run(port(),[&](QJsonValue v){result=v;done=true;});QElapsedTimer timer;timer.start();
    while(!done && timer.elapsed()<60000){app.processEvents();QThread::msleep(1);}
    server1_http_server_destroy(server);server1_http_router_destroy(router);
    if(!done){std::cerr<<"hardware profiler real loopback timed out\n";return 1;}
    if(argc==3 && result!=QJsonValue(false)){std::cerr<<"missing samples falsely verified hardware\n";return 6;}
    std::cout<<"PASS real FFmpeg loopback profile attempts; result="<<QJsonDocument(QJsonArray{result}).toJson(QJsonDocument::Compact).constData()<<'\n';
    return 0;
}
