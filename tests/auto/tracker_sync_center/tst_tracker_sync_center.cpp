#include "trackers/TrackerConnectionStore.h"
#include "trackers/TrackerConnectionService.h"
#include "trackers/TrackerDeliveryStore.h"
#include "trackers/TrackerHistoryEvidenceStore.h"
#include "trackers/TrackerImportStore.h"
#include "trackers/TrackerMappingStore.h"
#include "trackers/TrackerScrobbleStore.h"
#include "trackers/TrackerSyncCenterModel.h"
#include "trackers/TrackerSyncSettingsStore.h"
#include "trackers/TrackerProgressImportOwner.h"

#include "ProgressStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <initializer_list>
#include <algorithm>
#include <memory>

namespace {

constexpr auto kProfileIdA = "22222222-2222-4222-8222-222222222222";
constexpr auto kProfileIdB = "33333333-3333-4333-8333-333333333333";
constexpr auto kRemoteAccountId = "remote-account-private-42";
constexpr auto kRemoteItemId = "remote-item-private-episode-9";

TrackerProviderCapabilities simklCapabilities()
{
    return TrackerProviderCapability::ReadHistory
        | TrackerProviderCapability::ReadProgress
        | TrackerProviderCapability::WriteProgress
        | TrackerProviderCapability::WriteCompletion
        | TrackerProviderCapability::Scrobble;
}

TrackerImportProgressTarget exactTarget(const TrackerTitleMapping &mapping,
                                       int episode,
                                       double fraction = 1.0,
                                       bool completed = true)
{
    return {mapping.canonical.canonicalMediaId, QStringLiteral("video"),
            mapping.canonical.historyId + QLatin1Char(':') + QString::number(episode),
            fraction, completed};
}

QList<TrackerProviderDescriptor> testCatalogue()
{
    return {{TrackerProviderId::Simkl, QStringLiteral("SIMKL"), simklCapabilities(), true},
            {TrackerProviderId::Mal, QStringLiteral("MyAnimeList"), {}, false},
            {TrackerProviderId::Trakt, QStringLiteral("Trakt"), {}, false},
            {TrackerProviderId::AniList, QStringLiteral("AniList"), {}, false}};
}

class UnreachableImportOwner final : public TrackerImportOwner
{
public:
    std::optional<TrackerImportedProgressValue> currentProgress(
        const TrackerTitleMapping &) const override
    {
        return std::nullopt;
    }

    TrackerImportOwnerApplyResult applyImportedProgress(
        const QString &,
        const TrackerTitleMapping &,
        const std::optional<TrackerImportedProgressValue> &,
        int,
        bool,
        std::optional<TrackerImportedProgressValue> *,
        QString *) override
    {
        ++applyCalls;
        return TrackerImportOwnerApplyResult::Failed;
    }

    int applyCalls = 0;
};

class CommittedDeliverySource final : public TrackerDeliverySource
{
public:
    explicit CommittedDeliverySource(TrackerDeliveryFact fact)
        : m_fact(std::move(fact)) {}

    QList<TrackerDeliveryFact> currentCommittedFacts() const override
    {
        return {m_fact};
    }

    bool isDurablyCurrent(const TrackerDeliveryFact &fact) const override
    {
        return fact.canonicalMediaId == m_fact.canonicalMediaId
            && fact.historyKind == m_fact.historyKind
            && fact.historyId == m_fact.historyId
            && fact.kind == m_fact.kind
            && fact.sourceRevision == m_fact.sourceRevision
            && fact.sourceEventId == m_fact.sourceEventId
            && fact.progress == m_fact.progress
            && fact.contentFingerprint == m_fact.contentFingerprint
            && fact.origin == m_fact.origin
            && fact.mediaDomain == m_fact.mediaDomain;
    }

    void setFact(const TrackerDeliveryFact &fact)
    {
        m_fact = fact;
    }

private:
    TrackerDeliveryFact m_fact;
};

class ExportReviewSource final : public TrackerDeliverySource
{
public:
    QList<TrackerDeliveryFact> facts;
    QList<TrackerDeliveryFact> currentCommittedFacts() const override { return facts; }
    bool isDurablyCurrent(const TrackerDeliveryFact &fact) const override
    {
        return std::any_of(facts.cbegin(), facts.cend(), [&fact](const auto &current) {
            return current.canonicalMediaId == fact.canonicalMediaId
                && current.sourceRevision == fact.sourceRevision
                && current.contentFingerprint == fact.contentFingerprint;
        });
    }
};

class RecordingNormalizedTransport final : public TrackerNormalizedTransport
{
public:
    TrackerNormalizedTransportResult execute(
        const TrackerNormalizedTransportRequest &request) const override
    {
        ++calls;
        lastRequest = request;
        return {TrackerNormalizedTransportOutcome::Completed};
    }

    mutable int calls = 0;
    mutable TrackerNormalizedTransportRequest lastRequest;
};

class TrackerSyncCenterFixture
{
public:
    explicit TrackerSyncCenterFixture(const QString &profileId = QString::fromLatin1(kProfileIdA))
    {
        const auto created = ProfilePaths::account(profileId, root.path());
        if (!created)
            return;
        profile = *created;
        connections = std::make_unique<TrackerConnectionStore>(profile);
        mappings = std::make_unique<TrackerMappingStore>(profile);
        delivery = std::make_unique<TrackerDeliveryStore>(profile, mappings.get(), connections.get());
        scrobble = std::make_unique<TrackerScrobbleStore>(profile);
        settings = std::make_unique<TrackerSyncSettingsStore>(profile);
        imports = std::make_unique<TrackerImportStore>(profile, mappings.get(), connections.get());
        historyEvidence = std::make_unique<TrackerHistoryEvidenceStore>(profile, mappings.get());
        model = makeModel();
    }

    bool valid() const
    {
        return root.isValid() && connections && mappings && delivery && scrobble
            && settings && imports && historyEvidence && model;
    }

    bool connectSimkl(quint64 generation = 1)
    {
        return connections && connections->upsert({TrackerProviderId::Simkl,
            QString::fromLatin1(kRemoteAccountId), generation, 1234,
            simklCapabilities(), TrackerConnectionState::Connected});
    }

    std::optional<TrackerImportBatch> unmatchedPreview(
        const QString &snapshot = QStringLiteral("snapshot-unmatched"),
        const QString &providerItemId = QString::fromLatin1(kRemoteItemId))
    {
        TrackerImportRemoteItem item;
        item.providerItemId = providerItemId;
        item.remote = {TrackerProviderId::Simkl, QString::fromLatin1(kRemoteAccountId),
                       QStringLiteral("remote-media-private-9")};
        item.progress = 8;
        item.supported = true;
        item.displayTitle = QStringLiteral("A Safe Display Title");
        TrackerImportBatchDraft draft{TrackerProviderId::Simkl,
            QString::fromLatin1(kRemoteAccountId), 1, snapshot,
            QStringLiteral("cursor-unmatched"), true, true, {item}, {}};
        return imports->createPreview(draft);
    }

    bool enableTitleMatchingWithProgress()
    {
        titleProgress = std::make_unique<ProgressStore>(
            root.path() + QStringLiteral("/title-match-progress.ini"));
        titleProgress->record({{QStringLiteral("kind"), QStringLiteral("video")},
                               {QStringLiteral("id"), QStringLiteral("native-progress-private-7")},
                               {QStringLiteral("title"), QStringLiteral("Colosseum candidate title")},
                               {QStringLiteral("progress"), 0.35},
                               {QStringLiteral("watched"), false}});
        titleOwner = std::make_unique<TrackerProgressImportOwner>(titleProgress.get());
        model->setImportOwner(titleOwner.get());
        model->setTitleMatching(mappings.get(), titleOwner.get());
        QObject::connect(titleProgress.get(), &ProgressStore::healthChanged,
                         model.get(), &TrackerSyncCenterModel::refresh);
        return titleProgress->healthy();
    }

    TrackerScrobbleIntent pendingScrobble(const QString &operationId) const
    {
        TrackerScrobbleIntent intent;
        intent.operationId = operationId;
        intent.providerId = TrackerProviderId::Simkl;
        intent.remoteAccountId = QString::fromLatin1(kRemoteAccountId);
        intent.connectionGeneration = 1;
        intent.mappingRevision = 1;
        intent.canonicalMediaId = QStringLiteral("movie:colosseum-test");
        intent.remoteMediaId = QStringLiteral("remote-media-private-9");
        intent.playbackSessionId = QStringLiteral("session-test-1");
        intent.playbackGeneration = 1;
        intent.transitionSequence = 1;
        intent.action = TrackerScrobbleAction::Start;
        intent.createdAtMs = 987654;
        return intent;
    }

    std::unique_ptr<TrackerSyncCenterModel> makeModel(
        QList<TrackerProviderDescriptor> descriptors = testCatalogue(),
        TrackerConnectionStore *connectionStore = nullptr,
        TrackerImportStore *importStore = nullptr,
        TrackerDeliveryStore *deliveryStore = nullptr,
        TrackerScrobbleStore *scrobbleStore = nullptr,
        TrackerSyncSettingsStore *settingsStore = nullptr,
        TrackerSyncCenterModel::ResumeAfterSyncAction resume = {},
        TrackerSyncCenterModel::DisconnectAction disconnect = {})
    {
        connectionStore = connectionStore ? connectionStore : connections.get();
        importStore = importStore ? importStore : imports.get();
        deliveryStore = deliveryStore ? deliveryStore : delivery.get();
        scrobbleStore = scrobbleStore ? scrobbleStore : scrobble.get();
        settingsStore = settingsStore ? settingsStore : settings.get();
        return std::make_unique<TrackerSyncCenterModel>(
            connectionStore, importStore, deliveryStore, scrobbleStore, settingsStore,
            descriptors,
            [this](const QString &providerKey, bool enabled) {
                const auto id = trackerProviderIdFromKey(providerKey);
                const auto connection = id && connections ? connections->connection(*id) : std::nullopt;
                return id && connection && scrobble->setEnabled(
                    *id, connection->remoteAccountId, enabled);
            },
            std::move(resume), std::move(disconnect), historyEvidence.get(),
            [this](QString *error) {
                ++deliveryRefreshCalls;
                if (!delivery || !delivery->providerSendEnabled(
                        TrackerProviderId::Simkl, QString::fromLatin1(kRemoteAccountId))) {
                    if (error)
                        *error = QStringLiteral("Send preference was not persisted.");
                    return false;
                }
                return true;
            });
    }

    QTemporaryDir root;
    ProfilePaths profile = ProfilePaths::sealed();
    std::unique_ptr<TrackerConnectionStore> connections;
    std::unique_ptr<TrackerMappingStore> mappings;
    std::unique_ptr<TrackerDeliveryStore> delivery;
    std::unique_ptr<TrackerScrobbleStore> scrobble;
    std::unique_ptr<TrackerSyncSettingsStore> settings;
    std::unique_ptr<TrackerImportStore> imports;
    std::unique_ptr<TrackerHistoryEvidenceStore> historyEvidence;
    std::unique_ptr<ProgressStore> titleProgress;
    std::unique_ptr<TrackerProgressImportOwner> titleOwner;
    std::unique_ptr<TrackerSyncCenterModel> model;
    int deliveryRefreshCalls = 0;
};

QSet<QString> keys(const QVariantMap &map)
{
    QSet<QString> result;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        result.insert(it.key());
    return result;
}

QSet<QString> keySet(std::initializer_list<const char *> names)
{
    QSet<QString> result;
    for (const char *name : names)
        result.insert(QString::fromLatin1(name));
    return result;
}

} // namespace

class TrackerSyncCenterTest final : public QObject
{
    Q_OBJECT

private slots:
    void settingsDefaultPersistAndStayAccountScoped();
    void syncReceiptPreservesReviewDependentPullDefault();
    void adoptionCopiesOnlyWhenDestinationHasNoSettings();
    void accountlessCatalogueProjectionIsSafeAndInert();
    void connectStaysDisabledWithoutNativeAuthenticationAction();
    void safeDtoKeysMatchAllowlist();
    void connectedCardUsesSafeLabelAndEffectiveCapabilities();
    void importReviewUsesOpaqueIdsAndRevalidatesRevision();
    void findMatchIsRoutedAsASeparateNativeIntent();
    void titleMatchConfirmsOnlyARevalidatedNativeTitle();
    void titleMatchCandidatesExposeSafeNativeMetadata();
    void titleMatchCandidatesDisambiguateOrWithholdDuplicates();
    void titleMatchAvailabilityTracksProgressHealth();
    void titleMatchPreviewFailureRollsBackOrReportsRetainedMapping();
    void newProgressChoicesMatchDurableResolutionRules();
    void staleImportChoiceCannotSurviveTitleRemap();
    void selectedBulkImportReviewRevalidatesEveryChoiceAndKeepsExceptionsVisible();
    void automaticPullRequiresCompletedInitialReview();
    void providerSettingsAndSyncReceiptsAdvanceRevision();
    void firstExportReviewUsesOpaqueSelectionAndFreshSnapshots();
    void connectionServiceCountsOnlyCurrentSupportedReceipts();
    void connectionServiceDoesNotCountConnectedOnlyProvider();
    void connectionServiceNormalizedTransportContract();
    void diagnoseRouteIsCurrentAndSanitized();
    void enablingSendsRefreshesCurrentFactsAfterPersistingPreference();
    void disconnectIsRevisionFencedAndUsesOnlyExplicitLocalChoices();
    void disconnectRefusesInflightDeliveryThenKeepsUnknownWorkVisible();
    void disconnectedUnknownWorkRemainsInSyncAttentionAndDossier();
    void disconnectedKnownUnsentCanBeRetriedWithoutReconnect();
    void removeImportedDataDeletesOnlyThatTrackerSource();
    void removeImportedOverlayPreservesNativeAndOtherProviderProgress();
    void importedProgressChangesFenceRemovalConfirmation();
    void removeProgressOnlyDataSuppressesRoutineReimport();
    void pauseKeepsQueuedWorkAndResumeCallbackRunsAfterPersist();
    void waitingStateIsHealthyAndRetainsQueuedWork();
    void retryableProviderOutageNeverProjectsHealthy();
    void retryingDeliveryOutageNeverProjectsHealthy();
    void syncAllKeepsMixedProviderOutcomesVisible();
    void syncAllIsNotAdvertisedWithoutAHandler();
    void unresolvedAndUnknownOutcomesNeverProjectHealthy();
    void restartProjectionMatchesDurableOwnerTruth();
    void unavailableOwnerAndUnsupportedPreferencesFailClosed();
};

void TrackerSyncCenterTest::settingsDefaultPersistAndStayAccountScoped()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profileA = ProfilePaths::account(QString::fromLatin1(kProfileIdA), root.path());
    const auto profileB = ProfilePaths::account(QString::fromLatin1(kProfileIdB), root.path());
    QVERIFY(profileA && profileB);

    TrackerSyncSettingsStore settings(*profileA);
    QVERIFY(settings.healthy());
    QCOMPARE(settings.globalSettings().trackerSyncEnabled, true);
    QCOMPARE(settings.globalSettings().checkOnLaunch, true);
    QCOMPARE(settings.globalSettings().backgroundDelivery, true);
    QCOMPARE(settings.globalSettings().completionMessages, false);
    QCOMPARE(settings.pullAutomatically(TrackerProviderId::Simkl,
                                        QString::fromLatin1(kRemoteAccountId), false), false);
    QVERIFY(settings.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), false));
    QVERIFY(settings.setGlobalSetting(QStringLiteral("checkOnLaunch"), false));
    QVERIFY(settings.setGlobalSetting(QStringLiteral("backgroundDelivery"), false));
    QVERIFY(settings.setGlobalSetting(QStringLiteral("completionMessages"), true));
    QVERIFY(settings.setPullAutomatically(TrackerProviderId::Simkl,
                                          QString::fromLatin1(kRemoteAccountId), true));
    QVERIFY(settings.recordSuccessfulSync(TrackerProviderId::Simkl,
                                          QString::fromLatin1(kRemoteAccountId), 456789));

    TrackerSyncSettingsStore reopened(*profileA);
    QVERIFY(reopened.healthy());
    QCOMPARE(reopened.globalSettings().trackerSyncEnabled, false);
    QCOMPARE(reopened.globalSettings().checkOnLaunch, false);
    QCOMPARE(reopened.globalSettings().backgroundDelivery, false);
    QCOMPARE(reopened.globalSettings().completionMessages, true);
    QCOMPARE(reopened.pullAutomatically(TrackerProviderId::Simkl,
                                        QString::fromLatin1(kRemoteAccountId), false), true);
    QCOMPARE(reopened.lastSuccessfulSyncAtMs(TrackerProviderId::Simkl,
                                             QString::fromLatin1(kRemoteAccountId)), 456789);

    TrackerSyncSettingsStore otherProfile(*profileB);
    QVERIFY(otherProfile.healthy());
    QCOMPARE(otherProfile.globalSettings().trackerSyncEnabled, true);
    QCOMPARE(otherProfile.pullAutomatically(TrackerProviderId::Simkl,
                                           QString::fromLatin1(kRemoteAccountId), false), false);
}

void TrackerSyncCenterTest::syncReceiptPreservesReviewDependentPullDefault()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const QString account = QString::fromLatin1(kRemoteAccountId);

    QVERIFY(fixture.settings->recordSuccessfulSync(TrackerProviderId::Simkl,
        account, 456789));
    QCOMPARE(fixture.model->providerDossier(QStringLiteral("simkl"))
                 .value(QStringLiteral("pullAutomatically")).toBool(), false);

    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, account,
        QStringLiteral("review-default-remote")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("colosseum:series:review-default"), QStringLiteral("series"),
        QStringLiteral("series:review-default"), QStringLiteral("Review default")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::ExactProviderIdentity));
    const TrackerTitleMapping mapping = *fixture.mappings->mapping(remote);
    const TrackerImportProgressTarget target = exactTarget(mapping, 8);
    TrackerImportedProgressValue local{canonical.canonicalMediaId,
        canonical.historyKind, canonical.historyId, 8, true, 1, true};
    local.exactProgressTarget = target;

    TrackerImportRemoteItem item;
    item.providerItemId = QStringLiteral("review-default-item");
    item.remote = remote;
    item.mapping = mapping;
    item.progress = 8;
    item.completed = true;
    item.supported = true;
    item.localAtPreview = local;
    item.displayTitle = QStringLiteral("Review default");
    item.exactProgressTarget = target;
    const auto batch = fixture.imports->createPreview({TrackerProviderId::Simkl,
        account, 1, QStringLiteral("review-default-snapshot"),
        QStringLiteral("review-default-cursor"), true, true, {item}, {}});
    QVERIFY(batch);
    QCOMPARE(batch->items.first().classification, TrackerImportClassification::ExactMatch);

    UnreachableImportOwner owner;
    fixture.model->setImportOwner(&owner);
    QVERIFY(fixture.model->confirmImport(batch->batchId, fixture.model->revision()));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.imports->batch(batch->batchId).has_value()
        && fixture.imports->batch(batch->batchId)->cursorCommitted, 3000);
    QCOMPARE(fixture.model->providerDossier(QStringLiteral("simkl"))
                 .value(QStringLiteral("pullAutomatically")).toBool(), true);
    QCOMPARE(owner.applyCalls, 0);
}

void TrackerSyncCenterTest::adoptionCopiesOnlyWhenDestinationHasNoSettings()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto sourceProfile = ProfilePaths::account(QString::fromLatin1(kProfileIdA), root.path());
    const auto destinationProfile = ProfilePaths::account(QString::fromLatin1(kProfileIdB), root.path());
    QVERIFY(sourceProfile && destinationProfile);
    TrackerSyncSettingsStore source(*sourceProfile);
    TrackerSyncSettingsStore destination(*destinationProfile);
    QVERIFY(source.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), false));
    QVERIFY(source.setGlobalSetting(QStringLiteral("backgroundDelivery"), false));
    QVERIFY(source.setGlobalSetting(QStringLiteral("checkOnLaunch"), false));
    QVERIFY(source.setGlobalSetting(QStringLiteral("completionMessages"), true));
    QVERIFY(source.setPullAutomatically(TrackerProviderId::Simkl,
                                        QString::fromLatin1(kRemoteAccountId), false));
    QVERIFY(destination.adoptPrivateStateFrom(source));
    QCOMPARE(destination.globalSettings().trackerSyncEnabled, true);
    QCOMPARE(destination.globalSettings().backgroundDelivery, true);
    QCOMPARE(destination.globalSettings().checkOnLaunch, false);
    QCOMPARE(destination.globalSettings().completionMessages, true);
    QCOMPARE(destination.pullAutomatically(TrackerProviderId::Simkl,
                                          QString::fromLatin1(kRemoteAccountId), true), false);

    QVERIFY(destination.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), false));
    QVERIFY(source.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), true));
    QVERIFY(source.setGlobalSetting(QStringLiteral("backgroundDelivery"), true));
    QVERIFY(destination.adoptPrivateStateFrom(source));
    QCOMPARE(destination.globalSettings().trackerSyncEnabled, false);
    QCOMPARE(destination.globalSettings().backgroundDelivery, true);
}

void TrackerSyncCenterTest::accountlessCatalogueProjectionIsSafeAndInert()
{
    TrackerSyncCenterModel model;
    QCOMPARE(model.connectedTrackers().size(), 0);
    QCOMPARE(model.catalogue().size(), 4);
    QCOMPARE(model.globalSettings().value(QStringLiteral("profileAvailable")).toBool(), false);
    QCOMPARE(model.globalSettings().value(QStringLiteral("editable")).toBool(), false);
    QCOMPARE(model.aggregateState().value(QStringLiteral("status")).toString(),
             QStringLiteral("Empty"));
    for (const QVariant &rowValue : model.catalogue()) {
        const QVariantMap row = rowValue.toMap();
        QVERIFY(!row.value(QStringLiteral("connectEnabled")).toBool());
        QVERIFY(!row.contains(QStringLiteral("remoteAccountId")));
        QVERIFY(!row.contains(QStringLiteral("credential")));
    }
    const quint64 revision = model.revision();
    QVERIFY(!model.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), false, revision));
    QCOMPARE(model.lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("owner_unavailable"));
}

void TrackerSyncCenterTest::connectStaysDisabledWithoutNativeAuthenticationAction()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    const QVariantMap dossier = fixture.model->providerDossier(QStringLiteral("simkl"));
    QVERIFY(dossier.value(QStringLiteral("available")).toBool());
    QVERIFY(!dossier.value(QStringLiteral("connectEnabled")).toBool());
    const QVariantList catalogue = fixture.model->catalogue();
    QVERIFY(!catalogue.isEmpty());
    QVERIFY(!catalogue.first().toMap().value(QStringLiteral("connectEnabled")).toBool());
}

void TrackerSyncCenterTest::connectedCardUsesSafeLabelAndEffectiveCapabilities()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const QVariantList cards = fixture.model->connectedTrackers();
    QCOMPARE(cards.size(), 1);
    const QVariantMap card = cards.first().toMap();
    QCOMPARE(card.value(QStringLiteral("providerKey")).toString(), QStringLiteral("simkl"));
    QCOMPARE(card.value(QStringLiteral("providerName")).toString(), QStringLiteral("SIMKL"));
    QCOMPARE(card.value(QStringLiteral("accountLabel")).toString(),
             QStringLiteral("Connected account"));
    QCOMPARE(card.value(QStringLiteral("status")).toString(), QStringLiteral("Connected"));
    QVERIFY(card.value(QStringLiteral("capabilities")).toStringList().contains(
        QStringLiteral("read_progress")));
    QVERIFY(!card.contains(QStringLiteral("remoteAccountId")));
    QVERIFY(!card.contains(QStringLiteral("remote-account-private-42")));
    QVERIFY(!fixture.model->catalogue().at(0).toMap().contains(QStringLiteral("credential")));
    QCOMPARE(fixture.model->aggregateState().value(QStringLiteral("status")).toString(),
             QStringLiteral("Healthy"));
    QCOMPARE(fixture.model->aggregateState().value(QStringLiteral("healthy")).toBool(), true);
}

void TrackerSyncCenterTest::safeDtoKeysMatchAllowlist()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const auto batch = fixture.unmatchedPreview();
    QVERIFY(batch);
    QVERIFY(fixture.scrobble->recordIntent(
        fixture.pendingScrobble(QStringLiteral("allowlist-operation-1"))));

    const QVariantList connected = fixture.model->connectedTrackers();
    QCOMPARE(connected.size(), 1);
    QCOMPARE(keys(connected.first().toMap()), keySet({
        "providerKey", "providerName", "accountLabel", "status", "connectionState",
        "lastSuccessfulSyncAtMs", "pendingCount", "waitingCount", "unresolvedCount",
        "capabilities", "available", "connectEnabled", "syncEnabled", "disconnectEnabled"}));

    const QVariantList catalogue = fixture.model->catalogue();
    QCOMPARE(catalogue.size(), 3);
    QCOMPARE(keys(catalogue.first().toMap()), keySet({
        "providerKey", "providerName", "available", "status", "capabilities",
        "connected", "pendingWork", "waitingCount", "unresolvedCount", "connectEnabled"}));

    const QVariantList reviews = fixture.model->importReviews();
    QCOMPARE(reviews.size(), 1);
    QCOMPARE(keys(reviews.first().toMap()), keySet({
        "batchId", "providerKey", "providerName", "initialImport", "pageComplete",
        "reviewCount", "decisionCount", "awaitingApplyCount", "unresolvedCount", "confirmed"}));

    const QVariantMap global = fixture.model->globalSettings();
    QCOMPARE(keys(global), keySet({"trackerSyncEnabled", "checkOnLaunch", "backgroundDelivery",
        "completionMessages", "profileAvailable", "editable"}));
    const QVariantMap aggregate = fixture.model->aggregateState();
    QCOMPARE(keys(aggregate), keySet({"status", "healthy", "ownerHealthy", "ownerUnavailable",
        "syncing", "connectedCount", "waitingCount", "unresolvedCount",
        "attentionProviderCount", "canSyncAll", "currentActionKind", "revision"}));

    const QVariantMap dossier = fixture.model->providerDossier(QStringLiteral("simkl"));
    QCOMPARE(keys(dossier), keySet({"found", "revision", "providerKey", "providerName",
        "available", "status", "connected", "accountLabel", "capabilities",
        "lastSuccessfulSyncAtMs", "waitingCount", "pendingCount", "unresolvedCount",
        "knownUnsentCount", "unknownOutcomeCount", "importedHistoryCount",
        "importedProgressCount", "importedDataRemovalPreview", "pullAutomatically",
        "pullSettingEnabled", "sendProgressEnabled", "sendSettingEnabled",
        "exportReviewed", "exportReviewEnabled",
        "livePlaybackTrackingEnabled", "livePlaybackSettingEnabled", "connectEnabled",
        "pendingWork", "syncEnabled", "disconnectEnabled", "cleanupEnabled",
        "removeImportedEnabled", "removeImportedPending", "disconnectInFlight"}));

    const QVariantMap review = fixture.model->importReviewSnapshot(batch->batchId,
        fixture.model->revision());
    QCOMPARE(keys(review), keySet({"accepted", "code", "revision", "batchId", "providerKey",
        "providerName", "initialImport", "pageComplete", "confirmed", "items"}));
    const QVariantList reviewItems = review.value(QStringLiteral("items")).toList();
    QCOMPARE(reviewItems.size(), 1);
    QCOMPARE(keys(reviewItems.first().toMap()), keySet({"reviewItemId", "title", "classification",
        "state", "resolution", "matched", "providerProgress", "providerCompleted",
        "hasLocalAtPreview", "localProgress", "localCompleted", "nativeHistoryProtected",
        "allowedChoices"}));

    const QVariantList deliveryRows = fixture.model->deliveryRows(QStringLiteral("simkl"));
    QCOMPARE(deliveryRows.size(), 1);
    QCOMPARE(keys(deliveryRows.first().toMap()), keySet({"operationId", "title", "state", "reason",
        "progress", "attemptCount", "nextAttemptAtMs"}));
    QCOMPARE(keys(fixture.model->lastActionResult()),
             keySet({"accepted", "action", "code", "revision"}));

    QVariantMap boundary;
    boundary.insert(QStringLiteral("connected"), connected);
    boundary.insert(QStringLiteral("catalogue"), catalogue);
    boundary.insert(QStringLiteral("reviews"), reviews);
    boundary.insert(QStringLiteral("global"), global);
    boundary.insert(QStringLiteral("aggregate"), aggregate);
    boundary.insert(QStringLiteral("dossier"), dossier);
    boundary.insert(QStringLiteral("review"), review);
    boundary.insert(QStringLiteral("deliveryRows"), deliveryRows);
    boundary.insert(QStringLiteral("lastActionResult"), fixture.model->lastActionResult());
    const QByteArray serialized = QJsonDocument::fromVariant(boundary)
        .toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains(kRemoteAccountId));
    QVERIFY(!serialized.contains(kRemoteItemId));
    QVERIFY(!serialized.contains("remote-media-private-9"));
}

void TrackerSyncCenterTest::importReviewUsesOpaqueIdsAndRevalidatesRevision()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    QVERIFY(fixture.enableTitleMatchingWithProgress());
    QObject::connect(fixture.model.get(), &TrackerSyncCenterModel::findMatchRequested,
        fixture.model.get(), [](const QString &, const QString &, quint64) {});
    const quint64 staleRevision = fixture.model->revision();
    const auto batch = fixture.unmatchedPreview();
    QVERIFY(batch);
    QCOMPARE(batch->items.size(), 1);
    QCOMPARE(batch->items.first().classification, TrackerImportClassification::NeedsMatching);

    const QString privateItemId = batch->items.first().itemId;
    const QVariantMap review = fixture.model->importReviewSnapshot(batch->batchId,
                                                                   fixture.model->revision());
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    const QVariantList rows = review.value(QStringLiteral("items")).toList();
    QCOMPARE(rows.size(), 1);
    const QVariantMap row = rows.first().toMap();
    QCOMPARE(row.value(QStringLiteral("title")).toString(), QStringLiteral("A Safe Display Title"));
    QVERIFY(row.contains(QStringLiteral("reviewItemId")));
    QVERIFY(row.value(QStringLiteral("reviewItemId")).toString() != privateItemId);
    QVERIFY(!row.contains(QStringLiteral("itemId")));
    QVERIFY(!row.contains(QStringLiteral("providerItemId")));
    QVERIFY(!row.contains(QStringLiteral("remoteAccountId")));
    QVERIFY(!row.contains(QStringLiteral("canonicalMediaId")));
    QVERIFY(row.value(QStringLiteral("allowedChoices")).toStringList().contains(
        QStringLiteral("find_match")));
    QVERIFY(row.value(QStringLiteral("allowedChoices")).toStringList().contains(
        QStringLiteral("leave_unmatched")));
    const QByteArray serialized = QJsonDocument::fromVariant(row).toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains(kRemoteAccountId));
    QVERIFY(!serialized.contains(kRemoteItemId));

    QVERIFY(!fixture.model->resolveImportItem(batch->batchId,
                                               row.value(QStringLiteral("reviewItemId")).toString(),
                                               QStringLiteral("leave_unmatched"), staleRevision));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("stale_intent"));

    const QVariantMap freshReview = fixture.model->importReviewSnapshot(
        batch->batchId, fixture.model->revision());
    const QString publicItemId = freshReview.value(QStringLiteral("items")).toList()
        .first().toMap().value(QStringLiteral("reviewItemId")).toString();
    QVERIFY(!publicItemId.isEmpty());
    QVERIFY(fixture.model->resolveImportItem(batch->batchId, publicItemId,
                                              QStringLiteral("leave_unmatched"),
                                              fixture.model->revision()));
    QVERIFY(fixture.imports->batch(batch->batchId)->items.first().itemId == privateItemId);
    const QVariantMap resolvedRow = fixture.model->importReviewSnapshot(
        batch->batchId, fixture.model->revision()).value(QStringLiteral("items"))
            .toList().first().toMap();
    QCOMPARE(resolvedRow.value(QStringLiteral("resolution")).toString(),
             QStringLiteral("leave_unresolved"));
    QCOMPARE(fixture.model->aggregateState().value(QStringLiteral("healthy")).toBool(), false);
}

void TrackerSyncCenterTest::findMatchIsRoutedAsASeparateNativeIntent()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    QVERIFY(fixture.enableTitleMatchingWithProgress());
    const auto batch = fixture.unmatchedPreview();
    QVERIFY(batch);
    const QVariantMap review = fixture.model->importReviewSnapshot(
        batch->batchId, fixture.model->revision());
    QVERIFY(!review.value(QStringLiteral("items")).toList().first().toMap()
                 .value(QStringLiteral("allowedChoices")).toStringList()
                 .contains(QStringLiteral("find_match")));
    const QString itemId = review.value(QStringLiteral("items")).toList()
        .first().toMap().value(QStringLiteral("reviewItemId")).toString();

    QString requestedBatch;
    QString requestedItem;
    quint64 requestedRevision = 0;
    QObject::connect(fixture.model.get(), &TrackerSyncCenterModel::findMatchRequested,
        fixture.model.get(), [&requestedBatch, &requestedItem, &requestedRevision](
            const QString &batchId, const QString &publicItemId, quint64 revision) {
            requestedBatch = batchId;
            requestedItem = publicItemId;
            requestedRevision = revision;
        });

    const QVariantMap actionableReview = fixture.model->importReviewSnapshot(
        batch->batchId, fixture.model->revision());
    QVERIFY(actionableReview.value(QStringLiteral("items")).toList().first().toMap()
                .value(QStringLiteral("allowedChoices")).toStringList()
                .contains(QStringLiteral("find_match")));

    QVERIFY(fixture.model->resolveImportItem(batch->batchId, itemId,
        QStringLiteral("find_match"), fixture.model->revision()));
    QCOMPARE(requestedBatch, batch->batchId);
    QCOMPARE(requestedItem, itemId);
    QCOMPARE(requestedRevision, fixture.model->revision());
    QCOMPARE(fixture.imports->batch(batch->batchId)->items.first().state,
             TrackerImportItemState::ReviewRequired);
}

void TrackerSyncCenterTest::titleMatchConfirmsOnlyARevalidatedNativeTitle()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    QVERIFY(fixture.enableTitleMatchingWithProgress());
    const QVariantMap progressBefore = fixture.titleProgress->deliveryEntry(
        QStringLiteral("video"), QStringLiteral("native-progress-private-7"));
    QVERIFY(!progressBefore.isEmpty());
    QCOMPARE(progressBefore.value(QStringLiteral("progress")).toDouble(), 0.35);

    const auto batch = fixture.unmatchedPreview(QStringLiteral("title-match-snapshot"));
    QVERIFY(batch);
    const QVariantMap review = fixture.model->importReviewSnapshot(
        batch->batchId, fixture.model->revision());
    const QString reviewItemId = review.value(QStringLiteral("items")).toList()
        .first().toMap().value(QStringLiteral("reviewItemId")).toString();
    QVERIFY(!reviewItemId.isEmpty());

    const QVariantList candidates = fixture.model->titleMatchCandidates(
        batch->batchId, reviewItemId, QStringLiteral("candidate"), fixture.model->revision());
    QCOMPARE(candidates.size(), 1);
    const QVariantMap candidate = candidates.first().toMap();
    QCOMPARE(keys(candidate), keySet({"candidateId", "displayName", "mediaType",
                                      "displayContext"}));
    QCOMPARE(candidate.value(QStringLiteral("displayName")).toString(),
             QStringLiteral("Colosseum candidate title"));
    QCOMPARE(candidate.value(QStringLiteral("mediaType")).toString(), QStringLiteral("Video"));
    QCOMPARE(candidate.value(QStringLiteral("displayContext")).toString(),
             QStringLiteral("35% complete"));
    const QByteArray serialized = QJsonDocument::fromVariant(candidate)
        .toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains("native-progress-private-7"));
    QVERIFY(!serialized.contains("video:native-progress-private-7"));

    QVERIFY(!fixture.model->confirmTitleMatch(batch->batchId, reviewItemId,
        QStringLiteral("forged-candidate"), fixture.model->revision()));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("candidate_unavailable"));
    QCOMPARE(fixture.titleProgress->deliveryEntry(
                 QStringLiteral("video"), QStringLiteral("native-progress-private-7"))
                 .value(QStringLiteral("progress")).toDouble(), 0.35);

    const QVariantList freshCandidates = fixture.model->titleMatchCandidates(
        batch->batchId, reviewItemId, QString(), fixture.model->revision());
    QVERIFY(!freshCandidates.isEmpty());
    const QString candidateId = freshCandidates.first().toMap()
        .value(QStringLiteral("candidateId")).toString();
    QVERIFY(!candidateId.isEmpty());
    QVERIFY(fixture.model->confirmTitleMatch(batch->batchId, reviewItemId,
        candidateId, fixture.model->revision()));

    const auto mapping = fixture.mappings->mapping(batch->items.first().remote.remote);
    QVERIFY(mapping);
    QCOMPARE(mapping->provenance, TrackerMappingProvenance::UserConfirmed);
    const auto updatedBatch = fixture.imports->batch(batch->batchId);
    QVERIFY(updatedBatch);
    QCOMPARE(updatedBatch->items.first().classification,
             TrackerImportClassification::Unsupported);
    QVERIFY(!updatedBatch->items.first().remote.exactProgressTarget.has_value());
    QCOMPARE(fixture.titleProgress->deliveryEntry(
                 QStringLiteral("video"), QStringLiteral("native-progress-private-7"))
                 .value(QStringLiteral("progress")).toDouble(), 0.35);
    QCOMPARE(fixture.titleProgress->trackerImportedProgressCount(
                 QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)), 0);
    QVERIFY(fixture.historyEvidence->contributions().isEmpty());
}

void TrackerSyncCenterTest::titleMatchCandidatesExposeSafeNativeMetadata()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    QVERIFY(fixture.enableTitleMatchingWithProgress());
    fixture.titleProgress->record({{QStringLiteral("kind"), QStringLiteral("manga")},
                                   {QStringLiteral("id"), QStringLiteral("private-manga-id")},
                                   {QStringLiteral("title"), QStringLiteral("Colosseum candidate title")},
                                   {QStringLiteral("caption"), QStringLiteral("Volume 2")},
                                   {QStringLiteral("sub"), QStringLiteral("Chapter 8")},
                                   {QStringLiteral("progress"), 0.4}});
    const auto batch = fixture.unmatchedPreview(QStringLiteral("title-match-metadata-snapshot"));
    QVERIFY(batch);
    const QString itemId = fixture.model->importReviewSnapshot(
        batch->batchId, fixture.model->revision()).value(QStringLiteral("items")).toList()
            .first().toMap().value(QStringLiteral("reviewItemId")).toString();
    const QVariantList candidates = fixture.model->titleMatchCandidates(
        batch->batchId, itemId, QStringLiteral("Colosseum candidate"),
        fixture.model->revision());
    QCOMPARE(candidates.size(), 2);

    QVariantMap video;
    QVariantMap manga;
    for (const QVariant &value : candidates) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("mediaType")).toString() == QLatin1String("Video"))
            video = candidate;
        if (candidate.value(QStringLiteral("mediaType")).toString() == QLatin1String("Manga"))
            manga = candidate;
    }
    QVERIFY(!video.isEmpty());
    QVERIFY(!manga.isEmpty());
    QCOMPARE(video.value(QStringLiteral("displayContext")).toString(),
             QStringLiteral("35% complete"));
    QCOMPARE(manga.value(QStringLiteral("displayContext")).toString(),
             QStringLiteral("Chapter 8"));
    const QByteArray serialized = QJsonDocument::fromVariant(candidates)
        .toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains("private-manga-id"));
    QVERIFY(!serialized.contains("manga:private-manga-id"));
}

void TrackerSyncCenterTest::titleMatchCandidatesDisambiguateOrWithholdDuplicates()
{
    TrackerSyncCenterFixture distinctFixture;
    QVERIFY(distinctFixture.valid());
    QVERIFY(distinctFixture.connectSimkl());
    QVERIFY(distinctFixture.enableTitleMatchingWithProgress());
    distinctFixture.titleProgress->record({
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("id"), QStringLiteral("private-video-id-2")},
        {QStringLiteral("title"), QStringLiteral("Colosseum candidate title")},
        {QStringLiteral("progress"), 0.72},
        {QStringLiteral("watched"), false}});
    const auto distinctBatch = distinctFixture.unmatchedPreview(
        QStringLiteral("title-match-distinct-duplicate-snapshot"));
    QVERIFY(distinctBatch);
    const QVariantMap distinctReview = distinctFixture.model->importReviewSnapshot(
        distinctBatch->batchId, distinctFixture.model->revision());
    const QString distinctItemId = distinctReview.value(QStringLiteral("items")).toList()
        .first().toMap().value(QStringLiteral("reviewItemId")).toString();
    const QVariantList distinctCandidates = distinctFixture.model->titleMatchCandidates(
        distinctBatch->batchId, distinctItemId, QStringLiteral("Colosseum candidate"),
        distinctFixture.model->revision());
    QCOMPARE(distinctCandidates.size(), 2);
    QSet<QString> visibleContexts;
    for (const QVariant &candidateValue : distinctCandidates) {
        visibleContexts.insert(candidateValue.toMap()
            .value(QStringLiteral("displayContext")).toString());
    }
    QVERIFY(visibleContexts.contains(QStringLiteral("35% complete")));
    QVERIFY(visibleContexts.contains(QStringLiteral("72% complete")));

    TrackerSyncCenterFixture identicalFixture;
    QVERIFY(identicalFixture.valid());
    QVERIFY(identicalFixture.connectSimkl());
    QVERIFY(identicalFixture.enableTitleMatchingWithProgress());
    QObject::connect(identicalFixture.model.get(),
        &TrackerSyncCenterModel::findMatchRequested,
        identicalFixture.model.get(), [](const QString &, const QString &, quint64) {});
    identicalFixture.titleProgress->record({
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("id"), QStringLiteral("private-video-id-3")},
        {QStringLiteral("title"), QStringLiteral("Colosseum candidate title")},
        {QStringLiteral("progress"), 0.35},
        {QStringLiteral("watched"), false}});
    QVERIFY(identicalFixture.titleOwner->candidateSearchAvailable());
    const auto identicalBatch = identicalFixture.unmatchedPreview(
        QStringLiteral("title-match-identical-duplicate-snapshot"));
    QVERIFY(identicalBatch);
    const QVariantMap identicalReview = identicalFixture.model->importReviewSnapshot(
        identicalBatch->batchId, identicalFixture.model->revision());
    const QVariantList identicalItems = identicalReview.value(QStringLiteral("items")).toList();
    const QString identicalItemId = identicalItems.first().toMap()
        .value(QStringLiteral("reviewItemId")).toString();
    QStringList allowedChoices;
    for (const QVariant &choice : identicalItems.first().toMap()
             .value(QStringLiteral("allowedChoices")).toList()) {
        allowedChoices.append(choice.toString());
    }
    QVERIFY(allowedChoices.contains(QStringLiteral("find_match")));
    const QVariantList identicalCandidates = identicalFixture.model->titleMatchCandidates(
        identicalBatch->batchId, identicalItemId, QStringLiteral("Colosseum candidate"),
        identicalFixture.model->revision());
    QVERIFY(identicalCandidates.isEmpty());
}

void TrackerSyncCenterTest::titleMatchAvailabilityTracksProgressHealth()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    QVERIFY(fixture.enableTitleMatchingWithProgress());
    QObject::connect(fixture.model.get(), &TrackerSyncCenterModel::findMatchRequested,
        fixture.model.get(), [](const QString &, const QString &, quint64) {});
    const auto batch = fixture.unmatchedPreview(
        QStringLiteral("title-match-progress-health-snapshot"));
    QVERIFY(batch);

    const quint64 healthyRevision = fixture.model->revision();
    QVariantMap review = fixture.model->importReviewSnapshot(batch->batchId, healthyRevision);
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    QVariantList items = review.value(QStringLiteral("items")).toList();
    QCOMPARE(items.size(), 1);
    QStringList healthyChoices;
    for (const QVariant &choice : items.first().toMap()
             .value(QStringLiteral("allowedChoices")).toList()) {
        healthyChoices.append(choice.toString());
    }
    QVERIFY(healthyChoices.contains(QStringLiteral("find_match")));

    QSignalSpy modelChanged(fixture.model.get(), &TrackerSyncCenterModel::modelChanged);
    fixture.titleProgress->forceWatchStatePersistenceFailureForTesting(true);
    fixture.titleProgress->setWatchedMark(QStringLiteral("health-flip"), true);
    QVERIFY(!fixture.titleProgress->healthy());
    QCOMPARE(modelChanged.count(), 1);

    const quint64 unhealthyRevision = fixture.model->revision();
    QVERIFY(unhealthyRevision > healthyRevision);
    review = fixture.model->importReviewSnapshot(batch->batchId, unhealthyRevision);
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    items = review.value(QStringLiteral("items")).toList();
    QCOMPARE(items.size(), 1);
    QStringList unhealthyChoices;
    for (const QVariant &choice : items.first().toMap()
             .value(QStringLiteral("allowedChoices")).toList()) {
        unhealthyChoices.append(choice.toString());
    }
    QVERIFY(!unhealthyChoices.contains(QStringLiteral("find_match")));
}

void TrackerSyncCenterTest::titleMatchPreviewFailureRollsBackOrReportsRetainedMapping()
{
    TrackerSyncCenterFixture rollbackFixture;
    QVERIFY(rollbackFixture.valid());
    QVERIFY(rollbackFixture.connectSimkl());
    QVERIFY(rollbackFixture.enableTitleMatchingWithProgress());
    const auto rollbackBatch = rollbackFixture.unmatchedPreview(
        QStringLiteral("title-match-rollback-snapshot"));
    QVERIFY(rollbackBatch);
    const QString rollbackItemId = rollbackFixture.model->importReviewSnapshot(
        rollbackBatch->batchId, rollbackFixture.model->revision())
            .value(QStringLiteral("items")).toList().first().toMap()
            .value(QStringLiteral("reviewItemId")).toString();
    const QString rollbackCandidateId = rollbackFixture.model->titleMatchCandidates(
        rollbackBatch->batchId, rollbackItemId, QString(), rollbackFixture.model->revision())
            .first().toMap().value(QStringLiteral("candidateId")).toString();
    rollbackFixture.imports->forcePersistenceFailureForTesting(true);
    QVERIFY(!rollbackFixture.model->confirmTitleMatch(rollbackBatch->batchId, rollbackItemId,
        rollbackCandidateId, rollbackFixture.model->revision()));
    QVERIFY(!rollbackFixture.mappings->mapping(rollbackBatch->items.first().remote.remote));
    QCOMPARE(rollbackFixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("preview_refresh_failed_mapping_rolled_back"));

    TrackerSyncCenterFixture retainedFixture;
    QVERIFY(retainedFixture.valid());
    QVERIFY(retainedFixture.connectSimkl());
    QVERIFY(retainedFixture.enableTitleMatchingWithProgress());
    const auto retainedBatch = retainedFixture.unmatchedPreview(
        QStringLiteral("title-match-retained-snapshot"));
    QVERIFY(retainedBatch);
    const QString retainedItemId = retainedFixture.model->importReviewSnapshot(
        retainedBatch->batchId, retainedFixture.model->revision())
            .value(QStringLiteral("items")).toList().first().toMap()
            .value(QStringLiteral("reviewItemId")).toString();
    const QString retainedCandidateId = retainedFixture.model->titleMatchCandidates(
        retainedBatch->batchId, retainedItemId, QString(), retainedFixture.model->revision())
            .first().toMap().value(QStringLiteral("candidateId")).toString();
    retainedFixture.imports->forcePersistenceFailureForTesting(true);
    retainedFixture.mappings->failNextRemovalPersistenceForTesting();
    QVERIFY(!retainedFixture.model->confirmTitleMatch(retainedBatch->batchId, retainedItemId,
        retainedCandidateId, retainedFixture.model->revision()));
    const auto retainedMapping = retainedFixture.mappings->mapping(
        retainedBatch->items.first().remote.remote);
    QVERIFY(retainedMapping);
    QCOMPARE(retainedMapping->provenance, TrackerMappingProvenance::UserConfirmed);
    QCOMPARE(retainedFixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("preview_refresh_failed_mapping_retained"));
    QCOMPARE(retainedFixture.imports->batch(retainedBatch->batchId)->items.first().classification,
             TrackerImportClassification::NeedsMatching);
}

void TrackerSyncCenterTest::newProgressChoicesMatchDurableResolutionRules()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), QStringLiteral("remote-mapped-1")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("colosseum:series:mapped-1"), QStringLiteral("series"),
        QStringLiteral("series:mapped-1"), QStringLiteral("Mapped title")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::ExactProviderIdentity));

    TrackerImportRemoteItem item;
    item.providerItemId = QStringLiteral("provider-item-mapped-1");
    item.remote = remote;
    item.mapping = TrackerTitleMapping{remote, canonical,
        TrackerMappingProvenance::ExactProviderIdentity, 1};
    item.progress = 8;
    item.supported = true;
    item.displayTitle = QStringLiteral("Mapped title");
    item.exactProgressTarget = exactTarget(*item.mapping, 8);
    const auto batch = fixture.imports->createPreview({TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), 1, QStringLiteral("snapshot-mapped-1"),
        QStringLiteral("cursor-mapped-1"), true, true, {item}, {}});
    QVERIFY(batch);
    QCOMPARE(batch->items.first().classification, TrackerImportClassification::NewProgress);

    const QVariantMap review = fixture.model->importReviewSnapshot(
        batch->batchId, fixture.model->revision());
    QVERIFY(review.value(QStringLiteral("pageComplete")).toBool());
    const QVariantMap row = review.value(QStringLiteral("items")).toList().first().toMap();
    const QStringList choices = row.value(QStringLiteral("allowedChoices")).toStringList();
    QVERIFY(choices.contains(QStringLiteral("use_provider_progress")));
    QVERIFY(choices.contains(QStringLiteral("leave_unresolved")));
    QVERIFY(!choices.contains(QStringLiteral("keep_colosseum")));
    QVariantMap batchRow = fixture.model->importReviews().first().toMap();
    QCOMPARE(batchRow.value(QStringLiteral("decisionCount")).toInt(), 1);
    QCOMPARE(batchRow.value(QStringLiteral("awaitingApplyCount")).toInt(), 0);
    QCOMPARE(batchRow.value(QStringLiteral("unresolvedCount")).toInt(), 0);
    QVERIFY(!fixture.model->resolveImportItem(batch->batchId,
        row.value(QStringLiteral("reviewItemId")).toString(),
        QStringLiteral("keep_colosseum"), fixture.model->revision()));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("choice_not_allowed"));

    QVERIFY(fixture.model->resolveImportItem(batch->batchId,
        row.value(QStringLiteral("reviewItemId")).toString(),
        QStringLiteral("use_provider_progress"), fixture.model->revision()));
    const QVariantMap resolvedReview = fixture.model->importReviewSnapshot(
        batch->batchId, fixture.model->revision());
    const QVariantMap resolvedImportRow = resolvedReview.value(QStringLiteral("items"))
        .toList().first().toMap();
    QCOMPARE(resolvedImportRow.value(QStringLiteral("resolution")).toString(),
             QStringLiteral("use_provider_progress"));
    QVariantList openReviews = fixture.model->importReviews();
    QCOMPARE(openReviews.size(), 1);
    batchRow = openReviews.first().toMap();
    QCOMPARE(batchRow.value(QStringLiteral("reviewCount")).toInt(), 1);
    QCOMPARE(batchRow.value(QStringLiteral("decisionCount")).toInt(), 0);
    QCOMPARE(batchRow.value(QStringLiteral("awaitingApplyCount")).toInt(), 1);
    QCOMPARE(batchRow.value(QStringLiteral("unresolvedCount")).toInt(), 0);
    QVERIFY(fixture.model->confirmImport(batch->batchId, fixture.model->revision()));
    // Confirmed progress still remains visible while its canonical owner has
    // not yet supplied an apply receipt.
    openReviews = fixture.model->importReviews();
    QCOMPARE(openReviews.size(), 1);
}

void TrackerSyncCenterTest::staleImportChoiceCannotSurviveTitleRemap()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const QString account = QString::fromLatin1(kRemoteAccountId);
    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, account,
        QStringLiteral("remap-stale-intent-remote")};
    const TrackerCanonicalTitleCandidate firstCanonical{
        QStringLiteral("colosseum:series:remap-first"), QStringLiteral("series"),
        QStringLiteral("series:remap-first"), QStringLiteral("First mapped title")};
    const TrackerCanonicalTitleCandidate secondCanonical{
        QStringLiteral("colosseum:series:remap-second"), QStringLiteral("series"),
        QStringLiteral("series:remap-second"), QStringLiteral("Second mapped title")};
    QVERIFY(fixture.mappings->upsert(remote, firstCanonical,
        TrackerMappingProvenance::UserConfirmed));

    TrackerImportRemoteItem item;
    item.providerItemId = QStringLiteral("remap-stale-intent-item");
    item.remote = remote;
    item.mapping = *fixture.mappings->mapping(remote);
    item.progress = 8;
    item.supported = true;
    item.displayTitle = QStringLiteral("Provider title");
    item.exactProgressTarget = exactTarget(*item.mapping, 8);
    const TrackerImportBatchDraft draft{TrackerProviderId::Simkl, account, 1,
        QStringLiteral("remap-stale-intent-snapshot"),
        QStringLiteral("remap-stale-intent-cursor"), true, true, {item}, {}};
    const auto batch = fixture.imports->createPreview(draft);
    QVERIFY(batch);
    QCOMPARE(batch->items.first().classification,
             TrackerImportClassification::NewProgress);

    const quint64 reviewedRevision = fixture.model->revision();
    const QVariantMap review = fixture.model->importReviewSnapshot(
        batch->batchId, reviewedRevision);
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    const QString publicItemId = review.value(QStringLiteral("items")).toList()
        .first().toMap().value(QStringLiteral("reviewItemId")).toString();
    QVERIFY(!publicItemId.isEmpty());

    QVERIFY(fixture.mappings->upsert(remote, secondCanonical,
        TrackerMappingProvenance::UserConfirmed));
    TrackerImportRemoteItem refreshedItem = item;
    refreshedItem.mapping = *fixture.mappings->mapping(remote);
    refreshedItem.exactProgressTarget = exactTarget(*refreshedItem.mapping, 8);
    const auto refreshedBatch = fixture.imports->createPreview({draft.providerId,
        draft.remoteAccountId, draft.connectionGeneration, draft.snapshotId,
        draft.proposedCursor, draft.initialImport, draft.pageComplete,
        {refreshedItem}, draft.baseCursor});
    QVERIFY(refreshedBatch);
    QCOMPARE(refreshedBatch->items.first().classification,
             TrackerImportClassification::NewProgress);

    const quint64 remappedRevision = fixture.model->revision();
    QVERIFY(remappedRevision > reviewedRevision);
    QVERIFY(!fixture.model->resolveImportItem(batch->batchId, publicItemId,
        QStringLiteral("use_provider_progress"), reviewedRevision));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("stale_intent"));
    const auto currentBatch = fixture.imports->batch(batch->batchId);
    QVERIFY(currentBatch);
    QCOMPARE(currentBatch->items.first().state, TrackerImportItemState::ReviewRequired);
    QCOMPARE(currentBatch->items.first().resolution, TrackerImportResolution::None);
    QCOMPARE(currentBatch->items.first().remote.mapping->canonical.canonicalMediaId,
             secondCanonical.canonicalMediaId);
}

void TrackerSyncCenterTest::selectedBulkImportReviewRevalidatesEveryChoiceAndKeepsExceptionsVisible()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const TrackerCanonicalTitleCandidate canonicalA{
        QStringLiteral("ct1:9de96000-0000-4000-8000-000000000021"),
        QStringLiteral("series"), QStringLiteral("series:bulk-a"), QStringLiteral("Bulk A")};
    const TrackerCanonicalTitleCandidate canonicalB{
        QStringLiteral("ct1:9de96000-0000-4000-8000-000000000022"),
        QStringLiteral("series"), QStringLiteral("series:bulk-b"), QStringLiteral("Bulk B")};
    const TrackerCanonicalTitleCandidate canonicalProtected{
        QStringLiteral("ct1:9de96000-0000-4000-8000-000000000023"),
        QStringLiteral("series"), QStringLiteral("series:protected"), QStringLiteral("Protected")};
    const TrackerRemoteMediaKey remoteA{TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), QStringLiteral("bulk-a")};
    const TrackerRemoteMediaKey remoteB{TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), QStringLiteral("bulk-b")};
    const TrackerRemoteMediaKey remoteProtected{TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), QStringLiteral("bulk-protected")};
    QVERIFY(fixture.mappings->upsert(remoteA, canonicalA,
        TrackerMappingProvenance::ExactProviderIdentity));
    QVERIFY(fixture.mappings->upsert(remoteB, canonicalB,
        TrackerMappingProvenance::ExactProviderIdentity));
    QVERIFY(fixture.mappings->upsert(remoteProtected, canonicalProtected,
        TrackerMappingProvenance::ExactProviderIdentity));

    TrackerImportRemoteItem itemA;
    itemA.providerItemId = QStringLiteral("bulk-item-a");
    itemA.remote = remoteA;
    itemA.mapping = *fixture.mappings->mapping(remoteA);
    itemA.progress = 5;
    itemA.displayTitle = QStringLiteral("Bulk A");
    itemA.exactProgressTarget = exactTarget(*itemA.mapping, 5, 0.5, false);
    TrackerImportRemoteItem itemB = itemA;
    itemB.providerItemId = QStringLiteral("bulk-item-b");
    itemB.remote = remoteB;
    itemB.mapping = *fixture.mappings->mapping(remoteB);
    itemB.displayTitle = QStringLiteral("Bulk B");
    itemB.exactProgressTarget = exactTarget(*itemB.mapping, 5, 0.5, false);
    TrackerImportRemoteItem protectedItem = itemA;
    protectedItem.providerItemId = QStringLiteral("bulk-item-protected");
    protectedItem.remote = remoteProtected;
    protectedItem.mapping = *fixture.mappings->mapping(remoteProtected);
    protectedItem.progress = 12;
    protectedItem.contradictsNativeHistory = true;
    protectedItem.displayTitle = QStringLiteral("Protected");
    protectedItem.localAtPreview = TrackerImportedProgressValue{
        canonicalProtected.canonicalMediaId, canonicalProtected.historyKind,
        canonicalProtected.historyId, 8, false, 7, true};
    protectedItem.exactProgressTarget = exactTarget(*protectedItem.mapping, 12, 0.8, false);
    protectedItem.localAtPreview->exactProgressTarget =
        exactTarget(*protectedItem.mapping, 12, 0.4, false);

    const auto batch = fixture.imports->createPreview({TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), 1, QStringLiteral("snapshot-bulk-selection"),
        QStringLiteral("cursor-bulk-selection"), true, true,
        {itemA, itemB, protectedItem}, {}});
    QVERIFY(batch);
    QCOMPARE(batch->items.at(0).classification, TrackerImportClassification::NewProgress);
    QCOMPARE(batch->items.at(1).classification, TrackerImportClassification::NewProgress);
    QCOMPARE(batch->items.at(2).classification, TrackerImportClassification::Disagreement);

    const QVariantMap snapshot = fixture.model->importReviewSnapshot(
        batch->batchId, fixture.model->revision());
    QVERIFY(snapshot.value(QStringLiteral("accepted")).toBool());
    QHash<QString, QString> publicIds;
    for (const QVariant &rowValue : snapshot.value(QStringLiteral("items")).toList()) {
        const QVariantMap row = rowValue.toMap();
        publicIds.insert(row.value(QStringLiteral("title")).toString(),
                         row.value(QStringLiteral("reviewItemId")).toString());
    }
    const QString idA = publicIds.value(QStringLiteral("Bulk A"));
    const QString idB = publicIds.value(QStringLiteral("Bulk B"));
    const QString idProtected = publicIds.value(QStringLiteral("Protected"));
    QVERIFY(!idA.isEmpty() && !idB.isEmpty() && !idProtected.isEmpty());

    QVERIFY(!fixture.model->resolveImportItems(batch->batchId, {idA, idProtected},
        QStringLiteral("use_provider_progress"), fixture.model->revision()));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("choice_not_allowed"));
    const auto afterRejected = fixture.imports->batch(batch->batchId);
    QVERIFY(afterRejected);
    for (const TrackerImportItem &item : afterRejected->items)
        QCOMPARE(item.state, TrackerImportItemState::ReviewRequired);

    QVERIFY(fixture.model->resolveImportItems(batch->batchId, {idA, idB},
        QStringLiteral("use_provider_progress"), fixture.model->revision()));
    const auto afterAccepted = fixture.imports->batch(batch->batchId);
    QVERIFY(afterAccepted);
    QCOMPARE(afterAccepted->items.at(0).state, TrackerImportItemState::AwaitingApply);
    QCOMPARE(afterAccepted->items.at(1).state, TrackerImportItemState::AwaitingApply);
    QCOMPARE(afterAccepted->items.at(2).state, TrackerImportItemState::ReviewRequired);
}

void TrackerSyncCenterTest::disconnectIsRevisionFencedAndUsesOnlyExplicitLocalChoices()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    int calls = 0;
    QString observedProvider;
    QString observedChoice;
    auto model = fixture.makeModel(testCatalogue(), nullptr, nullptr, nullptr,
        nullptr, nullptr, {}, [&](const QString &providerKey, const QString &choice, QString *) {
            ++calls;
            observedProvider = providerKey;
            observedChoice = choice;
            return true;
        });

    QVERIFY(model->providerDossier(QStringLiteral("simkl"))
                .value(QStringLiteral("disconnectEnabled")).toBool());
    const quint64 staleRevision = model->revision();
    QVERIFY(fixture.settings->setGlobalSetting(QStringLiteral("checkOnLaunch"), false));
    QVERIFY(!model->disconnectTracker(QStringLiteral("simkl"), QStringLiteral("keep_paused"),
                                      staleRevision));
    QCOMPARE(calls, 0);
    QCOMPARE(model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("stale_intent"));

    quint64 revision = model->revision();
    QVERIFY(!model->disconnectTracker(QStringLiteral("simkl"), QStringLiteral("delete_account"),
                                      revision));
    QCOMPARE(calls, 0);
    QCOMPARE(model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("choice_not_allowed"));

    revision = model->revision();
    QVERIFY(model->disconnectTracker(QStringLiteral("simkl"), QStringLiteral("keep_paused"),
                                     revision));
    QCOMPARE(calls, 1);
    QCOMPARE(observedProvider, QStringLiteral("simkl"));
    QCOMPARE(observedChoice, QStringLiteral("keep_paused"));
    QCOMPARE(model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("disconnected_paused"));
}

void TrackerSyncCenterTest::disconnectRefusesInflightDeliveryThenKeepsUnknownWorkVisible()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const TrackerScrobbleIntent intent = fixture.pendingScrobble(
        QStringLiteral("operation-inflight-disconnect"));
    QVERIFY(fixture.scrobble->recordIntent(intent));
    QVERIFY(fixture.scrobble->markDelivering(intent.operationId));

    int disconnectCalls = 0;
    auto model = fixture.makeModel(testCatalogue(), nullptr, nullptr, nullptr,
        nullptr, nullptr, {}, [&](const QString &, const QString &, QString *) {
            ++disconnectCalls;
            return true;
        });
    QVariantMap dossier = model->providerDossier(QStringLiteral("simkl"));
    QVERIFY(!dossier.value(QStringLiteral("disconnectEnabled")).toBool());
    QVERIFY(!model->disconnectTracker(QStringLiteral("simkl"), QStringLiteral("keep_paused"),
                                      model->revision()));
    QCOMPARE(model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("delivery_in_flight"));
    QCOMPARE(disconnectCalls, 0);

    QVERIFY(fixture.scrobble->recordAttemptResult(intent.operationId,
        TrackerScrobbleAttemptResult::UnknownOutcome,
        TrackerScrobbleReason::AcknowledgementLost));
    dossier = model->providerDossier(QStringLiteral("simkl"));
    QVERIFY(dossier.value(QStringLiteral("disconnectEnabled")).toBool());
    QCOMPARE(model->deliveryRows(QStringLiteral("simkl")).size(), 1);
    QVERIFY(model->disconnectTracker(QStringLiteral("simkl"), QStringLiteral("keep_paused"),
                                     model->revision()));
    QCOMPARE(disconnectCalls, 1);
}

void TrackerSyncCenterTest::disconnectedUnknownWorkRemainsInSyncAttentionAndDossier()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const TrackerScrobbleIntent intent = fixture.pendingScrobble(
        QStringLiteral("operation-disconnected-unknown"));
    QVERIFY(fixture.scrobble->recordIntent(intent));
    QVERIFY(fixture.scrobble->markDelivering(intent.operationId));
    QVERIFY(fixture.scrobble->recordAttemptResult(intent.operationId,
        TrackerScrobbleAttemptResult::UnknownOutcome,
        TrackerScrobbleReason::AcknowledgementLost));

    auto model = fixture.makeModel(testCatalogue(), nullptr, nullptr, nullptr,
        nullptr, nullptr, {}, [&](const QString &providerKey, const QString &, QString *error) {
            const auto providerId = trackerProviderIdFromKey(providerKey);
            const auto connection = providerId
                ? fixture.connections->connection(*providerId) : std::nullopt;
            return providerId && connection && fixture.connections->setDisconnected(
                *providerId, connection->remoteAccountId, connection->connectionGeneration,
                error);
        });
    QVERIFY(model->disconnectTracker(QStringLiteral("simkl"), QStringLiteral("keep_paused"),
                                     model->revision()));

    const QVariantMap dossier = model->providerDossier(QStringLiteral("simkl"));
    QCOMPARE(dossier.value(QStringLiteral("connected")).toBool(), false);
    QCOMPARE(dossier.value(QStringLiteral("status")).toString(), QStringLiteral("Disconnected"));
    QCOMPARE(model->deliveryRows(QStringLiteral("simkl")).size(), 1);
    QCOMPARE(dossier.value(QStringLiteral("pendingCount")).toInt(), 1);
    QCOMPARE(dossier.value(QStringLiteral("unknownOutcomeCount")).toInt(), 1);
    const QVariantMap aggregate = model->aggregateState();
    QCOMPARE(aggregate.value(QStringLiteral("connectedCount")).toInt(), 0);
    QCOMPARE(aggregate.value(QStringLiteral("attentionProviderCount")).toInt(), 1);
    QCOMPARE(aggregate.value(QStringLiteral("status")).toString(), QStringLiteral("Attention"));
    QCOMPARE(aggregate.value(QStringLiteral("healthy")).toBool(), false);
    QCOMPARE(model->catalogue().first().toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("Attention"));
    QCOMPARE(fixture.scrobble->intent(intent.operationId)->state,
             TrackerScrobbleState::UnknownOutcome);
}

void TrackerSyncCenterTest::disconnectedKnownUnsentCanBeRetriedWithoutReconnect()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const TrackerScrobbleIntent intent = fixture.pendingScrobble(
        QStringLiteral("operation-discard-retry"));
    QVERIFY(fixture.scrobble->recordIntent(intent));

    int disconnectCalls = 0;
    auto model = fixture.makeModel(testCatalogue(), nullptr, nullptr, nullptr,
        nullptr, nullptr, {}, [&](const QString &providerKey, const QString &,
                                  QString *error) {
            ++disconnectCalls;
            const auto providerId = trackerProviderIdFromKey(providerKey);
            if (!providerId)
                return false;
            if (disconnectCalls == 1) {
                const auto connection = fixture.connections->connection(*providerId);
                if (!connection || !fixture.connections->setDisconnected(
                        *providerId, connection->remoteAccountId,
                        connection->connectionGeneration, error))
                    return false;
                if (error)
                    *error = QStringLiteral("storage interrupted");
                return false;
            }
            return fixture.scrobble->discardKnownUnsent(
                       *providerId, QString::fromLatin1(kRemoteAccountId), error) == 1;
        });

    QVERIFY(!model->disconnectTracker(QStringLiteral("simkl"),
        QStringLiteral("discard_known_unsent"), model->revision()));
    QCOMPARE(model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("disconnect_cleanup_pending"));
    QVariantMap dossier = model->providerDossier(QStringLiteral("simkl"));
    QCOMPARE(dossier.value(QStringLiteral("connected")).toBool(), false);
    QCOMPARE(dossier.value(QStringLiteral("knownUnsentCount")).toInt(), 1);
    QVERIFY(dossier.value(QStringLiteral("cleanupEnabled")).toBool());
    QVERIFY(!dossier.value(QStringLiteral("connectEnabled")).toBool());

    QVERIFY(model->disconnectTracker(QStringLiteral("simkl"),
        QStringLiteral("discard_known_unsent"), model->revision()));
    QCOMPARE(disconnectCalls, 2);
    dossier = model->providerDossier(QStringLiteral("simkl"));
    QCOMPARE(dossier.value(QStringLiteral("knownUnsentCount")).toInt(), 0);
    QVERIFY(!dossier.value(QStringLiteral("cleanupEnabled")).toBool());
    QVERIFY(fixture.scrobble->intents().first().state == TrackerScrobbleState::Superseded);
}

void TrackerSyncCenterTest::removeImportedDataDeletesOnlyThatTrackerSource()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const TrackerRemoteMediaKey simklRemote{TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), QStringLiteral("simkl:event-title")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("ct1:9de96000-0000-4000-8000-000000000001"),
        QStringLiteral("series"), QStringLiteral("series:frieren"),
        QStringLiteral("Frieren: Beyond Journey's End")};
    QVERIFY(fixture.mappings->upsert(simklRemote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    TrackerImportedHistoryEvidence simklEvidence;
    simklEvidence.mapping = *fixture.mappings->mapping(simklRemote);
    simklEvidence.eventKind = TrackerImportedEventKind::Completion;
    simklEvidence.providerEventId = QStringLiteral("simkl-event-1");
    simklEvidence.importSnapshotId = QStringLiteral("simkl-snapshot-1");
    simklEvidence.occurredAtMs = 1720000000123;
    simklEvidence.timestampSource = TrackerEvidenceTimestampSource::ProviderEvent;
    simklEvidence.timestampPrecision = TrackerEvidenceTimestampPrecision::ExactMillisecond;
    simklEvidence.eventPayloadFingerprint = QStringLiteral("completion:simkl-event-1");
    QVERIFY(fixture.historyEvidence->record(simklEvidence));

    const TrackerRemoteMediaKey traktRemote{TrackerProviderId::Trakt,
        QStringLiteral("other-trakt-account"), QStringLiteral("trakt:event-title")};
    QVERIFY(fixture.mappings->upsert(traktRemote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    TrackerImportedHistoryEvidence traktEvidence = simklEvidence;
    traktEvidence.mapping = *fixture.mappings->mapping(traktRemote);
    traktEvidence.providerEventId = QStringLiteral("trakt-event-1");
    traktEvidence.importSnapshotId = QStringLiteral("trakt-snapshot-1");
    traktEvidence.eventPayloadFingerprint = QStringLiteral("completion:trakt-event-1");
    QVERIFY(fixture.historyEvidence->record(traktEvidence));

    ProgressStore progress(fixture.root.path() + QStringLiteral("/progress.ini"));
    TrackerProgressImportOwner progressOwner(&progress);
    fixture.model->setImportOwner(&progressOwner);
    const auto simklMapping = fixture.mappings->mapping(simklRemote);
    const auto traktMapping = fixture.mappings->mapping(traktRemote);
    QVERIFY(simklMapping && traktMapping);
    const auto applyImported = [&](const QString &operationId,
                                   const TrackerTitleMapping &mapping,
                                   const TrackerImportProgressTarget &target) {
        bool applied = false;
        progressOwner.applyImportedProgressAsync(
            operationId, mapping, target, std::nullopt, 8, false,
            [&applied](TrackerImportOwnerApplyResult status, auto, const QString &) {
                applied = status == TrackerImportOwnerApplyResult::Applied;
            });
        QTRY_VERIFY(applied);
    };
    const TrackerImportProgressTarget simklTarget{
        canonical.canonicalMediaId, QStringLiteral("video"),
        canonical.historyId + QStringLiteral(":1"), 0.5, false};
    const TrackerImportProgressTarget traktTarget{
        canonical.canonicalMediaId, QStringLiteral("video"),
        canonical.historyId + QStringLiteral(":2"), 0.7, false};
    const TrackerImportProgressTarget locallyEditedTarget{
        canonical.canonicalMediaId, QStringLiteral("video"),
        canonical.historyId + QStringLiteral(":3"), 0.3, false};
    applyImported(QStringLiteral("simkl-progress-import-1"), *simklMapping, simklTarget);
    applyImported(QStringLiteral("trakt-progress-import-1"), *traktMapping, traktTarget);
    applyImported(QStringLiteral("simkl-progress-import-2"), *simklMapping,
                  locallyEditedTarget);
    QVariantMap localEdit{{QStringLiteral("kind"), QStringLiteral("video")},
                         {QStringLiteral("id"), locallyEditedTarget.id},
                         {QStringLiteral("title"), QStringLiteral("Local edit")},
                         {QStringLiteral("progress"), 0.4},
                         {QStringLiteral("watched"), false}};
    progress.record(localEdit);
    QCOMPARE(progress.trackerImportedProgressCount(
                 QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)), 1);
    QCOMPARE(progress.trackerImportedProgressCount(
                 QStringLiteral("trakt"), QStringLiteral("other-trakt-account")), 1);
    QVERIFY(!progress.get(QStringLiteral("video"), simklTarget.id)
                 .contains(QStringLiteral("_trackerRemoteAccountId")));

    QVariantMap dossier = fixture.model->providerDossier(QStringLiteral("simkl"));
    QCOMPARE(dossier.value(QStringLiteral("importedHistoryCount")).toInt(), 1);
    QCOMPARE(dossier.value(QStringLiteral("importedProgressCount")).toInt(), 1);
    const QVariantList removalPreview = dossier.value(
        QStringLiteral("importedDataRemovalPreview")).toList();
    QCOMPARE(removalPreview.size(), 2);
    QCOMPARE(removalPreview.first().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Frieren: Beyond Journey's End"));
    for (const QVariant &item : removalPreview) {
        const QVariantMap previewItem = item.toMap();
        QCOMPARE(keys(previewItem), keySet({"title", "dataKind", "consequence"}));
        QVERIFY(!previewItem.contains(QStringLiteral("remoteAccountId")));
        QVERIFY(!previewItem.contains(QStringLiteral("remoteMediaId")));
    }
    QVERIFY(dossier.value(QStringLiteral("removeImportedEnabled")).toBool());
    QVERIFY(fixture.model->removeImportedData(QStringLiteral("simkl"),
                                                 fixture.model->revision()));
    QTRY_COMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
                 QStringLiteral("removed"));
    QCOMPARE(fixture.historyEvidence->contributions().size(), 1);
    QCOMPARE(fixture.historyEvidence->contributions().first().mapping.remote.providerId,
             TrackerProviderId::Trakt);
    QVERIFY(fixture.historyEvidence->sourceRemovalSuppressed(
        TrackerProviderId::Simkl, QString::fromLatin1(kRemoteAccountId)));
    QCOMPARE(progress.trackerImportedProgressCount(
                 QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)), 0);
    QCOMPARE(progress.trackerImportedProgressCount(
                 QStringLiteral("trakt"), QStringLiteral("other-trakt-account")), 1);
    QVERIFY(progress.get(QStringLiteral("video"), simklTarget.id).isEmpty());
    QCOMPARE(progress.get(QStringLiteral("video"), locallyEditedTarget.id)
                 .value(QStringLiteral("progress")).toDouble(), 0.4);
    QVERIFY(!progress.get(QStringLiteral("video"), locallyEditedTarget.id)
                 .contains(QStringLiteral("_trackerOrigin")));
    QVERIFY(!progress.get(QStringLiteral("video"), traktTarget.id).isEmpty());

    dossier = fixture.model->providerDossier(QStringLiteral("simkl"));
    QCOMPARE(dossier.value(QStringLiteral("importedHistoryCount")).toInt(), 0);
    QCOMPARE(dossier.value(QStringLiteral("importedProgressCount")).toInt(), 0);
    QVERIFY(dossier.value(QStringLiteral("importedDataRemovalPreview")).toList().isEmpty());
    QVERIFY(!dossier.value(QStringLiteral("removeImportedEnabled")).toBool());
}

void TrackerSyncCenterTest::removeImportedOverlayPreservesNativeAndOtherProviderProgress()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("ct1:9de96000-0000-4000-8000-000000000003"),
        QStringLiteral("series"), QStringLiteral("series:shared-progress"),
        QStringLiteral("Shared progress title")};
    const TrackerRemoteMediaKey simklRemote{TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), QStringLiteral("simkl:shared-progress")};
    const TrackerRemoteMediaKey traktRemote{TrackerProviderId::Trakt,
        QStringLiteral("other-trakt-account"), QStringLiteral("trakt:shared-progress")};
    QVERIFY(fixture.mappings->upsert(simklRemote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    QVERIFY(fixture.mappings->upsert(traktRemote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    const auto simklMapping = fixture.mappings->mapping(simklRemote);
    const auto traktMapping = fixture.mappings->mapping(traktRemote);
    QVERIFY(simklMapping && traktMapping);

    ProgressStore progress(fixture.root.path() + QStringLiteral("/shared-progress.ini"));
    const TrackerImportProgressTarget target{
        canonical.canonicalMediaId, QStringLiteral("video"),
        canonical.historyId + QStringLiteral(":1"), 0.0, false};
    progress.record({{QStringLiteral("kind"), QStringLiteral("video")},
                     {QStringLiteral("id"), target.id},
                     {QStringLiteral("title"), QStringLiteral("Saved locally")},
                     {QStringLiteral("caption"), QStringLiteral("Local resume point")},
                     {QStringLiteral("progress"), 0.25},
                     {QStringLiteral("watched"), false}});
    TrackerProgressImportOwner progressOwner(&progress);
    fixture.model->setImportOwner(&progressOwner);

    const auto apply = [&progressOwner, &target](const QString &operationId,
                                                  const TrackerTitleMapping &mapping,
                                                  double fraction) {
        TrackerImportProgressTarget sourceTarget = target;
        sourceTarget.fraction = fraction;
        const auto expected = progressOwner.currentProgress(mapping, sourceTarget);
        bool applied = false;
        progressOwner.applyImportedProgressAsync(
            operationId, mapping, sourceTarget, expected, 8, false,
            [&applied](TrackerImportOwnerApplyResult status, auto, const QString &) {
                applied = status == TrackerImportOwnerApplyResult::Applied;
            });
        QTRY_VERIFY(applied);
    };
    apply(QStringLiteral("simkl-shared-progress-1"), *simklMapping, 0.5);
    apply(QStringLiteral("trakt-shared-progress-1"), *traktMapping, 0.7);

    QCOMPARE(progress.trackerImportedProgressCount(
                 QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)), 1);
    QCOMPARE(progress.trackerImportedProgressCount(
                 QStringLiteral("trakt"), QStringLiteral("other-trakt-account")), 1);
    QVERIFY(fixture.model->removeImportedData(QStringLiteral("simkl"),
                                               fixture.model->revision()));
    QTRY_COMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
                 QStringLiteral("removed"));

    QCOMPARE(progress.trackerImportedProgressCount(
                 QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)), 0);
    QCOMPARE(progress.trackerImportedProgressCount(
                 QStringLiteral("trakt"), QStringLiteral("other-trakt-account")), 1);
    QCOMPARE(progress.deliveryEntry(QStringLiteral("video"), target.id)
                 .value(QStringLiteral("progress")).toDouble(), 0.7);
    QVERIFY(!progress.get(QStringLiteral("video"), target.id)
                 .contains(QStringLiteral("_trackerSources")));
    QVERIFY(!progress.syncEntries().first().toMap().contains(QStringLiteral("_trackerSources")));

    bool traktRemoved = false;
    progressOwner.removeImportedProgressAsync(
        TrackerProviderId::Trakt, QStringLiteral("other-trakt-account"),
        [&traktRemoved](bool removed, int count, const QString &) {
            traktRemoved = removed && count == 1;
        });
    QTRY_VERIFY(traktRemoved);
    const QVariantMap restored = progress.get(QStringLiteral("video"), target.id);
    QCOMPARE(restored.value(QStringLiteral("progress")).toDouble(), 0.25);
    QCOMPARE(restored.value(QStringLiteral("title")).toString(), QStringLiteral("Saved locally"));
    QCOMPARE(progress.deliveryEntry(QStringLiteral("video"), target.id)
                 .value(QStringLiteral("_trackerOrigin")).toString(),
             QStringLiteral("native_local"));
}

void TrackerSyncCenterTest::importedProgressChangesFenceRemovalConfirmation()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), QStringLiteral("simkl:fenced-progress")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("ct1:9de96000-0000-4000-8000-000000000004"),
        QStringLiteral("series"), QStringLiteral("series:fenced-progress"),
        QStringLiteral("Fenced progress title")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    const auto mapping = fixture.mappings->mapping(remote);
    QVERIFY(mapping);

    ProgressStore progress(fixture.root.path() + QStringLiteral("/fenced-progress.ini"));
    TrackerProgressImportOwner progressOwner(&progress);
    fixture.model->setImportOwner(&progressOwner);
    const auto apply = [&progressOwner, &mapping, &canonical](const QString &operationId,
                                                               const QString &suffix) {
        const TrackerImportProgressTarget target{
            canonical.canonicalMediaId, QStringLiteral("video"),
            canonical.historyId + suffix, 0.5, false};
        bool applied = false;
        progressOwner.applyImportedProgressAsync(
            operationId, *mapping, target, std::nullopt, 8, false,
            [&applied](TrackerImportOwnerApplyResult result, auto, const QString &) {
                applied = result == TrackerImportOwnerApplyResult::Applied;
            });
        QTRY_VERIFY(applied);
    };

    apply(QStringLiteral("fenced-progress-1"), QStringLiteral(":1"));
    const QVariantMap displayedDossier = fixture.model->providerDossier(QStringLiteral("simkl"));
    const quint64 displayedRevision = displayedDossier.value(
        QStringLiteral("revision")).toULongLong();
    QCOMPARE(displayedDossier.value(QStringLiteral("importedProgressCount")).toInt(), 1);
    QCOMPARE(displayedDossier.value(QStringLiteral("importedDataRemovalPreview")).toList().size(), 1);

    apply(QStringLiteral("fenced-progress-2"), QStringLiteral(":2"));
    QVERIFY(fixture.model->revision() > displayedRevision);
    QVERIFY(!fixture.model->removeImportedData(QStringLiteral("simkl"), displayedRevision));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("stale_intent"));
    QCOMPARE(progress.trackerImportedProgressCount(
                 QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)), 2);
}

void TrackerSyncCenterTest::removeProgressOnlyDataSuppressesRoutineReimport()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), QStringLiteral("simkl:progress-only-title")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("ct1:9de96000-0000-4000-8000-000000000002"),
        QStringLiteral("series"), QStringLiteral("series:progress-only"),
        QStringLiteral("Progress only title")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    const auto mapping = fixture.mappings->mapping(remote);
    QVERIFY(mapping.has_value());

    const QString progressPath = fixture.root.path() + QStringLiteral("/progress.ini");
    const TrackerImportProgressTarget importedTarget{
        canonical.canonicalMediaId, QStringLiteral("video"),
        canonical.historyId + QStringLiteral(":1"), 0.5, false};
    {
        ProgressStore progress(progressPath);
        TrackerProgressImportOwner progressOwner(&progress);
        fixture.model->setImportOwner(&progressOwner);
        progress.record({{QStringLiteral("kind"), QStringLiteral("video")},
                         {QStringLiteral("id"), importedTarget.id},
                         {QStringLiteral("title"), QStringLiteral("Saved locally")},
                         {QStringLiteral("progress"), 0.25},
                         {QStringLiteral("watched"), false}});

        TrackerImportRemoteItem initialItem;
        initialItem.providerItemId = QStringLiteral("simkl-progress-only-title");
        initialItem.remote = remote;
        initialItem.mapping = *mapping;
        initialItem.progress = 8;
        initialItem.displayTitle = canonical.displayName;
        initialItem.exactProgressTarget = importedTarget;
        initialItem.localAtPreview = progressOwner.currentProgress(*mapping, importedTarget);
        const auto initial = fixture.imports->createPreview({TrackerProviderId::Simkl,
            QString::fromLatin1(kRemoteAccountId), 1,
            QStringLiteral("simkl-progress-only-initial"),
            QStringLiteral("simkl-progress-only-cursor-1"), true, true, {initialItem}, {}});
        QVERIFY(initial.has_value());
        QCOMPARE(initial->items.first().classification,
                 TrackerImportClassification::RemoteAdvance);
        QVERIFY(fixture.imports->resolve(initial->batchId, initial->items.first().itemId,
            TrackerImportResolution::UseProviderProgress));
        QVERIFY(fixture.imports->confirm(initial->batchId));
        bool initialApplied = false;
        fixture.imports->applyConfirmedAsync(initial->batchId, &progressOwner,
            [&initialApplied](bool accepted, const QString &) {
                initialApplied = accepted;
            });
        QTRY_VERIFY(initialApplied);

        QVariantMap dossier = fixture.model->providerDossier(QStringLiteral("simkl"));
        QCOMPARE(dossier.value(QStringLiteral("importedHistoryCount")).toInt(), 0);
        QCOMPARE(dossier.value(QStringLiteral("importedProgressCount")).toInt(), 1);
        QVERIFY(fixture.model->removeImportedData(QStringLiteral("simkl"),
                                                   fixture.model->revision()));
        QTRY_COMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
                     QStringLiteral("removed"));
        QCOMPARE(progress.trackerImportedProgressCount(
            QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)), 0);
        QCOMPARE(progress.deliveryEntry(QStringLiteral("video"), importedTarget.id)
                     .value(QStringLiteral("progress")).toDouble(), 0.25);
        QCOMPARE(progress.deliveryEntry(QStringLiteral("video"), importedTarget.id)
                     .value(QStringLiteral("_trackerOrigin")).toString(),
                 QStringLiteral("native_local"));
        QVERIFY(progress.trackerProgressSourceRemovalSuppressed(
            QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)));
        QVERIFY(fixture.historyEvidence->contributions().isEmpty());
        QVERIFY(fixture.historyEvidence->sourceRemovalSuppressed(
            TrackerProviderId::Simkl, QString::fromLatin1(kRemoteAccountId)));

        TrackerImportedHistoryEvidence laterEvidence;
        laterEvidence.mapping = *mapping;
        laterEvidence.eventKind = TrackerImportedEventKind::Completion;
        laterEvidence.providerEventId = QStringLiteral("simkl-later-history-event");
        laterEvidence.importSnapshotId = QStringLiteral("simkl-later-history-snapshot");
        laterEvidence.occurredAtMs = 1720000000456;
        laterEvidence.timestampSource = TrackerEvidenceTimestampSource::ProviderEvent;
        laterEvidence.timestampPrecision = TrackerEvidenceTimestampPrecision::ExactMillisecond;
        laterEvidence.eventPayloadFingerprint = QStringLiteral("completion:simkl-later-history-event");
        QString error;
        QVERIFY(!fixture.historyEvidence->record(laterEvidence, &error));
        QVERIFY(error.contains(QStringLiteral("removed")));
        progress.flush();
        fixture.model->setImportOwner(nullptr);
    }

    // The removal choice must survive a restart of the profile-local Continue store.
    ProgressStore reopenedProgress(progressPath);
    TrackerProgressImportOwner reopenedOwner(&reopenedProgress);
    QVERIFY(reopenedProgress.trackerProgressSourceRemovalSuppressed(
        QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)));

    TrackerImportRemoteItem routineItem;
    routineItem.providerItemId = QStringLiteral("simkl-progress-only-routine");
    routineItem.remote = remote;
    routineItem.mapping = *mapping;
    // Keep the provider's chapter count stable while its precise resume
    // fraction advances. This remains a true RemoteAdvance after the local
    // overlay has been removed and its source checkpoint is retained.
    routineItem.progress = 8;
    routineItem.displayTitle = canonical.displayName;
    const TrackerImportProgressTarget routineTarget{
        canonical.canonicalMediaId, QStringLiteral("video"), importedTarget.id, 0.75, false};
    routineItem.exactProgressTarget = routineTarget;
    routineItem.localAtPreview = reopenedOwner.currentProgress(*mapping, routineTarget);
    QVERIFY(routineItem.localAtPreview.has_value());
    QCOMPARE(routineItem.localAtPreview->exactProgressTarget->fraction, 0.25);

    const auto initialCursor = fixture.imports->confirmedCursor(
        TrackerProviderId::Simkl, QString::fromLatin1(kRemoteAccountId));
    QVERIFY(initialCursor.has_value());
    const auto routine = fixture.imports->createPreview({TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), 1,
        QStringLiteral("simkl-progress-only-routine"),
        QStringLiteral("simkl-progress-only-cursor-2"), false, true,
        {routineItem}, *initialCursor});
    QVERIFY(routine.has_value());
    QCOMPARE(routine->items.first().classification,
             TrackerImportClassification::RemoteAdvance);
    bool routineFinished = false;
    bool routineAccepted = false;
    fixture.imports->applyRoutineSafeAsync(routine->batchId, &reopenedOwner, true,
        [&routineFinished, &routineAccepted](bool accepted, const QString &) {
            routineFinished = true;
            routineAccepted = accepted;
        });
    QTRY_VERIFY(routineFinished);
    QVERIFY(routineAccepted);
    const auto routineSettled = fixture.imports->batch(routine->batchId);
    QVERIFY(routineSettled.has_value());
    QCOMPARE(routineSettled->items.first().state, TrackerImportItemState::Unresolved);
    QCOMPARE(routineSettled->items.first().resolution,
             TrackerImportResolution::LeaveUnresolved);
    QCOMPARE(reopenedProgress.deliveryEntry(QStringLiteral("video"), importedTarget.id)
                 .value(QStringLiteral("progress")).toDouble(), 0.25);
    QCOMPARE(reopenedProgress.deliveryEntry(QStringLiteral("video"), importedTarget.id)
                 .value(QStringLiteral("_trackerOrigin")).toString(),
             QStringLiteral("native_local"));
    QCOMPARE(reopenedProgress.trackerImportedProgressCount(
        QStringLiteral("simkl"), QString::fromLatin1(kRemoteAccountId)), 0);

    bool staleWriteRejected = false;
    reopenedOwner.applyImportedProgressAsync(
        QStringLiteral("simkl-stale-queued-progress"), *mapping, routineTarget,
        routineItem.localAtPreview, 12, false,
        [&staleWriteRejected](TrackerImportOwnerApplyResult status, auto, const QString &) {
            staleWriteRejected = status == TrackerImportOwnerApplyResult::Stale;
        });
    QVERIFY(staleWriteRejected);

    // Removing History-only imported data must install the same Progress
    // source suppression, even when that provider currently owns no Progress rows.
    const QString historyOnlyAccount = QStringLiteral("history-only-account");
    QVERIFY(fixture.connections->upsert({TrackerProviderId::Trakt, historyOnlyAccount,
        1, 2345, TrackerProviderCapability::ReadHistory, TrackerConnectionState::Connected}));
    const TrackerRemoteMediaKey historyOnlyRemote{TrackerProviderId::Trakt,
        historyOnlyAccount, QStringLiteral("trakt:history-only-title")};
    const TrackerCanonicalTitleCandidate historyOnlyCanonical{
        QStringLiteral("ct1:9de96000-0000-4000-8000-000000000003"),
        QStringLiteral("series"), QStringLiteral("series:history-only"),
        QStringLiteral("History only title")};
    QVERIFY(fixture.mappings->upsert(historyOnlyRemote, historyOnlyCanonical,
        TrackerMappingProvenance::UserConfirmed));
    const auto historyOnlyMapping = fixture.mappings->mapping(historyOnlyRemote);
    QVERIFY(historyOnlyMapping.has_value());
    const TrackerImportProgressTarget historyOnlyTarget{
        historyOnlyCanonical.canonicalMediaId, QStringLiteral("video"),
        historyOnlyCanonical.historyId + QStringLiteral(":1"), 0.25, false};
    reopenedProgress.record({{QStringLiteral("kind"), QStringLiteral("video")},
                             {QStringLiteral("id"), historyOnlyTarget.id},
                             {QStringLiteral("title"), QStringLiteral("Local progress")},
                             {QStringLiteral("progress"), 0.25},
                             {QStringLiteral("watched"), false}});

    TrackerImportRemoteItem historyOnlyInitialItem;
    historyOnlyInitialItem.providerItemId = QStringLiteral("trakt-history-only-progress");
    historyOnlyInitialItem.remote = historyOnlyRemote;
    historyOnlyInitialItem.mapping = *historyOnlyMapping;
    historyOnlyInitialItem.progress = 3;
    historyOnlyInitialItem.displayTitle = historyOnlyCanonical.displayName;
    historyOnlyInitialItem.exactProgressTarget = historyOnlyTarget;
    historyOnlyInitialItem.localAtPreview = reopenedOwner.currentProgress(
        *historyOnlyMapping, historyOnlyTarget);
    QVERIFY(historyOnlyInitialItem.localAtPreview.has_value());
    const auto historyOnlyInitial = fixture.imports->createPreview({TrackerProviderId::Trakt,
        historyOnlyAccount, 1, QStringLiteral("trakt-history-only-initial"),
        QStringLiteral("trakt-history-only-cursor-1"), true, true,
        {historyOnlyInitialItem}, {}});
    QVERIFY(historyOnlyInitial.has_value());
    QCOMPARE(historyOnlyInitial->items.first().classification,
             TrackerImportClassification::ExactMatch);
    QVERIFY(fixture.imports->confirm(historyOnlyInitial->batchId));
    bool historyOnlyInitialFinished = false;
    bool historyOnlyInitialAccepted = false;
    fixture.imports->applyConfirmedAsync(historyOnlyInitial->batchId, &reopenedOwner,
        [&historyOnlyInitialFinished, &historyOnlyInitialAccepted](bool accepted,
                                                                    const QString &) {
            historyOnlyInitialFinished = true;
            historyOnlyInitialAccepted = accepted;
        });
    QTRY_VERIFY(historyOnlyInitialFinished);
    QVERIFY(historyOnlyInitialAccepted);

    TrackerImportedHistoryEvidence historyOnlyEvidence;
    historyOnlyEvidence.mapping = *historyOnlyMapping;
    historyOnlyEvidence.eventKind = TrackerImportedEventKind::Completion;
    historyOnlyEvidence.providerEventId = QStringLiteral("trakt-history-only-event");
    historyOnlyEvidence.importSnapshotId = QStringLiteral("trakt-history-only-snapshot");
    historyOnlyEvidence.occurredAtMs = 1720000000789;
    historyOnlyEvidence.timestampSource = TrackerEvidenceTimestampSource::ProviderEvent;
    historyOnlyEvidence.timestampPrecision = TrackerEvidenceTimestampPrecision::ExactMillisecond;
    historyOnlyEvidence.eventPayloadFingerprint = QStringLiteral("completion:trakt-history-only-event");
    QVERIFY(fixture.historyEvidence->record(historyOnlyEvidence));
    fixture.model->setImportOwner(&reopenedOwner);
    QVERIFY(fixture.model->removeImportedData(QStringLiteral("trakt"),
                                               fixture.model->revision()));
    QTRY_COMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
                 QStringLiteral("removed"));
    QVERIFY(reopenedProgress.trackerProgressSourceRemovalSuppressed(
        QStringLiteral("trakt"), historyOnlyAccount));
    QVERIFY(fixture.historyEvidence->contributions().isEmpty());

    const auto historyOnlyCursor = fixture.imports->confirmedCursor(
        TrackerProviderId::Trakt, historyOnlyAccount);
    QVERIFY(historyOnlyCursor.has_value());
    TrackerImportRemoteItem historyOnlyRoutineItem = historyOnlyInitialItem;
    historyOnlyRoutineItem.providerItemId = QStringLiteral("trakt-history-only-routine");
    historyOnlyRoutineItem.exactProgressTarget = TrackerImportProgressTarget{
        historyOnlyCanonical.canonicalMediaId, QStringLiteral("video"),
        historyOnlyTarget.id, 0.9, false};
    historyOnlyRoutineItem.localAtPreview = reopenedOwner.currentProgress(
        *historyOnlyMapping, *historyOnlyRoutineItem.exactProgressTarget);
    QVERIFY(historyOnlyRoutineItem.localAtPreview.has_value());
    const auto historyOnlyRoutine = fixture.imports->createPreview({TrackerProviderId::Trakt,
        historyOnlyAccount, 1, QStringLiteral("trakt-history-only-routine"),
        QStringLiteral("trakt-history-only-cursor-2"), false, true,
        {historyOnlyRoutineItem}, *historyOnlyCursor});
    QVERIFY(historyOnlyRoutine.has_value());
    QCOMPARE(historyOnlyRoutine->items.first().classification,
             TrackerImportClassification::RemoteAdvance);
    QVERIFY(fixture.imports->applyRoutineSafe(historyOnlyRoutine->batchId,
                                               &reopenedOwner, true));
    const auto historyOnlyRoutineSettled = fixture.imports->batch(
        historyOnlyRoutine->batchId);
    QVERIFY(historyOnlyRoutineSettled.has_value());
    QCOMPARE(historyOnlyRoutineSettled->items.first().state,
             TrackerImportItemState::Unresolved);
    QCOMPARE(reopenedProgress.deliveryEntry(QStringLiteral("video"), historyOnlyTarget.id)
                 .value(QStringLiteral("progress")).toDouble(), 0.25);
    QCOMPARE(reopenedProgress.trackerImportedProgressCount(QStringLiteral("trakt"),
                                                             historyOnlyAccount), 0);
}

void TrackerSyncCenterTest::pauseKeepsQueuedWorkAndResumeCallbackRunsAfterPersist()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    QVERIFY(fixture.scrobble->recordIntent(fixture.pendingScrobble(QStringLiteral("operation-pause-1"))));
    int resumeCalls = 0;
    bool resumeSawEnabled = false;
    fixture.model = fixture.makeModel(testCatalogue(), nullptr, nullptr, nullptr, nullptr, nullptr,
        [&] {
            ++resumeCalls;
            resumeSawEnabled = fixture.settings->globalSettings().trackerSyncEnabled;
        });

    QVERIFY(fixture.model->setGlobalSetting(QStringLiteral("trackerSyncEnabled"), false,
                                             fixture.model->revision()));
    QCOMPARE(fixture.scrobble->intents().size(), 1);
    QCOMPARE(fixture.model->aggregateState().value(QStringLiteral("status")).toString(),
             QStringLiteral("Paused"));
    QCOMPARE(fixture.model->aggregateState().value(QStringLiteral("healthy")).toBool(), true);
    QCOMPARE(resumeCalls, 0);

    QVERIFY(fixture.model->setGlobalSetting(QStringLiteral("trackerSyncEnabled"), true,
                                             fixture.model->revision()));
    QCOMPARE(resumeCalls, 1);
    QVERIFY(resumeSawEnabled);
    QCOMPARE(fixture.scrobble->intents().size(), 1);
}

void TrackerSyncCenterTest::automaticPullRequiresCompletedInitialReview()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    QVariantMap dossier = fixture.model->providerDossier(QStringLiteral("simkl"));
    QCOMPARE(dossier.value(QStringLiteral("pullAutomatically")).toBool(), false);
    QCOMPARE(dossier.value(QStringLiteral("pullSettingEnabled")).toBool(), false);
    QVERIFY(!fixture.model->setProviderPullAutomatically(QStringLiteral("simkl"), true,
        fixture.model->revision()));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("import_review_required"));

    const auto batch = fixture.unmatchedPreview();
    QVERIFY(batch);
    QVariantMap review = fixture.model->importReviewSnapshot(batch->batchId,
        fixture.model->revision());
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    const QString reviewItemId = review.value(QStringLiteral("items")).toList()
        .first().toMap().value(QStringLiteral("reviewItemId")).toString();
    QVERIFY(fixture.model->resolveImportItem(batch->batchId, reviewItemId,
        QStringLiteral("leave_unmatched"), fixture.model->revision()));
    QVERIFY(fixture.model->confirmImport(batch->batchId, fixture.model->revision()));

    dossier = fixture.model->providerDossier(QStringLiteral("simkl"));
    QCOMPARE(dossier.value(QStringLiteral("pullSettingEnabled")).toBool(), false);
    UnreachableImportOwner owner;
    QVERIFY(fixture.imports->applyConfirmed(batch->batchId, &owner));
    QCOMPARE(owner.applyCalls, 0);

    dossier = fixture.model->providerDossier(QStringLiteral("simkl"));
    QCOMPARE(dossier.value(QStringLiteral("pullAutomatically")).toBool(), true);
    QCOMPARE(dossier.value(QStringLiteral("pullSettingEnabled")).toBool(), true);
    QVERIFY(fixture.model->setProviderPullAutomatically(QStringLiteral("simkl"), false,
        fixture.model->revision()));
    QCOMPARE(fixture.model->providerDossier(QStringLiteral("simkl"))
                 .value(QStringLiteral("pullAutomatically")).toBool(), false);
    QVERIFY(fixture.model->setProviderPullAutomatically(QStringLiteral("simkl"), true,
        fixture.model->revision()));
    QCOMPARE(fixture.model->providerDossier(QStringLiteral("simkl"))
                 .value(QStringLiteral("pullAutomatically")).toBool(), true);
}

void TrackerSyncCenterTest::providerSettingsAndSyncReceiptsAdvanceRevision()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const QString account = QString::fromLatin1(kRemoteAccountId);

    quint64 previousRevision = fixture.model->revision();
    QVERIFY(fixture.settings->setPullAutomatically(TrackerProviderId::Simkl,
        account, true));
    QVERIFY(fixture.model->revision() > previousRevision);

    previousRevision = fixture.model->revision();
    QVERIFY(fixture.settings->recordSuccessfulSync(TrackerProviderId::Simkl,
        account, 7654321));
    QVERIFY(fixture.model->revision() > previousRevision);

    previousRevision = fixture.model->revision();
    QVERIFY(fixture.scrobble->setEnabled(TrackerProviderId::Simkl, account, true));
    QVERIFY(fixture.model->revision() > previousRevision);

    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, account,
        QStringLiteral("sync-center-remote-1")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("colosseum:series:sync-center-1"), QStringLiteral("series"),
        QStringLiteral("series:sync-center-1"), QStringLiteral("Sync center title")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    const TrackerDeliveryFact fact{canonical.canonicalMediaId, canonical.historyKind,
        canonical.historyId, TrackerDeliveryFactKind::Progress, 1, {}, 8,
        QStringLiteral("local-fact-v1"), TrackerDeliveryOrigin::NativeLocal,
        TrackerMediaDomain::Television};
    CommittedDeliverySource source(fact);
    const TrackerRemoteDeliveryState remoteState{remote.remoteMediaId,
        TrackerDeliveryFactKind::Progress, {}, true, false,
        QStringLiteral("remote-state-v1"), QStringLiteral("Earlier progress")};
    const TrackerRemoteDeliverySnapshot remoteSnapshot{TrackerProviderId::Simkl,
        account, 1, QStringLiteral("review-snapshot-1"), 7654322, true,
        {remoteState}};
    const auto preview = fixture.delivery->createExportPreview(TrackerProviderId::Simkl,
        account, 1, {fact}, remoteSnapshot, &source);
    QVERIFY(preview);
    QVERIFY(fixture.delivery->confirmExport(preview->previewId,
        {preview->items.first().itemId}, remoteSnapshot, &source, 7654323));
    QVERIFY(fixture.delivery->setProviderSendEnabled(TrackerProviderId::Simkl,
        account, true));

    previousRevision = fixture.model->revision();
    QVERIFY(fixture.delivery->setProviderSendEnabled(TrackerProviderId::Simkl,
        account, false));
    QVERIFY(fixture.model->revision() > previousRevision);
}

void TrackerSyncCenterTest::firstExportReviewUsesOpaqueSelectionAndFreshSnapshots()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const QString account = QString::fromLatin1(kRemoteAccountId);
    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, account,
        QStringLiteral("private-remote-export-id")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("private-canonical-export-id"), QStringLiteral("series"),
        QStringLiteral("private-history-export-id"), QStringLiteral("A local title")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    const TrackerDeliveryFact nativeFact{canonical.canonicalMediaId, canonical.historyKind,
        canonical.historyId, TrackerDeliveryFactKind::Progress, 1, {}, 8,
        QStringLiteral("private-local-fingerprint"), TrackerDeliveryOrigin::NativeLocal,
        TrackerMediaDomain::Television};
    const TrackerDeliveryFact importedFact{QStringLiteral("private-imported-id"),
        QStringLiteral("series"), QStringLiteral("private-import-history"),
        TrackerDeliveryFactKind::Progress, 1, {}, 3,
        QStringLiteral("private-import-fingerprint"), TrackerDeliveryOrigin::TrackerImport,
        TrackerMediaDomain::Television};
    ExportReviewSource source;
    source.facts = {nativeFact, importedFact};
    TrackerRemoteDeliverySnapshot remoteSnapshot{TrackerProviderId::Simkl,
        account, 1, QStringLiteral("private-snapshot-id"), 7654322, true,
        {{remote.remoteMediaId, TrackerDeliveryFactKind::Progress, {}, true, false,
          QStringLiteral("private-remote-state"), QStringLiteral("Progress: 12 episodes")}}};
    int remoteReads = 0;
    const auto missing = fixture.model->beginExportReview(QStringLiteral("simkl"),
                                                           fixture.model->revision());
    QVERIFY(!missing.value(QStringLiteral("accepted")).toBool());
    fixture.model->setExportReview(&source,
        [&remoteSnapshot, &remoteReads](const TrackerConnection &,
                                        const QList<TrackerDeliveryFact> &, QString *) {
            ++remoteReads;
            return std::optional<TrackerRemoteDeliverySnapshot>(remoteSnapshot);
        });
    const quint64 firstRevision = fixture.model->revision();
    QVariantMap review = fixture.model->beginExportReview(QStringLiteral("simkl"),
                                                          firstRevision);
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(remoteReads, 1);
    QCOMPARE(review.value(QStringLiteral("eligibleCount")).toInt(), 1);
    const QVariantList rows = review.value(QStringLiteral("items")).toList();
    QCOMPARE(rows.size(), 2);
    const QVariantMap eligible = rows.first().toMap();
    QVERIFY(eligible.value(QStringLiteral("eligible")).toBool());
    QCOMPARE(eligible.value(QStringLiteral("remoteBefore")).toString(),
             QStringLiteral("Progress: 12 episodes"));
    QCOMPARE(eligible.value(QStringLiteral("remoteAfter")).toString(),
             QStringLiteral("Progress: 8"));
    QVERIFY(eligible.value(QStringLiteral("willChangeRemote")).toBool());
    QVERIFY(!rows.last().toMap().value(QStringLiteral("eligible")).toBool());
    const QString projected = QString::fromUtf8(QJsonDocument::fromVariant(review).toJson());
    QVERIFY(!projected.contains(account));
    QVERIFY(!projected.contains(remote.remoteMediaId));
    QVERIFY(!projected.contains(canonical.canonicalMediaId));
    QVERIFY(!projected.contains(remoteSnapshot.snapshotId));
    QVERIFY(!projected.contains(nativeFact.contentFingerprint));
    QVERIFY(!fixture.model->confirmExportReview(
        review.value(QStringLiteral("reviewId")).toString(),
        {rows.last().toMap().value(QStringLiteral("itemId")).toString()}, firstRevision));
    QVERIFY(!fixture.delivery->hasFirstExportConsent(TrackerProviderId::Simkl, account));
    QVERIFY(fixture.delivery->operations().isEmpty());

    remoteSnapshot.items[0].safeSummary = QStringLiteral("https://private.example/account");
    review = fixture.model->beginExportReview(QStringLiteral("simkl"),
                                              fixture.model->revision());
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(review.value(QStringLiteral("items")).toList().first().toMap()
                 .value(QStringLiteral("remoteBefore")).toString(),
             QStringLiteral("Existing tracker state"));
    remoteSnapshot.items[0].safeSummary = QStringLiteral("Progress: 12 episodes");
    const quint64 staleRevision = fixture.model->revision();
    QVERIFY(fixture.settings->setGlobalSetting(QStringLiteral("completionMessages"), true));
    QVERIFY(!fixture.model->confirmExportReview(
        review.value(QStringLiteral("reviewId")).toString(),
        {review.value(QStringLiteral("items")).toList().first().toMap()
            .value(QStringLiteral("itemId")).toString()}, staleRevision));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("stale_intent"));
    QVERIFY(!fixture.delivery->hasFirstExportConsent(TrackerProviderId::Simkl, account));

    review = fixture.model->beginExportReview(QStringLiteral("simkl"),
                                              fixture.model->revision());
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    source.facts[0].progress = 9;
    QVERIFY(!fixture.model->confirmExportReview(
        review.value(QStringLiteral("reviewId")).toString(),
        {review.value(QStringLiteral("items")).toList().first().toMap()
            .value(QStringLiteral("itemId")).toString()}, fixture.model->revision()));
    source.facts[0].progress = 8;
    QVERIFY(!fixture.delivery->hasFirstExportConsent(TrackerProviderId::Simkl, account));

    review = fixture.model->beginExportReview(QStringLiteral("simkl"),
                                              fixture.model->revision());
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    remoteSnapshot.snapshotId = QStringLiteral("changed-provider-snapshot");
    QVERIFY(!fixture.model->confirmExportReview(
        review.value(QStringLiteral("reviewId")).toString(),
        {review.value(QStringLiteral("items")).toList().first().toMap()
            .value(QStringLiteral("itemId")).toString()}, fixture.model->revision()));
    QVERIFY(!fixture.delivery->hasFirstExportConsent(TrackerProviderId::Simkl, account));

    review = fixture.model->beginExportReview(QStringLiteral("simkl"),
                                              fixture.model->revision());
    QVERIFY(review.value(QStringLiteral("accepted")).toBool());
    QVERIFY(fixture.model->confirmExportReview(
        review.value(QStringLiteral("reviewId")).toString(),
        {review.value(QStringLiteral("items")).toList().first().toMap()
            .value(QStringLiteral("itemId")).toString()}, fixture.model->revision()));
    QCOMPARE(remoteReads, 9);
    QVERIFY(fixture.delivery->hasFirstExportConsent(TrackerProviderId::Simkl, account));
    QCOMPARE(fixture.delivery->operations().size(), 1);
    QCOMPARE(fixture.delivery->operations().first().fact.canonicalMediaId,
             nativeFact.canonicalMediaId);
    QVERIFY(fixture.model->importReviews().isEmpty());
}

void TrackerSyncCenterTest::connectionServiceCountsOnlyCurrentSupportedReceipts()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const QString account = QString::fromLatin1(kRemoteAccountId);
    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, account,
        QStringLiteral("coverage-remote-1")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("colosseum:series:coverage-1"), QStringLiteral("series"),
        QStringLiteral("series:coverage-1"), QStringLiteral("Coverage title")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::UserConfirmed));

    const TrackerDeliveryFact fact{canonical.canonicalMediaId, canonical.historyKind,
        canonical.historyId, TrackerDeliveryFactKind::Progress, 1, {}, 8,
        QStringLiteral("coverage-fact-v1"), TrackerDeliveryOrigin::NativeLocal,
        TrackerMediaDomain::Television};
    CommittedDeliverySource source(fact);
    const TrackerRemoteDeliveryState remoteState{remote.remoteMediaId,
        TrackerDeliveryFactKind::Progress, {}, true, false,
        QStringLiteral("coverage-remote-state-v1"), QStringLiteral("Earlier progress")};
    const TrackerRemoteDeliverySnapshot remoteSnapshot{TrackerProviderId::Simkl,
        account, 1, QStringLiteral("coverage-snapshot-1"), 8765001, true,
        {remoteState}};
    const auto preview = fixture.delivery->createExportPreview(TrackerProviderId::Simkl,
        account, 1, {fact}, remoteSnapshot, &source);
    QVERIFY(preview);
    QCOMPARE(preview->items.size(), 1);
    QVERIFY(fixture.delivery->confirmExport(preview->previewId,
        {preview->items.first().itemId}, remoteSnapshot, &source, 8765002));

    const QString operationId = fixture.delivery->operations().first().operationId;
    QVERIFY(fixture.delivery->markDelivering(operationId, &source, 8765003));
    QVERIFY(fixture.delivery->recordAttemptResult(operationId,
        TrackerDeliveryAttemptResult::Succeeded, 8765004));

    TrackerConnectionService enabledService(fixture.connections.get(), fixture.mappings.get(),
        fixture.delivery.get(), &source, testCatalogue());
    QString error;
    QVERIFY2(enabledService.healthy(&error), qPrintable(error));
    QVERIFY(error.isEmpty());
    const auto connection = enabledService.connection(TrackerProviderId::Simkl);
    QVERIFY(connection);
    QCOMPARE(connection->remoteAccountId, account);
    QVERIFY(connection->capabilities.testFlag(TrackerProviderCapability::WriteProgress));
    QCOMPARE(enabledService.connections().size(), 1);

    TrackerCoverageSnapshot coverage = enabledService.coverageForTitle(canonical.canonicalMediaId);
    QVERIFY(coverage.available);
    QCOMPARE(coverage.providerCount, 1);
    QVERIFY(!coverage.localOnly);
    QCOMPARE(coverage.receiptStatuses.size(), 1);
    QCOMPARE(coverage.receiptStatuses.first().providerId, TrackerProviderId::Simkl);
    QCOMPARE(coverage.receiptStatuses.first().currentness, TrackerReceiptCurrentness::Current);
    QVERIFY(coverage.receiptStatuses.first().contributesToCoverage);

    TrackerConnectionService productionService(fixture.connections.get(), fixture.mappings.get(),
        fixture.delivery.get(), &source, trackerBuiltInProviderCatalog());
    coverage = productionService.coverageForTitle(canonical.canonicalMediaId);
    QVERIFY(coverage.available);
    QCOMPARE(coverage.providerCount, 0);
    QVERIFY(coverage.localOnly);
    QCOMPARE(coverage.receiptStatuses.size(), 1);
    QCOMPARE(coverage.receiptStatuses.first().currentness, TrackerReceiptCurrentness::Unsupported);
    QVERIFY(!coverage.receiptStatuses.first().contributesToCoverage);

    TrackerDeliveryFact staleFact = fact;
    staleFact.sourceRevision = 2;
    staleFact.progress = 9;
    staleFact.contentFingerprint = QStringLiteral("coverage-fact-v2");
    source.setFact(staleFact);
    coverage = enabledService.coverageForTitle(canonical.canonicalMediaId);
    QCOMPARE(coverage.providerCount, 0);
    QVERIFY(coverage.localOnly);
    QCOMPARE(coverage.receiptStatuses.size(), 1);
    QCOMPARE(coverage.receiptStatuses.first().currentness, TrackerReceiptCurrentness::StaleSource);

    source.setFact(fact);
    TrackerCanonicalTitleCandidate changedCanonical = canonical;
    changedCanonical.displayName = QStringLiteral("Renamed coverage title");
    QVERIFY(fixture.mappings->upsert(remote, changedCanonical,
        TrackerMappingProvenance::UserConfirmed));
    coverage = enabledService.coverageForTitle(canonical.canonicalMediaId);
    QCOMPARE(coverage.providerCount, 0);
    QVERIFY(coverage.localOnly);
    QCOMPARE(coverage.receiptStatuses.size(), 1);
    QCOMPARE(coverage.receiptStatuses.first().currentness, TrackerReceiptCurrentness::StaleMapping);

    QVERIFY(fixture.connections->upsert({TrackerProviderId::Simkl, account, 2, 1235,
        simklCapabilities(), TrackerConnectionState::Connected}));
    coverage = enabledService.coverageForTitle(canonical.canonicalMediaId);
    QCOMPARE(coverage.providerCount, 0);
    QVERIFY(coverage.localOnly);
    QCOMPARE(coverage.receiptStatuses.size(), 1);
    QCOMPARE(coverage.receiptStatuses.first().currentness,
             TrackerReceiptCurrentness::StaleConnection);

    QVERIFY(TrackerDeliveryStore::supportsProviderDelivery(
        TrackerProviderId::Simkl, TrackerMediaDomain::Television));
    QVERIFY(!TrackerDeliveryStore::supportsProviderDelivery(
        TrackerProviderId::Simkl, TrackerMediaDomain::Manga));
    QVERIFY(!TrackerDeliveryStore::supportsProviderDelivery(
        TrackerProviderId::Trakt, TrackerMediaDomain::Television));
    QVERIFY(!trackerProviderIdFromKey(QStringLiteral("stremio")));
}

void TrackerSyncCenterTest::connectionServiceDoesNotCountConnectedOnlyProvider()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const TrackerDeliveryFact fact{
        QStringLiteral("colosseum:series:connected-only"), QStringLiteral("series"),
        QStringLiteral("series:connected-only"), TrackerDeliveryFactKind::Progress, 1,
        {}, 8, QStringLiteral("connected-only-fact-v1"),
        TrackerDeliveryOrigin::NativeLocal, TrackerMediaDomain::Television};
    CommittedDeliverySource source(fact);
    TrackerConnectionService service(fixture.connections.get(), fixture.mappings.get(),
        fixture.delivery.get(), &source, testCatalogue());

    // A connected tracker with native progress but no successful tracker-plane
    // receipt remains Local only. Connection alone is never coverage.
    QCOMPARE(service.connections().size(), 1);
    const TrackerCoverageSnapshot coverage = service.coverageForTitle(fact.canonicalMediaId);
    QVERIFY(coverage.available);
    QCOMPARE(coverage.providerCount, 0);
    QVERIFY(coverage.localOnly);
    QVERIFY(coverage.receiptStatuses.isEmpty());
    QVERIFY(!trackerProviderIdFromKey(QStringLiteral("stremio")));
}

void TrackerSyncCenterTest::connectionServiceNormalizedTransportContract()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    CommittedDeliverySource source({QStringLiteral("colosseum:series:transport-contract"),
        QStringLiteral("series"), QStringLiteral("series:transport-contract"),
        TrackerDeliveryFactKind::Progress, 1, {}, 4, QStringLiteral("transport-fact-v1"),
        TrackerDeliveryOrigin::NativeLocal, TrackerMediaDomain::Television});

    TrackerNormalizedTransportRequest request;
    request.providerId = TrackerProviderId::Simkl;
    request.connectionGeneration = 1;
    request.action = TrackerNormalizedTransportAction::WriteProgress;
    request.canonicalMediaId = QStringLiteral("colosseum:series:transport-contract");
    request.historyKind = QStringLiteral("series");
    request.historyId = QStringLiteral("series:transport-contract");
    request.remoteMediaId = QStringLiteral("mapped-item-key");
    request.mediaDomain = TrackerMediaDomain::Television;
    request.progress = 4;

    TrackerConnectionService unavailable(fixture.connections.get(), fixture.mappings.get(),
        fixture.delivery.get(), &source, testCatalogue());
    QCOMPARE(unavailable.normalizedTransport().execute(request).outcome,
             TrackerNormalizedTransportOutcome::Unavailable);

    RecordingNormalizedTransport transport;
    TrackerConnectionService injected(fixture.connections.get(), fixture.mappings.get(),
        fixture.delivery.get(), &source, testCatalogue(), &transport);
    QCOMPARE(injected.normalizedTransport().execute(request).outcome,
             TrackerNormalizedTransportOutcome::Completed);
    QCOMPARE(transport.calls, 1);
    QCOMPARE(transport.lastRequest.providerId, TrackerProviderId::Simkl);
    QCOMPARE(transport.lastRequest.action, TrackerNormalizedTransportAction::WriteProgress);
    QCOMPARE(transport.lastRequest.canonicalMediaId, request.canonicalMediaId);
    QCOMPARE(transport.lastRequest.remoteMediaId, request.remoteMediaId);
    QCOMPARE(transport.lastRequest.progress, 4);
}

void TrackerSyncCenterTest::diagnoseRouteIsCurrentAndSanitized()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const QString account = QString::fromLatin1(kRemoteAccountId);
    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, account,
        QString::fromLatin1(kRemoteItemId)};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("colosseum:series:diagnose-private-title"), QStringLiteral("series"),
        QStringLiteral("series:diagnose-private-id"), QStringLiteral("Diagnose title")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    const TrackerDeliveryFact fact{canonical.canonicalMediaId, canonical.historyKind,
        canonical.historyId, TrackerDeliveryFactKind::Progress, 1, {}, 8,
        QStringLiteral("diagnose-fact-v1"), TrackerDeliveryOrigin::NativeLocal,
        TrackerMediaDomain::Television};
    CommittedDeliverySource source(fact);
    const TrackerRemoteDeliveryState remoteState{remote.remoteMediaId,
        TrackerDeliveryFactKind::Progress, {}, true, false,
        QStringLiteral("diagnose-remote-state-v1"), QStringLiteral("Earlier progress")};
    const TrackerRemoteDeliverySnapshot remoteSnapshot{TrackerProviderId::Simkl,
        account, 1, QStringLiteral("diagnose-snapshot-1"), 9765001, true,
        {remoteState}};
    const auto preview = fixture.delivery->createExportPreview(TrackerProviderId::Simkl,
        account, 1, {fact}, remoteSnapshot, &source);
    QVERIFY(preview);
    QCOMPARE(preview->items.size(), 1);
    QVERIFY(fixture.delivery->confirmExport(preview->previewId,
        {preview->items.first().itemId}, remoteSnapshot, &source, 9765002));

    const QString operationId = fixture.delivery->operations().first().operationId;
    QVERIFY(fixture.delivery->markDelivering(operationId, &source, 9765003));
    QVERIFY(fixture.delivery->recordAttemptResult(operationId,
        TrackerDeliveryAttemptResult::UnknownOutcome, 9765004, 0,
        TrackerDeliveryReason::AcknowledgementLost));

    const QVariantMap route = fixture.model->diagnoseRoute(QStringLiteral("simkl"));
    QCOMPARE(keys(route), keySet({"accepted", "code", "providerKey", "destination",
        "focusTarget", "state", "reason", "focusIndex", "revision"}));
    QVERIFY(route.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(route.value(QStringLiteral("revision")).toULongLong(),
             fixture.model->revision());
    QCOMPARE(route.value(QStringLiteral("providerKey")).toString(), QStringLiteral("simkl"));
    QCOMPARE(route.value(QStringLiteral("destination")).toString(),
             QStringLiteral("provider_dossier"));
    QCOMPARE(route.value(QStringLiteral("focusTarget")).toString(),
             QStringLiteral("delivery_status"));
    QCOMPARE(route.value(QStringLiteral("state")).toString(),
             QStringLiteral("checking_delivery"));
    QCOMPARE(route.value(QStringLiteral("reason")).toString(),
             QStringLiteral("acknowledgement_lost"));
    QCOMPARE(route.value(QStringLiteral("focusIndex")).toInt(), 0);
    const QByteArray routeJson = QJsonDocument::fromVariant(route).toJson(QJsonDocument::Compact);
    QVERIFY(!routeJson.contains(account.toUtf8()));
    QVERIFY(!routeJson.contains(remote.remoteMediaId.toUtf8()));
    QVERIFY(!routeJson.contains(canonical.canonicalMediaId.toUtf8()));
    QVERIFY(!routeJson.contains(canonical.historyId.toUtf8()));
    QVERIFY(!routeJson.contains(operationId.toUtf8()));

    const QVariantMap stremioRoute = fixture.model->diagnoseRoute(QStringLiteral("stremio"));
    QVERIFY(!stremioRoute.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(stremioRoute.value(QStringLiteral("code")).toString(),
             QStringLiteral("provider_unavailable"));

    QVERIFY(fixture.connections->upsert({TrackerProviderId::Simkl, account, 2, 1235,
        simklCapabilities(), TrackerConnectionState::Connected}));
    const QVariantMap staleRoute = fixture.model->diagnoseRoute(QStringLiteral("simkl"));
    QVERIFY(!staleRoute.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(staleRoute.value(QStringLiteral("code")).toString(),
             QStringLiteral("no_current_issue"));
}

void TrackerSyncCenterTest::enablingSendsRefreshesCurrentFactsAfterPersistingPreference()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const QString account = QString::fromLatin1(kRemoteAccountId);
    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, account,
        QStringLiteral("send-refresh-remote")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("colosseum:series:send-refresh"), QStringLiteral("series"),
        QStringLiteral("series:send-refresh"), QStringLiteral("Send refresh")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::UserConfirmed));
    const TrackerDeliveryFact fact{canonical.canonicalMediaId, canonical.historyKind,
        canonical.historyId, TrackerDeliveryFactKind::Progress, 1, {}, 8,
        QStringLiteral("send-refresh-fact-v1"), TrackerDeliveryOrigin::NativeLocal,
        TrackerMediaDomain::Television};
    CommittedDeliverySource source(fact);
    const TrackerRemoteDeliveryState remoteState{remote.remoteMediaId,
        TrackerDeliveryFactKind::Progress, {}, false, false,
        QStringLiteral("absent"), QStringLiteral("No progress")};
    const TrackerRemoteDeliverySnapshot remoteSnapshot{TrackerProviderId::Simkl,
        account, 1, QStringLiteral("send-refresh-snapshot"), 9876543, true, {remoteState}};
    const auto preview = fixture.delivery->createExportPreview(TrackerProviderId::Simkl,
        account, 1, {fact}, remoteSnapshot, &source);
    QVERIFY(preview);
    QVERIFY(fixture.delivery->confirmExport(preview->previewId,
        {preview->items.first().itemId}, remoteSnapshot, &source, 9876544));
    QVERIFY(fixture.delivery->providerSendEnabled(TrackerProviderId::Simkl, account));
    QVERIFY(fixture.delivery->setProviderSendEnabled(TrackerProviderId::Simkl,
        account, false));

    QVERIFY(fixture.model->setProviderSendEnabled(QStringLiteral("simkl"), true,
        fixture.model->revision()));
    QCOMPARE(fixture.deliveryRefreshCalls, 1);
    QVERIFY(fixture.delivery->providerSendEnabled(TrackerProviderId::Simkl, account));

    QVERIFY(fixture.model->setProviderSendEnabled(QStringLiteral("simkl"), false,
        fixture.model->revision()));
    QCOMPARE(fixture.deliveryRefreshCalls, 1);
}

void TrackerSyncCenterTest::unresolvedAndUnknownOutcomesNeverProjectHealthy()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const auto batch = fixture.unmatchedPreview();
    QVERIFY(batch);
    QVariantMap aggregate = fixture.model->aggregateState();
    QCOMPARE(aggregate.value(QStringLiteral("status")).toString(), QStringLiteral("Attention"));
    QCOMPARE(aggregate.value(QStringLiteral("healthy")).toBool(), false);
    QCOMPARE(aggregate.value(QStringLiteral("unresolvedCount")).toInt(), 1);

    const TrackerScrobbleIntent intent = fixture.pendingScrobble(QStringLiteral("operation-unknown-1"));
    QVERIFY(fixture.scrobble->recordIntent(intent));
    QVERIFY(fixture.scrobble->markDelivering(intent.operationId));
    QVERIFY(fixture.scrobble->recordAttemptResult(intent.operationId,
        TrackerScrobbleAttemptResult::UnknownOutcome,
        TrackerScrobbleReason::AcknowledgementLost));
    aggregate = fixture.model->aggregateState();
    QCOMPARE(aggregate.value(QStringLiteral("healthy")).toBool(), false);
    QCOMPARE(aggregate.value(QStringLiteral("status")).toString(), QStringLiteral("Attention"));
    QVERIFY(!fixture.model->requestSyncAll(fixture.model->revision()));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("sync_handler_unavailable"));
}

void TrackerSyncCenterTest::waitingStateIsHealthyAndRetainsQueuedWork()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    QVERIFY(fixture.scrobble->recordIntent(
        fixture.pendingScrobble(QStringLiteral("operation-waiting-1"))));

    const QVariantMap aggregate = fixture.model->aggregateState();
    QCOMPARE(aggregate.value(QStringLiteral("status")).toString(), QStringLiteral("Waiting"));
    QCOMPARE(aggregate.value(QStringLiteral("healthy")).toBool(), true);
    QCOMPARE(aggregate.value(QStringLiteral("waitingCount")).toInt(), 1);
    QCOMPARE(fixture.scrobble->intents().size(), 1);
}

void TrackerSyncCenterTest::retryableProviderOutageNeverProjectsHealthy()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const TrackerScrobbleIntent intent = fixture.pendingScrobble(
        QStringLiteral("simkl-retryable-outage"));
    QVERIFY(fixture.scrobble->recordIntent(intent));
    QVERIFY(fixture.scrobble->markDelivering(intent.operationId));
    QVERIFY(fixture.scrobble->recordAttemptResult(intent.operationId,
        TrackerScrobbleAttemptResult::KnownNotApplied,
        TrackerScrobbleReason::RetryableKnownNotApplied));

    const QVariantMap aggregate = fixture.model->aggregateState();
    QCOMPARE(aggregate.value(QStringLiteral("status")).toString(), QStringLiteral("Attention"));
    QVERIFY(!aggregate.value(QStringLiteral("healthy")).toBool());
    QVERIFY(aggregate.value(QStringLiteral("waitingCount")).toInt() > 0);
}

void TrackerSyncCenterTest::retryingDeliveryOutageNeverProjectsHealthy()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const QString account = QString::fromLatin1(kRemoteAccountId);
    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, account,
        QStringLiteral("retrying-delivery-remote")};
    const TrackerCanonicalTitleCandidate canonical{
        QStringLiteral("colosseum:series:retrying-delivery"), QStringLiteral("series"),
        QStringLiteral("series:retrying-delivery"), QStringLiteral("Retrying delivery title")};
    QVERIFY(fixture.mappings->upsert(remote, canonical,
        TrackerMappingProvenance::UserConfirmed));

    const TrackerDeliveryFact fact{canonical.canonicalMediaId, canonical.historyKind,
        canonical.historyId, TrackerDeliveryFactKind::Progress, 1, {}, 8,
        QStringLiteral("delivery-fact-v1"), TrackerDeliveryOrigin::NativeLocal,
        TrackerMediaDomain::Television};
    CommittedDeliverySource source(fact);
    const TrackerRemoteDeliveryState remoteState{remote.remoteMediaId,
        TrackerDeliveryFactKind::Progress, {}, true, false,
        QStringLiteral("remote-state-v1"), QStringLiteral("Earlier progress")};
    const TrackerRemoteDeliverySnapshot remoteSnapshot{TrackerProviderId::Simkl,
        account, 1, QStringLiteral("delivery-outage-review"), 7654322, true,
        {remoteState}};
    const auto preview = fixture.delivery->createExportPreview(TrackerProviderId::Simkl,
        account, 1, {fact}, remoteSnapshot, &source);
    QVERIFY(preview);
    QVERIFY(fixture.delivery->confirmExport(preview->previewId,
        {preview->items.first().itemId}, remoteSnapshot, &source, 7654323));
    const QString operationId = fixture.delivery->operations().first().operationId;
    QVERIFY(fixture.delivery->markDelivering(operationId, &source, 7654324));
    QVERIFY(fixture.delivery->recordAttemptResult(operationId,
        TrackerDeliveryAttemptResult::RetryableKnownNotApplied, 7654325, 0,
        TrackerDeliveryReason::ProviderRetryable));

    const QVariantMap aggregate = fixture.model->aggregateState();
    QCOMPARE(aggregate.value(QStringLiteral("status")).toString(), QStringLiteral("Attention"));
    QVERIFY(!aggregate.value(QStringLiteral("healthy")).toBool());
    QVERIFY(aggregate.value(QStringLiteral("waitingCount")).toInt() > 0);
}

void TrackerSyncCenterTest::syncAllKeepsMixedProviderOutcomesVisible()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const TrackerProviderCapabilities malCapabilities = TrackerProviderCapability::ReadHistory
        | TrackerProviderCapability::ReadProgress | TrackerProviderCapability::WriteProgress;
    QVERIFY(fixture.connections->upsert({TrackerProviderId::Mal,
        QStringLiteral("mal-account-private-7"), 1, 1234, malCapabilities,
        TrackerConnectionState::Connected}));

    const auto batch = fixture.unmatchedPreview();
    QVERIFY(batch);
    const TrackerScrobbleIntent unknown = fixture.pendingScrobble(
        QStringLiteral("simkl-unknown-outcome"));
    QVERIFY(fixture.scrobble->recordIntent(unknown));
    QVERIFY(fixture.scrobble->markDelivering(unknown.operationId));
    QVERIFY(fixture.scrobble->recordAttemptResult(unknown.operationId,
        TrackerScrobbleAttemptResult::UnknownOutcome,
        TrackerScrobbleReason::AcknowledgementLost));

    QList<TrackerProviderDescriptor> descriptors = testCatalogue();
    descriptors[1].capabilities = malCapabilities;
    descriptors[1].available = true;
    fixture.model = fixture.makeModel(descriptors);

    QStringList requestedProviders;
    quint64 requestedRevision = 0;
    QObject::connect(fixture.model.get(), &TrackerSyncCenterModel::syncAllRequested,
        fixture.model.get(), [&requestedProviders, &requestedRevision](
            const QStringList &providerKeys, quint64 revision) {
            requestedProviders = providerKeys;
            requestedRevision = revision;
        });

    QVariantMap aggregate = fixture.model->aggregateState();
    QCOMPARE(aggregate.value(QStringLiteral("status")).toString(), QStringLiteral("Attention"));
    QCOMPARE(aggregate.value(QStringLiteral("healthy")).toBool(), false);
    QCOMPARE(aggregate.value(QStringLiteral("waitingCount")).toInt(), 0);
    QCOMPARE(aggregate.value(QStringLiteral("unresolvedCount")).toInt(), 1);
    QCOMPARE(aggregate.value(QStringLiteral("attentionProviderCount")).toInt(), 1);
    QVERIFY(aggregate.value(QStringLiteral("canSyncAll")).toBool());
    QCOMPARE(fixture.model->connectedTrackers().at(1).toMap()
                 .value(QStringLiteral("status")).toString(), QStringLiteral("Connected"));

    QVERIFY(fixture.model->requestSyncAll(fixture.model->revision()));
    QCOMPARE(requestedProviders, QStringList({QStringLiteral("simkl"), QStringLiteral("mal")}));
    QCOMPARE(requestedRevision, fixture.model->revision());
    aggregate = fixture.model->aggregateState();
    QCOMPARE(aggregate.value(QStringLiteral("status")).toString(), QStringLiteral("Attention"));
    QCOMPARE(aggregate.value(QStringLiteral("healthy")).toBool(), false);
    QCOMPARE(aggregate.value(QStringLiteral("unresolvedCount")).toInt(), 1);
    QCOMPARE(fixture.imports->batch(batch->batchId)->items.first().state,
             TrackerImportItemState::ReviewRequired);
}

void TrackerSyncCenterTest::syncAllIsNotAdvertisedWithoutAHandler()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());

    const QVariantMap aggregate = fixture.model->aggregateState();
    QVERIFY(!aggregate.value(QStringLiteral("canSyncAll")).toBool());
    QVERIFY(!fixture.model->providerDossier(QStringLiteral("simkl"))
                 .value(QStringLiteral("syncEnabled")).toBool());
    QVERIFY(!fixture.model->requestSyncAll(fixture.model->revision()));
    QCOMPARE(fixture.model->lastActionResult().value(QStringLiteral("code")).toString(),
             QStringLiteral("sync_handler_unavailable"));
}

void TrackerSyncCenterTest::restartProjectionMatchesDurableOwnerTruth()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    const auto batch = fixture.unmatchedPreview();
    QVERIFY(batch);
    QVERIFY(fixture.settings->setGlobalSetting(QStringLiteral("trackerSyncEnabled"), false));
    QVERIFY(fixture.settings->recordSuccessfulSync(TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId), 7654321));

    const TrackerScrobbleIntent unknown = fixture.pendingScrobble(
        QStringLiteral("restart-unknown-outcome"));
    QVERIFY(fixture.scrobble->recordIntent(unknown));
    QVERIFY(fixture.scrobble->markDelivering(unknown.operationId));
    QVERIFY(fixture.scrobble->recordAttemptResult(unknown.operationId,
        TrackerScrobbleAttemptResult::UnknownOutcome,
        TrackerScrobbleReason::AcknowledgementLost));

    const QVariantMap before = fixture.model->aggregateState();
    QCOMPARE(before.value(QStringLiteral("status")).toString(), QStringLiteral("Attention"));
    QCOMPARE(before.value(QStringLiteral("healthy")).toBool(), false);
    QCOMPARE(before.value(QStringLiteral("unresolvedCount")).toInt(), 1);
    QCOMPARE(fixture.model->connectedTrackers().size(), 1);
    QCOMPARE(fixture.model->importReviews().size(), 1);
    QCOMPARE(fixture.model->deliveryRows(QStringLiteral("simkl")).size(), 1);

    fixture.model.reset();
    fixture.imports.reset();
    fixture.delivery.reset();
    fixture.scrobble.reset();
    fixture.settings.reset();
    fixture.mappings.reset();
    fixture.connections.reset();
    fixture.connections = std::make_unique<TrackerConnectionStore>(fixture.profile);
    fixture.mappings = std::make_unique<TrackerMappingStore>(fixture.profile);
    fixture.delivery = std::make_unique<TrackerDeliveryStore>(
        fixture.profile, fixture.mappings.get(), fixture.connections.get());
    fixture.scrobble = std::make_unique<TrackerScrobbleStore>(fixture.profile);
    fixture.settings = std::make_unique<TrackerSyncSettingsStore>(fixture.profile);
    fixture.imports = std::make_unique<TrackerImportStore>(
        fixture.profile, fixture.mappings.get(), fixture.connections.get());
    fixture.model = fixture.makeModel();
    QVERIFY(fixture.valid());

    const QVariantMap after = fixture.model->aggregateState();
    QCOMPARE(after.value(QStringLiteral("status")).toString(),
             before.value(QStringLiteral("status")).toString());
    QCOMPARE(after.value(QStringLiteral("healthy")).toBool(),
             before.value(QStringLiteral("healthy")).toBool());
    QCOMPARE(after.value(QStringLiteral("unresolvedCount")).toInt(),
             before.value(QStringLiteral("unresolvedCount")).toInt());
    QCOMPARE(fixture.model->connectedTrackers().size(), 1);
    QCOMPARE(fixture.model->importReviews().size(), 1);
    QCOMPARE(fixture.model->deliveryRows(QStringLiteral("simkl")).size(), 1);
    QCOMPARE(fixture.model->globalSettings().value(QStringLiteral("trackerSyncEnabled")).toBool(),
             false);
    QCOMPARE(fixture.settings->lastSuccessfulSyncAtMs(TrackerProviderId::Simkl,
        QString::fromLatin1(kRemoteAccountId)), 7654321);
    QVERIFY(fixture.imports->batch(batch->batchId).has_value());
}

void TrackerSyncCenterTest::unavailableOwnerAndUnsupportedPreferencesFailClosed()
{
    TrackerSyncCenterFixture fixture;
    QVERIFY(fixture.valid());
    QVERIFY(fixture.connectSimkl());
    auto ownerUnavailable = std::make_unique<TrackerSyncCenterModel>(
        fixture.connections.get(), fixture.imports.get(), nullptr, fixture.scrobble.get(),
        fixture.settings.get(), testCatalogue());
    QCOMPARE(ownerUnavailable->aggregateState().value(QStringLiteral("ownerUnavailable")).toBool(),
             true);
    QVERIFY(!ownerUnavailable->setGlobalSetting(QStringLiteral("backgroundDelivery"), false,
                                                 ownerUnavailable->revision()));

    const QList<TrackerProviderDescriptor> noCapabilities{
        {TrackerProviderId::Simkl, QStringLiteral("SIMKL"), {}, true},
        {TrackerProviderId::Mal, QStringLiteral("MyAnimeList"), {}, false},
        {TrackerProviderId::Trakt, QStringLiteral("Trakt"), {}, false},
        {TrackerProviderId::AniList, QStringLiteral("AniList"), {}, false}};
    auto unsupported = fixture.makeModel(noCapabilities);
    const QVariantMap dossier = unsupported->providerDossier(QStringLiteral("simkl"));
    QVERIFY(!dossier.value(QStringLiteral("pullSettingEnabled")).toBool());
    QVERIFY(!dossier.value(QStringLiteral("sendSettingEnabled")).toBool());
    QVERIFY(!unsupported->setProviderPullAutomatically(QStringLiteral("simkl"), true,
                                                        unsupported->revision()));
    QVERIFY(!unsupported->setProviderSendEnabled(QStringLiteral("simkl"), true,
                                                  unsupported->revision()));
    QVERIFY(!unsupported->setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true,
                                                         unsupported->revision()));
}

QTEST_GUILESS_MAIN(TrackerSyncCenterTest)
#include "tst_tracker_sync_center.moc"
