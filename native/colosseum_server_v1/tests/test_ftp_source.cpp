#include "server1/archive/FtpSource.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
using namespace server1::archive;
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc==3&&QString::fromUtf8(argv[1])=="--oracle"){QFile file(QString::fromUtf8(argv[2]));if(!file.open(QIODevice::ReadOnly))return 2;for(auto value:QJsonDocument::fromJson(file.readAll()).array()){auto row=value.toObject();auto plan=ftpRangePlan(100,row["seek"].toBool(),row["range"].toString());if(plan.status!=row["status"].toInt()||plan.length!=row["length"].toInteger()||plan.contentRange!=row["contentRange"].toString()){std::cerr<<"range differential mismatch\n";return 1;}}std::cout<<"14 FTP oracle ranges passed\n";return 0;}if(argc==4&&QString::fromUtf8(argv[1])=="--transport-oracle"){
 QFile file(QString::fromUtf8(argv[3]));if(!file.open(QIODevice::ReadOnly))return 2;auto golden=QJsonDocument::fromJson(file.readAll()).object();QUrl base(QString::fromUtf8(argv[2]));QEventLoop loop;
 for(auto item:golden["metadata"].toArray()){auto row=item.toObject();QUrl url=base;url.setPath(row["path"].toString());FtpSource source(url);FtpMetadata result;source.inspect([&](auto m){result=m;loop.quit();});loop.exec();if((result.error.isEmpty()?200:500)!=row["status"].toInt()||(result.error.isEmpty()&&(result.size!=row["size"].toInteger()||result.supportsSeek!=row["seek"].toBool()))){std::cerr<<"metadata differential mismatch: "<<row["path"].toString().toStdString()<<" "<<result.error.toStdString()<<"\n";return 1;}}
 QUrl movie=base;movie.setPath("/movie.mp4");FtpSource continuous(movie);
 for(auto item:golden["reads"].toArray()){auto row=item.toObject();QUrl url=base;url.setPath(row["path"].toString());FtpSource other(url);auto&source=row["path"].toString()=="/movie.mp4"?continuous:other;ReadResult result;source.read(row["offset"].toInteger(),row["count"].toInteger(),[&](auto r){result=r;loop.quit();});loop.exec();auto expected=QByteArray::fromBase64(row["bytes"].toString().toLatin1());if(result.bytes!=expected||result.error!=row["error"].toString()){std::cerr<<"transfer differential mismatch: "<<row["path"].toString().toStdString()<<" "<<result.error.toStdString()<<" != "<<row["error"].toString().toStdString()<<"\n";return 1;}}
 std::cout<<"9 FTP metadata + 7 real transfer oracle cases passed\n";return 0;
 }if(argc!=2)return 2;QUrl base(QString::fromUtf8(argv[1]));int failures=0;auto check=[&](bool ok,const char*label){if(!ok){std::cerr<<label<<"\n";++failures;}};
 for(QString path:{"/movie.mp4","/noseek.mp4","/missing.mp4","/mlsd.mp4","/list-mlsd.mp4","/pasv-mlsd.mp4","/unix.mp4"}){QUrl url=base;url.setPath(path);FtpSource source(url);QEventLoop loop;FtpMetadata meta;source.inspect([&](auto m){meta=m;loop.quit();});loop.exec();if(path.contains("missing")||path.contains("unix")){check(meta.error=="not found","missing");continue;}check(meta.error.isEmpty()&&meta.size==200000,"metadata");check(meta.supportsSeek==!path.contains("noseek"),"REST feature");
 for(auto pair:{std::pair<qint64,qint64>{0,65536},{65536,65536},{199900,100}}){ReadResult result;source.read(pair.first,pair.second,[&](auto r){result=r;loop.quit();},meta.supportsSeek);loop.exec();check(result.error.isEmpty()&&result.bytes.size()==pair.second,"read length");for(int i=0;i<result.bytes.size();++i)if(result.bytes[i]!=char((pair.first+i)%251)){check(false,"read bytes");break;}}
 bool called=false;auto token=source.read(0,65536,[&](auto){called=true;});source.cancel(token);QTimer::singleShot(30,&loop,&QEventLoop::quit);loop.exec();check(!called&&!source.pendingRequests(),"cancel suppresses callback");ReadResult again;source.read(9,8,[&](auto r){again=r;loop.quit();},meta.supportsSeek);loop.exec();check(again.error.isEmpty()&&again.bytes.size()==8,"reconnect");}
 {
 QEventLoop loop;QUrl url=base;url.setPath("/missing.mp4");FtpSource missing(url);ReadResult result;missing.read(0,10,[&](auto r){result=r;loop.quit();});loop.exec();check(!result.error.isEmpty(),"RETR failure");
 url.setPath("/slow.mp4");auto source=std::make_unique<FtpSource>(url);bool called=false;source->read(0,65536,[&](auto){called=true;});QTimer::singleShot(20,&loop,[&]{source.reset();});QTimer::singleShot(100,&loop,&QEventLoop::quit);loop.exec();check(!called,"destruction during transfer suppresses callback");
 url.setScheme("ftps");FtpSource secure(url);secure.inspect([&](auto m){check(!m.error.isEmpty(),"FTPS explicit rejection");loop.quit();});loop.exec();
 }
 {
 QUrl url=base;url.setPath("/once.mp4");FtpSource source(url);QEventLoop loop;
 for(qint64 offset=0;offset<200000;){auto n=qMin<qint64>(65536,200000-offset);ReadResult result;source.read(offset,n,[&](auto r){result=r;loop.quit();},false);loop.exec();check(result.error.isEmpty()&&result.bytes.size()==n,"persistent nonseek transfer");for(int i=0;i<result.bytes.size();++i)check(result.bytes[i]==char((offset+i)%251),"persistent bytes");offset+=n;}
 }
 auto p=ftpRangePlan(100,true,"bytes=0-0");check(p.status==206&&p.length==100,"oracle end zero");p=ftpRangePlan(100,true,"bytes=-5");check(p.contentRange=="bytes null-5/100"&&p.length==6,"oracle suffix");check(ftpRangePlan(100,false,"bytes=2-").status==405,"noseek range");std::cout<<"FTP failures: "<<failures<<"\n";return failures?1:0;}
