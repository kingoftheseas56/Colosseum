#include "engine/CatalogRemote.h"
#include "engine/ImdbCatalog.h"
#include "engine/MalCatalog.h"
#include "engine/ComicsCatalog.h"
#include <QCoreApplication>
#include <QMetaMethod>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QStringList>
#include <QTimer>
#include <iostream>
#include <cstdlib>
static void require(bool ok,const char* why) { if(!ok) { std::cerr<<"FAIL "<<why<<'\n'; std::exit(1); } }
template<class F> void until(F predicate) { QElapsedTimer elapsed; elapsed.start(); while(!predicate() && elapsed.elapsed()<5000) QCoreApplication::processEvents(QEventLoop::AllEvents,10); require(predicate(),"request completed within timeout"); }
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv); QTemporaryDir temp; QNetworkAccessManager nam;
 QTcpServer server; require(server.listen(QHostAddress::LocalHost),"loopback fixture listens");
 int sessions=0,calls=0; bool rejectOnce=false,invalid=false,delayed=false,invalidResult=false,notModified=false;
 QString failArg,emptyArg,slowArg,holdArg;
 QPointer<QTcpSocket> heldSocket; QByteArray heldWire;
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
     const QString selector=lastArgs.isEmpty() ? QString() : lastArgs.at(0).toString();
     if (!failArg.isEmpty() && selector==failArg) status=503;
     require(header.contains("Bearer guest"),"guest bearer on semantic request");
     if(rejectOnce) { rejectOnce=false; status=401; }
     QJsonValue result=QJsonArray{QJsonObject{{"tt","tt001"},{"title",invalidResult ? "Invalid" : "Catalogued"},{"year",2020}}};
     if(lastMethod=="search" || lastMethod=="matchByTitle") result=QJsonArray{};
     if(lastMethod=="mangaById" || lastMethod=="series") result=QJsonObject{{"title","Detail"}};
     if (!emptyArg.isEmpty() && selector==emptyArg) result=QJsonArray{};
     if(lastMethod=="discoverPageFiltered") {
      const int pageOffset=lastArgs.at(3).toInt();
      QJsonArray items;
      if (selector!=emptyArg)
       items.append(QJsonObject{{"mal_id",44},{"title","Remote Manga"},{"availability",false}});
      result=QJsonObject{{"items",items},{"nextOffset",pageOffset+items.size()},
                         {"exhausted",true},{"freshness","bundled"},
                         {"fallbackCatalog",selector=="trending" ? "popular" : ""}};
     }
     response={{"apiVersion",1},{"revision",sourceRevision},{"method",invalid ? "wrong" : lastMethod},{"result",result}};
    }
    const auto body=status==304 ? QByteArray() : QJsonDocument(response).toJson(QJsonDocument::Compact);
    const auto wire="HTTP/1.1 "+QByteArray::number(status)+" OK\r\n"+(fixtureEtag.isEmpty() ? QByteArray() : QByteArrayLiteral("ETag: ")+fixtureEtag.toLatin1()+"\r\n")+"Content-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;
    if(path!="/v1/guest/session" && !holdArg.isEmpty() && !lastArgs.isEmpty()
       && lastArgs.at(0).toString()==holdArg) {
     heldSocket=socket; heldWire=wire; // Test releases this response explicitly.
    } else if(path!="/v1/guest/session"
       && (delayed || (!slowArg.isEmpty() && !lastArgs.isEmpty() && lastArgs.at(0).toString()==slowArg)))
     QTimer::singleShot(160,socket,[socket,wire]{socket->write(wire);socket->disconnectFromHost();});
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
 MalCatalog mal(""); const QString malCache=temp.filePath("mal.json"); mal.configureRemote(&nam,endpoint,malCache);
 mal.mangaById(7); until([&]{return !mal.remotePending();}); require(mal.mangaById(7).value("title")=="Detail","MAL map adapter");
 // F2b MAL page projection: QML invokables and typed exact-query notification.
 const QMetaObject* malMeta=mal.metaObject();
 const int pageStateIndex=malMeta->indexOfMethod("discoverPageFilteredState(QString,QVariantList,bool,int,int)");
 const int pageRetryIndex=malMeta->indexOfMethod("retryDiscoverPageFiltered(QString,QVariantList,bool,int,int)");
 const int pageNoticeIndex=malMeta->indexOfSignal("remoteQueryChanged(QString)");
 require(pageStateIndex>=0 && pageRetryIndex>=0 && pageNoticeIndex>=0,
         "MAL projected page state/retry/signal are visible to QML metaobject");
 require(QByteArray(malMeta->method(pageStateIndex).typeName())=="QVariantMap"
         && QByteArray(malMeta->method(pageRetryIndex).typeName())=="bool"
         && malMeta->method(pageNoticeIndex).methodType()==QMetaMethod::Signal,
         "MAL projected invokables have typed return values and signal");
 const QVariantList genre{QVariantMap{{"axis","genre"},{"key","romance"}}};
 const QVariantList demographic{QVariantMap{{"axis","demographic"},{"key","seinen"}}};
 const auto pageState=[&](const QString& id,const QVariantList& filters,bool explicitContent,int offset) {
     return mal.discoverPageFilteredState(id,filters,explicitContent,offset,24);
 };
 QStringList pageNotices;
 QObject::connect(&mal,&MalCatalog::remoteQueryChanged,&app,[&](const QString& key){pageNotices.append(key);});
 const int inspectedCalls=calls;
 const auto page0=pageState("popular",genre,false,0);
 const auto page24=pageState("popular",genre,false,24);
 const auto filtered24=pageState("popular",demographic,false,24);
 require(page0.value("source").toString()=="remote"
         && page0.value("status").toString()=="missing"
         && page0.value("key").toString()!=page24.value("key").toString()
         && page24.value("key").toString()!=filtered24.value("key").toString()
         && calls==inspectedCalls && pageNotices.isEmpty(),
         "MAL inspection is nonfetching; offset and filters form distinct exact keys");
 const auto cold0=mal.discoverPageFiltered("popular",genre,false,0,24);
 const auto cold24=mal.discoverPageFiltered("popular",genre,false,24,24);
 const auto coldFiltered24=mal.discoverPageFiltered("popular",demographic,false,24,24);
 require(cold0.value("items").toList().isEmpty() && cold0.value("nextOffset").toInt()==0
         && cold0.value("exhausted").toBool()
         && cold24.value("nextOffset").toInt()==24
         && coldFiltered24.value("nextOffset").toInt()==24
         && pageState("popular",genre,false,0).value("pending").toBool()
         && pageState("popular",genre,false,24).value("pending").toBool()
         && pageNotices.isEmpty(),"MAL cold fallback retained; enqueue notification deferred");
 until([&]{return !mal.remotePending();});
 const auto loaded0=mal.discoverPageFiltered("popular",genre,false,0,24);
 const auto loaded24=mal.discoverPageFiltered("popular",genre,false,24,24);
 const auto loadedFiltered24=mal.discoverPageFiltered("popular",demographic,false,24,24);
 require(calls==inspectedCalls+3 && loaded0.value("items").toList().size()==1
         && loaded24.value("nextOffset").toInt()==25
         && loadedFiltered24.value("items").toList().size()==1
         && loaded0.value("freshness").toString()=="bundled"
         && loaded0.value("fallbackCatalog").toString().isEmpty()
         && pageState("popular",genre,false,0).value("status").toString()=="success"
         && pageNotices.contains(page0.value("key").toString())
         && pageNotices.contains(page24.value("key").toString())
         && pageNotices.contains(filtered24.value("key").toString()),
         "MAL page result has the real native map and two independent key notices");
 const int inspectionOnlyCalls=calls;
 pageState("popular",genre,false,0); pageState("popular",genre,false,24);
 require(calls==inspectionOnlyCalls,"page inspection does not send network traffic");
 const auto trendingCold=mal.discoverPageFiltered("trending",{},false,0,24);
 require(trendingCold.value("fallbackCatalog").toString()=="popular",
         "MAL remote cold trending fallback remains popular");
 until([&]{return !mal.remotePending();});
 require(mal.discoverPageFiltered("trending",{},false,0,24).value("fallbackCatalog").toString()=="popular",
         "MAL loaded remote trending fallback remains popular");
 // Successful empty map must never be confused with an uncached failed fallback map.
 emptyArg="g03-empty"; failArg="g03-fail";
 mal.discoverPageFiltered("g03-empty",{},false,48,24);
 mal.discoverPageFiltered("g03-fail",{},false,72,24);
 until([&]{return !mal.remotePending();});
 const auto emptyPage=mal.discoverPageFiltered("g03-empty",{},false,48,24);
 const auto failedPage=mal.discoverPageFiltered("g03-fail",{},false,72,24);
 require(emptyPage.value("items").toList().isEmpty() && emptyPage.value("nextOffset").toInt()==48
         && pageState("g03-empty",{},false,48).value("status").toString()=="success"
         && pageState("g03-empty",{},false,48).value("cached").toBool()
         && failedPage.value("items").toList().isEmpty() && failedPage.value("nextOffset").toInt()==72
         && pageState("g03-fail",{},false,72).value("status").toString()=="failure"
         && !pageState("g03-fail",{},false,72).value("cached").toBool()
         && pageState("g03-fail",{},false,72).value("error").toString()=="HTTP_503",
         "MAL projected success-empty and uncached failure fallback stay distinct");
 emptyArg.clear(); failArg.clear();
 // Force a cached revalidation via the existing stale-disk fixture technique.
 const QVariantList refreshedFilter{QVariantMap{{"axis","genre"},{"key","fantasy"}}};
 mal.discoverPageFiltered("g03-refresh",refreshedFilter,true,96,24);
 until([&]{return !mal.remotePending();});
 const auto savedPage=mal.discoverPageFiltered("g03-refresh",refreshedFilter,true,96,24);
 require(savedPage.value("items").toList().size()==1,"MAL page to revalidate was cached");
 const QString refreshKey=pageState("g03-refresh",refreshedFilter,true,96).value("key").toString();
 QFile malDisk(malCache); require(malDisk.open(QIODevice::ReadOnly),"MAL fixture page cache readable");
 auto malDocument=QJsonDocument::fromJson(malDisk.readAll()).object(); malDisk.close();
 auto malEntries=malDocument.value("entries").toObject();
 require(malEntries.contains(refreshKey),"MAL refresh key persisted in owner cache");
 auto malEntry=malEntries.value(refreshKey).toObject();
 malEntry.insert("fetchedAt","2020-01-01T00:00:00Z");malEntries.insert(refreshKey,malEntry);
 malDocument.insert("entries",malEntries);
 const QByteArray staleMalBytes=QJsonDocument(malDocument).toJson(QJsonDocument::Compact);
 require(malDisk.open(QIODevice::WriteOnly|QIODevice::Truncate)
         && malDisk.write(staleMalBytes)==staleMalBytes.size(),"MAL fixture cache made stale");malDisk.close();
 mal.configureRemote(&nam,endpoint,malCache);
 require(pageState("g03-refresh",refreshedFilter,true,96).value("cached").toBool(),
         "MAL cached result survives source configuration");
 failArg="g03-peer";
 mal.discoverPageFiltered("g03-peer",{},false,120,24);
 until([&]{return !mal.remotePending();});
 require(pageState("g03-peer",{},false,120).value("status").toString()=="failure",
         "independent failed MAL peer established");
 failArg="g03-refresh";
 const auto staleRead=mal.discoverPageFiltered("g03-refresh",refreshedFilter,true,96,24);
 require(staleRead.value("items").toList().size()==1
         && pageState("g03-refresh",refreshedFilter,true,96).value("pending").toBool()
         && pageState("g03-refresh",refreshedFilter,true,96).value("cached").toBool()
         && !pageState("g03-refresh",refreshedFilter,true,96).value("waiting").toBool(),
         "stale cached MAL result remains readable during refresh");
 until([&]{return !mal.remotePending();});
 const auto failedRefresh=pageState("g03-refresh",refreshedFilter,true,96);
 require(failedRefresh.value("status").toString()=="failure"
         && failedRefresh.value("cached").toBool()
         && failedRefresh.value("retryable").toBool()
         && failedRefresh.value("error").toString()=="HTTP_503"
         && mal.discoverPageFiltered("g03-refresh",refreshedFilter,true,96,24).value("items").toList().size()==1,
         "MAL failed revalidation keeps old validated page and exposes cached error");
 require(malDisk.open(QIODevice::ReadOnly) && malDisk.readAll()==staleMalBytes,
         "MAL failed revalidation does not corrupt durable cached page");malDisk.close();
 failArg.clear(); const int retryStartCalls=calls;
 require(mal.retryDiscoverPageFiltered("g03-refresh",refreshedFilter,true,96,24)
         && !mal.retryDiscoverPageFiltered("g03-refresh",refreshedFilter,true,96,24),
         "MAL exact failed retry dedupes immediately inside cooldown");
 require(mal.discoverPageFiltered("g03-refresh",refreshedFilter,true,96,24).value("items").toList().size()==1,
         "MAL page remains readable while retry is in flight");
 until([&]{return !mal.remotePending();});
 require(calls==retryStartCalls+1 && lastMethod=="discoverPageFiltered"
         && lastArgs.at(0).toString()=="g03-refresh"
         && lastArgs.at(1).toArray()==QJsonArray::fromVariantList(refreshedFilter)
         && lastArgs.at(2).toBool() && lastArgs.at(3).toInt()==96 && lastArgs.at(4).toInt()==24
         && pageState("g03-refresh",refreshedFilter,true,96).value("status").toString()=="success"
         && pageState("g03-refresh",refreshedFilter,true,96).value("error").toString().isEmpty()
         && pageState("g03-peer",{},false,120).value("status").toString()=="failure"
         && pageNotices.contains(refreshKey)
         && !mal.retryDiscoverPageFiltered("g03-refresh",refreshedFilter,true,96,24),
         "MAL targeted retry recovers exact page only without touching failed peer");
 const int offlinePageCalls=calls;
 mal.configureRemote(nullptr,endpoint,malCache);
 require(!pageState("g03-refresh",refreshedFilter,true,96).value("enabled").toBool()
         && pageState("g03-refresh",refreshedFilter,true,96).value("cached").toBool()
         && pageState("g03-refresh",refreshedFilter,true,96).value("status").toString()=="success"
         && mal.discoverPageFiltered("g03-refresh",refreshedFilter,true,96,24).value("items").toList().size()==1
         && calls==offlinePageCalls,"offline MAL page state exposes durable cached result");
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
 // F2a: exact-query state; the legacy global pending()/failed(method) stay compatible.
 delayed=false;
 CatalogRemote scoped("imdb"); const QString scopedCache=temp.filePath("scoped.json");
 scoped.configure(&nam,endpoint,scopedCache);
 auto state=[&](const QString& arg){ return scoped.queryState("titleCatalog",QVariantList{arg},QVariantList{}); };
 QStringList notified;
 QObject::connect(&scoped,&CatalogRemote::queryChanged,&app,[&](const QString& id){notified.append(id);});
 failArg="f2-fail";holdArg="f2-slow";
 scoped.read("titleCatalog",{"f2-fail"},QVariantList{});
 scoped.read("titleCatalog",{"f2-slow"},QVariantList{});
 const QString failedKey=state("f2-fail").value("key").toString();
 const QString slowKey=state("f2-slow").value("key").toString();
 require(failedKey!=slowKey && state("f2-fail").value("waiting").toBool()
         && state("f2-slow").value("pending").toBool(),"same-method query keys and pending are independent");
 until([&]{return state("f2-fail").value("status").toString()=="failure" && heldSocket;});
 require(state("f2-fail").value("error").toString()=="HTTP_503"
         && !state("f2-fail").value("cached").toBool()
         && state("f2-slow").value("pending").toBool()
         && state("f2-slow").value("error").toString().isEmpty()
         && notified.contains(failedKey) && heldSocket && !heldWire.isEmpty(),
         "one failing query does not complete or relabel held peer");
 heldSocket->write(heldWire);heldSocket->disconnectFromHost();
 heldSocket=nullptr;heldWire.clear();holdArg.clear();
 until([&]{return !scoped.pending();});
 require(state("f2-slow").value("status").toString()=="success"
         && state("f2-slow").value("cached").toBool() && notified.contains(slowKey),
         "same-method peer succeeds independently");
 slowArg.clear();
 emptyArg="f2-empty";
 scoped.read("titleCatalog",{"f2-empty"},QVariantList{});
 until([&]{return !scoped.pending();});
 require(state("f2-empty").value("status").toString()=="success"
         && state("f2-empty").value("cached").toBool()
         && scoped.read("titleCatalog",{"f2-empty"},QVariantList{}).toList().isEmpty()
         && state("f2-fail").value("status").toString()=="failure",
         "validated empty success differs from uncached failed fallback");

 scoped.read("titleCatalog",{"f2-cache"},QVariantList{});
 until([&]{return !scoped.pending();});
 QFile healthy(scopedCache);require(healthy.open(QIODevice::ReadOnly),"F2 cache on disk");
 const QByteArray healthyBytes=healthy.readAll();healthy.close();
 failArg="f2-cache";const int beforeFailure=calls;
 scoped.read("titleCatalog",{"f2-cache"},QVariantList{},true);
 until([&]{return !scoped.pending();});
 require(calls==beforeFailure+1
         && state("f2-cache").value("status").toString()=="failure"
         && state("f2-cache").value("cached").toBool()
         && state("f2-cache").value("error").toString()=="HTTP_503"
         && state("f2-cache").value("retryable").toBool()
         && scoped.read("titleCatalog",{"f2-cache"},QVariantList{}).toList().size()==1,
         "failed refresh preserves validated cache and records exact-query failure");
 require(healthy.open(QIODevice::ReadOnly) && healthy.readAll()==healthyBytes,
         "failed refresh never overwrites durable cache"); healthy.close();
 const int cooledCalls=calls;
 scoped.read("titleCatalog",{"f2-cache"},QVariantList{},true);
 require(calls==cooledCalls && !state("f2-cache").value("pending").toBool(),
         "force/automatic path still respects 30s failure cooldown");
 failArg.clear();
 require(scoped.retry("titleCatalog",{"f2-cache"},QVariantList{})
         && !scoped.retry("titleCatalog",{"f2-cache"},QVariantList{}),
         "manual retry bypasses cooldown once and dedupes in flight");
 scoped.read("titleCatalog",{"f2-cache"},QVariantList{},true);
 until([&]{return !scoped.pending();});
 require(calls==cooledCalls+1
         && state("f2-cache").value("status").toString()=="success"
         && state("f2-cache").value("error").toString().isEmpty(),
         "manual retry completes once and clears failure on success");
 const int uncachedRetryCalls=calls;
 require(scoped.retry("titleCatalog",{"f2-fail"},QVariantList{}),
         "uncached failed exact query retryable");
 until([&]{return !scoped.pending();});
 require(calls==uncachedRetryCalls+1 && state("f2-fail").value("status").toString()=="success"
         && state("f2-fail").value("cached").toBool(),"uncached failure recovers inside cooldown");

 failArg="f2-orphan";
 scoped.read("titleCatalog",{"f2-orphan"},QVariantList{});
 until([&]{return !scoped.pending();});
 require(state("f2-orphan").value("status").toString()=="failure","orphan error established");
 failArg.clear();sourceRevision=QString(64,QLatin1Char('c'));
 scoped.read("titleCatalog",{"f2-revision"},QVariantList{});
 until([&]{return !scoped.pending();});
 require(state("f2-orphan").value("status").toString()=="missing"
         && state("f2-orphan").value("error").toString().isEmpty(),
         "source revision invalidation clears stale query errors");

 slowArg="f2-late"; const int beforeLate=calls;
 scoped.read("titleCatalog",{"f2-late"},QVariantList{});
 until([&]{return calls>beforeLate;});
 require(state("f2-late").value("pending").toBool(),"old generation request in flight");
 scoped.configure(&nam,endpoint,temp.filePath("f2-reset.json"));
 require(state("f2-late").value("status").toString()=="missing"
         && state("f2-late").value("error").toString().isEmpty(),"configuration clears query state");
 QElapsedTimer drain;drain.start();
 while(drain.elapsed()<260) QCoreApplication::processEvents(QEventLoop::AllEvents,10);
 require(state("f2-late").value("status").toString()=="missing"
         && !state("f2-late").value("cached").toBool(),"late old generation cannot restore data/error");
 slowArg.clear();scoped.read("titleCatalog",{"f2-present"},QVariantList{});
 until([&]{return !scoped.pending();});
 require(state("f2-present").value("status").toString()=="success"
         && state("f2-late").value("status").toString()=="missing",
         "new configuration response unaffected by old reply");
 const int offlineCalls=calls;
 scoped.configure(nullptr,endpoint,temp.filePath("f2-reset.json"));
 require(!state("f2-present").value("enabled").toBool()
         && state("f2-present").value("cached").toBool()
         && state("f2-present").value("status").toString()=="success"
         && scoped.read("titleCatalog",{"f2-present"},QVariantList{}).toList().size()==1
         && calls==offlineCalls,"offline query state preserves useful validated cache");
 // Query subscribers may reconfigure a source. Enqueue notification must not
 // re-enter read() while it still holds a cache iterator, or dispatch old work
 // after a notification listener has replaced the configuration.
 CatalogRemote notificationReset("imdb"); notificationReset.configure(&nam,endpoint,{});
 int enqueueNotices=0;
 QObject::connect(&notificationReset,&CatalogRemote::queryChanged,&app,[&](const QString&){
     ++enqueueNotices; notificationReset.configure(nullptr,QUrl(),{});
 });
 const int beforeResetSessions=sessions, beforeResetCalls=calls;
 notificationReset.read("titleCatalog",{"f2-enqueue-reset"},QVariantList{});
 require(enqueueNotices==0,"enqueue notification must not reenter synchronous cache read");
 until([&]{return enqueueNotices==1;});
 QCoreApplication::processEvents();
 require(!notificationReset.pending() && sessions==beforeResetSessions && calls==beforeResetCalls,
         "enqueue subscriber reconfiguration cannot dispatch old-source work");
 // SQLite stays authoritative even if a remote endpoint is configured.
 const QString localMalDb=temp.filePath("mal-local-fixture.sqlite");
 {
  QSqlDatabase db=QSqlDatabase::addDatabase("QSQLITE","mal-page-test-seed");
  db.setDatabaseName(localMalDb);
  require(db.open(),"local MAL SQLite fixture opened");
  QSqlQuery sql(db);
  require(sql.exec("CREATE TABLE manga(mal_id INTEGER PRIMARY KEY,title TEXT,title_english TEXT,"
                   "type TEXT,score REAL,scored_by INTEGER,members INTEGER,favorites INTEGER,"
                   "year INTEGER,start_date TEXT,cover TEXT,tags TEXT,explicit INTEGER)")
          && sql.exec("CREATE TABLE classification(medium TEXT,axis TEXT,value TEXT,mal_id INTEGER)")
          && sql.exec("INSERT INTO manga VALUES (44,'Local Manga','','Manga',9.3,20000,"
                   "100000,100,2020,'2020-01-01','local.png','[]',0)")
          && sql.exec("INSERT INTO classification VALUES ('manga','genre','Romance',44)"),
          "local MAL SQLite fixture seeded");
  db.close();
 }
 QSqlDatabase::removeDatabase("mal-page-test-seed");
 {
  MalCatalog localMal(localMalDb);
  localMal.configureRemote(&nam,endpoint,temp.filePath("mal-local-unused.json"));
  const int beforeLocal=calls;
  const auto localState=localMal.discoverPageFilteredState("popular",{},false,0,24);
  const auto localPage=localMal.discoverPageFiltered("popular",{},false,0,24);
  require(localMal.ready() && localMal.remoteEnabled()
          && localState.value("source").toString()=="local"
          && localState.value("status").toString()=="success"
          && localState.value("key").toString().isEmpty()
          && !localState.value("enabled").toBool()
          && !localState.value("retryable").toBool()
          && localPage.value("items").toList().size()==1
          && localPage.value("items").toList().at(0).toMap().value("title").toString()=="Local Manga"
          && !localMal.retryDiscoverPageFiltered("popular",{},false,0,24)
          && !localMal.remotePending() && calls==beforeLocal,
          "ready SQLite MAL answers real page without remote wait or retry");
  until([&]{return !localMal.cachedDiscoverFilters("genre",false).isEmpty();});
 }
 std::cout<<"PASS async semantic RPC: existing fixture plus F2a and F2b MAL page projection, notifications, failed cache, retry, offline and SQLite paths\n";
}
