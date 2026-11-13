#include "server1/http/AncillaryRoutes.h"
#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace server1;
void check(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
policy::Value val(QJsonValue v){if(v.isBool())return policy::Value::boolean(v.toBool());if(v.isString())return policy::Value::string(v.toString().toStdString());if(v.isArray()){policy::Value::Array a;for(auto x:v.toArray())a.push_back(val(x));return policy::Value::array(a);}return policy::Value::null();}
QJsonObject current(settings::SettingsStore &s){auto v=QJsonDocument::fromJson(QByteArray::fromStdString(policy::jsonStringify(s.value()))).object();return {{"transcodeHardwareAccel",v["transcodeHardwareAccel"]},{"allTranscodeProfiles",v["allTranscodeProfiles"]}};}
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{
    check(argc==2 || argc==3,"fixture required; optional external sample directory");QFile file(QString::fromLocal8Bit(argv[1]));check(file.open(QIODevice::ReadOnly),"fixture open");auto fixtures=QJsonDocument::fromJson(file.readAll()).object();int count=0;
    for(auto v:fixtures["profiles"].toArray()) {
        auto row=v.toObject();QTemporaryDir dir;settings::SettingsStore settings({dir.path().toStdString(),"4.21.0"});
        const auto initial=row["initial"].toObject();for(auto i=initial.begin();i!=initial.end();++i)(void)settings.set(i.key().toStdString(),val(i.value()));
        QJsonArray requests,events;QJsonValue result;bool called=false;
        auto fetch=[&](QUrl url,media::HardwareProfiler::FetchResult done){requests.append(url.toString(QUrl::FullyEncoded));const bool destroy=url.path().endsWith("/destroy");bool ok=false;for(auto p:row["supported"].toArray())if(url.path().contains("-"+p.toString()+"-"))ok=true;if(row["audioFail"].toBool()&&url.path().contains("-audio-"))ok=false;done(!row["networkFail"].toBool(),destroy || ok);};
        media::HardwareProfiler profiler(settings,[&]{events.append(QJsonObject{{"save",current(settings)}});},nullptr,row["platform"].toString(),fetch);
        if(row["debug"].toBool())qputenv("HLS_DEBUG","1");else qunsetenv("HLS_DEBUG");
        if(row["ios"].toBool())qputenv("IS_IOS","1");else qunsetenv("IS_IOS");
        media::HardwareProfiler::Result cb;if(row["callback"].toBool())cb=[&](QJsonValue v){called=true;result=v;events.append(QJsonObject{{"callback",v}});};
        profiler.run(row["port"].toInt(),cb);
        QJsonObject actual{{"requests",requests},{"events",events},{"settings",current(settings)},{"called",called}};
        if(called)actual["result"]=result;
        if(actual!=row["expected"].toObject()){std::cerr<<row["name"].toString().toStdString()<<" actual "<<QJsonDocument(actual).toJson().constData()<<" expected "<<QJsonDocument(row["expected"].toObject()).toJson().constData();throw std::runtime_error("profiler differential mismatch");}++count;
    }
    qunsetenv("HLS_DEBUG");qunsetenv("IS_IOS");
    QTemporaryDir dir;settings::SettingsStore settings({dir.path().toStdString(),"4.21.0"});
    media::HardwareProfiler profiler(settings,{},nullptr,"android",{});auto*router=server1_http_router_create();check(http::installAncillaryRoutes(router,profiler,[]{return 11470;},argc==3?QString::fromLocal8Bit(argv[2]):QString()),"route install");
    for(auto v:fixtures["samples"].toArray()){auto row=v.toObject();auto*r=server1_http_router_dispatch(router,"GET",row["path"].toString().toUtf8(),"");QByteArray bytes(server1_http_response_body(r),server1_http_response_body_size(r));if(argc==2){check(server1_http_response_status(r)==404 && bytes.isEmpty(),"unconfigured samples must be unavailable");server1_http_response_destroy(r);++count;continue;}check(server1_http_response_status(r)==200,"sample status");check(QString::fromUtf8(server1_http_response_header(r,"content-type"))==row["mime"].toString(),"sample mime");check(bytes.size()==row["bytes"].toInt(),"sample length");check(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())==row["sha256"].toString(),"sample bytes");server1_http_response_destroy(r);++count;}
    for(auto path:{"/device-info","/hwaccel-profiler"}){auto*r=server1_http_router_dispatch(router,"GET",path,"");check(!server1_http_response_is_deferred(r),"profile not completed");if(QByteArray(path)=="/device-info"){check(server1_http_response_status(r)==200,"device status");check(QByteArray(server1_http_response_body(r))=="{\"availableHardwareAccelerations\":false}","device false response");check(QByteArray(server1_http_response_header(r,"cache-control"))=="no-store","device cache");}else{check(server1_http_response_status(r)==500,"profiler failure status");check(QByteArray(server1_http_response_body(r))=="No viable hardware acceleration profiles detected","profiler failure text");}server1_http_response_destroy(r);++count;}
    server1_http_router_destroy(router);
    {
        media::HardwareProfiler positive(settings,{},nullptr,"rpi",[](QUrl,auto done){done(true,true);});
        router=server1_http_router_create();check(http::installAncillaryRoutes(router,positive,[]{return 11470;}),"positive install");
        auto*r=server1_http_router_dispatch(router,"GET","/hwaccel-profiler","");
        check(server1_http_response_status(r)==200 && QByteArray(server1_http_response_body(r))=="[\"v4l2m2m\"]","positive profiler response");
        check(QByteArray(server1_http_response_header(r,"content-type")).isEmpty(),"profiler success must omit content type");
        server1_http_response_destroy(r);
        r=server1_http_router_dispatch(router,"GET","/device-info","");
        check(QByteArray(server1_http_response_body(r))=="{\"availableHardwareAccelerations\":[\"v4l2m2m\"]}","positive device response");
        server1_http_response_destroy(r);server1_http_router_destroy(router);count+=2;
    }
    {
        std::vector<media::HardwareProfiler::FetchResult> pending;
        auto fetch=[&](QUrl,auto done){pending.push_back(std::move(done));};
        auto profiler=std::make_unique<media::HardwareProfiler>(settings,std::function<void()>{},nullptr,"rpi",fetch);
        router=server1_http_router_create();check(http::installAncillaryRoutes(router,*profiler,[]{return 11470;}),"async install");
        auto*r=server1_http_router_dispatch(router,"GET","/device-info","");check(server1_http_response_is_deferred(r),"response should wait on profile request");
        server1_http_response_destroy(r); // Client abort must not leave a dangling response pointer.
        while(!pending.empty()){auto done=pending.front();pending.erase(pending.begin());done(true,true);}
        check(current(settings)["allTranscodeProfiles"].toArray()==QJsonArray{"v4l2m2m"},"response abort lost source settings update");
        bool called=false;profiler->run(11470,[&](auto){called=true;});profiler.reset();
        while(!pending.empty()){auto done=pending.front();pending.erase(pending.begin());done(true,true);}
        check(!called,"late callback after profiler destruction");server1_http_router_destroy(router);count+=2;
    }
    {
        QTemporaryDir wrong;QFile file(wrong.filePath("hevc.mkv"));check(file.open(QIODevice::WriteOnly),"wrong sample open");file.write(QByteArray(3955,'x'));file.close();
        media::HardwareProfiler missing(settings,{},nullptr,"android",{});
        router=server1_http_router_create();check(http::installAncillaryRoutes(router,missing,[]{return 11470;},wrong.path()),"wrong sample install");
        auto*r=server1_http_router_dispatch(router,"GET","/samples/hevc.mkv","");check(server1_http_response_status(r)==404,"mismatched sample must be unavailable");server1_http_response_destroy(r);server1_http_router_destroy(router);++count;
    }
    {
        QTcpServer stalled;check(stalled.listen(QHostAddress::LocalHost,0),"stalled fixture listen");
        media::HardwareProfiler bounded(settings,{},nullptr,"rpi",{},25);bool done=false;QJsonValue result;
        bounded.run(stalled.serverPort(),[&](auto v){result=v;done=true;});QElapsedTimer timer;timer.start();
        while(!done && timer.elapsed()<2000){app.processEvents();QThread::msleep(1);}
        check(done && result==QJsonValue(false),"stalled profile request must time out without success");++count;
    }
    std::cout<<"PASS "<<count<<" profiler/sample/route cases\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
