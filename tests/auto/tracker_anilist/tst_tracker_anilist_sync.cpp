#include <QtTest>

#include "ProgressStore.h"
#include "account/ActivityStore.h"
#include "account/HistoryStore.h"
#include "account/ProfilePaths.h"
#include "trackers/AniListApiClient.h"
#include "trackers/AniListConnectionController.h"
#include "trackers/AniListHttpTransport.h"
#include "trackers/AniListLoopbackServer.h"
#include "trackers/AniListProtocol.h"
#include "trackers/AniListSyncRuntime.h"
#include "trackers/TrackerCanonicalDeliverySource.h"
#include "trackers/TrackerProgressImportOwner.h"
#include "trackers/TrackerSyncSettingsStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>

#include <functional>
#include <optional>

namespace {

const QString kProfile = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString kViewer = QStringLiteral("99");

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
        const auto current = loadForProfile(slot.profileId, slot.providerId);
        return current && current->accessTokenExpiresAtMs > nowMs;
    }
    bool saveAndVerify(const TrackerCredential &credential) override
    {
        saved = credential;
        return true;
    }
    std::optional<TrackerCredential> loadForProfile(
        const QString &profileId, TrackerProviderId providerId) const override
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

AniListApiResult okResult(const QJsonObject &data)
{
    return {AniListTransportError::None, 200,
            QJsonDocument(QJsonObject{{QStringLiteral("data"), data}}), 0,
            false, false};
}

AniListApiResult notFoundResult()
{
    return {AniListTransportError::ProtocolFailure, 404,
            QJsonDocument(QJsonObject{
                {QStringLiteral("errors"), QJsonArray{QJsonObject{
                    {QStringLiteral("status"), 404},
                    {QStringLiteral("message"), QStringLiteral("Not Found.")}}}},
                {QStringLiteral("data"), QJsonValue::Null}}),
            0, false, false};
}

AniListApiResult lostAcknowledgement()
{
    return {AniListTransportError::Timeout, 0, {}, 0, true, false};
}

QJsonObject mediaObject(int id, int idMal, int episodes, int chapters)
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("idMal"), idMal},
        {QStringLiteral("title"), QJsonObject{
            {QStringLiteral("romaji"), QStringLiteral("Fixture %1").arg(id)},
            {QStringLiteral("english"), QJsonValue::Null}}},
        {QStringLiteral("episodes"), episodes > 0 ? QJsonValue(episodes)
                                                  : QJsonValue::Null},
        {QStringLiteral("chapters"), chapters > 0 ? QJsonValue(chapters)
                                                  : QJsonValue::Null}};
}

QJsonObject listEntry(const QJsonObject &media, const QString &status,
                      int progress)
{
    return QJsonObject{{QStringLiteral("status"), status},
                       {QStringLiteral("progress"), progress},
                       {QStringLiteral("updatedAt"), 1790000000},
                       {QStringLiteral("media"), media}};
}

QJsonObject collectionPage(const QJsonArray &entries, bool hasNext)
{
    return QJsonObject{{QStringLiteral("MediaListCollection"), QJsonObject{
        {QStringLiteral("hasNextChunk"), hasNext},
        {QStringLiteral("lists"), QJsonArray{QJsonObject{
            {QStringLiteral("entries"), entries}}}}}}};
}

QJsonObject entryData(const QString &field, int mediaId, const QString &status,
                      int progress)
{
    return QJsonObject{{field, QJsonObject{
        {QStringLiteral("id"), 1},
        {QStringLiteral("mediaId"), mediaId},
        {QStringLiteral("status"), status},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("updatedAt"), 1790000100}}}};
}

// Plays graphql.anilist.co. Every request is logged by kind; the save
// behaviour can be overridden per test.
class FakeTransport final : public AniListHttpTransport
{
public:
    using AniListHttpTransport::AniListHttpTransport;

    void post(const AniListAuthConfiguration &, const QByteArray &,
              const QJsonObject &body, ApiCompletion completion) override
    {
        const QString query = body.value(QStringLiteral("query")).toString();
        const QJsonObject variables =
            body.value(QStringLiteral("variables")).toObject();
        QString kind;
        AniListApiResult result;
        if (query.contains(QLatin1String("MediaListCollection"))) {
            kind = QStringLiteral("collection");
            const QString key = variables.value(QStringLiteral("type")).toString()
                + QLatin1Char(':')
                + QString::number(variables.value(QStringLiteral("chunk")).toInt());
            result = collections.contains(key)
                ? okResult(collections.value(key))
                : okResult(collectionPage({}, false));
        } else if (query.contains(QLatin1String("SaveMediaListEntry"))) {
            kind = QStringLiteral("save");
            const int mediaId = variables.value(QStringLiteral("mediaId")).toInt();
            const QString status = variables.contains(QStringLiteral("status"))
                ? variables.value(QStringLiteral("status")).toString()
                : QStringLiteral("CURRENT");
            const int progress = variables.value(QStringLiteral("progress")).toInt();
            if (saveHandler) {
                result = saveHandler(saves.size());
            } else {
                result = okResult(entryData(QStringLiteral("SaveMediaListEntry"),
                                            mediaId, status, progress));
            }
            // A "lost" acknowledgement may still have applied remotely.
            if (result.statusCode != 429 && result.statusCode != 401
                && (result.succeeded() || applyOnFailure)) {
                remote.insert(mediaId, qMakePair(status, progress));
            }
            saves.append(body);
        } else if (query.contains(QLatin1String("MediaList("))) {
            kind = QStringLiteral("readback");
            const int mediaId = variables.value(QStringLiteral("mediaId")).toInt();
            if (remote.contains(mediaId)) {
                result = okResult(entryData(QStringLiteral("MediaList"), mediaId,
                                            remote.value(mediaId).first,
                                            remote.value(mediaId).second));
            } else {
                result = notFoundResult();
            }
        } else if (query.contains(QLatin1String("idMal"))) {
            kind = QStringLiteral("lookup");
            const QString key = variables.value(QStringLiteral("type")).toString()
                + QLatin1Char(':')
                + QString::number(variables.value(QStringLiteral("idMal")).toInt());
            result = lookups.contains(key)
                ? okResult(QJsonObject{{QStringLiteral("Media"), lookups.value(key)}})
                : notFoundResult();
        }
        log.append(kind);
        bodies.append(body);
        completion(result);
    }

    int count(const QString &kind) const { return log.count(kind); }

    QHash<QString, QJsonObject> collections; // "TYPE:chunk" -> data
    QHash<QString, QJsonObject> lookups;     // "TYPE:idMal" -> Media
    QHash<int, QPair<QString, int>> remote;  // mediaId -> status, progress
    std::function<AniListApiResult(int)> saveHandler;
    bool applyOnFailure = false;
    QStringList log;
    QList<QJsonObject> bodies;
    QList<QJsonObject> saves;
};

struct Fixture {
    QTemporaryDir root;
    ProfilePaths profile = *ProfilePaths::account(kProfile, root.path());
    FakeVault vault;
    FakeTransport transport;
    AniListLoopbackServer loopback;
    TrackerConnectionStore connections{profile};
    TrackerMappingStore mappings{profile};
    TrackerImportStore imports{profile, &mappings, &connections};
    ProgressStore progress{profile.progressIniPath()};
    ActivityStore activity{profile.activityDbPath()};
    HistoryStore history{profile.historyIniPath()};
    TrackerProgressImportOwner importOwner{&progress};
    TrackerDeliveryStore delivery{profile, &mappings, &connections};
    TrackerCanonicalDeliverySource source{&progress, &activity, &history};
    TrackerSyncSettingsStore settings{profile};
    AniListApiClient api{profile, configuration(), &vault, &transport};
    AniListConnectionController controller{
        profile, &connections, configuration(), &vault, &transport,
        &loopback, [](const QUrl &) { return true; }, {}};
    AniListSyncRuntime sync{&connections, &mappings, &imports, &importOwner,
                            &delivery, &source, &settings, nullptr, &api,
                            &controller};
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    Fixture()
    {
        TrackerCredential credential;
        credential.slot = {profile.profileId(), TrackerProviderId::AniList,
                           kViewer};
        credential.accessToken = QByteArrayLiteral("fixture-token");
        credential.accessTokenExpiresAtMs = now + 3600LL * 1000 * 24 * 300;
        vault.saveAndVerify(credential);
        connections.upsert(
            {TrackerProviderId::AniList, kViewer, 1, now,
             TrackerProviderCapability::ReadHistory
                 | TrackerProviderCapability::ReadProgress
                 | TrackerProviderCapability::WriteProgress
                 | TrackerProviderCapability::WriteCompletion,
             TrackerConnectionState::Connected});
        settings.setGlobalSetting(QStringLiteral("checkOnLaunch"), false);
    }

    TrackerConnection connection() const
    {
        return *connections.connection(TrackerProviderId::AniList);
    }

    // A native progress fact; `progressFraction` 1.0 is a finished unit.
    TrackerDeliveryFact progressFact(const QString &kind, const QString &id,
                                     double progressFraction = 1.0)
    {
        progress.record({{QStringLiteral("kind"), kind},
                         {QStringLiteral("id"), id},
                         {QStringLiteral("title"), QStringLiteral("Fixture")},
                         {QStringLiteral("progress"), progressFraction},
                         {QStringLiteral("resume"), progressFraction * 100.0}});
        progress.flush();
        QCoreApplication::processEvents();
        const auto fact = source.currentProgressFact(kind, id);
        return fact ? *fact : TrackerDeliveryFact{};
    }

    TrackerDeliveryFact completionFact(const QString &kind, const QString &id)
    {
        activity.recordCompletion({
            {QStringLiteral("eventId"),
             QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("sessionId"), activity.newSessionId()},
            {QStringLiteral("world"), QStringLiteral("tankoban")},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("titleKey"), QStringLiteral("title:") + id},
            {QStringLiteral("itemKey"), id},
            {QStringLiteral("title"), QStringLiteral("Fixture")},
            {QStringLiteral("itemLabel"), QString()},
            {QStringLiteral("cover"), QString()},
            {QStringLiteral("utcOffsetMinutes"), 0},
            {QStringLiteral("syncable"), true},
            {QStringLiteral("source"), QStringLiteral("reader")},
            {QStringLiteral("atMs"), now - 1000},
            {QStringLiteral("reason"), QStringLiteral("eof")}});
        for (const auto &fact : source.currentCommittedFacts()) {
            if (fact.kind == TrackerDeliveryFactKind::Completion
                && fact.historyId == id) {
                return fact;
            }
        }
        return {};
    }

    std::optional<TrackerRemoteDeliverySnapshot> snapshot(
        const QList<TrackerDeliveryFact> &facts)
    {
        std::optional<TrackerRemoteDeliverySnapshot> result;
        sync.readExportSnapshotAsync(connection(), facts,
            [&result](auto value) { result = std::move(value); });
        return result;
    }

    bool consent(const TrackerDeliveryFact &fact)
    {
        const auto remote = snapshot({fact});
        if (!remote)
            return false;
        const auto preview = delivery.createExportPreview(
            TrackerProviderId::AniList, kViewer, 1, {fact}, *remote, &source);
        if (!preview || preview->items.size() != 1
            || !preview->items.first().eligible) {
            return false;
        }
        return delivery.confirmExport(
                   preview->previewId, {preview->items.first().itemId},
                   *remote, &source, now)
            && delivery.setProviderSendEnabled(
                   TrackerProviderId::AniList, kViewer, true);
    }

    void addAnime(int aniListId, int malId, int episodes)
    {
        transport.lookups.insert(
            QStringLiteral("ANIME:%1").arg(malId),
            mediaObject(aniListId, malId, episodes, 0));
    }
    void addManga(int aniListId, int malId, int chapters)
    {
        transport.lookups.insert(
            QStringLiteral("MANGA:%1").arg(malId),
            mediaObject(aniListId, malId, 0, chapters));
    }
    TrackerDeliveryOperation firstOperation() const
    {
        const auto operations = delivery.operations();
        return operations.isEmpty() ? TrackerDeliveryOperation{}
                                    : operations.first();
    }
};

QString saveStatus(const QJsonObject &body)
{
    return body.value(QStringLiteral("variables")).toObject()
        .value(QStringLiteral("status")).toString();
}
int saveProgress(const QJsonObject &body)
{
    return body.value(QStringLiteral("variables")).toObject()
        .value(QStringLiteral("progress")).toInt(-1);
}
int saveMediaId(const QJsonObject &body)
{
    return body.value(QStringLiteral("variables")).toObject()
        .value(QStringLiteral("mediaId")).toInt(-1);
}

} // namespace

class TrackerAniListSyncTest final : public QObject
{
    Q_OBJECT

private slots:
    void mapping_by_idMal_caches_unit_mapping();
    void mapping_missing_media_fails_closed();
    void mapping_without_mal_canonical_id_is_never_looked_up();
    void mapping_wrong_idMal_or_conflicting_join_fails_closed();
    void mapping_kind_mismatch_fails_closed();
    void tankoban_never_produces_a_request();
    void manga_and_manga_chapter_send_chapters_only();
    void anime_episode_progress_is_sent();
    void partial_episode_never_marks_completed();
    void completion_maps_to_completed();
    void import_pages_two_chunks_into_review_without_applying();
    void unknown_outcome_reads_back_before_retry_applied();
    void unknown_outcome_reads_back_before_retry_absent();
    void rate_limited_defers_without_loss();
    void unauthorized_requires_reconnect_and_keeps_queue();
};

void TrackerAniListSyncTest::mapping_by_idMal_caches_unit_mapping()
{
    Fixture f;
    f.addAnime(777, 1535, 24);
    const auto fact = f.progressFact(QStringLiteral("anime"),
                                     QStringLiteral("mal:1535:3"));
    QVERIFY(!fact.historyId.isEmpty());
    QVERIFY(f.consent(fact));
    const auto mapping = f.mappings.mapping(
        {TrackerProviderId::AniList, kViewer,
         QStringLiteral("anilist:anime:777:episode:3")});
    QVERIFY(mapping);
    QCOMPARE(mapping->canonical.historyId, QStringLiteral("mal:1535:3"));
    QCOMPARE(f.transport.count(QStringLiteral("lookup")), 1);
    // A second episode of the same title reuses the cached AniList id.
    const auto next = f.progressFact(QStringLiteral("anime"),
                                     QStringLiteral("mal:1535:4"));
    QVERIFY(f.snapshot({next}));
    QCOMPARE(f.transport.count(QStringLiteral("lookup")), 1);
    QVERIFY(f.mappings.mapping(
        {TrackerProviderId::AniList, kViewer,
         QStringLiteral("anilist:anime:777:episode:4")}));
}

void TrackerAniListSyncTest::mapping_missing_media_fails_closed()
{
    Fixture f; // no lookup entry: AniList answers 404
    const auto fact = f.progressFact(QStringLiteral("anime"),
                                     QStringLiteral("mal:4040:1"));
    const auto remote = f.snapshot({fact});
    QVERIFY(remote);
    QCOMPARE(remote->items.size(), 0);
    QVERIFY(f.mappings.mappings().isEmpty());
    QVERIFY(!f.consent(fact));
    QCOMPARE(f.transport.count(QStringLiteral("save")), 0);
}

void TrackerAniListSyncTest::mapping_without_mal_canonical_id_is_never_looked_up()
{
    Fixture f;
    const auto fact = f.progressFact(QStringLiteral("anime"),
                                     QStringLiteral("kitsu:77:2"));
    const auto remote = f.snapshot({fact});
    QVERIFY(remote);
    QCOMPARE(remote->items.size(), 0);
    f.sync.start();
    QTest::qWait(50);
    QVERIFY(f.mappings.mappings().isEmpty());
    QCOMPARE(f.transport.log.size(), 0);
}

void TrackerAniListSyncTest::mapping_wrong_idMal_or_conflicting_join_fails_closed()
{
    {
        // The record names a different MAL id than the one asked for.
        Fixture f;
        f.transport.lookups.insert(QStringLiteral("ANIME:1535"),
                                   mediaObject(777, 9999, 24, 0));
        const auto fact = f.progressFact(QStringLiteral("anime"),
                                         QStringLiteral("mal:1535:3"));
        QVERIFY(!f.consent(fact));
        QVERIFY(f.mappings.mappings().isEmpty());
    }
    {
        // The same title is already mapped to a different AniList media.
        Fixture f;
        f.addAnime(777, 1535, 24);
        QVERIFY(f.mappings.upsert(
            {TrackerProviderId::AniList, kViewer,
             QStringLiteral("anilist:anime:888:episode:2")},
            {QStringLiteral("anime:mal:1535:2"), QStringLiteral("anime"),
             QStringLiteral("mal:1535:2"), QStringLiteral("anime:mal:1535:2"), {}},
            TrackerMappingProvenance::ExactProviderIdentity));
        const auto fact = f.progressFact(QStringLiteral("anime"),
                                         QStringLiteral("mal:1535:3"));
        QVERIFY(!f.consent(fact));
        QVERIFY(!f.mappings.mapping(
            {TrackerProviderId::AniList, kViewer,
             QStringLiteral("anilist:anime:777:episode:3")}));
        QCOMPARE(f.transport.count(QStringLiteral("save")), 0);
    }
}

void TrackerAniListSyncTest::mapping_kind_mismatch_fails_closed()
{
    {
        // A MAL id that is a manga: the ANIME lookup finds nothing.
        Fixture f;
        f.addManga(555, 2200, 100);
        const auto fact = f.progressFact(QStringLiteral("anime"),
                                         QStringLiteral("mal:2200:1"));
        QVERIFY(!f.consent(fact));
        QVERIFY(f.mappings.mappings().isEmpty());
    }
    {
        // Import: a manga list item must not attach to an anime title.
        Fixture f;
        f.progress.record({{QStringLiteral("kind"), QStringLiteral("anime")},
                           {QStringLiteral("id"), QStringLiteral("mal:2200")},
                           {QStringLiteral("title"), QStringLiteral("Anime")},
                           {QStringLiteral("progress"), 0.5},
                           {QStringLiteral("resume"), 50.0}});
        f.progress.flush();
        QCoreApplication::processEvents();
        f.transport.collections.insert(
            QStringLiteral("MANGA:1"),
            collectionPage({listEntry(mediaObject(555, 2200, 0, 100),
                                      QStringLiteral("CURRENT"), 10)}, false));
        f.sync.syncAll({QStringLiteral("anilist")}, 1);
        QTRY_COMPARE(f.imports.batches().size(), 1);
        const auto batch = f.imports.batches().first();
        QCOMPARE(batch.items.size(), 1);
        QVERIFY(!batch.items.first().remote.mapping);
    }
}

void TrackerAniListSyncTest::tankoban_never_produces_a_request()
{
    Fixture f;
    // A normal anime delivery turns the outbox on first.
    f.addAnime(777, 1535, 24);
    f.addManga(555, 2200, 100);
    const auto anime = f.progressFact(QStringLiteral("anime"),
                                      QStringLiteral("mal:1535:3"));
    QVERIFY(f.consent(anime));
    f.transport.log.clear();
    f.transport.bodies.clear();

    const auto fact = f.progressFact(QStringLiteral("tankoban"),
                                     QStringLiteral("mal:2200:chapter:5"));
    QVERIFY(!fact.historyId.isEmpty());
    QCOMPARE(fact.mediaDomain, TrackerMediaDomain::Manga);
    // Not mapped, not previewed, and no request of any kind for it.
    const auto remote = f.snapshot({fact});
    QVERIFY(remote);
    QCOMPARE(remote->items.size(), 0);
    QCOMPARE(f.transport.log.size(), 0);
    QVERIFY(!f.mappings.mapping(
        {TrackerProviderId::AniList, kViewer,
         QStringLiteral("anilist:manga:555:chapter:5")}));

    // Even a hand-made mapping cannot put a Tankoban fact on the wire.
    QVERIFY(f.mappings.upsert(
        {TrackerProviderId::AniList, kViewer,
         QStringLiteral("anilist:manga:555:chapter:5")},
        {fact.canonicalMediaId, fact.historyKind, fact.historyId,
         fact.canonicalMediaId, {}},
        TrackerMappingProvenance::UserConfirmed));
    f.sync.start();
    QTRY_COMPARE(f.transport.count(QStringLiteral("save")), 1);
    QTest::qWait(1300);
    // Only the anime write ever left; nothing names the manga media.
    QCOMPARE(f.transport.count(QStringLiteral("save")), 1);
    QCOMPARE(saveMediaId(f.transport.saves.first()), 777);
    for (const QJsonObject &body : f.transport.bodies) {
        QVERIFY(body.value(QStringLiteral("variables")).toObject()
                    .value(QStringLiteral("type")).toString()
                != QLatin1String("MANGA"));
        QVERIFY(saveMediaId(body) != 555);
    }
    int tankobanOperations = 0;
    for (const auto &operation : f.delivery.operations()) {
        if (operation.fact.historyKind != QLatin1String("tankoban"))
            continue;
        ++tankobanOperations;
        QCOMPARE(operation.state, TrackerDeliveryState::FailedTerminal);
        QCOMPARE(operation.reason, TrackerDeliveryReason::UnsupportedAction);
    }
    QCOMPARE(tankobanOperations, 1);
}

void TrackerAniListSyncTest::manga_and_manga_chapter_send_chapters_only()
{
    for (const QString &kind : {QStringLiteral("manga"),
                                QStringLiteral("manga_chapter")}) {
        Fixture f;
        f.addManga(555, 2200, 100);
        const auto fact = f.progressFact(kind, QStringLiteral("mal:2200:chapter:5"));
        QVERIFY2(f.consent(fact), qPrintable(kind));
        f.sync.start();
        QTRY_COMPARE(f.transport.count(QStringLiteral("save")), 1);
        QTRY_COMPARE(f.firstOperation().state, TrackerDeliveryState::Succeeded);
        QCOMPARE(saveMediaId(f.transport.saves.first()), 555);
        QCOMPARE(saveProgress(f.transport.saves.first()), 5);
        QCOMPARE(saveStatus(f.transport.saves.first()), QStringLiteral("CURRENT"));
        for (const QJsonObject &body : f.transport.bodies) {
            QVERIFY(!body.value(QStringLiteral("query")).toString()
                         .contains(QLatin1String("rogressVolumes")));
        }
    }
}

void TrackerAniListSyncTest::anime_episode_progress_is_sent()
{
    Fixture f;
    f.addAnime(777, 1535, 24);
    const auto fact = f.progressFact(QStringLiteral("anime"),
                                     QStringLiteral("mal:1535:3"));
    QVERIFY(f.consent(fact));
    f.sync.start();
    QTRY_COMPARE(f.transport.count(QStringLiteral("save")), 1);
    QTRY_COMPARE(f.firstOperation().state, TrackerDeliveryState::Succeeded);
    QCOMPARE(saveMediaId(f.transport.saves.first()), 777);
    QCOMPARE(saveProgress(f.transport.saves.first()), 3);
    QCOMPARE(saveStatus(f.transport.saves.first()), QStringLiteral("CURRENT"));
}

void TrackerAniListSyncTest::partial_episode_never_marks_completed()
{
    Fixture f;
    f.addAnime(777, 1535, 12);
    // Forty-one percent into the LAST episode is not a finished episode.
    const auto partial = f.progressFact(QStringLiteral("anime"),
                                        QStringLiteral("mal:1535:12"), 0.41);
    QCOMPARE(partial.progress, 41);
    QVERIFY(f.consent(partial));
    f.sync.start();
    QTRY_COMPARE(f.firstOperation().state, TrackerDeliveryState::FailedTerminal);
    QCOMPARE(f.firstOperation().reason, TrackerDeliveryReason::UnsupportedAction);
    QCOMPARE(f.transport.count(QStringLiteral("save")), 0);
}

void TrackerAniListSyncTest::completion_maps_to_completed()
{
    {
        // A finished last episode completes the entry.
        Fixture f;
        f.addAnime(777, 1535, 12);
        const auto fact = f.progressFact(QStringLiteral("anime"),
                                         QStringLiteral("mal:1535:12"));
        QVERIFY(f.consent(fact));
        f.sync.start();
        QTRY_COMPARE(f.transport.count(QStringLiteral("save")), 1);
        QCOMPARE(saveStatus(f.transport.saves.first()),
                 QStringLiteral("COMPLETED"));
        QCOMPARE(saveProgress(f.transport.saves.first()), 12);
        QTRY_COMPARE(f.firstOperation().state, TrackerDeliveryState::Succeeded);
    }
    {
        // A witnessed completion fact (chapters are filed as manga_chapter).
        Fixture f;
        f.addManga(555, 2200, 100);
        const auto fact = f.completionFact(
            QStringLiteral("manga_chapter"),
            QStringLiteral("mal:2200:chapter:100"));
        QVERIFY(!fact.sourceEventId.isEmpty());
        QCOMPARE(fact.kind, TrackerDeliveryFactKind::Completion);
        QVERIFY(f.consent(fact));
        f.sync.start();
        QTRY_COMPARE(f.transport.count(QStringLiteral("save")), 1);
        QCOMPARE(saveStatus(f.transport.saves.first()),
                 QStringLiteral("COMPLETED"));
        QCOMPARE(saveProgress(f.transport.saves.first()), 100);
        QTRY_COMPARE(f.firstOperation().state, TrackerDeliveryState::Succeeded);
    }
}

void TrackerAniListSyncTest::import_pages_two_chunks_into_review_without_applying()
{
    Fixture f;
    const QVariantList localBefore = f.progress.syncEntries();
    f.transport.collections.insert(
        QStringLiteral("ANIME:1"),
        collectionPage({listEntry(mediaObject(1, 101, 12, 0),
                                  QStringLiteral("CURRENT"), 4)}, true));
    f.transport.collections.insert(
        QStringLiteral("ANIME:2"),
        collectionPage({listEntry(mediaObject(2, 102, 24, 0),
                                  QStringLiteral("COMPLETED"), 24),
                        // A custom list repeats an entry; it collapses.
                        listEntry(mediaObject(1, 101, 12, 0),
                                  QStringLiteral("CURRENT"), 4)}, false));
    f.transport.collections.insert(
        QStringLiteral("MANGA:1"),
        collectionPage({listEntry(mediaObject(3, 103, 0, 50),
                                  QStringLiteral("PLANNING"), 0)}, false));

    f.sync.syncAll({QStringLiteral("anilist")}, 1);
    QTRY_COMPARE(f.imports.batches().size(), 1);

    int animeChunks = 0;
    int mangaChunks = 0;
    for (const QJsonObject &body : f.transport.bodies) {
        const QJsonObject variables = body.value(QStringLiteral("variables")).toObject();
        if (!body.value(QStringLiteral("query")).toString()
                 .contains(QLatin1String("MediaListCollection"))) {
            continue;
        }
        QCOMPARE(variables.value(QStringLiteral("userId")).toInt(), 99);
        QCOMPARE(variables.value(QStringLiteral("perChunk")).toInt(), 100);
        if (variables.value(QStringLiteral("type")).toString() == QLatin1String("ANIME"))
            ++animeChunks;
        else
            ++mangaChunks;
    }
    QCOMPARE(animeChunks, 2);
    QCOMPARE(mangaChunks, 1);

    const auto batch = f.imports.batches().first();
    QCOMPARE(batch.providerId, TrackerProviderId::AniList);
    QCOMPARE(batch.items.size(), 3);
    QVERIFY(!batch.confirmed);
    QVERIFY(!batch.cursorCommitted);
    // Reviewed, not applied: local progress is untouched.
    QCOMPARE(f.progress.syncEntries(), localBefore);
    QVERIFY(!f.imports.confirmedCursor(TrackerProviderId::AniList, kViewer));
}

void TrackerAniListSyncTest::unknown_outcome_reads_back_before_retry_applied()
{
    Fixture f;
    f.addAnime(777, 1535, 24);
    f.transport.applyOnFailure = true;
    f.transport.saveHandler = [](int) { return lostAcknowledgement(); };
    const auto fact = f.progressFact(QStringLiteral("anime"),
                                     QStringLiteral("mal:1535:3"));
    QVERIFY(f.consent(fact));
    f.transport.log.clear();
    f.sync.start();
    // The write landed but its acknowledgement was lost: one save, then a
    // readback that finds the exact state; nothing is sent twice.
    QTRY_COMPARE(f.firstOperation().state, TrackerDeliveryState::Succeeded);
    QCOMPARE(f.firstOperation().reason, TrackerDeliveryReason::ReadbackPresent);
    QCOMPARE(f.transport.count(QStringLiteral("save")), 1);
    QCOMPARE(f.transport.count(QStringLiteral("readback")), 1);
}

void TrackerAniListSyncTest::unknown_outcome_reads_back_before_retry_absent()
{
    Fixture f;
    f.addAnime(777, 1535, 24);
    f.transport.applyOnFailure = false;
    f.transport.saveHandler = [](int index) {
        return index == 0 ? lostAcknowledgement()
                          : okResult(entryData(QStringLiteral("SaveMediaListEntry"),
                                               777, QStringLiteral("CURRENT"), 3));
    };
    const auto fact = f.progressFact(QStringLiteral("anime"),
                                     QStringLiteral("mal:1535:3"));
    QVERIFY(f.consent(fact));
    f.transport.log.clear();
    f.sync.start();
    // The retry waits for the readback: save, readback, then the next save.
    QTRY_COMPARE_WITH_TIMEOUT(f.transport.count(QStringLiteral("save")), 2, 15000);
    const int firstSave = f.transport.log.indexOf(QStringLiteral("save"));
    const int readback = f.transport.log.indexOf(QStringLiteral("readback"));
    const int secondSave = f.transport.log.lastIndexOf(QStringLiteral("save"));
    QVERIFY(firstSave >= 0 && readback > firstSave && secondSave > readback);
    QTRY_COMPARE(f.firstOperation().state, TrackerDeliveryState::Succeeded);
}

void TrackerAniListSyncTest::rate_limited_defers_without_loss()
{
    Fixture f;
    f.addAnime(777, 1535, 24);
    f.transport.saveHandler = [](int) {
        return AniListApiResult{AniListTransportError::RateLimited, 429, {},
                                7000, false, false};
    };
    const auto fact = f.progressFact(QStringLiteral("anime"),
                                     QStringLiteral("mal:1535:3"));
    QVERIFY(f.consent(fact));
    const qint64 before = QDateTime::currentMSecsSinceEpoch();
    f.sync.start();
    QTRY_COMPARE(f.firstOperation().state, TrackerDeliveryState::Retrying);
    QCOMPARE(f.firstOperation().reason, TrackerDeliveryReason::ProviderRateLimited);
    QVERIFY(f.firstOperation().nextAttemptAtMs >= before + 7000);
    QCOMPARE(f.delivery.operations().size(), 1);
    QCOMPARE(f.transport.count(QStringLiteral("save")), 1);
    // The 429 is not a sign-in problem.
    QCOMPARE(f.controller.phase(), QStringLiteral("idle"));
}

void TrackerAniListSyncTest::unauthorized_requires_reconnect_and_keeps_queue()
{
    Fixture f;
    f.addAnime(777, 1535, 24);
    f.transport.saveHandler = [](int) {
        return AniListApiResult{AniListTransportError::AuthenticationRequired,
                                401, {}, 0, false, false};
    };
    const auto fact = f.progressFact(QStringLiteral("anime"),
                                     QStringLiteral("mal:1535:3"));
    QVERIFY(f.consent(fact));
    f.sync.start();
    QTRY_COMPARE(f.controller.phase(), QStringLiteral("reconnect_required"));
    QVERIFY(f.controller.reconnectRequired());
    QCOMPARE(f.connection().state, TrackerConnectionState::Connected);
    QCOMPARE(f.delivery.operations().size(), 1);
    QCOMPARE(f.firstOperation().state, TrackerDeliveryState::NeedsAttention);
    QCOMPARE(f.firstOperation().reason,
             TrackerDeliveryReason::AuthenticationRequired);
    // Nothing else goes out until the user reconnects, and nothing is dropped.
    f.sync.start();
    QTest::qWait(100);
    QCOMPARE(f.transport.count(QStringLiteral("save")), 1);
    QCOMPARE(f.delivery.operations().size(), 1);
}

QTEST_GUILESS_MAIN(TrackerAniListSyncTest)
#include "tst_tracker_anilist_sync.moc"
