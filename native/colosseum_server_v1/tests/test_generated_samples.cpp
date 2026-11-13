#include "server1/ServerComposition.h"
#include "server1/http/AncillaryRoutes.h"
#include "server1/http/HlsV2Routes.h"
#include "server1/http/HttpContract.h"
#include "server1/media/HardwareProbeFetch.h"
#include "server1/settings/MediaSettings.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace server1;
namespace {
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
void wait(bool &done,int limit=20000){
    QElapsedTimer timer;timer.start();
    while(!done && timer.elapsed()<limit){QCoreApplication::processEvents();QThread::msleep(1);}
    check(done,"callback timeout");
}
QByteArray process(QString executable,QStringList arguments){
    QProcess child;child.start(executable,arguments);
    if(!child.waitForFinished(15000) || child.exitStatus()!=QProcess::NormalExit || child.exitCode()!=0){
        std::cerr<<child.readAllStandardError().constData();
        throw std::runtime_error("media process failed");
    }
    return child.readAllStandardOutput();
}
struct HttpFixture {
    void *router=server1_http_router_create();
    void *server=nullptr;
    ~HttpFixture(){server1_http_server_destroy(server);server1_http_router_destroy(router);}
};
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
    check(argc==4,"ffmpeg ffprobe manifest required");
    QTemporaryDir dir;check(dir.isValid(),"temporary directory unavailable");
    QFile manifest(argv[3]);check(manifest.open(QIODevice::ReadOnly),"manifest unavailable");
    const auto rows=QJsonDocument::fromJson(manifest.readAll()).object()["samples"].toArray();
    check(rows.size()==3,"three samples required");
    settings::SettingsStore settings({dir.path().toStdString(),"4.21.0"});
    settings.extend(policy::Value::object({{"transcodeHardwareAccel",policy::Value::boolean(false)}}));
    media::HlsV2Service hls(argv[1],argv[2]);
    settings::applyMediaSettings(settings,hls,QThread::idealThreadCount());
    media::HardwareProfiler profiler(settings,{},nullptr,{},media::hardwareProbeFetch([&]{
        auto service=std::make_shared<media::HlsV2Service>(argv[1],argv[2]);
        settings::applyMediaSettings(settings,*service,QThread::idealThreadCount());return service;
    }));
    HttpFixture fixture;
    const auto port=[&]{return server1_http_server_port(fixture.server);};
    const auto base=[&]{return QString("http://127.0.0.1:%1").arg(port());};
    check(http::installAncillaryRoutes(fixture.router,profiler,port),"ancillary install");
    check(http::installHlsV2Routes(fixture.router,&hls,base),"HLS install");
    fixture.server=server1_http_server_create(fixture.router,1024*1024);
    check(server1_http_server_listen(fixture.server,0),"listen");
    QNetworkAccessManager network;
    QString origin=base();
    auto get=[&](QString path,QString mime={}){
        auto *reply=network.get(QNetworkRequest(QUrl(origin+path)));bool done=false;
        QObject::connect(reply,&QNetworkReply::finished,[&]{done=true;});wait(done);
        check(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200,"HTTP status");
        check(reply->error()==QNetworkReply::NoError,"HTTP transfer error");
        if(!mime.isEmpty())check(reply->header(QNetworkRequest::ContentTypeHeader).toString()==mime,"MIME mismatch");
        const auto bytes=reply->readAll();check(!bytes.isEmpty(),"empty HTTP data");
        reply->deleteLater();return bytes;
    };
    auto checkedSample=[&](QJsonObject row){
        const auto bytes=get(row["route"].toString(),row["mime"].toString());
        check(bytes.size()==row["bytes"].toInt(),"sample size");
        check(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()==row["sha256"].toString().toLatin1(),"sample hash");
        return bytes;
    };
    auto decode=[&](const QByteArray &bytes,QString filename){
        QFile file(dir.filePath(filename));check(file.open(QIODevice::WriteOnly),"decode input write");
        check(file.write(bytes)==bytes.size(),"decode input short write");file.close();
        process(argv[1],{"-v","error","-xerror","-i",file.fileName(),"-f","null","-"});
    };
    auto probeFile=[&](QString filename){
        const auto parsed=QJsonDocument::fromJson(process(argv[2],{"-v","error","-show_streams","-of","json",dir.filePath(filename)}));
        const auto streams=parsed.object()["streams"].toArray();
        check(streams.size()==1,"expected one media stream");return streams.first().toObject();
    };
    QString playbackPath,playbackId;
    for(auto item:rows){
        const auto row=item.toObject();const auto name=row["file"].toString();
        decode(checkedSample(row),name);
        const auto actual=probeFile(name);
        const auto expected=row["probe"].toObject()["streams"].toArray().first().toObject();
        for(const auto key:{"codec_name","codec_type","width","height","pix_fmt","sample_rate","channels","channel_layout"})
            if(expected.contains(key))check(actual[key]==expected[key],"codec/layout mismatch");
        bool done=false;QString error;QJsonObject probe;
        hls.probe(base()+row["route"].toString(),false,[&](auto value,auto failure){probe=value;error=failure;done=true;});wait(done);
        check(error.isEmpty() && !probe["streams"].toArray().isEmpty(),"HTTP ffprobe failed");
        const bool video=name=="hevc.mkv";
        const auto track=video?QString("video0"):QString("audio0");const auto id="generated-"+name;
        const auto suffix="?mediaURL="+QString::fromLatin1(QUrl::toPercentEncoding(base()+row["route"].toString()))+"&forceTranscoding=true&maxWidth=1200";
        const auto playlistPath="/hlsv2/"+id+"/"+track+".m3u8"+suffix;
        check(get(playlistPath).contains("#EXTM3U"),"HLS playlist");
        const auto init=get("/hlsv2/"+id+"/"+track+"/init.mp4"+suffix);
        const auto segment=get("/hlsv2/"+id+"/"+track+"/segment1.m4s"+suffix);
        decode(init+segment,name+".mp4");const auto output=probeFile(name+".mp4");
        check(output["codec_name"].toString()==(video?"h264":"aac"),"expected transcoded codec");
        if(video)check(output["width"].toInt()==1200 && output["height"].toInt()==600,"expected scaled video");
        else check(output["channels"]==expected["channels"] && output["sample_rate"]==expected["sample_rate"],"transcoded audio layout/rate");
        if(video)hls.destroy(id);
        else {playbackPath=playlistPath;playbackId=id;}
        std::cout<<"PASS generated HTTP/hash/probe/source decode/software HLS decode "<<name.toStdString()<<'\n';
    }
    bool done=false;QJsonValue result;
    profiler.run(port(),[&](auto value){result=value;done=true;});wait(done,65000);
    check(result==QJsonValue(false) || (result.isArray() && !result.toArray().isEmpty()),"invalid hardware profile result");
    check(hls.contains(playbackId) && get(playbackPath).contains("#EXTM3U"),"hardware profiling interrupted playback");
    std::cout<<"PASS isolated real profiling; observed hardware profile result: "
             <<QJsonDocument(QJsonArray{result}).toJson(QJsonDocument::Compact).constData()<<'\n';

    // Check the actual application composition with the default empty sample
    // directory, not only the low-level route installer used above.
    policy::EngineRegistryConfig config;config.cacheRoot=dir.filePath("composition-cache").toStdString();
    config.discoveryNetworkFactory=[] {return std::shared_ptr<discovery::DiscoveryNetwork>();};
    ServerMediaConfig mediaConfig;mediaConfig.ffmpeg=argv[1];mediaConfig.ffprobe=argv[2];
    ServerRuntimeConfig runtimeConfig;runtimeConfig.appPath=dir.filePath("composition-app").toStdString();
    runtimeConfig.settingsPathOverride=runtimeConfig.appPath;
    ServerComposition composition(std::move(config),std::move(mediaConfig),std::move(runtimeConfig));
    check(composition.listen(0),"default composition listen");
    origin=QString("http://127.0.0.1:%1").arg(composition.port());
    get("/heartbeat");
    for(auto item:rows)checkedSample(item.toObject());
    composition.stop();
    std::cout<<"PASS default application composition serves all three generated samples without external assets\n";
    return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
