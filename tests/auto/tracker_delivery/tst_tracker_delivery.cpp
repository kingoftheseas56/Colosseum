#include "trackers/TrackerDeliveryStore.h"
#include "trackers/TrackerCanonicalDeliverySource.h"
#include "trackers/TrackerDeliveryRuntime.h"
#include "trackers/TrackerScrobbleStore.h"
#include "trackers/TrackerScrobbleRuntime.h"
#include "trackers/TrackerSyncSettingsStore.h"
#include "account/ActivityPlaybackTracker.h"
#include "account/ActivityStore.h"
#include "account/ConsumptionHistoryBridge.h"
#include "account/HistoryStore.h"
#include "ProgressStore.h"

#include <QTemporaryDir>
#include <QFile>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <algorithm>

namespace {

constexpr auto kProfileId = "22222222-2222-4222-8222-222222222222";

QString sourceKey(const TrackerDeliveryFact &fact)
{
    return fact.canonicalMediaId + QChar(0x1f) + QString::number(static_cast<int>(fact.kind))
        + QChar(0x1f) + fact.sourceEventId;
}

class FakeDeliverySource final : public TrackerDeliverySource
{
public:
    QList<TrackerDeliveryFact> currentCommittedFacts() const override { return m_facts.values(); }

    bool isDurablyCurrent(const TrackerDeliveryFact &fact) const override
    {
        const auto current = m_facts.constFind(sourceKey(fact));
        return current != m_facts.cend() && current->sourceRevision == fact.sourceRevision
            && current->progress == fact.progress
            && current->contentFingerprint == fact.contentFingerprint
            && current->origin == fact.origin && current->mediaDomain == fact.mediaDomain;
    }

    void commit(const TrackerDeliveryFact &fact)
    {
        m_facts.insert(sourceKey(fact), fact);
    }

private:
    QHash<QString, TrackerDeliveryFact> m_facts;
};

TrackerDeliveryFact progressFact(const QString &canonicalId,
                                quint64 revision,
                                int progress,
                                const QString &fingerprint)
{
    return {canonicalId, QStringLiteral("series"), QStringLiteral("theatre:") + canonicalId,
            TrackerDeliveryFactKind::Progress, revision, {}, progress, fingerprint,
            TrackerDeliveryOrigin::NativeLocal, TrackerMediaDomain::Television};
}

TrackerDeliveryFact completionFact(const QString &canonicalId,
                                   quint64 revision,
                                   const QString &eventId,
                                   const QString &fingerprint)
{
    return {canonicalId, QStringLiteral("series"), QStringLiteral("theatre:") + canonicalId,
            TrackerDeliveryFactKind::Completion, revision, eventId, 12, fingerprint,
            TrackerDeliveryOrigin::NativeLocal, TrackerMediaDomain::Television};
}

QVariantMap progressEntry(const QString &kind, const QString &id, double progress)
{
    return {{QStringLiteral("kind"), kind}, {QStringLiteral("id"), id},
            {QStringLiteral("progress"), progress}, {QStringLiteral("resume"), 12.0}};
}

QVariantMap activityCompletion(const QString &eventId, const QString &kind,
                               const QString &itemId, qint64 atMs)
{
    return {{QStringLiteral("eventId"), eventId},
            {QStringLiteral("v"), 1},
            {QStringLiteral("type"), QStringLiteral("media_completed")},
            {QStringLiteral("sessionId"), QStringLiteral("session-%1").arg(eventId)},
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("titleKey"), QStringLiteral("title:%1").arg(itemId)},
            {QStringLiteral("itemKey"), itemId},
            {QStringLiteral("title"), QStringLiteral("Test title")},
            {QStringLiteral("itemLabel"), QString()},
            {QStringLiteral("cover"), QString()},
            {QStringLiteral("utcOffsetMinutes"), 0},
            {QStringLiteral("syncable"), true},
            {QStringLiteral("source"), QStringLiteral("player")},
            {QStringLiteral("atMs"), atMs},
            {QStringLiteral("reason"), QStringLiteral("eof")}};
}

TrackerRemoteDeliveryState remoteAbsent(
    const QString &remoteMediaId,
    TrackerDeliveryFactKind factKind = TrackerDeliveryFactKind::Progress,
    const QString &sourceEventId = {})
{
    return {remoteMediaId, factKind, sourceEventId, false, false,
            QStringLiteral("absent"), {}};
}

TrackerRemoteDeliveryState remoteDifferent(const QString &remoteMediaId,
                                           const QString &fingerprint = QStringLiteral("remote-state"),
                                           const QString &summary = QStringLiteral("Progress: 4 episodes"),
                                           TrackerDeliveryFactKind factKind = TrackerDeliveryFactKind::Progress,
                                           const QString &sourceEventId = {})
{
    return {remoteMediaId, factKind, sourceEventId, true, false, fingerprint, summary};
}

TrackerRemoteDeliveryState remoteMatching(const QString &remoteMediaId,
                                          const QString &fingerprint = QStringLiteral("remote-state"),
                                          const QString &summary = QStringLiteral("Already current"),
                                          TrackerDeliveryFactKind factKind = TrackerDeliveryFactKind::Progress,
                                          const QString &sourceEventId = {})
{
    return {remoteMediaId, factKind, sourceEventId, true, true, fingerprint, summary};
}

TrackerRemoteDeliverySnapshot remoteSnapshot(
    TrackerProviderId provider, const QString &account, quint64 generation,
    const QString &snapshotId, const QList<TrackerRemoteDeliveryState> &items)
{
    return {provider, account, generation, snapshotId, 1500, true, items};
}

TrackerRemoteMediaKey remote(TrackerProviderId provider, const QString &account,
                             const QString &mediaId)
{
    return {provider, account, mediaId};
}

bool installMapping(TrackerMappingStore *store,
                    TrackerProviderId provider,
                    const QString &account,
                    const QString &mediaId,
                    const QString &canonicalId)
{
    return store && store->upsert(
        remote(provider, account, mediaId),
        {canonicalId, QStringLiteral("series"), QStringLiteral("theatre:") + canonicalId,
         QStringLiteral("Example title")},
        TrackerMappingProvenance::UserConfirmed);
}

bool installConnection(TrackerConnectionStore *store,
                       TrackerProviderId provider,
                       const QString &account,
                       quint64 generation = 1)
{
    return store && store->upsert(
        {provider, account, generation, 1000,
         TrackerProviderCapability::ReadProgress | TrackerProviderCapability::WriteProgress
             | TrackerProviderCapability::WriteCompletion,
         TrackerConnectionState::Connected});
}

class FakeSimklScrobbleTransport final : public SimklScrobbleTransport
{
public:
    void send(const TrackerScrobbleIntent &intent, SendCompletion completion) override
    {
        requests.append(intent);
        if (holdSends)
            sendCompletions.append(std::move(completion));
        else
            completion(sendResults.isEmpty() ? SimklScrobbleSendResult::Succeeded
                                             : sendResults.takeFirst());
    }

    void readback(const TrackerScrobbleIntent &intent,
                  ReadbackCompletion completion) override
    {
        readbackRequests.append(intent);
        completion(readbackResults.isEmpty() ? SimklScrobbleReadbackResult::Indeterminate
                                             : readbackResults.takeFirst());
    }

    void completeNextSend(SimklScrobbleSendResult result)
    {
        Q_ASSERT(!sendCompletions.isEmpty());
        sendCompletions.takeFirst()(result);
    }

    QList<TrackerScrobbleIntent> requests;
    QList<TrackerScrobbleIntent> readbackRequests;
    QList<SimklScrobbleSendResult> sendResults;
    QList<SimklScrobbleReadbackResult> readbackResults;
    QList<SendCompletion> sendCompletions;
    bool holdSends = false;
};

std::optional<ProfilePaths> testProfile(QTemporaryDir &root)
{
    return ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
}

class FakeTrackerClock
{
public:
    void advance(qint64 deltaMs)
    {
        m_monotonicMs += deltaMs;
    }

    ActivityPlaybackTracker::MonotonicClockFn monotonicFn()
    {
        return [this]() { return m_monotonicMs; };
    }

    ActivityPlaybackTracker::WallClockFn wallFn()
    {
        return []() { return QDateTime::currentMSecsSinceEpoch(); };
    }

    ActivityPlaybackTracker::UtcOffsetFn offsetFn()
    {
        return []() { return 0; };
    }

private:
    qint64 m_monotonicMs = 0;
};

} // namespace

class TrackerDeliveryTest : public QObject
{
    Q_OBJECT

private slots:
    void firstExportIsInertUntilSelectedItemsAreConfirmed();
    void sameTitleProgressAndCompletionUseSeparateRemoteComparisons();
    void exportReviewBindsACompleteFreshRemoteSnapshot();
    void unselectedFirstExportFactsDoNotLeakThroughStartupRecovery();
    void failedPersistenceCannotGrantConsentOrQueueAnItem();
    void corruptJournalFailsClosed();
    void nativeWritesQueueOnlyAfterConsentAndProgressCoalescesOnlyBeforeSend();
    void startupRecoveryFindsTheLocalSaveToOutboxGapExactlyOnce();
    void changedLocalFactCannotCrossTheDeliveryBoundary();
    void interruptedDeliveryRequiresReadbackBeforeItCanBeSentAgain();
    void providerFailuresRemainItemScopedAndRespectRetryAfter();
    void terminalProviderRefusalSurvivesReopenWithoutBecomingRetryable();
    void malCannotReceiveTelevisionOperationsEvenWithForgedCapabilities();
    void historyOnlyCompletionWitnessSurvivesRestartWithoutEnteringPortableHistory();
    void canonicalSourcePreservesLocalAndAccountSyncProvenance();
    void samePlaybackCompletionCorrelationSuppressesOnlyItsHistoryCopy();
    void delayedActivityCompletionCorrelatesByExactSession();
    void liveRuntimeSuppressesDelayedDivergentKindActivity();
    void liveRuntimeDeduplicatesGuardedNinetyAndEofButKeepsRewatch();
    void movieCompletionCorrelationSurvivesProgressEpisodeHeuristic();
    void episodeCompletionCorrelationSuppressesOnlyItsHistoryCopy();
    void reEnablingSendRefreshesProgressCommittedWhilePaused();
    void profileRuntimeRecoversDurableProgressAfterRestart();
    void playbackLifecycleReportsRealTransitionsButNeverSamplingTicks();
    void closedPlaybackGenerationCannotReplayAsNewStart();
    void scrobblingLeavesNativeHistoryAndStatisticsDigestUnchanged();
    void scrobbleConsentAndUnknownOutcomeSurviveProfileReopen();
    void disconnectDiscardNeverDeletesUnknownOutcomes();
    void scrobbleIntentsRequireConsentExactMovieMappingAndCurrentPlayback();
    void scrobbleDispatchIsSingleFlightAndUnknownRequiresReadback();
    void globalPausePreservesQueueAndResumeReadsUnknownBeforeSendingPending();
    void inFlightUnknownScrobbleWaitsForResumeAfterGlobalPause();
    void disablingScrobblingClosesTheCurrentSessionFirst();
    void ambiguousSimklMovieMappingsFailClosed();
    void queuedScrobbleRevalidatesItsCapturedMappingRevision();
    void profileDeactivationClosesWithThePlayersCurrentPosition();
    void uncertainStopRequiresReadbackAndBlocksOvertake();
    void missingTransportWaitsWithoutAnAttempt();
    void knownNotAppliedEdgeIsSupersededByCurrentPlayerEvent();
    void newSessionRetriesProtectiveCloseBeforeStart();
    void waitingProtectiveCloseSurvivesLaterSessionEvents();
    void failedProtectiveCloseKeepsNewStartQueued();
    void offlineWaitingStartDoesNotBlockAfterRestart();
    void knownNotAppliedPauseDoesNotHideAnOpenProviderSession();
};

void TrackerDeliveryTest::firstExportIsInertUntilSelectedItemsAreConfirmed()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-b"), QStringLiteral("canonical-b")));

    FakeDeliverySource source;
    const auto first = progressFact(QStringLiteral("canonical-a"), 4, 8, QStringLiteral("sha-a"));
    const auto second = progressFact(QStringLiteral("canonical-b"), 7, 3, QStringLiteral("sha-b"));
    const auto remote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       QStringLiteral("snapshot-1"),
                                       {remoteDifferent(QStringLiteral("remote-a")),
                                        remoteDifferent(QStringLiteral("remote-b"))});
    source.commit(first);
    source.commit(second);

    TrackerDeliveryStore store(*profile, &mappings, &connections);
    QVERIFY(!store.createExportPreview(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       {first}, remote, &source).has_value());
    const auto preview = store.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, {first, second}, remote, &source);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.size(), 2);
    QVERIFY(preview->items.at(0).eligible);
    QVERIFY(preview->items.at(1).eligible);
    QVERIFY(preview->items.at(0).willChangeRemote);
    QVERIFY(preview->items.at(0).remotePresent);
    QCOMPARE(preview->items.at(0).remoteStateSummary, QStringLiteral("Progress: 4 episodes"));
    QVERIFY(store.operations().isEmpty());
    QVERIFY(!store.hasFirstExportConsent(TrackerProviderId::Simkl, QStringLiteral("42")));

    const auto changedRemote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                              QStringLiteral("snapshot-2"),
                                              {remoteDifferent(QStringLiteral("remote-a")),
                                               remoteDifferent(QStringLiteral("remote-b"))});
    QVERIFY(!store.confirmExport(preview->previewId, {preview->items.at(0).itemId},
                                 changedRemote, &source, 1999));
    QVERIFY(!store.hasFirstExportConsent(TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(store.confirmExport(preview->previewId, {preview->items.at(0).itemId},
                                remote, &source, 2000));
    QVERIFY(store.hasFirstExportConsent(TrackerProviderId::Simkl, QStringLiteral("42")));
    QCOMPARE(store.operations().size(), 1);
    QCOMPARE(store.operations().first().fact.canonicalMediaId, QStringLiteral("canonical-a"));
    QCOMPARE(store.operations().first().state, TrackerDeliveryState::Pending);
    QCOMPARE(store.operations().first().reviewedRemoteSnapshotId, remote.snapshotId);
    QCOMPARE(store.operations().first().reviewedRemoteStateFingerprint,
             preview->items.at(0).remoteStateFingerprint);

    TrackerDeliveryStore reopened(*profile, &mappings, &connections);
    QVERIFY(reopened.healthy());
    QVERIFY(reopened.hasFirstExportConsent(TrackerProviderId::Simkl, QStringLiteral("42")));
    QCOMPARE(reopened.operations().size(), 1);
    QCOMPARE(reopened.operations().first().operationId, store.operations().first().operationId);

    QVERIFY(store.setProviderSendEnabled(TrackerProviderId::Simkl, QStringLiteral("42"), false));
    QVERIFY(store.confirmExport(preview->previewId, {preview->items.at(0).itemId},
                                remote, &source, 3000));
    QVERIFY(!store.providerSendEnabled(TrackerProviderId::Simkl, QStringLiteral("42")));
}

void TrackerDeliveryTest::exportReviewBindsACompleteFreshRemoteSnapshot()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));
    FakeDeliverySource source;
    const auto fact = progressFact(QStringLiteral("canonical-a"), 1, 5, QStringLiteral("sha-1"));
    source.commit(fact);
    TrackerDeliveryStore store(*profile, &mappings, &connections);

    const auto incomplete = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                           QStringLiteral("partial"), {});
    QVERIFY(!store.createExportPreview(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       {fact}, incomplete, &source));
    QVERIFY(!store.hasFirstExportConsent(TrackerProviderId::Simkl, QStringLiteral("42")));

    const auto current = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                        QStringLiteral("current"),
                                        {remoteMatching(QStringLiteral("remote-a"))});
    const auto preview = store.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, {fact}, current, &source);
    QVERIFY(preview.has_value());
    QVERIFY(!preview->items.first().eligible);
    QVERIFY(preview->items.first().remoteExactlyMatches);
    QVERIFY(!preview->items.first().willChangeRemote);
    QCOMPARE(preview->items.first().reason, TrackerExportIneligibleReason::RemoteAlreadyCurrent);
    QVERIFY(!store.confirmExport(preview->previewId, {preview->items.first().itemId},
                                 current, &source, 2000));
    QVERIFY(store.operations().isEmpty());
    QVERIFY(!store.hasFirstExportConsent(TrackerProviderId::Simkl, QStringLiteral("42")));
}

void TrackerDeliveryTest::sameTitleProgressAndCompletionUseSeparateRemoteComparisons()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));

    FakeDeliverySource source;
    const TrackerDeliveryFact progress = progressFact(
        QStringLiteral("canonical-a"), 8, 62, QStringLiteral("progress-62"));
    const QString completionEvent = QStringLiteral("completion-event-1");
    const TrackerDeliveryFact completion = completionFact(
        QStringLiteral("canonical-a"), 9000, completionEvent, QStringLiteral("completion-9"));
    source.commit(progress);
    source.commit(completion);

    const auto remote = remoteSnapshot(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, QStringLiteral("typed-state"),
        {remoteMatching(QStringLiteral("remote-a"), QStringLiteral("progress-state"),
                        QStringLiteral("Progress is current"), TrackerDeliveryFactKind::Progress),
         remoteDifferent(QStringLiteral("remote-a"), QStringLiteral("completion-state"),
                         QStringLiteral("Completion is absent"),
                         TrackerDeliveryFactKind::Completion, completionEvent)});
    TrackerDeliveryStore store(*profile, &mappings, &connections);
    const auto preview = store.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, {progress, completion},
        remote, &source);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.size(), 2);
    const auto progressItem = std::find_if(preview->items.cbegin(), preview->items.cend(),
        [](const TrackerExportPreviewItem &item) {
            return item.fact.kind == TrackerDeliveryFactKind::Progress;
        });
    const auto completionItem = std::find_if(preview->items.cbegin(), preview->items.cend(),
        [](const TrackerExportPreviewItem &item) {
            return item.fact.kind == TrackerDeliveryFactKind::Completion;
        });
    QVERIFY(progressItem != preview->items.cend());
    QVERIFY(completionItem != preview->items.cend());
    QCOMPARE(progressItem->reason, TrackerExportIneligibleReason::RemoteAlreadyCurrent);
    QVERIFY(!progressItem->willChangeRemote);
    QCOMPARE(completionItem->reason, TrackerExportIneligibleReason::None);
    QVERIFY(completionItem->eligible);
    QVERIFY(completionItem->willChangeRemote);
}

void TrackerDeliveryTest::unselectedFirstExportFactsDoNotLeakThroughStartupRecovery()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-b"), QStringLiteral("canonical-b")));
    FakeDeliverySource source;
    const auto selected = progressFact(QStringLiteral("canonical-a"), 1, 8, QStringLiteral("sha-a"));
    const auto leftUnselected = progressFact(QStringLiteral("canonical-b"), 1, 4, QStringLiteral("sha-b"));
    const auto remote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       QStringLiteral("first-export"),
                                       {remoteAbsent(QStringLiteral("remote-a")),
                                        remoteAbsent(QStringLiteral("remote-b"))});
    source.commit(selected);
    source.commit(leftUnselected);

    TrackerDeliveryStore store(*profile, &mappings, &connections);
    const auto preview = store.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, {selected, leftUnselected}, remote, &source);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.size(), 2);
    const auto selectedItem = std::find_if(preview->items.cbegin(), preview->items.cend(),
        [&selected](const TrackerExportPreviewItem &item) {
            return item.fact.canonicalMediaId == selected.canonicalMediaId;
        });
    QVERIFY(selectedItem != preview->items.cend());
    QVERIFY(store.confirmExport(preview->previewId, {selectedItem->itemId}, remote, &source, 2000));
    QCOMPARE(store.operations().size(), 1);

    int recovered = -1;
    QVERIFY(store.recoverSourceGap(&source, &recovered));
    QCOMPARE(recovered, 0);
    QCOMPARE(store.operations().size(), 1);

    const auto laterProgress = progressFact(QStringLiteral("canonical-b"), 2, 5, QStringLiteral("sha-b2"));
    source.commit(laterProgress);
    QVERIFY(store.recoverSourceGap(&source, &recovered));
    QCOMPARE(recovered, 1);
    QCOMPARE(store.operations().size(), 2);
}

void TrackerDeliveryTest::failedPersistenceCannotGrantConsentOrQueueAnItem()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));
    FakeDeliverySource source;
    const auto fact = progressFact(QStringLiteral("canonical-a"), 1, 5, QStringLiteral("sha-1"));
    const auto remote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       QStringLiteral("export"),
                                       {remoteAbsent(QStringLiteral("remote-a"))});
    source.commit(fact);
    TrackerDeliveryStore store(*profile, &mappings, &connections);
    const auto preview = store.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, {fact}, remote, &source);
    QVERIFY(preview.has_value());

    store.forcePersistenceFailureForTesting(true);
    QVERIFY(!store.confirmExport(preview->previewId, {preview->items.first().itemId},
                                 remote, &source, 2000));
    QVERIFY(!store.hasFirstExportConsent(TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(store.operations().isEmpty());

    store.forcePersistenceFailureForTesting(false);
    QVERIFY(store.confirmExport(preview->previewId, {preview->items.first().itemId},
                                remote, &source, 2001));
    QVERIFY(store.hasFirstExportConsent(TrackerProviderId::Simkl, QStringLiteral("42")));
    QCOMPARE(store.operations().size(), 1);
}

void TrackerDeliveryTest::corruptJournalFailsClosed()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    TrackerDeliveryStore empty(*profile, &mappings, &connections);
    QVERIFY(empty.healthy());
    QFile journal(TrackerDeliveryStore::storagePath(*profile));
    QVERIFY(journal.open(QIODevice::WriteOnly));
    QCOMPARE(journal.write("{corrupt"), qint64(8));
    journal.close();

    TrackerDeliveryStore corrupt(*profile, &mappings, &connections);
    QString error;
    QVERIFY(!corrupt.healthy(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(corrupt.operations().isEmpty());
    QVERIFY(corrupt.readyOperations(100'000).isEmpty());
}

void TrackerDeliveryTest::nativeWritesQueueOnlyAfterConsentAndProgressCoalescesOnlyBeforeSend()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));
    FakeDeliverySource source;
    const auto first = progressFact(QStringLiteral("canonical-a"), 1, 2, QStringLiteral("sha-1"));
    const auto second = progressFact(QStringLiteral("canonical-a"), 2, 3, QStringLiteral("sha-2"));
    source.commit(first);

    TrackerDeliveryStore store(*profile, &mappings, &connections);
    QString operationId;
    QVERIFY(!store.observeCommittedFact(first, &source, &operationId));
    auto imported = first;
    imported.origin = TrackerDeliveryOrigin::TrackerImport;
    source.commit(imported);
    QVERIFY(!store.observeCommittedFact(imported, &source, &operationId));
    QVERIFY(store.operations().isEmpty());
    source.commit(first);
    const auto remote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       QStringLiteral("export"),
                                       {remoteAbsent(QStringLiteral("remote-a"))});

    const auto preview = store.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, {first}, remote, &source);
    QVERIFY(preview.has_value());
    QVERIFY(store.confirmExport(preview->previewId, {preview->items.first().itemId},
                                remote, &source, 2000));
    source.commit(second);
    QVERIFY(store.observeCommittedFact(second, &source, &operationId));
    QCOMPARE(store.operations().size(), 1);
    QCOMPARE(store.operations().first().fact.progress, 3);
    QCOMPARE(store.operations().first().fact.sourceRevision, quint64(2));

    QVERIFY(store.markDelivering(operationId, &source, 3000));
    const auto third = progressFact(QStringLiteral("canonical-a"), 3, 4, QStringLiteral("sha-3"));
    source.commit(third);
    QVERIFY(store.observeCommittedFact(third, &source));
    QCOMPARE(store.operations().size(), 2);
    QCOMPARE(store.operations().first().state, TrackerDeliveryState::Delivering);
    QCOMPARE(store.operations().last().fact.progress, 4);

    const auto completionOne = completionFact(QStringLiteral("canonical-a"), 4,
                                              QStringLiteral("completion-1"), QStringLiteral("sha-c1"));
    const auto completionTwo = completionFact(QStringLiteral("canonical-a"), 5,
                                              QStringLiteral("completion-2"), QStringLiteral("sha-c2"));
    source.commit(completionOne);
    source.commit(completionTwo);
    QVERIFY(store.observeCommittedFact(completionOne, &source));
    QVERIFY(store.observeCommittedFact(completionTwo, &source));
    QCOMPARE(store.operations().size(), 4);
}

void TrackerDeliveryTest::startupRecoveryFindsTheLocalSaveToOutboxGapExactlyOnce()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));
    FakeDeliverySource source;
    const auto reviewed = progressFact(QStringLiteral("canonical-a"), 1, 2, QStringLiteral("sha-1"));
    const auto savedButNotQueued = progressFact(QStringLiteral("canonical-a"), 2, 3, QStringLiteral("sha-2"));
    const auto remote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       QStringLiteral("export"),
                                       {remoteAbsent(QStringLiteral("remote-a"))});
    source.commit(reviewed);
    {
        TrackerDeliveryStore store(*profile, &mappings, &connections);
        const auto preview = store.createExportPreview(
            TrackerProviderId::Simkl, QStringLiteral("42"), 1, {reviewed}, remote, &source);
        QVERIFY(preview.has_value());
        QVERIFY(store.confirmExport(preview->previewId, {preview->items.first().itemId},
                                    remote, &source, 2000));
        source.commit(savedButNotQueued);
    }

    TrackerDeliveryStore reopened(*profile, &mappings, &connections);
    int recovered = 0;
    QVERIFY(reopened.recoverSourceGap(&source, &recovered));
    QCOMPARE(recovered, 1);
    QCOMPARE(reopened.operations().size(), 1);
    QCOMPARE(reopened.operations().first().fact.sourceRevision, quint64(2));
    QCOMPARE(reopened.operations().first().fact.progress, 3);
    QVERIFY(reopened.recoverSourceGap(&source, &recovered));
    QCOMPARE(recovered, 0);
    QCOMPARE(reopened.operations().size(), 1);
}

void TrackerDeliveryTest::changedLocalFactCannotCrossTheDeliveryBoundary()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));
    FakeDeliverySource source;
    const auto reviewed = progressFact(QStringLiteral("canonical-a"), 1, 2, QStringLiteral("sha-1"));
    const auto newer = progressFact(QStringLiteral("canonical-a"), 2, 4, QStringLiteral("sha-2"));
    const auto remote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       QStringLiteral("export"),
                                       {remoteAbsent(QStringLiteral("remote-a"))});
    source.commit(reviewed);
    TrackerDeliveryStore store(*profile, &mappings, &connections);
    const auto preview = store.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, {reviewed}, remote, &source);
    QVERIFY(preview.has_value());
    QVERIFY(store.confirmExport(preview->previewId, {preview->items.first().itemId},
                                remote, &source, 2000));
    const QString id = store.operations().first().operationId;
    source.commit(newer);

    QVERIFY(!store.markDelivering(id, &source, 3000));
    QCOMPARE(store.operation(id)->state, TrackerDeliveryState::NeedsAttention);
    QCOMPARE(store.operation(id)->reason, TrackerDeliveryReason::StaleLocalFact);
}

void TrackerDeliveryTest::interruptedDeliveryRequiresReadbackBeforeItCanBeSentAgain()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));
    FakeDeliverySource source;
    const auto fact = progressFact(QStringLiteral("canonical-a"), 1, 8, QStringLiteral("sha-1"));
    const auto remote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       QStringLiteral("export"),
                                       {remoteAbsent(QStringLiteral("remote-a"))});
    source.commit(fact);

    QString operationId;
    {
        TrackerDeliveryStore store(*profile, &mappings, &connections);
        const auto preview = store.createExportPreview(
            TrackerProviderId::Simkl, QStringLiteral("42"), 1, {fact}, remote, &source);
        QVERIFY(preview.has_value());
        QVERIFY(store.confirmExport(preview->previewId, {preview->items.first().itemId},
                                    remote, &source, 2000));
        operationId = store.operations().first().operationId;
        QVERIFY(store.markDelivering(operationId, &source, 3000));
    }

    TrackerDeliveryStore reopened(*profile, &mappings, &connections);
    QVERIFY(reopened.healthy());
    QCOMPARE(reopened.operations().first().state, TrackerDeliveryState::UnknownOutcome);
    QVERIFY(reopened.readyOperations(10'000).isEmpty());
    QCOMPARE(reopened.operations().first().attemptCount, 1);
    QVERIFY(!reopened.markDelivering(operationId, &source, 10'000));
    QCOMPARE(reopened.operation(operationId)->state, TrackerDeliveryState::UnknownOutcome);
    QCOMPARE(reopened.operation(operationId)->attemptCount, 1);
    QVERIFY(reopened.reconcileUnknown(operationId, TrackerDeliveryReadback::Indeterminate, 10'001));
    QCOMPARE(reopened.operations().first().state, TrackerDeliveryState::UnknownOutcome);
    QVERIFY(reopened.readyOperations(10'002).isEmpty());
    QVERIFY(reopened.reconcileUnknown(operationId, TrackerDeliveryReadback::Absent, 10'003));
    QCOMPARE(reopened.operations().first().state, TrackerDeliveryState::Pending);
    QCOMPARE(reopened.readyOperations(10'003).size(), 1);
    QVERIFY(reopened.markDelivering(operationId, &source, 10'003));
    QVERIFY(reopened.recordAttemptResult(operationId, TrackerDeliveryAttemptResult::UnknownOutcome,
                                         10'004));
    QCOMPARE(reopened.operations().first().state, TrackerDeliveryState::UnknownOutcome);
    QVERIFY(reopened.reconcileUnknown(operationId, TrackerDeliveryReadback::ExactPresent, 10'005));
    QCOMPARE(reopened.operations().first().state, TrackerDeliveryState::Succeeded);
    QVERIFY(reopened.readyOperations(20'000).isEmpty());

    const auto newer = progressFact(QStringLiteral("canonical-a"), 2, 9, QStringLiteral("sha-2"));
    source.commit(newer);
    QVERIFY(reopened.observeCommittedFact(newer, &source));
    const QString newerId = reopened.operations().last().operationId;
    QVERIFY(reopened.markDelivering(newerId, &source, 20'001));
    QVERIFY(reopened.recordAttemptResult(newerId, TrackerDeliveryAttemptResult::UnknownOutcome,
                                         20'002));
    QVERIFY(reopened.reconcileUnknown(newerId, TrackerDeliveryReadback::PresentDifferent, 20'003));
    QCOMPARE(reopened.operation(newerId)->state, TrackerDeliveryState::NeedsAttention);
    QCOMPARE(reopened.operation(newerId)->reason, TrackerDeliveryReason::ReadbackDifferent);
}

void TrackerDeliveryTest::providerFailuresRemainItemScopedAndRespectRetryAfter()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-s"), QStringLiteral("canonical-s")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-t"), QStringLiteral("canonical-t")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-u"), QStringLiteral("canonical-u")));
    FakeDeliverySource source;
    const auto simklFact = progressFact(QStringLiteral("canonical-s"), 1, 8, QStringLiteral("sha-s"));
    const auto secondFact = progressFact(QStringLiteral("canonical-t"), 1, 6, QStringLiteral("sha-t"));
    const auto waitingFact = progressFact(QStringLiteral("canonical-u"), 1, 4, QStringLiteral("sha-u"));
    source.commit(simklFact);
    source.commit(secondFact);
    source.commit(waitingFact);
    const auto remote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("42"), 1,
                                       QStringLiteral("batch"),
                                       {remoteAbsent(QStringLiteral("remote-s")),
                                        remoteAbsent(QStringLiteral("remote-t")),
                                        remoteAbsent(QStringLiteral("remote-u"))});

    TrackerDeliveryStore store(*profile, &mappings, &connections);
    const auto preview = store.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1,
        {simklFact, secondFact, waitingFact}, remote, &source);
    QVERIFY(preview.has_value());
    const auto simklItem = std::find_if(preview->items.cbegin(), preview->items.cend(),
        [&simklFact](const TrackerExportPreviewItem &item) {
            return item.fact.canonicalMediaId == simklFact.canonicalMediaId;
        });
    const auto secondItem = std::find_if(preview->items.cbegin(), preview->items.cend(),
        [&secondFact](const TrackerExportPreviewItem &item) {
            return item.fact.canonicalMediaId == secondFact.canonicalMediaId;
        });
    const auto waitingItem = std::find_if(preview->items.cbegin(), preview->items.cend(),
        [&waitingFact](const TrackerExportPreviewItem &item) {
            return item.fact.canonicalMediaId == waitingFact.canonicalMediaId;
        });
    QVERIFY(simklItem != preview->items.cend());
    QVERIFY(secondItem != preview->items.cend());
    QVERIFY(waitingItem != preview->items.cend());
    QVERIFY(store.confirmExport(preview->previewId,
                                {simklItem->itemId, secondItem->itemId, waitingItem->itemId},
                                remote, &source, 2000));
    const auto operations = store.operations();
    QCOMPARE(operations.size(), 3);
    const auto operationFor = [&store](const QString &canonicalId) {
        for (const TrackerDeliveryOperation &operation : store.operations()) {
            if (operation.fact.canonicalMediaId == canonicalId)
                return operation.operationId;
        }
        return QString();
    };
    const QString simklId = operationFor(QStringLiteral("canonical-s"));
    const QString secondId = operationFor(QStringLiteral("canonical-t"));
    const QString waitingId = operationFor(QStringLiteral("canonical-u"));
    QVERIFY(!simklId.isEmpty());
    QVERIFY(!secondId.isEmpty());
    QVERIFY(!waitingId.isEmpty());

    QVERIFY(store.markDelivering(simklId, &source, 3000));
    QVERIFY(store.recordAttemptResult(simklId, TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied,
                                      3001, 9000, TrackerDeliveryReason::ProviderRateLimited));
    QVERIFY(!store.markDelivering(secondId, &source, 3002));
    QCOMPARE(store.operation(simklId)->state, TrackerDeliveryState::Retrying);
    QCOMPARE(store.operation(simklId)->nextAttemptAtMs, qint64(9000));
    QVERIFY(store.readyOperations(8999).isEmpty());
    QCOMPARE(store.readyOperations(9000).size(), 3);
    QVERIFY(store.markDelivering(secondId, &source, 9000));
    QVERIFY(store.recordAttemptResult(secondId, TrackerDeliveryAttemptResult::Succeeded, 9001));
    QCOMPARE(store.operation(secondId)->state, TrackerDeliveryState::Succeeded);
    TrackerDeliveryStore afterPartialBatch(*profile, &mappings, &connections);
    QVERIFY(afterPartialBatch.healthy());
    QCOMPARE(afterPartialBatch.operation(simklId)->state, TrackerDeliveryState::Retrying);
    QCOMPARE(afterPartialBatch.operation(secondId)->state, TrackerDeliveryState::Succeeded);

    qint64 finalRetryAfter = 0;
    for (int attempt = 2; attempt <= 5; ++attempt) {
        const qint64 sendAt = store.operation(simklId)->nextAttemptAtMs;
        QVERIFY(store.markDelivering(simklId, &source, sendAt));
        const auto result = attempt == 5
            ? TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied
            : TrackerDeliveryAttemptResult::RetryableKnownNotApplied;
        finalRetryAfter = attempt == 5 ? sendAt + 60'000 : 0;
        QVERIFY(store.recordAttemptResult(simklId, result, sendAt + 1, finalRetryAfter,
                                          attempt == 5 ? TrackerDeliveryReason::ProviderRateLimited
                                                       : TrackerDeliveryReason::None));
        if (attempt < 5) {
            QCOMPARE(store.operation(simklId)->state, TrackerDeliveryState::Retrying);
        } else {
            QCOMPARE(store.operation(simklId)->state, TrackerDeliveryState::NeedsAttention);
            QCOMPARE(store.operation(simklId)->reason, TrackerDeliveryReason::RetryLimitReached);
            QVERIFY(!store.markDelivering(waitingId, &source, sendAt + 2));
        }
    }
    TrackerDeliveryStore afterRetryLimit(*profile, &mappings, &connections);
    QVERIFY(afterRetryLimit.healthy());
    QCOMPARE(afterRetryLimit.operation(simklId)->state, TrackerDeliveryState::NeedsAttention);
    QCOMPARE(afterRetryLimit.operation(simklId)->reason, TrackerDeliveryReason::RetryLimitReached);
    QCOMPARE(afterRetryLimit.operation(secondId)->state, TrackerDeliveryState::Succeeded);
    QVERIFY(!afterRetryLimit.markDelivering(waitingId, &source, finalRetryAfter - 1));
    QVERIFY(afterRetryLimit.markDelivering(waitingId, &source, finalRetryAfter));
    QVERIFY(afterRetryLimit.recordAttemptResult(waitingId, TrackerDeliveryAttemptResult::Succeeded,
                                                finalRetryAfter + 1));
}

void TrackerDeliveryTest::terminalProviderRefusalSurvivesReopenWithoutBecomingRetryable()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("account-simkl")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("account-simkl"),
                           QStringLiteral("remote-simkl"), QStringLiteral("canonical-simkl")));
    FakeDeliverySource source;
    const auto fact = progressFact(QStringLiteral("canonical-simkl"), 1, 7, QStringLiteral("sha-simkl"));
    const auto remote = remoteSnapshot(TrackerProviderId::Simkl, QStringLiteral("account-simkl"), 1,
                                       QStringLiteral("initial"),
                                       {remoteAbsent(QStringLiteral("remote-simkl"))});
    source.commit(fact);
    QString operationId;
    {
        TrackerDeliveryStore store(*profile, &mappings, &connections);
        const auto preview = store.createExportPreview(
            TrackerProviderId::Simkl, QStringLiteral("account-simkl"), 1, {fact}, remote, &source);
        QVERIFY(preview.has_value());
        QVERIFY(store.confirmExport(preview->previewId, {preview->items.first().itemId},
                                    remote, &source, 2000));
        operationId = store.operations().first().operationId;
        QVERIFY(store.markDelivering(operationId, &source, 3000));
        QVERIFY(store.recordAttemptResult(operationId, TrackerDeliveryAttemptResult::FailedTerminal,
                                          3001));
    }
    TrackerDeliveryStore reopened(*profile, &mappings, &connections);
    QVERIFY(reopened.healthy());
    QCOMPARE(reopened.operation(operationId)->state, TrackerDeliveryState::FailedTerminal);
    QCOMPARE(reopened.operation(operationId)->reason, TrackerDeliveryReason::TerminalProviderRefusal);
    QVERIFY(reopened.readyOperations(100'000).isEmpty());
}

void TrackerDeliveryTest::malCannotReceiveTelevisionOperationsEvenWithForgedCapabilities()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Mal, QStringLiteral("account-mal")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Mal, QStringLiteral("account-mal"),
                           QStringLiteral("remote-mal"), QStringLiteral("canonical-mal")));
    FakeDeliverySource source;
    const auto fact = progressFact(QStringLiteral("canonical-mal"), 1, 7, QStringLiteral("sha-mal"));
    source.commit(fact);
    const auto remote = remoteSnapshot(TrackerProviderId::Mal, QStringLiteral("account-mal"), 1,
                                       QStringLiteral("mal-state"),
                                       {remoteAbsent(QStringLiteral("remote-mal"))});
    TrackerDeliveryStore store(*profile, &mappings, &connections);
    const auto preview = store.createExportPreview(
        TrackerProviderId::Mal, QStringLiteral("account-mal"), 1, {fact}, remote, &source);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.first().reason, TrackerExportIneligibleReason::Unsupported);
    QVERIFY(!preview->items.first().eligible);
    QVERIFY(!store.confirmExport(preview->previewId, {preview->items.first().itemId},
                                 remote, &source, 2000));
    QVERIFY(store.operations().isEmpty());
    QVERIFY(!store.hasFirstExportConsent(TrackerProviderId::Mal, QStringLiteral("account-mal")));
}

void TrackerDeliveryTest::historyOnlyCompletionWitnessSurvivesRestartWithoutEnteringPortableHistory()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString path = root.filePath(QStringLiteral("history.ini"));
    QString eventId;
    {
        HistoryStore history(path);
        QVERIFY(history.markProgressCompleted(QStringLiteral("movie"),
                                              QStringLiteral("movie:local"), 50000,
                                              QString(), QStringLiteral("session-persisted")));
        const QVariantList witnesses = history.trackerLocalCompletionFacts();
        QCOMPARE(witnesses.size(), 1);
        eventId = witnesses.first().toMap().value(QStringLiteral("eventId")).toString();
        QVERIFY(eventId.startsWith(QLatin1String("history-progress-")));
        QCOMPARE(history.syncEntries().size(), 1);
        QVERIFY(!history.syncEntries().first().toMap().contains(QStringLiteral("eventId")));
        QVERIFY(!history.syncEntries().first().toMap().contains(QStringLiteral("activitySessionId")));
    }

    {
        HistoryStore reopened(path);
        QVERIFY(reopened.trackerEvidenceHealthy());
        QCOMPARE(reopened.trackerLocalCompletionFact(eventId)
                     .value(QStringLiteral("atMs")).toLongLong(), 50000);
        QCOMPARE(reopened.trackerLocalCompletionFact(eventId)
                     .value(QStringLiteral("activitySessionId")).toString(),
                 QStringLiteral("session-persisted"));
        QVERIFY(reopened.remove(QStringLiteral("movie"), QStringLiteral("movie:local")));
        QVERIFY(reopened.trackerLocalCompletionFacts().isEmpty());
    }

    HistoryStore accountSynced(root.filePath(QStringLiteral("remote-history.ini")));
    QVERIFY(accountSynced.applySyncedRecord(
        {{QStringLiteral("kind"), QStringLiteral("movie")},
         {QStringLiteral("id"), QStringLiteral("movie:remote")},
         {QStringLiteral("firstActivityAt"), 60000},
         {QStringLiteral("lastActivityAt"), 70000},
         {QStringLiteral("completedAt"), 70000}}));
    QVERIFY(accountSynced.trackerLocalCompletionFacts().isEmpty());
}

void TrackerDeliveryTest::canonicalSourcePreservesLocalAndAccountSyncProvenance()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    ProgressStore progress(root.filePath(QStringLiteral("progress.ini")));
    ActivityStore activity;
    HistoryStore history(root.filePath(QStringLiteral("history.ini")));

    progress.record(progressEntry(QStringLiteral("anime"), QStringLiteral("anime:local"), 0.35));
    QVariantMap syncedProgress = progressEntry(QStringLiteral("anime"),
                                                QStringLiteral("anime:remote"), 0.60);
    syncedProgress.insert(QStringLiteral("updatedAt"), 1000);
    QVERIFY(progress.applySyncedEntry(syncedProgress));
    progress.flush();
    QTRY_VERIFY(progress.deliverySnapshotDurable());

    QVERIFY(activity.recordCompletion(activityCompletion(
        QStringLiteral("completion-local-1"), QStringLiteral("movie"),
        QStringLiteral("movie:x"), 20000)));
    QVERIFY(activity.recordCompletion(activityCompletion(
        QStringLiteral("completion-local-2"), QStringLiteral("movie"),
        QStringLiteral("movie:x"), 30000)));
    QString error;
    QVERIFY2(activity.applySyncedPortableFact(activityCompletion(
        QStringLiteral("completion-remote"), QStringLiteral("movie"),
        QStringLiteral("movie:y"), 40000), &error), qPrintable(error));

    // The same title can have separate completion events (for example, rewatches).
    // Keep the Progress-origin witness unless shared event identity proves a duplicate.
    QVERIFY(history.markProgressCompleted(QStringLiteral("movie"),
                                          QStringLiteral("movie:x"), 25000));

    // This is the real ProgressStore -> ConsumptionHistoryBridge route. It
    // commits native History without inventing a second Activity/stat event.
    ConsumptionHistoryBridge bridge(&activity, &progress, &history);
    progress.record(progressEntry(QStringLiteral("video"),
                                  QStringLiteral("movie:history-only"), 0.35));
    progress.record(progressEntry(QStringLiteral("video"),
                                  QStringLiteral("movie:history-only"), 0.92));
    progress.flush();
    QVERIFY(history.completed(QStringLiteral("movie"), QStringLiteral("movie:history-only")));

    TrackerCanonicalDeliverySource source(&progress, &activity, &history);
    QVERIFY(source.isReady());
    const QList<TrackerDeliveryFact> facts = source.currentCommittedFacts();
    QCOMPARE(facts.size(), 7);

    int nativeProgress = 0;
    int accountProgress = 0;
    QStringList nativeCompletionIds;
    QStringList historyCompletionIds;
    int accountCompletions = 0;
    int historyCompletions = 0;
    for (const TrackerDeliveryFact &fact : facts) {
        if (fact.kind == TrackerDeliveryFactKind::Progress
            && fact.historyId == QLatin1String("anime:local")) {
            QCOMPARE(fact.origin, TrackerDeliveryOrigin::NativeLocal);
            ++nativeProgress;
            QVERIFY(source.isDurablyCurrent(fact));
        } else if (fact.kind == TrackerDeliveryFactKind::Progress
                   && fact.historyId == QLatin1String("anime:remote")) {
            QCOMPARE(fact.origin, TrackerDeliveryOrigin::AccountSync);
            ++accountProgress;
        } else if (fact.kind == TrackerDeliveryFactKind::Completion
                   && fact.historyId == QLatin1String("movie:x")) {
            QCOMPARE(fact.origin, TrackerDeliveryOrigin::NativeLocal);
            QVERIFY(source.isDurablyCurrent(fact));
            if (fact.sourceEventId.startsWith(QLatin1String("history-progress-"))) {
                historyCompletionIds.append(fact.sourceEventId);
                ++historyCompletions;
            } else {
                nativeCompletionIds.append(fact.sourceEventId);
            }
        } else if (fact.kind == TrackerDeliveryFactKind::Completion
                   && fact.historyId == QLatin1String("movie:y")) {
            QCOMPARE(fact.origin, TrackerDeliveryOrigin::AccountSync);
            ++accountCompletions;
        } else if (fact.kind == TrackerDeliveryFactKind::Completion
                   && fact.historyId == QLatin1String("movie:history-only")) {
            QCOMPARE(fact.origin, TrackerDeliveryOrigin::NativeLocal);
            QVERIFY(fact.sourceEventId.startsWith(QLatin1String("history-progress-")));
            QVERIFY(source.isDurablyCurrent(fact));
            ++historyCompletions;
            historyCompletionIds.append(fact.sourceEventId);
        }
    }
    QCOMPARE(nativeProgress, 1);
    QCOMPARE(accountProgress, 1);
    QCOMPARE(nativeCompletionIds.size(), 2);
    QVERIFY(nativeCompletionIds.contains(QStringLiteral("completion-local-1")));
    QVERIFY(nativeCompletionIds.contains(QStringLiteral("completion-local-2")));
    QCOMPARE(accountCompletions, 1);
    QCOMPARE(historyCompletions, 2);
    QCOMPARE(historyCompletionIds.size(), 2);
    QVERIFY(historyCompletionIds.at(0) != historyCompletionIds.at(1));

    for (const QVariant &entry : progress.syncEntries())
        QVERIFY(!entry.toMap().contains(QStringLiteral("_trackerOrigin")));
    for (const QVariantMap &entry : activity.portableSyncFacts())
        QVERIFY(!entry.contains(QStringLiteral("_trackerOrigin")));
}

void TrackerDeliveryTest::samePlaybackCompletionCorrelationSuppressesOnlyItsHistoryCopy()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    ProgressStore progress(root.filePath(QStringLiteral("progress.ini")));
    ActivityStore activity;
    HistoryStore history(root.filePath(QStringLiteral("history.ini")));
    ConsumptionHistoryBridge bridge(&activity, &progress, &history);

    const QString kind = QStringLiteral("video");
    const QString id = QStringLiteral("movie:rewatch");
    QVariantMap baseline = progressEntry(kind, id, 0.35);
    baseline.insert(QStringLiteral("_trackerActivityEventId"), QStringLiteral("activity-first"));
    baseline.insert(QStringLiteral("_trackerActivitySessionId"), QStringLiteral("session-first"));
    progress.record(baseline);
    QVERIFY(!progress.deliveryEntry(kind, id)
                 .contains(QStringLiteral("_trackerActivityEventId")));
    QVERIFY(!progress.deliveryEntry(kind, id)
                 .contains(QStringLiteral("_trackerActivitySessionId")));
    for (const QVariant &entry : progress.syncEntries())
        QVERIFY(!entry.toMap().contains(QStringLiteral("_trackerActivityEventId")));
    for (const QVariant &entry : progress.syncEntries())
        QVERIFY(!entry.toMap().contains(QStringLiteral("_trackerActivitySessionId")));

    QVERIFY(activity.recordCompletion(activityCompletion(
        QStringLiteral("activity-first"), QStringLiteral("movie"), id, 20000)));
    QVariantMap crossing = progressEntry(kind, id, 0.90);
    crossing.insert(QStringLiteral("_trackerActivityEventId"), QStringLiteral("activity-first"));
    progress.record(crossing);

    QVariantList witnesses = history.trackerLocalCompletionFacts();
    QCOMPARE(witnesses.size(), 1);
    QCOMPARE(witnesses.first().toMap().value(QStringLiteral("activityEventId")).toString(),
             QStringLiteral("activity-first"));
    QVERIFY(!history.syncEntries().first().toMap().contains(QStringLiteral("activityEventId")));

    TrackerCanonicalDeliverySource source(&progress, &activity, &history);
    QList<TrackerDeliveryFact> facts = source.currentCommittedFacts();
    QCOMPARE(facts.size(), 1);
    QCOMPARE(facts.first().sourceEventId, QStringLiteral("activity-first"));
    QVERIFY(source.isDurablyCurrent(facts.first()));

    // A later completion of the same title has a different Activity event ID
    // and no correlation link, so it remains a distinct rewatch candidate.
    QVERIFY(activity.recordCompletion(activityCompletion(
        QStringLiteral("activity-rewatch"), QStringLiteral("movie"), id, 30000)));
    progress.record(progressEntry(kind, id, 0.35));
    progress.record(progressEntry(kind, id, 0.90));

    witnesses = history.trackerLocalCompletionFacts();
    QCOMPARE(witnesses.size(), 2);
    facts = source.currentCommittedFacts();
    QCOMPARE(facts.size(), 3);
    QStringList activityEventIds;
    int uncorrelatedProgressCompletions = 0;
    for (const TrackerDeliveryFact &fact : facts) {
        QVERIFY(source.isDurablyCurrent(fact));
        if (fact.sourceEventId.startsWith(QLatin1String("history-progress-"))) {
            ++uncorrelatedProgressCompletions;
        } else {
            activityEventIds.append(fact.sourceEventId);
        }
    }
    QCOMPARE(activityEventIds.size(), 2);
    QVERIFY(activityEventIds.contains(QStringLiteral("activity-first")));
    QVERIFY(activityEventIds.contains(QStringLiteral("activity-rewatch")));
    QCOMPARE(uncorrelatedProgressCompletions, 1);
}

void TrackerDeliveryTest::delayedActivityCompletionCorrelatesByExactSession()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    ProgressStore progress(root.filePath(QStringLiteral("progress.ini")));
    ActivityStore activity;
    HistoryStore history(root.filePath(QStringLiteral("history.ini")));
    ConsumptionHistoryBridge bridge(&activity, &progress, &history);

    const QString kind = QStringLiteral("video");
    const QString id = QStringLiteral("movie:seeked-near-end");
    const QString sessionId = QStringLiteral("play-session-delayed");
    progress.record(progressEntry(kind, id, 0.35));
    QVariantMap pausedNearEnd = progressEntry(kind, id, 0.95);
    pausedNearEnd.insert(QStringLiteral("_trackerActivitySessionId"), sessionId);
    progress.record(pausedNearEnd);
    progress.flush();
    QTRY_VERIFY_WITH_TIMEOUT(progress.deliverySnapshotDurable(), 3000);

    QVariantList witnesses = history.trackerLocalCompletionFacts();
    QCOMPARE(witnesses.size(), 1);
    QCOMPARE(witnesses.first().toMap().value(QStringLiteral("activitySessionId")).toString(),
             sessionId);
    TrackerCanonicalDeliverySource source(&progress, &activity, &history);
    QList<TrackerDeliveryFact> facts = source.currentCommittedFacts();
    QCOMPARE(facts.size(), 1);
    QVERIFY(facts.first().sourceEventId.startsWith(QLatin1String("history-progress-")));
    QVERIFY(source.isDurablyCurrent(facts.first()));

    QTest::qWait(5);
    QVariantMap delayedEof = activityCompletion(
        QStringLiteral("activity-delayed-eof"), QStringLiteral("movie"), id,
        QDateTime::currentMSecsSinceEpoch());
    delayedEof.insert(QStringLiteral("sessionId"), sessionId);
    QVERIFY(activity.recordCompletion(delayedEof));
    facts = source.currentCommittedFacts();
    QCOMPARE(facts.size(), 1);
    QCOMPARE(facts.first().sourceEventId,
             witnesses.first().toMap().value(QStringLiteral("eventId")).toString());
    QVERIFY(source.isDurablyCurrent(facts.first()));

    QTest::qWait(5);
    QVariantMap rewatch = activityCompletion(
        QStringLiteral("activity-new-session-rewatch"), QStringLiteral("movie"), id,
        QDateTime::currentMSecsSinceEpoch());
    rewatch.insert(QStringLiteral("sessionId"), QStringLiteral("play-session-rewatch"));
    QVERIFY(activity.recordCompletion(rewatch));
    facts = source.currentCommittedFacts();
    QCOMPARE(facts.size(), 2);
    QStringList completionIds;
    for (const TrackerDeliveryFact &fact : facts) {
        QVERIFY(source.isDurablyCurrent(fact));
        completionIds.append(fact.sourceEventId);
    }
    QVERIFY(completionIds.contains(QStringLiteral("activity-new-session-rewatch")));
    QVERIFY(completionIds.contains(witnesses.first().toMap()
        .value(QStringLiteral("eventId")).toString()));
    QVERIFY(!completionIds.contains(QStringLiteral("activity-delayed-eof")));
}

void TrackerDeliveryTest::liveRuntimeSuppressesDelayedDivergentKindActivity()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    ConsumptionHistoryBridge bridge(&activity, &progress, &history);
    TrackerDeliveryRuntime runtime(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(runtime.start(&error), qPrintable(error));

    const QString account = QStringLiteral("simkl-account");
    const QString remoteId = QStringLiteral("simkl-feature-cut");
    const QString id = QStringLiteral("catalog:feature-cut:extended");
    const QString sessionId = QStringLiteral("play-session-delayed-feature-cut");
    QVERIFY(installConnection(runtime.connectionStore(), TrackerProviderId::Simkl, account));

    // ProgressStore's legacy colon heuristic calls this an episode, while Activity
    // knows the same exact item as a movie. Progress crosses first after a seek.
    progress.record(progressEntry(QStringLiteral("video"), id, 0.35));
    QVariantMap pausedNearEnd = progressEntry(QStringLiteral("video"), id, 0.95);
    pausedNearEnd.insert(QStringLiteral("_trackerActivitySessionId"), sessionId);
    progress.record(pausedNearEnd);
    progress.flush();
    QTRY_VERIFY_WITH_TIMEOUT(progress.deliverySnapshotDurable(), 3000);

    const QList<TrackerDeliveryFact> facts = runtime.canonicalSource()->currentCommittedFacts();
    const auto historyFactIt = std::find_if(facts.cbegin(), facts.cend(),
        [](const TrackerDeliveryFact &fact) {
            return fact.kind == TrackerDeliveryFactKind::Completion
                && fact.sourceEventId.startsWith(QLatin1String("history-progress-"));
        });
    QVERIFY(historyFactIt != facts.cend());
    const TrackerDeliveryFact historyFact = *historyFactIt;
    QCOMPARE(historyFact.historyKind, QStringLiteral("episode"));
    QVERIFY(runtime.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, remoteId),
        {historyFact.canonicalMediaId, historyFact.historyKind, historyFact.historyId,
         QStringLiteral("Feature cut")},
        TrackerMappingProvenance::UserConfirmed));

    const auto preview = runtime.deliveryStore()->createExportPreview(
        TrackerProviderId::Simkl, account, 1, facts,
        remoteSnapshot(TrackerProviderId::Simkl, account, 1, QStringLiteral("first-export"),
                       {remoteAbsent(remoteId, TrackerDeliveryFactKind::Completion,
                                     historyFact.sourceEventId)}),
        runtime.canonicalSource(), &error);
    QVERIFY2(preview.has_value(), qPrintable(error));
    const auto selected = std::find_if(preview->items.cbegin(), preview->items.cend(),
        [&historyFact](const TrackerExportPreviewItem &item) {
            return item.fact.sourceEventId == historyFact.sourceEventId && item.eligible;
        });
    QVERIFY(selected != preview->items.cend());
    QVERIFY2(runtime.deliveryStore()->confirmExport(
                 preview->previewId, {selected->itemId}, preview->remoteSnapshot,
                 runtime.canonicalSource(), 2000, &error), qPrintable(error));
    QCOMPARE(runtime.deliveryStore()->operations().size(), 1);
    QCOMPARE(runtime.deliveryStore()->operations().first().fact.sourceEventId,
             historyFact.sourceEventId);

    // A completion committed later in the exact same session, even at the same
    // millisecond, must not replace or duplicate the already selected History fact.
    QVariantMap tiedEof = activityCompletion(
        QStringLiteral("activity-tied-eof"), QStringLiteral("movie"), id,
        static_cast<qint64>(historyFact.sourceRevision));
    tiedEof.insert(QStringLiteral("sessionId"), sessionId);
    QVERIFY(activity.recordCompletion(tiedEof));
    QVERIFY(!runtime.canonicalSource()->currentActivityCompletionFact(
        QStringLiteral("activity-tied-eof")).has_value());
    QCOMPARE(runtime.deliveryStore()->operations().size(), 1);

    QTest::qWait(5);
    QVariantMap delayedEof = activityCompletion(
        QStringLiteral("activity-delayed-eof"), QStringLiteral("movie"), id,
        QDateTime::currentMSecsSinceEpoch());
    delayedEof.insert(QStringLiteral("sessionId"), sessionId);
    QVERIFY(activity.recordCompletion(delayedEof));
    QVERIFY(!runtime.canonicalSource()->currentActivityCompletionFact(
        QStringLiteral("activity-delayed-eof")).has_value());
    QCOMPARE(runtime.deliveryStore()->operations().size(), 1);
    QVERIFY(runtime.canonicalSource()->isDurablyCurrent(
        runtime.deliveryStore()->operations().first().fact));

    int completionFacts = 0;
    for (const TrackerDeliveryFact &fact : runtime.canonicalSource()->currentCommittedFacts()) {
        if (fact.kind != TrackerDeliveryFactKind::Completion
            || fact.origin != TrackerDeliveryOrigin::NativeLocal)
            continue;
        ++completionFacts;
        QCOMPARE(fact.sourceEventId, historyFact.sourceEventId);
    }
    QCOMPARE(completionFacts, 1);
}

void TrackerDeliveryTest::liveRuntimeDeduplicatesGuardedNinetyAndEofButKeepsRewatch()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    ConsumptionHistoryBridge bridge(&activity, &progress, &history);
    TrackerDeliveryRuntime runtime(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(runtime.start(&error), qPrintable(error));

    const QString account = QStringLiteral("simkl-account");
    const QString remoteId = QStringLiteral("simkl-guarded-feature");
    const QString id = QStringLiteral("movie:guarded-feature");
    QVERIFY(installConnection(runtime.connectionStore(), TrackerProviderId::Simkl, account));

    const QVariantMap identity{
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("titleKey"), QStringLiteral("theatre:guarded-feature")},
        {QStringLiteral("itemKey"), id},
        {QStringLiteral("title"), QStringLiteral("Guarded feature")},
        {QStringLiteral("itemLabel"), QString()},
        {QStringLiteral("cover"), QString()},
        {QStringLiteral("syncable"), true},
        {QStringLiteral("source"), QStringLiteral("player")}};
    FakeTrackerClock clock;
    ActivityPlaybackTracker playback;
    playback.setSink(&activity);
    playback.setMonotonicClock(clock.monotonicFn());
    playback.setWallClock(clock.wallFn());
    playback.setUtcOffsetProvider(clock.offsetFn());

    const QString firstSession = activity.newSessionId();
    progress.record(progressEntry(QStringLiteral("video"), id, 0.35));
    playback.begin(identity, firstSession);
    playback.sample(35000, 100000, 1000, true);
    clock.advance(1000);

    // A seek to 95% does not create an Activity completion, but Progress still
    // records its native completion witness for this exact playback session.
    playback.discontinuity(95000, 100000, 1000);
    QVariantMap firstProgressOnly = progressEntry(QStringLiteral("video"), id, 0.95);
    firstProgressOnly.insert(QStringLiteral("_trackerActivitySessionId"), firstSession);
    progress.record(firstProgressOnly);
    QTRY_COMPARE_WITH_TIMEOUT(history.trackerLocalCompletionFacts().size(), 1, 3000);
    const QString firstEventId = history.trackerLocalCompletionFacts().first()
        .toMap().value(QStringLiteral("eventId")).toString();
    QVERIFY(!firstEventId.isEmpty());

    // Rewind and cross 90% again without an Activity completion. This second
    // private witness must not displace the first fact for this session.
    progress.record(progressEntry(QStringLiteral("video"), id, 0.35));
    QTest::qWait(5);
    QVariantMap secondProgressOnly = progressEntry(QStringLiteral("video"), id, 0.95);
    secondProgressOnly.insert(QStringLiteral("_trackerActivitySessionId"), firstSession);
    progress.record(secondProgressOnly);
    QTRY_COMPARE_WITH_TIMEOUT(history.trackerLocalCompletionFacts().size(), 2, 3000);
    const QVariantList progressWitnesses = history.trackerLocalCompletionFacts();
    const auto secondProgressWitness = std::find_if(
        progressWitnesses.cbegin(), progressWitnesses.cend(),
        [&firstEventId](const QVariant &value) {
            return value.toMap().value(QStringLiteral("eventId")).toString() != firstEventId;
        });
    QVERIFY(secondProgressWitness != progressWitnesses.cend());
    const QString secondProgressEventId = secondProgressWitness->toMap()
        .value(QStringLiteral("eventId")).toString();
    QVERIFY(secondProgressEventId != firstEventId);

    // A later seek/re-cross emits both Activity and Progress evidence. The
    // exact link joins those copies; it must not eclipse the earlier witness.
    progress.record(progressEntry(QStringLiteral("video"), id, 0.35));
    playback.discontinuity(35000, 100000, 1000);
    QTest::qWait(5);
    clock.advance(1000);
    playback.sample(89000, 100000, 1000, true);
    clock.advance(1000);
    playback.sample(95000, 100000, 1000, true);

    QList<QVariantMap> activityFacts = activity.historyProjectionFacts();
    const auto firstGuarded = std::find_if(activityFacts.cbegin(), activityFacts.cend(),
        [&firstSession](const QVariantMap &event) {
            return event.value(QStringLiteral("sessionId")).toString() == firstSession
                && event.value(QStringLiteral("type")).toString()
                    == QLatin1String("media_completed")
                && event.value(QStringLiteral("reason")).toString()
                    == QLatin1String("guarded_90_percent");
        });
    QVERIFY(firstGuarded != activityFacts.cend());
    const QString firstActivityEventId = firstGuarded->value(QStringLiteral("eventId")).toString();
    QVERIFY(!firstActivityEventId.isEmpty());
    QVERIFY(!runtime.canonicalSource()->currentActivityCompletionFact(
        firstActivityEventId).has_value());

    QVariantMap linkedProgressCompletion = progressEntry(QStringLiteral("video"), id, 0.95);
    linkedProgressCompletion.insert(QStringLiteral("_trackerActivityEventId"), firstActivityEventId);
    linkedProgressCompletion.insert(QStringLiteral("_trackerActivitySessionId"), firstSession);
    progress.record(linkedProgressCompletion);
    progress.flush();
    QTRY_VERIFY_WITH_TIMEOUT(progress.deliverySnapshotDurable(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(history.trackerLocalCompletionFacts().size(), 3, 3000);

    const QList<TrackerDeliveryFact> firstFacts =
        runtime.canonicalSource()->currentCommittedFacts();
    const auto firstCompletion = std::find_if(firstFacts.cbegin(), firstFacts.cend(),
        [](const TrackerDeliveryFact &fact) {
            return fact.kind == TrackerDeliveryFactKind::Completion
                && fact.origin == TrackerDeliveryOrigin::NativeLocal;
        });
    QVERIFY(firstCompletion != firstFacts.cend());
    QCOMPARE(firstCompletion->sourceEventId, firstEventId);
    QCOMPARE(std::count_if(firstFacts.cbegin(), firstFacts.cend(),
        [](const TrackerDeliveryFact &fact) {
            return fact.kind == TrackerDeliveryFactKind::Completion
                && fact.origin == TrackerDeliveryOrigin::NativeLocal;
        }), 1);

    QVERIFY(runtime.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, remoteId),
        {firstCompletion->canonicalMediaId, firstCompletion->historyKind,
         firstCompletion->historyId, QStringLiteral("Guarded feature")},
        TrackerMappingProvenance::UserConfirmed));
    const auto preview = runtime.deliveryStore()->createExportPreview(
        TrackerProviderId::Simkl, account, 1, firstFacts,
        remoteSnapshot(TrackerProviderId::Simkl, account, 1, QStringLiteral("first-export"),
                       {remoteAbsent(remoteId, TrackerDeliveryFactKind::Completion, firstEventId)}),
        runtime.canonicalSource(), &error);
    QVERIFY2(preview.has_value(), qPrintable(error));
    const auto selected = std::find_if(preview->items.cbegin(), preview->items.cend(),
        [&firstEventId](const TrackerExportPreviewItem &item) {
            return item.fact.sourceEventId == firstEventId && item.eligible;
        });
    QVERIFY(selected != preview->items.cend());
    QVERIFY2(runtime.deliveryStore()->confirmExport(
                 preview->previewId, {selected->itemId}, preview->remoteSnapshot,
                 runtime.canonicalSource(), 2000, &error), qPrintable(error));
    QCOMPARE(runtime.deliveryStore()->operations().size(), 1);

    clock.advance(1000);
    playback.naturalEof();
    playback.endSession();
    activityFacts = activity.historyProjectionFacts();
    int firstSessionCompletions = 0;
    QString eofEventId;
    for (const QVariantMap &event : activityFacts) {
        if (event.value(QStringLiteral("sessionId")).toString() != firstSession
            || event.value(QStringLiteral("type")).toString()
                != QLatin1String("media_completed"))
            continue;
        ++firstSessionCompletions;
        if (event.value(QStringLiteral("reason")).toString() == QLatin1String("eof"))
            eofEventId = event.value(QStringLiteral("eventId")).toString();
    }
    QCOMPARE(firstSessionCompletions, 2); // Native History keeps guarded-90 and EOF facts.
    QVERIFY(!eofEventId.isEmpty());
    QVERIFY(!runtime.canonicalSource()->currentActivityCompletionFact(eofEventId).has_value());
    QCOMPARE(runtime.deliveryStore()->operations().size(), 1);
    QVERIFY(runtime.canonicalSource()->isDurablyCurrent(
        runtime.deliveryStore()->operations().first().fact));

    // The same title in a separate playback session is a real rewatch and queues a new fact.
    progress.record(progressEntry(QStringLiteral("video"), id, 0.35));
    const QString rewatchSession = activity.newSessionId();
    playback.begin(identity, rewatchSession);
    playback.sample(35000, 100000, 1000, true);
    clock.advance(1000);
    playback.sample(89000, 100000, 1000, true);
    clock.advance(1000);
    playback.sample(95000, 100000, 1000, true);

    activityFacts = activity.historyProjectionFacts();
    const auto rewatchGuarded = std::find_if(activityFacts.cbegin(), activityFacts.cend(),
        [&rewatchSession](const QVariantMap &event) {
            return event.value(QStringLiteral("sessionId")).toString() == rewatchSession
                && event.value(QStringLiteral("type")).toString()
                    == QLatin1String("media_completed")
                && event.value(QStringLiteral("reason")).toString()
                    == QLatin1String("guarded_90_percent");
        });
    QVERIFY(rewatchGuarded != activityFacts.cend());
    const QString rewatchEventId = rewatchGuarded->value(QStringLiteral("eventId")).toString();
    QVERIFY(!rewatchEventId.isEmpty());
    QVERIFY(runtime.canonicalSource()->currentActivityCompletionFact(rewatchEventId).has_value());
    QCOMPARE(runtime.deliveryStore()->operations().size(), 2);
    const QList<TrackerDeliveryOperation> rewatchOperations =
        runtime.deliveryStore()->operations();
    const auto rewatchOperation = std::find_if(
        rewatchOperations.cbegin(), rewatchOperations.cend(),
        [&rewatchEventId](const TrackerDeliveryOperation &operation) {
            return operation.fact.sourceEventId == rewatchEventId;
        });
    QVERIFY(rewatchOperation != rewatchOperations.cend());
    QVERIFY(rewatchOperation->fact.sourceEventId != firstEventId);

    QVariantMap rewatchProgressCompletion = progressEntry(QStringLiteral("video"), id, 0.95);
    rewatchProgressCompletion.insert(QStringLiteral("_trackerActivityEventId"), rewatchEventId);
    rewatchProgressCompletion.insert(QStringLiteral("_trackerActivitySessionId"), rewatchSession);
    progress.record(rewatchProgressCompletion);
    progress.flush();
    QTRY_VERIFY_WITH_TIMEOUT(progress.deliverySnapshotDurable(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(history.trackerLocalCompletionFacts().size(), 4, 3000);
    QCOMPARE(runtime.deliveryStore()->operations().size(), 2);

    clock.advance(1000);
    playback.naturalEof();
    playback.endSession();
    activityFacts = activity.historyProjectionFacts();
    const auto rewatchEof = std::find_if(activityFacts.cbegin(), activityFacts.cend(),
        [&rewatchSession](const QVariantMap &event) {
            return event.value(QStringLiteral("sessionId")).toString() == rewatchSession
                && event.value(QStringLiteral("type")).toString()
                    == QLatin1String("media_completed")
                && event.value(QStringLiteral("reason")).toString() == QLatin1String("eof");
        });
    QVERIFY(rewatchEof != activityFacts.cend());
    QVERIFY(!runtime.canonicalSource()->currentActivityCompletionFact(
        rewatchEof->value(QStringLiteral("eventId")).toString()).has_value());
    QCOMPARE(runtime.deliveryStore()->operations().size(), 2);
    QVERIFY(runtime.canonicalSource()->isDurablyCurrent(rewatchOperation->fact));

    int nativeCompletionFacts = 0;
    for (const TrackerDeliveryFact &fact : runtime.canonicalSource()->currentCommittedFacts()) {
        if (fact.kind != TrackerDeliveryFactKind::Completion
            || fact.origin != TrackerDeliveryOrigin::NativeLocal)
            continue;
        ++nativeCompletionFacts;
    }
    QCOMPARE(nativeCompletionFacts, 2);
}

void TrackerDeliveryTest::movieCompletionCorrelationSurvivesProgressEpisodeHeuristic()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    ProgressStore progress(root.filePath(QStringLiteral("progress.ini")));
    ActivityStore activity;
    HistoryStore history(root.filePath(QStringLiteral("history.ini")));
    ConsumptionHistoryBridge bridge(&activity, &progress, &history);

    // ProgressStore's legacy colon-count heuristic calls this a video episode;
    // Activity's numeric season/episode identity correctly calls it a movie.
    const QString id = QStringLiteral("catalog:feature-cut:extended");
    progress.record(progressEntry(QStringLiteral("video"), id, 0.35));
    QVERIFY(activity.recordCompletion(activityCompletion(
        QStringLiteral("activity-movie"), QStringLiteral("movie"), id, 20000)));
    QVariantMap crossing = progressEntry(QStringLiteral("video"), id, 0.90);
    crossing.insert(QStringLiteral("_trackerActivityEventId"), QStringLiteral("activity-movie"));
    progress.record(crossing);
    progress.flush();
    QTRY_VERIFY_WITH_TIMEOUT(progress.deliveryEntryDurable(QStringLiteral("video"), id), 3000);

    const QVariantList witnesses = history.trackerLocalCompletionFacts();
    QCOMPARE(witnesses.size(), 1);
    QCOMPARE(witnesses.first().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("episode"));
    QCOMPARE(witnesses.first().toMap().value(QStringLiteral("activityEventId")).toString(),
             QStringLiteral("activity-movie"));

    TrackerCanonicalDeliverySource source(&progress, &activity, &history);
    const QList<TrackerDeliveryFact> facts = source.currentCommittedFacts();
    QCOMPARE(facts.size(), 2); // progress plus one completion; no duplicate History completion
    int progressFacts = 0;
    int completionFacts = 0;
    for (const TrackerDeliveryFact &fact : facts) {
        QVERIFY2(source.isDurablyCurrent(fact),
                 qPrintable(QStringLiteral("Stale fact: %1 / %2 / %3")
                     .arg(fact.historyKind, fact.historyId, fact.sourceEventId)));
        if (fact.kind == TrackerDeliveryFactKind::Progress)
            ++progressFacts;
        else {
            ++completionFacts;
            QCOMPARE(fact.sourceEventId, QStringLiteral("activity-movie"));
        }
    }
    QCOMPARE(progressFacts, 1);
    QCOMPARE(completionFacts, 1);
}

void TrackerDeliveryTest::episodeCompletionCorrelationSuppressesOnlyItsHistoryCopy()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    ProgressStore progress(root.filePath(QStringLiteral("progress.ini")));
    ActivityStore activity;
    HistoryStore history(root.filePath(QStringLiteral("history.ini")));
    ConsumptionHistoryBridge bridge(&activity, &progress, &history);

    const QString id = QStringLiteral("tt7654321:2:5");
    progress.record(progressEntry(QStringLiteral("video"), id, 0.35));
    QVERIFY(activity.recordCompletion(activityCompletion(
        QStringLiteral("activity-episode"), QStringLiteral("episode"), id, 20000)));
    QVariantMap crossing = progressEntry(QStringLiteral("video"), id, 0.90);
    crossing.insert(QStringLiteral("_trackerActivityEventId"), QStringLiteral("activity-episode"));
    progress.record(crossing);
    progress.flush();
    QTRY_VERIFY_WITH_TIMEOUT(progress.deliveryEntryDurable(QStringLiteral("video"), id), 3000);

    const QVariantList witnesses = history.trackerLocalCompletionFacts();
    QCOMPARE(witnesses.size(), 1);
    QCOMPARE(witnesses.first().toMap().value(QStringLiteral("activityEventId")).toString(),
             QStringLiteral("activity-episode"));

    TrackerCanonicalDeliverySource source(&progress, &activity, &history);
    const QList<TrackerDeliveryFact> facts = source.currentCommittedFacts();
    QCOMPARE(facts.size(), 2); // progress plus one completion; no duplicate History completion
    int progressFacts = 0;
    int completionFacts = 0;
    for (const TrackerDeliveryFact &fact : facts) {
        QVERIFY2(source.isDurablyCurrent(fact),
                 qPrintable(QStringLiteral("Stale fact: %1 / %2 / %3")
                     .arg(fact.historyKind, fact.historyId, fact.sourceEventId)));
        if (fact.kind == TrackerDeliveryFactKind::Progress)
            ++progressFacts;
        else {
            ++completionFacts;
            QCOMPARE(fact.sourceEventId, QStringLiteral("activity-episode"));
        }
    }
    QCOMPARE(progressFacts, 1);
    QCOMPARE(completionFacts, 1);
}

void TrackerDeliveryTest::reEnablingSendRefreshesProgressCommittedWhilePaused()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    const QString progressId = QStringLiteral("paused-save:1:1");
    progress.record(progressEntry(QStringLiteral("video"), progressId, 0.35));
    progress.flush();
    QTRY_VERIFY_WITH_TIMEOUT(progress.deliverySnapshotDurable(), 3000);

    const QString account = QStringLiteral("simkl-paused-send-account");
    const QString remoteId = QStringLiteral("simkl-paused-send-show");
    const QString canonical = QStringLiteral("video:") + progressId;
    TrackerDeliveryRuntime runtime(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(runtime.start(&error), qPrintable(error));
    QVERIFY(installConnection(runtime.connectionStore(), TrackerProviderId::Simkl, account));
    QVERIFY(runtime.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, remoteId),
        {canonical, QStringLiteral("video"), progressId, QStringLiteral("Paused-save show")},
        TrackerMappingProvenance::UserConfirmed));

    const auto preview = runtime.deliveryStore()->createExportPreview(
        TrackerProviderId::Simkl, account, 1,
        runtime.canonicalSource()->currentCommittedFacts(),
        remoteSnapshot(TrackerProviderId::Simkl, account, 1, QStringLiteral("paused-first-export"),
                       {remoteAbsent(remoteId)}),
        runtime.canonicalSource(), &error);
    QVERIFY2(preview.has_value(), qPrintable(error));
    QCOMPARE(preview->items.size(), 1);
    QVERIFY(preview->items.first().eligible);
    QVERIFY2(runtime.deliveryStore()->confirmExport(
        preview->previewId, {preview->items.first().itemId}, preview->remoteSnapshot,
        runtime.canonicalSource(), 2000, &error), qPrintable(error));
    QCOMPARE(runtime.deliveryStore()->operations().size(), 1);
    QCOMPARE(runtime.deliveryStore()->operations().first().fact.progress, 35);

    QVERIFY(runtime.deliveryStore()->setProviderSendEnabled(
        TrackerProviderId::Simkl, account, false, &error));
    progress.record(progressEntry(QStringLiteral("video"), progressId, 0.65));
    progress.flush();
    QTRY_VERIFY_WITH_TIMEOUT(progress.deliverySnapshotDurable(), 3000);
    QCOMPARE(runtime.deliveryStore()->operations().size(), 1);
    QCOMPARE(runtime.deliveryStore()->operations().first().fact.progress, 35);

    QVERIFY2(runtime.deliveryStore()->setProviderSendEnabled(
        TrackerProviderId::Simkl, account, true, &error), qPrintable(error));
    QVERIFY2(runtime.refreshCurrentFacts(&error), qPrintable(error));
    const QList<TrackerDeliveryOperation> refreshed = runtime.deliveryStore()->operations();
    QCOMPARE(refreshed.size(), 1);
    QCOMPARE(refreshed.first().fact.progress, 65);
    QCOMPARE(refreshed.first().state, TrackerDeliveryState::Pending);
}

void TrackerDeliveryTest::profileRuntimeRecoversDurableProgressAfterRestart()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    const QString progressId = QStringLiteral("show:1:1");
    progress.record(progressEntry(QStringLiteral("video"), progressId, 0.35));
    progress.flush();
    QTRY_VERIFY(progress.deliverySnapshotDurable());

    const QString account = QStringLiteral("simkl-account");
    const QString remoteId = QStringLiteral("simkl-show");
    const QString canonical = QStringLiteral("video:") + progressId;
    {
        TrackerDeliveryRuntime runtime(*profile, &progress, &activity, &history);
        QString error;
        QVERIFY2(runtime.start(&error), qPrintable(error));
        QVERIFY(installConnection(runtime.connectionStore(), TrackerProviderId::Simkl, account));
        QVERIFY(runtime.mappingStore()->upsert(
            remote(TrackerProviderId::Simkl, account, remoteId),
            {canonical, QStringLiteral("video"), progressId, QStringLiteral("Test show")},
            TrackerMappingProvenance::UserConfirmed));

        const auto preview = runtime.deliveryStore()->createExportPreview(
            TrackerProviderId::Simkl, account, 1,
            runtime.canonicalSource()->currentCommittedFacts(),
            remoteSnapshot(TrackerProviderId::Simkl, account, 1, QStringLiteral("remote-1"),
                           {remoteAbsent(remoteId)}),
            runtime.canonicalSource(), &error);
        QVERIFY2(preview.has_value(), qPrintable(error));
        QVERIFY2(runtime.deliveryStore()->confirmExport(
                     preview->previewId, {preview->items.first().itemId},
                     preview->remoteSnapshot, runtime.canonicalSource(), 2000, &error),
                 qPrintable(error));
        QCOMPARE(runtime.deliveryStore()->operations().size(), 1);
    }

    // Simulate the durable owner commit winning the crash race before the
    // outbox callback: no runtime is listening while the local row advances.
    progress.record(progressEntry(QStringLiteral("video"), progressId, 0.65));
    progress.flush();
    QTRY_VERIFY(progress.deliverySnapshotDurable());

    TrackerDeliveryRuntime recovered(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(recovered.start(&error), qPrintable(error));
    QVERIFY2(recovered.start(&error), qPrintable(error));
    const QList<TrackerDeliveryOperation> operations = recovered.deliveryStore()->operations();
    QCOMPARE(operations.size(), 1);
    QCOMPARE(operations.first().fact.progress, 65);
    QCOMPARE(operations.first().state, TrackerDeliveryState::Pending);
    QCOMPARE(operations.first().attemptCount, 0);
}

void TrackerDeliveryTest::playbackLifecycleReportsRealTransitionsButNeverSamplingTicks()
{
    ActivityPlaybackTracker tracker;
    QSignalSpy lifecycle(&tracker, &ActivityPlaybackTracker::playbackLifecycleChanged);
    QVERIFY(lifecycle.isValid());

    tracker.begin({{QStringLiteral("world"), QStringLiteral("theatre")},
                   {QStringLiteral("kind"), QStringLiteral("movie")},
                   {QStringLiteral("itemKey"), QStringLiteral("movie-42")}},
                  QStringLiteral("playback-session-1"));
    tracker.playbackStateChanged(true, 0, 100000);
    QCOMPARE(lifecycle.size(), 1);
    QCOMPARE(lifecycle.at(0).at(0).toMap().value(QStringLiteral("action")).toString(),
             QStringLiteral("start"));

    tracker.setMonotonicClock([now = qint64(1000)]() mutable { return now += 5000; });
    tracker.setWallClock([now = qint64(100000)]() mutable { return now += 5000; });
    tracker.setUtcOffsetProvider([] { return 0; });
    tracker.sample(5000, 100000, 1000, true);
    tracker.sample(10000, 100000, 1000, true);
    tracker.discontinuity(50000, 100000, 1000); // a seek is not a scrobble action
    QCOMPARE(lifecycle.size(), 1);

    tracker.playbackStateChanged(false, 50000, 100000);
    tracker.playbackStateChanged(true, 50000, 100000);
    tracker.endSession(60000, 100000);
    QCOMPARE(lifecycle.size(), 4);
    QCOMPARE(lifecycle.at(1).at(0).toMap().value(QStringLiteral("action")).toString(),
             QStringLiteral("pause"));
    QCOMPARE(lifecycle.at(2).at(0).toMap().value(QStringLiteral("action")).toString(),
             QStringLiteral("resume"));
    QCOMPARE(lifecycle.at(3).at(0).toMap().value(QStringLiteral("action")).toString(),
             QStringLiteral("close"));
    QCOMPARE(lifecycle.at(0).at(0).toMap().value(QStringLiteral("playbackGeneration")).toULongLong(),
             lifecycle.at(3).at(0).toMap().value(QStringLiteral("playbackGeneration")).toULongLong());
}

void TrackerDeliveryTest::closedPlaybackGenerationCannotReplayAsNewStart()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));

    FakeSimklScrobbleTransport transport;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 generation, quint64 sequence,
                     const QString &sessionId) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), sessionId},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), generation},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), 1000},
                           {QStringLiteral("durationMs"), 100000}};
    };

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 40, 1,
                                            QStringLiteral("playback-40")));
    QCOMPARE(transport.requests.size(), 1);
    scrobble.observePlaybackLifecycle(event(QStringLiteral("close"), 40, 2,
                                            QStringLiteral("playback-40")));
    QCOMPARE(transport.requests.size(), 2);
    QCOMPARE(scrobble.store()->intents().size(), 2);
    QVERIFY(!scrobble.store()->hasOpenPlayback(
        TrackerProviderId::Simkl, account, QStringLiteral("playback-40")));

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 39, 1,
                                            QStringLiteral("stale-playback")));
    QCOMPARE(scrobble.store()->intents().size(), 2);
    QCOMPARE(transport.requests.size(), 2);
    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 40, 1,
                                            QStringLiteral("replayed-playback")));
    QCOMPARE(scrobble.store()->intents().size(), 2);
    QCOMPARE(transport.requests.size(), 2);
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::scrobblingLeavesNativeHistoryAndStatisticsDigestUnchanged()
{
    struct NativeDigests {
        QByteArray activity;
        QByteArray history;
        QByteArray statistics;
        int providerRequests = 0;
        bool valid = false;
        QString error;
    };
    const auto digestRows = [](const QList<QVariantMap> &rows) {
        QStringList normalized;
        normalized.reserve(rows.size());
        for (QVariantMap row : rows) {
            row.remove(QStringLiteral("eventId"));
            normalized.append(QString::fromUtf8(
                QJsonDocument(QJsonObject::fromVariantMap(row)).toJson(QJsonDocument::Compact)));
        }
        std::sort(normalized.begin(), normalized.end());
        return QCryptographicHash::hash(normalized.join(QChar('\n')).toUtf8(),
                                        QCryptographicHash::Sha256).toHex();
    };
    const auto runPlayback = [&](const QString &rootPath, bool scrobblingEnabled) {
        NativeDigests result;
        if (!QDir().mkpath(rootPath)) {
            result.error = QStringLiteral("The playback fixture root could not be created.");
            return result;
        }
        const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), rootPath);
        if (!profile) {
            result.error = QStringLiteral("The playback fixture profile could not be created.");
            return result;
        }

        ProgressStore progress(profile->progressIniPath());
        ActivityStore activity(profile->activityDbPath());
        HistoryStore history(profile->historyIniPath());
        if (!progress.healthy() || !activity.healthy() || !history.healthy()) {
            result.error = QStringLiteral("A native playback owner is unavailable.");
            return result;
        }
        ConsumptionHistoryBridge historyBridge(&activity, &progress, &history);
        if (!historyBridge.replayExisting(&result.error))
            return result;

        FakeSimklScrobbleTransport transport;
        std::unique_ptr<TrackerDeliveryRuntime> trackerDelivery;
        std::unique_ptr<TrackerScrobbleRuntime> scrobble;
        if (scrobblingEnabled) {
            trackerDelivery = std::make_unique<TrackerDeliveryRuntime>(
                *profile, &progress, &activity, &history);
            if (!trackerDelivery->start(&result.error))
                return result;
            const QString account = QStringLiteral("simkl-account");
            if (!trackerDelivery->connectionStore()->upsert(
                    {TrackerProviderId::Simkl, account, 4, 2000,
                     TrackerProviderCapability::Scrobble,
                     TrackerConnectionState::Connected}, &result.error)
                || !trackerDelivery->mappingStore()->upsert(
                    remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
                    {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
                     QStringLiteral("movie-42"), QStringLiteral("Example movie")},
                    TrackerMappingProvenance::UserConfirmed, &result.error)) {
                return result;
            }
            scrobble = std::make_unique<TrackerScrobbleRuntime>(
                *profile, trackerDelivery->connectionStore(),
                trackerDelivery->mappingStore(), &transport);
            if (!scrobble->start(&result.error)
                || !scrobble->setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true)) {
                result.error = QStringLiteral("SIMKL scrobbling could not be enabled for the fixture.");
                return result;
            }
        }

        ActivityPlaybackTracker playback;
        playback.setSink(&activity);
        if (scrobble) {
            playback.setLifecycleScopeGeneration(scrobble->playbackScopeGeneration());
            QObject::connect(&playback, &ActivityPlaybackTracker::playbackLifecycleChanged,
                             scrobble.get(), &TrackerScrobbleRuntime::observePlaybackLifecycle);
        }
        qint64 monotonicMs = 0;
        qint64 wallMs = QDateTime(QDate(2026, 1, 15), QTime(10, 0), Qt::UTC).toMSecsSinceEpoch();
        playback.setMonotonicClock([&monotonicMs] { return monotonicMs; });
        playback.setWallClock([&wallMs] { return wallMs; });
        playback.setUtcOffsetProvider([] { return 0; });
        const QVariantMap identity{{QStringLiteral("world"), QStringLiteral("theatre")},
                                  {QStringLiteral("kind"), QStringLiteral("movie")},
                                  {QStringLiteral("titleKey"), QStringLiteral("theatre:movie-42")},
                                  {QStringLiteral("itemKey"), QStringLiteral("movie-42")},
                                  {QStringLiteral("title"), QStringLiteral("Example movie")},
                                  {QStringLiteral("utcOffsetMinutes"), 0},
                                  {QStringLiteral("syncable"), true},
                                  {QStringLiteral("source"), QStringLiteral("player1")}};
        constexpr qint64 durationMs = 100000;
        playback.begin(identity, QStringLiteral("shared-native-session"));
        playback.playbackStateChanged(true, 0, durationMs);
        playback.sample(0, durationMs, 1000, true);
        for (qint64 positionMs : {qint64(5000), qint64(10000), qint64(15000)}) {
            monotonicMs += 5000;
            wallMs += 5000;
            playback.sample(positionMs, durationMs, 1000, true);
        }
        playback.naturalEof(15000, durationMs);
        playback.endSession(15000, durationMs);

        QVariantList historyValues = history.records();
        QList<QVariantMap> historyRows;
        historyRows.reserve(historyValues.size());
        for (const QVariant &value : historyValues)
            historyRows.append(value.toMap());
        result.activity = digestRows(activity.historyProjectionFacts());
        result.history = digestRows(historyRows);
        const QVariantMap month = activity.projectMonth(QStringLiteral("2026-01"));
        result.statistics = QCryptographicHash::hash(
            QJsonDocument(QJsonObject::fromVariantMap(month)).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256).toHex();
        result.providerRequests = transport.requests.size();
        result.valid = true;
        return result;
    };

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const NativeDigests localOnly = runPlayback(root.filePath(QStringLiteral("local-only")), false);
    QVERIFY2(localOnly.valid, qPrintable(localOnly.error));
    QCOMPARE(localOnly.providerRequests, 0);
    const NativeDigests withScrobbling = runPlayback(root.filePath(QStringLiteral("with-scrobbling")), true);
    QVERIFY2(withScrobbling.valid, qPrintable(withScrobbling.error));
    QVERIFY(withScrobbling.providerRequests >= 2);
    QCOMPARE(withScrobbling.activity, localOnly.activity);
    QCOMPARE(withScrobbling.history, localOnly.history);
    QCOMPARE(withScrobbling.statistics, localOnly.statistics);
}

void TrackerDeliveryTest::scrobbleConsentAndUnknownOutcomeSurviveProfileReopen()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    const QString account = QStringLiteral("simkl-account");

    {
        TrackerScrobbleStore store(*profile);
        QString error;
        QVERIFY2(store.healthy(&error), qPrintable(error));
        QVERIFY(!store.enabled(TrackerProviderId::Simkl, account)); // explicit opt-in only
        QVERIFY2(store.setEnabled(TrackerProviderId::Simkl, account, true, &error),
                 qPrintable(error));

        TrackerScrobbleIntent intent;
        intent.operationId = QStringLiteral("scrobble-op-1");
        intent.providerId = TrackerProviderId::Simkl;
        intent.remoteAccountId = account;
        intent.connectionGeneration = 7;
        intent.mappingRevision = 3;
        intent.canonicalMediaId = QStringLiteral("movie:movie-42");
        intent.remoteMediaId = QStringLiteral("752138");
        intent.playbackSessionId = QStringLiteral("playback-session-1");
        intent.playbackGeneration = 3;
        intent.transitionSequence = 1;
        intent.action = TrackerScrobbleAction::Start;
        intent.progressHundredths = 125;
        intent.createdAtMs = 5000;
        QVERIFY2(store.recordIntent(intent, &error), qPrintable(error));
        QVERIFY2(store.recordIntent(intent, &error), qPrintable(error)); // stable replay is inert
        QCOMPARE(store.intents().size(), 1);
        QVERIFY2(store.markDelivering(intent.operationId, &error), qPrintable(error));
        QCOMPARE(store.intents().first().state, TrackerScrobbleState::Delivering);
    }

    TrackerScrobbleStore reopened(*profile);
    QString error;
    QVERIFY2(reopened.healthy(&error), qPrintable(error));
    QVERIFY(reopened.enabled(TrackerProviderId::Simkl, account));
    QCOMPARE(reopened.intents().size(), 1);
    QCOMPARE(reopened.intents().first().state, TrackerScrobbleState::UnknownOutcome);
    QCOMPARE(reopened.intents().first().attemptCount, 1);
    QVERIFY(reopened.hasUnknownOutcome(TrackerProviderId::Simkl, account));
    QVERIFY2(reopened.reconcileUnknown(QStringLiteral("scrobble-op-1"),
                                       TrackerScrobbleReadback::ExactPresent, &error),
             qPrintable(error));
    QCOMPARE(reopened.intents().first().state, TrackerScrobbleState::Succeeded);

    TrackerScrobbleStore recovered(*profile);
    QVERIFY2(recovered.healthy(&error), qPrintable(error));
    QVERIFY(!recovered.hasUnknownOutcome(TrackerProviderId::Simkl, account));
    QCOMPARE(recovered.intents().first().state, TrackerScrobbleState::Succeeded);
}

void TrackerDeliveryTest::disconnectDiscardNeverDeletesUnknownOutcomes()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    TrackerConnectionStore connections(*profile);
    TrackerMappingStore mappings(*profile);
    QVERIFY(installConnection(&connections, TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-a"), QStringLiteral("canonical-a")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-b"), QStringLiteral("canonical-b")));
    QVERIFY(installMapping(&mappings, TrackerProviderId::Simkl, QStringLiteral("42"),
                           QStringLiteral("remote-c"), QStringLiteral("canonical-c")));
    FakeDeliverySource source;
    const QList<TrackerDeliveryFact> facts{
        progressFact(QStringLiteral("canonical-a"), 1, 1, QStringLiteral("a")),
        progressFact(QStringLiteral("canonical-b"), 1, 2, QStringLiteral("b")),
        progressFact(QStringLiteral("canonical-c"), 1, 3, QStringLiteral("c"))};
    for (const TrackerDeliveryFact &fact : facts)
        source.commit(fact);
    const TrackerRemoteDeliverySnapshot remote = remoteSnapshot(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, QStringLiteral("disconnect-review"),
        {remoteAbsent(QStringLiteral("remote-a")), remoteAbsent(QStringLiteral("remote-b")),
         remoteAbsent(QStringLiteral("remote-c"))});

    TrackerDeliveryStore delivery(*profile, &mappings, &connections);
    const auto preview = delivery.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("42"), 1, facts, remote, &source);
    QVERIFY(preview.has_value());
    QStringList selected;
    for (const TrackerExportPreviewItem &item : preview->items)
        selected.append(item.itemId);
    QVERIFY(delivery.confirmExport(preview->previewId, selected, remote, &source, 2000));
    QCOMPARE(delivery.operations().size(), 3);
    const QString uncertainDeliveryId = delivery.operations().first().operationId;
    QVERIFY(delivery.markDelivering(uncertainDeliveryId, &source, 3000));
    QVERIFY(delivery.recordAttemptResult(
        uncertainDeliveryId, TrackerDeliveryAttemptResult::UnknownOutcome, 3001));
    const QString exhaustedDeliveryId = delivery.operations().last().operationId;
    qint64 retryAt = 100000;
    while (delivery.operation(exhaustedDeliveryId)->state != TrackerDeliveryState::NeedsAttention) {
        QVERIFY(delivery.markDelivering(exhaustedDeliveryId, &source, retryAt));
        QVERIFY(delivery.recordAttemptResult(
            exhaustedDeliveryId, TrackerDeliveryAttemptResult::RetryableKnownNotApplied, retryAt));
        retryAt += 100000;
    }
    QCOMPARE(delivery.operation(exhaustedDeliveryId)->reason,
             TrackerDeliveryReason::RetryLimitReached);
    QCOMPARE(delivery.discardKnownUnsent(TrackerProviderId::Simkl, QStringLiteral("42")), 2);
    QCOMPARE(delivery.operations().size(), 1);
    QCOMPARE(delivery.operations().first().state, TrackerDeliveryState::UnknownOutcome);
    TrackerDeliveryStore reopenedDelivery(*profile, &mappings, &connections);
    QCOMPARE(reopenedDelivery.operations().size(), 1);
    QCOMPARE(reopenedDelivery.operations().first().operationId, uncertainDeliveryId);

    const QString account = QStringLiteral("simkl-account");
    TrackerScrobbleStore scrobble(*profile);
    QVERIFY(scrobble.setEnabled(TrackerProviderId::Simkl, account, true));
    TrackerScrobbleIntent pending;
    pending.providerId = TrackerProviderId::Simkl;
    pending.remoteAccountId = account;
    pending.connectionGeneration = 1;
    pending.mappingRevision = 1;
    pending.remoteMediaId = QStringLiteral("752138");
    pending.playbackGeneration = 1;
    pending.transitionSequence = 1;
    pending.action = TrackerScrobbleAction::Start;
    pending.createdAtMs = 5000;
    pending.operationId = QStringLiteral("discarded-start");
    pending.canonicalMediaId = QStringLiteral("movie:discarded");
    pending.playbackSessionId = QStringLiteral("session-discarded");
    QVERIFY(scrobble.recordIntent(pending));
    QVERIFY(scrobble.markDelivering(pending.operationId));
    QVERIFY(scrobble.recordAttemptResult(
        pending.operationId, TrackerScrobbleAttemptResult::KnownNotApplied));
    QVERIFY(scrobble.retryWaitingOnNextEvent(pending.operationId));
    QVERIFY(scrobble.markDelivering(pending.operationId));
    QVERIFY(scrobble.recordAttemptResult(
        pending.operationId, TrackerScrobbleAttemptResult::KnownNotApplied));
    QCOMPARE(scrobble.intent(pending.operationId)->reason,
             TrackerScrobbleReason::RetryLimitReached);
    TrackerScrobbleIntent uncertain = pending;
    uncertain.operationId = QStringLiteral("unknown-start");
    uncertain.canonicalMediaId = QStringLiteral("movie:unknown");
    uncertain.playbackSessionId = QStringLiteral("session-unknown");
    uncertain.transitionSequence = 2;
    QVERIFY(scrobble.recordIntent(uncertain));
    QVERIFY(scrobble.markDelivering(uncertain.operationId));
    QVERIFY(scrobble.recordAttemptResult(
        uncertain.operationId, TrackerScrobbleAttemptResult::UnknownOutcome));
    QCOMPARE(scrobble.discardKnownUnsent(TrackerProviderId::Simkl, account), 1);

    TrackerScrobbleStore reopenedScrobble(*profile);
    QCOMPARE(reopenedScrobble.intents().size(), 2);
    QCOMPARE(reopenedScrobble.intent(pending.operationId)->state,
             TrackerScrobbleState::Superseded);
    QCOMPARE(reopenedScrobble.intent(pending.operationId)->reason,
             TrackerScrobbleReason::UserDiscarded);
    QCOMPARE(reopenedScrobble.intent(uncertain.operationId)->state,
             TrackerScrobbleState::UnknownOutcome);
}

void TrackerDeliveryTest::scrobbleIntentsRequireConsentExactMovieMappingAndCurrentPlayback()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));

    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::ReadHistory | TrackerProviderCapability::ReadProgress
             | TrackerProviderCapability::WriteProgress | TrackerProviderCapability::WriteCompletion
             | TrackerProviderCapability::Scrobble,
         TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));

    FakeSimklScrobbleTransport transport;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QSignalSpy ready(&scrobble, &TrackerScrobbleRuntime::intentReadyForDispatch);
    QVERIFY(ready.isValid());

    QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                         {QStringLiteral("world"), QStringLiteral("theatre")},
                         {QStringLiteral("kind"), QStringLiteral("movie")},
                         {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 generation, quint64 sequence,
                     qint64 position, bool completed = false) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), generation},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000},
                           {QStringLiteral("completedLocally"), completed}};
    };

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 40, 1, 1250));
    QVERIFY(scrobble.store()->intents().isEmpty()); // no opt-in, no work
    QCOMPARE(ready.size(), 0);
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    QVERIFY(scrobble.livePlaybackTrackingEnabled(QStringLiteral("simkl")));

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 40, 1, 1250));
    QCOMPARE(scrobble.store()->intents().size(), 1);
    QCOMPARE(ready.size(), 1);
    QCOMPARE(scrobble.store()->intents().first().action, TrackerScrobbleAction::Start);
    QCOMPARE(scrobble.store()->intents().first().progressHundredths, 125);
    QCOMPARE(scrobble.store()->intents().first().state, TrackerScrobbleState::Succeeded);
    QCOMPARE(transport.requests.size(), 1);

    QVariantMap competingIdentity = identity;
    competingIdentity.insert(QStringLiteral("source"), QStringLiteral("player2"));
    QVariantMap competingStart = event(QStringLiteral("start"), 41, 1, 0);
    competingStart.insert(QStringLiteral("identity"), competingIdentity);
    competingStart.insert(QStringLiteral("sessionId"), QStringLiteral("playback-2"));
    scrobble.observePlaybackLifecycle(competingStart);
    QCOMPARE(scrobble.store()->intents().size(), 1); // a second source cannot replace an open session

    scrobble.observePlaybackLifecycle(event(QStringLiteral("pause"), 40, 2, 95000));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("close"), 40, 3, 95000));
    QCOMPARE(scrobble.store()->intents().size(), 2);
    QCOMPARE(scrobble.store()->intents().at(1).action, TrackerScrobbleAction::Pause);
    QCOMPARE(scrobble.store()->intents().at(1).progressHundredths, 9500);
    QVERIFY(scrobble.store()->intents().at(1).closesSession);
    QCOMPARE(transport.requests.size(), 2);

    identity.insert(QStringLiteral("kind"), QStringLiteral("episode"));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 41, 1, 0));
    QCOMPARE(scrobble.store()->intents().size(), 2); // existing mapping lacks show+episode shape

    identity.insert(QStringLiteral("kind"), QStringLiteral("movie"));
    QVariantMap staleScope = event(QStringLiteral("start"), 42, 1, 0);
    staleScope.insert(QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration() + 1);
    scrobble.observePlaybackLifecycle(staleScope);
    QCOMPARE(scrobble.store()->intents().size(), 2);

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 43, 1, 0));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("close"), 43, 2, 85000, true));
    QCOMPARE(scrobble.store()->intents().size(), 4);
    QCOMPARE(scrobble.store()->intents().last().action, TrackerScrobbleAction::Pause);
    QCOMPARE(scrobble.store()->intents().last().progressHundredths, 8500);

    QCOMPARE(activity.historyProjectionFacts().size(), 0); // provider intents never become local history
}

void TrackerDeliveryTest::scrobbleDispatchIsSingleFlightAndUnknownRequiresReadback()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble | TrackerProviderCapability::WriteProgress,
         TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    FakeSimklScrobbleTransport transport;
    transport.holdSends = true;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));

    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 generation, quint64 sequence,
                     qint64 position, bool completed = false) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), generation},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000},
                           {QStringLiteral("completedLocally"), completed}};
    };

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 40, 1, 1250));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("pause"), 40, 2, 50000));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("close"), 40, 3, 50000));
    QCOMPARE(transport.requests.size(), 1); // pause waits behind the unacknowledged start
    QCOMPARE(scrobble.store()->intents().at(1).state, TrackerScrobbleState::Pending);
    transport.completeNextSend(SimklScrobbleSendResult::Succeeded);
    QCOMPARE(transport.requests.size(), 2);
    transport.readbackResults.append(SimklScrobbleReadbackResult::Indeterminate);
    transport.completeNextSend(SimklScrobbleSendResult::UnknownOutcome);
    QCOMPARE(transport.readbackRequests.size(), 1);
    QCOMPARE(scrobble.store()->intents().at(1).state, TrackerScrobbleState::NeedsAttention);
    QVERIFY(scrobble.store()->hasBlockingOutcome(TrackerProviderId::Simkl, account));
    QVariantMap newSession = event(QStringLiteral("start"), 41, 1, 0);
    newSession.insert(QStringLiteral("sessionId"), QStringLiteral("playback-2"));
    scrobble.observePlaybackLifecycle(newSession);
    QCOMPARE(scrobble.store()->intents().size(), 3); // saved behind unresolved close, never sent past it
    QCOMPARE(transport.requests.size(), 2);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Pending);
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::globalPausePreservesQueueAndResumeReadsUnknownBeforeSendingPending()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    const TrackerProviderCapabilities capabilities = TrackerProviderCapability::Scrobble
        | TrackerProviderCapability::WriteProgress;
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000, capabilities,
         TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));

    TrackerSyncSettingsStore settings(*profile);
    QVERIFY(settings.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), false));
    FakeSimklScrobbleTransport transport;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    scrobble.setSyncSettingsStore(&settings);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.store()->setEnabled(TrackerProviderId::Simkl, account, true));

    TrackerScrobbleIntent unknown;
    unknown.operationId = QStringLiteral("paused-unknown");
    unknown.providerId = TrackerProviderId::Simkl;
    unknown.remoteAccountId = account;
    unknown.connectionGeneration = 4;
    unknown.mappingRevision = 1;
    unknown.canonicalMediaId = QStringLiteral("movie:movie-42");
    unknown.remoteMediaId = QStringLiteral("752138");
    unknown.playbackSessionId = QStringLiteral("playback-unknown");
    unknown.playbackGeneration = 1;
    unknown.transitionSequence = 1;
    unknown.createdAtMs = 5000;
    QVERIFY(scrobble.store()->recordIntent(unknown));
    QVERIFY(scrobble.store()->markDelivering(unknown.operationId));
    QVERIFY(scrobble.store()->recordAttemptResult(
        unknown.operationId, TrackerScrobbleAttemptResult::UnknownOutcome,
        TrackerScrobbleReason::AcknowledgementLost));

    TrackerScrobbleIntent pending = unknown;
    pending.operationId = QStringLiteral("paused-pending");
    pending.remoteMediaId = QStringLiteral("752138");
    pending.playbackSessionId = QStringLiteral("playback-pending");
    pending.transitionSequence = 2;
    pending.action = TrackerScrobbleAction::Pause;
    pending.closesSession = true;
    pending.createdAtMs = 6000;
    QVERIFY(scrobble.store()->recordIntent(pending));

    scrobble.resumeAfterGlobalSyncEnabled();
    QCOMPARE(transport.readbackRequests.size(), 0);
    QCOMPARE(transport.requests.size(), 0);
    QCOMPARE(scrobble.store()->intents().size(), 2);

    transport.readbackResults.append(SimklScrobbleReadbackResult::ExactPresent);
    QVERIFY(settings.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), true));
    scrobble.resumeAfterGlobalSyncEnabled();
    QCOMPARE(transport.readbackRequests.size(), 1);
    QCOMPARE(transport.readbackRequests.first().operationId, unknown.operationId);
    QCOMPARE(transport.requests.size(), 1);
    QCOMPARE(transport.requests.first().operationId, pending.operationId);
    QCOMPARE(scrobble.store()->intent(unknown.operationId)->state,
             TrackerScrobbleState::Succeeded);
    QCOMPARE(scrobble.store()->intent(pending.operationId)->state,
             TrackerScrobbleState::Succeeded);
}

void TrackerDeliveryTest::inFlightUnknownScrobbleWaitsForResumeAfterGlobalPause()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    const TrackerProviderCapabilities capabilities = TrackerProviderCapability::Scrobble;
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000, capabilities,
         TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));

    TrackerSyncSettingsStore settings(*profile);
    QVERIFY(settings.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), true));
    FakeSimklScrobbleTransport transport;
    transport.holdSends = true;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    scrobble.setSyncSettingsStore(&settings);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));

    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 sequence) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), QStringLiteral("playback-pause-race")},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), 40},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), 45000},
                           {QStringLiteral("durationMs"), 100000}};
    };

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 1));
    QCOMPARE(transport.requests.size(), 1);
    QCOMPARE(transport.sendCompletions.size(), 1);
    QCOMPARE(scrobble.store()->intents().first().state, TrackerScrobbleState::Delivering);

    QVERIFY(settings.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), false));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("pause"), 2));
    QCOMPARE(transport.requests.size(), 1);
    QCOMPARE(scrobble.store()->intents().size(), 2);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Pending);

    transport.completeNextSend(SimklScrobbleSendResult::UnknownOutcome);
    QCOMPARE(scrobble.store()->intents().first().state, TrackerScrobbleState::UnknownOutcome);
    QCOMPARE(transport.readbackRequests.size(), 0);
    QCOMPARE(transport.requests.size(), 1);

    transport.holdSends = false;
    transport.readbackResults.append(SimklScrobbleReadbackResult::ExactPresent);
    QVERIFY(settings.setGlobalSetting(QStringLiteral("trackerSyncEnabled"), true));
    scrobble.resumeAfterGlobalSyncEnabled();
    QCOMPARE(transport.readbackRequests.size(), 1);
    QCOMPARE(transport.readbackRequests.first().operationId,
             scrobble.store()->intents().first().operationId);
    QCOMPARE(transport.requests.size(), 2);
    QCOMPARE(transport.requests.last().action, TrackerScrobbleAction::Pause);
    QCOMPARE(scrobble.store()->intents().first().state, TrackerScrobbleState::Succeeded);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Succeeded);
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::disablingScrobblingClosesTheCurrentSessionFirst()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble,
         TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    FakeSimklScrobbleTransport transport;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    scrobble.observePlaybackLifecycle({{QStringLiteral("action"), QStringLiteral("start")},
                                       {QStringLiteral("identity"), identity},
                                       {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                                       {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                                       {QStringLiteral("playbackGeneration"), 40},
                                       {QStringLiteral("transitionSequence"), 1},
                                       {QStringLiteral("positionMs"), 1000},
                                       {QStringLiteral("durationMs"), 100000}});
    QCOMPARE(transport.requests.size(), 1);
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), false));
    QCOMPARE(transport.requests.size(), 2);
    QCOMPARE(transport.requests.last().action, TrackerScrobbleAction::Pause);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Succeeded);
    QVERIFY(!scrobble.livePlaybackTrackingEnabled(QStringLiteral("simkl")));
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::ambiguousSimklMovieMappingsFailClosed()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble,
         TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752139")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    FakeSimklScrobbleTransport transport;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    scrobble.observePlaybackLifecycle({{QStringLiteral("action"), QStringLiteral("start")},
                                       {QStringLiteral("identity"), identity},
                                       {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                                       {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                                       {QStringLiteral("playbackGeneration"), 40},
                                       {QStringLiteral("transitionSequence"), 1},
                                       {QStringLiteral("positionMs"), 1000},
                                       {QStringLiteral("durationMs"), 100000}});
    QVERIFY(scrobble.store()->intents().isEmpty());
    QVERIFY(transport.requests.isEmpty());
}

void TrackerDeliveryTest::queuedScrobbleRevalidatesItsCapturedMappingRevision()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    const TrackerRemoteMediaKey remoteKey = remote(
        TrackerProviderId::Simkl, account, QStringLiteral("752138"));
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remoteKey,
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));

    FakeSimklScrobbleTransport transport;
    transport.holdSends = true;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 sequence, qint64 position) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), 40},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000}};
    };

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 1, 1000));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("pause"), 2, 50000));
    QCOMPARE(transport.requests.size(), 1);
    const quint64 capturedRevision = scrobble.store()->intents().last().mappingRevision;
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remoteKey,
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Corrected title")},
        TrackerMappingProvenance::UserConfirmed, &error));
    QVERIFY(trackerDelivery.mappingStore()->mapping(remoteKey)->revision > capturedRevision);

    transport.completeNextSend(SimklScrobbleSendResult::Succeeded);
    QCOMPARE(transport.requests.size(), 1); // the queued old mapping is never sent
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::NeedsAttention);
    QCOMPARE(scrobble.store()->intents().last().reason, TrackerScrobbleReason::MappingChanged);
}

void TrackerDeliveryTest::profileDeactivationClosesWithThePlayersCurrentPosition()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    FakeSimklScrobbleTransport transport;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 sequence, qint64 position) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), 40},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000}};
    };
    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 1, 1000));
    QVERIFY(scrobble.snapshotPlaybackPosition(
        QStringLiteral("player1"), QStringLiteral("playback-1"),
        scrobble.playbackScopeGeneration(), 46000, 100000, false));

    QVERIFY(scrobble.prepareForProfileDeactivation());
    QCOMPARE(transport.requests.size(), 2);
    QCOMPARE(transport.requests.last().action, TrackerScrobbleAction::Pause);
    QCOMPARE(transport.requests.last().progressHundredths, 4600);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Succeeded);
    QVERIFY(scrobble.store()->intents().last().closesSession);

    TrackerScrobbleStore reopened(*profile);
    QVERIFY(reopened.healthy());
    QVERIFY(!reopened.hasOpenPlayback(TrackerProviderId::Simkl, account));
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::uncertainStopRequiresReadbackAndBlocksOvertake()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    FakeSimklScrobbleTransport transport;
    transport.sendResults = {SimklScrobbleSendResult::Succeeded,
                             SimklScrobbleSendResult::UnknownOutcome};
    transport.readbackResults = {SimklScrobbleReadbackResult::Indeterminate};
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 sequence, qint64 position,
                     bool completed = false) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), 40},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000},
                           {QStringLiteral("completedLocally"), completed}};
    };

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 1, 1000));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("close"), 2, 95000, true));
    QCOMPARE(transport.requests.size(), 2);
    QCOMPARE(transport.requests.last().action, TrackerScrobbleAction::Stop);
    QCOMPARE(transport.readbackRequests.size(), 1);
    QCOMPARE(transport.readbackRequests.first().action, TrackerScrobbleAction::Stop);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::NeedsAttention);
    QVERIFY(scrobble.store()->hasBlockingOutcome(TrackerProviderId::Simkl, account));
    QVERIFY(scrobble.store()->hasOpenPlayback(TrackerProviderId::Simkl, account));
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::missingTransportWaitsWithoutAnAttempt()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble,
         TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore());
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    scrobble.observePlaybackLifecycle({{QStringLiteral("action"), QStringLiteral("start")},
                                       {QStringLiteral("identity"), identity},
                                       {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                                       {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                                       {QStringLiteral("playbackGeneration"), 40},
                                       {QStringLiteral("transitionSequence"), 1},
                                       {QStringLiteral("positionMs"), 1000},
                                       {QStringLiteral("durationMs"), 100000}});
    QCOMPARE(scrobble.store()->intents().size(), 1);
    QCOMPARE(scrobble.store()->intents().first().state, TrackerScrobbleState::Waiting);
    QCOMPARE(scrobble.store()->intents().first().attemptCount, 0);
    QCOMPARE(scrobble.store()->intents().first().reason, TrackerScrobbleReason::ProviderUnavailable);
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::knownNotAppliedEdgeIsSupersededByCurrentPlayerEvent()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble,
         TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    FakeSimklScrobbleTransport transport;
    transport.sendResults = {SimklScrobbleSendResult::KnownNotApplied,
                             SimklScrobbleSendResult::Succeeded};
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 sequence, qint64 position) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), 40},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000}};
    };

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 1, 0));
    QCOMPARE(transport.requests.size(), 1);
    QCOMPARE(scrobble.store()->intents().first().state, TrackerScrobbleState::Waiting);
    scrobble.observePlaybackLifecycle(event(QStringLiteral("pause"), 2, 50000));
    QCOMPARE(transport.requests.size(), 2); // the current pause replaces the known-unsent start
    QCOMPARE(transport.requests.at(0).action, TrackerScrobbleAction::Start);
    QCOMPARE(transport.requests.at(1).action, TrackerScrobbleAction::Pause);
    QCOMPARE(scrobble.store()->intents().first().attemptCount, 1);
    QCOMPARE(scrobble.store()->intents().first().state, TrackerScrobbleState::Superseded);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Succeeded);
    scrobble.observePlaybackLifecycle(event(QStringLiteral("close"), 3, 50000));
    QCOMPARE(transport.requests.size(), 2); // an identical pause closes locally without another POST
    QVERIFY(scrobble.store()->intents().last().closesSession);
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::newSessionRetriesProtectiveCloseBeforeStart()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    FakeSimklScrobbleTransport transport;
    transport.holdSends = true;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 generation, quint64 sequence,
                     const QString &sessionId, qint64 position) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), sessionId},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), generation},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000}};
    };

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 40, 1,
                                              QStringLiteral("old-session"), 1000));
    QCOMPARE(transport.requests.size(), 1);
    transport.completeNextSend(SimklScrobbleSendResult::Succeeded);
    scrobble.observePlaybackLifecycle(event(QStringLiteral("close"), 40, 2,
                                              QStringLiteral("old-session"), 50000));
    QCOMPARE(transport.requests.size(), 2);
    QCOMPARE(transport.requests.last().action, TrackerScrobbleAction::Pause);

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 41, 1,
                                              QStringLiteral("new-session"), 0));
    QCOMPARE(scrobble.store()->intents().size(), 3);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Pending);
    QCOMPARE(transport.requests.size(), 2); // no new-session start while the old close is unresolved
    scrobble.observePlaybackLifecycle(event(QStringLiteral("pause"), 41, 2,
                                              QStringLiteral("new-session"), 50000));
    QCOMPARE(scrobble.store()->intents().size(), 4);
    QCOMPARE(scrobble.store()->intents().at(2).state, TrackerScrobbleState::Superseded);
    QCOMPARE(scrobble.store()->intents().last().action, TrackerScrobbleAction::Pause);
    QCOMPARE(transport.requests.size(), 2); // a later pause cannot overtake the close either

    transport.completeNextSend(SimklScrobbleSendResult::KnownNotApplied);
    QCOMPARE(transport.requests.size(), 3); // the protective close gets its bounded retry first
    QCOMPARE(transport.requests.last().action, TrackerScrobbleAction::Pause);
    QCOMPARE(transport.requests.last().playbackSessionId, QStringLiteral("old-session"));
    QCOMPARE(scrobble.store()->intents().at(1).attemptCount, 2);
    QCOMPARE(scrobble.store()->intents().at(1).state, TrackerScrobbleState::Delivering);

    transport.completeNextSend(SimklScrobbleSendResult::Succeeded);
    QCOMPARE(transport.requests.size(), 4);
    QCOMPARE(transport.requests.last().action, TrackerScrobbleAction::Pause);
    QCOMPARE(transport.requests.last().playbackSessionId, QStringLiteral("new-session"));
    transport.completeNextSend(SimklScrobbleSendResult::Succeeded);
    QCOMPARE(scrobble.store()->intents().at(1).state, TrackerScrobbleState::Succeeded);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Succeeded);
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::waitingProtectiveCloseSurvivesLaterSessionEvents()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    FakeSimklScrobbleTransport initialTransport;
    initialTransport.sendResults = {SimklScrobbleSendResult::Succeeded,
                                    SimklScrobbleSendResult::KnownNotApplied};
    {
        TrackerScrobbleRuntime initial(*profile, trackerDelivery.connectionStore(),
                                         trackerDelivery.mappingStore(), &initialTransport);
        QVERIFY2(initial.start(&error), qPrintable(error));
        QVERIFY(initial.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
        auto event = [&](const QString &action, quint64 generation, quint64 sequence,
                         const QString &sessionId, qint64 position) {
            return QVariantMap{{QStringLiteral("action"), action},
                               {QStringLiteral("identity"), identity},
                               {QStringLiteral("sessionId"), sessionId},
                               {QStringLiteral("scopeGeneration"), initial.playbackScopeGeneration()},
                               {QStringLiteral("playbackGeneration"), generation},
                               {QStringLiteral("transitionSequence"), sequence},
                               {QStringLiteral("positionMs"), position},
                               {QStringLiteral("durationMs"), 100000}};
        };
        initial.observePlaybackLifecycle(event(QStringLiteral("start"), 40, 1,
                                                QStringLiteral("old-session"), 1000));
        initial.observePlaybackLifecycle(event(QStringLiteral("close"), 40, 2,
                                                QStringLiteral("old-session"), 50000));
        QCOMPARE(initial.store()->intents().at(1).state, TrackerScrobbleState::Waiting);
        QVERIFY(initial.store()->hasOpenPlaybackForSession(
            TrackerProviderId::Simkl, account, QStringLiteral("old-session")));
    }

    TrackerScrobbleRuntime resumed(*profile, trackerDelivery.connectionStore(),
                                    trackerDelivery.mappingStore());
    QVERIFY2(resumed.start(&error), qPrintable(error));
    const auto event = [&](const QString &action, quint64 generation, quint64 sequence,
                           const QString &sessionId, qint64 position) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), sessionId},
                           {QStringLiteral("scopeGeneration"), resumed.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), generation},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000}};
    };
    resumed.observePlaybackLifecycle(event(QStringLiteral("start"), 41, 1,
                                             QStringLiteral("new-session"), 0));
    QCOMPARE(resumed.store()->intents().at(1).state, TrackerScrobbleState::Waiting);
    QCOMPARE(resumed.store()->intents().last().state, TrackerScrobbleState::Pending);
    resumed.observePlaybackLifecycle(event(QStringLiteral("pause"), 41, 2,
                                             QStringLiteral("new-session"), 50000));
    QCOMPARE(resumed.store()->intents().at(1).state, TrackerScrobbleState::Waiting);
    QCOMPARE(resumed.store()->intents().at(2).state, TrackerScrobbleState::Superseded);
    QCOMPARE(resumed.store()->intents().last().state, TrackerScrobbleState::Pending);
    QCOMPARE(initialTransport.requests.size(), 2); // unresolved close blocks all later-session delivery
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::failedProtectiveCloseKeepsNewStartQueued()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    FakeSimklScrobbleTransport transport;
    transport.holdSends = true;
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 generation, quint64 sequence,
                     const QString &sessionId, qint64 position) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), sessionId},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), generation},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000}};
    };

    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 40, 1,
                                              QStringLiteral("old-session"), 1000));
    transport.completeNextSend(SimklScrobbleSendResult::Succeeded);
    scrobble.observePlaybackLifecycle(event(QStringLiteral("close"), 40, 2,
                                              QStringLiteral("old-session"), 50000));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 41, 1,
                                              QStringLiteral("new-session"), 0));
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Pending);

    transport.completeNextSend(SimklScrobbleSendResult::KnownNotApplied);
    QCOMPARE(transport.requests.size(), 3);
    transport.completeNextSend(SimklScrobbleSendResult::KnownNotApplied);
    QCOMPARE(transport.requests.size(), 3); // the new start cannot overtake an unresolved close
    QCOMPARE(scrobble.store()->intents().at(1).state, TrackerScrobbleState::NeedsAttention);
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Pending);
    QVERIFY(scrobble.store()->hasOpenPlaybackForSession(
        TrackerProviderId::Simkl, account, QStringLiteral("old-session")));
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::offlineWaitingStartDoesNotBlockAfterRestart()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};

    {
        TrackerScrobbleRuntime offline(*profile, trackerDelivery.connectionStore(),
                                        trackerDelivery.mappingStore());
        QVERIFY2(offline.start(&error), qPrintable(error));
        QVERIFY(offline.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
        offline.observePlaybackLifecycle({{QStringLiteral("action"), QStringLiteral("start")},
                                           {QStringLiteral("identity"), identity},
                                           {QStringLiteral("sessionId"), QStringLiteral("offline-session")},
                                           {QStringLiteral("scopeGeneration"), offline.playbackScopeGeneration()},
                                           {QStringLiteral("playbackGeneration"), 40},
                                           {QStringLiteral("transitionSequence"), 1},
                                           {QStringLiteral("positionMs"), 1000},
                                           {QStringLiteral("durationMs"), 100000}});
        QCOMPARE(offline.store()->intents().first().state, TrackerScrobbleState::Waiting);
    }

    FakeSimklScrobbleTransport transport;
    TrackerScrobbleRuntime resumed(*profile, trackerDelivery.connectionStore(),
                                    trackerDelivery.mappingStore(), &transport);
    QVERIFY2(resumed.start(&error), qPrintable(error));
    QVERIFY(resumed.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    resumed.observePlaybackLifecycle({{QStringLiteral("action"), QStringLiteral("start")},
                                      {QStringLiteral("identity"), identity},
                                      {QStringLiteral("sessionId"), QStringLiteral("new-session")},
                                      {QStringLiteral("scopeGeneration"), resumed.playbackScopeGeneration()},
                                      {QStringLiteral("playbackGeneration"), 41},
                                      {QStringLiteral("transitionSequence"), 1},
                                      {QStringLiteral("positionMs"), 12000},
                                      {QStringLiteral("durationMs"), 100000}});

    QCOMPARE(transport.requests.size(), 1);
    QCOMPARE(transport.requests.first().playbackSessionId, QStringLiteral("new-session"));
    QCOMPARE(transport.requests.first().progressHundredths, 1200);
    QCOMPARE(resumed.store()->intents().first().state, TrackerScrobbleState::Superseded);
    QCOMPARE(resumed.store()->intents().last().state, TrackerScrobbleState::Succeeded);
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

void TrackerDeliveryTest::knownNotAppliedPauseDoesNotHideAnOpenProviderSession()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = testProfile(root);
    QVERIFY(profile.has_value());
    ProgressStore progress(profile->progressIniPath());
    ActivityStore activity(profile->activityDbPath());
    HistoryStore history(profile->historyIniPath());
    TrackerDeliveryRuntime trackerDelivery(*profile, &progress, &activity, &history);
    QString error;
    QVERIFY2(trackerDelivery.start(&error), qPrintable(error));
    const QString account = QStringLiteral("simkl-account");
    QVERIFY(trackerDelivery.connectionStore()->upsert(
        {TrackerProviderId::Simkl, account, 4, 2000,
         TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}, &error));
    QVERIFY(trackerDelivery.mappingStore()->upsert(
        remote(TrackerProviderId::Simkl, account, QStringLiteral("752138")),
        {QStringLiteral("movie:movie-42"), QStringLiteral("movie"),
         QStringLiteral("movie-42"), QStringLiteral("Example movie")},
        TrackerMappingProvenance::UserConfirmed, &error));
    FakeSimklScrobbleTransport transport;
    transport.sendResults = {SimklScrobbleSendResult::Succeeded,
                             SimklScrobbleSendResult::KnownNotApplied,
                             SimklScrobbleSendResult::Succeeded};
    TrackerScrobbleRuntime scrobble(*profile, trackerDelivery.connectionStore(),
                                     trackerDelivery.mappingStore(), &transport);
    QVERIFY2(scrobble.start(&error), qPrintable(error));
    QVERIFY(scrobble.setLivePlaybackTrackingEnabled(QStringLiteral("simkl"), true));
    const QVariantMap identity{{QStringLiteral("source"), QStringLiteral("player1")},
                               {QStringLiteral("world"), QStringLiteral("theatre")},
                               {QStringLiteral("kind"), QStringLiteral("movie")},
                               {QStringLiteral("itemKey"), QStringLiteral("movie-42")}};
    auto event = [&](const QString &action, quint64 sequence, qint64 position) {
        return QVariantMap{{QStringLiteral("action"), action},
                           {QStringLiteral("identity"), identity},
                           {QStringLiteral("sessionId"), QStringLiteral("playback-1")},
                           {QStringLiteral("scopeGeneration"), scrobble.playbackScopeGeneration()},
                           {QStringLiteral("playbackGeneration"), 40},
                           {QStringLiteral("transitionSequence"), sequence},
                           {QStringLiteral("positionMs"), position},
                           {QStringLiteral("durationMs"), 100000}};
    };
    scrobble.observePlaybackLifecycle(event(QStringLiteral("start"), 1, 1000));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("pause"), 2, 50000));
    QCOMPARE(scrobble.store()->intents().last().state, TrackerScrobbleState::Waiting);
    QVERIFY(scrobble.store()->hasOpenPlayback(TrackerProviderId::Simkl, account));
    scrobble.observePlaybackLifecycle(event(QStringLiteral("close"), 3, 50000));
    QCOMPARE(transport.requests.size(), 3);
    QCOMPARE(transport.requests.last().action, TrackerScrobbleAction::Pause);
    QVERIFY(scrobble.store()->intents().last().closesSession);
    QVERIFY(!scrobble.store()->hasOpenPlayback(TrackerProviderId::Simkl, account));
    QCOMPARE(activity.historyProjectionFacts().size(), 0);
}

QTEST_GUILESS_MAIN(TrackerDeliveryTest)
#include "tst_tracker_delivery.moc"
