#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include "HomeServerService.h"

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
private slots:
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
};
QTEST_GUILESS_MAIN(HomeServerServiceTest)
#include "tst_home_server_service.moc"
