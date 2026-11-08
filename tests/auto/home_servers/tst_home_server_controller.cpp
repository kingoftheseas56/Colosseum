#include "home_servers/HomeServerConnectionsController.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <algorithm>
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
    void completeRefreshPrunesOnlyAfterEveryPageSucceeds();
    void canceledRefreshCannotReplacePreviousSnapshot();
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

void tst_home_server_controller::completeRefreshPrunesOnlyAfterEveryPageSucceeds()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto secrets = std::make_shared<MemorySecrets>();
    int mode = 1;
    int itemRequests = 0;
    HomeServerConnectionsController controller(secrets, nullptr,
        [&mode, &itemRequests](const QNetworkRequest &req, bool post,
                               const QByteArray &, JellyfinEmbyClient::ReplyDone done) {
            if (post) {
                done(accepted());
                return;
            }
            if (req.url().path() == QLatin1String("/UserViews")) {
                done({200, QJsonDocument(QJsonObject{
                    {"Items", QJsonArray{
                        QJsonObject{{"Id", "movies"}, {"CollectionType", "movies"},
                                    {"Name", "Movies"}},
                        QJsonObject{{"Id", "shows"}, {"CollectionType", "tvshows"},
                                    {"Name", "Shows"}},
                        QJsonObject{{"Id", "music"}, {"CollectionType", "music"},
                                    {"Name", "Music"}}
                    }}
                }).toJson(QJsonDocument::Compact)});
                return;
            }
            QCOMPARE(req.url().path(), QStringLiteral("/Items"));
            const QUrlQuery query(req.url());
            QCOMPARE(query.queryItemValue(QStringLiteral("userId")),
                     QStringLiteral("user-1"));
            ++itemRequests;
            const QString library = query.queryItemValue(QStringLiteral("ParentId"));
            const int offset = query.queryItemValue(QStringLiteral("StartIndex")).toInt();
            QJsonArray rows;
            int total = 0;
            if (library == QLatin1String("movies")) {
                total = mode == 3 ? 200 : 201;
                if (mode == 2 && offset >= 200) {
                    done({503, QByteArrayLiteral("{}")});
                    return;
                }
                for (int n = offset; n < total && n < offset + 200; ++n) {
                    const QString itemId = QStringLiteral("film-%1").arg(n, 3, 10, QLatin1Char('0'));
                    rows.append(QJsonObject{
                        {"Id", itemId}, {"Type", "Movie"}, {"Name", "Film"},
                        {"ProviderIds", QJsonObject{{"Imdb", "tt1234567"}}},
                        {"MediaSources", QJsonArray{QJsonObject{{"Id", itemId + QStringLiteral("-hd")}}}}
                    });
                }
            } else {
                QCOMPARE(library, QStringLiteral("shows"));
                QCOMPARE(offset, 0);
                total = 2;
                rows.append(QJsonObject{
                    {"Id", mode == 4 ? "film-000" : "series-x"},
                    {"Type", "Series"}, {"ProviderIds", QJsonObject{{"Imdb", "tt9876543"}}}
                });
                rows.append(QJsonObject{
                    {"Id", "episode-x"}, {"Type", "Episode"},
                    {"SeriesId", "series-x"}, {"ParentIndexNumber", 1}, {"IndexNumber", 2}
                });
            }
            done({200, QJsonDocument(QJsonObject{
                {"TotalRecordCount", total}, {"Items", rows}
            }).toJson(QJsonDocument::Compact)});
        });
    controller.activateProfile(QStringLiteral("local"), directory.path());
    QVERIFY(controller.available());
    QVERIFY(controller.connectServer(QStringLiteral("jellyfin"), QStringLiteral("Home"),
        QStringLiteral("https://media.example"), QStringLiteral("user"),
        QStringLiteral("password")));
    const QString id = controller.connections().first().toMap().value("id").toString();
    HomeServerCatalogueStore index(QStringLiteral("local"), directory.path());

    QVERIFY(controller.refreshConnection(id));
    QVERIFY(!controller.refreshing());
    QVERIFY(!controller.hasError());
    QCOMPARE(controller.connections().first().toMap().value("indexedCount").toInt(), 203);
    auto current = index.load(id, QStringLiteral("user-1"), QStringLiteral("server-1"));
    QVERIFY(current);
    QCOMPARE(current->items.size(), 203);
    QCOMPARE(current->libraries.size(), 2);
    QCOMPARE(current->identifiedCount(), 202);
    QCOMPARE(current->items.last().item.episodeNumber, 2);
    QVERIFY(itemRequests >= 3);

    mode = 2; // A successful first page followed by server failure.
    QVERIFY(controller.refreshConnection(id));
    QVERIFY(controller.hasError());
    current = index.load(id, QStringLiteral("user-1"), QStringLiteral("server-1"));
    QVERIFY(current);
    QCOMPARE(current->items.size(), 203);
    QCOMPARE(controller.connections().first().toMap().value("indexedCount").toInt(), 203);

    mode = 4; // Duplicate remote ID across two libraries must not sweep.
    QVERIFY(controller.refreshConnection(id));
    QVERIFY(controller.hasError());
    current = index.load(id, QStringLiteral("user-1"), QStringLiteral("server-1"));
    QVERIFY(current);
    QCOMPARE(current->items.size(), 203);

    mode = 3; // All pages complete; deleted film-200 is now removed.
    QVERIFY(controller.refreshConnection(id));
    QVERIFY(!controller.hasError());
    current = index.load(id, QStringLiteral("user-1"), QStringLiteral("server-1"));
    QVERIFY(current);
    QCOMPARE(current->items.size(), 202);
    QCOMPARE(controller.connections().first().toMap().value("indexedCount").toInt(), 202);
    QVERIFY(std::none_of(current->items.cbegin(), current->items.cend(),
        [](const IndexedServerItem &row) { return row.item.id == QLatin1String("film-200"); }));

    controller.activateProfile(QStringLiteral("other"), directory.path());
    QVERIFY(controller.connections().isEmpty());
    QVERIFY(!HomeServerCatalogueStore(QStringLiteral("other"), directory.path()).load(
        id, QStringLiteral("user-1"), QStringLiteral("server-1")));
    controller.activateProfile(QStringLiteral("local"), directory.path());
    QCOMPARE(controller.connections().first().toMap().value("indexedCount").toInt(), 202);
    QVERIFY(controller.removeConnection(id));
    QVERIFY(!index.load(id, QStringLiteral("user-1"), QStringLiteral("server-1")));
}

void tst_home_server_controller::canceledRefreshCannotReplacePreviousSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto secrets = std::make_shared<MemorySecrets>();
    JellyfinEmbyClient::ReplyDone held;
    HomeServerConnectionsController controller(secrets, nullptr,
        [&held](const QNetworkRequest &req, bool post, const QByteArray &,
                JellyfinEmbyClient::ReplyDone done) {
            if (post) {
                done(accepted());
                return;
            }
            if (req.url().path() == QLatin1String("/UserViews")) {
                done({200, QJsonDocument(QJsonObject{
                    {"Items", QJsonArray{QJsonObject{
                        {"Id", "movies"}, {"CollectionType", "movies"}
                    }}}
                }).toJson(QJsonDocument::Compact)});
                return;
            }
            held = std::move(done);
        });
    controller.activateProfile(QStringLiteral("local"), directory.path());
    QVERIFY(controller.connectServer(QStringLiteral("jellyfin"), QStringLiteral("Home"),
        QStringLiteral("https://media.example"), QStringLiteral("user"),
        QStringLiteral("password")));
    const QString id = controller.connections().first().toMap().value("id").toString();
    HomeServerCatalogueStore index(QStringLiteral("local"), directory.path());
    QVERIFY(controller.refreshConnection(id));
    QVERIFY(controller.refreshing());
    QVERIFY(bool(held));
    controller.cancelRefresh();
    QVERIFY(!controller.refreshing());
    std::exchange(held, {})({200, QJsonDocument(QJsonObject{
        {"TotalRecordCount", 1}, {"Items", QJsonArray{
            QJsonObject{{"Id", "film-late"}, {"Type", "Movie"}}
        }}
    }).toJson(QJsonDocument::Compact)});
    QVERIFY(!index.load(id, QStringLiteral("user-1"), QStringLiteral("server-1")));
    QVERIFY(controller.refreshConnection(id));
    QVERIFY(controller.refreshing());
    controller.activateProfile(QStringLiteral("another-profile"), directory.path());
    QVERIFY(!controller.refreshing());
    QVERIFY(bool(held));
    std::exchange(held, {})({200, QJsonDocument(QJsonObject{
        {"TotalRecordCount", 0}, {"Items", QJsonArray{}}
    }).toJson(QJsonDocument::Compact)});
    QVERIFY(!index.load(id, QStringLiteral("user-1"), QStringLiteral("server-1")));
}

QTEST_GUILESS_MAIN(tst_home_server_controller)
#include "tst_home_server_controller.moc"
