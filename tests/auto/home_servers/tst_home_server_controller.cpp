#include "home_servers/HomeServerConnectionsController.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkRequest>
#include <QtTest>
#include <functional>
#include <memory>
#include <utility>

using namespace Colosseum::HomeServers;

namespace {
class MemorySecrets final : public AccountSecretBackend {
public:
    bool ready = true;
    QMap<QString, QByteArray> records;
    bool isAvailable() const override { return ready; }
    bool write(const QString &key, const QByteArray &body) override {
        if (!ready || key.isEmpty() || body.isEmpty() || body.size() > 2560)
            return false;
        records[key] = body;
        return true;
    }
    std::optional<QByteArray> read(const QString &key) const override {
        if (!ready || !records.contains(key))
            return std::nullopt;
        return records.value(key);
    }
    bool remove(const QString &key) override {
        if (!ready) return false;
        records.remove(key);
        return true;
    }
    QStringList targets(const QString &prefix) const override {
        QStringList list;
        if (!ready) return list;
        for (auto it = records.cbegin(); it != records.cend(); ++it)
            if (it.key().startsWith(prefix)) list.append(it.key());
        return list;
    }
};

TransportReply accepted(const QString &user = QStringLiteral("user-1"),
                        const QByteArray &token = QByteArrayLiteral("secret-token")) {
    return {200, QJsonDocument(QJsonObject{
        {"User", QJsonObject{{"Id", user}}},
        {"AccessToken", QString::fromLatin1(token)},
        {"ServerId", QStringLiteral("server-1")}
    }).toJson(QJsonDocument::Compact)};
}
}

class tst_home_server_controller : public QObject {
    Q_OBJECT
private slots:
    void signInSavesOneProfileWithoutExposingToken();
    void embyRetriesOnlySameOriginAfterNotFound();
    void wrongCredentialsDoNotRetryOrSave();
    void profileSwitchFencesLateAuthentication();
    void cancelFencesLateAuthentication();
    void duplicateAndUnavailableStoresFailClosed();
};

void tst_home_server_controller::signInSavesOneProfileWithoutExposingToken()
{
    auto secrets = std::make_shared<MemorySecrets>();
    int requests = 0;
    HomeServerConnectionsController controller(secrets, nullptr,
        [&requests](const QNetworkRequest &req, bool post, const QByteArray &body,
                    JellyfinEmbyClient::ReplyDone done) {
            ++requests;
            QVERIFY(post);
            QCOMPARE(req.url().toString(),
                     QStringLiteral("https://media.example/jellyfin/Users/AuthenticateByName"));
            QVERIFY(req.rawHeader("Authorization").startsWith("MediaBrowser Client="));
            QVERIFY(!req.hasRawHeader("X-Emby-Token"));
            QCOMPARE(QJsonDocument::fromJson(body).object().value("Pw").toString(),
                     QStringLiteral("pass-1"));
            done(accepted());
        });
    controller.activateProfile(QStringLiteral("local"));
    QVERIFY(controller.available());
    QVERIFY(controller.connectServer(QStringLiteral("jellyfin"), QStringLiteral("Home"),
        QStringLiteral("https://media.example/jellyfin"),
        QStringLiteral("username"), QStringLiteral("pass-1")));
    QCOMPARE(requests, 1);
    QVERIFY(!controller.busy());
    QVERIFY(!controller.hasError());
    QCOMPARE(controller.connections().size(), 1);
    const auto row = controller.connections().first().toMap();
    QCOMPARE(row.value("name").toString(), QStringLiteral("Home"));
    QVERIFY(row.value("id").toString().size() > 20);
    QVERIFY(!row.contains("accessToken"));
    QVERIFY(!row.contains("password"));
    QVERIFY(!QJsonDocument::fromVariant(controller.connections()).toJson().contains("secret-token"));
    QVERIFY(controller.setConnectionEnabled(row.value("id").toString(), false));
    QCOMPARE(controller.connections().first().toMap().value("enabled").toBool(), false);
    QVERIFY(controller.removeConnection(row.value("id").toString()));
    QVERIFY(controller.connections().isEmpty());
    QVERIFY(secrets->records.isEmpty());
}

void tst_home_server_controller::embyRetriesOnlySameOriginAfterNotFound()
{
    auto secrets = std::make_shared<MemorySecrets>();
    QStringList urls;
    HomeServerConnectionsController controller(secrets, nullptr,
        [&urls](const QNetworkRequest &req, bool, const QByteArray &body,
                JellyfinEmbyClient::ReplyDone done) {
            urls << req.url().toString();
            QVERIFY(req.url().scheme() == QLatin1String("https"));
            QCOMPARE(req.url().host(), QStringLiteral("media.example"));
            QVERIFY(!req.hasRawHeader("Authorization"));
            QCOMPARE(req.rawHeader("X-Emby-Authorization").left(12),
                     QByteArray("Emby Client="));
            QCOMPARE(QJsonDocument::fromJson(body).object().value("Pw").toString(),
                     QStringLiteral("password"));
            if (urls.size() == 1) done({404, {}});
            else done(accepted());
        });
    controller.activateProfile(QStringLiteral("local"));
    QVERIFY(controller.connectServer(QStringLiteral("emby"), QStringLiteral("Emby"),
        QStringLiteral("https://media.example/proxy"),
        QStringLiteral("username"), QStringLiteral("password")));
    QCOMPARE(urls.size(), 2);
    QCOMPARE(urls[0], QStringLiteral("https://media.example/proxy/Users/AuthenticateByName"));
    QCOMPARE(urls[1], QStringLiteral("https://media.example/proxy/emby/Users/AuthenticateByName"));
    QCOMPARE(controller.connections().size(), 1);
    QCOMPARE(controller.connections().first().toMap().value("address").toString(),
             QStringLiteral("https://media.example/proxy/emby"));
}

void tst_home_server_controller::wrongCredentialsDoNotRetryOrSave()
{
    auto secrets = std::make_shared<MemorySecrets>();
    int calls = 0;
    HomeServerConnectionsController controller(secrets, nullptr,
        [&calls](const QNetworkRequest &, bool, const QByteArray &,
                 JellyfinEmbyClient::ReplyDone done) {
            ++calls;
            done({401, {}});
        });
    controller.activateProfile(QStringLiteral("local"));
    QVERIFY(controller.connectServer(QStringLiteral("emby"), QStringLiteral("Emby"),
        QStringLiteral("https://media.example"), QStringLiteral("name"),
        QStringLiteral("wrong")));
    QCOMPARE(calls, 1);
    QVERIFY(controller.hasError());
    QCOMPARE(controller.connections().size(), 0);
    QVERIFY(secrets->records.isEmpty());
    QVERIFY(!controller.connectServer(QStringLiteral("plex"), QStringLiteral("Invalid"),
        QStringLiteral("https://media.example"), QStringLiteral("name"),
        QStringLiteral("password")));
    QCOMPARE(calls, 1);
}

void tst_home_server_controller::profileSwitchFencesLateAuthentication()
{
    auto secrets = std::make_shared<MemorySecrets>();
    JellyfinEmbyClient::ReplyDone held;
    HomeServerConnectionsController controller(secrets, nullptr,
        [&held](const QNetworkRequest &, bool, const QByteArray &,
                JellyfinEmbyClient::ReplyDone done) { held = std::move(done); });
    controller.activateProfile(QStringLiteral("profile-A"));
    QVERIFY(controller.connectServer(QStringLiteral("jellyfin"), QStringLiteral("A"),
        QStringLiteral("https://media.example"), QStringLiteral("name"),
        QStringLiteral("password")));
    QVERIFY(controller.busy());
    QVERIFY(bool(held));
    controller.activateProfile(QStringLiteral("profile-B"));
    QVERIFY(!controller.busy());
    QCOMPARE(controller.profileId(), QStringLiteral("profile-B"));
    std::exchange(held, {})(accepted());
    QVERIFY(controller.connections().isEmpty());
    QVERIFY(secrets->records.isEmpty());
}

void tst_home_server_controller::cancelFencesLateAuthentication()
{
    auto secrets = std::make_shared<MemorySecrets>();
    JellyfinEmbyClient::ReplyDone held;
    HomeServerConnectionsController controller(secrets, nullptr,
        [&held](const QNetworkRequest &, bool, const QByteArray &,
                JellyfinEmbyClient::ReplyDone done) { held = std::move(done); });
    controller.activateProfile(QStringLiteral("local"));
    QVERIFY(controller.connectServer(QStringLiteral("jellyfin"), QStringLiteral("Home"),
        QStringLiteral("https://media.example"), QStringLiteral("name"),
        QStringLiteral("password")));
    controller.cancelConnect();
    QVERIFY(!controller.busy());
    std::exchange(held, {})(accepted());
    QVERIFY(controller.connections().isEmpty());
    QVERIFY(secrets->records.isEmpty());
}

void tst_home_server_controller::duplicateAndUnavailableStoresFailClosed()
{
    auto secrets = std::make_shared<MemorySecrets>();
    HomeServerConnectionsController controller(secrets, nullptr,
        [](const QNetworkRequest &, bool, const QByteArray &,
           JellyfinEmbyClient::ReplyDone done) { done(accepted()); });
    controller.activateProfile(QStringLiteral("local"));
    for (int i = 0; i != 2; ++i) {
        QVERIFY(controller.connectServer(QStringLiteral("jellyfin"), QStringLiteral("Home"),
            QStringLiteral("https://media.example"), QStringLiteral("user"),
            QStringLiteral("password")));
    }
    QCOMPARE(controller.connections().size(), 1);
    QVERIFY(controller.hasError());
    secrets->ready = false;
    controller.activateProfile(QStringLiteral("local"));
    QVERIFY(!controller.available());
    QVERIFY(!controller.connectServer(QStringLiteral("jellyfin"), QStringLiteral("Home"),
        QStringLiteral("https://media.example"), QStringLiteral("user"),
        QStringLiteral("password")));
    QVERIFY(controller.connections().isEmpty());
}

QTEST_GUILESS_MAIN(tst_home_server_controller)
#include "tst_home_server_controller.moc"
