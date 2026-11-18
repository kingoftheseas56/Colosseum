#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include "HomeServerService.h"

class ShelfSecrets final : public AccountSecretBackend {
public:
    bool enabled=true;
    QHash<QString,QByteArray> values;
    bool isAvailable() const override { return enabled; }
    bool write(const QString &key,const QByteArray &value) override { if(!enabled)return false;values.insert(key,value);return true; }
    std::optional<QByteArray> read(const QString &key) const override { if(!enabled || !values.contains(key))return std::nullopt;return values.value(key); }
    bool remove(const QString &key) override { if(!enabled)return false;values.remove(key);return true; }
    QStringList targets(const QString &prefix) const override { QStringList out;for(auto it=values.cbegin();it!=values.cend();++it)if(it.key().startsWith(prefix))out.append(it.key());return out; }
};
class ShelfProgress final : public HomeServerProgressOwner {
public:
    QHash<QString,QVariantMap> rows;
    QVariantList entries() const override { QVariantList out;for(const auto &row:rows)out.append(row);return out; }
    QVariantMap get(const QString &id) const override { return rows.value(id); }
    bool apply(const QVariantMap &value) override { rows.insert(value.value("id").toString(),value);return true; }
    bool remove(const QString &id) override { rows.remove(id);return true; }
    Watch watch(const QString &) const override { return {}; }
    bool applyWatched(const QString &,bool,qint64) override { return true; }
};
class ShelfExpiryServer final : public QTcpServer {
public:
    int requests=0;
    ShelfExpiryServer() {
        listen(QHostAddress::LocalHost);
        connect(this,&QTcpServer::newConnection,this,[this] {
            while(auto *socket=nextPendingConnection()) {
                connect(socket,&QTcpSocket::readyRead,socket,[this,socket] {
                    socket->readAll();if(socket->property("answered").toBool())return;socket->setProperty("answered",true);++requests;
                    socket->write("HTTP/1.1 401 Unauthorized\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");socket->disconnectFromHost();
                });
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
    }
    QString origin() const {return "http://127.0.0.1:"+QString::number(serverPort());}
};
class HomeServerShelvesTest : public QObject {
    Q_OBJECT
    static QJsonObject item(QString id,QString kind,QString title,QJsonObject identity,bool playable,QString parent={}) {
        QJsonObject out{{"id",id},{"kind",kind},{"title",title},{"year",2024},{"identity",identity},{"parentId",parent},
            {"libraryId","video"},{"libraryName","Video"},{"rawPath","/SECRET/raw/file.mkv"},{"token","SECRET-RAW"}};
        if(playable)out.insert("versions",QJsonArray{QJsonObject{{"id","copy/"+id},{"name","Original"},{"directPlayable",false}}});
        else out.insert("versions",QJsonArray{});
        return out;
    }
    static QJsonArray heldItems() {
        QJsonArray out;
        out.append(item("movie","movie","Alpha",{{"imdbId","tt100"},{"tmdbId",101}},true));
        out.append(item("movie-copy","movie","Alpha duplicate",{{"imdbId","tt100"},{"tmdbId",101}},true));
        out.append(item("show","series","Beta",{{"imdbId","tt200"},{"tmdbId",202},{"tvdbId",303}},false));
        out.append(item("season","season","Specials",{},false,"show"));
        out.append(item("special","episode","Special",{{"season",0},{"episode",2}},true,"show"));
        out.append(item("special-copy","episode","Special duplicate",{{"season",0},{"episode",2}},true,"show"));
        out.append(item("pilot","episode","Pilot",{{"season",1},{"episode",1}},true,"show"));
        out.append(item("fallback","movie","Gamma",{{"tmdbId",777}},true));
        out.append(item("fallback-show","series","Delta",{{"tvdbId",888}},false));
        out.append(item("fallback-special","episode","Zero Special",{{"season",0},{"episode",0}},true,"fallback-show"));
        out.append(item("unknown","movie","Unknown",{},true));
        out.append(item("missing-copy","movie","Missing Copy",{{"imdbId","tt300"}},false));
        out.append(item("no-episodes","series","Empty Series",{{"imdbId","tt400"}},false));
        out.append(item("no-episode-copy","episode","Missing Episode",{{"season",1},{"episode",2}},false,"no-episodes"));
        out.append(item("orphan","episode","Orphan",{{"imdbId","tt500"},{"season",1},{"episode",1}},true,"missing-root"));
        return out;
    }
    static QString seed(const std::shared_ptr<ShelfSecrets> &secrets,const QString &root,QString provider,QJsonArray items=heldItems(),QString origin="https://SECRET.invalid/proxy",QString profile="p") {
        if(!QDir().mkpath(root))return {};
        HomeServerStore store(secrets);if(!store.activate(profile,root))return {};
        const auto id=store.add({{"provider",provider},{"name","Home"},{"origin",origin},{"userId","SECRET-USER"}},"SECRET-TOKEN");
        return !id.isEmpty() && store.replaceItems(id,items)?id:QString();
    }
    static HomeServerServiceOptions options(const std::shared_ptr<ShelfSecrets> &secrets,const QTemporaryDir &dir) {
        HomeServerServiceOptions out;out.secrets=secrets;out.identityDirectory=dir.filePath("installation");out.refreshOnActivation=false;return out;
    }
private slots:
    void serverTabsKeepDisabledLibrariesAndRestartOrderWithoutSecrets() {
        QTemporaryDir dir; auto secrets=std::make_shared<ShelfSecrets>(); const auto root=dir.filePath("profile");
        const auto id=seed(secrets,root,"jellyfin"); QVERIFY(!id.isEmpty());
        HomeServerStore saved(secrets); QVERIFY(saved.activate("p",root));
        QJsonArray members; for(const auto &value:heldItems()) members.append(value.toObject().value("id"));
        const QJsonArray libraries{
            QJsonObject{{"id","video"},{"name","Zulu"},{"kind","mixed"},{"itemIds",members},{"token","SECRET-MANIFEST"},{"poster","https://SECRET/private"}},
            QJsonObject{{"id","empty"},{"name","Empty library"},{"kind","series"},{"itemIds",QJsonArray{}}},
            QJsonObject{{"id","movies"},{"name","Alpha copies"},{"kind","movie"},{"itemIds",QJsonArray{"movie"}}}};
        QVERIFY(saved.replaceCatalog(id,libraries,heldItems()));
        HomeServerService service(options(secrets,dir)); service.activate("p",root);
        auto tabs=service.serverTabs(); QCOMPARE(tabs.size(),1); QCOMPARE(tabs.first().toMap().value("state").toString(),QString("cached"));
        QVERIFY(tabs.first().toMap().value("manifestKnown").toBool()); QCOMPARE(tabs.first().toMap().value("libraryCount").toInt(),3); QCOMPARE(tabs.first().toMap().value("titleCount").toInt(),4);
        auto rows=service.serverLibraries(id); QCOMPARE(rows.size(),3);
        QCOMPARE(rows.at(0).toMap().value("name").toString(),QString("Zulu")); QCOMPARE(rows.at(0).toMap().value("kind").toString(),QString("mixed"));
        QCOMPARE(rows.at(0).toMap().value("titleCount").toInt(),4);
        QCOMPARE(rows.at(1).toMap().value("name").toString(),QString("Empty library")); QCOMPARE(rows.at(1).toMap().value("itemCount").toInt(),0);
        QCOMPARE(rows.at(2).toMap().value("position").toInt(),2); QCOMPARE(rows.at(2).toMap().value("titleCount").toInt(),1);
        const auto json=QJsonDocument::fromVariant(QVariantList{tabs,rows}).toJson();
        QVERIFY(!json.contains("SECRET")); QVERIFY(!json.contains("origin")); QVERIFY(!json.contains("userId")); QVERIFY(!json.contains("versions")); QVERIFY(!json.contains("infoHash"));
        service.setEnabled(id,false); QCOMPARE(service.serverTabs().size(),1); QCOMPARE(service.serverTabs().first().toMap().value("state").toString(),QString("disabled"));
        QVERIFY(service.serverLibraries(id).first().toMap().value("dimmed").toBool()); QCOMPARE(service.serverLibraries(id).size(),3);
        QVERIFY(service.shelves().isEmpty()); QVERIFY(service.sources({{"id","tt100"},{"type","movie"}}).isEmpty());
        HomeServerService restarted(options(secrets,dir)); restarted.activate("p",root); QCOMPARE(restarted.serverLibraries(id).size(),3); QCOMPARE(restarted.serverTabs().first().toMap().value("state").toString(),QString("disabled"));
        service.setEnabled(id,true); secrets->values.clear(); QCOMPARE(service.serverTabs().first().toMap().value("state").toString(),QString("needsReconnect")); QCOMPARE(service.serverLibraries(id).size(),3); QVERIFY(service.shelves().isEmpty());
        service.activate("other",dir.filePath("other-profile")); QVERIFY(service.serverTabs().isEmpty()); QVERIFY(service.serverLibraries(id).isEmpty());
        service.activate("p",root); service.remove(id); QVERIFY(service.serverTabs().isEmpty()); QVERIFY(service.serverLibraries(id).isEmpty());
    }
    void knownEmptyManifestDiffersFromLegacyAndPostersUseOnlyValidatedImdb() {
        QTemporaryDir dir; auto secrets=std::make_shared<ShelfSecrets>(); const auto root=dir.filePath("profile");
        auto malicious=item("malicious","movie","Bad poster",{{"imdbId","tt1/SECRET?token=x"},{"tmdbId",901}},true);
        malicious.insert("coverUrl","https://SECRET/private?token=x");
        const auto id=seed(secrets,root,"emby",QJsonArray{malicious}); QVERIFY(!id.isEmpty());
        HomeServerService service(options(secrets,dir)); service.activate("p",root);
        QVERIFY(!service.serverTabs().first().toMap().value("manifestKnown").toBool()); QVERIFY(service.serverLibraries(id).isEmpty());
        QCOMPARE(service.shelfTitles(id).size(),1); QVERIFY(service.shelfTitles(id).first().toMap().value("coverUrl").toString().isEmpty());
        HomeServerStore saved(secrets); QVERIFY(saved.activate("p",root)); QVERIFY(saved.replaceCatalog(id,{},{}));
        service.activate("p",root); QVERIFY(service.serverTabs().first().toMap().value("manifestKnown").toBool());
        QCOMPARE(service.serverTabs().first().toMap().value("libraryCount").toInt(),0); QVERIFY(service.serverLibraries(id).isEmpty());
        service.deactivate(); QVERIFY(service.serverTabs().isEmpty()); QVERIFY(service.serverLibraries(id).isEmpty());
    }
    void providerProjection_data() {QTest::addColumn<QString>("provider");for(auto provider:{"jellyfin","emby","plex"})QTest::newRow(provider)<<QString(provider);}
    void providerProjection() {
        QFETCH(QString,provider);QTemporaryDir dir;auto secrets=std::make_shared<ShelfSecrets>();const auto id=seed(secrets,dir.filePath("profile"),provider);QVERIFY(!id.isEmpty());
        HomeServerService service(options(secrets,dir));service.activate("p",dir.filePath("profile"));
        const auto shelves=service.shelves();QCOMPARE(shelves.size(),1);QCOMPARE(shelves.first().toMap(),(QVariantMap{{"id",id},{"name","Home"},{"titleCount",4}}));
        const auto rows=service.shelfTitles(id);QCOMPARE(rows.size(),4);
        QStringList titles,ids;for(const auto &value:rows){const auto row=value.toMap();titles.append(row.value("title").toString());ids.append(row.value("id").toString());QCOMPARE(row.value("year").toInt(),2024);const auto publicId=row.value("id").toString();const auto cover=publicId.startsWith("tt")?QString("https://live.metahub.space/poster/small/%1/img").arg(publicId):QString();QCOMPARE(row.value("coverUrl").toString(),cover);const auto identity=row.value("identity").toMap();QCOMPARE(identity.value("coverUrl").toString(),cover);QCOMPARE(identity.value("world").toString(),QString("Theatre"));QCOMPARE(identity.value("identityId"),row.value("id"));QCOMPARE(identity.value("type"),row.value("kind"));}
        QCOMPARE(titles,(QStringList{"Alpha","Beta","Delta","Gamma"}));QCOMPARE(ids,(QStringList{"tt100","tt200","tvdb:tv:888","tmdb:movie:777"}));
        const auto publicJson=QJsonDocument::fromVariant(QVariantList{shelves,rows}).toJson();QVERIFY(!publicJson.contains("SECRET"));QVERIFY(!publicJson.contains("versions"));QVERIFY(!publicJson.contains("origin"));QVERIFY(!publicJson.contains("infoHash"));
    }
    void offlineMetadataAndExactEpisodes() {
        QTemporaryDir dir;auto secrets=std::make_shared<ShelfSecrets>();const auto id=seed(secrets,dir.filePath("profile"),"jellyfin");QVERIFY(!id.isEmpty());HomeServerService service(options(secrets,dir));service.activate("p",dir.filePath("profile"));
        const auto movie=service.titleMetadata({{"id","tt100"},{"type","movie"}});QCOMPARE(movie.value("id").toString(),QString("tt100"));QCOMPARE(movie.value("name").toString(),QString("Alpha"));QCOMPARE(movie.value("tmdbId").toInt(),101);QCOMPARE(movie.value("moviedb_id").toInt(),101);QVERIFY(movie.value("videos").toList().isEmpty());
        const auto show=service.titleMetadata({{"id","tt200"},{"type","series"}});QCOMPARE(show.value("type").toString(),QString("series"));QCOMPARE(show.value("tvdbId").toInt(),303);const auto videos=show.value("videos").toList();QCOMPARE(videos.size(),2);QCOMPARE(videos.at(0).toMap(),(QVariantMap{{"id","tt200:0:2"},{"title","Special"},{"season",0},{"episode",2}}));QCOMPARE(videos.at(1).toMap().value("id").toString(),QString("tt200:1:1"));
        const auto fallback=service.titleMetadata({{"id","tt7777777"},{"type","movie"},{"tmdbId",777}});QCOMPARE(fallback.value("id").toString(),QString("tt7777777"));QCOMPARE(fallback.value("name").toString(),QString("Gamma"));QCOMPARE(fallback.value("moviedb_id").toInt(),777);
        const auto zero=service.titleMetadata({{"id","tvdb:tv:888"},{"type","tv"}});QCOMPARE(zero.value("videos").toList().first().toMap().value("id").toString(),QString("tvdb:tv:888:0:0"));
        const auto publicJson=QJsonDocument::fromVariant(QVariantList{movie,show,fallback,zero}).toJson();QVERIFY(!publicJson.contains("SECRET"));QVERIFY(!publicJson.contains("://"));QVERIFY(!publicJson.contains("headers"));
    }
    void negativesDoNotFabricateTitles() {
        QTemporaryDir dir;auto secrets=std::make_shared<ShelfSecrets>();const auto id=seed(secrets,dir.filePath("profile"),"emby");QVERIFY(!id.isEmpty());HomeServerService service(options(secrets,dir));service.activate("p",dir.filePath("profile"));
        QVERIFY(service.shelfTitles("not-a-connection").isEmpty());
        for(const auto &query:QVariantList{QVariantMap{{"id","tt999"},{"type","movie"}},QVariantMap{{"id","tt999"},{"type","movie"},{"tmdbId",101}},QVariantMap{{"id","tt100"},{"type","series"},{"tmdbId",101}},QVariantMap{{"id","tt300"},{"type","movie"}},QVariantMap{{"id","tt400"},{"type","series"}},QVariantMap{{"id","tt500"},{"type","series"}}})QVERIFY(service.titleMetadata(query.toMap()).isEmpty());
    }
    void disabledRemovedAndProfileSwitchHideRows() {
        QTemporaryDir dir;auto secrets=std::make_shared<ShelfSecrets>();const auto root=dir.filePath("profile");const auto id=seed(secrets,root,"plex");QVERIFY(!id.isEmpty());HomeServerService service(options(secrets,dir));service.activate("p",root);QCOMPARE(service.shelves().size(),1);
        service.setEnabled(id,false);QVERIFY(service.shelves().isEmpty());QVERIFY(service.shelfTitles(id).isEmpty());QVERIFY(service.titleMetadata({{"id","tt100"},{"type","movie"}}).isEmpty());
        service.setEnabled(id,true);QCOMPARE(service.shelfTitles(id).size(),4);service.activate("other",dir.filePath("other-profile"));QVERIFY(service.shelves().isEmpty());QVERIFY(service.titleMetadata({{"id","tt100"},{"type","movie"}}).isEmpty());
        service.activate("p",root);QCOMPARE(service.shelfTitles(id).size(),4);service.remove(id);QVERIFY(service.shelves().isEmpty());QVERIFY(service.shelfTitles(id).isEmpty());
    }
    void missingTokenAndUnavailableStoreHideRows() {
        QTemporaryDir dir;auto secrets=std::make_shared<ShelfSecrets>();const auto id=seed(secrets,dir.filePath("profile"),"jellyfin");QVERIFY(!id.isEmpty());HomeServerService service(options(secrets,dir));service.activate("p",dir.filePath("profile"));QCOMPARE(service.shelves().size(),1);
        secrets->enabled=false;QVERIFY(service.shelves().isEmpty());QVERIFY(service.shelfTitles(id).isEmpty());QVERIFY(service.titleMetadata({{"id","tt100"},{"type","movie"}}).isEmpty());secrets->enabled=true;QCOMPARE(service.shelves().size(),1);
        secrets->values.clear();QVERIFY(service.shelves().isEmpty());QVERIFY(service.shelfTitles(id).isEmpty());QVERIFY(service.titleMetadata({{"id","tt100"},{"type","movie"}}).isEmpty());service.deactivate();QVERIFY(service.shelves().isEmpty());
    }
    void emptyConnectedShelfHasZeroCount() {
        QTemporaryDir dir;auto secrets=std::make_shared<ShelfSecrets>();const auto id=seed(secrets,dir.filePath("profile"),"emby",QJsonArray{item("unknown","movie","Unknown",{},true)});QVERIFY(!id.isEmpty());HomeServerService service(options(secrets,dir));service.activate("p",dir.filePath("profile"));QCOMPARE(service.shelves().size(),1);QCOMPARE(service.shelves().first().toMap().value("titleCount").toInt(),0);QVERIFY(service.shelfTitles(id).isEmpty());
    }
    void progressAliasesReuseKnownCanonicalMovieId() {
        QTemporaryDir dir;auto secrets=std::make_shared<ShelfSecrets>();const auto id=seed(secrets,dir.filePath("profile"),"jellyfin");QVERIFY(!id.isEmpty());auto progress=std::make_shared<ShelfProgress>();progress->rows.insert("tt7777777",{{"id","tt7777777"},{"kind","video"},{"updatedAt",0},{"_homeServerIdentity",QVariantMap{{"type","movie"},{"imdbId","tt7777777"},{"tmdbId",777}}}});
        HomeServerService service(options(secrets,dir));service.activate("p",dir.filePath("profile"),progress);bool found=false;for(const auto &value:service.shelfTitles(id)){const auto row=value.toMap();if(row.value("title")=="Gamma"){found=true;QCOMPARE(row.value("id").toString(),QString("tt7777777"));}}
        QVERIFY(found);QCOMPARE(service.titleMetadata({{"id","tmdb:movie:777"},{"type","movie"}}).value("id").toString(),QString("tt7777777"));
    }
    void publicExpiryRouteHidesAllProjections() {
        QTemporaryDir dir;ShelfExpiryServer server;auto secrets=std::make_shared<ShelfSecrets>();const auto id=seed(secrets,dir.filePath("profile"),"jellyfin",heldItems(),server.origin());QVERIFY(!id.isEmpty());HomeServerService service(options(secrets,dir));service.activate("p",dir.filePath("profile"));QCOMPARE(service.shelves().size(),1);service.refresh(id);
        QTRY_VERIFY(service.connections().first().toMap().value("needsReconnect").toBool());QVERIFY(service.shelves().isEmpty());QVERIFY(service.shelfTitles(id).isEmpty());QVERIFY(service.titleMetadata({{"id","tt100"},{"type","movie"}}).isEmpty());QCOMPARE(server.requests,1);
    }
};
QTEST_GUILESS_MAIN(HomeServerShelvesTest)
#include "tst_home_server_shelves.moc"
