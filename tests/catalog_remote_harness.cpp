#include "engine/CatalogRemote.h"
#include "engine/ImdbCatalog.h"
#include "engine/MalCatalog.h"
#include "engine/ComicsCatalog.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <cstdlib>
static void require(bool ok,const char* why) { if(!ok) { std::cerr<<"FAIL "<<why<<'\n'; std::exit(1); } }
template<class F> void until(F predicate) { QElapsedTimer elapsed; elapsed.start(); while(!predicate() && elapsed.elapsed()<5000) QCoreApplication::processEvents(QEventLoop::AllEvents,10); require(predicate(),"request completed within timeout"); }
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv); QTemporaryDir temp; QNetworkAccessManager nam;
 QTcpServer server; require(server.listen(QHostAddress::LocalHost),"loopback fixture listens");
 int sessions=0,calls=0; bool rejectOnce=false,invalid=false,delayed=false,invalidResult=false,notModified=false;
 QJsonArray lastArgs; QString lastMethod; QString sourceRevision(64, QLatin1Char('a')); QString fixtureEtag; QByteArray lastHeaders;
 QObject::connect(&server,&QTcpServer::newConnection,&app,[&]{
  while(server.hasPendingConnections()) {
   auto* socket=server.nextPendingConnection(); auto bytes=QSharedPointer<QByteArray>::create();
   QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
   QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket,bytes]{
    bytes->append(socket->readAll()); const int split=bytes->indexOf("\r\n\r\n"); if(split<0) return;
    const auto header=bytes->left(split); int length=0;
    for(const auto& line:header.split('\n')) if(line.toLower().startsWith("content-length:")) length=line.mid(15).trimmed().toInt();
    if(bytes->size()<split+4+length) return;
    socket->disconnect(SIGNAL(readyRead()));
    const auto path=header.split(' ').value(1); const auto request=QJsonDocument::fromJson(bytes->mid(split+4,length)).object();
    QJsonObject response; int status=200;
    if(path=="/v1/guest/session") {
     ++sessions; require(request.value("appId")==QJsonValue("colosseum"),"guest app id");
     response={{"token","guest"},{"expiresAt","2099-01-01T00:00:00Z"}};
    } else {
     ++calls; lastHeaders=header; if(notModified) status=304; lastMethod=QString::fromUtf8(path.mid(path.lastIndexOf('/')+1)); lastArgs=request.value("args").toArray();
     require(header.contains("Bearer guest"),"guest bearer on semantic request");
     if(rejectOnce) { rejectOnce=false; status=401; }
     QJsonValue result=QJsonArray{QJsonObject{{"tt","tt001"},{"title",invalidResult ? "Invalid" : "Catalogued"},{"year",2020}}};
     if(lastMethod=="search" || lastMethod=="matchByTitle") result=QJsonArray{};
     if(lastMethod=="mangaById" || lastMethod=="series") result=QJsonObject{{"title","Detail"}};
     response={{"apiVersion",1},{"revision",sourceRevision},{"method",invalid ? "wrong" : lastMethod},{"result",result}};
    }
    const auto body=status==304 ? QByteArray() : QJsonDocument(response).toJson(QJsonDocument::Compact);
    const auto wire="HTTP/1.1 "+QByteArray::number(status)+" OK\r\n"+(fixtureEtag.isEmpty() ? QByteArray() : QByteArrayLiteral("ETag: ")+fixtureEtag.toLatin1()+"\r\n")+"Content-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;
    if(delayed && path!="/v1/guest/session") QTimer::singleShot(80,socket,[socket,wire]{socket->write(wire);socket->disconnectFromHost();});
    else {socket->write(wire);socket->disconnectFromHost();}
   });
  }
 });
 const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
 const QString cache=temp.filePath("imdb.json");
 ImdbCatalog imdb(""); imdb.configureRemote(&nam,endpoint,cache);
 require(!imdb.ready() && imdb.available(),"configured API does not pretend local DB ready");
 QVariantMap query{{"type","movie"}};
 require(imdb.titleCatalog(query,0,24).isEmpty(),"cold miss immediate empty");
 require(imdb.remotePending(),"cold miss queued async"); imdb.titleCatalog(query,0,24);
 until([&]{return !imdb.remotePending();});
 require(calls==1 && sessions==1,"deduplicated miss and one guest lease");
 require(imdb.titleCatalog(query,0,24).size()==1,"API row cached in native result shape");
 require(calls==1,"fresh cache no request");
 imdb.search("  The Matrix (1999)  ",20); until([&]{return !imdb.remotePending();});
 require(lastArgs.at(0).toString()=="matrix","authoritative VaultKit normalized input");
 rejectOnce=true; imdb.titleCatalog(query,24,24); until([&]{return !imdb.remotePending();});
 require(sessions==2 && imdb.titleCatalog(query,24,24).size()==1,"one auth renewal retries original query");
 sourceRevision=QString(64, QLatin1Char('b'));
 imdb.titleCatalog(query,30,24); until([&]{return !imdb.remotePending();});
 require(imdb.titleCatalog(query,0,24).isEmpty(),"new source revision invalidates old query cache");
 until([&]{return !imdb.remotePending();}); require(imdb.titleCatalog(query,0,24).size()==1,"old query revalidated under new revision");
 int errors=0; QObject::connect(&imdb,&ImdbCatalog::requestFailed,&app,[&](QString,QString){++errors;});
 invalid=true; imdb.titleCatalog(query,48,24); until([&]{return !imdb.remotePending();});
 require(errors==1 && imdb.titleCatalog(query,48,24).isEmpty(),"wrong method response rejected and miss honest"); invalid=false;
 imdb.configureRemote(nullptr,endpoint,cache);
 require(!imdb.remoteEnabled() && imdb.available() && !imdb.ready(),"offline cache availability distinct from local readiness");
 require(imdb.titleCatalog(query,0,24).size()==1,"durable offline semantic cache");
 MalCatalog mal(""); mal.configureRemote(&nam,endpoint,temp.filePath("mal.json"));
 mal.mangaById(7); until([&]{return !mal.remotePending();}); require(mal.mangaById(7).value("title")=="Detail","MAL map adapter");
 ComicsCatalog comics(""); comics.configureRemote(&nam,endpoint,temp.filePath("comics.json"));
 comics.series(12); until([&]{return !comics.remotePending();}); require(comics.series(12).value("title")=="Detail","Comics map adapter");
 CatalogRemote remote("imdb"); remote.configure(&nam,endpoint,{});
 remote.read("titleCatalog",{query,0,24},QVariantList{}); until([&]{return !remote.pending();});
 const int before=calls; remote.read("titleCatalog",{query,0,24},QVariantList{},true); until([&]{return !remote.pending();}); require(calls==before+1,"explicit refresh bypasses freshness");
 CatalogRemote validated("imdb");
 validated.setResultValidator([](const QString& method,const QJsonValue& value) {
     return method=="titleCatalog" && value.isArray() && !value.toArray().isEmpty()
         && value.toArray().first().toObject().value("title").toString()=="Catalogued";
 });
 const QString validationCache=temp.filePath("validated.json");
 validated.configure(&nam,endpoint,validationCache);
 validated.read("titleCatalog",{query,0,24},QVariantList{}); until([&]{return !validated.pending();});
 QFile goodFile(validationCache); require(goodFile.open(QIODevice::ReadOnly),"validated cache stored");
 const auto originalCache=goodFile.readAll(); goodFile.close();
 invalidResult=true; validated.read("titleCatalog",{query,0,24},QVariantList{},true); until([&]{return !validated.pending();});
 require(validated.read("titleCatalog",{query,0,24},QVariantList{}).toList().first().toMap().value("title")=="Catalogued","deep validator preserves prior valid cache");
 require(goodFile.open(QIODevice::ReadOnly) && goodFile.readAll()==originalCache,"invalid result never overwrites durable cache"); goodFile.close(); invalidResult=false;
 auto stale=QJsonDocument::fromJson(originalCache).object(); auto entries=stale.value("entries").toObject();
 for(auto it=entries.begin();it!=entries.end();++it) {auto entry=it.value().toObject();entry.insert("fetchedAt","2020-01-01T00:00:00Z");it.value()=entry;}
 stale.insert("entries",entries); require(goodFile.open(QIODevice::WriteOnly|QIODevice::Truncate),"fixture stales cache");goodFile.write(QJsonDocument(stale).toJson());goodFile.close();
 validated.configure(&nam,endpoint,validationCache);
 require(!validated.read("titleCatalog",{query,0,24},QVariantList{}).toList().isEmpty() && validated.pending() && !validated.waiting(),"stale cache paints while revalidation runs");
 until([&]{return !validated.pending();});
 for(auto it=entries.begin();it!=entries.end();++it) {auto entry=it.value().toObject();entry.insert("result",QJsonArray{QJsonObject{{"title","Invalid"}}});it.value()=entry;}
 stale.insert("entries",entries);require(goodFile.open(QIODevice::WriteOnly|QIODevice::Truncate),"fixture corrupts deep payload");goodFile.write(QJsonDocument(stale).toJson());goodFile.close();
 validated.configure(nullptr,endpoint,validationCache);require(!validated.available(),"deep validator rejects poisoned disk cache before exposure");
 CatalogRemote conditional("imdb"); const QString etagCache=temp.filePath("conditional.json");
 fixtureEtag=QStringLiteral("\"stable-tag\""); conditional.configure(&nam,endpoint,etagCache);
 conditional.read("titleCatalog",{query,0,24},QVariantList{});until([&]{return !conditional.pending();});
 notModified=true;conditional.read("titleCatalog",{query,0,24},QVariantList{},true);until([&]{return !conditional.pending();});
 require(lastHeaders.contains("If-None-Match: \"stable-tag\""),"cached ETag sent conditionally");
 require(conditional.read("titleCatalog",{query,0,24},QVariantList{}).toList().size()==1,"304 preserves validated result");
 QFile etagFile(etagCache);require(etagFile.open(QIODevice::ReadOnly),"304 durable cache readable");
 const auto savedEtag=QJsonDocument::fromJson(etagFile.readAll()).object().value("entries").toObject();etagFile.close();
 require(savedEtag.size()==1 && savedEtag.begin().value().toObject().value("etag").toString()==fixtureEtag,"304 persists healthy ETag cache");
 QString conditionalError;QObject::connect(&conditional,&CatalogRemote::failed,&app,[&](QString,QString reason){conditionalError=reason;});
 conditional.read("titleCatalog",{query,500,24},QVariantList{});until([&]{return !conditional.pending();});
 require(conditionalError=="UNEXPECTED_NOT_MODIFIED","cold 304 rejected rather than empty success");
 notModified=false;fixtureEtag.clear();conditional.read("titleCatalog",{query,600,24},QVariantList{});until([&]{return !conditional.pending();});
 require(conditional.read("titleCatalog",{query,600,24},QVariantList{}).toList().size()==1,"200 without ETag remains supported");
 delayed=true; remote.read("titleCatalog",{query,100,24},QVariantList{}); QCoreApplication::processEvents(); remote.configure(nullptr,QUrl(),{});
 require(!remote.pending() && !remote.available(),"reconfigure cancels generation and clears source cache");
 std::cout<<"PASS async semantic RPC: dedupe, guest lease, renewal, shape rejection, offline cache, adapters, refresh, revision invalidation, deep validation, stale delivery, ETag/304, cancellation\n";
}
