#include "server1/settings/MediaSettings.h"
#include "server1/http/RootRoutes.h"
#include "server1/http/HttpContract.h"
#include "server1/media/HlsV2Service.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace server1;
void check(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
void wait(bool &done){QElapsedTimer timer;timer.start();while(!done && timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(1);}check(done,"callback timeout");}
QJsonObject status(const std::shared_ptr<media::HlsV2Converter>&c){bool done=false;QJsonObject value;c->status([&](auto v,auto e){check(e.isEmpty(),"source status error");value=v;done=true;});wait(done);return value;}
void start(const std::shared_ptr<media::HlsV2Converter>&c,const QString &track){bool done=false,ok=false;c->playlist(track,[&](const auto &r){ok=r.ok();done=true;});wait(done);check(ok,"controlled init failed");}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
    check(argc==3,"require process fixture and result path");QTemporaryDir dir;
    auto log=dir.filePath("processes.jsonl");qputenv("SERVER1_MEDIA_SETTINGS_LOG",log.toUtf8());
    settings::SettingsStore store({dir.path().toStdString(),"4.21.0"});
    media::HlsV2Service service(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[1]));
    qint64 tick=0;service.setClock([&]{return ++tick;});
    auto sync=[&]{settings::applyMediaSettings(store,service,8);};sync();
    auto *router=server1_http_router_create();http::RootRoutesConfig config;config.settings=&store;config.settingsChanged=sync;
    check(http::installRootRoutes(router,config),"root install");
    auto post=[&](const QJsonObject &data){auto body=QJsonDocument(data).toJson(QJsonDocument::Compact);auto wire=QByteArray("POST /settings HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: ")+QByteArray::number(body.size())+"\r\n\r\n"+body;auto*p=server1_http_parser_create(100000);server1_http_parser_feed(p,wire.data(),wire.size());auto*r=server1_http_router_dispatch_parsed(router,p);check(server1_http_response_status(r)==200,"settings POST failed");server1_http_response_destroy(r);server1_http_parser_destroy(p);};
    post({{"transcodeConcurrency",3},{"transcodeTrackConcurrency",2},{"transcodeHorsepower",.5},{"transcodeMaxWidth",1600},{"transcodeMaxBitRate",4000000},{"transcodeHardwareAccel",false},{"transcodeProfile","nvenc-linux"}});
    auto savedSettings=[&]{return QJsonDocument::fromJson(QByteArray::fromStdString(policy::jsonStringify(store.value()))).object();};
    auto initial=savedSettings();
    auto source=service.acquire("first",{{"mediaURL","fixture:settings"},{"forceTranscoding",true}});
    start(source,"video0");auto oldOptions=status(source)["converters"].toObject()["video0"].toObject()["convertOptions"].toObject();check(!oldOptions.isEmpty(),"old options missing");
    QJsonArray captured;
    auto record=[&](QString name,const std::shared_ptr<media::HlsV2Converter>&c,QString track,QJsonObject setting){auto o=status(c)["converters"].toObject()[track].toObject()["convertOptions"].toObject();check(!o.isEmpty(),"new options missing");captured.append(QJsonObject{{"name",name},{"options",o},{"settings",setting},{"cpuCores",8}});return o;};
    record("existing video before update",source,"video0",initial);
    post({{"transcodeHorsepower",.25},{"transcodeMaxWidth",640},{"transcodeMaxBitRate",1000000},{"transcodeHardwareAccel",false},{"transcodeTrackConcurrency",2}});auto updated=savedSettings();
    start(source,"audio0");auto audio=record("new track on existing source",source,"audio0",updated);
    check(oldOptions==status(source)["converters"].toObject()["video0"].toObject()["convertOptions"].toObject(),"settings rewrote existing track");
    auto next=service.acquire("second",{{"mediaURL","fixture:second"},{"forceTranscoding",true}});start(next,"video0");record("new source software settings",next,"video0",updated);
    start(source,"audio1");check(status(source)["converters"].toObject().contains("audio0"),"track concurrency two not applied");
    post({{"transcodeTrackConcurrency",1}});start(source,"audio2");const auto tracks=status(source)["converters"].toObject();check(tracks.contains("audio2")&&!tracks.contains("audio0")&&!tracks.contains("audio1")&&tracks.contains("video0"),"live track LRU not enforced per type");
    check(service.contains("first")&&service.contains("second"),"sources evicted before limit change");
    post({{"transcodeConcurrency",1},{"transcodeHardwareAccel",true},{"transcodeProfile","nvenc-linux"}});auto hardware=savedSettings();
    check(service.contains("first")&&service.contains("second"),"source concurrency must take effect on next acquire");
    auto last=service.acquire("third",{{"mediaURL","fixture:third"},{"forceTranscoding",true}});check(!service.contains("first")&&!service.contains("second")&&service.contains("third"),"live source LRU not enforced");
    start(last,"video0");record("explicit hardware profile settings",last,"video0",hardware);
    QFile calls(log);check(calls.open(QIODevice::ReadOnly),"process log missing");QJsonArray invocations;for(auto line:calls.readAll().split('\n'))if(!line.isEmpty())invocations.append(QJsonDocument::fromJson(line).array());
    for(const auto &row:captured){bool found=false;for(const auto &args:invocations)if(args==row.toObject()["options"].toObject()["args"])found=true;check(found,"status args differ from launched process args");}
    QFile output(QString::fromLocal8Bit(argv[2]));check(output.open(QIODevice::WriteOnly),"output open failed");output.write(QJsonDocument(captured).toJson());server1_http_router_destroy(router);
    std::cout<<"PASS POST /settings live media args, snapshot preservation, track/source LRU\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
