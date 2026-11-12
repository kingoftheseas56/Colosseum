#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include "HomeServerService.h"
#include "player/playerbackendcontract.h"

class ServicePlayer final : public QObject, public PlayerBackendContract {
public:
    QString url;
    QVariantMap headers;
    int loads = 0, stops = 0;
    std::function<void()> onStop;
    QVariantMap capabilities() const override { return {}; }
    void loadSource(const QString &value, const QVariantMap &fields) override { url = value; headers = fields; ++loads; }
    void stopPlayback() override { ++stops; if (onStop) { auto callback = onStop; callback(); } }
    void setHostLifecycleState(const QString &) override {}
    void setAudioFocusState(const QString &) override {}
    void releaseVideoSurface() override {}
    void restoreVideoSurface() override {}
};

class ServiceSecrets final : public AccountSecretBackend {
public:
    bool enabled = true;
    QHash<QString, QByteArray> values;
    bool isAvailable() const override { return enabled; }
    bool write(const QString &key, const QByteArray &blob) override { values[key] = blob; return enabled; }
    std::optional<QByteArray> read(const QString &key) const override {
        if (!enabled || !values.contains(key)) return std::nullopt;
        return values.value(key);
    }
    bool remove(const QString &key) override { values.remove(key); return enabled; }
    QStringList targets(const QString &prefix) const override {
        QStringList result;
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (it.key().startsWith(prefix)) result.append(it.key());
        return result;
    }
};

class ServiceProgress final : public HomeServerProgressOwner {
public:
    QHash<QString, QVariantMap> rows;
    QVariantList entries() const override { QVariantList r; for (const auto &row : rows) r.append(row); return r; }
    QVariantMap get(const QString &id) const override { return rows.value(id); }
    bool apply(const QVariantMap &row) override { rows[row.value("id").toString()] = row; return true; }
    bool remove(const QString &id) override { rows.remove(id); return true; }
    Watch watch(const QString &) const override { return {}; }
    bool applyWatched(const QString &, bool, qint64) override { return true; }
};

class ServiceHttp final : public QTcpServer {
public:
    QList<QByteArray> requests;
    std::function<QByteArray(const QByteArray &)> response;
    int delayMs = 0;
    int status = 200;
    ServiceHttp() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = nextPendingConnection()) {
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
                    *bytes += socket->readAll();
                    const int end = bytes->indexOf("\r\n\r\n");
                    if (end < 0 || socket->property("answered").toBool()) return;
                    int length = 0;
                    for (const auto &line : bytes->left(end).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    if (bytes->size() < end + 4 + length) return;
                    socket->setProperty("answered", true);
                    requests.append(*bytes);
                    const auto body = response ? response(*bytes) : QByteArray("{}");
                    const auto code = status;
                    QTimer::singleShot(delayMs, socket, [socket, body, code] {
                        if (socket->state() != QAbstractSocket::ConnectedState) return;
                        socket->write("HTTP/1.1 " + QByteArray::number(code) + " Replay\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                            + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl origin() const { return QUrl("http://127.0.0.1:" + QString::number(serverPort())); }
};

class HomeServerServiceTest : public QObject {
    Q_OBJECT
    static QJsonObject movie(const QString &id) {
        return {{"Id", id}, {"Type", "Movie"}, {"Name", id}, {"RunTimeTicks", 36000000000LL},
            {"ProviderIds", QJsonObject{{"Imdb", "tt1234567"}}},
            {"MediaSources", QJsonArray{QJsonObject{{"Id", "copy"}, {"Container", "mp4"},
                {"MediaStreams", QJsonArray{QJsonObject{{"Type", "Video"}, {"Codec", "h264"}},
                                              QJsonObject{{"Type", "Audio"}, {"Codec", "aac"}}}}}}}};
    }
    static QByteArray catalogResponse(const QByteArray &request, bool partial = false) {
        if (request.contains("/System/Info/Public")) return "{\"Version\":\"12.4.0\"}";
        if (request.contains("/UserViews?"))
            return "{\"Items\":[{\"Id\":\"films\",\"Name\":\"Films\",\"CollectionType\":\"movies\"}]}";
        const bool next = request.toLower().contains("startindex=1");
        return QJsonDocument(QJsonObject{{"Items", next ? QJsonArray{} : QJsonArray{movie("new")}},
            {"TotalRecordCount", partial ? 2 : 1}, {"StartIndex", next ? 1 : 0}}).toJson();
    }
    static QByteArray playbackResponse(const QByteArray &) {
        return "{\"PlaySessionId\":\"replay-session\",\"MediaSources\":[{\"Id\":\"copy\",\"SupportsDirectPlay\":true,\"Container\":\"mp4\"}]}";
    }
    static QString seedCopy(const std::shared_ptr<ServiceSecrets> &secrets, const QString &root, const QUrl &origin) {
        if (!QDir().mkpath(root)) return {};
        HomeServerStore saved(secrets);
        if (!saved.activate("a", root)) return {};
        const auto id = saved.add({{"provider", "jellyfin"}, {"name", "Home"},
            {"origin", origin.toString()}, {"userId", "user"}}, "REPLAY_NATIVE_SECRET");
        const auto item = HomeServerCatalog::normalizeJellyfinItem(movie("new"), {{"id", "films"}, {"name", "Films"}});
        if (id.isEmpty() || !saved.replaceItems(id, QJsonArray{item})) return {};
        return id;
    }
private slots:
    void importedContinueUsesCurrentConnectionAndQueuedImportIsProfileFenced() {
        QTemporaryDir dir; ServiceHttp server; auto secrets=std::make_shared<ServiceSecrets>();
        const auto id=seedCopy(secrets,dir.filePath("a"),server.origin()); QVERIFY(!id.isEmpty());
        auto owner=std::make_shared<ServiceProgress>();
        HomeServerService service({secrets,dir.filePath("installation"),QUrl("https://plex.tv"),1500,false});
        service.activate("a",dir.filePath("a"),owner);
        const QVariantMap imported{{"id","tt1234567"},{"kind","video"},{"updatedAt",5000},{"duration",100},
            {"progress",.3},{"resume",QVariantMap{{"infoHash","home-server:other-device:item:copy"},{"position",30}}}};
        owner->apply(imported); service.progressEntryApplied("video","tt1234567");
        QTRY_VERIFY(owner->get("tt1234567").value("resume").toMap().value("infoHash").toString().startsWith("home-server:"+id+':'));
        QCOMPARE(owner->get("tt1234567").value("updatedAt").toInt(),5000);
        QVERIFY(!owner->get("tt1234567").value("_homeServerHidden").toBool());
        owner->apply(imported); service.progressEntryApplied("video","tt1234567");
        auto other=std::make_shared<ServiceProgress>(); service.activate("b",dir.filePath("b"),other);
        const auto ordinary=QVariantMap{{"id","tt1234567"},{"kind","video"},{"updatedAt",7000},{"resume",QVariantMap{{"infoHash","ordinary-hash"},{"position",70}}}};
        other->apply(ordinary); QTest::qWait(80); QCOMPARE(other->get("tt1234567"),ordinary);
        other->apply(imported); service.progressEntryApplied("video","tt1234567");
        QTRY_VERIFY(other->get("tt1234567").value("_homeServerHidden").toBool());
        QCOMPARE(other->get("tt1234567").value("resume").toMap().value("position").toInt(),30);
    }
    void fallbackMatchKeepsOneCanonicalProgressIdAcrossRestart() {
        QTemporaryDir dir; ServiceHttp server; auto secrets=std::make_shared<ServiceSecrets>();
        const auto id=seedCopy(secrets,dir.filePath("a"),server.origin()); QVERIFY(!id.isEmpty());
        { HomeServerStore saved(secrets); QVERIFY(saved.activate("a",dir.filePath("a")));
          auto rows=saved.items(id); auto item=rows.first().toObject(); item["identity"]=QJsonObject{{"tmdbId",101}};
          rows[0]=item; QVERIFY(saved.replaceItems(id,rows)); }
        server.response=[](const QByteArray &r) { return r.contains("/PlaybackInfo")?playbackResponse(r):QByteArray("{}"); };
        auto owner=std::make_shared<ServiceProgress>();
        { HomeServerService service({secrets,dir.filePath("installation"),QUrl("https://plex.tv"),1500,false});
          service.activate("a",dir.filePath("a"),owner);
          const auto row=service.sources({{"type","movie"},{"id","tt1234567"},{"tmdbId",101}}).first().toMap();
          QCOMPARE(row.value("canonicalId").toString(),QString("tt1234567"));
          QVERIFY(!row.value("homeIdentity").toMap().isEmpty());
          const auto key=row.value("infoHash").toString(); ServicePlayer player;
          QVERIFY(service.loadPlayback(&player,key)); QTRY_COMPARE(player.loads,1);
          service.reportPlayback(&player,key,100000,100000,false,"ended");
          QVERIFY(owner->get("tt1234567").value("watched").toBool());
          QVERIFY(owner->get("tmdb:movie:101").isEmpty()); }
        HomeServerService restarted({secrets,dir.filePath("installation"),QUrl("https://plex.tv"),1500,false});
        restarted.activate("a",dir.filePath("a"),owner);
        QVERIFY(owner->get("tmdb:movie:101").isEmpty()); QCOMPARE(owner->rows.size(),1);
    }
    void disableAndRemoveHideContinueWithoutLosingPosition() {
        QTemporaryDir dir; ServiceHttp server; auto secrets=std::make_shared<ServiceSecrets>();
        const auto id=seedCopy(secrets,dir.filePath("a"),server.origin()); QVERIFY(!id.isEmpty());
        auto owner=std::make_shared<ServiceProgress>();
        const auto key="home-server:"+id+":new:copy";
        owner->apply({{"id","tt1234567"},{"kind","video"},{"updatedAt",5000},{"duration",100},
            {"progress",.3},{"resume",QVariantMap{{"infoHash",key},{"position",30}}}});
        HomeServerService service({secrets,dir.filePath("installation"),QUrl("https://plex.tv"),1500,false});
        service.activate("a",dir.filePath("a"),owner);
        service.setEnabled(id,false);
        QVERIFY(owner->get("tt1234567").value("_homeServerHidden").toBool());
        QCOMPARE(owner->get("tt1234567").value("resume").toMap().value("position").toInt(),30);
        service.setEnabled(id,true);
        QVERIFY(!owner->get("tt1234567").value("_homeServerHidden").toBool());
        QCOMPARE(owner->get("tt1234567").value("updatedAt").toInt(),5000);
        service.remove(id); QVERIFY(owner->get("tt1234567").value("_homeServerHidden").toBool());
        service.activate("b",dir.filePath("b"),owner);
        QVERIFY(owner->get("tt1234567").value("_homeServerHidden").toBool());
    }
    void progressUnauthorizedPurgesQueueAndHidesProviderContinue() {
        QTemporaryDir dir; ServiceHttp server; auto secrets=std::make_shared<ServiceSecrets>();
        const auto id=seedCopy(secrets,dir.filePath("a"),server.origin()); QVERIFY(!id.isEmpty());
        server.response=[](const QByteArray &r) { return playbackResponse(r); };
        auto owner=std::make_shared<ServiceProgress>();
        HomeServerService service({secrets,dir.filePath("installation"),QUrl("https://plex.tv"),1500,false});
        service.activate("a",dir.filePath("a"),owner);
        const auto key=service.sources({{"type","movie"},{"id","tt1234567"}}).first().toMap().value("infoHash").toString();
        ServicePlayer player; QVERIFY(service.loadPlayback(&player,key)); QTRY_COMPARE(player.loads,1);
        owner->apply({{"id","tt1234567"},{"kind","video"},{"resume",QVariantMap{{"infoHash",key},{"position",20}}}});
        server.status=401;
        service.reportPlayback(&player,key,20000,100000,false,"start");
        service.reportPlayback(&player,key,25000,100000,false,"progress");
        QTRY_VERIFY(service.connections().first().toMap().value("needsReconnect").toBool());
        QVERIFY(owner->get("tt1234567").value("_homeServerHidden").toBool());
        const auto count=server.requests.size();
        service.reportPlayback(&player,key,30000,100000,false,"progress");
        QTest::qWait(100); QCOMPARE(server.requests.size(),count); QCOMPARE(count,2);
        QVERIFY(service.sources({{"type","movie"},{"id","tt1234567"}}).isEmpty());
    }
    void actualPlaybackEventsAndNaturalEnd() {
        QTemporaryDir dir; ServiceHttp server;
        auto secrets = std::make_shared<ServiceSecrets>();
        const auto id = seedCopy(secrets, dir.filePath("a"), server.origin()); QVERIFY(!id.isEmpty());
        server.response = [](const QByteArray &r) {
            if (r.contains("/PlaybackInfo")) return playbackResponse(r);
            if (r.startsWith("POST /Sessions/") || r.startsWith("POST /UserPlayedItems/")) return QByteArray("{}");
            return catalogResponse(r);
        };
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        auto owner = std::make_shared<ServiceProgress>();
        service.activate("a", dir.filePath("a"), owner);
        service.refresh(id); QTRY_VERIFY(!service.connections().first().toMap().value("refreshing").toBool());
        const auto key = service.sources({{"type","movie"},{"id","tt1234567"}}).first().toMap().value("infoHash").toString();
        ServicePlayer player; QVERIFY(service.loadPlayback(&player,key)); QTRY_COMPARE(player.loads,1);
        service.reportPlayback(&player,key,12000,100000,false,"start");
        service.reportPlayback(&player,key,30000,100000,true,"progress");
        service.reportPlayback(&player,key,100000,100000,false,"ended");
        service.cancelPlayback(&player);
        auto reports = [&] { QList<QByteArray> r; for(const auto &q:server.requests) if(q.contains("/Sessions/") || q.contains("/UserPlayedItems/")) r.append(q); return r; };
        QTRY_COMPARE(reports().size(),4);
        QVERIFY(reports()[0].startsWith("POST /Sessions/Playing "));
        QVERIFY(reports()[1].startsWith("POST /Sessions/Playing/Progress "));
        QVERIFY(reports()[2].startsWith("POST /Sessions/Playing/Stopped "));
        QVERIFY(reports()[3].startsWith("POST /UserPlayedItems/new?"));
        QVERIFY(owner->get("tt1234567").value("watched").toBool());
        service.reportPlayback(&player,key,0,100000,false,"start"); QTest::qWait(80); QCOMPARE(reports().size(),4);
    }
    void stopDoesNotMarkWatchedAndProfileReplyIsFenced() {
        QTemporaryDir dir; ServiceHttp server; server.delayMs = 40;
        auto secrets=std::make_shared<ServiceSecrets>();
        const auto id=seedCopy(secrets,dir.filePath("a"),server.origin()); QVERIFY(!id.isEmpty());
        server.response=[](const QByteArray &r) { return r.contains("/PlaybackInfo") ? playbackResponse(r) : QByteArray("{}"); };
        HomeServerService service({secrets,dir.filePath("installation"),QUrl("https://plex.tv"),1500,false});
        service.activate("a",dir.filePath("a"));
        const auto key=service.sources({{"type","movie"},{"id","tt1234567"}}).first().toMap().value("infoHash").toString();
        ServicePlayer player; QVERIFY(service.loadPlayback(&player,key)); QTRY_COMPARE(player.loads,1);
        service.reportPlayback(&player,key,12000,100000,false,"start");
        service.reportPlayback(&player,key,25000,100000,false,"progress");
        service.reportPlayback(&player,key,28000,100000,false,"progress");
        auto newOwner=std::make_shared<ServiceProgress>(); service.activate("b",dir.filePath("b"),newOwner);
        service.reportPlayback(&player,key,100000,100000,false,"ended");
        auto reports=[&] { QList<QByteArray> r; for(const auto &q:server.requests) if(q.contains("/Sessions/")) r.append(q); return r; };
        QTRY_COMPARE(reports().size(),3);
        QVERIFY(reports()[0].startsWith("POST /Sessions/Playing "));
        QVERIFY(reports()[1].startsWith("POST /Sessions/Playing/Progress "));
        QVERIFY(reports()[1].contains("280000000"));
        QVERIFY(reports()[2].startsWith("POST /Sessions/Playing/Stopped "));
        QVERIFY(newOwner->rows.isEmpty()); QVERIFY(service.connections().isEmpty());
        for(const auto &r:server.requests) QVERIFY(!r.contains("/UserPlayedItems/"));
    }
    void profileLifecycle() {
        QTemporaryDir dir;
        auto secrets = std::make_shared<ServiceSecrets>();
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        QVERIFY(!service.available());
        service.activate("profile-a", dir.filePath("a"));
        QVERIFY(service.available());
        QVERIFY(service.connections().isEmpty());
        service.deactivate();
        QVERIFY(!service.available());
        QVERIFY(service.connections().isEmpty());
        QVERIFY(service.plexServers().isEmpty());
        QVERIFY(service.plexCode().isEmpty());
    }
    void backendUnavailableFailsClosed() {
        QTemporaryDir dir;
        auto secrets = std::make_shared<ServiceSecrets>();
        secrets->enabled = false;
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        QVERIFY(!service.available());
        service.connectServer("jellyfin", "http://127.0.0.1:1", "REPLAY", "REPLAY", true);
        QVERIFY(!service.busy());
        QVERIFY(!service.status().isEmpty());
        QVERIFY(service.connections().isEmpty());
    }
    void viewNeverContainsSecretsAndSwitchesProfiles() {
        QTemporaryDir dir;
        auto secrets = std::make_shared<ServiceSecrets>();
        HomeServerStore store(secrets);
        QVERIFY(QDir().mkpath(dir.filePath("a")));
        QVERIFY(store.activate("a", dir.filePath("a")));
        const QString id = store.add({{"provider", "emby"}, {"origin", "https://example.test/proxy"},
            {"name", "Living room"}, {"userId", "REPLAY_USER"}}, "REPLAY_TOKEN_DO_NOT_DISPLAY");
        QVERIFY(!id.isEmpty());
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        QCOMPARE(service.connections().size(), 1);
        const auto encoded = QJsonDocument::fromVariant(service.connections()).toJson();
        QVERIFY(!encoded.contains("REPLAY_TOKEN_DO_NOT_DISPLAY"));
        service.setEnabled(id, false);
        QVERIFY(!service.connections().first().toMap().value("enabled").toBool());
        service.activate("b", dir.filePath("b"));
        QVERIFY(service.connections().isEmpty());
        service.remove(id);
        service.activate("a", dir.filePath("a"));
        QCOMPARE(service.connections().size(), 1);
        service.remove(id);
        QVERIFY(service.connections().isEmpty());
        QVERIFY(secrets->values.isEmpty());
    }
    void insecureAddressAndNoImplicitSignin() {
        QTemporaryDir dir;
        auto secrets = std::make_shared<ServiceSecrets>();
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        QVERIFY(service.isInsecureAddress("home.local:8096/proxy"));
        QVERIFY(!service.isInsecureAddress("https://home.local/proxy"));
        service.connectServer("emby", "http://127.0.0.1:1/proxy", "REPLAY", "REPLAY", false);
        QTRY_VERIFY(!service.busy());
        QVERIFY(service.httpConsentRequired());
        QVERIFY(service.connections().isEmpty());
        service.cancelSignIn();
        QVERIFY(!service.httpConsentRequired());
    }
    void signInStoresOnlyNativeToken_data() {
        QTest::addColumn<QString>("provider");
        QTest::newRow("jellyfin") << QString("jellyfin");
        QTest::newRow("emby") << QString("emby");
    }
    void signInStoresOnlyNativeToken() {
        QFETCH(QString, provider);
        QTemporaryDir dir;
        ServiceHttp server;
        server.response = [](const QByteArray &) {
            return QByteArray("{\"AccessToken\":\"REPLAY_NATIVE_SECRET\",\"User\":{\"Id\":\"user-id\",\"Name\":\"Replay\"}}");
        };
        auto secrets = std::make_shared<ServiceSecrets>();
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        QSignalSpy added(&service, &HomeServerService::connectionAdded);
        service.connectServer(provider, server.origin().toString() + "/reverse-proxy", "Replay", "", true);
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(service.connections().size(), 1);
        QCOMPARE(secrets->values.size(), 1);
        QVERIFY(!QJsonDocument::fromVariant(service.connections()).toJson().contains("REPLAY_NATIVE_SECRET"));
        QVERIFY(server.requests.first().startsWith("POST /reverse-proxy/Users/AuthenticateByName "));
        QVERIFY(server.requests.first().contains("\"Pw\":\"\""));
        QFile metadata(dir.filePath("a/home-servers/connections-v1.json"));
        QVERIFY(metadata.open(QIODevice::ReadOnly));
        QVERIFY(!metadata.readAll().contains("REPLAY_NATIVE_SECRET"));
    }
    void profileChangeDiscardsPendingSignIn() {
        QTemporaryDir dir;
        ServiceHttp server;
        server.delayMs = 150;
        server.response = [](const QByteArray &) {
            return QByteArray("{\"AccessToken\":\"REPLAY\",\"User\":{\"Id\":\"user\"}}");
        };
        auto secrets = std::make_shared<ServiceSecrets>();
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        service.connectServer("jellyfin", server.origin().toString(), "user", "fake", true);
        QTRY_COMPARE(server.requests.size(), 1);
        service.activate("b", dir.filePath("b"));
        QTest::qWait(250);
        QVERIFY(service.connections().isEmpty());
        QVERIFY(secrets->values.isEmpty());
        service.activate("a", dir.filePath("a"));
        QVERIFY(service.connections().isEmpty());
    }
    void reentrantProfileChangePreventsSubmission() {
        QTemporaryDir dir;
        ServiceHttp server;
        auto secrets = std::make_shared<ServiceSecrets>();
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        connect(&service, &HomeServerService::changed, &service, [&] {
            if (service.busy()) service.activate("b", dir.filePath("b"));
        });
        service.connectServer("emby", server.origin().toString(), "user", "fake", true);
        QTest::qWait(100);
        QVERIFY(server.requests.isEmpty());
        QVERIFY(secrets->values.isEmpty());
        QVERIFY(!service.busy());
    }
    void plexUsesSelectedResourceTokenAndRequiresHttpConsent() {
        QTemporaryDir dir;
        ServiceHttp server;
        server.response = [&](const QByteArray &request) {
            if (request.startsWith("POST /api/v2/pins?"))
                return QByteArray("{\"id\":51,\"code\":\"1234\",\"expiresIn\":60}");
            if (request.startsWith("GET /api/v2/pins/51?"))
                return QByteArray("{\"authToken\":\"REPLAY_ACCOUNT_TOKEN\"}");
            if (request.startsWith("GET /api/v2/resources?"))
                return QJsonDocument(QJsonArray{QJsonObject{{"clientIdentifier", "server-id"}, {"name", "Home Plex"},
                    {"provides", "server"}, {"presence", true}, {"accessToken", "REPLAY_PMS_TOKEN"},
                    {"connections", QJsonArray{QJsonObject{{"uri", server.origin().toString()}, {"local", true}}}}}}).toJson();
            return QByteArray("{\"MediaContainer\":{\"machineIdentifier\":\"server-id\"}}");
        };
        auto secrets = std::make_shared<ServiceSecrets>();
        HomeServerService service({secrets, dir.filePath("installation"), server.origin(), 10, false});
        service.activate("a", dir.filePath("a"));
        service.beginPlex();
        QTRY_COMPARE(service.plexServers().size(), 1);
        QVERIFY(!service.busy());
        QVERIFY(!QJsonDocument::fromVariant(service.plexServers()).toJson().contains("TOKEN"));
        service.selectPlexServer("server-id", false);
        QVERIFY(service.httpConsentRequired());
        QVERIFY(service.connections().isEmpty());
        QCOMPARE(server.requests.size(), 3);
        service.selectPlexServer("server-id", true);
        QTRY_COMPARE(service.connections().size(), 1);
        QCOMPARE(server.requests.size(), 4);
        QVERIFY(server.requests.last().contains("X-Plex-Token: REPLAY_PMS_TOKEN"));
        QVERIFY(!server.requests.last().contains("REPLAY_ACCOUNT_TOKEN"));
        QCOMPARE(secrets->values.cbegin().value(), QByteArray("REPLAY_PMS_TOKEN"));
    }
    void partialRefreshKeepsWholePreviousIndexAnd401StopsRetry() {
        QTemporaryDir dir;
        ServiceHttp server;
        bool partial = true;
        bool expired = false;
        server.response = [&](const QByteArray &request) {
            server.status = expired ? 401 : 200;
            return catalogResponse(request, partial);
        };
        auto secrets = std::make_shared<ServiceSecrets>();
        QVERIFY(QDir().mkpath(dir.filePath("a")));
        HomeServerStore saved(secrets);
        QVERIFY(saved.activate("a", dir.filePath("a")));
        const auto id = saved.add({{"provider", "jellyfin"}, {"name", "Home"},
            {"origin", server.origin().toString()}, {"userId", "user"}}, "REPLAY");
        QVERIFY(!id.isEmpty());
        QVERIFY(saved.replaceItems(id, QJsonArray{QJsonObject{{"id", "old-a"}}, QJsonObject{{"id", "old-b"}}}));
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        QCOMPARE(service.itemCount(id), 2);
        service.refresh(id);
        QTRY_VERIFY(!service.connections().first().toMap().value("refreshing").toBool());
        QCOMPARE(service.itemCount(id), 2);
        QCOMPARE(saved.items(id).size(), 2);
        QCOMPARE(saved.items(id).first().toObject().value("id").toString(), QString("old-a"));
        partial = false;
        service.refresh(id);
        QTRY_VERIFY(!service.connections().first().toMap().value("refreshing").toBool());
        QCOMPARE(service.itemCount(id), 1);
        QCOMPARE(saved.items(id).first().toObject().value("id").toString(), QString("new"));
        expired = true;
        service.refresh(id);
        QTRY_VERIFY(service.connections().first().toMap().value("needsReconnect").toBool());
        const auto requests = server.requests.size();
        service.refresh(id);
        service.refresh(id);
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), requests);
        QCOMPARE(service.itemCount(id), 1);
    }
    void refreshReplyCannotEnterAnotherProfile() {
        QTemporaryDir dir;
        ServiceHttp server;
        server.delayMs = 150;
        server.response = [](const QByteArray &request) { return catalogResponse(request); };
        auto secrets = std::make_shared<ServiceSecrets>();
        QVERIFY(QDir().mkpath(dir.filePath("a")));
        HomeServerStore saved(secrets);
        QVERIFY(saved.activate("a", dir.filePath("a")));
        const auto id = saved.add({{"provider", "jellyfin"}, {"name", "Home"},
            {"origin", server.origin().toString()}, {"userId", "user"}}, "REPLAY");
        QVERIFY(saved.replaceItems(id, QJsonArray{QJsonObject{{"id", "old"}}}));
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        service.refresh(id);
        QTRY_COMPARE(server.requests.size(), 1);
        service.activate("b", dir.filePath("b"));
        QTest::qWait(250);
        QVERIFY(service.connections().isEmpty());
        QCOMPARE(service.itemCount(id), 0);
        QCOMPARE(saved.items(id).first().toObject().value("id").toString(), QString("old"));
    }
    void closingSigninDoesNotCancelInitialRefresh() {
        QTemporaryDir dir;
        ServiceHttp server;
        server.response = [](const QByteArray &request) {
            if (request.startsWith("POST /Users/AuthenticateByName "))
                return QByteArray("{\"AccessToken\":\"REPLAY\",\"User\":{\"Id\":\"user\"},\"ServerId\":\"home\"}");
            if (request.startsWith("GET /System/Info "))
                return QByteArray("{\"Id\":\"home\",\"ServerName\":\"Living room\"}");
            return catalogResponse(request);
        };
        auto secrets = std::make_shared<ServiceSecrets>();
        HomeServerService service({secrets, dir.filePath("installation")});
        service.activate("a", dir.filePath("a"));
        connect(&service, &HomeServerService::connectionAdded, &service, [&](const QString &) {
            service.cancelSignIn();
        });
        service.connectServer("jellyfin", server.origin().toString(), "user", "fake", true);
        QTRY_COMPARE(service.connections().size(), 1);
        const auto id = service.connections().first().toMap().value("id").toString();
        QTRY_COMPARE(service.itemCount(id), 1);
        QCOMPARE(service.connections().first().toMap().value("name").toString(), QString("Living room"));
    }
    void nativeBackendReceivesCredentialsWhileSourceRowsStayOpaque() {
        QTemporaryDir dir;
        ServiceHttp server;
        server.response = playbackResponse;
        auto secrets = std::make_shared<ServiceSecrets>();
        const auto id = seedCopy(secrets, dir.filePath("a"), server.origin());
        QVERIFY(!id.isEmpty());
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        auto rows = service.sources({{"type", "movie"}, {"id", "tt1234567"}});
        QCOMPARE(rows.size(), 1);
        const auto row = rows.first().toMap();
        QCOMPARE(row.value("canonicalId").toString(), QString("tt1234567"));
        QVERIFY(!row.contains("url"));
        QVERIFY(!row.contains("headers"));
        QVERIFY(!QJsonDocument::fromVariant(rows).toJson().contains("REPLAY_NATIVE_SECRET"));
        ServicePlayer player;
        QSignalSpy resolved(&service, &HomeServerService::playbackResolved);
        const auto key = row.value("infoHash").toString();
        QVERIFY(service.loadPlayback(&player, key, 60000));
        QTRY_COMPARE(player.loads, 1);
        QCOMPARE(resolved.size(), 1);
        QVERIFY(player.url.startsWith(server.origin().toString() + "/Videos/new/stream?"));
        QVERIFY(!player.url.contains("REPLAY_NATIVE_SECRET"));
        QVERIFY(player.headers.value("Authorization").toString().contains("REPLAY_NATIVE_SECRET"));
        service.setEnabled(id, false);
        QVERIFY(player.stops > 0);
        QVERIFY(service.sources({{"type", "movie"}, {"id", "tt1234567"}}).isEmpty());
        QVERIFY(!service.loadPlayback(&player, key));
        service.remove(id);
        QVERIFY(service.connections().isEmpty());
    }
    void cancelledNegotiationCannotReplaceAnotherSource() {
        QTemporaryDir dir;
        ServiceHttp server;
        server.delayMs = 150;
        server.response = playbackResponse;
        auto secrets = std::make_shared<ServiceSecrets>();
        QVERIFY(!seedCopy(secrets, dir.filePath("a"), server.origin()).isEmpty());
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        const auto key = service.sources({{"type", "movie"}, {"id", "tt1234567"}}).first().toMap().value("infoHash").toString();
        ServicePlayer player;
        QVERIFY(service.loadPlayback(&player, key));
        QTRY_COMPARE(server.requests.size(), 1);
        service.cancelPlayback(&player);
        player.loadSource("https://ordinary.test/video", {});
        QTest::qWait(250);
        QCOMPARE(player.loads, 1);
        QCOMPARE(player.url, QString("https://ordinary.test/video"));
        QVERIFY(player.headers.isEmpty());
    }
    void stopReentrancyCannotStartPlaybackInAnotherProfile() {
        QTemporaryDir dir;
        ServiceHttp server;
        server.response = playbackResponse;
        auto secrets = std::make_shared<ServiceSecrets>();
        QVERIFY(!seedCopy(secrets, dir.filePath("a"), server.origin()).isEmpty());
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        const auto key = service.sources({{"type", "movie"}, {"id", "tt1234567"}}).first().toMap().value("infoHash").toString();
        ServicePlayer player;
        QVERIFY(service.loadPlayback(&player, key));
        QTRY_COMPARE(player.loads, 1);
        player.onStop = [&] { player.onStop = {}; service.activate("b", dir.filePath("b")); };
        QVERIFY(!service.loadPlayback(&player, key));
        QCOMPARE(player.loads, 1);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(service.connections().isEmpty());
    }
    void backendLifetimeAndProfileLeaveFenceHomePlayback() {
        QTemporaryDir dir;
        ServiceHttp server;
        server.delayMs = 150;
        server.response = playbackResponse;
        auto secrets = std::make_shared<ServiceSecrets>();
        QVERIFY(!seedCopy(secrets, dir.filePath("a"), server.origin()).isEmpty());
        HomeServerService service({secrets, dir.filePath("installation"), QUrl("https://plex.tv"), 1500, false});
        service.activate("a", dir.filePath("a"));
        const auto key = service.sources({{"type", "movie"}, {"id", "tt1234567"}}).first().toMap().value("infoHash").toString();
        QSignalSpy resolved(&service, &HomeServerService::playbackResolved);
        auto player = std::make_unique<ServicePlayer>();
        QVERIFY(service.loadPlayback(player.get(), key));
        QTRY_COMPARE(server.requests.size(), 1);
        player.reset();
        QTest::qWait(250);
        QCOMPARE(resolved.size(), 0);
        ServicePlayer next;
        QVERIFY(service.loadPlayback(&next, key));
        QTRY_COMPARE(next.loads, 1);
        service.deactivate();
        QVERIFY(next.stops > 0);
        QVERIFY(!service.loadPlayback(&next, key));
    }
};
QTEST_GUILESS_MAIN(HomeServerServiceTest)
#include "tst_home_server_service.moc"
