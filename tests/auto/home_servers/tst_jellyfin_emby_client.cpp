#include "home_servers/JellyfinEmbyClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QtTest>
#include <memory>
#include <utility>

using namespace Colosseum::HomeServers;

namespace {
ServerSession fixtureSession()
{
    return {QStringLiteral("user123"), QStringLiteral("server123"),
            QByteArrayLiteral("fixture-token")};
}

TransportReply jsonReply(const QJsonObject &body, int status = 200)
{
    return {status, QJsonDocument(body).toJson(QJsonDocument::Compact)};
}
}

class tst_jellyfin_emby_client final : public QObject {
    Q_OBJECT

private slots:
    void baseUrlsAreValidatedAndEmbyPrefixesCorrectly();
    void authenticationIsScopedAndDoesNotLeakCredentials();
    void viewsUseAuthenticatedRequest();
    void pagesPreserveIdsVersionsAndResumeData();
    void failuresAndInvalidInputsFailClosed();
    void lateTransportResponseCannotCallDeletedClient();
};

void tst_jellyfin_emby_client::baseUrlsAreValidatedAndEmbyPrefixesCorrectly()
{
    const QString device = QStringLiteral("device-123");
    JellyfinEmbyClient jellyfin(ServerKind::Jellyfin,
                               QUrl(QStringLiteral("https://media.example/jellyfin/")), device);
    QVERIFY(jellyfin.isValid());
    QCOMPARE(jellyfin.apiBaseUrl().toString(),
             QStringLiteral("https://media.example/jellyfin"));

    JellyfinEmbyClient emby(ServerKind::Emby,
                           QUrl(QStringLiteral("http://192.168.1.2:8096")), device);
    QVERIFY(emby.isValid());
    QCOMPARE(emby.apiBaseUrl().toString(),
             QStringLiteral("http://192.168.1.2:8096/emby"));

    JellyfinEmbyClient embyWithProxy(ServerKind::Emby,
        QUrl(QStringLiteral("https://media.example/myproxy/emby/")), device);
    QCOMPARE(embyWithProxy.apiBaseUrl().path(),
             QStringLiteral("/myproxy/emby"));

    const QStringList invalid = {
        QStringLiteral("ftp://media.example"),
        QStringLiteral("https://user:pass@media.example"),
        QStringLiteral("https://media.example/?apikey=secret"),
        QStringLiteral("https://media.example/#fragment"),
        QStringLiteral("file:///etc/passwd"),
        QStringLiteral("http:///path"),
        QStringLiteral("http://media.example/../api"),
        QStringLiteral("https://media.example/api//other")
    };
    for (const QString &url : invalid) {
        JellyfinEmbyClient client(ServerKind::Jellyfin, QUrl(url), device);
        QVERIFY2(!client.isValid(), qPrintable(url));
    }
    JellyfinEmbyClient invalidDevice(ServerKind::Jellyfin,
        QUrl(QStringLiteral("https://media.example")), QStringLiteral("evil\"\r\nX-Token:1"));
    QVERIFY(!invalidDevice.isValid());
}

void tst_jellyfin_emby_client::authenticationIsScopedAndDoesNotLeakCredentials()
{
    int calls = 0;
    JellyfinEmbyClient client(ServerKind::Emby,
        QUrl(QStringLiteral("https://media.example/proxy")), QStringLiteral("app-device"),
        nullptr, [&calls](const QNetworkRequest &req, bool post,
                          const QByteArray &body, JellyfinEmbyClient::ReplyDone done) {
            ++calls;
            QVERIFY(post);
            QCOMPARE(req.url().path(), QStringLiteral("/proxy/emby/Users/AuthenticateByName"));
            QVERIFY(req.url().query().isEmpty());
            QVERIFY(!req.hasRawHeader("X-Emby-Token"));
            QVERIFY(!req.url().toString().contains(QStringLiteral("my-password")));
            QVERIFY(req.rawHeader("X-Emby-Authorization").startsWith("Emby Client="));
            QVERIFY(req.rawHeader("X-Emby-Authorization").contains("app-device"));
            QCOMPARE(req.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                     int(QNetworkRequest::ManualRedirectPolicy));
            const QJsonObject sent = QJsonDocument::fromJson(body).object();
            QCOMPARE(sent.value(QStringLiteral("Username")).toString(),
                     QStringLiteral("tester"));
            QCOMPARE(sent.value(QStringLiteral("Pw")).toString(),
                     QStringLiteral("my-password"));
            done(jsonReply({
                {QStringLiteral("AccessToken"), QStringLiteral("server-secret")},
                {QStringLiteral("ServerId"), QStringLiteral("remote-server")},
                {QStringLiteral("User"), QJsonObject{
                     {QStringLiteral("Id"), QStringLiteral("user123")}}}
            }));
        });
    ServerResult<ServerSession> result;
    client.authenticate(QStringLiteral("tester"), QStringLiteral("my-password"),
                        [&result](auto r) { result = std::move(r); });
    QCOMPARE(calls, 1);
    QVERIFY(result.ok());
    QCOMPARE(result.value.userId, QStringLiteral("user123"));
    QCOMPARE(result.value.serverId, QStringLiteral("remote-server"));
    QCOMPARE(result.value.accessToken, QByteArray("server-secret"));
}

void tst_jellyfin_emby_client::viewsUseAuthenticatedRequest()
{
    JellyfinEmbyClient client(ServerKind::Jellyfin,
        QUrl(QStringLiteral("http://127.0.0.1:8096/jellyfin")),
        QStringLiteral("device123"), nullptr,
        [](const QNetworkRequest &req, bool post, const QByteArray &body,
           JellyfinEmbyClient::ReplyDone done) {
            QVERIFY(!post);
            QVERIFY(body.isEmpty());
            QCOMPARE(req.url().path(), QStringLiteral("/jellyfin/Users/user123/Views"));
            QVERIFY(req.rawHeader("X-Emby-Authorization").startsWith("MediaBrowser Client="));
            QCOMPARE(req.rawHeader("X-Emby-Token"), QByteArray("fixture-token"));
            done(jsonReply({{QStringLiteral("Items"), QJsonArray{
                QJsonObject{{QStringLiteral("Id"), QStringLiteral("movies123")},
                            {QStringLiteral("Name"), QStringLiteral("Movies")},
                            {QStringLiteral("CollectionType"), QStringLiteral("movies")}},
                QJsonObject{{QStringLiteral("Id"), QStringLiteral("shows123")},
                            {QStringLiteral("Name"), QStringLiteral("Shows")},
                            {QStringLiteral("CollectionType"), QStringLiteral("tvshows")}}
            }}}));
        });
    ServerResult<QList<ServerView>> result;
    client.fetchViews(fixtureSession(), [&result](auto r) { result = std::move(r); });
    QVERIFY(result.ok());
    QCOMPARE(result.value.size(), 2);
    QCOMPARE(result.value[0].id, QStringLiteral("movies123"));
    QCOMPARE(result.value[1].collectionType, QStringLiteral("tvshows"));
}

void tst_jellyfin_emby_client::pagesPreserveIdsVersionsAndResumeData()
{
    int pages = 0;
    JellyfinEmbyClient client(ServerKind::Jellyfin,
        QUrl(QStringLiteral("http://localhost:8096")),
        QStringLiteral("device123"), nullptr,
        [&pages](const QNetworkRequest &req, bool post, const QByteArray &body,
                 JellyfinEmbyClient::ReplyDone done) {
            QVERIFY(!post);
            QVERIFY(body.isEmpty());
            QCOMPARE(req.url().path(), QStringLiteral("/Users/user123/Items"));
            QCOMPARE(req.rawHeader("X-Emby-Token"), QByteArray("fixture-token"));
            const QUrlQuery query(req.url());
            QCOMPARE(query.queryItemValue(QStringLiteral("ParentId")), QStringLiteral("movies123"));
            QCOMPARE(query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("2"));
            QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
            QCOMPARE(query.queryItemValue(QStringLiteral("IncludeItemTypes")),
                     QStringLiteral("Movie,Series,Episode"));
            QCOMPARE(query.queryItemValue(QStringLiteral("EnableMediaSources")),
                     QStringLiteral("true"));
            if (pages++ == 0) {
                QCOMPARE(query.queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("0"));
                done(jsonReply({{QStringLiteral("TotalRecordCount"), 3},
                    {QStringLiteral("Items"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("Id"), QStringLiteral("impostor")},
                        {QStringLiteral("Name"), QStringLiteral("The Same Movie")},
                        {QStringLiteral("Type"), QStringLiteral("Movie")},
                        {QStringLiteral("ProviderIds"), QJsonObject{
                            {QStringLiteral("Imdb"), QStringLiteral("tt7654321")}}}
                    },
                    QJsonObject{
                        {QStringLiteral("Id"), QStringLiteral("correct")},
                        {QStringLiteral("Name"), QStringLiteral("The Same Movie")},
                        {QStringLiteral("Type"), QStringLiteral("Movie")},
                        {QStringLiteral("ProviderIds"), QJsonObject{
                            {QStringLiteral("IMDB"), QStringLiteral("tt1234567")},
                            {QStringLiteral("TMDB"), QStringLiteral("42")}}},
                        {QStringLiteral("MediaSources"), QJsonArray{
                            QJsonObject{{QStringLiteral("Id"), QStringLiteral("video1080")},
                                        {QStringLiteral("SupportsDirectPlay"), true}}}},
                        {QStringLiteral("UserData"), QJsonObject{
                            {QStringLiteral("PlaybackPositionTicks"), qint64(120000000)},
                            {QStringLiteral("Played"), false}}}
                    }
                }}}));
            } else {
                QCOMPARE(query.queryItemValue(QStringLiteral("StartIndex")), QStringLiteral("2"));
                done(jsonReply({{QStringLiteral("TotalRecordCount"), 3},
                    {QStringLiteral("Items"), QJsonArray{
                    QJsonObject{{QStringLiteral("Id"), QStringLiteral("unidentified")},
                                {QStringLiteral("Name"), QStringLiteral("No external ID")},
                                {QStringLiteral("Type"), QStringLiteral("Episode")}}
                }}}));
            }
        });
    ServerResult<ItemPage> first;
    client.fetchItemsPage(fixtureSession(), QStringLiteral("movies123"), 0, 2,
        [&first](auto r) { first = std::move(r); });
    QVERIFY(first.ok());
    QCOMPARE(first.value.totalRecordCount, 3);
    QCOMPARE(first.value.items.size(), 2);
    QVERIFY(!first.value.items[0].matchesImdb(QStringLiteral("tt1234567")));
    QVERIFY(first.value.items[1].matchesImdb(QStringLiteral("TT1234567")));
    QCOMPARE(first.value.items[1].tmdbId, QStringLiteral("42"));
    QCOMPARE(first.value.items[1].mediaSources.size(), 1);
    QCOMPARE(first.value.items[1].userData.value(
                 QStringLiteral("PlaybackPositionTicks")).toInteger(), qint64(120000000));

    ServerResult<ItemPage> second;
    client.fetchItemsPage(fixtureSession(), QStringLiteral("movies123"), 2, 2,
        [&second](auto r) { second = std::move(r); });
    QVERIFY(second.ok());
    QCOMPARE(second.value.startIndex, 2);
    QCOMPARE(second.value.items.size(), 1);
    QVERIFY(!second.value.items.first().hasExternalId());
    QCOMPARE(pages, 2);
}

void tst_jellyfin_emby_client::failuresAndInvalidInputsFailClosed()
{
    int requests = 0;
    JellyfinEmbyClient client(ServerKind::Jellyfin,
        QUrl(QStringLiteral("https://media.example")), QStringLiteral("device"),
        nullptr, [&requests](const QNetworkRequest &, bool, const QByteArray &,
                             JellyfinEmbyClient::ReplyDone done) {
            ++requests;
            done({302, QByteArrayLiteral("{\"AccessToken\":\"should-not-follow\"}")});
        });
    ServerResult<ServerSession> auth;
    client.authenticate(QStringLiteral("user"), QStringLiteral("pass"),
                        [&auth](auto r) { auth = std::move(r); });
    QVERIFY(!auth.ok());
    QCOMPARE(auth.status, 302);
    QVERIFY(auth.value.accessToken.isEmpty());

    ServerResult<ItemPage> page;
    client.fetchItemsPage(fixtureSession(), QStringLiteral("../bad"), 0, 10,
                          [&page](auto r) { page = std::move(r); });
    QVERIFY(!page.ok());
    QCOMPARE(page.status, 400);
    client.fetchItemsPage(fixtureSession(), QStringLiteral("movies123"), -1, 10,
                          [&page](auto r) { page = std::move(r); });
    QCOMPARE(page.status, 400);
    client.fetchItemsPage(fixtureSession(), QStringLiteral("movies123"), 0, 201,
                          [&page](auto r) { page = std::move(r); });
    QCOMPARE(page.status, 400);
    QCOMPARE(requests, 1);

    JellyfinEmbyClient badTokenClient(ServerKind::Jellyfin,
        QUrl(QStringLiteral("https://media.example")), QStringLiteral("device"),
        nullptr, [&requests](const auto &, bool, const auto &, auto) { ++requests; });
    ServerSession invalid = fixtureSession();
    invalid.accessToken = "bad\r\nInjected: yes";
    badTokenClient.fetchViews(invalid, [](auto r) { QCOMPARE(r.status, 400); });
    QCOMPARE(requests, 1);
}

void tst_jellyfin_emby_client::lateTransportResponseCannotCallDeletedClient()
{
    JellyfinEmbyClient::ReplyDone held;
    bool delivered = false;
    auto client = std::make_unique<JellyfinEmbyClient>(
        ServerKind::Jellyfin, QUrl(QStringLiteral("https://media.example")),
        QStringLiteral("device"), nullptr,
        [&held](const auto &, bool, const auto &, auto done) {
            held = std::move(done);
        });
    client->authenticate(QStringLiteral("user"), QStringLiteral("pass"),
                         [&delivered](auto) { delivered = true; });
    QVERIFY(bool(held));
    client.reset();
    std::exchange(held, {})(jsonReply({
        {QStringLiteral("AccessToken"), QStringLiteral("after-destruction")},
        {QStringLiteral("ServerId"), QStringLiteral("server")},
        {QStringLiteral("User"), QJsonObject{
            {QStringLiteral("Id"), QStringLiteral("user123")}}}
    }));
    QVERIFY(!delivered);
}

QTEST_GUILESS_MAIN(tst_jellyfin_emby_client)
#include "tst_jellyfin_emby_client.moc"
