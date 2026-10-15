// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/ActivityStore.h"
#include "account/AccountAttachmentReceipt.h"
#include "account/FirstAccountProfileCoordinator.h"
#include "account/LegacyPersonalStateStorage.h"
#include "account/ProfileAdoption.h"
#include "account/ProfilePaths.h"
#include "account/ProfileStoreRuntime.h"
#include "account/ProfilePreferencesStore.h"
#include "account/RatingsReviewsConversionMap.h"
#include "account/RatingsReviewsDelivery.h"
#include "account/RatingsReviewsDeliveryOutbox.h"
#include "account/RatingsReviewsDeliveryReceiptStore.h"
#include "account/RatingsReviewsProviderMappingStore.h"
#include "account/RatingsReviewsStore.h"
#include "trackers/TrackerConnectionStore.h"
#include "trackers/TrackerMappingStore.h"
#include "trackers/TrackerScrobbleRuntime.h"
#include "trackers/TrackerScrobbleStore.h"

#include "ProgressStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSaveFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto kAccountA =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kAccountB =
    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";

RatingsReviewsConversionMap fixtureConversionMap(
    const RatingsReviewsConversionTestHook &hook) {
    QString error;
    const auto map = RatingsReviewsConversionMap::recommended(
        QStringLiteral("fixture-a"),
        QStringLiteral("fixture-halfpoint-v1"),
        1,
        hook,
        &error);
    if (!map.has_value())
        qFatal("conversion fixture failed: %s", qPrintable(error));
    return *map;
}

PersonalStateSnapshot populatedSnapshot() {
    PersonalStateSnapshot snapshot;

    QJsonObject progress;
    progress.insert(
        QStringLiteral("id"),
        QStringLiteral("series-1"));
    progress.insert(
        QStringLiteral("kind"),
        QStringLiteral("manga"));
    progress.insert(
        QStringLiteral("caption"),
        QStringLiteral("Chapter 9"));
    progress.insert(
        QStringLiteral("progress"),
        0.45);
    progress.insert(
        QStringLiteral("updatedAt"),
        1720000000000.0);
    snapshot.progressEntries.insert(
        QStringLiteral("manga\x1fseries-1"),
        progress);

    snapshot.progressLastSeason.insert(
        QStringLiteral("show-1"),
        3);
    snapshot.progressWatchedMarks.insert(
        QStringLiteral("show-1"),
        -1);
    snapshot.progressWatchedMarkActionTimes.insert(
        QStringLiteral("show-1"),
        1720000003000.0);

    QJsonObject collection;
    collection.insert(
        QStringLiteral("id"),
        QStringLiteral("series-1"));
    collection.insert(
        QStringLiteral("world"),
        QStringLiteral("Tankoban"));
    collection.insert(
        QStringLiteral("type"),
        QStringLiteral("manga"));
    collection.insert(
        QStringLiteral("title"),
        QStringLiteral("Fixture Manga"));
    collection.insert(
        QStringLiteral("addedAt"),
        1720000001000.0);
    snapshot.collectionEntries.insert(
        QStringLiteral("Tankoban\x1fseries-1"),
        collection);

    QJsonArray mangaSearch;
    mangaSearch.append(
        QStringLiteral("berserk"));
    mangaSearch.append(
        QStringLiteral("vagabond"));
    snapshot.searchHistory.insert(
        QStringLiteral("manga"),
        mangaSearch);

    QJsonObject pairing;
    pairing.insert(
        QStringLiteral("bookId"),
        QStringLiteral("book-1"));
    pairing.insert(
        QStringLiteral("audiobookId"),
        QStringLiteral("audio-1"));
    pairing.insert(
        QStringLiteral("mappings"),
        QJsonArray());
    pairing.insert(
        QStringLiteral("updatedAt"),
        1720000002000.0);
    snapshot.audioPairings.insert(
        QStringLiteral("book-1"),
        pairing);

    QJsonObject history;
    history.insert(
        QStringLiteral("kind"),
        QStringLiteral("manga"));
    history.insert(
        QStringLiteral("id"),
        QStringLiteral("series-1"));
    history.insert(
        QStringLiteral("firstActivityAt"),
        1720000000500.0);
    history.insert(
        QStringLiteral("lastActivityAt"),
        1720000002500.0);
    history.insert(
        QStringLiteral("completedAt"),
        1720000002500.0);
    snapshot.historyRecords.insert(
        QStringLiteral("manga")
            + QChar(0x1f)
            + QStringLiteral("series-1"),
        history);

    const RatingsReviewsStore::Identity liveIdentity{
        QStringLiteral("theatre"),
        QStringLiteral("series"),
        QStringLiteral("fixture-series")};
    const QString liveKey = RatingsReviewsStore::recordKeyForIdentity(liveIdentity);
    snapshot.ratingsReviewsRecords.insert(liveKey, QJsonObject{
        {QStringLiteral("world"), liveIdentity.world},
        {QStringLiteral("kind"), liveIdentity.kind},
        {QStringLiteral("media_id"), liveIdentity.mediaId},
        {QStringLiteral("rating"), 8.5},
        {QStringLiteral("review"), QStringLiteral("C:\\Notes\\review.txt")},
        {QStringLiteral("spoiler"), false},
        {QStringLiteral("created_at_ms"), 1720000004000.0},
        {QStringLiteral("updated_at_ms"), 1720000005000.0}});

    const RatingsReviewsStore::Identity deletedIdentity{
        QStringLiteral("biblio"),
        QStringLiteral("book"),
        QStringLiteral("fixture-deleted")};
    const QString deletedKey = RatingsReviewsStore::recordKeyForIdentity(deletedIdentity);
    snapshot.ratingsReviewsTombstones.insert(deletedKey, QJsonObject{
        {QStringLiteral("deleted_at_ms"), 1720000006000.0}});

    snapshot.showExplicit = true;
    return snapshot;
}

struct AdoptionFixture {
    QTemporaryDir temp;
    QString legacyRoot;
    QString appDataRoot;
    LegacyPersonalStateStorage legacy;

    AdoptionFixture()
        : legacyRoot(
              QDir(temp.path())
                  .filePath(
                      QStringLiteral("legacy"))),
          appDataRoot(
              QDir(temp.path())
                  .filePath(
                      QStringLiteral("appdata"))),
          legacy(
              LegacyPersonalStateStorage::isolated(
                  legacyRoot)) {
        if (!temp.isValid())
            qFatal("Could not create account adoption test directory.");
        QDir().mkpath(legacyRoot);
        QDir().mkpath(appDataRoot);
    }

    ProfilePaths accountPaths(
        const QString &accountId =
            QString::fromLatin1(kAccountA)) const {
        const auto paths =
            ProfilePaths::account(
                accountId,
                appDataRoot);
        if (!paths.has_value())
            qFatal("Fixture account id was invalid.");
        return *paths;
    }
};

RatingsReviewsPrivateProfileBinding privateBinding(
    const LegacyPersonalStateStorage &storage) {
    return {
        storage.profileId(),
        storage.preferencesIniPath(),
        {storage.devicePrivateRatingsReviewsProviderMappingsPath(),
         storage.devicePrivateRatingsReviewsDeliveryOutboxPath(),
         storage.devicePrivateRatingsReviewsDeliveryReceiptsPath()}};
}

RatingsReviewsPrivateProfileBinding privateBinding(
    const ProfilePaths &paths) {
    return {
        paths.profileId(),
        paths.preferencesIniPath(),
        {paths.ratingsReviewsProviderMappingsPath(),
         paths.ratingsReviewsDeliveryOutboxPath(),
         paths.ratingsReviewsDeliveryReceiptsPath()}};
}

bool seedPrivateDeliveryState(
    const RatingsReviewsPrivateProfileBinding &binding,
    const QString &canonicalPath,
    const QString &operationId,
    const QString &state,
    int attemptCount,
    const QString &receiptStatus = QString(),
    RatingsReviewsDeliveryOperation *operationReadback = nullptr) {
    const RatingsReviewsStore::Identity identity{
        QStringLiteral("theatre"), QStringLiteral("series"),
        QStringLiteral("fixture-series")};
    RatingsReviewsStore canonical(canonicalPath);
    QString error;
    if (!canonical.healthy(&error))
        return false;
    const QString key = RatingsReviewsStore::recordKeyForIdentity(identity);
    const auto record = canonical.recordByKey(key);
    if (!record)
        return false;

    RatingsReviewsProviderMappingStore mappings(
        binding.privatePaths.mappingsPath);
    if (!mappings.upsert({QStringLiteral("fixture-a"), key,
                          QStringLiteral("matched"), QStringLiteral("fixture-a-title"),
                          QStringLiteral("exact"), 1720000006000LL, 7}, &error))
        return false;

    RatingsReviewsDeliveryOperation operation;
    operation.operationId = operationId;
    operation.profileId = binding.profileId;
    operation.profileIncarnation = 9;
    operation.providerId = QStringLiteral("fixture-a");
    operation.connectionGeneration = 7;
    operation.operationType = QStringLiteral("rating.set");
    operation.canonicalKey = key;
    operation.canonicalRevision = canonical.revision();
    operation.canonicalPayloadDigest =
        ratingsReviewsCanonicalPayloadDigestV1(*record);
    operation.mappingProviderMediaId = QStringLiteral("fixture-a-title");
    operation.conversionMapDigest = QString(64, QLatin1Char('a'));
    operation.intentCreatedAtMs = 1720000007000LL;
    operation.state = state;
    operation.attemptCount = attemptCount;
    if (attemptCount > 0)
        operation.lastAttemptAtMs = 1720000008000LL;
    operation.retryClass = QStringLiteral("manualAfterUnknown");
    operation.adapterIdempotency = QStringLiteral("none");
    operation.safePayload = {
        {QStringLiteral("kind"), QStringLiteral("rating")},
        {QStringLiteral("native_value"), 81},
        {QStringLiteral("source_rating"), record->rating.value_or(8.5)}};
    RatingsReviewsDeliveryOutbox outbox(binding.privatePaths.outboxPath);
    if (!outbox.append({operation}, &error))
        return false;

    if (!receiptStatus.isEmpty()) {
        RatingsReviewsDeliveryReceipt receipt;
        receipt.operationId = operation.operationId;
        receipt.providerId = operation.providerId;
        receipt.operationType = operation.operationType;
        receipt.canonicalKey = operation.canonicalKey;
        receipt.canonicalPayloadDigest = operation.canonicalPayloadDigest;
        receipt.status = receiptStatus;
        receipt.attemptNumber = attemptCount;
        receipt.createdAtMs = operation.intentCreatedAtMs;
        receipt.updatedAtMs = 1720000009000LL;
        receipt.completedAtMs = receipt.updatedAtMs;
        receipt.safeProviderStatus = QStringLiteral("fixture-ok");
        RatingsReviewsDeliveryReceiptStore receipts(
            binding.privatePaths.receiptsPath);
        if (!receipts.upsert(receipt, &error))
            return false;
    }
    if (operationReadback)
        *operationReadback = operation;
    return true;
}

void seedMachineSentinels(
    const LegacyPersonalStateStorage &legacy) {
    QSettings progress(
        legacy.progressIniPath(),
        QSettings::IniFormat);
    progress.setValue(
        QStringLiteral("session/machineSentinel"),
        QStringLiteral("keep-progress-machine-state"));
    progress.sync();

    QSettings preferences(
        legacy.preferencesIniPath(),
        QSettings::IniFormat);
    preferences.setValue(
        QStringLiteral("machine/windowGeometry"),
        QByteArrayLiteral("keep-window-state"));
    preferences.sync();
}

// A single valid playback_delta fact — 30 seconds of movie playback, the
// projector's own per-event activeMs cap (ActivityProjector.cpp) — used to
// seed a legacy activity ledger the same way a real playback session would.
// Field set mirrors tests/auto/activity/tst_activity_store.cpp's own fixture
// builders; duplicated here deliberately, same reasoning as that file's own
// compareJson() note: this is test infrastructure, not activity-engine logic.
QVariantMap fixtureMovieFact() {
    QVariantMap fact;
    fact.insert(QStringLiteral("sessionId"), QStringLiteral("adoption-fixture-session"));
    fact.insert(QStringLiteral("world"), QStringLiteral("theatre"));
    fact.insert(QStringLiteral("kind"), QStringLiteral("movie"));
    fact.insert(QStringLiteral("titleKey"), QStringLiteral("theatre:adoption-fixture-movie"));
    fact.insert(QStringLiteral("itemKey"), QStringLiteral("adoption-fixture-movie"));
    fact.insert(QStringLiteral("title"), QStringLiteral("Adoption Fixture Movie"));
    fact.insert(QStringLiteral("itemLabel"), QString());
    fact.insert(QStringLiteral("cover"), QString());
    fact.insert(QStringLiteral("utcOffsetMinutes"), qint64(330));
    fact.insert(QStringLiteral("syncable"), true);
    fact.insert(QStringLiteral("source"), QStringLiteral("test"));
    fact.insert(QStringLiteral("startAtMs"), qint64(1720000000000));
    fact.insert(QStringLiteral("endAtMs"), qint64(1720000030000));
    fact.insert(QStringLiteral("activeMs"), qint64(30000));
    fact.insert(QStringLiteral("rateMilli"), qint64(1000));
    return fact;
}

QVariantMap fixtureMovieCompletionFact() {
    QVariantMap fact;
    fact.insert(QStringLiteral("eventId"), QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc"));
    fact.insert(QStringLiteral("sessionId"), QStringLiteral("adoption-completion-session"));
    fact.insert(QStringLiteral("world"), QStringLiteral("theatre"));
    fact.insert(QStringLiteral("kind"), QStringLiteral("movie"));
    fact.insert(QStringLiteral("titleKey"), QStringLiteral("theatre:adoption-completion-movie"));
    fact.insert(QStringLiteral("itemKey"), QStringLiteral("adoption-completion-movie"));
    fact.insert(QStringLiteral("title"), QStringLiteral("Adoption Completion Movie"));
    fact.insert(QStringLiteral("itemLabel"), QString());
    fact.insert(QStringLiteral("cover"), QString());
    fact.insert(QStringLiteral("utcOffsetMinutes"), qint64(330));
    fact.insert(QStringLiteral("syncable"), true);
    fact.insert(QStringLiteral("source"), QStringLiteral("test"));
    fact.insert(QStringLiteral("atMs"), qint64(1720000030000));
    fact.insert(QStringLiteral("reason"), QStringLiteral("eof"));
    return fact;
}

void verifyMachineSentinels(
    const LegacyPersonalStateStorage &legacy) {
    QSettings progress(
        legacy.progressIniPath(),
        QSettings::IniFormat);
    QCOMPARE(
        progress.value(
            QStringLiteral("session/machineSentinel"))
            .toString(),
        QStringLiteral("keep-progress-machine-state"));

    QSettings preferences(
        legacy.preferencesIniPath(),
        QSettings::IniFormat);
    QCOMPARE(
        preferences.value(
            QStringLiteral("machine/windowGeometry"))
            .toByteArray(),
        QByteArrayLiteral("keep-window-state"));
}

bool activityContainsItem(
    const ActivityStore &activity,
    const QString &itemKey) {
    for (const QVariantMap &fact : activity.historyProjectionFacts()) {
        if (fact.value(QStringLiteral("itemKey")).toString() == itemKey)
            return true;
    }
    return false;
}

bool rewriteAdoptionState(
    const ProfilePaths &paths,
    const QString &state,
    bool removeSourceKind,
    QString *error = nullptr) {
    QFile input(paths.adoptionJournalPath());
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not read the adoption journal fixture.");
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument document =
        QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        if (error)
            *error = QStringLiteral("The adoption journal fixture is malformed.");
        return false;
    }

    QJsonObject object = document.object();
    object.insert(QStringLiteral("state"), state);
    if (removeSourceKind)
        object.remove(QStringLiteral("source_kind"));
    input.close();

    QSaveFile output(paths.adoptionJournalPath());
    if (!output.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not write the adoption journal fixture.");
        return false;
    }
    const QByteArray payload =
        QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (output.write(payload) != payload.size()
        || !output.commit()) {
        if (error)
            *error = QStringLiteral("Could not commit the adoption journal fixture.");
        return false;
    }
    return true;
}
}

class tst_account_adoption : public QObject {
    Q_OBJECT

private slots:
    void populatedFirstAccountCommitsBeforeActivation();
    void cleanRestartKeepsCommittedAdoption();
    void committedAccountSessionMergesResidualLocalOnlyState();
    void ordinarySignInAdoptsLegacyLocalState();
    void R8_existing_account_merge_preserves_terminal_receipt();
    void activeAccountSessionMergesLaterLocalOnlyState();
    void rememberedAccountSessionMergesLaterLocalOnlyState();
    void coldStartTrackerOnlyLocalOnlyProfileRetriesPrivateHandoff();
    void continueLocalBeforeAdoptionKeepsLegacyAuthority();
    void continueLocalAfterAdoptionUsesDedicatedLocalProfile();
    void corruptRestartPreservesAccountAndEvidence();
    void missingFinalStorePreservesAccountEvidence();
    void retryIntentReAdoptsOnLaterSignIn();
    void legacySnapshotV1RemainsReadableWithoutHistory();
    void legacySnapshotsV1ThroughV4RemainCompatible();
    void ratingsReviewsSnapshotV5RoundTripsAndV4DigestRemainsExact();
    void populatedRatingsReviewsCannotMatchLegacyV4Digest();
    void R8_legacy_private_handoff_idempotent_and_order_preserved();
    void ratingsReviewsConversionMapCopyFailurePreservesSource();
    void R8_handoff_failure_preserves_source_and_blocks_activation();
    void directAccountSwitchRequiresSealing();

    void existingAccountMergeAcceptsCompletedActivity();
    void activityOnlyLocalStateIsMergedIntoExistingAccount();
    void existingCachedAccountKeepsSourceForAttachment();
    void activityOnlyExistingAccountKeepsSourceForAttachment();
    void firstAccountAdoptionMigratesActivityLedger();
    void interruptedAdoptionPreservesAccountActivityEvidence();
    void postAdoptionWritesSurviveCleanRestart();
    void legacyQuarantinedRestartPreservesAccountWrites();
    void explicitLocalQuarantineIgnoresUnrelatedLegacyState();
    void explicitLocalPreparingAdoptionResumesFromLocalSource();
    void retryFailsClosedWhenCompetingSourceUnreadable();
    void R8_localonly_private_handoff_before_source_clear();
    void legacyAccountlessAdoptionCarriesStremioCredential();
    void stremioCredentialTransferReplacesStaleDestinationForSameAccount();
    void stremioCredentialTransferRetriesBeforeSourceRetirement();
    void migrationSuspensionStopsWhenTrackerCloseCannotBePersisted();
};

void tst_account_adoption::
legacySnapshotV1RemainsReadableWithoutHistory() {
    PersonalStateSnapshot source =
        populatedSnapshot();

    QJsonObject legacy =
        source.toJson();
    legacy.insert(
        QStringLiteral("version"),
        1);
    legacy.remove(
        QStringLiteral(
            "history_records"));

    QString error;
    const auto parsed =
        PersonalStateSnapshot::
            fromJson(
                legacy,
                &error);

    QVERIFY2(
        parsed.has_value(),
        qPrintable(error));
    QVERIFY(
        parsed->historyRecords.isEmpty());
    QVERIFY(
        parsed->matchesSemanticDigest(
            parsed->legacySemanticDigestV1()));
    QVERIFY(
        parsed->semanticDigest()
        != parsed->legacySemanticDigestV1());
    QCOMPARE(
        parsed->collectionEntries,
        source.collectionEntries);
    QCOMPARE(
        parsed->progressEntries,
        source.progressEntries);
}

void tst_account_adoption::
legacySnapshotsV1ThroughV4RemainCompatible() {
    PersonalStateSnapshot source = populatedSnapshot();
    source.ratingsReviewsRecords = {};
    source.ratingsReviewsTombstones = {};

    QJsonObject v4 = source.toJson();
    v4.insert(QStringLiteral("version"), 4);
    v4.remove(QStringLiteral("ratings_reviews_records"));
    v4.remove(QStringLiteral("ratings_reviews_tombstones"));

    QJsonObject v3 = v4;
    v3.insert(QStringLiteral("version"), 3);
    v3.remove(QStringLiteral("main_sync_provider"));
    v3.remove(QStringLiteral("stremio_state"));
    v3.remove(QStringLiteral("theatre_extensions"));

    QJsonObject v2 = v3;
    v2.insert(QStringLiteral("version"), 2);
    v2.remove(QStringLiteral("progress_watched_mark_action_times"));

    QJsonObject v1 = v2;
    v1.insert(QStringLiteral("version"), 1);
    v1.remove(QStringLiteral("history_records"));

    const QList<QJsonObject> legacy{v1, v2, v3, v4};
    for (int index = 0; index < legacy.size(); ++index) {
        QString error;
        const auto parsed =
            PersonalStateSnapshot::fromJson(legacy.at(index), &error);
        QVERIFY2(parsed.has_value(), qPrintable(error));
        QVERIFY(parsed->ratingsReviewsRecords.isEmpty());
        QVERIFY(parsed->ratingsReviewsTombstones.isEmpty());

        const QString digest =
            index == 0 ? parsed->legacySemanticDigestV1()
            : index == 1 ? parsed->legacySemanticDigestV2()
            : index == 2 ? parsed->legacySemanticDigestV3()
                         : parsed->legacySemanticDigestV4();
        QVERIFY(parsed->matchesSemanticDigest(digest));
    }
}

void tst_account_adoption::
ratingsReviewsSnapshotV5RoundTripsAndV4DigestRemainsExact() {
    const PersonalStateSnapshot source = populatedSnapshot();
    const QJsonObject encoded = source.toJson();

    QCOMPARE(encoded.value(QStringLiteral("version")).toInt(), 5);
    QVERIFY(encoded.contains(QStringLiteral("ratings_reviews_records")));
    QVERIFY(encoded.contains(QStringLiteral("ratings_reviews_tombstones")));

    QString error;
    const auto parsed = PersonalStateSnapshot::fromJson(encoded, &error);
    QVERIFY2(parsed.has_value(), qPrintable(error));
    QCOMPARE(parsed->toJson(), encoded);
    QCOMPARE(parsed->ratingsReviewsRecords, source.ratingsReviewsRecords);
    QCOMPARE(parsed->ratingsReviewsTombstones, source.ratingsReviewsTombstones);

    PersonalStateSnapshot preV5 = source;
    preV5.ratingsReviewsRecords = {};
    preV5.ratingsReviewsTombstones = {};
    QJsonObject v4 = preV5.toJson();
    v4.insert(QStringLiteral("version"), 4);
    v4.remove(QStringLiteral("ratings_reviews_records"));
    v4.remove(QStringLiteral("ratings_reviews_tombstones"));
    const QString v4Digest = QString::fromLatin1(
        QCryptographicHash::hash(
            QJsonDocument(v4).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256).toHex());
    QCOMPARE(preV5.legacySemanticDigestV4(), v4Digest);
    QVERIFY(preV5.matchesSemanticDigest(v4Digest));

    const auto parsedV4 = PersonalStateSnapshot::fromJson(v4, &error);
    QVERIFY2(parsedV4.has_value(), qPrintable(error));
    QVERIFY(parsedV4->ratingsReviewsRecords.isEmpty());
    QVERIFY(parsedV4->ratingsReviewsTombstones.isEmpty());

    QJsonObject malformedV5 = encoded;
    malformedV5.remove(QStringLiteral("ratings_reviews_tombstones"));
    QVERIFY(!PersonalStateSnapshot::fromJson(malformedV5, &error).has_value());
}

void tst_account_adoption::
populatedRatingsReviewsCannotMatchLegacyV4Digest() {
    const PersonalStateSnapshot source = populatedSnapshot();
    QVERIFY(!source.ratingsReviewsRecords.isEmpty());
    QVERIFY(!source.ratingsReviewsTombstones.isEmpty());
    QVERIFY(!source.matchesSemanticDigest(source.legacySemanticDigestV4()));
    QVERIFY(!source.matchesSemanticDigest(source.legacySemanticDigestV3()));
    QVERIFY(!source.matchesSemanticDigest(source.legacySemanticDigestV2()));
    QVERIFY(!source.matchesSemanticDigest(source.legacySemanticDigestV1()));
}

void tst_account_adoption::
R8_legacy_private_handoff_idempotent_and_order_preserved() {
    AdoptionFixture fixture;
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    const RatingsReviewsConversionMap map = fixtureConversionMap(hook);
    QVERIFY(fixture.legacy.restorePersonalState(populatedSnapshot()));

    const auto legacyBinding = privateBinding(fixture.legacy);
    const QString operationId = QStringLiteral("49b86174-79b8-4da2-8b54-7a902bbc8701");
    RatingsReviewsDeliveryOperation legacyUnknown;
    QVERIFY(seedPrivateDeliveryState(
        legacyBinding, fixture.legacy.ratingsReviewsPath(), operationId,
        QStringLiteral("unknownOutcome"), 1, {}, &legacyUnknown));
    QFile legacyOutboxFile(legacyBinding.privatePaths.outboxPath);
    QVERIFY(legacyOutboxFile.open(QIODevice::ReadOnly));
    const QByteArray sourceOutboxBytes = legacyOutboxFile.readAll();
    legacyOutboxFile.close();

    ProfilePreferencesStore sourcePreferences(
        fixture.legacy.preferencesIniPath(), hook);
    QVERIFY(sourcePreferences.setRatingsReviewsConversionMap(map));
    QVERIFY(sourcePreferences.setRatingsReviewsProviderOrder({
        QStringLiteral("anilist"), QStringLiteral("mal"), QStringLiteral("trakt"),
        QStringLiteral("simkl"), QStringLiteral("imdb"), QStringLiteral("tmdb"),
        QStringLiteral("rotten_tomatoes"), QStringLiteral("metacritic")}));
    QVERIFY(sourcePreferences.setRatingsReviewsDefaultRatingDestinations(
        {QStringLiteral("mal")}));
    QVERIFY(sourcePreferences.setRatingsReviewsDefaultReviewDestinations(
        {QStringLiteral("anilist"), QStringLiteral("mal")}));

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot,
        {},
        hook,
        RatingsReviewsPrivateAdoptionCallbacks{
            [](const RatingsReviewsPrivateProfileBinding &source,
               const RatingsReviewsPrivateProfileBinding &destination,
               RatingsReviewsStore *destinationCanonical,
               QString *handoffError) {
                return RatingsReviewsDelivery::handoffPrivateState(
                    source,
                    destination,
                    destinationCanonical,
                    handoffError);
            }});
    QString error;
    QVERIFY2(
        coordinator.prepareCreatedAccount(QString::fromLatin1(kAccountA), &error),
        qPrintable(error));

    ProfilePreferencesStore destination(
        fixture.accountPaths().preferencesIniPath(), hook);
    const auto copied = destination.ratingsReviewsConversionMap(map.providerId);
    QVERIFY(copied.has_value());
    QCOMPARE(copied->digest(), map.digest());
    QCOMPARE(
        destination.ratingsReviewsProviderOrder(),
        QStringList({
            QStringLiteral("anilist"), QStringLiteral("mal"), QStringLiteral("trakt"),
            QStringLiteral("simkl"), QStringLiteral("imdb"), QStringLiteral("tmdb"),
            QStringLiteral("rotten_tomatoes"), QStringLiteral("metacritic")}));
    QCOMPARE(
        destination.ratingsReviewsDefaultRatingDestinations(),
        QStringList({QStringLiteral("mal")}));
    QCOMPARE(
        destination.ratingsReviewsDefaultReviewDestinations(),
        QStringList({QStringLiteral("anilist"), QStringLiteral("mal")}));

    const auto adoption = ProfileAdoption::open(fixture.accountPaths(), &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QVERIFY(adoption->snapshot().ratingsReviewsPrivateHandoffMarkerRecorded);
    QVERIFY(adoption->snapshot().ratingsReviewsPrivateHandoffVerified);
    QVERIFY(QFileInfo::exists(
        fixture.accountPaths().ratingsReviewsDeliveryOutboxPath()));
    RatingsReviewsDeliveryOutbox accountOutbox(
        fixture.accountPaths().ratingsReviewsDeliveryOutboxPath());
    const auto adoptedUnknown = accountOutbox.operation(operationId);
    QVERIFY(adoptedUnknown.has_value());
    QCOMPARE(adoptedUnknown->profileId, fixture.accountPaths().profileId());
    QCOMPARE(adoptedUnknown->profileIncarnation, legacyUnknown.profileIncarnation);
    QCOMPARE(adoptedUnknown->intentCreatedAtMs, legacyUnknown.intentCreatedAtMs);
    QCOMPARE(adoptedUnknown->state, QStringLiteral("unknownOutcome"));
    QFile legacyOutboxAfter(legacyBinding.privatePaths.outboxPath);
    QVERIFY(legacyOutboxAfter.open(QIODevice::ReadOnly));
    QCOMPARE(legacyOutboxAfter.readAll(), sourceOutboxBytes);

    const auto destinationBinding = privateBinding(fixture.accountPaths());
    RatingsReviewsStore destinationCanonical(
        fixture.accountPaths().ratingsReviewsPath());
    QVERIFY2(destinationCanonical.healthy(&error), qPrintable(error));
    QVERIFY2(RatingsReviewsDelivery::handoffPrivateState(
                 legacyBinding, destinationBinding, &destinationCanonical, &error),
             qPrintable(error));
    RatingsReviewsDeliveryOutbox idempotentReadback(
        destinationBinding.privatePaths.outboxPath);
    QCOMPARE(idempotentReadback.operations().size(), 1);
    QCOMPARE(idempotentReadback.operation(operationId)->state,
             QStringLiteral("unknownOutcome"));
    ProfilePreferencesStore orderReadback(
        fixture.accountPaths().preferencesIniPath(), hook);
    QCOMPARE(orderReadback.ratingsReviewsProviderOrder(),
             destination.ratingsReviewsProviderOrder());
}

void tst_account_adoption::
ratingsReviewsConversionMapCopyFailurePreservesSource() {
    AdoptionFixture fixture;
    const auto gate = RatingsReviewsConversionTestHook::settingsFailureGate();
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains(gate);
    const RatingsReviewsConversionMap map = fixtureConversionMap(hook);
    QVERIFY(fixture.legacy.restorePersonalState(populatedSnapshot()));

    ProfilePreferencesStore sourcePreferences(
        fixture.legacy.preferencesIniPath(), hook);
    QVERIFY(sourcePreferences.setRatingsReviewsConversionMap(map));

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime, fixture.appDataRoot, {}, hook);
    *gate = true;
    QString error;
    QVERIFY(!coordinator.prepareCreatedAccount(
        QString::fromLatin1(kAccountA), &error));
    QVERIFY(!error.isEmpty());
    *gate = false;

    ProfilePreferencesStore sourceReadback(
        fixture.legacy.preferencesIniPath(), hook);
    const auto preserved =
        sourceReadback.ratingsReviewsConversionMap(map.providerId);
    QVERIFY(preserved.has_value());
    QCOMPARE(preserved->digest(), map.digest());
}

void tst_account_adoption::
R8_handoff_failure_preserves_source_and_blocks_activation() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source = populatedSnapshot();
    QVERIFY(fixture.legacy.restorePersonalState(source));
    const auto legacyBinding = privateBinding(fixture.legacy);
    const QString operationId = QStringLiteral("27319e13-55ee-4b1a-a52c-80028998135f");
    QVERIFY(seedPrivateDeliveryState(
        legacyBinding, fixture.legacy.ratingsReviewsPath(), operationId,
        QStringLiteral("unknownOutcome"), 1));
    QFile sourceOutbox(legacyBinding.privatePaths.outboxPath);
    QVERIFY(sourceOutbox.open(QIODevice::ReadOnly));
    const QByteArray originalOutboxBytes = sourceOutbox.readAll();
    sourceOutbox.close();

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime, fixture.appDataRoot, {}, {},
        RatingsReviewsPrivateAdoptionCallbacks{
            [](const RatingsReviewsPrivateProfileBinding &,
               const RatingsReviewsPrivateProfileBinding &,
               RatingsReviewsStore *,
               QString *error) {
                if (error)
                    *error = QStringLiteral("injected private handoff failure");
                return false;
            }});
    QString error;
    QVERIFY(!coordinator.prepareCreatedAccount(
        QString::fromLatin1(kAccountA), &error));
    QVERIFY(error.contains(QStringLiteral("injected private handoff failure")));
    QVERIFY(runtime.activeProfile().kind() != ProfilePaths::Kind::Account);

    const ProfilePaths accountPaths = fixture.accountPaths();
    RatingsReviewsStore accountCanonical(accountPaths.ratingsReviewsPath());
    QVERIFY(accountCanonical.healthy());
    QVERIFY(accountCanonical.recordByKey(RatingsReviewsStore::recordKeyForIdentity({
        QStringLiteral("theatre"), QStringLiteral("series"),
        QStringLiteral("fixture-series")})).has_value());
    const auto adoption = ProfileAdoption::open(accountPaths, &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QCOMPARE(adoption->state(), ProfileAdoption::State::Promoted);
    QVERIFY(adoption->snapshot().ratingsReviewsPrivateHandoffMarkerRecorded);
    QVERIFY(!adoption->snapshot().ratingsReviewsPrivateHandoffVerified);

    const auto sourceReadback = fixture.legacy.capture(&error);
    QVERIFY2(sourceReadback.has_value(), qPrintable(error));
    QCOMPARE(sourceReadback->semanticDigest(), source.semanticDigest());
    QFile sourceOutboxAfter(legacyBinding.privatePaths.outboxPath);
    QVERIFY(sourceOutboxAfter.open(QIODevice::ReadOnly));
    QCOMPARE(sourceOutboxAfter.readAll(), originalOutboxBytes);
}

void tst_account_adoption::
populatedFirstAccountCommitsBeforeActivation() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();

    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));
    seedMachineSentinels(fixture.legacy);

    const QString mediaPath =
        QDir(fixture.appDataRoot)
            .filePath(
                QStringLiteral("Vault/fixture.cbz"));
    QVERIFY(
        QDir().mkpath(
            QFileInfo(mediaPath)
                .absolutePath()));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QCOMPARE(
        media.write(
            QByteArrayLiteral("MEDIA-SENTINEL")),
        qint64(14));
    media.close();

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareCreatedAccount(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const ProfilePaths paths =
        fixture.accountPaths();

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);
    QCOMPARE(
        runtime.activeProfile().profileId(),
        paths.profileId());

    const auto legacyAfter =
        fixture.legacy.capture(&error);
    QVERIFY2(
        legacyAfter.has_value(),
        qPrintable(error));
    // Promotion now preserves the exact local source for the attachment
    // coordinator; cloud proof owns the later retirement boundary.
    QCOMPARE(legacyAfter->semanticDigest(), source.semanticDigest());
    QVERIFY(QFileInfo::exists(paths.cloudAttachmentReceiptPath()));

    const auto profileStorage =
        LegacyPersonalStateStorage::forProfile(
            paths,
            &error);
    QVERIFY2(
        profileStorage.has_value(),
        qPrintable(error));

    const auto profileAfter =
        profileStorage->capture(&error);
    QVERIFY2(
        profileAfter.has_value(),
        qPrintable(error));
    QCOMPARE(
        profileAfter->semanticDigest(),
        source.semanticDigest());

    const auto adoption =
        ProfileAdoption::open(
            paths,
            &error);
    QVERIFY2(
        adoption.has_value(),
        qPrintable(error));
    QCOMPARE(
        adoption->state(),
        ProfileAdoption::State::Committed);
    QVERIFY(adoption->snapshot().sourceKindRecorded);
    QCOMPARE(
        adoption->snapshot().sourceKind,
        ProfilePaths::Kind::LegacyLocal);

    QVERIFY(
        QFileInfo::exists(
            QDir(paths.adoptionBackupRoot())
                .filePath(
                    QStringLiteral(
                        "personal-state.json"))));

    verifyMachineSentinels(fixture.legacy);

    QFile mediaRead(mediaPath);
    QVERIFY(mediaRead.open(QIODevice::ReadOnly));
    QCOMPARE(
        mediaRead.readAll(),
        QByteArrayLiteral("MEDIA-SENTINEL"));
}

void tst_account_adoption::
cleanRestartKeepsCommittedAdoption() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    const ProfilePaths paths =
        fixture.accountPaths();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto adoption =
            ProfileAdoption::open(
                paths,
                &error);
        QVERIFY2(
            adoption.has_value(),
            qPrintable(error));
        QCOMPARE(
            adoption->state(),
            ProfileAdoption::State::Committed);
    }

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto adoption =
            ProfileAdoption::open(
                paths,
                &error);
        QVERIFY2(
            adoption.has_value(),
            qPrintable(error));
        QCOMPARE(
            adoption->state(),
            ProfileAdoption::State::Committed);

        QCOMPARE(
            runtime.activeProfile().profileId(),
            paths.profileId());
    }
}

void tst_account_adoption::
committedAccountSessionMergesResidualLocalOnlyState() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    const ProfilePaths paths =
        fixture.accountPaths();
    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }
    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto adoption =
            ProfileAdoption::open(
                paths,
                &error);
        QVERIFY2(
            adoption.has_value(),
            qPrintable(error));
        QCOMPARE(
            adoption->state(),
            ProfileAdoption::State::Committed);

        PersonalStateSnapshot residual;
        residual.progressEntries.insert(
            QStringLiteral("movie\x1fresidual-local-movie"),
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("residual-local-movie")},
                {QStringLiteral("kind"), QStringLiteral("movie")},
                {QStringLiteral("progress"), 0.6},
                {QStringLiteral("updatedAt"), 1720000006000.0}});

        const ProfilePaths localPaths =
            ProfilePaths::localOnly(fixture.appDataRoot);
        const auto localStorage =
            LegacyPersonalStateStorage::forProfile(
                localPaths,
                &error);
        QVERIFY2(
            localStorage.has_value(),
            qPrintable(error));
        QVERIFY2(
            localStorage->restorePersonalState(
                residual,
                &error),
            qPrintable(error));

        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto accountStorage =
            LegacyPersonalStateStorage::forProfile(
                paths,
                &error);
        QVERIFY2(
            accountStorage.has_value(),
            qPrintable(error));
        const auto merged =
            accountStorage->capture(&error);
        QVERIFY2(
            merged.has_value(),
            qPrintable(error));
        // A committed account with a pending attachment receipt does not
        // consume a second source opportunistically.  The residual source
        // remains available for its own explicit adoption/attachment pass.
        QVERIFY(!merged->progressEntries.contains(
            QStringLiteral("movie\x1fresidual-local-movie")));

        const auto localAfter =
            localStorage->capture(&error);
        QVERIFY2(
            localAfter.has_value(),
            qPrintable(error));
        QVERIFY(!localAfter->isEmpty());
        QVERIFY(QFileInfo::exists(paths.cloudAttachmentReceiptPath()));
    }
}

void tst_account_adoption::
ordinarySignInAdoptsLegacyLocalState() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const ProfilePaths paths =
        fixture.accountPaths();
    const auto legacyAfter =
        fixture.legacy.capture(&error);
    QVERIFY2(
        legacyAfter.has_value(),
        qPrintable(error));
    QCOMPARE(legacyAfter->semanticDigest(), source.semanticDigest());
    QVERIFY(QFileInfo::exists(paths.cloudAttachmentReceiptPath()));

    const auto profileStorage =
        LegacyPersonalStateStorage::forProfile(
            paths,
            &error);
    QVERIFY2(
        profileStorage.has_value(),
        qPrintable(error));

    const auto accountState =
        profileStorage->capture(&error);
    QVERIFY2(
        accountState.has_value(),
        qPrintable(error));
    QCOMPARE(
        accountState->semanticDigest(),
        source.semanticDigest());
    QVERIFY(QFileInfo::exists(paths.adoptionJournalPath()));
}

void tst_account_adoption::
R8_existing_account_merge_preserves_terminal_receipt() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot accountState = populatedSnapshot();
    PersonalStateSnapshot localState = populatedSnapshot();
    localState.progressEntries.insert(
        QStringLiteral("movie\x1fmovie-2"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("movie-2")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("progress"), 0.8},
            {QStringLiteral("updatedAt"), 1720000003000.0}});
    localState.collectionEntries.insert(
        QStringLiteral("Theatre\x1fmovie-2"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("movie-2")},
            {QStringLiteral("world"), QStringLiteral("Theatre")},
            {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("Fixture Movie")}});

    const ProfilePaths localPaths =
        ProfilePaths::localOnly(fixture.appDataRoot);
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(localPaths);
    QVERIFY(localStorage.has_value());
    QVERIFY(localStorage->restorePersonalState(localState));

    const ProfilePaths accountPaths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(accountPaths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(accountPaths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(accountState));

    const auto localBinding = privateBinding(*localStorage);
    const auto accountBinding = privateBinding(accountPaths);
    const QString sharedOperationId =
        QStringLiteral("3d62aa0a-8b9d-4e2b-8fa7-4104abbbad55");
    QVERIFY(seedPrivateDeliveryState(
        localBinding, localStorage->ratingsReviewsPath(), sharedOperationId,
        QStringLiteral("pending"), 1));
    QVERIFY(seedPrivateDeliveryState(
        accountBinding, accountStorage->ratingsReviewsPath(), sharedOperationId,
        QStringLiteral("inFlight"), 1, QStringLiteral("succeeded")));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime, fixture.appDataRoot, {}, {},
        RatingsReviewsPrivateAdoptionCallbacks{
            [](const RatingsReviewsPrivateProfileBinding &sourceBinding,
               const RatingsReviewsPrivateProfileBinding &destinationBinding,
               RatingsReviewsStore *destinationCanonical,
               QString *handoffError) {
                return RatingsReviewsDelivery::handoffPrivateState(
                    sourceBinding, destinationBinding, destinationCanonical,
                    handoffError);
            }});

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);

    const auto merged =
        accountStorage->capture(&error);
    QVERIFY2(merged.has_value(), qPrintable(error));
    QVERIFY(merged->progressEntries.contains(QStringLiteral("movie\x1fmovie-2")));
    QVERIFY(merged->collectionEntries.contains(QStringLiteral("Theatre\x1fmovie-2")));

    const auto localAfter =
        localStorage->capture(&error);
    QVERIFY2(localAfter.has_value(), qPrintable(error));
    QVERIFY(!localAfter->isEmpty());
    QVERIFY(QFileInfo::exists(accountPaths.cloudAttachmentReceiptPath()));

    RatingsReviewsDeliveryOutbox mergedOutbox(
        accountPaths.ratingsReviewsDeliveryOutboxPath());
    QCOMPARE(mergedOutbox.operations().size(), 1);
    QCOMPARE(mergedOutbox.operation(sharedOperationId)->state,
             QStringLiteral("succeeded"));
    RatingsReviewsDeliveryReceiptStore mergedReceipts(
        accountPaths.ratingsReviewsDeliveryReceiptsPath());
    QCOMPARE(mergedReceipts.receipts().size(), 1);
    QCOMPARE(mergedReceipts.receipt(sharedOperationId)->status,
             QStringLiteral("succeeded"));
    RatingsReviewsDeliveryOutbox localOutbox(
        localBinding.privatePaths.outboxPath);
    QCOMPARE(localOutbox.operation(sharedOperationId)->state,
             QStringLiteral("pending"));
}

void tst_account_adoption::
activeAccountSessionMergesLaterLocalOnlyState() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot accountState = populatedSnapshot();
    const ProfilePaths accountPaths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(accountPaths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(accountPaths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(accountState));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);

    PersonalStateSnapshot laterLocalState;
    laterLocalState.progressEntries.insert(
        QStringLiteral("movie\x1flate-local-movie"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("late-local-movie")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("progress"), 0.7},
            {QStringLiteral("updatedAt"), 1720000004000.0}});
    laterLocalState.collectionEntries.insert(
        QStringLiteral("Theatre\x1flate-local-movie"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("late-local-movie")},
            {QStringLiteral("world"), QStringLiteral("Theatre")},
            {QStringLiteral("type"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("Later Local Movie")} });

    const ProfilePaths localPaths =
        ProfilePaths::localOnly(fixture.appDataRoot);
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(localPaths);
    QVERIFY(localStorage.has_value());
    QVERIFY(localStorage->restorePersonalState(laterLocalState));

    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const auto merged = accountStorage->capture(&error);
    QVERIFY2(merged.has_value(), qPrintable(error));
    QVERIFY(merged->progressEntries.contains(
        QStringLiteral("movie\x1flate-local-movie")));
    QVERIFY(merged->collectionEntries.contains(
        QStringLiteral("Theatre\x1flate-local-movie")));

    const auto localAfter = localStorage->capture(&error);
    QVERIFY2(localAfter.has_value(), qPrintable(error));
    QVERIFY(!localAfter->isEmpty());
    QVERIFY(QFileInfo::exists(accountPaths.cloudAttachmentReceiptPath()));
}

void tst_account_adoption::
rememberedAccountSessionMergesLaterLocalOnlyState() {
    AdoptionFixture fixture;
    const ProfilePaths accountPaths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(accountPaths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(accountPaths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(populatedSnapshot()));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareLocalOnly(&error),
        qPrintable(error));

    PersonalStateSnapshot laterLocalState;
    laterLocalState.progressEntries.insert(
        QStringLiteral("movie\x1fremembered-local-movie"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("remembered-local-movie")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("progress"), 0.4},
            {QStringLiteral("updatedAt"), 1720000005000.0}});

    const ProfilePaths localPaths =
        ProfilePaths::localOnly(fixture.appDataRoot);
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(localPaths);
    QVERIFY(localStorage.has_value());
    QVERIFY(localStorage->restorePersonalState(laterLocalState));

    QVERIFY2(
        coordinator.prepareRememberedAccount(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const auto merged = accountStorage->capture(&error);
    QVERIFY2(merged.has_value(), qPrintable(error));
    QVERIFY(merged->progressEntries.contains(
        QStringLiteral("movie\x1fremembered-local-movie")));

    const auto localAfter = localStorage->capture(&error);
    QVERIFY2(localAfter.has_value(), qPrintable(error));
    QVERIFY(!localAfter->isEmpty());
    QVERIFY(QFileInfo::exists(accountPaths.cloudAttachmentReceiptPath()));
}

void tst_account_adoption::
coldStartTrackerOnlyLocalOnlyProfileRetriesPrivateHandoff() {
    AdoptionFixture fixture;
    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    int handoffCalls = 0;
    QString handedOffSourceId;
    QString handedOffDestinationId;
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot,
        {},
        {},
        {},
        TrackerPrivateAdoptionCallbacks{
            [&](const ProfilePaths &source,
                const ProfilePaths &destination,
                QString *handoffError) {
                ++handoffCalls;
                handedOffSourceId = source.profileId();
                handedOffDestinationId = destination.profileId();
                if (handoffCalls == 1) {
                    if (handoffError)
                        *handoffError = QStringLiteral(
                            "Simulated tracker credential transfer failure.");
                    return false;
                }
                return true;
            }});

    QString error;
    const ProfilePaths local = ProfilePaths::localOnly(fixture.appDataRoot);
    TrackerConnectionStore localConnections(local);
    QVERIFY(localConnections.upsert({TrackerProviderId::Simkl, QStringLiteral("local-account"),
        1, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));

    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::Sealed);
    QVERIFY(!coordinator.prepareAccountSession(
        QString::fromLatin1(kAccountA), &error));
    QCOMPARE(handoffCalls, 1);
    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::LocalOnly);
    QCOMPARE(localConnections.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    auto pendingAdoption = ProfileAdoption::open(fixture.accountPaths(), &error);
    QVERIFY2(pendingAdoption.has_value(), qPrintable(error));
    QCOMPARE(pendingAdoption->state(), ProfileAdoption::State::Promoted);

    error.clear();
    QVERIFY2(coordinator.prepareAccountSession(
                 QString::fromLatin1(kAccountA), &error),
             qPrintable(error));
    QCOMPARE(handoffCalls, 2);
    QCOMPARE(handedOffSourceId, local.profileId());
    QCOMPARE(handedOffDestinationId, fixture.accountPaths().profileId());
    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::Account);
}

void tst_account_adoption::
continueLocalBeforeAdoptionKeepsLegacyAuthority() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareLocalOnly(
            &error),
        qPrintable(error));

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::LegacyLocal);

    const auto legacyAfter =
        fixture.legacy.capture(&error);
    QVERIFY2(
        legacyAfter.has_value(),
        qPrintable(error));
    QCOMPARE(
        legacyAfter->semanticDigest(),
        source.semanticDigest());
}

void tst_account_adoption::
continueLocalAfterAdoptionUsesDedicatedLocalProfile() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareCreatedAccount(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    QVERIFY2(
        coordinator.prepareLocalOnly(
            &error),
        qPrintable(error));

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::LocalOnly);

    const ProfilePaths local =
        ProfilePaths::localOnly(
            fixture.appDataRoot);
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(
            local,
            &error);
    QVERIFY2(
        localStorage.has_value(),
        qPrintable(error));

    const auto localState =
        localStorage->capture(&error);
    QVERIFY2(
        localState.has_value(),
        qPrintable(error));
    QVERIFY(localState->isEmpty());

    const ProfilePaths account =
        fixture.accountPaths();
    const auto legacyState =
        fixture.legacy.capture(&error);
    QVERIFY2(
        legacyState.has_value(),
        qPrintable(error));
    QCOMPARE(legacyState->semanticDigest(), source.semanticDigest());
    QVERIFY(QFileInfo::exists(account.cloudAttachmentReceiptPath()));
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(
            account,
            &error);
    QVERIFY2(
        accountStorage.has_value(),
        qPrintable(error));

    const auto accountState =
        accountStorage->capture(&error);
    QVERIFY2(
        accountState.has_value(),
        qPrintable(error));
    QCOMPARE(
        accountState->semanticDigest(),
        source.semanticDigest());
}

void tst_account_adoption::
corruptRestartPreservesAccountAndEvidence() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    const ProfilePaths paths =
        fixture.accountPaths();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }

    // Force the old post-quarantine verification boundary so this fixture
    // exercises restart validation against a damaged active profile.  The
    // committed production path no longer routes through this state after a
    // successful activation.
    QString error;
    QVERIFY2(
        rewriteAdoptionState(
            paths,
            QStringLiteral("legacy_quarantined"),
            false,
            &error),
        qPrintable(error));

    QSettings corrupted(
        paths.collectionIniPath(),
        QSettings::IniFormat);
    corrupted.setValue(
        QStringLiteral("collection/entries"),
        QByteArrayLiteral("not-json"));
    corrupted.sync();
    QCOMPARE(
        corrupted.status(),
        QSettings::NoError);

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QVERIFY(
        !coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error));
    QVERIFY(!error.isEmpty());

    const auto restored =
        fixture.legacy.capture(&error);
    QVERIFY2(
        restored.has_value(),
        qPrintable(error));
    QCOMPARE(restored->semanticDigest(), source.semanticDigest());
    QVERIFY(QFileInfo::exists(paths.cloudAttachmentReceiptPath()));

    QVERIFY(
        QFileInfo::exists(paths.profileRoot()));
    QCOMPARE(
        corrupted.value(
            QStringLiteral("collection/entries"))
            .toByteArray(),
        QByteArrayLiteral("not-json"));
    QVERIFY(
        QFileInfo::exists(
            QDir(paths.adoptionBackupRoot())
                .filePath(QStringLiteral("personal-state.json"))));

    const auto adoption =
        ProfileAdoption::open(
            paths,
            &error);
    QVERIFY2(
        adoption.has_value(),
        qPrintable(error));
    QCOMPARE(
        adoption->state(),
        ProfileAdoption::State::LegacyQuarantined);

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Sealed);
}

void tst_account_adoption::
missingFinalStorePreservesAccountEvidence() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source = populatedSnapshot();
    QVERIFY(fixture.legacy.restorePersonalState(source));
    const ProfilePaths paths = fixture.accountPaths();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);
        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }

    QString error;
    QVERIFY2(
        rewriteAdoptionState(
            paths,
            QStringLiteral("legacy_quarantined"),
            false,
            &error),
        qPrintable(error));
    QVERIFY(QFile::remove(paths.collectionIniPath()));

    ProfileStoreRuntime restartedRuntime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator restartedCoordinator(
        &restartedRuntime,
        fixture.appDataRoot);
    QVERIFY(
        !restartedCoordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(QFileInfo::exists(paths.profileRoot()));
    QVERIFY(!QFileInfo::exists(paths.collectionIniPath()));
    QVERIFY(QFileInfo::exists(
        QDir(paths.adoptionBackupRoot())
            .filePath(QStringLiteral("personal-state.json"))));
    const auto legacy = fixture.legacy.capture(&error);
    QVERIFY2(legacy.has_value(), qPrintable(error));
    QCOMPARE(legacy->semanticDigest(), source.semanticDigest());
    QVERIFY(QFileInfo::exists(paths.cloudAttachmentReceiptPath()));
    const auto adoption = ProfileAdoption::open(paths, &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QCOMPARE(adoption->state(), ProfileAdoption::State::LegacyQuarantined);
}

void tst_account_adoption::
retryIntentReAdoptsOnLaterSignIn() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    const ProfilePaths paths =
        fixture.accountPaths();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }

    QString error;
    QVERIFY2(
        fixture.legacy.restorePersonalState(
            source,
            &error),
        qPrintable(error));
    QVERIFY2(
        rewriteAdoptionState(
            paths,
            QStringLiteral("retry_pending"),
            false,
            &error),
        qPrintable(error));

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));

        const auto adoption =
            ProfileAdoption::open(
                paths,
                &error);
        QVERIFY2(
            adoption.has_value(),
            qPrintable(error));
        QCOMPARE(
            adoption->state(),
            ProfileAdoption::State::Committed);
    }
}

void tst_account_adoption::
directAccountSwitchRequiresSealing() {
    AdoptionFixture fixture;

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    error.clear();
    QVERIFY(
        !coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountB),
            &error));
    QVERIFY(
        error.contains(
            QStringLiteral("sealed")));

    QCOMPARE(
        runtime.activeProfile().profileId(),
        fixture.accountPaths().profileId());
}

void tst_account_adoption::
existingAccountMergeAcceptsCompletedActivity() {
    AdoptionFixture fixture;
    QVERIFY(fixture.legacy.restorePersonalState(populatedSnapshot()));

    const ProfilePaths paths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(paths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(paths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(PersonalStateSnapshot{}));

    {
        ActivityStore legacyActivity(fixture.legacy.activityDbPath());
        QVERIFY(legacyActivity.healthy());
        QVERIFY(legacyActivity.recordCompletion(fixtureMovieCompletionFact()));
    }

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(&runtime, fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(QString::fromLatin1(kAccountA), &error),
        qPrintable(error));

    ActivityStore mergedActivity(paths.activityDbPath());
    QVERIFY(mergedActivity.healthy());
    const QList<QVariantMap> facts = mergedActivity.historyProjectionFacts();
    QCOMPARE(facts.size(), 1);
    QCOMPARE(facts.first().value(QStringLiteral("type")).toString(),
             QStringLiteral("media_completed"));
}

void tst_account_adoption::
    activityOnlyLocalStateIsMergedIntoExistingAccount() {
    AdoptionFixture fixture;

    const ProfilePaths paths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(paths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(paths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(PersonalStateSnapshot{}));

    {
        ActivityStore legacyActivity(fixture.legacy.activityDbPath());
        QVERIFY(legacyActivity.healthy());
        QVERIFY(legacyActivity.recordPlaybackDelta(fixtureMovieFact()));
    }

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(&runtime, fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(QString::fromLatin1(kAccountA), &error),
        qPrintable(error));

    ActivityStore mergedActivity(paths.activityDbPath());
    QVERIFY(mergedActivity.healthy());
    const QList<QVariantMap> facts = mergedActivity.historyProjectionFacts();
    QCOMPARE(facts.size(), 1);
    QCOMPARE(
        facts.first().value(QStringLiteral("type")).toString(),
        QStringLiteral("playback_delta"));
}

void tst_account_adoption::
    existingCachedAccountKeepsSourceForAttachment() {
    AdoptionFixture fixture;
    QVERIFY(fixture.legacy.restorePersonalState(populatedSnapshot()));

    const PersonalStateSnapshot before =
        *fixture.legacy.capture();
    const ProfilePaths paths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(paths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(paths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(PersonalStateSnapshot{}));

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(&runtime, fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(QString::fromLatin1(kAccountA), &error),
        qPrintable(error));

    const auto after = fixture.legacy.capture(&error);
    QVERIFY2(after.has_value(), qPrintable(error));
    QVERIFY(after->matchesSemanticDigest(before.semanticDigest()));
    QVERIFY2(
        QFileInfo::exists(paths.cloudAttachmentReceiptPath()),
        "Existing cached-account adoption must leave a durable attachment receipt.");

    const auto firstReceipt = AccountAttachmentReceipt::read(paths);
    QCOMPARE(
        firstReceipt.status,
        AccountAttachmentReceipt::ReadStatus::Ok);
    const QString attachmentId = firstReceipt.data.attachmentId;
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    const auto repeatedReceipt = AccountAttachmentReceipt::read(paths);
    QCOMPARE(
        repeatedReceipt.status,
        AccountAttachmentReceipt::ReadStatus::Ok);
    QCOMPARE(repeatedReceipt.data.attachmentId, attachmentId);
    QCOMPARE(
        repeatedReceipt.data.sourceSemanticDigest,
        firstReceipt.data.sourceSemanticDigest);
}

void tst_account_adoption::
    activityOnlyExistingAccountKeepsSourceForAttachment() {
    AdoptionFixture fixture;

    const ProfilePaths paths = fixture.accountPaths();
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(paths);
    QVERIFY(accountStorage.has_value());
    QVERIFY(QDir().mkpath(paths.profileRoot()));
    QVERIFY(accountStorage->restorePersonalState(PersonalStateSnapshot{}));

    {
        ActivityStore legacyActivity(fixture.legacy.activityDbPath());
        QVERIFY(legacyActivity.healthy());
        QVERIFY(legacyActivity.recordPlaybackDelta(fixtureMovieFact()));
    }

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(&runtime, fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareAccountSession(QString::fromLatin1(kAccountA), &error),
        qPrintable(error));

    ActivityStore sourceAfter(fixture.legacy.activityDbPath());
    QVERIFY(sourceAfter.healthy());
    QVERIFY2(
        activityContainsItem(sourceAfter, QStringLiteral("adoption-fixture-movie")),
        "Activity-only adoption must retain the source ledger for cloud attachment.");
    QVERIFY2(
        QFileInfo::exists(paths.cloudAttachmentReceiptPath()),
        "Activity-only adoption must leave a durable attachment receipt.");
}

void tst_account_adoption::
firstAccountAdoptionMigratesActivityLedger() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    // Seed a legacy activity ledger the way a real legacy-local session
    // would accumulate one: write directly at the legacy activity path,
    // then let the store close cleanly (scope exit) before adoption runs.
    {
        ActivityStore legacyActivity(
            fixture.legacy.activityDbPath());
        QVERIFY(legacyActivity.healthy());
        QVERIFY2(
            legacyActivity.recordPlaybackDelta(
                fixtureMovieFact()),
            "seeding the legacy activity fact should succeed");
    }

    const QString expectedActivityDigest =
        ActivityStore::fileDigestSha256(
            fixture.legacy.activityDbPath());
    QVERIFY(!expectedActivityDigest.isEmpty());

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QString error;
    QVERIFY2(
        coordinator.prepareCreatedAccount(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const ProfilePaths paths =
        fixture.accountPaths();

    // The source ledger remains byte-identical until the attachment
    // coordinator receives cloud proof and performs exact retirement.
    QVERIFY(
        QFileInfo::exists(
            fixture.legacy.activityDbPath()));
    QCOMPARE(
        ActivityStore::fileDigestSha256(
            fixture.legacy.activityDbPath()),
        expectedActivityDigest);

    // The promoted profile's activity ledger is a byte-identical copy.
    QCOMPARE(
        ActivityStore::fileDigestSha256(
            paths.activityDbPath()),
        expectedActivityDigest);

    // A rollback backup of the activity ledger exists alongside the
    // existing personal-state.json backup and matches too.
    const QString backupActivityPath =
        QDir(paths.adoptionBackupRoot())
            .filePath(
                QStringLiteral("activity.sqlite"));
    QVERIFY(
        QFileInfo::exists(backupActivityPath));
    QCOMPARE(
        ActivityStore::fileDigestSha256(
            backupActivityPath),
        expectedActivityDigest);

    // The migrated fact is readable and semantically intact through the
    // real ActivityStore API, not just byte-identical on disk.
    ActivityStore promotedActivity(
        paths.activityDbPath());
    QVERIFY(promotedActivity.healthy());
    const QString monthKey =
        promotedActivity.earliestActivityMonth();
    QVERIFY(!monthKey.isEmpty());
    const QVariantMap projection =
        promotedActivity.projectMonth(monthKey);
    QCOMPARE(
        projection.value(
            QStringLiteral("watchSeconds"))
            .toLongLong(),
        qint64(30));
}

void tst_account_adoption::
interruptedAdoptionPreservesAccountActivityEvidence() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source =
        populatedSnapshot();
    QVERIFY(
        fixture.legacy.restorePersonalState(
            source));

    {
        ActivityStore legacyActivity(
            fixture.legacy.activityDbPath());
        QVERIFY(legacyActivity.healthy());
        QVERIFY2(
            legacyActivity.recordPlaybackDelta(
                fixtureMovieFact()),
            "seeding the legacy activity fact should succeed");
    }

    const QString expectedActivityDigest =
        ActivityStore::fileDigestSha256(
            fixture.legacy.activityDbPath());
    QVERIFY(!expectedActivityDigest.isEmpty());

    const ProfilePaths paths =
        fixture.accountPaths();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }

    QString error;
    QVERIFY2(
        rewriteAdoptionState(
            paths,
            QStringLiteral("legacy_quarantined"),
            false,
            &error),
        qPrintable(error));

    // Damage the active account ledger after promotion.  Restart validation
    // must fail closed while keeping the account file and rollback backup;
    // restoring the pre-account ledger would overwrite private account
    // history and is no longer an allowed recovery action.
    QFile corrupted(paths.activityDbPath());
    QVERIFY(corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(
        corrupted.write(QByteArrayLiteral("not-a-sqlite-database")),
        qint64(21));
    corrupted.close();

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QVERIFY(
            !coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error));
        QVERIFY(!error.isEmpty());
    }

    // The source ledger remains untouched, and both active account evidence
    // and the rollback backup remain available for repair.
    QVERIFY(QFileInfo::exists(fixture.legacy.activityDbPath()));
    QCOMPARE(
        ActivityStore::fileDigestSha256(
            fixture.legacy.activityDbPath()),
        expectedActivityDigest);
    QVERIFY(QFileInfo::exists(paths.profileRoot()));
    QCOMPARE(
        QFileInfo(paths.activityDbPath()).size(),
        qint64(21));
    QVERIFY(QFileInfo::exists(
        QDir(paths.adoptionBackupRoot())
            .filePath(QStringLiteral("activity.sqlite"))));

    QString adoptError;
    const auto adoption =
        ProfileAdoption::open(
            paths,
            &adoptError);
    QVERIFY2(
        adoption.has_value(),
        qPrintable(adoptError));
    QCOMPARE(
        adoption->state(),
        ProfileAdoption::State::LegacyQuarantined);
}

void tst_account_adoption::
postAdoptionWritesSurviveCleanRestart() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source = populatedSnapshot();
    QVERIFY(fixture.legacy.restorePersonalState(source));
    {
        ActivityStore activity(fixture.legacy.activityDbPath());
        QVERIFY(activity.recordPlaybackDelta(fixtureMovieFact()));
        QVERIFY(activity.checkpointForSafeCopy());
    }

    const ProfilePaths paths = fixture.accountPaths();
    const QVariantMap postAdoptionProgress{
        {QStringLiteral("id"), QStringLiteral("post-adoption-movie")},
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("progress"), 0.35},
        {QStringLiteral("caption"), QStringLiteral("Post adoption")}};

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
        QVERIFY(runtime.progressStore());
        runtime.progressStore()->record(postAdoptionProgress);
        QVERIFY(runtime.activityStore());
        QVERIFY(runtime.activityStore()->recordCompletion(
            fixtureMovieCompletionFact()));
        runtime.flushPersonalStores();
    }

    ProfileStoreRuntime restartedRuntime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator restartedCoordinator(
        &restartedRuntime,
        fixture.appDataRoot);
    QString error;
    QVERIFY2(
        restartedCoordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QCOMPARE(
        restartedRuntime.activeProfile().kind(),
        ProfilePaths::Kind::Account);

    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(paths, &error);
    QVERIFY2(accountStorage.has_value(), qPrintable(error));
    const auto accountState = accountStorage->capture(&error);
    QVERIFY2(accountState.has_value(), qPrintable(error));
    QVERIFY(accountState->progressEntries.contains(
        QStringLiteral("video\x1fpost-adoption-movie")));

    ActivityStore activity(paths.activityDbPath());
    QVERIFY(activity.healthy());
    QVERIFY(activityContainsItem(
        activity,
        QStringLiteral("adoption-fixture-movie")));
    bool completionFound = false;
    for (const QVariantMap &fact : activity.historyProjectionFacts()) {
        if (fact.value(QStringLiteral("eventId")).toString()
            == QString::fromLatin1(
                "cccccccc-cccc-4ccc-8ccc-cccccccccccc")) {
            completionFound = true;
            break;
        }
    }
    QVERIFY(completionFound);

    const auto adoption = ProfileAdoption::open(paths, &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QCOMPARE(adoption->state(), ProfileAdoption::State::Committed);
    QVERIFY(adoption->snapshot().sourceKindRecorded);
    QCOMPARE(
        adoption->snapshot().sourceKind,
        ProfilePaths::Kind::LegacyLocal);
}

void tst_account_adoption::
legacyQuarantinedRestartPreservesAccountWrites() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source = populatedSnapshot();
    QVERIFY(fixture.legacy.restorePersonalState(source));
    {
        ActivityStore activity(fixture.legacy.activityDbPath());
        QVERIFY(activity.recordPlaybackDelta(fixtureMovieFact()));
        QVERIFY(activity.checkpointForSafeCopy());
    }

    const ProfilePaths paths = fixture.accountPaths();
    const QVariantMap postAdoptionProgress{
        {QStringLiteral("id"), QStringLiteral("legacy-journal-movie")},
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("progress"), 0.25}};

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);

        QString error;
        QVERIFY2(
            coordinator.prepareCreatedAccount(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
        runtime.progressStore()->record(postAdoptionProgress);
        QVERIFY(runtime.activityStore()->recordCompletion(
            fixtureMovieCompletionFact()));
        runtime.flushPersonalStores();
    }

    QString error;
    // The old journal state below represents a post-attachment restart.  In
    // the current lifecycle the source is cleared only after cloud proof, so
    // model that verified boundary explicitly before replaying the legacy
    // quarantine state.
    QVERIFY2(
        fixture.legacy.clearPersonalState(&error),
        qPrintable(error));
    QVERIFY(
        !QFileInfo::exists(fixture.legacy.activityDbPath())
        || QFile::remove(fixture.legacy.activityDbPath()));
    QVERIFY2(
        rewriteAdoptionState(
            paths,
            QStringLiteral("legacy_quarantined"),
            true,
            &error),
        qPrintable(error));

    ProfileStoreRuntime restartedRuntime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator restartedCoordinator(
        &restartedRuntime,
        fixture.appDataRoot);
    QVERIFY2(
        restartedCoordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(paths, &error);
    QVERIFY2(accountStorage.has_value(), qPrintable(error));
    const auto accountState = accountStorage->capture(&error);
    QVERIFY2(accountState.has_value(), qPrintable(error));
    QVERIFY(accountState->progressEntries.contains(
        QStringLiteral("video\x1flegacy-journal-movie")));

    ActivityStore activity(paths.activityDbPath());
    QVERIFY(activity.healthy());
    QVERIFY(activityContainsItem(
        activity,
        QStringLiteral("adoption-fixture-movie")));
    bool completionFound = false;
    for (const QVariantMap &fact : activity.historyProjectionFacts()) {
        if (fact.value(QStringLiteral("eventId")).toString()
            == QString::fromLatin1(
                "cccccccc-cccc-4ccc-8ccc-cccccccccccc")) {
            completionFound = true;
            break;
        }
    }
    QVERIFY(completionFound);
    QVERIFY(!QFileInfo::exists(fixture.legacy.activityDbPath()));

    const auto adoption = ProfileAdoption::open(paths, &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QCOMPARE(adoption->state(), ProfileAdoption::State::Committed);
    // This fixture deliberately rewrites the journal without source_kind to
    // exercise the v1 compatibility path.  Recovery preserves that older
    // journal shape while still committing the account safely.
    QVERIFY(!adoption->snapshot().sourceKindRecorded);
}

void tst_account_adoption::
explicitLocalQuarantineIgnoresUnrelatedLegacyState() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source = populatedSnapshot();
    const ProfilePaths localPaths =
        ProfilePaths::localOnly(fixture.appDataRoot);
    const ProfilePaths paths = fixture.accountPaths();
    QString error;
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(
            localPaths,
            &error);
    QVERIFY2(localStorage.has_value(), qPrintable(error));
    QVERIFY2(
        localStorage->restorePersonalState(source, &error),
        qPrintable(error));

    {
        ProfileStoreRuntime runtime(
            fixture.legacy,
            fixture.appDataRoot);
        QVERIFY2(
            runtime.activateLocalOnlyProfile(&error),
            qPrintable(error));
        FirstAccountProfileCoordinator coordinator(
            &runtime,
            fixture.appDataRoot);
        QVERIFY2(
            coordinator.prepareAccountSession(
                QString::fromLatin1(kAccountA),
                &error),
            qPrintable(error));
    }

    // Simulate an unrelated legacy profile being written while the explicit
    // local source is already quarantined.  Recovery must consult the source
    // kind in the journal and leave this legacy state untouched.
    const auto localBefore = localStorage->capture(&error);
    QVERIFY2(localBefore.has_value(), qPrintable(error));
    QVERIFY(localBefore->isEmpty());
    QVERIFY2(
        fixture.legacy.restorePersonalState(source, &error),
        qPrintable(error));
    QVERIFY2(
        rewriteAdoptionState(
            paths,
            QStringLiteral("legacy_quarantined"),
            false,
            &error),
        qPrintable(error));

    ProfileStoreRuntime restartedRuntime(
        fixture.legacy,
        fixture.appDataRoot);
    FirstAccountProfileCoordinator restartedCoordinator(
        &restartedRuntime,
        fixture.appDataRoot);
    QVERIFY2(
        restartedCoordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));
    QCOMPARE(
        restartedRuntime.activeProfile().kind(),
        ProfilePaths::Kind::Account);

    const auto localAfter = localStorage->capture(&error);
    QVERIFY2(localAfter.has_value(), qPrintable(error));
    QVERIFY(localAfter->isEmpty());
    const auto legacyAfter = fixture.legacy.capture(&error);
    QVERIFY2(legacyAfter.has_value(), qPrintable(error));
    QCOMPARE(legacyAfter->semanticDigest(), source.semanticDigest());

    const auto adoption = ProfileAdoption::open(paths, &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QCOMPARE(adoption->state(), ProfileAdoption::State::Committed);
    QVERIFY(adoption->snapshot().sourceKindRecorded);
    QCOMPARE(
        adoption->snapshot().sourceKind,
        ProfilePaths::Kind::LocalOnly);
}

void tst_account_adoption::
explicitLocalPreparingAdoptionResumesFromLocalSource() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source = populatedSnapshot();
    const ProfilePaths paths = fixture.accountPaths();
    const ProfilePaths localPaths =
        ProfilePaths::localOnly(fixture.appDataRoot);
    QString error;
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(localPaths, &error);
    QVERIFY2(localStorage.has_value(), qPrintable(error));
    QVERIFY2(
        localStorage->restorePersonalState(source, &error),
        qPrintable(error));

    // This fixture intentionally leaves a v1-style journal without source_kind:
    // recovery must identify the sole matching explicit-local source rather than
    // reading the unrelated legacyStorage() location.
    auto adoption =
        ProfileAdoption::begin(paths, source.semanticDigest(), &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    QVERIFY2(
        runtime.activateLocalOnlyProfile(&error),
        qPrintable(error));
    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);
    QVERIFY2(
        coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error),
        qPrintable(error));

    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::Account);
    const auto accountStorage =
        LegacyPersonalStateStorage::forProfile(paths, &error);
    QVERIFY2(accountStorage.has_value(), qPrintable(error));
    const auto accountState = accountStorage->capture(&error);
    QVERIFY2(accountState.has_value(), qPrintable(error));
    QCOMPARE(accountState->semanticDigest(), source.semanticDigest());

    const auto localAfter = localStorage->capture(&error);
    QVERIFY2(localAfter.has_value(), qPrintable(error));
    QVERIFY(localAfter->isEmpty());
    const auto legacyAfter = fixture.legacy.capture(&error);
    QVERIFY2(legacyAfter.has_value(), qPrintable(error));
    QVERIFY(legacyAfter->isEmpty());
}

void tst_account_adoption::
retryFailsClosedWhenCompetingSourceUnreadable() {
    AdoptionFixture fixture;
    const PersonalStateSnapshot source = populatedSnapshot();
    const ProfilePaths paths = fixture.accountPaths();
    const ProfilePaths localPaths =
        ProfilePaths::localOnly(fixture.appDataRoot);
    QString error;
    const auto localStorage =
        LegacyPersonalStateStorage::forProfile(localPaths, &error);
    QVERIFY2(localStorage.has_value(), qPrintable(error));
    QVERIFY2(
        localStorage->restorePersonalState(source, &error),
        qPrintable(error));

    // Create an old retry journal without source_kind.  Recovery must inspect
    // both possible sources before selecting the one to retry.
    const auto adoption =
        ProfileAdoption::begin(paths, source.semanticDigest(), &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QVERIFY2(
        rewriteAdoptionState(
            paths,
            QStringLiteral("retry_pending"),
            true,
            &error),
        qPrintable(error));
    const auto retryJournal = ProfileAdoption::open(paths, &error);
    QVERIFY2(retryJournal.has_value(), qPrintable(error));
    QCOMPARE(
        retryJournal->state(),
        ProfileAdoption::State::RetryPending);

    ProfileStoreRuntime runtime(
        fixture.legacy,
        fixture.appDataRoot);
    QVERIFY2(
        runtime.activateLocalOnlyProfile(&error),
        qPrintable(error));

    // A malformed legacy value is unreadable evidence, not an empty legacy
    // profile.  Write it after the runtime leaves the legacy stores so its
    // flush cannot repair or replace the fixture before recovery inspects it.
    QSettings malformedLegacy(
        fixture.legacy.progressIniPath(),
        QSettings::IniFormat);
    malformedLegacy.setValue(
        QStringLiteral("continue/entries"),
        QByteArrayLiteral("{malformed"));
    malformedLegacy.sync();
    QString malformedError;
    const auto malformedSnapshot =
        fixture.legacy.capture(&malformedError);
    QVERIFY(!malformedSnapshot.has_value());

    FirstAccountProfileCoordinator coordinator(
        &runtime,
        fixture.appDataRoot);

    QVERIFY(
        !coordinator.prepareAccountSession(
            QString::fromLatin1(kAccountA),
            &error));
    QVERIFY(error.contains(QStringLiteral("inspect both")));
    QCOMPARE(
        runtime.activeProfile().kind(),
        ProfilePaths::Kind::LocalOnly);

    QString localError;
    const auto localAfter = localStorage->capture(&localError);
    QVERIFY2(localAfter.has_value(), qPrintable(localError));
    QCOMPARE(localAfter->semanticDigest(), source.semanticDigest());

    const auto retry = ProfileAdoption::open(paths, &error);
    QVERIFY2(retry.has_value(), qPrintable(error));
    QCOMPARE(retry->state(), ProfileAdoption::State::RetryPending);
    QVERIFY(!retry->snapshot().sourceKindRecorded);
}

void tst_account_adoption::
R8_localonly_private_handoff_before_source_clear() {
    AdoptionFixture fixture;
    const ProfilePaths paths = fixture.accountPaths();
    const ProfilePaths localPaths = ProfilePaths::localOnly(fixture.appDataRoot);
    QString error;
    const auto localStorage = LegacyPersonalStateStorage::forProfile(localPaths, &error);
    QVERIFY2(localStorage.has_value(), qPrintable(error));

    PersonalStateSnapshot source = populatedSnapshot();
    source.mainSyncProvider = QStringLiteral("stremio");
    source.stremioState = QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profileId"), localPaths.profileId()},
        {QStringLiteral("bindingGeneration"), QStringLiteral("7")},
        {QStringLiteral("accountId"), QStringLiteral("stremio-account-a")},
        {QStringLiteral("displayName"), QStringLiteral("Fixture Viewer")},
        {QStringLiteral("acknowledgedBaselines"), QJsonObject{}},
        {QStringLiteral("importRedoReceipts"), QJsonArray{}},
        {QStringLiteral("intentionalMembershipDifferences"), QJsonArray{}},
        {QStringLiteral("lastSuccessAtMs"), QStringLiteral("1720000040000")},
        {QStringLiteral("firstMergeComplete"), true},
        {QStringLiteral("reconnectRequired"), false},
        {QStringLiteral("pendingIntents"), QJsonArray{}}};
    source.theatreExtensions = QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("fixture.addon")},
        {QStringLiteral("transportUrl"), QStringLiteral("https://fixture.invalid/Private/manifest.json?Token=Case")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("core"), false},
        {QStringLiteral("manifest"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("fixture.addon")},
            {QStringLiteral("name"), QStringLiteral("Fixture Addon")},
            {QStringLiteral("types"), QJsonArray{QStringLiteral("movie")}}}}}};
    QVERIFY2(localStorage->restorePersonalState(source, &error), qPrintable(error));
    const auto restoredSource = localStorage->capture(&error);
    QVERIFY2(restoredSource.has_value(), qPrintable(error));
    QCOMPARE(restoredSource->mainSyncProvider, QStringLiteral("stremio"));
    QVERIFY(restoredSource->stremioState.value(QStringLiteral("profileId")).toString().isEmpty());
    QCOMPARE(restoredSource->stremioState.value(QStringLiteral("accountId")).toString(),
             QStringLiteral("stremio-account-a"));
    QCOMPARE(restoredSource->theatreExtensions, source.theatreExtensions);

    const auto localBinding = privateBinding(*localStorage);
    const QString operationId = QStringLiteral("4b474b6a-03ca-455b-bc28-8a3f0a27b903");
    RatingsReviewsDeliveryOperation localUnknown;
    QVERIFY(seedPrivateDeliveryState(
        localBinding, localStorage->ratingsReviewsPath(), operationId,
        QStringLiteral("unknownOutcome"), 1, {}, &localUnknown));
    QFile localOutboxFile(localBinding.privatePaths.outboxPath);
    QVERIFY(localOutboxFile.open(QIODevice::ReadOnly));
    const QByteArray localOutboxBytes = localOutboxFile.readAll();
    localOutboxFile.close();

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    QVERIFY2(runtime.activateLocalOnlyProfile(&error), qPrintable(error));
    FirstAccountProfileCoordinator coordinator(
        &runtime, fixture.appDataRoot, {}, {},
        RatingsReviewsPrivateAdoptionCallbacks{
            [](const RatingsReviewsPrivateProfileBinding &sourceBinding,
               const RatingsReviewsPrivateProfileBinding &destinationBinding,
               RatingsReviewsStore *destinationCanonical,
               QString *handoffError) {
                return RatingsReviewsDelivery::handoffPrivateState(
                    sourceBinding, destinationBinding, destinationCanonical,
                    handoffError);
            }});
    QVERIFY2(coordinator.prepareCreatedAccount(QString::fromLatin1(kAccountA), &error),
             qPrintable(error));

    const auto accountStorage = LegacyPersonalStateStorage::forProfile(paths, &error);
    QVERIFY2(accountStorage.has_value(), qPrintable(error));
    const auto adopted = accountStorage->capture(&error);
    QVERIFY2(adopted.has_value(), qPrintable(error));
    QCOMPARE(adopted->mainSyncProvider, QStringLiteral("stremio"));
    QVERIFY(adopted->stremioState.value(QStringLiteral("profileId")).toString().isEmpty());
    QCOMPARE(adopted->stremioState.value(QStringLiteral("accountId")).toString(),
             QStringLiteral("stremio-account-a"));
    QCOMPARE(adopted->theatreExtensions, source.theatreExtensions);
    QFile adoptedJournal(paths.stremioSyncStatePath());
    QVERIFY(adoptedJournal.open(QIODevice::ReadOnly));
    const QJsonDocument adoptedJournalDocument = QJsonDocument::fromJson(
        adoptedJournal.readAll());
    QVERIFY(adoptedJournalDocument.isObject());
    QCOMPARE(adoptedJournalDocument.object().value(QStringLiteral("profileId")).toString(),
             paths.profileId());

    const auto cleared = localStorage->capture(&error);
    QVERIFY2(cleared.has_value(), qPrintable(error));
    QVERIFY(cleared->mainSyncProvider.isEmpty());
    QVERIFY(cleared->stremioState.isEmpty());
    QVERIFY(cleared->theatreExtensions.isEmpty());
    RatingsReviewsDeliveryOutbox accountOutbox(
        paths.ratingsReviewsDeliveryOutboxPath());
    const auto adoptedUnknown = accountOutbox.operation(operationId);
    QVERIFY(adoptedUnknown.has_value());
    QCOMPARE(adoptedUnknown->profileId, paths.profileId());
    QCOMPARE(adoptedUnknown->profileIncarnation, localUnknown.profileIncarnation);
    QCOMPARE(adoptedUnknown->state, QStringLiteral("unknownOutcome"));
    QFile localOutboxAfter(localBinding.privatePaths.outboxPath);
    QVERIFY(localOutboxAfter.open(QIODevice::ReadOnly));
    QCOMPARE(localOutboxAfter.readAll(), localOutboxBytes);
}

void tst_account_adoption::
legacyAccountlessAdoptionCarriesStremioCredential() {
    AdoptionFixture fixture;
    const ProfilePaths paths = fixture.accountPaths();
    QString error;

    PersonalStateSnapshot source = populatedSnapshot();
    source.mainSyncProvider = QStringLiteral("stremio");
    source.stremioState = QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profileId"), QStringLiteral("legacy")},
        {QStringLiteral("bindingGeneration"), QStringLiteral("1")},
        {QStringLiteral("accountId"), QStringLiteral("stremio-account-a")},
        {QStringLiteral("displayName"), QStringLiteral("Fixture Viewer")},
        {QStringLiteral("acknowledgedBaselines"), QJsonObject{}},
        {QStringLiteral("importRedoReceipts"), QJsonArray{}},
        {QStringLiteral("intentionalMembershipDifferences"), QJsonArray{}},
        {QStringLiteral("lastSuccessAtMs"), QStringLiteral("1720000040000")},
        {QStringLiteral("firstMergeComplete"), true},
        {QStringLiteral("reconnectRequired"), false},
        {QStringLiteral("pendingIntents"), QJsonArray{}}};
    QVERIFY2(fixture.legacy.restorePersonalState(source, &error), qPrintable(error));

    const auto key = [](const QString &profileId, const QString &accountId) {
        return profileId + QLatin1Char('|') + accountId;
    };
    QHash<QString, QByteArray> vault;
    vault.insert(key(QStringLiteral("legacy"), QStringLiteral("stremio-account-a")),
                 QByteArrayLiteral("fixture-secret"));
    StremioCredentialAdoptionCallbacks callbacks{
        [&vault, &key](const QString &profileId, const QString &accountId)
            -> std::optional<QByteArray> {
            const auto found = vault.constFind(key(profileId, accountId));
            return found == vault.cend() ? std::nullopt
                                         : std::optional<QByteArray>(*found);
        },
        [&vault, &key](const QString &profileId,
                       const QString &accountId,
                       const QByteArray &authKey) {
            vault.insert(key(profileId, accountId), authKey);
            return true;
        },
        [&vault](const QString &profileId) {
            const QString prefix = profileId + QLatin1Char('|');
            for (auto it = vault.begin(); it != vault.end();) {
                if (it.key().startsWith(prefix))
                    it = vault.erase(it);
                else
                    ++it;
            }
            return true;
        }};

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    FirstAccountProfileCoordinator coordinator(&runtime, fixture.appDataRoot, callbacks);
    QVERIFY2(coordinator.prepareCreatedAccount(QString::fromLatin1(kAccountA), &error),
             qPrintable(error));
    QVERIFY(!vault.contains(key(QStringLiteral("legacy"),
                                QStringLiteral("stremio-account-a"))));
    QCOMPARE(vault.value(key(paths.profileId(), QStringLiteral("stremio-account-a"))),
             QByteArrayLiteral("fixture-secret"));
    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::Account);
}

void tst_account_adoption::
stremioCredentialTransferReplacesStaleDestinationForSameAccount() {
    AdoptionFixture fixture;
    const ProfilePaths paths = fixture.accountPaths();
    const ProfilePaths localPaths = ProfilePaths::localOnly(fixture.appDataRoot);
    QString error;
    const auto localStorage = LegacyPersonalStateStorage::forProfile(localPaths, &error);
    QVERIFY2(localStorage.has_value(), qPrintable(error));

    PersonalStateSnapshot source = populatedSnapshot();
    source.mainSyncProvider = QStringLiteral("stremio");
    source.stremioState = QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profileId"), localPaths.profileId()},
        {QStringLiteral("bindingGeneration"), QStringLiteral("2")},
        {QStringLiteral("accountId"), QStringLiteral("stremio-account-a")},
        {QStringLiteral("displayName"), QStringLiteral("Fixture Viewer")},
        {QStringLiteral("acknowledgedBaselines"), QJsonObject{}},
        {QStringLiteral("importRedoReceipts"), QJsonArray{}},
        {QStringLiteral("intentionalMembershipDifferences"), QJsonArray{}},
        {QStringLiteral("lastSuccessAtMs"), QStringLiteral("1720000040000")},
        {QStringLiteral("firstMergeComplete"), true},
        {QStringLiteral("reconnectRequired"), false},
        {QStringLiteral("pendingIntents"), QJsonArray{}}};
    QVERIFY2(localStorage->restorePersonalState(source, &error), qPrintable(error));

    const auto key = [](const QString &profileId, const QString &accountId) {
        return profileId + QLatin1Char('|') + accountId;
    };
    QHash<QString, QByteArray> vault;
    vault.insert(key(localPaths.profileId(), QStringLiteral("stremio-account-a")),
                 QByteArrayLiteral("current-local-secret"));
    vault.insert(key(paths.profileId(), QStringLiteral("stremio-account-a")),
                 QByteArrayLiteral("stale-account-secret"));
    StremioCredentialAdoptionCallbacks callbacks{
        [&vault, &key](const QString &profileId, const QString &accountId)
            -> std::optional<QByteArray> {
            const auto found = vault.constFind(key(profileId, accountId));
            return found == vault.cend() ? std::nullopt
                                         : std::optional<QByteArray>(*found);
        },
        [&vault, &key](const QString &profileId,
                       const QString &accountId,
                       const QByteArray &authKey) {
            vault.insert(key(profileId, accountId), authKey);
            return true;
        },
        [&vault](const QString &profileId) {
            const QString prefix = profileId + QLatin1Char('|');
            for (auto it = vault.begin(); it != vault.end();) {
                if (it.key().startsWith(prefix))
                    it = vault.erase(it);
                else
                    ++it;
            }
            return true;
        }};

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    QVERIFY2(runtime.activateLocalOnlyProfile(&error), qPrintable(error));
    FirstAccountProfileCoordinator coordinator(&runtime, fixture.appDataRoot, callbacks);
    QVERIFY2(coordinator.prepareCreatedAccount(QString::fromLatin1(kAccountA), &error),
             qPrintable(error));
    QVERIFY(!vault.contains(key(localPaths.profileId(),
                                QStringLiteral("stremio-account-a"))));
    QCOMPARE(vault.value(key(paths.profileId(), QStringLiteral("stremio-account-a"))),
             QByteArrayLiteral("current-local-secret"));
    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::Account);
}

void tst_account_adoption::
stremioCredentialTransferRetriesBeforeSourceRetirement() {
    AdoptionFixture fixture;
    const ProfilePaths paths = fixture.accountPaths();
    const ProfilePaths localPaths = ProfilePaths::localOnly(fixture.appDataRoot);
    QString error;
    const auto localStorage = LegacyPersonalStateStorage::forProfile(localPaths, &error);
    QVERIFY2(localStorage.has_value(), qPrintable(error));

    PersonalStateSnapshot source = populatedSnapshot();
    source.mainSyncProvider = QStringLiteral("stremio");
    source.stremioState = QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profileId"), localPaths.profileId()},
        {QStringLiteral("bindingGeneration"), QStringLiteral("1")},
        {QStringLiteral("accountId"), QStringLiteral("stremio-account-a")},
        {QStringLiteral("displayName"), QStringLiteral("Fixture Viewer")},
        {QStringLiteral("acknowledgedBaselines"), QJsonObject{}},
        {QStringLiteral("importRedoReceipts"), QJsonArray{}},
        {QStringLiteral("intentionalMembershipDifferences"), QJsonArray{}},
        {QStringLiteral("lastSuccessAtMs"), QStringLiteral("1720000040000")},
        {QStringLiteral("firstMergeComplete"), true},
        {QStringLiteral("reconnectRequired"), false},
        {QStringLiteral("pendingIntents"), QJsonArray{}}};
    QVERIFY2(localStorage->restorePersonalState(source, &error), qPrintable(error));

    const auto key = [](const QString &profileId, const QString &accountId) {
        return profileId + QLatin1Char('|') + accountId;
    };
    QHash<QString, QByteArray> vault;
    vault.insert(key(localPaths.profileId(), QStringLiteral("stremio-account-a")),
                 QByteArrayLiteral("fixture-secret"));
    bool allowSave = false;
    int clearCalls = 0;
    StremioCredentialAdoptionCallbacks callbacks{
        [&vault, &key](const QString &profileId, const QString &accountId)
            -> std::optional<QByteArray> {
            const auto found = vault.constFind(key(profileId, accountId));
            return found == vault.cend()
                ? std::nullopt
                : std::optional<QByteArray>(*found);
        },
        [&vault, &key, &allowSave](const QString &profileId,
                                   const QString &accountId,
                                   const QByteArray &authKey) {
            if (!allowSave)
                return false;
            vault.insert(key(profileId, accountId), authKey);
            return true;
        },
        [&vault, &clearCalls](const QString &profileId) {
            ++clearCalls;
            const QString prefix = profileId + QLatin1Char('|');
            for (auto it = vault.begin(); it != vault.end();) {
                if (it.key().startsWith(prefix))
                    it = vault.erase(it);
                else
                    ++it;
            }
            return true;
        }};

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    QVERIFY2(runtime.activateLocalOnlyProfile(&error), qPrintable(error));
    FirstAccountProfileCoordinator coordinator(&runtime, fixture.appDataRoot, callbacks);
    QVERIFY(!coordinator.prepareCreatedAccount(QString::fromLatin1(kAccountA), &error));
    QVERIFY(error.contains(QStringLiteral("credential")));
    QCOMPARE(clearCalls, 0);
    QVERIFY(vault.contains(key(localPaths.profileId(),
                               QStringLiteral("stremio-account-a"))));
    QVERIFY(!vault.contains(key(paths.profileId(),
                                QStringLiteral("stremio-account-a"))));

    allowSave = true;
    error.clear();
    QVERIFY2(coordinator.prepareCreatedAccount(QString::fromLatin1(kAccountA), &error),
             qPrintable(error));
    QCOMPARE(clearCalls, 1);
    QVERIFY(!vault.contains(key(localPaths.profileId(),
                                QStringLiteral("stremio-account-a"))));
    QCOMPARE(vault.value(key(paths.profileId(), QStringLiteral("stremio-account-a"))),
             QByteArrayLiteral("fixture-secret"));
    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::Account);
}

void tst_account_adoption::
migrationSuspensionStopsWhenTrackerCloseCannotBePersisted() {
    AdoptionFixture fixture;
    const ProfilePaths local = ProfilePaths::localOnly(fixture.appDataRoot);
    TrackerConnectionStore connections(local);
    QVERIFY(connections.upsert({
        TrackerProviderId::Simkl,
        QStringLiteral("simkl-account"),
        1,
        1000,
        TrackerProviderCapability::Scrobble,
        TrackerConnectionState::Connected}));
    TrackerMappingStore mappings(local);
    QVERIFY(mappings.upsert(
        {TrackerProviderId::Simkl, QStringLiteral("simkl-account"), QStringLiteral("123")},
        {QStringLiteral("movie:fixture"), QStringLiteral("movie"),
         QStringLiteral("fixture"), QStringLiteral("Fixture Movie")},
        TrackerMappingProvenance::UserConfirmed));

    ProfileStoreRuntime runtime(fixture.legacy, fixture.appDataRoot);
    QString error;
    QVERIFY2(runtime.activateLocalOnlyProfile(&error), qPrintable(error));
    QQmlApplicationEngine engine;
    runtime.prepareForQml(&engine);
    auto *trackers = qobject_cast<TrackerScrobbleRuntime *>(
        engine.rootContext()->contextProperty(QStringLiteral("ProfileTrackers"))
            .value<QObject *>());
    QVERIFY(trackers);
    QVERIFY(trackers->setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));

    trackers->observePlaybackLifecycle({
        {QStringLiteral("scopeGeneration"), QVariant::fromValue(
             trackers->playbackScopeGeneration())},
        {QStringLiteral("playbackGeneration"), 1},
        {QStringLiteral("transitionSequence"), 1},
        {QStringLiteral("sessionId"), QStringLiteral("migration-session")},
        {QStringLiteral("action"), QStringLiteral("start")},
        {QStringLiteral("positionMs"), 10'000},
        {QStringLiteral("durationMs"), 100'000},
        {QStringLiteral("identity"), QVariantMap{
             {QStringLiteral("source"), QStringLiteral("theatre-player")},
             {QStringLiteral("world"), QStringLiteral("theatre")},
             {QStringLiteral("kind"), QStringLiteral("movie")},
             {QStringLiteral("itemKey"), QStringLiteral("fixture")}}}});
    QVERIFY(!trackers->store()->intents().isEmpty());
    QCOMPARE(trackers->store()->intents().first().playbackSessionId,
             QStringLiteral("migration-session"));

    const QString scrobblePath = TrackerScrobbleStore::storagePath(local);
    QVERIFY(QFile::remove(scrobblePath));
    QVERIFY(QDir().mkpath(scrobblePath));

    QSignalSpy aboutToChange(&runtime, &ProfileStoreRuntime::storesAboutToChange);
    QSignalSpy deactivationRequested(
        &runtime, &ProfileStoreRuntime::profileDeactivationRequested);
    QSignalSpy deactivationCommitted(
        &runtime, &ProfileStoreRuntime::profileDeactivationCommitted);
    QVERIFY(!runtime.suspendPersonalStoresForMigration(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(aboutToChange.count(), 0);
    QCOMPARE(deactivationRequested.count(), 1);
    QCOMPARE(deactivationCommitted.count(), 0);
    QCOMPARE(runtime.activeProfile().kind(), ProfilePaths::Kind::LocalOnly);
    QVERIFY(runtime.progressStore());
    QVERIFY(runtime.activityStore());
    QVERIFY(QDir(scrobblePath).removeRecursively());
}

QTEST_MAIN(tst_account_adoption)
#include "tst_account_adoption.moc"
