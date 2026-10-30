#include <QtTest>

#include "account/ProfilePaths.h"
#include "trackers/AniListApiClient.h"
#include "trackers/AniListHttpTransport.h"
#include "trackers/AniListProtocol.h"
#include "trackers/TrackerCredentialVault.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

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

QTEST_GUILESS_MAIN(TrackerAniListTest)
#include "tst_tracker_anilist.moc"
