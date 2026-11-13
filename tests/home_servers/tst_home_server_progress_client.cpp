#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QUrlQuery>
#include <limits>
#include "HomeServerProgressClient.h"

class ProgressReplay final : public QTcpServer {
public:
    struct Reply { int status=204; QByteArray body; int delay=0; };
    QList<Reply> replies;
    QList<QByteArray> requests;
    ProgressReplay() {
        listen(QHostAddress::LocalHost);
        connect(this,&QTcpServer::newConnection,this,[this] {
            while(auto *socket=nextPendingConnection()) {
                auto bytes=std::make_shared<QByteArray>();
                connect(socket,&QTcpSocket::readyRead,socket,[this,socket,bytes] {
                    *bytes+=socket->readAll();
                    const auto end=bytes->indexOf("\r\n\r\n");
                    if(end<0 || socket->property("answered").toBool()) return;
                    qsizetype length=0;
                    for(const auto &line:bytes->left(end).split('\n')) if(line.toLower().startsWith("content-length:")) length=line.mid(15).trimmed().toLongLong();
                    if(bytes->size()<end+4+length) return;
                    socket->setProperty("answered",true); requests.append(*bytes);
                    const auto reply=replies.isEmpty()?Reply{}:replies.takeFirst();
                    QTimer::singleShot(reply.delay,socket,[socket,reply] {
                        socket->write("HTTP/1.1 "+QByteArray::number(reply.status)+" Replay\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(reply.body.size())+"\r\n\r\n"+reply.body);
                        socket->disconnectFromHost();
                    });
                });
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
    }
    QJsonObject connection(QString provider,int major=12) const {
        return {{"provider",provider},{"origin","http://127.0.0.1:"+QString::number(serverPort())+"/proxy"},{"userId","user/a"},{"serverMajor",major}};
    }
    static QJsonObject item() { return {{"id","item/a"}}; }
    static HomeServerPlaybackJob::Result playback() { HomeServerPlaybackJob::Result r; r.mediaSourceId="chosen-source";r.playSessionId="chosen-session";r.playMethod="DirectPlay";return r; }
    static QByteArray fixture(const char *name) { QFile f(QStringLiteral(HOME_SERVER_FIXTURE_DIR)+"/progress/"+name);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
    static QJsonObject body(QByteArray request) { return QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n")+4)).object(); }
    static QUrl target(QByteArray request) { return QUrl::fromEncoded(request.left(request.indexOf("\r\n")).split(' ').value(1)); }
};
class HomeServerProgressClientTest : public QObject {
    Q_OBJECT
private slots:
    void sessionEvents_data() {
        QTest::addColumn<QString>("provider");QTest::addColumn<QString>("event");QTest::addColumn<bool>("paused");
        for(auto provider:{"jellyfin","emby"}) for(auto event:{"start","progress","stop"}) {
            const auto tag=QByteArray(provider)+'-'+event;QTest::newRow(tag.constData())<<QString(provider)<<QString(event)<<(QString(event)=="progress");
        }
    }
    void sessionEvents() {
        QFETCH(QString,provider);QFETCH(QString,event);QFETCH(bool,paused);
        ProgressReplay server;QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);
        HomeServerProgressState state;state.positionMs=1500;state.durationMs=60000;state.paused=paused;state.played=true;
        bool done=false;HomeServerProgressJob::Result result;
        job.start(server.connection(provider),ProgressReplay::item(),ProgressReplay::playback(),"REPLAY-TOKEN",event,state,[&](auto r){result=r;done=true;});
        QTRY_VERIFY(done);QVERIFY2(result.ok(),qPrintable(result.error));QCOMPARE(server.requests.size(),1);
        const auto expected=event=="start"?QByteArray("/Sessions/Playing"):event=="progress"?QByteArray("/Sessions/Playing/Progress"):QByteArray("/Sessions/Playing/Stopped");
        QVERIFY(server.requests.first().startsWith("POST /proxy"+expected+" HTTP/1.1"));
        const auto body=ProgressReplay::body(server.requests.first());QCOMPARE(body.value("ItemId").toString(),QString("item/a"));QCOMPARE(body.value("MediaSourceId").toString(),QString("chosen-source"));QCOMPARE(body.value("PlaySessionId").toString(),QString("chosen-session"));QCOMPARE(body.value("PositionTicks").toInteger(),15000000);
        QVERIFY(!body.contains("Played"));QVERIFY(!body.contains("DeviceId"));QVERIFY(server.requests.first().contains(client.installationId().toLatin1()));
        if(event!="stop") {QCOMPARE(body.value("IsPaused").toBool(),paused);QCOMPARE(body.value("PlayMethod").toString(),QString("DirectPlay"));QVERIFY(body.value("CanSeek").toBool());}
        else {QVERIFY(!body.contains("IsPaused"));QVERIFY(!body.contains("PlayMethod"));}
    }
    void watchedRoutes_data() {
        QTest::addColumn<QString>("provider");QTest::addColumn<int>("major");QTest::addColumn<bool>("played");
        for(auto provider:{"jellyfin","emby"}) for(int major:{10,12}) for(bool played:{false,true}) {
            const auto tag=QString("%1-%2-%3").arg(provider).arg(major).arg(played).toLatin1();QTest::newRow(tag.constData())<<QString(provider)<<major<<played;
        }
    }
    void watchedRoutes() {
        QFETCH(QString,provider);QFETCH(int,major);QFETCH(bool,played);
        ProgressReplay server;QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);
        HomeServerProgressState state;state.writePlayed=true;state.played=played;state.updatedAt=1700000000123;
        bool done=false;HomeServerProgressJob::Result result;
        job.start(server.connection(provider,major),ProgressReplay::item(),{},"REPLAY","watched",state,[&](auto r){result=r;done=true;});
        QTRY_VERIFY(done);QVERIFY2(result.ok(),qPrintable(result.error));QCOMPARE(server.requests.size(),1);
        QVERIFY(server.requests.first().startsWith(played?"POST ":"DELETE "));
        const auto target=ProgressReplay::target(server.requests.first());const QUrlQuery query(target);
        QCOMPARE(target.path(QUrl::FullyEncoded),provider=="jellyfin"&&major>=12?QString("/proxy/UserPlayedItems/item%2Fa"):QString("/proxy/Users/user%2Fa/PlayedItems/item%2Fa"));
        if(provider=="jellyfin"&&major>=12) {QCOMPARE(query.queryItemValue("userId"),QString("user/a"));if(played) QCOMPARE(query.queryItemValue("datePlayed"),QString("2023-11-14T22:13:20.123Z"));}
        else if(played) QCOMPARE(query.queryItemValue("DatePlayed"),QString("20231114221320"));
    }
    void syncPreservesUserData_data() {QTest::addColumn<QString>("provider");QTest::addColumn<int>("major");QTest::newRow("jf12")<<QString("jellyfin")<<12;QTest::newRow("jf10")<<QString("jellyfin")<<10;QTest::newRow("emby")<<QString("emby")<<0;}
    void syncPreservesUserData() {
        QFETCH(QString,provider);QFETCH(int,major);ProgressReplay server;
        const auto fixture=ProgressReplay::fixture(provider=="emby"?"emby-userdata.json":"jellyfin-userdata.json");server.replies.append({200,fixture});
        QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);
        HomeServerProgressState state;state.positionMs=12345;state.durationMs=60000;state.updatedAt=1700000000123;state.played=true;
        bool done=false;HomeServerProgressJob::Result result;
        job.start(server.connection(provider,major),ProgressReplay::item(),{},"REPLAY","sync",state,[&](auto r){result=r;done=true;});
        QTRY_VERIFY(done);QVERIFY2(result.ok(),qPrintable(result.error));QCOMPARE(server.requests.size(),2);QVERIFY(server.requests.first().startsWith("GET "));QVERIFY(server.requests.last().startsWith("POST "));
        const auto firstTarget=ProgressReplay::target(server.requests.first());QCOMPARE(ProgressReplay::target(server.requests.last()),firstTarget);
        QCOMPARE(firstTarget.path(QUrl::FullyEncoded),provider=="jellyfin"&&major>=12?QString("/proxy/UserItems/item%2Fa/UserData"):QString("/proxy/Users/user%2Fa/Items/item%2Fa/UserData"));
        auto original=QJsonDocument::fromJson(fixture).object();auto expected=original;expected.insert("PlaybackPositionTicks",123450000);expected.insert("LastPlayedDate","2023-11-14T22:13:20.123Z");QCOMPARE(ProgressReplay::body(server.requests.last()),expected);
    }
    void plexEvents_data() {
        QTest::addColumn<QString>("event");QTest::addColumn<bool>("paused");
        QTest::newRow("start")<<QString("start")<<false;QTest::newRow("paused")<<QString("progress")<<true;QTest::newRow("resume")<<QString("progress")<<false;QTest::newRow("stop")<<QString("stop")<<false;QTest::newRow("offline-sync")<<QString("sync")<<false;
    }
    void plexEvents() {
        QFETCH(QString,event);QFETCH(bool,paused);ProgressReplay server;if(event=="sync")server.replies.append({200,ProgressReplay::fixture("plex-metadata.json")});server.replies.append({200,ProgressReplay::fixture("plex-ack.json")});
        QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);
        HomeServerProgressState state;state.positionMs=12345;state.durationMs=60000;state.updatedAt=1700000000123;state.paused=paused;
        bool done=false;HomeServerProgressJob::Result result;
        job.start(server.connection("plex"),ProgressReplay::item(),event=="sync"?HomeServerPlaybackJob::Result{}:ProgressReplay::playback(),"REPLAY-TOKEN",event,state,[&](auto r){result=r;done=true;});
        QTRY_VERIFY(done);QVERIFY2(result.ok(),qPrintable(result.error));QCOMPARE(server.requests.size(),event=="sync"?2:1);if(event=="sync")QVERIFY(server.requests.first().startsWith("GET /proxy/library/metadata/item%2Fa "));const auto request=server.requests.last();QVERIFY(request.startsWith("POST /proxy/:/timeline?"));
        const auto target=ProgressReplay::target(request);const QUrlQuery query(target);QCOMPARE(query.queryItemValue("ratingKey"),QString("item/a"));QCOMPARE(query.queryItemValue("key"),QString("/library/metadata/item/a"));QCOMPARE(query.queryItemValue("identifier"),QString("com.plexapp.plugins.library"));
        QCOMPARE(query.queryItemValue("time"),QString("12345"));QCOMPARE(query.queryItemValue("duration"),QString("60000"));QCOMPARE(query.queryItemValue("state"),event=="stop"||event=="sync"?QString("stopped"):paused?QString("paused"):QString("playing"));
        QVERIFY(!query.hasQueryItem("X-Plex-Token"));QVERIFY(!target.toString().contains("REPLAY-TOKEN"));QVERIFY(request.contains("X-Plex-Token: REPLAY-TOKEN"));QVERIFY(request.contains("X-Plex-Client-Identifier: "+client.installationId().toLatin1()));
        const auto session=query.queryItemValue("X-Plex-Session-Identifier");QVERIFY(!session.isEmpty());if(event!="sync") QCOMPARE(session,QString("chosen-session"));
        if(event=="sync") {QCOMPARE(query.queryItemValue("updated"),QString("1700000000"));QCOMPARE(query.queryItemValue("offline"),QString("1"));} else QVERIFY(!query.hasQueryItem("updated"));
    }
    void plexWatched_data() {QTest::addColumn<bool>("played");QTest::newRow("played")<<true;QTest::newRow("unplayed")<<false;}
    void plexWatched() {
        QFETCH(bool,played);ProgressReplay server;QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.played=played;state.writePlayed=true;bool done=false;
        job.start(server.connection("plex"),ProgressReplay::item(),{},"REPLAY","watched",state,[&](auto r){done=true;QVERIFY(r.ok());done=true;});QTRY_VERIFY(done);QCOMPARE(server.requests.size(),1);QVERIFY(server.requests.first().startsWith(played?"PUT /proxy/:/scrobble?":"PUT /proxy/:/unscrobble?"));
        const auto query=QUrlQuery(ProgressReplay::target(server.requests.first()));QCOMPARE(query.queryItemValue("key"),QString("item/a"));QCOMPARE(query.queryItemValue("identifier"),QString("com.plexapp.plugins.library"));
    }
    void afterStopWritesExplicitWatched_data() {QTest::addColumn<QString>("provider");for(auto provider:{"jellyfin","emby","plex"})QTest::newRow(provider)<<QString(provider);}
    void afterStopWritesExplicitWatched() {
        QFETCH(QString,provider);
        ProgressReplay server;QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.played=true;state.writePlayed=true;bool done=false;
        job.start(server.connection(provider),ProgressReplay::item(),ProgressReplay::playback(),"REPLAY","stop",state,[&](auto r){done=true;QVERIFY(r.ok());done=true;});QTRY_VERIFY(done);QCOMPARE(server.requests.size(),2);
        QVERIFY(server.requests.first().startsWith(provider=="plex"?"POST /proxy/:/timeline?":"POST /proxy/Sessions/Playing/Stopped "));
        QVERIFY(server.requests.last().startsWith(provider=="plex"?"PUT /proxy/:/scrobble?":provider=="jellyfin"?"POST /proxy/UserPlayedItems/":"POST /proxy/Users/user%2Fa/PlayedItems/"));
        const auto query=QUrlQuery(ProgressReplay::target(server.requests.last()));QVERIFY(!query.hasQueryItem("datePlayed"));QVERIFY(!query.hasQueryItem("DatePlayed"));
    }
    void syncThenExplicitWatched() {
        ProgressReplay server;auto fresh=QJsonDocument::fromJson(ProgressReplay::fixture("jellyfin-userdata.json")).object();fresh.insert("Played",false);server.replies.append({200,QJsonDocument(fresh).toJson(QJsonDocument::Compact)});QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.updatedAt=1700000000123;state.played=true;state.writePlayed=true;bool done=false;
        job.start(server.connection("jellyfin"),ProgressReplay::item(),{},"REPLAY","sync",state,[&](auto r){done=true;QVERIFY(r.ok());done=true;});QTRY_VERIFY(done);QCOMPARE(server.requests.size(),3);QVERIFY(server.requests.first().startsWith("GET /proxy/UserItems/"));QVERIFY(server.requests.at(1).startsWith("POST /proxy/UserItems/"));QVERIFY(server.requests.last().startsWith("POST /proxy/UserPlayedItems/"));
    }
    void syncReadFailureDoesNotWrite_data() {QTest::addColumn<int>("status");QTest::newRow("unauthorized")<<401;QTest::newRow("server-error")<<500;}
    void syncReadFailureDoesNotWrite() {
        QFETCH(int,status);ProgressReplay server;server.replies.append({status,"{}"});QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.updatedAt=1700000000000;state.played=true;state.writePlayed=true;bool done=false;
        job.start(server.connection("jellyfin"),ProgressReplay::item(),{},"REPLAY","sync",state,[&](auto r){done=true;QVERIFY(!r.ok());QCOMPARE(r.expired,status==401);done=true;});QTRY_VERIFY(done);QCOMPARE(server.requests.size(),1);
    }
    void freshWatchedStateSurvivesRewatchSync() {
        ProgressReplay server;server.replies.append({200,ProgressReplay::fixture("jellyfin-userdata.json")});QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.positionMs=1000;state.updatedAt=1700000000123;state.played=false;state.writePlayed=false;bool done=false;HomeServerProgressJob::Result result;
        job.start(server.connection("jellyfin"),ProgressReplay::item(),{},"REPLAY","sync",state,[&](auto r){result=r;done=true;});QTRY_VERIFY(done);QVERIFY(result.ok());QCOMPARE(server.requests.size(),2);QVERIFY(ProgressReplay::body(server.requests.last()).value("Played").toBool());QCOMPARE(ProgressReplay::body(server.requests.last()).value("PlayCount").toInt(),7);
    }
    void syncSamePlayedStateAvoidsExtraEndpoint_data() {
        QTest::addColumn<QString>("provider");QTest::addColumn<bool>("played");
        for(auto provider:{"jellyfin","emby","plex"})for(bool played:{false,true}) {const auto tag=QString("%1-%2").arg(provider).arg(played).toLatin1();QTest::newRow(tag.constData())<<QString(provider)<<played;}
    }
    void syncSamePlayedStateAvoidsExtraEndpoint() {
        QFETCH(QString,provider);QFETCH(bool,played);ProgressReplay server;QJsonObject body;
        if(provider=="plex") {
            body=QJsonDocument::fromJson(ProgressReplay::fixture("plex-metadata.json")).object();auto container=body.value("MediaContainer").toObject();auto metadata=container.value("Metadata").toArray();auto item=metadata.first().toObject();item.insert("viewCount",played?2:0);metadata.replace(0,item);container.insert("Metadata",metadata);body.insert("MediaContainer",container);
        } else {body=QJsonDocument::fromJson(ProgressReplay::fixture(provider=="emby"?"emby-userdata.json":"jellyfin-userdata.json")).object();body.insert("Played",played);}
        server.replies.append({200,QJsonDocument(body).toJson(QJsonDocument::Compact)});QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.updatedAt=1700000000123;state.positionMs=1000;state.played=played;state.writePlayed=true;bool done=false;HomeServerProgressJob::Result result;
        job.start(server.connection(provider),ProgressReplay::item(),{},"REPLAY","sync",state,[&](auto r){result=r;done=true;});QTRY_VERIFY(done);QVERIFY2(result.ok(),qPrintable(result.error));QCOMPARE(server.requests.size(),2);QVERIFY(server.requests.first().startsWith("GET "));QVERIFY(server.requests.last().startsWith("POST "));
        for(const auto &request:server.requests) {QVERIFY(!request.contains("/PlayedItems/"));QVERIFY(!request.contains("/UserPlayedItems/"));QVERIFY(!request.contains("/:/scrobble?"));QVERIFY(!request.contains("/:/unscrobble?"));}
    }
    void freshNewerRemoteSkipsSyncWrite_data() {QTest::addColumn<QString>("provider");for(auto provider:{"jellyfin","emby","plex"})QTest::newRow(provider)<<QString(provider);}
    void freshNewerRemoteSkipsSyncWrite() {
        QFETCH(QString,provider);ProgressReplay server;QJsonObject body;
        if(provider=="plex") body=QJsonDocument::fromJson("{\"MediaContainer\":{\"size\":1,\"Metadata\":[{\"ratingKey\":\"item/a\",\"lastViewedAt\":1700000001}]}}").object();
        else {body=QJsonDocument::fromJson(ProgressReplay::fixture(provider=="emby"?"emby-userdata.json":"jellyfin-userdata.json")).object();body.insert("LastPlayedDate","2023-11-14T22:13:21.000Z");}
        server.replies.append({200,QJsonDocument(body).toJson(QJsonDocument::Compact)});QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.positionMs=1000;state.updatedAt=1700000000123;state.played=false;state.writePlayed=true;bool done=false;HomeServerProgressJob::Result result;
        job.start(server.connection(provider),ProgressReplay::item(),{},"REPLAY","sync",state,[&](auto r){result=r;done=true;});QTRY_VERIFY(done);QVERIFY2(result.ok(),qPrintable(result.error));QCOMPARE(server.requests.size(),1);QVERIFY(server.requests.first().startsWith("GET "));
    }
    void plexSyncRequiresExactFreshMetadata_data() {
        QTest::addColumn<QByteArray>("body");QTest::newRow("wrong-rating-key")<<QByteArray("{\"MediaContainer\":{\"Metadata\":[{\"ratingKey\":\"wrong\",\"lastViewedAt\":1600000000}]}}");QTest::newRow("missing-metadata")<<QByteArray("{}");QTest::newRow("invalid-timestamp")<<QByteArray("{\"MediaContainer\":{\"Metadata\":[{\"ratingKey\":\"item/a\",\"lastViewedAt\":\"bad\"}]}}");
    }
    void plexSyncRequiresExactFreshMetadata() {
        QFETCH(QByteArray,body);ProgressReplay server;server.replies.append({200,body});QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.updatedAt=1700000000123;bool done=false;HomeServerProgressJob::Result result;
        job.start(server.connection("plex"),ProgressReplay::item(),{},"REPLAY","sync",state,[&](auto r){result=r;done=true;});QTRY_VERIFY(done);QVERIFY(!result.ok());QCOMPARE(server.requests.size(),1);QVERIFY(server.requests.first().startsWith("GET "));
    }
    void unauthorizedIsTerminal_data() {QTest::addColumn<QString>("provider");for(auto provider:{"jellyfin","emby","plex"})QTest::newRow(provider)<<QString(provider);}
    void unauthorizedIsTerminal() {
        QFETCH(QString,provider);ProgressReplay server;server.replies.append({401,"{\"token\":\"DO-NOT-ECHO\"}"});QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.played=true;state.writePlayed=true;bool done=false;HomeServerProgressJob::Result result;
        job.start(server.connection(provider),ProgressReplay::item(),ProgressReplay::playback(),"REPLAY","stop",state,[&](auto r){result=r;done=true;});QTRY_VERIFY(done);QVERIFY(!result.ok());QVERIFY(result.expired);QCOMPARE(server.requests.size(),1);QVERIFY(!result.error.contains("DO-NOT-ECHO"));
    }
    void malformedUserDataNeverPosts_data() {QTest::addColumn<QByteArray>("body");QTest::newRow("blank")<<QByteArray("{}");QTest::newRow("bad-types")<<QByteArray("{\"PlaybackPositionTicks\":\"bad\",\"PlayCount\":2,\"IsFavorite\":true,\"Played\":false}");QTest::newRow("array")<<QByteArray("[]");}
    void malformedUserDataNeverPosts() {
        QFETCH(QByteArray,body);ProgressReplay server;server.replies.append({200,body});QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.updatedAt=1700000000000;bool done=false;
        job.start(server.connection("emby"),ProgressReplay::item(),{},"REPLAY","sync",state,[&](auto r){done=true;QVERIFY(!r.ok());done=true;});QTRY_VERIFY(done);QCOMPARE(server.requests.size(),1);
    }
    void invalidState_data() {
        QTest::addColumn<int>("which");for(int which=0;which<7;++which)QTest::newRow(QByteArray::number(which).constData())<<which;
    }
    void invalidState() {
        QFETCH(int,which);ProgressReplay server;QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;auto playback=ProgressReplay::playback();auto connection=server.connection("jellyfin");QString event="progress";
        if(which==0)state.positionMs=-1;if(which==1)state.durationMs=-1;if(which==2)state.positionMs=std::numeric_limits<qint64>::max();if(which==3)playback.playSessionId.clear();if(which==4)playback.mediaSourceId.clear();if(which==5)event="sync";if(which==6){event="watched";state.writePlayed=true;connection.insert("serverMajor",0);}
        bool done=false;job.start(connection,ProgressReplay::item(),playback,"REPLAY",event,state,[&](auto r){done=true;QVERIFY(!r.ok());done=true;});QTRY_VERIFY(done);QCOMPARE(server.requests.size(),0);
    }
    void clampsPositionToDuration() {
        ProgressReplay server;QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());HomeServerProgressJob job(&client);HomeServerProgressState state;state.positionMs=90000;state.durationMs=60000;bool done=false;
        job.start(server.connection("emby"),ProgressReplay::item(),ProgressReplay::playback(),"REPLAY","progress",state,[&](auto r){done=true;QVERIFY(r.ok());done=true;});QTRY_VERIFY(done);QCOMPARE(ProgressReplay::body(server.requests.first()).value("PositionTicks").toInteger(),600000000);
    }
    void cancellationAndDestructionSuppressCallbacks() {
        ProgressReplay server;server.replies.append({204,{},150});QTemporaryDir dir;HomeServerClient client(nullptr,dir.path());bool callback=false;
        {HomeServerProgressJob job(&client);job.start(server.connection("plex"),ProgressReplay::item(),ProgressReplay::playback(),"REPLAY","progress",{},[&](auto){callback=true;});QTRY_COMPARE(server.requests.size(),1);job.cancel();QTest::qWait(250);QVERIFY(!callback);}
        auto *job=new HomeServerProgressJob(&client);job->start(server.connection("emby"),ProgressReplay::item(),ProgressReplay::playback(),"REPLAY","progress",{},[&](auto){callback=true;});delete job;QTest::qWait(100);QVERIFY(!callback);
    }
};
QTEST_GUILESS_MAIN(HomeServerProgressClientTest)
#include "tst_home_server_progress_client.moc"
