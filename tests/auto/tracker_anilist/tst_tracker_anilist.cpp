#include <QtTest>

#include "account/ProfilePaths.h"
#include "trackers/AniListApiClient.h"
#include "trackers/AniListConnectionController.h"
#include "trackers/AniListLoopbackServer.h"
#include "trackers/TrackerConnectionStore.h"
#include "trackers/AniListHttpTransport.h"
#include "trackers/AniListProtocol.h"
#include "trackers/TrackerCredentialVault.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <optional>

namespace {

AniListAuthConfiguration configuration()
{
    AniListAuthConfiguration value;
    value.clientId = QStringLiteral("55437");
    value.authorizationEndpoint =
        QUrl(QStringLiteral("https://anilist.co/api/v2/oauth/authorize"));
    value.graphqlEndpoint = QUrl(QStringLiteral("https://graphql.anilist.co"));
    value.redirectUri =
        QUrl(QStringLiteral("http://127.0.0.1:47823/anilist/callback"));
    value.appName = QStringLiteral("Colosseum");
    value.appVersion = QStringLiteral("test");
    return value;
}

class FakeVault final : public TrackerCredentialVault
{
public:
    bool isAvailable() const override { return true; }

    bool hasReusableCredential(const TrackerCredentialSlot &slot,
                               qint64 nowMs) const override
    {
        const auto current =
            loadForProfile(slot.profileId, slot.providerId);
        return current && current->accessTokenExpiresAtMs > nowMs;
    }

    bool saveAndVerify(const TrackerCredential &credential) override
    {
        saved = credential;
        return true;
    }

    std::optional<TrackerCredential> loadForProfile(
        const QString &profileId,
        TrackerProviderId providerId) const override
    {
        if (!saved || saved->slot.profileId != profileId
            || saved->slot.providerId != providerId) {
            return std::nullopt;
        }
        return saved;
    }

    bool clearForProfile(const QString &, TrackerProviderId) override
    {
        saved.reset();
        return true;
    }

    std::optional<TrackerCredential> saved;
};

class FakeTransport final : public AniListHttpTransport
{
public:
    using AniListHttpTransport::AniListHttpTransport;

    void post(const AniListAuthConfiguration &,
              const QByteArray &accessToken,
              const QJsonObject &body,
              ApiCompletion completion) override
    {
        ++postCalls;
        lastAccessToken = accessToken;
        lastBody = body;
        completion(result);
    }

    AniListApiResult result{
        AniListTransportError::None, 200,
        QJsonDocument(QJsonObject{}), 0, false, false};
    int postCalls = 0;
    QByteArray lastAccessToken;
    QJsonObject lastBody;
};

TrackerCredential credentialFor(const ProfilePaths &profile,
                                qint64 accessExpiry)
{
    TrackerCredential value;
    value.slot = {profile.profileId(),
                  TrackerProviderId::AniList,
                  QStringLiteral("99")};
    value.accessToken = QByteArrayLiteral("year-long-token");
    value.accessTokenExpiresAtMs = accessExpiry;
    return value;
}

QJsonObject media(int id,
                  const QJsonValue &idMal,
                  const QString &romaji,
                  const QJsonValue &episodes,
                  const QJsonValue &chapters)
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("idMal"), idMal},
        {QStringLiteral("title"), QJsonObject{
            {QStringLiteral("romaji"), romaji},
            {QStringLiteral("english"), QJsonValue::Null}}},
        {QStringLiteral("episodes"), episodes},
        {QStringLiteral("chapters"), chapters}};
}

QJsonObject entry(const QJsonObject &mediaObject,
                  const QString &status,
                  const QJsonValue &progress,
                  const QJsonValue &updatedAt = 1790000000)
{
    return QJsonObject{{QStringLiteral("status"), status},
                       {QStringLiteral("progress"), progress},
                       {QStringLiteral("updatedAt"), updatedAt},
                       {QStringLiteral("media"), mediaObject}};
}

QJsonDocument collectionDocument(const QJsonArray &entries,
                                 bool hasNext = false)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("MediaListCollection"), QJsonObject{
                {QStringLiteral("hasNextChunk"), hasNext},
                {QStringLiteral("lists"), QJsonArray{QJsonObject{
                    {QStringLiteral("entries"), entries}}}}}}}}});
}

QJsonDocument entryStateDocument(const QString &field,
                                 const QJsonObject &state)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("data"), QJsonObject{{field, state}}}});
}

QJsonObject stateObject()
{
    return QJsonObject{{QStringLiteral("id"), 7},
                       {QStringLiteral("mediaId"), 21},
                       {QStringLiteral("status"), QStringLiteral("CURRENT")},
                       {QStringLiteral("progress"), 12},
                       {QStringLiteral("updatedAt"), 1790000000}};
}

QJsonDocument errorDocument(int status, const QString &message)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("errors"), QJsonArray{QJsonObject{
            {QStringLiteral("message"), message},
            {QStringLiteral("status"), status}}}},
        {QStringLiteral("data"), QJsonValue::Null}});
}


// ---- S2: loopback server and connection controller helpers ----

const QByteArray kState = QByteArrayLiteral("state-0123456789abcdef");
const QByteArray kToken = QByteArrayLiteral("eyJ0eXAi.secret-token_value.sig");

quint16 freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

QUrl redirectFor(quint16 port)
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/anilist/callback")
                    .arg(port));
}

struct HttpReply {
    bool connected = false;
    int status = 0;
    QByteArray raw;
    QByteArray body;
};

HttpReply exchange(quint16 port, const QByteArray &request)
{
    HttpReply reply;
    QTcpSocket socket;
    QObject::connect(&socket, &QTcpSocket::readyRead,
                     [&] { reply.raw += socket.readAll(); });
    socket.connectToHost(QHostAddress::LocalHost, port);
    (void)QTest::qWaitFor([&] {
        return socket.state() == QAbstractSocket::ConnectedState
            || socket.state() == QAbstractSocket::UnconnectedState;
    }, 3000);
    if (socket.state() != QAbstractSocket::ConnectedState)
        return reply;
    reply.connected = true;
    socket.write(request);
    (void)QTest::qWaitFor([&] {
        return socket.state() == QAbstractSocket::UnconnectedState;
    }, 5000);
    reply.raw += socket.readAll();
    const QList<QByteArray> parts = reply.raw.split(' ');
    if (parts.size() > 1)
        reply.status = parts.at(1).toInt();
    const qsizetype split = reply.raw.indexOf("\r\n\r\n");
    if (split >= 0)
        reply.body = reply.raw.mid(split + 4);
    return reply;
}

QByteArray getRequest(quint16 port, const QByteArray &path)
{
    return "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:"
        + QByteArray::number(port) + "\r\nConnection: close\r\n\r\n";
}

QByteArray postRequest(quint16 port,
                       const QByteArray &path,
                       const QByteArray &body,
                       const QByteArray &origin = {},
                       const QByteArray &method = "POST",
                       const QByteArray &contentType =
                           "application/x-www-form-urlencoded",
                       const QByteArray &host = {})
{
    const QByteArray portText = QByteArray::number(port);
    QByteArray request = method + " " + path + " HTTP/1.1\r\nHost: "
        + (host.isEmpty() ? "127.0.0.1:" + portText : host) + "\r\n";
    request += "Origin: "
        + (origin.isEmpty() ? "http://127.0.0.1:" + portText : origin)
        + "\r\n";
    request += "Content-Type: " + contentType + "\r\n";
    request += "Content-Length: " + QByteArray::number(body.size())
        + "\r\nConnection: close\r\n\r\n" + body;
    return request;
}

QByteArray fragmentFor(const QByteArray &state,
                       const QByteArray &token = kToken,
                       const QByteArray &type = "Bearer",
                       const QByteArray &expires = "31536000")
{
    QByteArray body = "access_token=" + token + "&token_type=" + type
        + "&expires_in=" + expires;
    if (!state.isEmpty())
        body += "&state=" + state;
    return body;
}

QJsonDocument viewerDocument(int id = 5821)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("Viewer"), QJsonObject{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), QStringLiteral("Test User")}}}}}});
}

const QByteArray kEntropy = QByteArray(32, '\x07');

struct ControllerFixture {
    QTemporaryDir root;
    ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeVault vault;
    FakeTransport transport;
    AniListLoopbackServer loopback;
    TrackerConnectionStore connections{profile};
    quint16 port = freePort();
    QList<QUrl> opened;
    AniListConnectionController controller;

    static AniListAuthConfiguration config(quint16 port)
    {
        auto value = configuration();
        value.redirectUri = redirectFor(port);
        return value;
    }

    ControllerFixture()
        : controller(
              profile, &connections, config(port), &vault, &transport,
              &loopback,
              [this](const QUrl &url) { opened.append(url); return true; },
              [](qsizetype bytes) { return kEntropy.left(bytes); })
    {
        transport.result = AniListApiResult{
            AniListTransportError::None, 200, viewerDocument(), 0, false,
            false};
    }

    QByteArray state() const
    {
        return kEntropy.toBase64(QByteArray::Base64UrlEncoding
                                 | QByteArray::OmitTrailingEquals);
    }

    // Plays the browser: delivers the fragment through the loopback.
    HttpReply deliverToken(const QByteArray &token = kToken,
                           const QByteArray &expires = "31536000")
    {
        return exchange(port, postRequest(
            port, "/anilist/callback/token",
            fragmentFor(state(), token, "Bearer", expires)));
    }
};

} // namespace

class TrackerAniListTest final : public QObject
{
    Q_OBJECT

private slots:
    void configuration_validity();
    void configuration_rejects_bad_values();
    void remote_identity_round_trip();
    void status_mapping();
    void viewer_request_and_parse();
    void collection_request_shape();
    void collection_parse_anime_with_absent_idmal();
    void collection_parse_manga_uses_chapters();
    void collection_parse_fails_closed();
    void collection_parse_bounds_list_size();
    void media_lookup_request_and_parse();
    void save_entry_request_shape();
    void save_and_readback_parse();
    void entry_request_shape();
    void graphql_errors_fail_closed_everywhere();
    void classification_rate_limited_with_retry_after();
    void classification_auth_server_network_and_redirect();
    void classification_malformed_and_oversize();
    void client_sends_stored_token_without_refresh();
    void client_unauthorized_requires_reauthorization_no_retry();
    void client_missing_or_expired_credential_makes_no_request();
    void unconfigured_build_fails_closed();
    void loopback_serves_static_callback_page();
    void loopback_binds_loopback_only();
    void loopback_accepts_one_valid_token();
    void loopback_rejects_invalid_requests();
    void loopback_rejects_second_token_after_success();
    void loopback_reports_provider_error_and_timeout();
    void controller_success_stores_credential();
    void controller_viewer_failure_stores_nothing();
    void controller_cancel_and_timeout();
    void controller_disconnect_removes_credential();
    void controller_unauthorized_requires_reconnect_and_keeps_record();
    void controller_unconfigured_is_unavailable();
};

void TrackerAniListTest::configuration_validity()
{
    QVERIFY(aniListConfigurationIsValid(configuration()));
    auto localhost = configuration();
    localhost.redirectUri =
        QUrl(QStringLiteral("http://localhost:47823/anilist/callback"));
    QVERIFY(aniListConfigurationIsValid(localhost));
}

void TrackerAniListTest::configuration_rejects_bad_values()
{
    auto missingId = configuration();
    missingId.clientId.clear();
    QVERIFY(!aniListConfigurationIsValid(missingId));

    auto paddedId = configuration();
    paddedId.clientId = QStringLiteral(" 55437");
    QVERIFY(!aniListConfigurationIsValid(paddedId));

    auto remoteRedirect = configuration();
    remoteRedirect.redirectUri =
        QUrl(QStringLiteral("http://example.com:47823/anilist/callback"));
    QVERIFY(!aniListConfigurationIsValid(remoteRedirect));

    auto httpsRedirect = configuration();
    httpsRedirect.redirectUri =
        QUrl(QStringLiteral("https://127.0.0.1:47823/anilist/callback"));
    QVERIFY(!aniListConfigurationIsValid(httpsRedirect));

    auto noPort = configuration();
    noPort.redirectUri = QUrl(QStringLiteral("http://127.0.0.1/anilist/callback"));
    QVERIFY(!aniListConfigurationIsValid(noPort));

    auto otherGraphql = configuration();
    otherGraphql.graphqlEndpoint =
        QUrl(QStringLiteral("https://graphql.example.com"));
    QVERIFY(!aniListConfigurationIsValid(otherGraphql));

    auto insecureGraphql = configuration();
    insecureGraphql.graphqlEndpoint =
        QUrl(QStringLiteral("http://graphql.anilist.co"));
    QVERIFY(!aniListConfigurationIsValid(insecureGraphql));

    auto otherAuth = configuration();
    otherAuth.authorizationEndpoint =
        QUrl(QStringLiteral("https://evil.example/api/v2/oauth/authorize"));
    QVERIFY(!aniListConfigurationIsValid(otherAuth));
}

void TrackerAniListTest::remote_identity_round_trip()
{
    AniListRemoteIdentity anime{AniListMediaKind::Anime,
                                QStringLiteral("21"), 5, false};
    QCOMPARE(anime.remoteMediaId(), QStringLiteral("anilist:anime:21:episode:5"));
    const auto parsedAnime = aniListRemoteIdentity(anime.remoteMediaId());
    QVERIFY(parsedAnime);
    QCOMPARE(parsedAnime->unitNumber, 5);
    QCOMPARE(parsedAnime->kind, AniListMediaKind::Anime);

    AniListRemoteIdentity manga{AniListMediaKind::Manga,
                                QStringLiteral("30002"), 77, false};
    QCOMPARE(manga.remoteMediaId(),
             QStringLiteral("anilist:manga:30002:chapter:77"));
    QVERIFY(aniListRemoteIdentity(manga.remoteMediaId()));

    AniListRemoteIdentity list{AniListMediaKind::Manga,
                               QStringLiteral("30002"), 0, true};
    QCOMPARE(list.remoteMediaId(), QStringLiteral("anilist:manga:30002:list"));
    const auto parsedList = aniListRemoteIdentity(list.remoteMediaId());
    QVERIFY(parsedList && parsedList->listAggregate);

    // No cross-kind units, no zero ids, no volumes.
    QVERIFY(!aniListRemoteIdentity(QStringLiteral("anilist:anime:21:chapter:5")));
    QVERIFY(!aniListRemoteIdentity(QStringLiteral("anilist:manga:21:episode:5")));
    QVERIFY(!aniListRemoteIdentity(QStringLiteral("anilist:manga:21:volume:5")));
    QVERIFY(!aniListRemoteIdentity(QStringLiteral("anilist:anime:0:episode:5")));
    QVERIFY(!aniListRemoteIdentity(QStringLiteral("anilist:anime:21:episode:0")));
    QVERIFY(!aniListRemoteIdentity(QStringLiteral("mal:anime:21:episode:5")));
}

void TrackerAniListTest::status_mapping()
{
    for (const QString &status :
         {QStringLiteral("CURRENT"), QStringLiteral("PLANNING"),
          QStringLiteral("COMPLETED"), QStringLiteral("DROPPED"),
          QStringLiteral("PAUSED"), QStringLiteral("REPEATING")}) {
        QVERIFY2(aniListStatusIsValid(status), qPrintable(status));
    }
    QVERIFY(!aniListStatusIsValid(QStringLiteral("watching")));
    QVERIFY(!aniListStatusIsValid(QStringLiteral("current")));
    QVERIFY(!aniListStatusIsValid(QString()));
    QCOMPARE(aniListSafeStatusLabel(QStringLiteral("PLANNING")),
             QStringLiteral("Planning"));
    QCOMPARE(aniListSafeStatusLabel(QStringLiteral("bogus")),
             QStringLiteral("Unknown"));
    QCOMPARE(aniListMediaTypeName(AniListMediaKind::Anime), QStringLiteral("ANIME"));
    QCOMPARE(aniListMediaTypeName(AniListMediaKind::Manga), QStringLiteral("MANGA"));
}

void TrackerAniListTest::viewer_request_and_parse()
{
    const QJsonObject request = aniListViewerRequest();
    const QString query = request.value(QStringLiteral("query")).toString();
    QVERIFY(query.contains(QLatin1String("Viewer")));
    QVERIFY(query.contains(QLatin1String("id")));
    QVERIFY(query.contains(QLatin1String("name")));
    QVERIFY(request.value(QStringLiteral("variables")).isObject());

    const QJsonDocument good(QJsonObject{
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("Viewer"), QJsonObject{
                {QStringLiteral("id"), 5821},
                {QStringLiteral("name"), QStringLiteral("  Test  User ")}}}}}});
    const auto viewer = aniListParseViewer(good);
    QVERIFY(viewer);
    QCOMPARE(viewer->userId, QStringLiteral("5821"));
    QCOMPARE(viewer->userName, QStringLiteral("Test User"));

    // Missing id, string id, null viewer, non-object body.
    QVERIFY(!aniListParseViewer(QJsonDocument(QJsonObject{
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("Viewer"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("x")}}}}}})));
    QVERIFY(!aniListParseViewer(QJsonDocument(QJsonObject{
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("Viewer"), QJsonObject{
                {QStringLiteral("id"), QStringLiteral("5821")}}}}}})));
    QVERIFY(!aniListParseViewer(QJsonDocument(QJsonObject{
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("Viewer"), QJsonValue::Null}}}})));
    QVERIFY(!aniListParseViewer(QJsonDocument(QJsonArray{})));
    QVERIFY(!aniListParseViewer(QJsonDocument()));
}

void TrackerAniListTest::collection_request_shape()
{
    const auto anime = aniListMediaListCollectionRequest(
        QStringLiteral("5821"), AniListMediaKind::Anime, 2, 50);
    QVERIFY(anime);
    const QString query = anime->value(QStringLiteral("query")).toString();
    QVERIFY(query.contains(QLatin1String("MediaListCollection")));
    QVERIFY(query.contains(QLatin1String("idMal")));
    QVERIFY(query.contains(QLatin1String("episodes")));
    QVERIFY(query.contains(QLatin1String("chapters")));
    QVERIFY(query.contains(QLatin1String("hasNextChunk")));
    QVERIFY(!query.contains(QLatin1String("progressVolumes")));
    const QJsonObject vars = anime->value(QStringLiteral("variables")).toObject();
    QCOMPARE(vars.value(QStringLiteral("userId")).toInt(), 5821);
    QCOMPARE(vars.value(QStringLiteral("type")).toString(), QStringLiteral("ANIME"));
    QCOMPARE(vars.value(QStringLiteral("chunk")).toInt(), 2);
    QCOMPARE(vars.value(QStringLiteral("perChunk")).toInt(), 50);

    const auto manga = aniListMediaListCollectionRequest(
        QStringLiteral("5821"), AniListMediaKind::Manga);
    QVERIFY(manga);
    QCOMPARE(manga->value(QStringLiteral("variables")).toObject()
                 .value(QStringLiteral("type")).toString(),
             QStringLiteral("MANGA"));

    QVERIFY(!aniListMediaListCollectionRequest(
        QStringLiteral("0"), AniListMediaKind::Anime));
    QVERIFY(!aniListMediaListCollectionRequest(
        QStringLiteral("abc"), AniListMediaKind::Anime));
    QVERIFY(!aniListMediaListCollectionRequest(
        QStringLiteral("99999999999"), AniListMediaKind::Anime));
    QVERIFY(!aniListMediaListCollectionRequest(
        QStringLiteral("5"), AniListMediaKind::Anime, 0, 50));
    QVERIFY(!aniListMediaListCollectionRequest(
        QStringLiteral("5"), AniListMediaKind::Anime, 1, 101));
}

void TrackerAniListTest::collection_parse_anime_with_absent_idmal()
{
    const QJsonArray entries{
        entry(media(21, 21, QStringLiteral("One Piece"), 1155,
                    QJsonValue::Null),
              QStringLiteral("CURRENT"), 1130),
        // idMal explicitly null, and idMal key absent entirely.
        entry(media(9000, QJsonValue::Null, QStringLiteral("No MAL Show"),
                    12, QJsonValue::Null),
              QStringLiteral("PLANNING"), 0),
        entry(QJsonObject{
                  {QStringLiteral("id"), 9001},
                  {QStringLiteral("title"), QJsonObject{
                      {QStringLiteral("english"), QStringLiteral("English Only")}}}},
              QStringLiteral("COMPLETED"), 24, QJsonValue::Null)};
    const auto page = aniListParseMediaListCollection(
        collectionDocument(entries, true), AniListMediaKind::Anime);
    QVERIFY(page);
    QVERIFY(page->hasNextChunk);
    QCOMPARE(page->items.size(), 3);

    QCOMPARE(page->items.at(0).aniListId, QStringLiteral("21"));
    QCOMPARE(page->items.at(0).malId, QStringLiteral("21"));
    QCOMPARE(page->items.at(0).title, QStringLiteral("One Piece"));
    QCOMPARE(page->items.at(0).status, QStringLiteral("CURRENT"));
    QCOMPARE(page->items.at(0).progress, 1130);
    QCOMPARE(page->items.at(0).totalUnits, 1155);
    QCOMPARE(page->items.at(0).updatedAtMs, qint64(1790000000) * 1000);
    QCOMPARE(page->items.at(0).kind, AniListMediaKind::Anime);

    QCOMPARE(page->items.at(1).aniListId, QStringLiteral("9000"));
    QVERIFY(page->items.at(1).malId.isEmpty());
    QCOMPARE(page->items.at(1).totalUnits, 12);

    QVERIFY(page->items.at(2).malId.isEmpty());
    QCOMPARE(page->items.at(2).title, QStringLiteral("English Only"));
    QCOMPARE(page->items.at(2).totalUnits, 0);
    QCOMPARE(page->items.at(2).updatedAtMs, qint64(0));
}

void TrackerAniListTest::collection_parse_manga_uses_chapters()
{
    // A manga row carries both counts; only chapters may feed totalUnits.
    const QJsonArray entries{
        entry(media(30002, 2, QStringLiteral("Berserk"), 99, 381),
              QStringLiteral("CURRENT"), 377)};
    const auto page = aniListParseMediaListCollection(
        collectionDocument(entries), AniListMediaKind::Manga);
    QVERIFY(page);
    QCOMPARE(page->items.size(), 1);
    QCOMPARE(page->items.at(0).kind, AniListMediaKind::Manga);
    QCOMPARE(page->items.at(0).totalUnits, 381);
    QCOMPARE(page->items.at(0).progress, 377);

    // Unknown chapter total (ongoing series) is zero, not a failure.
    const auto ongoing = aniListParseMediaListCollection(
        collectionDocument(QJsonArray{entry(
            media(30003, QJsonValue::Null, QStringLiteral("Ongoing"),
                  QJsonValue::Null, QJsonValue::Null),
            QStringLiteral("CURRENT"), 10)}),
        AniListMediaKind::Manga);
    QVERIFY(ongoing);
    QCOMPARE(ongoing->items.at(0).totalUnits, 0);
}

void TrackerAniListTest::collection_parse_fails_closed()
{
    const QJsonObject goodMedia =
        media(21, 21, QStringLiteral("X"), 12, QJsonValue::Null);
    auto parse = [](const QJsonArray &entries) {
        return aniListParseMediaListCollection(
            collectionDocument(entries), AniListMediaKind::Anime);
    };
    QVERIFY(parse(QJsonArray{entry(goodMedia, QStringLiteral("CURRENT"), 1)}));

    // Wrong or unknown status.
    QVERIFY(!parse(QJsonArray{entry(goodMedia, QStringLiteral("WATCHING"), 1)}));
    QVERIFY(!parse(QJsonArray{entry(goodMedia, QString(), 1)}));
    // Progress missing, negative, fractional, or the wrong type.
    QVERIFY(!parse(QJsonArray{entry(goodMedia, QStringLiteral("CURRENT"),
                                    QJsonValue::Undefined)}));
    QVERIFY(!parse(QJsonArray{entry(goodMedia, QStringLiteral("CURRENT"), -1)}));
    QVERIFY(!parse(QJsonArray{entry(goodMedia, QStringLiteral("CURRENT"), 1.5)}));
    QVERIFY(!parse(QJsonArray{entry(goodMedia, QStringLiteral("CURRENT"),
                                    QStringLiteral("3"))}));
    // updatedAt wrong type.
    QVERIFY(!parse(QJsonArray{entry(goodMedia, QStringLiteral("CURRENT"), 1,
                                    QStringLiteral("yesterday"))}));
    // Media missing, null, or with bad ids/counts/title.
    QVERIFY(!parse(QJsonArray{entry(QJsonObject{}, QStringLiteral("CURRENT"), 1)}));
    QVERIFY(!parse(QJsonArray{QJsonObject{
        {QStringLiteral("status"), QStringLiteral("CURRENT")},
        {QStringLiteral("progress"), 1},
        {QStringLiteral("media"), QJsonValue::Null}}}));
    QVERIFY(!parse(QJsonArray{entry(
        media(0, 21, QStringLiteral("X"), 12, QJsonValue::Null),
        QStringLiteral("CURRENT"), 1)}));
    QVERIFY(!parse(QJsonArray{entry(
        media(21, QStringLiteral("21"), QStringLiteral("X"), 12, QJsonValue::Null),
        QStringLiteral("CURRENT"), 1)}));
    QVERIFY(!parse(QJsonArray{entry(
        media(21, 21, QStringLiteral("X"), QStringLiteral("12"), QJsonValue::Null),
        QStringLiteral("CURRENT"), 1)}));
    QVERIFY(!parse(QJsonArray{entry(
        media(21, 21, QStringLiteral("X"), -3, QJsonValue::Null),
        QStringLiteral("CURRENT"), 1)}));
    // One bad entry poisons the page rather than being silently skipped.
    QVERIFY(!parse(QJsonArray{
        entry(goodMedia, QStringLiteral("CURRENT"), 1),
        entry(goodMedia, QStringLiteral("BOGUS"), 1)}));

    // Structural failures.
    QVERIFY(!aniListParseMediaListCollection(
        QJsonDocument(QJsonObject{}), AniListMediaKind::Anime));
    QVERIFY(!aniListParseMediaListCollection(
        QJsonDocument(QJsonObject{{QStringLiteral("data"), QJsonObject{}}}),
        AniListMediaKind::Anime));
    QVERIFY(!aniListParseMediaListCollection(
        QJsonDocument(QJsonObject{
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("MediaListCollection"), QJsonObject{
                    {QStringLiteral("lists"), QJsonArray{}}}}}}}),
        AniListMediaKind::Anime)); // hasNextChunk missing
    QVERIFY(!aniListParseMediaListCollection(
        QJsonDocument(QJsonObject{
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("MediaListCollection"), QJsonObject{
                    {QStringLiteral("hasNextChunk"), false},
                    {QStringLiteral("lists"), QStringLiteral("nope")}}}}}}),
        AniListMediaKind::Anime));
}

void TrackerAniListTest::collection_parse_bounds_list_size()
{
    const QJsonObject goodMedia =
        media(21, 21, QStringLiteral("X"), 12, QJsonValue::Null);
    QJsonArray atCap;
    for (int i = 0; i < 500; ++i)
        atCap.append(entry(goodMedia, QStringLiteral("CURRENT"), 1));
    const auto accepted = aniListParseMediaListCollection(
        collectionDocument(atCap), AniListMediaKind::Anime);
    QVERIFY(accepted);
    QCOMPARE(accepted->items.size(), 500);

    QJsonArray oversize = atCap;
    oversize.append(entry(goodMedia, QStringLiteral("CURRENT"), 1));
    QVERIFY(!aniListParseMediaListCollection(
        collectionDocument(oversize), AniListMediaKind::Anime));

    QJsonArray manyLists;
    for (int i = 0; i < 65; ++i)
        manyLists.append(QJsonObject{{QStringLiteral("entries"), QJsonArray{}}});
    QVERIFY(!aniListParseMediaListCollection(
        QJsonDocument(QJsonObject{
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("MediaListCollection"), QJsonObject{
                    {QStringLiteral("hasNextChunk"), false},
                    {QStringLiteral("lists"), manyLists}}}}}}),
        AniListMediaKind::Anime));

    // Titles are clipped, not trusted.
    const auto clipped = aniListParseMediaListCollection(
        collectionDocument(QJsonArray{entry(
            media(21, 21, QString(2000, QLatin1Char('a')), 12, QJsonValue::Null),
            QStringLiteral("CURRENT"), 1)}),
        AniListMediaKind::Anime);
    QVERIFY(clipped);
    QCOMPARE(clipped->items.at(0).title.size(), 500);
}

void TrackerAniListTest::media_lookup_request_and_parse()
{
    const auto request = aniListMediaByMalIdRequest(
        QStringLiteral("21"), AniListMediaKind::Anime);
    QVERIFY(request);
    const QString query = request->value(QStringLiteral("query")).toString();
    QVERIFY(query.contains(QLatin1String("Media(idMal")));
    const QJsonObject vars = request->value(QStringLiteral("variables")).toObject();
    QCOMPARE(vars.value(QStringLiteral("idMal")).toInt(), 21);
    QCOMPARE(vars.value(QStringLiteral("type")).toString(), QStringLiteral("ANIME"));
    QVERIFY(!aniListMediaByMalIdRequest(QStringLiteral("-4"), AniListMediaKind::Anime));
    QVERIFY(!aniListMediaByMalIdRequest(QStringLiteral(""), AniListMediaKind::Manga));

    const auto ref = aniListParseMediaLookup(
        QJsonDocument(QJsonObject{
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("Media"),
                 media(21, 21, QStringLiteral("One Piece"), 1155,
                       QJsonValue::Null)}}}}),
        AniListMediaKind::Anime);
    QVERIFY(ref);
    QCOMPARE(ref->aniListId, QStringLiteral("21"));
    QCOMPARE(ref->malId, QStringLiteral("21"));
    QCOMPARE(ref->totalUnits, 1155);

    const auto manga = aniListParseMediaLookup(
        QJsonDocument(QJsonObject{
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("Media"),
                 media(30002, 2, QStringLiteral("Berserk"), QJsonValue::Null,
                       381)}}}}),
        AniListMediaKind::Manga);
    QVERIFY(manga);
    QCOMPARE(manga->totalUnits, 381);

    // Not found is a GraphQL 404 error with data null: distinguishable, and
    // still fail-closed for the parser.
    const QJsonDocument notFound = errorDocument(404, QStringLiteral("Not Found."));
    QVERIFY(!aniListParseMediaLookup(notFound, AniListMediaKind::Anime));
    QVERIFY(aniListResponseIsNotFound(notFound));
    QVERIFY(!aniListResponseIsNotFound(errorDocument(500, QStringLiteral("x"))));
    QVERIFY(!aniListResponseIsNotFound(QJsonDocument()));

    // Missing id / null media.
    QVERIFY(!aniListParseMediaLookup(
        QJsonDocument(QJsonObject{
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("Media"), QJsonValue::Null}}}}),
        AniListMediaKind::Anime));
    QVERIFY(!aniListParseMediaLookup(
        QJsonDocument(QJsonObject{
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("Media"), QJsonObject{
                    {QStringLiteral("title"), QJsonObject{}}}}}}}),
        AniListMediaKind::Anime));
}

void TrackerAniListTest::save_entry_request_shape()
{
    const auto withStatus = aniListSaveEntryRequest(
        QStringLiteral("21"), 12, QStringLiteral("CURRENT"));
    QVERIFY(withStatus);
    const QString query = withStatus->value(QStringLiteral("query")).toString();
    QVERIFY(query.startsWith(QLatin1String("mutation")));
    QVERIFY(query.contains(QLatin1String("SaveMediaListEntry")));
    QVERIFY(!query.contains(QLatin1String("progressVolumes")));
    const QJsonObject vars =
        withStatus->value(QStringLiteral("variables")).toObject();
    QCOMPARE(vars.value(QStringLiteral("mediaId")).toInt(), 21);
    QCOMPARE(vars.value(QStringLiteral("progress")).toInt(), 12);
    QCOMPARE(vars.value(QStringLiteral("status")).toString(),
             QStringLiteral("CURRENT"));

    const auto progressOnly = aniListSaveEntryRequest(
        QStringLiteral("21"), 13, QString());
    QVERIFY(progressOnly);
    QVERIFY(!progressOnly->value(QStringLiteral("variables")).toObject()
                 .contains(QStringLiteral("status")));

    QVERIFY(!aniListSaveEntryRequest(QStringLiteral("21"), -1, QString()));
    QVERIFY(!aniListSaveEntryRequest(QStringLiteral("0"), 1, QString()));
    QVERIFY(!aniListSaveEntryRequest(QStringLiteral("21"), 1,
                                     QStringLiteral("WATCHING")));
}

void TrackerAniListTest::save_and_readback_parse()
{
    const auto saved = aniListParseSaveEntry(
        entryStateDocument(QStringLiteral("SaveMediaListEntry"), stateObject()));
    QVERIFY(saved);
    QCOMPARE(saved->aniListId, QStringLiteral("21"));
    QCOMPARE(saved->status, QStringLiteral("CURRENT"));
    QCOMPARE(saved->progress, 12);
    QCOMPARE(saved->updatedAtMs, qint64(1790000000) * 1000);

    const auto readback = aniListParseMediaListEntry(
        entryStateDocument(QStringLiteral("MediaList"), stateObject()));
    QVERIFY(readback);
    QCOMPARE(readback->progress, 12);

    // Wrong field name, null entry, bad status, bad progress, missing mediaId.
    QVERIFY(!aniListParseSaveEntry(
        entryStateDocument(QStringLiteral("MediaList"), stateObject())));
    QVERIFY(!aniListParseMediaListEntry(QJsonDocument(QJsonObject{
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("MediaList"), QJsonValue::Null}}}})));
    auto badStatus = stateObject();
    badStatus.insert(QStringLiteral("status"), QStringLiteral("WATCHING"));
    QVERIFY(!aniListParseSaveEntry(
        entryStateDocument(QStringLiteral("SaveMediaListEntry"), badStatus)));
    auto badProgress = stateObject();
    badProgress.insert(QStringLiteral("progress"), QStringLiteral("12"));
    QVERIFY(!aniListParseSaveEntry(
        entryStateDocument(QStringLiteral("SaveMediaListEntry"), badProgress)));
    auto noMedia = stateObject();
    noMedia.remove(QStringLiteral("mediaId"));
    QVERIFY(!aniListParseMediaListEntry(
        entryStateDocument(QStringLiteral("MediaList"), noMedia)));
    auto noProgress = stateObject();
    noProgress.remove(QStringLiteral("progress"));
    QVERIFY(!aniListParseMediaListEntry(
        entryStateDocument(QStringLiteral("MediaList"), noProgress)));
}

void TrackerAniListTest::entry_request_shape()
{
    const auto request = aniListMediaListEntryRequest(
        QStringLiteral("5821"), QStringLiteral("21"));
    QVERIFY(request);
    QVERIFY(request->value(QStringLiteral("query")).toString()
                .contains(QLatin1String("MediaList(")));
    const QJsonObject vars = request->value(QStringLiteral("variables")).toObject();
    QCOMPARE(vars.value(QStringLiteral("userId")).toInt(), 5821);
    QCOMPARE(vars.value(QStringLiteral("mediaId")).toInt(), 21);
    QVERIFY(!aniListMediaListEntryRequest(QStringLiteral("0"), QStringLiteral("21")));
    QVERIFY(!aniListMediaListEntryRequest(QStringLiteral("5821"), QStringLiteral("x")));
}

void TrackerAniListTest::graphql_errors_fail_closed_everywhere()
{
    // A populated `errors` array fails every parser even when partial `data`
    // is present alongside it.
    const QJsonArray errors{QJsonObject{
        {QStringLiteral("message"), QStringLiteral("boom")},
        {QStringLiteral("status"), 500}}};
    auto withErrors = [&](const QJsonObject &data) {
        return QJsonDocument(QJsonObject{
            {QStringLiteral("errors"), errors},
            {QStringLiteral("data"), data}});
    };
    QVERIFY(!aniListParseViewer(withErrors(QJsonObject{
        {QStringLiteral("Viewer"), QJsonObject{
            {QStringLiteral("id"), 1}}}})));
    QVERIFY(!aniListParseSaveEntry(withErrors(QJsonObject{
        {QStringLiteral("SaveMediaListEntry"), stateObject()}})));
    QVERIFY(!aniListParseMediaListEntry(withErrors(QJsonObject{
        {QStringLiteral("MediaList"), stateObject()}})));
    QVERIFY(!aniListParseMediaLookup(withErrors(QJsonObject{
        {QStringLiteral("Media"),
         media(21, 21, QStringLiteral("X"), 1, QJsonValue::Null)}}),
        AniListMediaKind::Anime));
    QVERIFY(!aniListParseMediaListCollection(withErrors(QJsonObject{
        {QStringLiteral("MediaListCollection"), QJsonObject{
            {QStringLiteral("hasNextChunk"), false},
            {QStringLiteral("lists"), QJsonArray{}}}}}),
        AniListMediaKind::Anime));

    // `errors` of the wrong type also fails closed.
    QVERIFY(!aniListParseViewer(QJsonDocument(QJsonObject{
        {QStringLiteral("errors"), QStringLiteral("nope")},
        {QStringLiteral("data"), QJsonObject{
            {QStringLiteral("Viewer"), QJsonObject{
                {QStringLiteral("id"), 1}}}}}})));
}

void TrackerAniListTest::classification_rate_limited_with_retry_after()
{
    const auto limited = aniListResultFromResponse(
        429, QNetworkReply::UnknownContentError,
        QByteArrayLiteral("{\"errors\":[{\"message\":\"Too Many Requests.\","
                          "\"status\":429}]}"),
        QByteArrayLiteral("37"));
    QCOMPARE(limited.error, AniListTransportError::RateLimited);
    QCOMPARE(limited.statusCode, 429);
    QCOMPARE(limited.retryAfterMs, qint64(37000));
    QVERIFY(!limited.succeeded());

    // Missing, non-numeric, zero, and absurd Retry-After never invent a wait.
    for (const QByteArray &header :
         {QByteArray(), QByteArrayLiteral("soon"), QByteArrayLiteral("0"),
          QByteArrayLiteral("-5"), QByteArrayLiteral("999999999")}) {
        const auto result = aniListResultFromResponse(
            429, QNetworkReply::NoError, QByteArray(), header);
        QCOMPARE(result.error, AniListTransportError::RateLimited);
        QCOMPARE(result.retryAfterMs, qint64(0));
    }
}

void TrackerAniListTest::classification_auth_server_network_and_redirect()
{
    QCOMPARE(aniListResultFromResponse(
                 401, QNetworkReply::AuthenticationRequiredError,
                 QByteArray(), QByteArray()).error,
             AniListTransportError::AuthenticationRequired);

    // AniList answers a bad bearer token with 400 + "Invalid token".
    const auto invalidToken = aniListResultFromResponse(
        400, QNetworkReply::ProtocolInvalidOperationError,
        QByteArrayLiteral("{\"errors\":[{\"message\":\"Invalid token\","
                          "\"status\":400}],\"data\":null}"),
        QByteArray());
    QCOMPARE(invalidToken.error, AniListTransportError::AuthenticationRequired);
    // A different 400 (bad query) is a protocol failure, not reauthorization.
    QCOMPARE(aniListResultFromResponse(
                 400, QNetworkReply::ProtocolInvalidOperationError,
                 QByteArrayLiteral("{\"errors\":[{\"message\":\"validation\","
                                   "\"status\":400}]}"),
                 QByteArray()).error,
             AniListTransportError::ProtocolFailure);
    // A 200 whose GraphQL error says 401 is still an auth failure.
    QCOMPARE(aniListResultFromResponse(
                 200, QNetworkReply::NoError,
                 QByteArrayLiteral("{\"errors\":[{\"message\":\"x\","
                                   "\"status\":401}],\"data\":null}"),
                 QByteArray()).error,
             AniListTransportError::AuthenticationRequired);

    QCOMPARE(aniListResultFromResponse(
                 502, QNetworkReply::InternalServerError,
                 QByteArray(), QByteArray()).error,
             AniListTransportError::ServerFailure);
    QCOMPARE(aniListResultFromResponse(
                 500, QNetworkReply::InternalServerError,
                 QByteArray(), QByteArray()).error,
             AniListTransportError::ServerFailure);

    const auto network = aniListResultFromResponse(
        0, QNetworkReply::ConnectionRefusedError, QByteArray(), QByteArray());
    QCOMPARE(network.error, AniListTransportError::NetworkFailure);
    QVERIFY(network.networkFailure);
    QCOMPARE(aniListResultFromResponse(
                 0, QNetworkReply::TimeoutError, QByteArray(), QByteArray())
                 .error,
             AniListTransportError::Timeout);
    QCOMPARE(aniListResultFromResponse(
                 0, QNetworkReply::OperationCanceledError, QByteArray(),
                 QByteArray()).error,
             AniListTransportError::Timeout);

    // Redirects are rejected, never followed.
    for (const int status : {301, 302, 307, 308}) {
        const auto redirect = aniListResultFromResponse(
            status, QNetworkReply::NoError, QByteArray(), QByteArray());
        QCOMPARE(redirect.error, AniListTransportError::RedirectRejected);
        QVERIFY(!redirect.succeeded());
    }
}

void TrackerAniListTest::classification_malformed_and_oversize()
{
    const auto ok = aniListResultFromResponse(
        200, QNetworkReply::NoError,
        QByteArrayLiteral("{\"data\":{\"Viewer\":{\"id\":1,\"name\":\"a\"}}}"),
        QByteArray());
    QCOMPARE(ok.error, AniListTransportError::None);
    QVERIFY(ok.succeeded());
    QVERIFY(ok.document.isObject());

    QCOMPARE(aniListResultFromResponse(
                 200, QNetworkReply::NoError, QByteArrayLiteral("<html>"),
                 QByteArray()).error,
             AniListTransportError::ProtocolFailure);
    QCOMPARE(aniListResultFromResponse(
                 200, QNetworkReply::NoError, QByteArray(), QByteArray())
                 .error,
             AniListTransportError::ProtocolFailure);
    QCOMPARE(aniListResultFromResponse(
                 200, QNetworkReply::NoError, QByteArrayLiteral("[1,2]"),
                 QByteArray()).error,
             AniListTransportError::ProtocolFailure);
    QCOMPARE(aniListResultFromResponse(
                 404, QNetworkReply::ContentNotFoundError,
                 QByteArray(), QByteArray()).error,
             AniListTransportError::ProtocolFailure);

    const auto big = aniListResultFromResponse(
        200, QNetworkReply::NoError,
        QByteArray(64 * 1024 + 1, 'x'), QByteArray());
    QCOMPARE(big.error, AniListTransportError::PayloadTooLarge);
    QVERIFY(big.payloadTooLarge);
    QVERIFY(!big.succeeded());
}

void TrackerAniListTest::client_sends_stored_token_without_refresh()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(temp.path());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    FakeVault vault;
    vault.saved = credentialFor(profile, now + 100LL * 24 * 60 * 60 * 1000);
    FakeTransport transport;
    transport.result.document = QJsonDocument(QJsonObject{
        {QStringLiteral("data"), QJsonObject{}}});

    AniListApiClient client(profile, configuration(), &vault, &transport);
    QVERIFY(client.available());
    bool completed = false;
    client.post(aniListViewerRequest(), [&](const AniListApiResult &result) {
        QVERIFY(result.succeeded());
        completed = true;
    });
    QVERIFY(completed);
    QCOMPARE(transport.postCalls, 1);
    QCOMPARE(transport.lastAccessToken, QByteArrayLiteral("year-long-token"));
    QVERIFY(transport.lastBody.contains(QStringLiteral("query")));

    // A credential with no recorded expiry is still sent (expiry unknown).
    vault.saved->accessTokenExpiresAtMs = 0;
    completed = false;
    client.post(aniListViewerRequest(), [&](const AniListApiResult &result) {
        QVERIFY(result.succeeded());
        completed = true;
    });
    QVERIFY(completed);
    QCOMPARE(transport.postCalls, 2);

    QVERIFY(!client.disconnectThenForget([] { return false; }));
    QVERIFY(client.disconnectThenForget([] { return true; }));
}

void TrackerAniListTest::client_unauthorized_requires_reauthorization_no_retry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(temp.path());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    FakeVault vault;
    vault.saved = credentialFor(profile, now + 100LL * 24 * 60 * 60 * 1000);
    FakeTransport transport;
    transport.result = AniListApiResult{
        AniListTransportError::AuthenticationRequired, 401,
        QJsonDocument(), 0, false, false};

    AniListApiClient client(profile, configuration(), &vault, &transport);
    bool completed = false;
    client.post(aniListViewerRequest(), [&](const AniListApiResult &result) {
        QCOMPARE(result.error, AniListTransportError::AuthenticationRequired);
        QCOMPARE(result.statusCode, 401);
        QVERIFY(!result.succeeded());
        completed = true;
    });
    QVERIFY(completed);
    // Exactly one attempt, and the credential is left alone for the caller.
    QCOMPARE(transport.postCalls, 1);
    QVERIFY(vault.saved.has_value());
    QCOMPARE(vault.saved->accessToken, QByteArrayLiteral("year-long-token"));

    // A rate limit passes through untouched, retry hint preserved, no retry.
    transport.postCalls = 0;
    transport.result = AniListApiResult{
        AniListTransportError::RateLimited, 429, QJsonDocument(), 45000,
        false, false};
    completed = false;
    client.post(aniListViewerRequest(), [&](const AniListApiResult &result) {
        QCOMPARE(result.error, AniListTransportError::RateLimited);
        QCOMPARE(result.retryAfterMs, qint64(45000));
        completed = true;
    });
    QVERIFY(completed);
    QCOMPARE(transport.postCalls, 1);
}

void TrackerAniListTest::client_missing_or_expired_credential_makes_no_request()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(temp.path());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    FakeVault vault;
    FakeTransport transport;
    AniListApiClient client(profile, configuration(), &vault, &transport);

    bool completed = false;
    client.post(aniListViewerRequest(), [&](const AniListApiResult &result) {
        QCOMPARE(result.error, AniListTransportError::AuthenticationRequired);
        completed = true;
    });
    QVERIFY(completed);

    vault.saved = credentialFor(profile, now - 1000);
    completed = false;
    client.post(aniListViewerRequest(), [&](const AniListApiResult &result) {
        QCOMPARE(result.error, AniListTransportError::AuthenticationRequired);
        completed = true;
    });
    QVERIFY(completed);

    // Invalid configuration: unavailable, no request.
    auto bad = configuration();
    bad.clientId.clear();
    AniListApiClient unconfigured(profile, bad, &vault, &transport);
    QVERIFY(!unconfigured.available());
    completed = false;
    unconfigured.post(aniListViewerRequest(),
                      [&](const AniListApiResult &result) {
        QVERIFY(!result.succeeded());
        completed = true;
    });
    QVERIFY(completed);
    QCOMPARE(transport.postCalls, 0);
}

void TrackerAniListTest::unconfigured_build_fails_closed()
{
#if defined(COLOSSEUM_ANILIST_CLIENT_ID) || defined(COLOSSEUM_ANILIST_REDIRECT_URI)
    QSKIP("This target was explicitly configured with AniList build credentials.");
#else
    QVERIFY(!aniListProductionConfiguration().has_value());
#endif
}

void TrackerAniListTest::loopback_serves_static_callback_page()
{
    AniListLoopbackServer server;
    const quint16 port = freePort();
    QVERIFY(port != 0);
    QVERIFY(server.start(redirectFor(port), kState));

    const HttpReply reply = exchange(port, getRequest(port, "/anilist/callback"));
    QCOMPARE(reply.status, 200);
    const QByteArray head = reply.raw.left(reply.raw.indexOf("\r\n\r\n")).toLower();
    QVERIFY(head.contains("content-security-policy: default-src 'none'"));
    QVERIFY(head.contains("script-src 'unsafe-inline'"));
    QVERIFY(head.contains("connect-src 'self'"));
    QVERIFY(head.contains("cache-control: no-store"));
    QVERIFY(head.contains("text/html"));
    QVERIFY(reply.body.contains("location.hash"));
    QVERIFY(reply.body.contains("history.replaceState"));
    QVERIFY(reply.body.contains("You can close this tab and return to Colosseum"));
    // No external resources: no absolute URLs, no src/href/link/img/iframe.
    QVERIFY(!reply.body.contains("http"));
    QVERIFY(!reply.body.contains("src="));
    QVERIFY(!reply.body.contains("href"));
    QVERIFY(!reply.body.contains("<link"));
    QVERIFY(!reply.body.contains("<img"));
    QVERIFY(!reply.body.contains("<iframe"));
    // The page itself never carries the state or any token.
    QVERIFY(!reply.body.contains(kState));
}

void TrackerAniListTest::loopback_binds_loopback_only()
{
    AniListLoopbackServer server;
    const quint16 port = freePort();
    QVERIFY(server.start(redirectFor(port), kState));
    QCOMPARE(server.listenAddress(), QHostAddress(QHostAddress::LocalHost));
    QCOMPARE(server.listenPort(), port);
    server.stop();
    QVERIFY(!server.active());

    const QStringList bad{
        QStringLiteral("http://localhost:%1/anilist/callback"),
        QStringLiteral("http://0.0.0.0:%1/anilist/callback"),
        QStringLiteral("https://127.0.0.1:%1/anilist/callback"),
        QStringLiteral("http://127.0.0.1:%1/anilist/callback?x=1"),
        QStringLiteral("http://127.0.0.1:%1/")};
    for (const QString &pattern : bad) {
        QString error;
        QVERIFY2(!server.start(QUrl(pattern.arg(port)), kState, &error),
                 qPrintable(pattern));
        QVERIFY(!error.isEmpty());
        QVERIFY(!server.active());
    }
    QString error;
    QVERIFY(!server.start(redirectFor(port), QByteArray("short"), &error));
}

void TrackerAniListTest::loopback_accepts_one_valid_token()
{
    AniListLoopbackServer server;
    const quint16 port = freePort();
    QVERIFY(server.start(redirectFor(port), kState));
    QSignalSpy tokens(&server, &AniListLoopbackServer::tokenReceived);
    QSignalSpy failures(&server, &AniListLoopbackServer::signInFailed);

    const HttpReply reply = exchange(port, postRequest(
        port, "/anilist/callback/token", fragmentFor(kState)));
    QCOMPARE(reply.status, 200);
    QTRY_COMPARE(tokens.count(), 1);
    QCOMPARE(tokens.first().at(0).toByteArray(), kToken);
    QCOMPARE(tokens.first().at(1).toLongLong(), 31536000LL);
    QCOMPARE(failures.count(), 0);
    QVERIFY(!server.active());
    // The token is never echoed back.
    QVERIFY(!reply.raw.contains(kToken));
}

void TrackerAniListTest::loopback_rejects_invalid_requests()
{
    AniListLoopbackServer server;
    const quint16 port = freePort();
    QVERIFY(server.start(redirectFor(port), kState));
    QSignalSpy tokens(&server, &AniListLoopbackServer::tokenReceived);
    QSignalSpy failures(&server, &AniListLoopbackServer::signInFailed);
    const QByteArray tokenPath = "/anilist/callback/token";
    const QByteArray portText = QByteArray::number(port);

    auto status = [&](const QByteArray &request) {
        const HttpReply reply = exchange(port, request);
        return reply.connected ? reply.status : -1;
    };

    // Bad, missing, and near-miss state.
    QCOMPARE(status(postRequest(port, tokenPath,
                                fragmentFor("state-WRONG-0123456789"))), 400);
    QCOMPARE(status(postRequest(port, tokenPath, fragmentFor({}))), 400);
    QCOMPARE(status(postRequest(port, tokenPath,
                                fragmentFor(kState.left(kState.size() - 1)))),
             400);
    // Wrong token_type, bad expiry, bad token characters, empty token.
    QCOMPARE(status(postRequest(port, tokenPath,
                                fragmentFor(kState, kToken, "mac"))), 400);
    QCOMPARE(status(postRequest(port, tokenPath,
                                fragmentFor(kState, kToken, "Bearer", "0"))),
             400);
    QCOMPARE(status(postRequest(port, tokenPath,
                                fragmentFor(kState, kToken, "Bearer", "abc"))),
             400);
    QCOMPARE(status(postRequest(port, tokenPath,
                                fragmentFor(kState, "bad%20token%3C%3E"))),
             400);
    QCOMPARE(status(postRequest(port, tokenPath,
                                fragmentFor(kState, QByteArray()))), 400);
    // Oversized body (declared) and oversized declared length with no body.
    QCOMPARE(status(postRequest(port, tokenPath,
                                fragmentFor(kState,
                                            QByteArray(9000, 'a')))), 413);
    QCOMPARE(status("POST " + tokenPath + " HTTP/1.1\r\nHost: 127.0.0.1:"
                    + portText + "\r\nContent-Length: 20000\r\n\r\n"), 413);
    // Wrong path, wrong method.
    QCOMPARE(status(postRequest(port, "/anilist/other", fragmentFor(kState))),
             404);
    QCOMPARE(status(getRequest(port, "/")), 404);
    QCOMPARE(status(postRequest(port, "/anilist/callback",
                                fragmentFor(kState))), 405);
    QCOMPARE(status(postRequest(port, tokenPath, fragmentFor(kState), {},
                                "PUT")), 405);
    QCOMPARE(status(getRequest(port, tokenPath)), 405);
    // Foreign Origin, missing Origin, foreign Host, wrong content type.
    QCOMPARE(status(postRequest(port, tokenPath, fragmentFor(kState),
                                "http://evil.example")), 403);
    QCOMPARE(status(postRequest(port, tokenPath, fragmentFor(kState),
                                "http://127.0.0.1:1")), 403);
    QCOMPARE(status("POST " + tokenPath + " HTTP/1.1\r\nHost: 127.0.0.1:"
                    + portText
                    + "\r\nContent-Type: application/x-www-form-urlencoded"
                      "\r\nContent-Length: 0\r\n\r\n"), 403);
    QCOMPARE(status(postRequest(port, tokenPath, fragmentFor(kState), {},
                                "POST", "application/x-www-form-urlencoded",
                                "evil.example")), 403);
    QCOMPARE(status(postRequest(port, tokenPath, fragmentFor(kState), {},
                                "POST", "application/json")), 415);

    QCOMPARE(tokens.count(), 0);
    QCOMPARE(failures.count(), 0);
    QVERIFY(server.active());

    // After all of that, the legitimate token still goes through.
    QCOMPARE(status(postRequest(port, tokenPath, fragmentFor(kState))), 200);
    QTRY_COMPARE(tokens.count(), 1);
}

void TrackerAniListTest::loopback_rejects_second_token_after_success()
{
    AniListLoopbackServer server;
    const quint16 port = freePort();
    QVERIFY(server.start(redirectFor(port), kState));
    QSignalSpy tokens(&server, &AniListLoopbackServer::tokenReceived);
    QCOMPARE(exchange(port, postRequest(port, "/anilist/callback/token",
                                        fragmentFor(kState))).status, 200);
    const HttpReply second = exchange(port, postRequest(
        port, "/anilist/callback/token",
        fragmentFor(kState, "second.token.value")));
    // Closed listener: refused. A racing in-flight request would get 409.
    QVERIFY(!second.connected || second.status == 409);
    QTRY_COMPARE(tokens.count(), 1);
    const HttpReply page = exchange(port, getRequest(port, "/anilist/callback"));
    QVERIFY(!page.connected || page.status == 409);
}

void TrackerAniListTest::loopback_reports_provider_error_and_timeout()
{
    {
        AniListLoopbackServer server;
        const quint16 port = freePort();
        QVERIFY(server.start(redirectFor(port), kState));
        QSignalSpy tokens(&server, &AniListLoopbackServer::tokenReceived);
        QSignalSpy failures(&server, &AniListLoopbackServer::signInFailed);
        QCOMPARE(exchange(port, postRequest(
            port, "/anilist/callback/token",
            "error=access_denied&state=" + kState)).status, 200);
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(failures.first().at(0).toString(),
                 QStringLiteral("access_denied"));
        QCOMPARE(tokens.count(), 0);
        QVERIFY(!server.active());
    }
    {
        // An error without the right state must not end the sign-in.
        AniListLoopbackServer server;
        const quint16 port = freePort();
        QVERIFY(server.start(redirectFor(port), kState));
        QSignalSpy failures(&server, &AniListLoopbackServer::signInFailed);
        QCOMPARE(exchange(port, postRequest(
            port, "/anilist/callback/token",
            "error=access_denied")).status, 400);
        QCOMPARE(failures.count(), 0);
        QVERIFY(server.active());
    }
    {
        AniListLoopbackServer server;
        server.setLifetimeMs(50);
        const quint16 port = freePort();
        QVERIFY(server.start(redirectFor(port), kState));
        QSignalSpy failures(&server, &AniListLoopbackServer::signInFailed);
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(failures.first().at(0).toString(), QStringLiteral("timeout"));
        QVERIFY(!server.active());
    }
}

void TrackerAniListTest::controller_success_stores_credential()
{
    ControllerFixture f;
    QSignalSpy established(&f.controller,
                           &AniListConnectionController::connectionEstablished);
    QVERIFY(f.controller.available());
    QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
    QCOMPARE(f.controller.phase(), QStringLiteral("awaiting_approval"));
    QVERIFY(f.controller.busy());

    const QUrl url(f.controller.verificationUrl());
    QCOMPARE(url.host(), QStringLiteral("anilist.co"));
    QCOMPARE(url.path(), QStringLiteral("/api/v2/oauth/authorize"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("client_id")),
             QStringLiteral("55437"));
    QCOMPARE(query.queryItemValue(QStringLiteral("response_type")),
             QStringLiteral("token"));
    QCOMPARE(query.queryItemValue(QStringLiteral("state")),
             QString::fromLatin1(f.state()));
    QVERIFY(f.controller.openApprovalPage());
    QCOMPARE(f.opened.size(), 1);

    const qint64 before = QDateTime::currentMSecsSinceEpoch();
    QCOMPARE(f.deliverToken().status, 200);
    QTRY_COMPARE(f.controller.phase(), QStringLiteral("connected"));
    QCOMPARE(established.count(), 1);
    QCOMPARE(f.transport.postCalls, 1);
    QCOMPARE(f.transport.lastAccessToken, kToken);
    QVERIFY(f.transport.lastBody.value(QStringLiteral("query"))
                .toString().contains(QLatin1String("Viewer")));

    QVERIFY(f.vault.saved);
    QCOMPARE(f.vault.saved->slot.profileId, f.profile.profileId());
    QCOMPARE(f.vault.saved->slot.providerId, TrackerProviderId::AniList);
    QCOMPARE(f.vault.saved->slot.remoteAccountId, QStringLiteral("5821"));
    QCOMPARE(f.vault.saved->accessToken, kToken);
    QVERIFY(f.vault.saved->refreshToken.isEmpty());
    const qint64 expiry = f.vault.saved->accessTokenExpiresAtMs;
    QVERIFY(expiry >= before + 31536000LL * 1000);
    QVERIFY(expiry <= QDateTime::currentMSecsSinceEpoch() + 31536000LL * 1000);

    const auto connection = f.connections.connection(TrackerProviderId::AniList);
    QVERIFY(connection);
    QCOMPARE(connection->state, TrackerConnectionState::Connected);
    QCOMPARE(connection->remoteAccountId, QStringLiteral("5821"));
    QCOMPARE(connection->connectionGeneration, quint64(1));
    QVERIFY(!f.loopback.active());
    QVERIFY(!f.controller.reconnectRequired());
    // The token never reaches anything the UI can read.
    QVERIFY(!f.controller.statusMessage().contains(QString::fromLatin1(kToken)));
    QVERIFY(!f.controller.verificationUrl().contains(QString::fromLatin1(kToken)));
}

void TrackerAniListTest::controller_viewer_failure_stores_nothing()
{
    {
        ControllerFixture f;
        f.transport.result = AniListApiResult{
            AniListTransportError::AuthenticationRequired, 401, {}, 0, false,
            false};
        QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
        QCOMPARE(f.deliverToken().status, 200);
        QTRY_COMPARE(f.controller.phase(), QStringLiteral("attention"));
        QVERIFY(!f.vault.saved);
        QVERIFY(!f.connections.connection(TrackerProviderId::AniList));
        QVERIFY(!f.controller.busy());
    }
    {
        ControllerFixture f;
        f.transport.result = AniListApiResult{
            AniListTransportError::NetworkFailure, 0, {}, 0, true, false};
        QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
        f.deliverToken();
        QTRY_COMPARE(f.controller.phase(), QStringLiteral("attention"));
        QVERIFY(!f.vault.saved);
        QVERIFY(!f.connections.connection(TrackerProviderId::AniList));
    }
    {
        // 200 with an unusable body is also fail-closed.
        ControllerFixture f;
        f.transport.result.document = QJsonDocument(QJsonObject{});
        QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
        f.deliverToken();
        QTRY_COMPARE(f.controller.phase(), QStringLiteral("attention"));
        QVERIFY(!f.vault.saved);
    }
}

void TrackerAniListTest::controller_cancel_and_timeout()
{
    {
        ControllerFixture f;
        QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
        QVERIFY(f.loopback.active());
        f.controller.cancel();
        QCOMPARE(f.controller.phase(), QStringLiteral("cancelled"));
        QVERIFY(!f.loopback.active());
        QVERIFY(!f.vault.saved);
        // A late token after cancel has nowhere to go.
        const HttpReply late = f.deliverToken();
        QVERIFY(!late.connected);
        QCOMPARE(f.transport.postCalls, 0);
    }
    {
        ControllerFixture f;
        f.loopback.setLifetimeMs(50);
        QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
        QTRY_COMPARE(f.controller.phase(), QStringLiteral("attention"));
        QVERIFY(f.controller.statusMessage().contains(QLatin1String("timed out")));
        QVERIFY(!f.loopback.active());
        QVERIFY(!f.vault.saved);
        QVERIFY(!f.connections.connection(TrackerProviderId::AniList));
    }
}

void TrackerAniListTest::controller_disconnect_removes_credential()
{
    ControllerFixture f;
    QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
    f.deliverToken();
    QTRY_COMPARE(f.controller.phase(), QStringLiteral("connected"));
    QVERIFY(f.vault.saved);

    QVERIFY(f.controller.disconnect());
    QVERIFY(!f.vault.saved);
    const auto connection = f.connections.connection(TrackerProviderId::AniList);
    QVERIFY(connection);
    QCOMPARE(connection->state, TrackerConnectionState::Disconnected);
    QCOMPARE(f.controller.phase(), QStringLiteral("idle"));
}

void TrackerAniListTest::controller_unauthorized_requires_reconnect_and_keeps_record()
{
    ControllerFixture f;
    QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
    f.deliverToken();
    QTRY_COMPARE(f.controller.phase(), QStringLiteral("connected"));
    const quint64 generation =
        f.connections.connection(TrackerProviderId::AniList)->connectionGeneration;

    // A 401 from the API client: reconnect state, nothing deleted.
    f.controller.noteAuthenticationFailure();
    QCOMPARE(f.controller.phase(), QStringLiteral("reconnect_required"));
    QVERIFY(f.controller.reconnectRequired());
    QVERIFY(f.vault.saved);
    QCOMPARE(f.connections.connection(TrackerProviderId::AniList)->state,
             TrackerConnectionState::Connected);

    // Reconnect with the same account: new credential, same generation.
    f.vault.saved->accessToken = QByteArrayLiteral("revoked-token");
    QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
    QCOMPARE(f.controller.phase(), QStringLiteral("awaiting_approval"));
    f.deliverToken(QByteArrayLiteral("fresh.token.value"));
    QTRY_COMPARE(f.controller.phase(), QStringLiteral("connected"));
    QCOMPARE(f.vault.saved->accessToken, QByteArrayLiteral("fresh.token.value"));
    QVERIFY(!f.controller.reconnectRequired());
    QCOMPARE(f.connections.connection(TrackerProviderId::AniList)->connectionGeneration,
             generation);

    // An expired stored credential is a reconnect too, with no 401 needed.
    f.vault.saved->accessTokenExpiresAtMs = 1;
    QVERIFY(f.controller.reconnectRequired());
    // A different account on a still-connected profile is refused.
    f.transport.result.document = viewerDocument(777);
    QVERIFY(f.controller.beginConnection(QStringLiteral("anilist")));
    f.deliverToken(QByteArrayLiteral("other.account.token"));
    QTRY_COMPARE(f.controller.phase(), QStringLiteral("attention"));
    QCOMPARE(f.connections.connection(TrackerProviderId::AniList)->remoteAccountId,
             QStringLiteral("5821"));
}

void TrackerAniListTest::controller_unconfigured_is_unavailable()
{
    QTemporaryDir root;
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeVault vault;
    FakeTransport transport;
    AniListLoopbackServer loopback;
    TrackerConnectionStore connections(profile);
    AniListConnectionController controller(
        profile, &connections, std::nullopt, &vault, &transport, &loopback,
        [](const QUrl &) { return true; }, {});
    QVERIFY(!controller.available());
    QVERIFY(!controller.beginConnection(QStringLiteral("anilist")));
    QVERIFY(!loopback.active());
    QCOMPARE(controller.phase(), QStringLiteral("attention"));
    // Wrong provider key is refused too.
    ControllerFixture f;
    QVERIFY(!f.controller.beginConnection(QStringLiteral("mal")));
    QVERIFY(!f.loopback.active());
}

QTEST_GUILESS_MAIN(TrackerAniListTest)
#include "tst_tracker_anilist.moc"
