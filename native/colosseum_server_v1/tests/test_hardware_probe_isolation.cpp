#include "server1/media/HardwareProbeFetch.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QFile>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace server1::media;
void check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void wait(bool& b){QElapsedTimer t;t.start();while(!b&&t.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(1);}check(b,"timeout");}
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{
 check(argc==2,"fixture required");QTemporaryDir dir;qputenv("SERVER1_MEDIA_SETTINGS_LOG",dir.filePath("args").toUtf8());
 HlsV2Service playback(argv[1],argv[1]),probes(argv[1],argv[1]);playback.setConcurrency(1,1);probes.setConcurrency(1,1);
 auto victim=playback.acquire("original",{{"mediaURL","fixture:original"}});
 auto competing=playback.acquire("startup-probe",{{"mediaURL","fixture:probe"}});
 bool done=false;QString error;victim->playlist("master",[&](auto r){error=r.error;done=true;});wait(done);
 check(error=="MasterConverter is destroyed","shared source registry did not reproduce cancellation");
 auto media=playback.acquire("real-playback",{{"mediaURL","fixture:real"}});
 done=false;bool ok=false;media->playlist("master",[&](auto r){ok=r.ok();done=true;});
 bool probed=false;auto fetch=hardwareProbeFetch([&]{return std::make_shared<HlsV2Service>(argv[1],argv[1]);});
 fetch(QUrl("http://127.0.0.1:11470/hlsv2/probe/video0.m3u8?mediaURL=fixture%3Aprobe&profile=test&maxWidth=1200"),[&](bool responded,bool success){check(responded&&success,"probe conversion failed");probed=true;});
 wait(done);wait(probed);check(ok && playback.contains("real-playback"),"isolated probe cancelled playback");
 bool destroyed=false;fetch(QUrl("http://127.0.0.1:11470/hlsv2/probe/destroy"),[&](bool r,bool s){destroyed=r&&s;});check(destroyed,"probe destroy failed");
 done=false;media->playlist("master",[&](auto r){ok=r.ok();done=true;});wait(done);check(ok,"probe cleanup destroyed playback");
 bool a=false,b=false;
 const QUrl target("http://127.0.0.1:11470/hlsv2/repeated/video0.m3u8?mediaURL=fixture%3Aprobe&profile=test&maxWidth=1200");
 fetch(target,[&](bool r,bool s){check(r&&s,"first overlap failed");a=true;});
 fetch(target,[&](bool r,bool s){check(r&&s,"second overlap failed");b=true;});
 wait(a);wait(b);a=false;fetch(target,[&](bool r,bool s){check(r&&s,"repeated ID failed");a=true;});wait(a);
 server1::settings::SettingsStore store({dir.path().toStdString(),"4.21.0"});
 HardwareProfiler profiler(store,{},nullptr,"linux",fetch);a=false;b=false;
 profiler.run(11470,[&](QJsonValue v){check(v.isArray()&&v.toArray().size()==3,"first profiler failed");a=true;});
 profiler.run(11470,[&](QJsonValue v){check(v.isArray()&&v.toArray().size()==3,"overlapping profiler failed");b=true;});wait(a);wait(b);
 a=false;profiler.run(11470,[&](QJsonValue v){check(v.isArray()&&v.toArray().size()==3,"repeated profiler failed");a=true;});wait(a);
 QFile slow(dir.filePath("slow.py"));check(slow.open(QIODevice::WriteOnly),"slow fixture");slow.write("#!/usr/bin/env python3\nimport time\ntime.sleep(60)\n");slow.close();slow.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
 std::weak_ptr<HlsV2Service> active;
 auto factory=[&]{auto s=std::make_shared<HlsV2Service>(slow.fileName(),slow.fileName());active=s;return s;};
 auto timed=hardwareProbeFetch(factory,10);int calls=0;bool expired=false;
 timed(target,[&](bool r,bool s){check(!r&&!s,"timeout reported success");++calls;expired=true;});wait(expired);check(active.expired()&&calls==1,"timeout did not release owner");
 auto cancelled=hardwareProbeFetch(factory,1000);cancelled(target,[&](bool,bool){++calls;});cancelled={};check(active.expired()&&calls==1,"owner destruction leaked probe or invoked callback");
 std::cout<<"PASS shared LRU cancellation; isolated playback; repeated/overlapping profiler runs; timeout and cancellation ownership\n";
}catch(std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
