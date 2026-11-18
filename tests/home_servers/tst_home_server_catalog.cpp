#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include "HomeServerCatalog.h"
class CatalogReplay final : public QTcpServer {
public:
 struct Reply { int status=200; QByteArray body; int delay=0; };
 QList<Reply> responses; QList<QByteArray> requests;
 CatalogReplay() { listen(QHostAddress::LocalHost); connect(this,&QTcpServer::newConnection,this,[this] {
  while(auto *socket=nextPendingConnection()) {
   auto bytes=std::make_shared<QByteArray>();
   connect(socket,&QTcpSocket::readyRead,socket,[this,socket,bytes] {
    *bytes+=socket->readAll(); if(!bytes->contains("\r\n\r\n") || socket->property("answered").toBool()) return;
    socket->setProperty("answered",true); requests.append(*bytes);
    const auto response=responses.isEmpty()?Reply{500,"{}",0}:responses.takeFirst();
    QTimer::singleShot(response.delay,socket,[socket,response] {
     socket->write("HTTP/1.1 "+QByteArray::number(response.status)+" Replay\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(response.body.size())+"\r\n\r\n"+response.body); socket->disconnectFromHost();
    });
   }); connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
  }
 }); }
 QUrl origin() const { return QUrl("http://127.0.0.1:"+QString::number(serverPort())+"/proxy"); }
 static QByteArray fixture(const char *name) { QFile f(QStringLiteral(HOME_SERVER_FIXTURE_DIR)+"/catalog/"+name); return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
 QJsonObject connection(QString provider) const { return {{"id","11111111-1111-4111-8111-111111111111"},{"provider",provider},{"origin",origin().toString()},{"userId","user"},{"name","Replay"},{"enabled",true}}; }
};
class HomeServerCatalogTest : public QObject {
 Q_OBJECT
private slots:
 void libraryManifestOrderEmptyAndMembership_data() {
  QTest::addColumn<QString>("provider");
  for(auto provider:{"jellyfin","emby","plex"}) QTest::newRow(provider)<<QString(provider);
 }
 void libraryManifestOrderEmptyAndMembership() {
  QFETCH(QString,provider); CatalogReplay server; const bool plex=provider=="plex";
  if(provider=="jellyfin") server.responses.append({200,"{\"Version\":\"12.4.0\"}"});
  QJsonArray libraries;
  for(const auto &pair:QList<QPair<QString,QString>>{{"z","Zulu"},{"empty","Empty library"},{"a","Alpha"}})
   libraries.append(plex?QJsonObject{{"key",pair.first},{"title",pair.second},{"type","movie"}}
                        :QJsonObject{{"Id",pair.first},{"Name",pair.second},{"CollectionType","movies"}});
  server.responses.append({200,QJsonDocument(plex?QJsonObject{{"MediaContainer",QJsonObject{{"Directory",libraries}}}}:QJsonObject{{"Items",libraries}}).toJson()});
  for(int i=0;i<3;++i) {
   const QJsonArray rows=i==1?QJsonArray{}:QJsonArray{plex?QJsonObject{{"ratingKey","shared"},{"type","movie"},{"title","Movie"}}
                                                                    :QJsonObject{{"Id","shared"},{"Type","Movie"},{"Name","Movie"}}};
   server.responses.append({200,QJsonDocument(plex?QJsonObject{{"MediaContainer",QJsonObject{{"Metadata",rows},{"size",rows.size()},{"totalSize",rows.size()},{"offset",0}}}}
                                                   :QJsonObject{{"Items",rows},{"TotalRecordCount",rows.size()},{"StartIndex",0}}).toJson()});
  }
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false; HomeServerRefresh::Result result;
  job.start(server.connection(provider),"REPLAY",[&](auto r){result=r;done=true;}); QTRY_VERIFY(done); QVERIFY2(result.ok(),qPrintable(result.error));
  QCOMPARE(result.items.size(),1); QCOMPARE(result.libraries.size(),3);
  QCOMPARE(result.libraries.at(0).toObject().value("name").toString(),QString("Zulu"));
  QCOMPARE(result.libraries.at(1).toObject(),(QJsonObject{{"id","empty"},{"name","Empty library"},{"kind","movie"},{"itemIds",QJsonArray{}}}));
  QCOMPARE(result.libraries.at(2).toObject().value("name").toString(),QString("Alpha"));
  for(int i:{0,2}) QCOMPARE(result.libraries.at(i).toObject().value("itemIds").toArray(),QJsonArray{"shared"});
 }
 void strictIdentityAndCanonicalIds() {
  using namespace HomeServerCatalog;
  QVERIFY(!identitiesMatch({{"imdbId","tt1234567"}},{{"imdbId","tt1234567"}}));
  QVERIFY(identitiesMatch({{"type","movie"},{"imdbId","tt1234567"},{"tmdbId",77}},{{"type","movie"},{"imdbId","tt1234567"},{"tmdbId",88}}));
  QVERIFY(!identitiesMatch({{"type","movie"},{"imdbId","tt1234567"},{"tmdbId",77}},{{"type","movie"},{"imdbId","tt9999999"},{"tmdbId",77}}));
  QVERIFY(!identitiesMatch({{"type","movie"},{"tmdbId",77}},{{"type","series"},{"tmdbId",77}}));
  QVERIFY(identitiesMatch({{"type","tv"},{"tmdbId",77},{"season",0},{"episode",2}},{{"type","series"},{"id","tmdb:tv/77:0:2"}}));
  QVERIFY(!identitiesMatch({{"type","tv"},{"tmdbId",77},{"season",0},{"episode",2}},{{"type","series"},{"tmdbId",77},{"season",1},{"episode",2}}));
  QCOMPARE(canonicalVideoId({{"kind","movie"},{"identity",QJsonObject{{"tmdbId",77}}}}),QString("tmdb:movie:77"));
  QCOMPARE(canonicalVideoId({{"kind","series"},{"identity",QJsonObject{{"tvdbId",88}}}}),QString("tvdb:tv:88"));
  QVERIFY(canonicalVideoId({{"kind","movie"},{"identity",QJsonObject{}}}).isEmpty());
 }
 void normalizationAndSafeSources() {
  auto p1=QJsonDocument::fromJson(CatalogReplay::fixture("jellyfin-page1.json")).object().value("Items").toArray();
  auto p2=QJsonDocument::fromJson(CatalogReplay::fixture("jellyfin-page2.json")).object().value("Items").toArray();
  QJsonArray items; const QJsonObject library{{"id","shows"},{"name","Shows"}};
  for(auto v:p1) items.append(HomeServerCatalog::normalizeJellyfinItem(v.toObject(),library));
  for(auto v:p2) items.append(HomeServerCatalog::normalizeJellyfinItem(v.toObject(),library));
  items=HomeServerCatalog::inheritSeriesIdentity(items);
  auto episode=items.last().toObject(); QCOMPARE(episode.value("identity").toObject().value("imdbId").toString(),QString("tt1234567"));
  QCOMPARE(episode.value("progress").toObject().value("positionMs").toInteger(),15000); QCOMPARE(episode.value("progress").toObject().value("updatedAt").toInteger(),0);
  QCOMPARE(episode.value("progress").toObject().value("durationMs").toInteger(),60000);
  QVERIFY(!episode.value("versions").toArray().first().toObject().value("directPlayable").toBool());
  QCOMPARE(HomeServerCatalog::canonicalVideoId(episode),QString("tt1234567:0:2"));
  auto rows=HomeServerCatalog::matchingSources(items,{{"id","tt1234567:0:2"},{"type","series"}},{{"id","connection"},{"name","Replay"},{"enabled",true},{"token","SECRET"},{"origin","https://SECRET"}});
  QCOMPARE(rows.size(),1); auto row=rows.first().toMap(); QCOMPARE(row.value("infoHash").toString(),QString("home-server:connection:episode:version%2Fa"));
  QVERIFY(row.value("homeServer").toBool()); QVERIFY(!row.contains("url")); QVERIFY(!row.contains("headers"));
  QVERIFY(!QJsonDocument::fromVariant(rows).toJson().contains("SECRET")); QVERIFY(!QJsonDocument(items).toJson().contains("SECRET"));
  QVERIFY(HomeServerCatalog::matchingSources(items,{{"id","tt9999999:0:2"},{"tmdbId",77},{"type","series"}},{{"id","connection"},{"enabled",true}}).isEmpty());
  QVERIFY(HomeServerCatalog::matchingSources(items,{{"id","tt1234567:1:2"},{"type","series"}},{{"id","connection"},{"enabled",true}}).isEmpty());
 }
 void plexMultipleMediaAndTimestamps() {
  const auto raw=QJsonDocument::fromJson(CatalogReplay::fixture("plex-episodes1.json")).object().value("MediaContainer").toObject().value("Metadata").toArray().first().toObject();
  auto changed=raw; auto media=raw.value("Media").toArray();
  media.append(QJsonObject{{"title","Alternate"},{"supportsDirectPlay",false},{"Part",QJsonArray{QJsonObject{{"key","/library/parts/alternate/file.mkv"}}}}});
  changed.insert("Media",media); changed.insert("lastViewedAt",1700000000);
  auto item=HomeServerCatalog::normalizePlexItem(changed,{{"id","library"},{"name","Library"}});
  const auto versions=item.value("versions").toArray(); QCOMPARE(versions.size(),3); QCOMPARE(versions.last().toObject().value("mediaIndex").toInt(),1); QCOMPARE(versions.last().toObject().value("partIndex").toInt(),0);
  QVERIFY(!versions.last().toObject().value("directPlayable").toBool());
  QCOMPARE(item.value("progress").toObject().value("positionMs").toInteger(),15000);
  QCOMPARE(item.value("progress").toObject().value("updatedAt").toInteger(),1700000000000LL);
  QVERIFY(!item.value("identity").toObject().contains("imdbId")); QVERIFY(!QJsonDocument(item).toJson().contains("SECRET"));
 }
 void completeRefresh_data() { QTest::addColumn<QString>("provider"); QTest::addColumn<int>("major"); QTest::newRow("jf10")<<QString("jellyfin")<<10; QTest::newRow("jf12")<<QString("jellyfin")<<12; QTest::newRow("emby")<<QString("emby")<<0; QTest::newRow("plex")<<QString("plex")<<0; }
 void mixedVideoLibraries_data() { QTest::addColumn<QString>("provider"); QTest::newRow("jellyfin")<<QString("jellyfin"); QTest::newRow("emby")<<QString("emby"); }
 void mixedVideoLibraries() {
  QFETCH(QString,provider); CatalogReplay server;
  if(provider=="jellyfin") server.responses.append({200,"{\"Version\":\"12.4.0\"}"});
  server.responses.append({200,CatalogReplay::fixture("jellyfin-mixed-views.json")});
  for(int index=0;index<3;++index) server.responses.append({200,CatalogReplay::fixture("jellyfin-mixed-movie.json")});
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false; HomeServerRefresh::Result result;
  job.start(server.connection(provider),"REPLAY",[&](auto r){result=r;done=true;}); QTRY_VERIFY(done); QVERIFY2(result.ok(),qPrintable(result.error));
  QCOMPARE(result.items.size(),1); QCOMPARE(HomeServerCatalog::canonicalVideoId(result.items.first().toObject()),QString("tt1234567"));
  const int firstPage=provider=="jellyfin"?2:1; QCOMPARE(server.requests.size(),firstPage+3);
  for(int index=0;index<3;++index) {
   const auto id=QByteArray(index==0?"mixed":index==1?"empty":"folder");
   QVERIFY(server.requests.at(firstPage+index).contains((provider=="jellyfin"?"parentId=":"ParentId=")+id));
   QVERIFY(server.requests.at(firstPage+index).contains(provider=="jellyfin"?"includeItemTypes=":"IncludeItemTypes="));
  }
 }
 void mixedLibraryRequiresName() {
  CatalogReplay server; server.responses={{200,"{\"Items\":[{\"Id\":\"mixed\",\"Name\":\"  \",\"CollectionType\":\"mixed\"}]}"}};
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false; HomeServerRefresh::Result result;
  job.start(server.connection("emby"),"REPLAY",[&](auto r){result=r;done=true;}); QTRY_VERIFY(done); QVERIFY(!result.ok()); QVERIFY(result.items.isEmpty()); QCOMPARE(server.requests.size(),1);
 }
 void completeRefresh() {
  QFETCH(QString,provider); QFETCH(int,major); CatalogReplay server;
  if(provider=="jellyfin") server.responses.append({200,"{\"Version\":\""+QByteArray::number(major)+".4.0\"}"});
  if(provider=="plex") for(auto file:{"plex-sections.json","plex-shows.json","plex-seasons.json","plex-episodes1.json","plex-episodes2.json"}) server.responses.append({200,CatalogReplay::fixture(file)});
  else for(auto file:{"jellyfin-views.json","jellyfin-page1.json","jellyfin-page2.json"}) server.responses.append({200,CatalogReplay::fixture(file)});
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false; HomeServerRefresh::Result result;
  job.start(server.connection(provider),"REPLAY-TOKEN",[&](auto r){result=r;done=true;}); QTRY_VERIFY(done); QVERIFY2(result.ok(),qPrintable(result.error));
  QCOMPARE(result.items.size(),provider=="plex"?4:3); QCOMPARE(result.serverMajor,major);
  auto episode=result.items.at(2).toObject(); QCOMPARE(HomeServerCatalog::canonicalVideoId(episode),QString("tt1234567:0:2"));
  QCOMPARE(episode.value("progress").toObject().value("updatedAt").toInteger(),0);
  if(provider=="plex") { QCOMPARE(episode.value("versions").toArray().size(),2); QCOMPARE(episode.value("versions").toArray().at(1).toObject().value("partIndex").toInt(),1); QVERIFY(server.requests.last().contains("X-Plex-Container-Start=1")); }
  else { const auto views=server.requests.at(provider=="jellyfin"?1:0); QVERIFY(views.startsWith(major>=12?"GET /proxy/UserViews?userId=user ":"GET /proxy/Users/user/Views ")); QVERIFY(server.requests.last().contains(major>=12?"startIndex=2":"StartIndex=2")); }
 }
 void failedRefreshNeverReturnsPartial_data() { QTest::addColumn<QByteArray>("last"); QTest::addColumn<int>("status"); QTest::newRow("empty-before-total")<<QByteArray("{\"Items\":[],\"TotalRecordCount\":3,\"StartIndex\":2}")<<200; QTest::newRow("changed-total")<<QByteArray("{\"Items\":[],\"TotalRecordCount\":2,\"StartIndex\":2}")<<200; QTest::newRow("malformed")<<QByteArray("{}")<<200; QTest::newRow("unauthorized")<<QByteArray("{}")<<401; QTest::newRow("duplicate")<<QByteArray("{\"Items\":[{\"Id\":\"series\",\"Type\":\"Series\"}],\"TotalRecordCount\":3,\"StartIndex\":2}")<<200; }
 void failedRefreshNeverReturnsPartial() {
  QFETCH(QByteArray,last); QFETCH(int,status); CatalogReplay server; server.responses={{200,CatalogReplay::fixture("jellyfin-views.json")},{200,CatalogReplay::fixture("jellyfin-page1.json")},{status,last}};
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false; HomeServerRefresh::Result result;
  job.start(server.connection("emby"),"REPLAY",[&](auto r){result=r;done=true;}); QTRY_VERIFY(done); QVERIFY(!result.ok()); QVERIFY(result.items.isEmpty()); QVERIFY(result.libraries.isEmpty()); QCOMPARE(result.expired,status==401); QCOMPARE(server.requests.size(),3);
 }
 void blankLibrariesAndInfoFailure() {
  CatalogReplay server; server.responses={{200,"{}"}}; QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false;
  job.start(server.connection("jellyfin"),"REPLAY",[&](auto r){QVERIFY(!r.ok());QVERIFY(r.items.isEmpty());done=true;}); QTRY_VERIFY(done); QCOMPARE(server.requests.size(),1);
 }
 void cancellationSuppressesCallback() {
  CatalogReplay server; server.responses={{200,CatalogReplay::fixture("jellyfin-views.json"),150}}; QTemporaryDir dir; HomeServerClient client(nullptr,dir.path());
  bool callback=false; { HomeServerRefresh job(&client); job.start(server.connection("emby"),"REPLAY",[&](auto){callback=true;}); QTRY_COMPARE(server.requests.size(),1); job.cancel(); QTest::qWait(250); QVERIFY(!callback); }
  auto *job=new HomeServerRefresh(&client); server.responses={{200,CatalogReplay::fixture("jellyfin-views.json"),100}}; job->start(server.connection("emby"),"REPLAY",[&](auto){callback=true;}); delete job; QTest::qWait(150); QVERIFY(!callback);
 }
 void libraryShapes_data() {
  QTest::addColumn<QString>("provider"); QTest::addColumn<QByteArray>("body"); QTest::addColumn<bool>("success");
  QTest::newRow("emby-blank")<<QString("emby")<<QByteArray("{}")<<false;
  QTest::newRow("plex-blank")<<QString("plex")<<QByteArray("{\"MediaContainer\":{}}")<<false;
  QTest::newRow("emby-empty")<<QString("emby")<<QByteArray("{\"Items\":[]}")<<true;
  QTest::newRow("plex-empty")<<QString("plex")<<QByteArray("{\"MediaContainer\":{\"Directory\":[]}}")<<true;
  QTest::newRow("plex-explicit-zero-omitted-array")<<QString("plex")<<QByteArray("{\"MediaContainer\":{\"size\":0}}")<<true;
  QTest::newRow("plex-string-zero-is-invalid")<<QString("plex")<<QByteArray("{\"MediaContainer\":{\"size\":\"0\"}}")<<false;
 }
 void libraryShapes() {
  QFETCH(QString,provider); QFETCH(QByteArray,body); QFETCH(bool,success); CatalogReplay server; server.responses={{200,body}};
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false; HomeServerRefresh::Result result;
  job.start(server.connection(provider),"REPLAY",[&](auto r){result=r;done=true;}); QTRY_VERIFY(done); QCOMPARE(result.ok(),success); QVERIFY(result.items.isEmpty()); QCOMPARE(server.requests.size(),1);
 }
 void plexPageShapes_data() {
  QTest::addColumn<QByteArray>("body"); QTest::addColumn<bool>("success");
  QTest::newRow("unpaginated-with-size")<<QByteArray("{\"MediaContainer\":{\"size\":1,\"Metadata\":[{\"ratingKey\":\"movie\",\"type\":\"movie\",\"title\":\"Movie\"}]}}")<<true;
  QTest::newRow("missing-size-and-total")<<QByteArray("{\"MediaContainer\":{\"Metadata\":[]}}")<<false;
  QTest::newRow("explicit-zero-offset-missing-total")<<QByteArray("{\"MediaContainer\":{\"size\":0,\"offset\":0,\"Metadata\":[]}}")<<false;
  QTest::newRow("wrong-offset")<<QByteArray("{\"MediaContainer\":{\"size\":0,\"offset\":1,\"totalSize\":0,\"Metadata\":[]}}")<<false;
  QTest::newRow("nonarray-metadata")<<QByteArray("{\"MediaContainer\":{\"size\":0,\"totalSize\":0,\"Metadata\":{}}}")<<false;
  QTest::newRow("explicit-zero-stage-omitted-array")<<QByteArray("{\"MediaContainer\":{\"size\":0,\"totalSize\":0}}")<<true;
 }
 void plexPageShapes() {
  QFETCH(QByteArray,body); QFETCH(bool,success); CatalogReplay server;
  server.responses={{200,"{\"MediaContainer\":{\"Directory\":[{\"key\":\"movies\",\"title\":\"Movies\",\"type\":\"movie\"}]}}"},{200,body}};
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false; HomeServerRefresh::Result result;
  job.start(server.connection("plex"),"REPLAY",[&](auto r){result=r;done=true;}); QTRY_VERIFY(done); QCOMPARE(result.ok(),success); if(!success) QVERIFY(result.items.isEmpty()); QCOMPARE(server.requests.size(),2);
 }
 void crossLibraryDuplicates_data() { QTest::addColumn<bool>("contradiction"); QTest::newRow("same-physical-item")<<false; QTest::newRow("conflicting-identity")<<true; }
 void crossLibraryDuplicates() {
  QFETCH(bool,contradiction); CatalogReplay server;
  server.responses.append({200,"{\"Items\":[{\"Id\":\"first\",\"Name\":\"First\",\"CollectionType\":\"movies\"},{\"Id\":\"second\",\"Name\":\"Second\",\"CollectionType\":\"movies\"}]}"});
  auto item=QJsonObject{{"Id","shared"},{"Type","Movie"},{"Name","Movie"},{"ProviderIds",QJsonObject{{"Imdb","tt1234567"}}}};
  auto page=[&] { return QJsonDocument(QJsonObject{{"Items",QJsonArray{item}},{"TotalRecordCount",1},{"StartIndex",0}}).toJson(QJsonDocument::Compact); };
  server.responses.append({200,page()}); if(contradiction) item.insert("ProviderIds",QJsonObject{{"Imdb","tt9999999"}}); server.responses.append({200,page()});
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false; HomeServerRefresh::Result result;
  job.start(server.connection("emby"),"REPLAY",[&](auto r){result=r;done=true;}); QTRY_VERIFY(done); QCOMPARE(result.ok(),!contradiction);
  if(contradiction) QVERIFY(result.items.isEmpty()); else { QCOMPARE(result.items.size(),1); QCOMPARE(result.items.first().toObject().value("libraryId").toString(),QString("first")); }
 }
 void invalidNumericIdsAndUnsafePartKeys() {
  QVERIFY(HomeServerCatalog::normalizePlexItem({{"ratingKey",-1},{"type","movie"}},{}).isEmpty());
  QVERIFY(HomeServerCatalog::normalizePlexItem({{"ratingKey",1.5},{"type","movie"}},{}).isEmpty());
  const auto raw=QJsonObject{{"ratingKey","movie"},{"type","movie"},{"Media",QJsonArray{QJsonObject{{"Part",QJsonArray{QJsonObject{{"key","https://SECRET/file"}},QJsonObject{{"key","/library/parts/%2e%2e/SECRET"}}}}}}}};
  QVERIFY(HomeServerCatalog::normalizePlexItem(raw,{}).value("versions").toArray().isEmpty());
 }
 void normalizedSnapshotBudget() {
  CatalogReplay server; server.responses.append({200,"{\"Items\":[{\"Id\":\"movies\",\"Name\":\"Movies\",\"CollectionType\":\"movies\"}]}"});
  QJsonArray sources; const auto name=QString(4096,'x');
  for(int index=0;index<256;++index) sources.append(QJsonObject{{"Id",QString::number(index+1)},{"Name",name}});
  const int total=70, pageSize=4;
  for(int offset=0;offset<total;offset+=pageSize) {
   QJsonArray items; for(int index=offset;index<qMin(offset+pageSize,total);++index) items.append(QJsonObject{{"Id",QString::number(index+1)},{"Type","Movie"},{"Name","Movie"},{"MediaSources",sources}});
   server.responses.append({200,QJsonDocument(QJsonObject{{"Items",items},{"TotalRecordCount",total},{"StartIndex",offset}}).toJson(QJsonDocument::Compact)});
  }
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); HomeServerRefresh job(&client); bool done=false; HomeServerRefresh::Result result;
  job.start(server.connection("emby"),"REPLAY",[&](auto r){result=r;done=true;}); QTRY_VERIFY_WITH_TIMEOUT(done,30000);
  QVERIFY(!result.ok()); QVERIFY(result.items.isEmpty()); QCOMPARE(result.error,QString("Server catalog snapshot size limit exceeded"));
 }
};
QTEST_GUILESS_MAIN(HomeServerCatalogTest)
#include "tst_home_server_catalog.moc"
