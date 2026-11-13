#include "server1/local/LocalAddon.h"
#include "server1/local/LocalMetadata.h"
#include <QTcpSocket>
#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using server1::local::LocalAddon;
void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}
void until(const std::function<bool()> &done){QElapsedTimer time;time.start();while(!done()&&time.elapsed()<3000)QCoreApplication::processEvents();check(done(),"timeout");}
void touch(const QString&path){QFile f(path);check(f.open(QIODevice::WriteOnly),"fixture write");f.write("synthetic lawful media");}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
 if(argc==2){QFile f(QString::fromLocal8Bit(argv[1]));check(f.open(QIODevice::ReadOnly),"fixture read");const auto rows=QJsonDocument::fromJson(f.readAll()).array();for(auto v:rows){auto row=v.toObject();LocalAddon::Config config;config.disableCatalogSupport=row["disabled"].toBool();LocalAddon addon(config);for(auto r:row["entries"].toArray())addon.saveEntry(r.toObject()["key"].toString(),r.toObject()["entry"].toObject());auto metas=row["metas"].toObject();for(auto i=metas.begin();i!=metas.end();++i)addon.saveMeta(i.key(),i.value().toObject());QJsonObject actual;auto action=row["action"].toString();if(action=="recognize")actual=server1::local::LocalMetadata::matchMetadata(row["query"].toObject(),row["records"].toArray());if(action=="enrich"){
 auto *r=server1_http_router_create();const auto payload=QJsonDocument(row["remote"].toObject()).toJson(QJsonDocument::Compact);server1_http_router_add_static(r,0,0,"GET","/meta/movie/tt123.json",200,payload.constData());auto *srv=server1_http_server_create(r,1024*1024);check(server1_http_server_listen(srv,0),"enrichment fixture listen");server1::local::LocalMetadata::Config conf;conf.enabled=true;conf.metaBase=QUrl("http://127.0.0.1:"+QString::number(server1_http_server_port(srv)));server1::local::LocalMetadata client(conf);bool done=false;client.enrich(row["entry"].toObject(),[&](QJsonObject result){actual=result;done=true;});until([&]{return done;});server1_http_server_destroy(srv);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);server1_http_router_destroy(r);
 }if(action=="manifest")actual=addon.manifest();if(action=="catalog")actual=addon.catalog();if(action=="stream")actual=addon.streams(row["type"].toString(),row["id"].toString());if(action=="meta")actual=addon.meta(row["id"].toString(),qint64(row["now"].toDouble()));if(actual!=row["expected"].toObject()){std::cerr<<QJsonDocument(row).toJson().constData()<<"Actual: "<<QJsonDocument(actual).toJson().constData();throw std::runtime_error("differential mismatch");}}
 std::cout<<"PASS "<<rows.size()<<" pinned local-addon comparisons\n";}
 QTemporaryDir dir;check(dir.isValid(),"temp dir");touch(dir.path()+"/One.mp4");touch(dir.path()+"/ignored.txt");QDir(dir.path()).mkdir("node_modules");touch(dir.path()+"/node_modules/hidden.mp4");QDir(dir.path()).mkdir(".hidden");touch(dir.path()+"/.hidden/hidden.mp4");
 int calls=0;LocalAddon::Config config;config.roots={dir.path()};config.disableCatalogSupport=false;config.indexFile=[&](const QString&p){++calls;return QJsonObject{{"itemId","local:tt123"},{"name","One"},{"files",QJsonArray{QJsonObject{{"path",p},{"name",QFileInfo(p).fileName()},{"imdb_id","tt123"},{"type","movie"}}}}};};
 LocalAddon addon(config);bool done=false;addon.startIndexing([&]{done=true;});until([&]{return done;});check(calls==1&&addon.size()==1,"bounded discovery");
 done=false;addon.startIndexing([&]{done=true;});until([&]{return done;});check(calls==1,"existing paths skipped");
 touch(dir.path()+"/Two.MKV");done=false;addon.startIndexing([&]{done=true;});until([&]{return done;});check(calls==2,"new file indexed");
 QFile::remove(dir.path()+"/One.mp4");done=false;addon.startIndexing([&]{done=true;});until([&]{return done;});check(addon.size()==2,"source retains deleted records until reload");check(addon.persist(dir.path()+"/db"),"persist");LocalAddon loaded(config);check(loaded.load(dir.path()+"/db"),"load");check(loaded.size()==1,"reload prunes unreadable paths");
 done=false;addon.startIndexing([&]{done=true;});addon.cancelIndexing();QCoreApplication::processEvents();check(!done,"cancel suppresses completion");
 std::function<void(QJsonObject)> delayed;LocalAddon::Config asyncConfig;asyncConfig.roots={dir.path()};asyncConfig.indexFileAsync=[&](const QString&,std::function<void(QJsonObject)> cb){delayed=std::move(cb);};auto *asyncOwner=new LocalAddon(asyncConfig);asyncOwner->startIndexing();until([&]{return bool(delayed);});asyncOwner->cancelIndexing();delayed({{"itemId","local:tt1"},{"files",QJsonArray{}}});check(asyncOwner->size()==0,"late completion ignored");delete asyncOwner;delayed({});
 auto *owner=new LocalAddon(config);owner->startIndexing([&]{throw std::runtime_error("destroyed owner callback");});delete owner;QCoreApplication::processEvents();
 auto *router=server1_http_router_create();auto *routeOwner=new LocalAddon(config);check(server1::local::installLocalAddonRoutes(router,*routeOwner),"mount");auto *response=server1_http_router_dispatch(router,"GET","/local-addon/manifest.json","");check(server1_http_response_status(response)==200,"manifest route");server1_http_response_destroy(response);response=server1_http_router_dispatch(router,"GET","/local-addon/meta/other/local:missing.json","");check(server1_http_response_status(response)==200&&QByteArray(server1_http_response_body(response),server1_http_response_body_size(response))=="null","source missing meta response");server1_http_response_destroy(response);
 response=server1_http_router_dispatch(router,"GET","/local-addon/catalog/other/local/skip=5.json","");check(server1_http_response_status(response)==200,"extra segment");server1_http_response_destroy(response);
 auto *server=server1_http_server_create(router,1024*1024);check(server1_http_server_listen(server,0)==1,"loopback listen");
 QTcpSocket socket;socket.connectToHost("127.0.0.1",server1_http_server_port(server));check(socket.waitForConnected(1000),"connect");socket.write("GET /local-addon/catalog/other/local.json HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");socket.flush();until([&]{return socket.state()==QAbstractSocket::UnconnectedState;});auto wire=socket.readAll();check(wire.contains("200 OK")&&wire.contains("{\"metas\":[]}"),"real addon route");
 const QByteArray names="{\"result\":[{\"name\":\"Fixture\",\"type\":\"movie\",\"year\":2024,\"imdb_id\":\"tt1234567\"}]}";
 server1_http_router_add_static(router,0,0,"GET","/names",200,names.constData());server1_http_router_add_static(router,0,0,"GET","/meta/movie/tt1234567.json",200,"{\"meta\":{\"name\":\"Enriched\",\"secret\":\"discard\",\"type\":\"movie\"}}");
 server1::local::LocalMetadata::Config mc;mc.enabled=true;mc.metaBase=QUrl("http://127.0.0.1:"+QString::number(server1_http_server_port(server)));mc.movieNames=QUrl(mc.metaBase.toString()+"/names");server1::local::LocalMetadata metadata(mc);QJsonObject recognized;done=false;metadata.recognize({{"name","Fixture"},{"type","movie"},{"year",2025}},[&](QJsonObject result){recognized=result;done=true;});until([&]{return done;});check(recognized["id"]=="tt1234567","live recognition");
 done=false;QJsonObject indexed;metadata.indexParsedFile(dir.path()+"/Two.MKV",{{"name","Fixture"},{"type","movie"},{"year",2024}},[&](QJsonObject result){indexed=result;done=true;});until([&]{return done;});check(indexed["itemId"]=="local:tt1234567","live indexing");
 done=false;metadata.enrich(indexed,[&](QJsonObject result){check(result["name"]=="Enriched"&&!result.contains("secret")&&result["type"]=="other","enrichment projection");done=true;});until([&]{return done;});
 bool late=false;metadata.enrich(indexed,[&](QJsonObject){late=true;});metadata.cancel();QCoreApplication::processEvents();check(!late,"network cancel");server1_http_server_destroy(server);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
 delete routeOwner;response=server1_http_router_dispatch(router,"GET","/local-addon/manifest.json","");check(server1_http_response_status(response)==503,"owner teardown");server1_http_response_destroy(response);server1_http_router_destroy(router);
 std::cout<<"PASS local discovery, persistence, cancellation, teardown\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
