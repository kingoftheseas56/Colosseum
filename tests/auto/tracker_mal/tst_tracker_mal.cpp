#include <QtTest>

#include "account/ProfilePaths.h"
#include "trackers/MalApiClient.h"
#include "trackers/MalListStateStore.h"
#include "trackers/MalProtocol.h"
#include "trackers/TrackerCredentialVault.h"

#include <QDateTime>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <optional>

namespace {

MalAuthConfiguration configuration()
{
    MalAuthConfiguration value;
    value.clientId = QStringLiteral("0123456789abcdef0123456789abcdef");
    value.authorizationEndpoint =
        QUrl(QStringLiteral("https://myanimelist.net/v1/oauth2/authorize"));
    value.tokenEndpoint =
        QUrl(QStringLiteral("https://myanimelist.net/v1/oauth2/token"));
    value.apiEndpoint =
        QUrl(QStringLiteral("https://api.myanimelist.net/v2"));
    value.redirectUri =
        QUrl(QStringLiteral("http://127.0.0.1:38191/oauth/mal/callback"));
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
        return current
            && current->slot.remoteAccountId == slot.remoteAccountId
            && current->accessTokenExpiresAtMs > nowMs;
    }

    bool saveAndVerify(const TrackerCredential &credential) override
    {
        saved = credential;
        ++saveCount;
        return saveSucceeds;
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

    bool clearForProfile(const QString &profileId,
                         TrackerProviderId providerId) override
    {
        if (saved && saved->slot.profileId == profileId
            && saved->slot.providerId == providerId) {
            saved.reset();
        }
        ++clearCount;
        return true;
    }

    mutable std::optional<TrackerCredential> saved;
    bool saveSucceeds = true;
    int saveCount = 0;
    int clearCount = 0;
};

class FakeTransport final : public MalHttpTransport
{
public:
    using MalHttpTransport::MalHttpTransport;

    void refreshToken(const MalAuthConfiguration &,
                      const QByteArray &refreshToken,
                      TokenCompletion completion) override
    {
        ++refreshCalls;
        lastRefreshToken = refreshToken;
        completion(refreshResult);
    }

    void get(const MalAuthConfiguration &,
             const QByteArray &accessToken,
             const QString &path,
             const QUrlQuery &,
             ApiCompletion completion) override
    {
        ++getCalls;
        lastAccessToken = accessToken;
        lastPath = path;
        if (firstGetUnauthorized && getCalls == 1) {
            MalApiResult unauthorized;
            unauthorized.error =
                MalTransportError::AuthenticationRequired;
            unauthorized.statusCode = 401;
            completion(unauthorized);
            return;
        }
        completion(getResult);
    }

    void patchForm(const MalAuthConfiguration &,
                   const QByteArray &accessToken,
                   const QString &path,
                   const QUrlQuery &form,
                   ApiCompletion completion) override
    {
        ++patchCalls;
        lastAccessToken = accessToken;
        lastPath = path;
        lastForm = form;
        completion(patchResult);
    }

    MalTokenResult refreshResult{
        MalTransportError::None,
        QByteArrayLiteral("new-access"),
        QByteArrayLiteral("new-refresh"),
        60 * 60 * 1000,
        200};
    MalApiResult getResult{
        MalTransportError::None, 200, QJsonDocument(QJsonObject{}),
        0, false, false};
    MalApiResult patchResult{
        MalTransportError::None, 200, QJsonDocument(QJsonObject{}),
        0, false, false};
    bool firstGetUnauthorized = false;
    int refreshCalls = 0;
    int getCalls = 0;
    int patchCalls = 0;
    QByteArray lastRefreshToken;
    QByteArray lastAccessToken;
    QString lastPath;
    QUrlQuery lastForm;
};

TrackerCredential credentialFor(const ProfilePaths &profile,
                                qint64 accessExpiry,
                                qint64 refreshExpiry)
{
    TrackerCredential value;
    value.slot = {profile.profileId(),
                  TrackerProviderId::Mal,
                  QStringLiteral("99")};
    value.accessToken = QByteArrayLiteral("old-access");
    value.refreshToken = QByteArrayLiteral("old-refresh");
    value.accessTokenExpiresAtMs = accessExpiry;
    value.refreshTokenExpiresAtMs = refreshExpiry;
    value.grantedScopes = {QStringLiteral("mal:api")};
    return value;
}

QJsonObject animeRow()
{
    return QJsonObject{
        {QStringLiteral("node"), QJsonObject{
            {QStringLiteral("id"), 21},
            {QStringLiteral("title"), QStringLiteral("One Piece")},
            {QStringLiteral("num_episodes"), 1155}}},
        {QStringLiteral("list_status"), QJsonObject{
            {QStringLiteral("status"), QStringLiteral("watching")},
            {QStringLiteral("num_episodes_watched"), 1130},
            {QStringLiteral("updated_at"),
             QStringLiteral("2026-09-30T10:30:00+00:00")}}}};
}

QJsonObject mangaRow()
{
    return QJsonObject{
        {QStringLiteral("node"), QJsonObject{
            {QStringLiteral("id"), 2},
            {QStringLiteral("title"), QStringLiteral("Berserk")},
            {QStringLiteral("num_chapters"), 381}}},
        {QStringLiteral("list_status"), QJsonObject{
            {QStringLiteral("status"), QStringLiteral("reading")},
            {QStringLiteral("num_chapters_read"), 377},
            {QStringLiteral("updated_at"),
             QStringLiteral("2026-09-30T10:31:00+00:00")}}}};
}

} // namespace

class TrackerMalTest final : public QObject
{
    Q_OBJECT

private slots:
    void pkce_and_oauth_state();
    void token_refresh_rotation();
    void unauthorized_retries_once_after_refresh();
    void profile_isolation_and_list_import();
    void list_parsing_uses_official_field_names();
    void episode_and_chapter_progress_updates();
    void completion_updates();
    void id_mismatches_fail_closed();
    void fractional_continue_is_not_mal_count();
    void api_errors_are_not_reported_as_success();
    void disconnect_is_local_only();
    void unconfigured_build_fails_closed();
};

void TrackerMalTest::pkce_and_oauth_state()
{
    QByteArray verifierEntropy(48, '\0');
    QByteArray stateEntropy(32, '\0');
    for (int i = 0; i < verifierEntropy.size(); ++i)
        verifierEntropy[i] = static_cast<char>(i + 1);
    for (int i = 0; i < stateEntropy.size(); ++i)
        stateEntropy[i] = static_cast<char>(100 + i);

    const QByteArray verifier =
        malPkceVerifierFromEntropy(verifierEntropy);
    const QByteArray state =
        malStateFromEntropy(stateEntropy);
    QVERIFY(verifier.size() >= 43);
    QVERIFY(verifier.size() <= 128);
    QVERIFY(state.size() >= 32);
    QVERIFY(malConstantTimeEqual(state, state));
    QVERIFY(!malConstantTimeEqual(
        state, QByteArray(state.size(), 'x')));

    const QUrl url =
        malAuthorizationUrl(configuration(), state, verifier);
    QVERIFY(url.isValid());
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("client_id")),
             configuration().clientId);
    QCOMPARE(query.queryItemValue(QStringLiteral("state")),
             QString::fromLatin1(state));
    QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge")),
             QString::fromLatin1(verifier));
    QCOMPARE(query.queryItemValue(
                 QStringLiteral("code_challenge_method")),
             QStringLiteral("plain"));
}

void TrackerMalTest::token_refresh_rotation()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile =
        ProfilePaths::localOnly(temp.path());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    FakeVault vault;
    vault.saved = credentialFor(
        profile, now - 1000, now + 10 * 24 * 60 * 60 * 1000LL);
    FakeTransport transport;

    MalApiClient client(
        profile, configuration(), &vault, &transport);
    bool completed = false;
    client.get(QStringLiteral("/users/@me"), {},
        [&](const MalApiResult &result) {
            QVERIFY(result.succeeded());
            completed = true;
        });

    QVERIFY(completed);
    QCOMPARE(transport.refreshCalls, 1);
    QCOMPARE(transport.lastRefreshToken,
             QByteArrayLiteral("old-refresh"));
    QCOMPARE(transport.lastAccessToken,
             QByteArrayLiteral("new-access"));
    QVERIFY(vault.saved.has_value());
    QCOMPARE(vault.saved->refreshToken,
             QByteArrayLiteral("new-refresh"));
    QVERIFY(vault.saved->accessTokenExpiresAtMs > now);
    QVERIFY(vault.saved->refreshTokenExpiresAtMs
            > vault.saved->accessTokenExpiresAtMs);
}

void TrackerMalTest::unauthorized_retries_once_after_refresh()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile =
        ProfilePaths::localOnly(temp.path());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    FakeVault vault;
    vault.saved = credentialFor(
        profile, now + 60 * 60 * 1000,
        now + 20 * 24 * 60 * 60 * 1000LL);
    FakeTransport transport;
    transport.firstGetUnauthorized = true;

    MalApiClient client(
        profile, configuration(), &vault, &transport);
    bool completed = false;
    client.get(QStringLiteral("/users/@me"), {},
        [&](const MalApiResult &result) {
            QVERIFY(result.succeeded());
            completed = true;
        });

    QVERIFY(completed);
    QCOMPARE(transport.getCalls, 2);
    QCOMPARE(transport.refreshCalls, 1);
    QCOMPARE(transport.lastAccessToken,
             QByteArrayLiteral("new-access"));
}

void TrackerMalTest::profile_isolation_and_list_import()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto first = ProfilePaths::account(
        QStringLiteral("11111111-1111-1111-1111-111111111111"),
        temp.path());
    const auto second = ProfilePaths::account(
        QStringLiteral("22222222-2222-2222-2222-222222222222"),
        temp.path());
    QVERIFY(first.has_value());
    QVERIFY(second.has_value());

    MalListStateStore a(*first);
    MalListStateStore b(*second);
    QVERIFY(a.healthy());
    QVERIFY(b.healthy());
    QVERIFY(a.replaceAll(
        QStringLiteral("99"), QStringLiteral("snapshot-a"),
        QList<MalListItem>{
            *malListItemFromJson(animeRow(), MalMediaKind::Anime),
            *malListItemFromJson(mangaRow(), MalMediaKind::Manga)}));
    QVERIFY(b.replaceAll(
        QStringLiteral("100"), QStringLiteral("snapshot-b"),
        QList<MalListItem>{
            MalListItem{MalMediaKind::Anime, QStringLiteral("1"),
                        QStringLiteral("Cowboy Bebop"),
                        QStringLiteral("completed"), 26, 26, 1}}));

    QVERIFY(MalListStateStore::storagePath(*first)
            != MalListStateStore::storagePath(*second));
    MalListStateStore reloadedA(*first);
    MalListStateStore reloadedB(*second);
    QCOMPARE(reloadedA.remoteAccountId(), QStringLiteral("99"));
    QCOMPARE(reloadedB.remoteAccountId(), QStringLiteral("100"));
    QCOMPARE(reloadedA.items().size(), 2);
    QCOMPARE(reloadedB.items().size(), 1);
    QCOMPARE(reloadedA.item(
                 MalMediaKind::Manga,
                 QStringLiteral("2"))->progress, 377);
}

void TrackerMalTest::list_parsing_uses_official_field_names()
{
    const auto anime =
        malListItemFromJson(animeRow(), MalMediaKind::Anime);
    const auto manga =
        malListItemFromJson(mangaRow(), MalMediaKind::Manga);
    QVERIFY(anime.has_value());
    QVERIFY(manga.has_value());
    QCOMPARE(anime->progress, 1130);
    QCOMPARE(anime->totalUnits, 1155);
    QCOMPARE(manga->progress, 377);
    QCOMPARE(manga->totalUnits, 381);

    QJsonObject wrongAnime = animeRow();
    QJsonObject status =
        wrongAnime.value(QStringLiteral("list_status")).toObject();
    status.remove(QStringLiteral("num_episodes_watched"));
    status.insert(QStringLiteral("num_watched_episodes"), 1130);
    wrongAnime.insert(QStringLiteral("list_status"), status);
    QVERIFY(!malListItemFromJson(
        wrongAnime, MalMediaKind::Anime).has_value());
}

void TrackerMalTest::episode_and_chapter_progress_updates()
{
    TrackerDeliveryFact anime;
    anime.historyId = QStringLiteral("mal:21:12");
    anime.kind = TrackerDeliveryFactKind::Completion;
    anime.progress = 100;
    anime.mediaDomain = TrackerMediaDomain::Anime;
    const auto animeIdentity =
        malIdentityForCanonicalFact(anime);
    QVERIFY(animeIdentity.has_value());
    QCOMPARE(animeIdentity->unitNumber, 12);
    const auto animeMutation =
        malMutationForDelivery(*animeIdentity, anime, 24);
    QVERIFY(animeMutation.has_value());
    QCOMPARE(animeMutation->form.queryItemValue(
                 QStringLiteral("num_watched_episodes")),
             QStringLiteral("12"));
    QCOMPARE(animeMutation->form.queryItemValue(
                 QStringLiteral("status")),
             QStringLiteral("watching"));

    TrackerDeliveryFact manga;
    manga.historyId = QStringLiteral("mal:13:chapter:41");
    manga.kind = TrackerDeliveryFactKind::Completion;
    manga.progress = 100;
    manga.mediaDomain = TrackerMediaDomain::Manga;
    const auto mangaIdentity =
        malIdentityForCanonicalFact(manga);
    QVERIFY(mangaIdentity.has_value());
    const auto mangaMutation =
        malMutationForDelivery(*mangaIdentity, manga, 100);
    QVERIFY(mangaMutation.has_value());
    QCOMPARE(mangaMutation->form.queryItemValue(
                 QStringLiteral("num_chapters_read")),
             QStringLiteral("41"));
    QCOMPARE(mangaMutation->form.queryItemValue(
                 QStringLiteral("status")),
             QStringLiteral("reading"));
}

void TrackerMalTest::completion_updates()
{
    TrackerDeliveryFact anime;
    anime.historyId = QStringLiteral("mal:21:24");
    anime.kind = TrackerDeliveryFactKind::Completion;
    anime.progress = 100;
    anime.mediaDomain = TrackerMediaDomain::Anime;
    const auto identity =
        malIdentityForCanonicalFact(anime);
    QVERIFY(identity.has_value());
    const auto mutation =
        malMutationForDelivery(*identity, anime, 24);
    QVERIFY(mutation.has_value());
    QVERIFY(mutation->intendedCompleted);
    QCOMPARE(mutation->form.queryItemValue(
                 QStringLiteral("status")),
             QStringLiteral("completed"));

    TrackerDeliveryFact manga;
    manga.historyId = QStringLiteral("mal:13:chapter:42");
    manga.kind = TrackerDeliveryFactKind::Completion;
    manga.progress = 100;
    manga.mediaDomain = TrackerMediaDomain::Manga;
    const auto mangaIdentity =
        malIdentityForCanonicalFact(manga);
    QVERIFY(mangaIdentity.has_value());
    const auto mangaMutation =
        malMutationForDelivery(*mangaIdentity, manga, 42);
    QVERIFY(mangaMutation.has_value());
    QVERIFY(mangaMutation->intendedCompleted);
}

void TrackerMalTest::id_mismatches_fail_closed()
{
    TrackerDeliveryFact edition;
    edition.historyId = QStringLiteral("mal:13:color");
    edition.mediaDomain = TrackerMediaDomain::Manga;
    QVERIFY(!malIdentityForCanonicalFact(edition).has_value());

    TrackerDeliveryFact wrongDomain;
    wrongDomain.historyId = QStringLiteral("mal:21:12");
    wrongDomain.mediaDomain = TrackerMediaDomain::Television;
    QVERIFY(!malIdentityForCanonicalFact(wrongDomain).has_value());

    QVERIFY(!malRemoteIdentity(
        QStringLiteral("anime:not-a-number:episode:3")).has_value());
    QVERIFY(!malRemoteIdentity(
        QStringLiteral("manga:2:chapter:0")).has_value());
}

void TrackerMalTest::fractional_continue_is_not_mal_count()
{
    TrackerDeliveryFact partial;
    partial.historyId = QStringLiteral("mal:21:12");
    partial.kind = TrackerDeliveryFactKind::Progress;
    partial.progress = 55;
    partial.mediaDomain = TrackerMediaDomain::Anime;
    const auto identity =
        malIdentityForCanonicalFact(partial);
    QVERIFY(identity.has_value());
    QVERIFY(!malMutationForDelivery(
        *identity, partial, 24).has_value());

    MalRemoteIdentity aggregate{
        MalMediaKind::Anime, QStringLiteral("21"), 0, true};
    TrackerDeliveryFact completion = partial;
    completion.kind = TrackerDeliveryFactKind::Completion;
    completion.progress = 100;
    QVERIFY(!malMutationForDelivery(
        aggregate, completion, 24).has_value());
}

void TrackerMalTest::api_errors_are_not_reported_as_success()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile =
        ProfilePaths::localOnly(temp.path());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    FakeVault vault;
    vault.saved = credentialFor(
        profile, now + 60 * 60 * 1000,
        now + 20 * 24 * 60 * 60 * 1000LL);
    FakeTransport transport;
    transport.getResult.error =
        MalTransportError::ProtocolFailure;
    transport.getResult.statusCode = 500;

    MalApiClient client(
        profile, configuration(), &vault, &transport);
    bool completed = false;
    client.get(QStringLiteral("/users/@me"), {},
        [&](const MalApiResult &result) {
            QCOMPARE(result.statusCode, 500);
            QVERIFY(!result.succeeded());
            completed = true;
        });
    QVERIFY(completed);
    QCOMPARE(transport.refreshCalls, 0);
}

void TrackerMalTest::disconnect_is_local_only()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProfilePaths profile =
        ProfilePaths::localOnly(temp.path());
    FakeVault vault;
    FakeTransport transport;
    MalApiClient client(
        profile, configuration(), &vault, &transport);

    bool called = false;
    QVERIFY(client.disconnectThenForget([&called] {
        called = true;
        return true;
    }));
    QVERIFY(called);
    QCOMPARE(transport.refreshCalls, 0);
    QCOMPARE(transport.getCalls, 0);
    QCOMPARE(transport.patchCalls, 0);

    QVERIFY(!client.disconnectThenForget([] { return false; }));
}

void TrackerMalTest::unconfigured_build_fails_closed()
{
#if defined(COLOSSEUM_MAL_CLIENT_ID) || defined(COLOSSEUM_MAL_REDIRECT_URI)
    QSKIP("This target was explicitly configured with MAL build credentials.");
#else
    QVERIFY(!malProductionConfiguration().has_value());
#endif
}

QTEST_GUILESS_MAIN(TrackerMalTest)
#include "tst_tracker_mal.moc"
