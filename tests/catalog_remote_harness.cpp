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
     QString selector=lastArgs.isEmpty() ? QString() : lastArgs.at(0).toString();
     if(selector.isEmpty() && !lastArgs.isEmpty() && lastArgs.at(0).isObject()) {
      const auto qObj=lastArgs.at(0).toObject();
      selector=qObj.value("genre").toString();
      if(selector.isEmpty()) selector=qObj.value("tag").toString();
      if(selector.isEmpty()) selector=qObj.value("type").toString();
     }
     if(lastMethod=="genreEntries" && lastArgs.size()>1) selector=lastArgs.at(1).toString();
     if (!failArg.isEmpty() && selector==failArg) status=503;
     require(header.contains("Bearer guest"),"guest bearer on semantic request");
     if(rejectOnce) { rejectOnce=false; status=401; }
     QJsonValue result=QJsonArray{QJsonObject{{"tt","tt001"},{"title",invalidResult ? "Invalid" : "Catalogued"},{"year",2020}}};
     if(lastMethod=="search" || lastMethod=="matchByTitle") result=QJsonArray{};
     if(lastMethod=="mangaById" || lastMethod=="series") result=QJsonObject{{"title","Detail"}};
     if(lastMethod=="animeCatalog" || lastMethod=="genreEntries")
      result=QJsonArray{QJsonObject{{"mal_id",44},{"title","Remote Anime"},{"type","TV"},{"year",2021},{"score",8.4},{"members",50000}}};
     if (!emptyArg.isEmpty() && selector==emptyArg) result=QJsonArray{};
     if(lastMethod=="discoverPageFiltered") {
      const bool comicsPage=path.startsWith("/v1/catalogue/comics/");
      const int pageOffset=lastArgs.at(3).toInt();
      QJsonArray items;
      if (selector!=emptyArg) {
       if(comicsPage)
        items.append(QJsonObject{{"locgId","comic-44"},{"title","Remote Comic"},
                                 {"year",2022},{"publisher","Local Press"},
                                 {"cover","remote.png"},{"genres","Adventure"},
                                 {"availability",true},{"explicit",false},
                                 {"houseScore",0.65},
                                 {"houseComponents",QJsonObject{{"popularity",0.65},
                                                                {"availability",0.0},
                                                                {"recency",0.0},
                                                                {"metadata",0.0}}}});
       else items.append(QJsonObject{{"mal_id",44},{"title","Remote Manga"},
                                     {"availability",false}});
      }
      QJsonObject page{{"items",items},{"nextOffset",pageOffset+items.size()},
                       {"exhausted",true},{"freshness","bundled"}};
      if(!comicsPage) page.insert("fallbackCatalog",selector=="trending" ? "popular" : "");
      result=page;
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
 ComicsCatalog comics(""); const QString comicsCache=temp.filePath("comics.json");
 comics.configureRemote(&nam,endpoint,comicsCache);
 comics.series(12); until([&]{return !comics.remotePending();}); require(comics.series(12).value("title")=="Detail","Comics map adapter");
 // F2c Comics page query-state/retry projection: typed QML methods and key signal.
 const QMetaObject* comicsMeta=comics.metaObject();
 const int comicsStateMethod=comicsMeta->indexOfMethod("discoverPageFilteredState(QString,QVariantList,bool,int,int)");
 const int comicsRetryMethod=comicsMeta->indexOfMethod("retryDiscoverPageFiltered(QString,QVariantList,bool,int,int)");
 const int comicsNoticeSignal=comicsMeta->indexOfSignal("remoteQueryChanged(QString)");
 require(comicsStateMethod>=0 && comicsRetryMethod>=0 && comicsNoticeSignal>=0,
         "Comics page state/retry and key notice are QML-invokable");
 require(QByteArray(comicsMeta->method(comicsStateMethod).typeName())=="QVariantMap"
         && QByteArray(comicsMeta->method(comicsRetryMethod).typeName())=="bool"
         && comicsMeta->method(comicsNoticeSignal).methodType()==QMetaMethod::Signal,
         "Comics page projection exposes typed metaobject methods and signal");
 const QVariantList comicsGenre{QVariantMap{{"axis","genre"},{"key","Adventure"}}};
 const QVariantList comicsPublisher{QVariantMap{{"axis","publisher"},{"key","Local Press"}}};
 const auto comicsState=[&](const QString& id,const QVariantList& filters,bool adult,int offset,int limit=17) {
     return comics.discoverPageFilteredState(id,filters,adult,offset,limit);
 };
 QStringList comicsNotices;
 QObject::connect(&comics,&ComicsCatalog::remoteQueryChanged,&app,[&](const QString& key) {
     comicsNotices.append(key);
 });
 const int beforeComicsStates=calls;
 const auto comicsFirst=comicsState("popular",comicsGenre,false,0);
 const auto comicsOffset=comicsState("popular",comicsGenre,false,17);
 const auto comicsFilter=comicsState("popular",comicsPublisher,false,17);
 const auto comicsExplicit=comicsState("popular",comicsGenre,true,17);
 const auto comicsLimit=comicsState("popular",comicsGenre,false,17,18);
 const auto comicsRawOffset=comicsState("popular",comicsGenre,false,-1);
 require(comicsFirst.value("source").toString()=="remote"
         && comicsFirst.value("status").toString()=="missing"
         && comicsFirst.value("enabled").toBool()
         && !comicsFirst.value("waiting").toBool()
         && !comicsFirst.value("cached").toBool()
         && !comicsFirst.value("retryable").toBool()
         && !comicsFirst.value("key").toString().isEmpty()
         && comicsFirst.value("key")!=comicsOffset.value("key")
         && comicsOffset.value("key")!=comicsFilter.value("key")
         && comicsOffset.value("key")!=comicsExplicit.value("key")
         && comicsOffset.value("key")!=comicsLimit.value("key")
         && comicsFirst.value("key")!=comicsRawOffset.value("key")
         && calls==beforeComicsStates && comicsNotices.isEmpty(),
         "Comics page inspection does not fetch; original filters/explicit/offset/limit isolate keys");
 const auto comicsCold=comics.discoverPageFiltered("popular",comicsGenre,false,0,17);
 const auto comicsPeerCold=comics.discoverPageFiltered("popular",comicsGenre,false,17,17);
 const auto comicsFilteredCold=comics.discoverPageFiltered("popular",comicsPublisher,false,17,17);
 require(comicsCold.value("items").toList().isEmpty()
         && comicsCold.value("nextOffset").toInt()==0
         && comicsCold.value("exhausted").toBool()
         && comicsCold.value("freshness").toString()=="bundled"
         && comicsPeerCold.value("nextOffset").toInt()==17
         && comicsFilteredCold.value("nextOffset").toInt()==17
         && comicsState("popular",comicsGenre,false,0).value("waiting").toBool()
         && comicsState("popular",comicsGenre,false,17).value("pending").toBool()
         && comicsNotices.isEmpty(),
         "Comics cold fallback retained, enqueue key notices deferred");
 until([&]{return comicsState("popular",comicsGenre,false,0).value("status").toString()=="success"
                 && comicsState("popular",comicsGenre,false,17).value("status").toString()=="success"
                 && comicsState("popular",comicsPublisher,false,17).value("status").toString()=="success";});
 const auto comicsLoaded=comics.discoverPageFiltered("popular",comicsGenre,false,0,17);
 const auto comicsPage17=comics.discoverPageFiltered("popular",comicsGenre,false,17,17);
 require(calls==beforeComicsStates+3
         && comicsLoaded.value("items").toList().size()==1
         && comicsLoaded.value("items").toList().first().toMap().value("locgId").toString()=="comic-44"
         && comicsLoaded.value("items").toList().first().toMap().value("title").toString()=="Remote Comic"
         && comicsLoaded.value("items").toList().first().toMap().value("houseComponents").toMap().contains("popularity")
         && comicsPage17.value("nextOffset").toInt()==18
         && comicsLoaded.value("freshness").toString()=="bundled"
         && !comicsLoaded.contains("fallbackCatalog")
         && comicsNotices.contains(comicsFirst.value("key").toString())
         && comicsNotices.contains(comicsOffset.value("key").toString())
         && comicsNotices.contains(comicsFilter.value("key").toString()),
         "Comics real typed result, independent async page states and exact notices");
 const int afterComicsRead=calls;
 comicsState("popular",comicsGenre,false,0); comicsState("popular",comicsGenre,false,17);
 require(calls==afterComicsRead,"Comics state inspection remains nonfetching after success");
 comics.discoverPageFiltered("g04-wire",comicsGenre,true,86,19);
 until([&]{return comicsState("g04-wire",comicsGenre,true,86,19).value("status").toString()=="success";});
 require(lastMethod=="discoverPageFiltered"
         && lastArgs==QJsonArray::fromVariantList(QVariantList{"g04-wire",comicsGenre,true,86,19}),
         "Comics request retains exact unnormalized five wire arguments");
 emptyArg="g04-empty"; failArg="g04-fail";
 comics.discoverPageFiltered("g04-empty",{},false,48,17);
 comics.discoverPageFiltered("g04-fail",{},false,72,17);
 until([&]{return !comics.remotePending();});
 const auto comicsEmpty=comics.discoverPageFiltered("g04-empty",{},false,48,17);
 const auto comicsFailed=comics.discoverPageFiltered("g04-fail",{},false,72,17);
 require(comicsEmpty.value("items").toList().isEmpty()
         && comicsEmpty.value("nextOffset").toInt()==48
         && comicsState("g04-empty",{},false,48).value("status").toString()=="success"
         && comicsState("g04-empty",{},false,48).value("cached").toBool()
         && comicsFailed.value("items").toList().isEmpty()
         && comicsFailed.value("nextOffset").toInt()==72
         && comicsState("g04-fail",{},false,72).value("status").toString()=="failure"
         && !comicsState("g04-fail",{},false,72).value("cached").toBool()
         && comicsState("g04-fail",{},false,72).value("error").toString()=="HTTP_503",
         "Comics successful empty is not uncached failed fallback");
 emptyArg.clear(); failArg.clear();
 // A failing page must not change a same-method peer still held on the wire.
 holdArg="g04-held"; failArg="g04-held-fail";
 comics.discoverPageFiltered("g04-held",{},false,112,17);
 comics.discoverPageFiltered("g04-held-fail",{},false,129,17);
 const QString heldComicsKey=comicsState("g04-held",{},false,112).value("key").toString();
 until([&]{return comicsState("g04-held-fail",{},false,129).value("status").toString()=="failure" && heldSocket;});
 require(comicsState("g04-held",{},false,112).value("pending").toBool()
         && !comicsState("g04-held",{},false,112).value("cached").toBool()
         && comicsState("g04-held",{},false,112).value("error").toString().isEmpty()
         && comicsNotices.contains(heldComicsKey) && !heldWire.isEmpty(),
         "Comics peer remains waiting while separate page fails");
 heldSocket->write(heldWire); heldSocket->disconnectFromHost();
 heldSocket=nullptr; heldWire.clear(); holdArg.clear();failArg.clear();
 until([&]{return comicsState("g04-held",{},false,112).value("status").toString()=="success";});
 require(comicsState("g04-held-fail",{},false,129).value("status").toString()=="failure",
         "Comics peer completion does not erase other page failure");
 // Make actual durable cache entry stale to force revalidation without private hooks.
 const QVariantList comicsRefreshFacet{QVariantMap{{"axis","format"},{"key","Paperback"}}};
 comics.discoverPageFiltered("g04-refresh",comicsRefreshFacet,true,96,17);
 until([&]{return comicsState("g04-refresh",comicsRefreshFacet,true,96).value("status").toString()=="success";});
 const QString comicsRefreshKey=comicsState("g04-refresh",comicsRefreshFacet,true,96).value("key").toString();
 QFile comicsDisk(comicsCache); require(comicsDisk.open(QIODevice::ReadOnly),"Comics cache on disk");
 auto comicsDocument=QJsonDocument::fromJson(comicsDisk.readAll()).object();comicsDisk.close();
 auto comicsEntries=comicsDocument.value("entries").toObject();
 require(comicsEntries.contains(comicsRefreshKey),"Comics exact refresh key persisted");
 auto comicsEntry=comicsEntries.value(comicsRefreshKey).toObject();
 comicsEntry.insert("fetchedAt","2020-01-01T00:00:00Z");
 comicsEntries.insert(comicsRefreshKey,comicsEntry);comicsDocument.insert("entries",comicsEntries);
 const QByteArray staleComicsBytes=QJsonDocument(comicsDocument).toJson(QJsonDocument::Compact);
 require(comicsDisk.open(QIODevice::WriteOnly|QIODevice::Truncate)
         && comicsDisk.write(staleComicsBytes)==staleComicsBytes.size(),
         "Comics durable page cache staled");comicsDisk.close();
 comics.configureRemote(&nam,endpoint,comicsCache);
 require(comicsState("g04-refresh",comicsRefreshFacet,true,96).value("cached").toBool(),
         "Comics stale validated disk cache loads");
 failArg="g04-peer";
 comics.discoverPageFiltered("g04-peer",{},false,147,17);
 until([&]{return comicsState("g04-peer",{},false,147).value("status").toString()=="failure";});
 failArg="g04-refresh";
 const auto staleComicsRead=comics.discoverPageFiltered("g04-refresh",comicsRefreshFacet,true,96,17);
 require(staleComicsRead.value("items").toList().size()==1
         && comicsState("g04-refresh",comicsRefreshFacet,true,96).value("pending").toBool()
         && comicsState("g04-refresh",comicsRefreshFacet,true,96).value("cached").toBool()
         && !comicsState("g04-refresh",comicsRefreshFacet,true,96).value("waiting").toBool(),
         "Comics stale validated page remains readable during refresh");
 until([&]{return comicsState("g04-refresh",comicsRefreshFacet,true,96).value("status").toString()=="failure";});
 const auto failedComicsRefresh=comicsState("g04-refresh",comicsRefreshFacet,true,96);
 require(failedComicsRefresh.value("cached").toBool()
         && failedComicsRefresh.value("retryable").toBool()
         && failedComicsRefresh.value("error").toString()=="HTTP_503"
         && comics.discoverPageFiltered("g04-refresh",comicsRefreshFacet,true,96,17).value("items").toList().size()==1,
         "Comics failed refresh retains visible cached page and exact failure");
 require(comicsDisk.open(QIODevice::ReadOnly) && comicsDisk.readAll()==staleComicsBytes,
         "Comics failed refresh preserves durable validated bytes");comicsDisk.close();
 failArg.clear(); const int comicsBeforeRetry=calls;
 require(comics.retryDiscoverPageFiltered("g04-refresh",comicsRefreshFacet,true,96,17)
         && !comics.retryDiscoverPageFiltered("g04-refresh",comicsRefreshFacet,true,96,17)
         && !comics.retryDiscoverPageFiltered("g04-wire",comicsGenre,true,86,19),
         "Comics exact failed key retried inside cooldown; dedupe and unrelated-key refusal");
 require(comics.discoverPageFiltered("g04-refresh",comicsRefreshFacet,true,96,17).value("items").toList().size()==1,
         "Comics cached page readable during explicit retry");
 until([&]{return comicsState("g04-refresh",comicsRefreshFacet,true,96).value("status").toString()=="success";});
 require(calls==comicsBeforeRetry+1 && lastMethod=="discoverPageFiltered"
         && lastArgs==QJsonArray::fromVariantList(QVariantList{"g04-refresh",comicsRefreshFacet,true,96,17})
         && comicsState("g04-refresh",comicsRefreshFacet,true,96).value("error").toString().isEmpty()
         && comicsState("g04-peer",{},false,147).value("status").toString()=="failure"
         && comicsNotices.contains(comicsRefreshKey)
         && !comics.retryDiscoverPageFiltered("g04-refresh",comicsRefreshFacet,true,96,17),
         "Comics explicit retry recovers one query, retaining independent failed peer");
 const int beforeComicsOffline=calls;
 comics.configureRemote(nullptr,endpoint,comicsCache);
 require(!comicsState("g04-refresh",comicsRefreshFacet,true,96).value("enabled").toBool()
         && comicsState("g04-refresh",comicsRefreshFacet,true,96).value("source").toString()=="remote"
         && comicsState("g04-refresh",comicsRefreshFacet,true,96).value("status").toString()=="success"
         && comicsState("g04-refresh",comicsRefreshFacet,true,96).value("cached").toBool()
         && comics.discoverPageFiltered("g04-refresh",comicsRefreshFacet,true,96,17).value("items").toList().size()==1
         && !comics.retryDiscoverPageFiltered("g04-refresh",comicsRefreshFacet,true,96,17)
         && calls==beforeComicsOffline,"Comics durable validated page survives offline");
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
          && localMal.animeCatalogState(QVariantMap{{"order","members"}},0,24).value("source").toString()=="local"
          && localMal.animeCatalogState(QVariantMap{{"order","members"}},0,24).value("status").toString()=="success"
          && !localMal.retryAnimeCatalog(QVariantMap{{"order","members"}},0,24)
          && localMal.genreEntriesState("anime","Action","members",13).value("source").toString()=="local"
          && localMal.genreEntriesState("anime","Action","members",13).value("status").toString()=="success"
          && !localMal.retryGenreEntries("anime","Action","members",13)
          && !localMal.remotePending() && calls==beforeLocal,
          "ready SQLite MAL answers real page and Theatre anime queries without remote wait or retry");
  until([&]{return !localMal.cachedDiscoverFilters("genre",false).isEmpty();});
 }
 // SQLite is authoritative for Comics too, independent of configured remote.
 const QString localComicsDb=temp.filePath("comics-local-fixture.sqlite");
 {
  QSqlDatabase db=QSqlDatabase::addDatabase("QSQLITE","comics-page-test-seed");
  db.setDatabaseName(localComicsDb);
  require(db.open(),"local Comics SQLite fixture opened");
  QSqlQuery sql(db);
  require(sql.exec("CREATE TABLE series(gcd_id INTEGER PRIMARY KEY,title TEXT)")
          && sql.exec("CREATE TABLE curated_series(locg_id TEXT PRIMARY KEY,rank INTEGER,title TEXT,"
                      "norm_title TEXT,year INTEGER,publisher TEXT,cover TEXT,synopsis TEXT)")
          && sql.exec("CREATE TABLE curated_edition(locg_id TEXT,available INTEGER,"
                      "getcomics_post TEXT,published TEXT,format TEXT)")
          && sql.exec("CREATE TABLE curated_genre(locg_id TEXT,genre TEXT)")
          && sql.exec("INSERT INTO series VALUES (12,'Local Comic')")
          && sql.exec("INSERT INTO curated_series VALUES "
                      "('local-comic',1,'Local Comic','local comic',2020,'Local Press','local.png','Local edition')")
          && sql.exec("INSERT INTO curated_edition VALUES "
                      "('local-comic',1,'https://example.invalid/local','2020','Paperback')")
          && sql.exec("INSERT INTO curated_genre VALUES ('local-comic','Adventure')"),
          "local Comics curated SQLite fixture seeded");
  db.close();
 }
 QSqlDatabase::removeDatabase("comics-page-test-seed");
 {
  ComicsCatalog localComics(localComicsDb);
  localComics.configureRemote(&nam,endpoint,temp.filePath("comics-local-unused.json"));
  const int beforeLocalComics=calls;
  const auto localState=localComics.discoverPageFilteredState("all",comicsGenre,false,0,17);
  const auto localPage=localComics.discoverPageFiltered("all",comicsGenre,false,0,17);
  require(localComics.ready() && localComics.curatedReady() && localComics.remoteEnabled()
          && localState.value("source").toString()=="local"
          && localState.value("status").toString()=="success"
          && localState.value("key").toString().isEmpty()
          && !localState.value("enabled").toBool()
          && !localState.value("pending").toBool()
          && !localState.value("waiting").toBool()
          && !localState.value("cached").toBool()
          && !localState.value("retryable").toBool()
          && localState.value("error").toString().isEmpty()
          && localPage.value("items").toList().size()==1
          && localPage.value("items").toList().first().toMap().value("locgId").toString()=="local-comic"
          && localPage.value("items").toList().first().toMap().value("title").toString()=="Local Comic"
          && !localComics.retryDiscoverPageFiltered("all",comicsGenre,false,0,17)
          && !localComics.remotePending() && calls==beforeLocalComics,
          "ready curated Comics SQLite takes precedence over configured remote");
 }
 // A01 Theatre: ImdbCatalog titleCatalogState/retryTitleCatalog/remoteQueryChanged and MalCatalog animeCatalogState/retryAnimeCatalog/genreEntriesState/retryGenreEntries.
 {
  ImdbCatalog theatreImdb(""); const QString theatreImdbCache=temp.filePath("theatre-imdb.json");
  theatreImdb.configureRemote(&nam,endpoint,theatreImdbCache);
  const QMetaObject* imdbMeta=theatreImdb.metaObject();
  const int imdbStateIdx=imdbMeta->indexOfMethod("titleCatalogState(QVariantMap,int,int)");
  const int imdbRetryIdx=imdbMeta->indexOfMethod("retryTitleCatalog(QVariantMap,int,int)");
  const int imdbNoticeIdx=imdbMeta->indexOfSignal("remoteQueryChanged(QString)");
  require(imdbStateIdx>=0 && imdbRetryIdx>=0 && imdbNoticeIdx>=0
          && QByteArray(imdbMeta->method(imdbStateIdx).typeName())=="QVariantMap"
          && QByteArray(imdbMeta->method(imdbRetryIdx).typeName())=="bool"
          && imdbMeta->method(imdbNoticeIdx).methodType()==QMetaMethod::Signal,
          "IMDb titleCatalog state/retry/signal visible to QML metaobject");
  const int malAnimeStateIdx=malMeta->indexOfMethod("animeCatalogState(QVariantMap,int,int)");
  const int malAnimeRetryIdx=malMeta->indexOfMethod("retryAnimeCatalog(QVariantMap,int,int)");
  const int malGenreStateIdx=malMeta->indexOfMethod("genreEntriesState(QString,QString,QString,int)");
  const int malGenreRetryIdx=malMeta->indexOfMethod("retryGenreEntries(QString,QString,QString,int)");
  require(malAnimeStateIdx>=0 && malAnimeRetryIdx>=0 && malGenreStateIdx>=0 && malGenreRetryIdx>=0,
          "MAL animeCatalog and genreEntries state/retry visible to QML metaobject");
  QStringList imdbNotices;
  QObject::connect(&theatreImdb,&ImdbCatalog::remoteQueryChanged,&app,[&](const QString& k){imdbNotices.append(k);});
  const QVariantMap qDrama{{"type","series"},{"genre","Drama"}};
  const QVariantMap qFail{{"type","series"},{"genre","a01-imdb-fail"}};
  const QVariantMap qEmpty{{"type","series"},{"genre","a01-imdb-empty"}};
  const int beforeImdbInspect=calls;
  const auto s0=theatreImdb.titleCatalogState(qDrama,0,24);
  const auto s24=theatreImdb.titleCatalogState(qDrama,24,24);
  require(s0.value("source").toString()=="remote" && s0.value("status").toString()=="missing"
          && s0.value("key")!=s24.value("key") && calls==beforeImdbInspect && imdbNotices.isEmpty(),
          "IMDb titleCatalogState is nonfetching and isolates offset keys");
  emptyArg="a01-imdb-empty"; failArg="a01-imdb-fail";
  theatreImdb.titleCatalog(qDrama,0,24);
  theatreImdb.titleCatalog(qEmpty,0,24);
  theatreImdb.titleCatalog(qFail,24,24);
  until([&]{return !theatreImdb.remotePending();});
  require(theatreImdb.titleCatalog(qDrama,0,24).size()==1
          && theatreImdb.titleCatalogState(qDrama,0,24).value("status").toString()=="success"
          && theatreImdb.titleCatalog(qEmpty,0,24).isEmpty()
          && theatreImdb.titleCatalogState(qEmpty,0,24).value("status").toString()=="success"
          && theatreImdb.titleCatalogState(qEmpty,0,24).value("cached").toBool()
          && theatreImdb.titleCatalog(qFail,24,24).isEmpty()
          && theatreImdb.titleCatalogState(qFail,24,24).value("status").toString()=="failure"
          && !theatreImdb.titleCatalogState(qFail,24,24).value("cached").toBool()
          && theatreImdb.titleCatalogState(qFail,24,24).value("retryable").toBool()
          && imdbNotices.contains(s0.value("key").toString()),
          "IMDb titleCatalog distinguishes remote success, valid empty, and uncached failure");
  emptyArg.clear(); failArg.clear();
  require(theatreImdb.retryTitleCatalog(qFail,24,24) && !theatreImdb.retryTitleCatalog(qFail,24,24),
          "IMDb retryTitleCatalog bypasses failure cooldown once and dedupes in flight");
  until([&]{return !theatreImdb.remotePending();});
  require(theatreImdb.titleCatalogState(qFail,24,24).value("status").toString()=="success"
          && theatreImdb.titleCatalog(qFail,24,24).size()==1,
          "IMDb retryTitleCatalog recovers exact failed query");
  MalCatalog theatreMal(""); theatreMal.configureRemote(&nam,endpoint,temp.filePath("theatre-mal.json"));
  const QVariantMap qAnimeFail{{"order","members"},{"tag","a01-anime-fail"}};
  failArg="a01-anime-fail";
  theatreMal.animeCatalog(qAnimeFail,0,40);
  theatreMal.genreEntries("anime","a01-anime-fail","members",13);
  until([&]{return !theatreMal.remotePending();});
  require(theatreMal.animeCatalogState(qAnimeFail,0,40).value("status").toString()=="failure"
          && theatreMal.animeCatalogState(qAnimeFail,0,40).value("retryable").toBool()
          && theatreMal.genreEntriesState("anime","a01-anime-fail","members",13).value("status").toString()=="failure"
          && theatreMal.genreEntriesState("anime","a01-anime-fail","members",13).value("retryable").toBool(),
          "MAL animeCatalogState and genreEntriesState surface uncached remote failures");
  failArg.clear();
  require(theatreMal.retryAnimeCatalog(qAnimeFail,0,40)
          && theatreMal.retryGenreEntries("anime","a01-anime-fail","members",13),
          "MAL retryAnimeCatalog and retryGenreEntries re-queue failed remote queries");
  until([&]{return !theatreMal.remotePending();});
  require(theatreMal.animeCatalogState(qAnimeFail,0,40).value("status").toString()=="success"
          && theatreMal.animeCatalog(qAnimeFail,0,40).size()==1
          && theatreMal.genreEntriesState("anime","a01-anime-fail","members",13).value("status").toString()=="success"
          && theatreMal.genreEntries("anime","a01-anime-fail","members",13).size()==1,
          "MAL retryAnimeCatalog and retryGenreEntries recover exact failed queries");
 }
 const QString localImdbDb=temp.filePath("imdb-local-fixture.sqlite");
 {
  QSqlDatabase db=QSqlDatabase::addDatabase("QSQLITE","imdb-page-test-seed");
  db.setDatabaseName(localImdbDb);
  require(db.open(),"local IMDb SQLite fixture opened");
  QSqlQuery sql(db);
  require(sql.exec("CREATE TABLE title(tt TEXT PRIMARY KEY,type TEXT,title TEXT,norm_title TEXT,"
                   "year INTEGER,endYear INTEGER,runtimeMin INTEGER,genres TEXT,rating REAL,"
                   "votes INTEGER,episodes INTEGER,origLang TEXT,isAnime INTEGER)")
          && sql.exec("CREATE TABLE genre(tt TEXT,genre TEXT)")
          && sql.exec("INSERT INTO title VALUES ('tt0133093','movie','The Matrix','matrix',"
                      "1999,0,136,'[\"Action\",\"Sci-Fi\"]',8.7,2000000,0,'en',0)"),
          "local IMDb SQLite fixture seeded");
  db.close();
 }
 QSqlDatabase::removeDatabase("imdb-page-test-seed");
 {
  ImdbCatalog localImdb(localImdbDb);
  localImdb.configureRemote(&nam,endpoint,temp.filePath("imdb-local-unused.json"));
  const int beforeLocalImdb=calls;
  const QVariantMap qMovie{{"type","movie"}};
  const auto localImdbState=localImdb.titleCatalogState(qMovie,0,24);
  const auto localImdbRows=localImdb.titleCatalog(qMovie,0,24);
  require(localImdb.ready() && localImdb.remoteEnabled()
          && localImdbState.value("source").toString()=="local"
          && localImdbState.value("status").toString()=="success"
          && localImdbState.value("key").toString().isEmpty()
          && !localImdbState.value("enabled").toBool()
          && !localImdbState.value("retryable").toBool()
          && localImdbRows.size()==1
          && localImdbRows.first().toMap().value("tt").toString()=="tt0133093"
          && !localImdb.retryTitleCatalog(qMovie,0,24)
          && !localImdb.remotePending() && calls==beforeLocalImdb,
          "ready SQLite IMDb answers titleCatalog synchronously with local state");
 }
 std::cout<<"PASS async semantic RPC: existing fixture plus F2a, MAL, Comics, and Theatre page projection, notifications, failure, retry, offline and SQLite paths\n";
}
