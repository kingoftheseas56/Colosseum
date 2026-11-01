#include "server1/media/LegacyHlsService.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <iostream>
#include "server1/http/LegacyMediaRoutes.h"
#include "server1/http/HttpContract.h"
#include <QElapsedTimer>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
using namespace server1::media;
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc<2)return 2;QFile f(argv[1]);if(!f.open(QIODevice::ReadOnly))return 2;int n=0;for(auto v:QJsonDocument::fromJson(f.readAll()).array()){auto row=v.toObject();auto actual=LegacyHlsService::render(row["action"].toString(),row["instance"].toObject(),row["input"].toObject(),row["subs"].toArray());QJsonObject result{{"status",actual.status},{"headers",actual.headers},{"body",QString::fromUtf8(actual.body)}};if(!actual.program.isEmpty()){result["program"]=actual.program;result["args"]=QJsonArray::fromStringList(actual.arguments);}auto expected=row["expected"].toObject();if(result!=expected){std::cerr<<"Mismatch "<<row["action"].toString().toStdString()<<":"<<row["variant"].toInt()<<"\n"<<QJsonDocument(expected).toJson().constData()<<"actual:"<<QJsonDocument(result).toJson().constData();return 1;}n++;}std::cout<<"PASS "<<n<<" exact legacy oracle cases\n";

 auto spin=[&](std::function<bool()> ready,int limit=5000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<limit){app.processEvents();QThread::msleep(1);}return ready();};
 QString fake=QFileInfo(argv[1]).dir().filePath("../legacy_process_fixture.py");
 {
 LegacyHlsService process({fake,{},{}});auto input=[](QString from){return QJsonObject{{"params",QJsonObject{{"from",from}}},{"query",QJsonObject{}}};};
 bool ready=false;auto slow=process.request("thumb",input("slow-large"),[&](LegacyResponse){ready=true;});slow->cancel();spin([]{return false;},250);if(ready||slow->running()){std::cerr<<"canceled delayed probe leaked\n";return 11;}
 bool a=false,b=false;auto first=process.request("thumb",input("large-a"),[&](LegacyResponse r){a=r.status==200;});auto second=process.request("thumb",input("large-b"),[&](LegacyResponse r){b=r.status==200;});if(!spin([&]{return a&&b;}))return 12;
 first->cancel();spin([]{return false;},100);if(first->running()||second->bufferedBytes()>196608){std::cerr<<"process cancellation or bound failed\n";return 13;}
 QByteArray bytes;char buffer[32768];if(!spin([&]{auto n=second->read(buffer,sizeof(buffer));if(n>0)bytes.append(buffer,n);return n==0;},10000)||bytes.size()!=8*1024*1024)return 14;
 int started=0;QVector<std::shared_ptr<LegacyOperation>> queued;for(int i=0;i<10;i++)queued.append(process.request("thumb",input("large-queue"+QString::number(i)),[&](LegacyResponse){started++;}));if(!spin([&]{return started==8;}))return 17;spin([]{return false;},100);if(started!=8)return 18;queued[0]->cancel();if(!spin([&]{return started==9;}))return 19;for(auto op:queued)op->cancel();
 bool failed=false;auto bad=process.request("thumb",input("failed"),[&](LegacyResponse r){failed=r.status==500;});if(!spin([&]{return failed;}))return 15;
 std::shared_ptr<LegacyOperation> survivor;{LegacyHlsService shortLife({fake,{},{}});survivor=shortLife.request("thumb",input("slow-large"),[](LegacyResponse){});}if(survivor->read(buffer,sizeof(buffer))!=-1)return 16;
 std::cout<<"PASS delayed-probe cancellation, independent concurrent consumers, bounded output, failed child, service teardown\n";
 }
 if(argc<3)return 0;
 LegacyHlsService svc({"/usr/bin/ffmpeg","/usr/bin/ffprobe",{}});auto router=server1_http_router_create();if(!server1::http::installLegacyMediaRoutes(router,&svc,"http://127.0.0.1:1"))return 3;auto server=server1_http_server_create(router,65536);if(!server1_http_server_listen(server,0))return 4;
 QNetworkAccessManager net;QString root="http://127.0.0.1:"+QString::number(server1_http_server_port(server));QString encoded=QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(argv[2])));
 auto fetch=[&](QString path,int status,QByteArray contains,QByteArray magic=QByteArray()){auto* reply=net.get(QNetworkRequest(QUrl(root+path)));QElapsedTimer timer;timer.start();while(!reply->isFinished()&&timer.elapsed()<30000){app.processEvents();QThread::msleep(1);}auto body=reply->readAll();bool ok=reply->isFinished()&&reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==status&&(contains.isEmpty()||body.contains(contains))&&(magic.isEmpty()||body.startsWith(magic));std::cout<<path.toStdString()<<" status="<<reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()<<" bytes="<<body.size()<<" "<<(ok?"PASS":"FAIL")<<"\n";if(!ok)std::cerr<<body.left(300).constData()<<"\n";reply->deleteLater();return ok;};
 int failed=0;
 failed+=!fetch("/file/"+encoded+"/master.m3u8",200,"stream-0.m3u8");
 failed+=!fetch("/file/"+encoded+"/hls.m3u8",200,"stream-q-240.m3u8");
 failed+=!fetch("/file/"+encoded+"/stream-q-240.m3u8",200,"#EXTINF:6.000");
 failed+=!fetch("/file/"+encoded+"/stream-q-o.m3u8",200,"#EXTINF:2.000");
 failed+=!fetch("/file/"+encoded+"/stream.m3u8",404,"no such quality");
 failed+=!fetch("/file/"+encoded+"/stream-1.m3u8",404,"no such quality");
 failed+=!fetch("/file/"+encoded+"/subs-eng.m3u8",200,{});
 failed+=!fetch("/file/"+encoded+"/stream-q-240/0.ts",200,{},QByteArray(1,char(0x47)));
 failed+=!fetch("/file/"+encoded+"/stream-q-240/99.ts",404,"segment does not exist in segmentsUniform");
 failed+=!fetch("/file/"+encoded+"/stream-1/99.ts",404,"segment does not exist in segmentsByFrame");
 failed+=!fetch("/file/"+encoded+"/thumb.jpg",200,{},QByteArray::fromHex("ffd8"));
 failed+=!fetch("/thumb.jpg?from="+encoded+"&at=1",200,{},QByteArray::fromHex("ffd8"));
 failed+=!fetch("/probe?url="+QString::fromLatin1(QUrl::toPercentEncoding("file://"+QString::fromUtf8(argv[2]))),200,"duration");
 failed+=!fetch("/file/"+encoded+"/dlna",500,{});
 if(argc>3)failed+=!fetch("/file/"+QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(argv[3])))+"/dlna",200,{},QByteArray(1,char(0x47)));
 server1_http_server_destroy(server);server1_http_router_destroy(router);app.processEvents();return failed?5:0;}

