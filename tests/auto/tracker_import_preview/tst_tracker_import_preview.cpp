#include "trackers/TrackerImportStore.h"
#include "trackers/TrackerProgressImportOwner.h"
#include "ProgressStore.h"

#include <QHash>
#include <QTemporaryDir>
#include <QtTest>

namespace {

constexpr auto kProfileId = "22222222-2222-4222-8222-222222222222";

TrackerRemoteMediaKey remote(const QString &mediaId)
{
    return {TrackerProviderId::Simkl, QStringLiteral("42"), mediaId};
}

TrackerTitleMapping mapped(const QString &mediaId, const QString &canonicalId = {})
{
    const QString canonical = canonicalId.isEmpty() ? QStringLiteral("ct1:") + mediaId : canonicalId;
    return {remote(mediaId),
            {canonical, QStringLiteral("series"), QStringLiteral("theatre:") + mediaId,
             QStringLiteral("Frieren: Beyond Journey's End")},
            TrackerMappingProvenance::ExactProviderIdentity,
            1};
}

TrackerImportProgressTarget exactTarget(const TrackerTitleMapping &mapping,
                                        double fraction,
                                        bool completed = false)
{
    return {mapping.canonical.canonicalMediaId, QStringLiteral("video"),
            mapping.canonical.historyId + QStringLiteral(":1:1"), fraction, completed};
}

TrackerImportedProgressValue local(const TrackerTitleMapping &mapping, int progress, qint64 revision,
                                   bool nativeWitnessed = false)
{
    TrackerImportedProgressValue value{mapping.canonical.canonicalMediaId,
                                       mapping.canonical.historyKind,
                                       mapping.canonical.historyId, progress, false,
                                       revision, nativeWitnessed};
    value.exactProgressTarget = exactTarget(
        mapping, qBound(0.0, progress / 10.0, 1.0));
    return value;
}

TrackerImportRemoteItem item(const QString &id, const TrackerTitleMapping &mapping, int progress)
{
    TrackerImportRemoteItem result{id, mapping.remote, mapping, progress, false,
                                   true, false, false, std::nullopt};
    result.exactProgressTarget = exactTarget(
        mapping, qBound(0.0, progress / 10.0, 1.0));
    return result;
}

class FakeOwner final : public TrackerImportOwner
{
public:
    std::optional<TrackerImportedProgressValue> currentProgress(
        const TrackerTitleMapping &mapping) const override
    {
        const auto it = current.constFind(mapping.canonical.canonicalMediaId);
        return it == current.cend() ? std::nullopt
                                   : std::optional<TrackerImportedProgressValue>(*it);
    }

    TrackerImportOwnerApplyResult applyImportedProgress(
        const QString &operationId,
        const TrackerTitleMapping &mapping,
        const std::optional<TrackerImportedProgressValue> &expectedLocal,
        int progress,
        bool completed,
        std::optional<TrackerImportedProgressValue> *resultingProgress,
        QString *error) override
    {
        ++calls;
        if (const auto receipt = receipts.constFind(operationId); receipt != receipts.cend()) {
            if (receipt->canonicalMediaId != mapping.canonical.canonicalMediaId
                || receipt->historyKind != mapping.canonical.historyKind
                || receipt->historyId != mapping.canonical.historyId
                || receipt->progress != progress || receipt->completed != completed) {
                if (error)
                    *error = QStringLiteral("Import operation receipt is bound to another effect.");
                return TrackerImportOwnerApplyResult::Failed;
            }
            if (resultingProgress)
                *resultingProgress = receipt->resultingProgress;
            return TrackerImportOwnerApplyResult::AlreadyApplied;
        }
        const auto currentIt = current.constFind(mapping.canonical.canonicalMediaId);
        const std::optional<TrackerImportedProgressValue> observed = currentIt == current.cend()
            ? std::nullopt : std::optional<TrackerImportedProgressValue>(*currentIt);
        if (expectedLocal.has_value() != observed.has_value()
            || (expectedLocal && (expectedLocal->canonicalMediaId != observed->canonicalMediaId
                                  || expectedLocal->historyKind != observed->historyKind
                                  || expectedLocal->historyId != observed->historyId
                                  || expectedLocal->progress != observed->progress
                                  || expectedLocal->completed != observed->completed
                                  || expectedLocal->revision != observed->revision
                                  || expectedLocal->nativeWitnessedHistory != observed->nativeWitnessedHistory))) {
            if (error)
                *error = QStringLiteral("Local progress changed while this import was under review.");
            return TrackerImportOwnerApplyResult::Stale;
        }
        if (failBeforeFirstReceipt && calls == 1)
            return TrackerImportOwnerApplyResult::Failed;
        applied.insert(operationId);
        current.insert(mapping.canonical.canonicalMediaId,
                       local(mapping, progress,
                             current.value(mapping.canonical.canonicalMediaId).revision + 1,
                             expectedLocal && expectedLocal->nativeWitnessedHistory));
        current[mapping.canonical.canonicalMediaId].completed = completed;
        const auto result = current.value(mapping.canonical.canonicalMediaId);
        receipts.insert(operationId, {mapping.canonical.canonicalMediaId,
                                      mapping.canonical.historyKind, mapping.canonical.historyId,
                                      progress, completed, result});
        if (resultingProgress)
            *resultingProgress = result;
        if (failAfterFirstReceipt && calls == 1)
            return TrackerImportOwnerApplyResult::Failed;
        return TrackerImportOwnerApplyResult::Applied;
    }

    QHash<QString, TrackerImportedProgressValue> current;
    QSet<QString> applied;
    struct Receipt {
        QString canonicalMediaId;
        QString historyKind;
        QString historyId;
        int progress = 0;
        bool completed = false;
        TrackerImportedProgressValue resultingProgress;
    };
    QHash<QString, Receipt> receipts;
    int calls = 0;
    bool failBeforeFirstReceipt = false;
    bool failAfterFirstReceipt = false;
};

TrackerImportBatchDraft draft(const QList<TrackerImportRemoteItem> &items)
{
    return {TrackerProviderId::Simkl, QStringLiteral("42"), 1, QStringLiteral("snapshot-1"),
            QStringLiteral("cursor-1"), true, true, items};
}

bool installMappings(TrackerMappingStore *mappings, const QList<TrackerImportRemoteItem> &items)
{
    if (!mappings)
        return false;
    for (const TrackerImportRemoteItem &item : items) {
        if (item.mapping && !mappings->upsert(item.remote, item.mapping->canonical, item.mapping->provenance))
            return false;
    }
    return true;
}

bool installConnection(TrackerConnectionStore *connections, quint64 generation = 1)
{
    return connections && connections->upsert({TrackerProviderId::Simkl, QStringLiteral("42"),
                                                generation, 1,
                                                TrackerProviderCapability::ReadProgress,
                                                TrackerConnectionState::Connected});
}

} // namespace

class TrackerImportPreviewTest : public QObject
{
    Q_OBJECT

private slots:
    void previewIsInertAndClassifiesEveryProviderFact();
    void aggregateCountsWithoutExactProgressTargetsStayNonMutating();
    void productionProgressOwnerWritesExactEffectAndDurableOperationReceipt();
    void productionOwnerRejectsStaleSameKeyAndPreservesUnrelatedProgress();
    void productionOwnerWriterFailureDoesNotPublishOrAcknowledge();
    void asyncConfirmationWaitsForDurableOwnerBeforeCursorAdvance();
    void asyncMappingChangeReturnsThePersistedItemToReview();
    void productionProgressSnapshotDoesNotLookLikeAnUnrelatedLocalChange();
    void missingBatchIdsAreRejectedSafely();
    void forgedAndStaleMappingsCannotCreateOrApplyAProgressImport();
    void ownerReceiptCannotBeReusedAfterMappingChanges();
    void confirmationHonoursPerItemExceptionsAndAdvancesCursorOnlyWhenSettled();
    void staleLocalRevisionReturnsTheItemToReviewWithoutApplying();
    void crashBeforeOwnerReceiptCannotOverwriteNewerLocalProgress();
    void interruptedOwnerReceiptRecoversIdempotentlyAfterReopen();
    void failedPersistenceCannotAdvanceReviewOrConfirmation();
    void bulkResolutionPersistsAtomically();
    void routinePullAppliesOnlySafeAdvancesAndPersistsReviewItems();
    void routinePullDoesNotAutoApplyFractionOrCompletionRegressions();
    void bothSidesChangingSinceLastSyncRequiresReview();
    void oldUnresolvedPayloadRequiresFreshSnapshotBeforeApplying();
    void freshMatchedSnapshotSupersedesUnmatchedQueue();
    void importedProgressPreservesNativeHistoryWitness();
    void staleConnectionGenerationCannotApplyOrAdvanceCursor();
    void settledOldGenerationDoesNotBreakReconnectRecovery();
    void replayingAnOlderSettledPageCannotRegressTheCursor();
    void failedPageAndRemoteAbsenceCannotAdvanceOrDeleteLocalProgress();
};

void TrackerImportPreviewTest::previewIsInertAndClassifiesEveryProviderFact()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    FakeOwner owner;

    TrackerImportRemoteItem newly = item(QStringLiteral("new"), mapped(QStringLiteral("new")), 5);
    TrackerImportRemoteItem exact = item(QStringLiteral("exact"), mapped(QStringLiteral("exact")), 5);
    exact.localAtPreview = local(*exact.mapping, 5, 1);
    TrackerImportRemoteItem advance = item(QStringLiteral("advance"), mapped(QStringLiteral("advance")), 5);
    advance.localAtPreview = local(*advance.mapping, 2, 1);
    TrackerImportRemoteItem disagreement = item(QStringLiteral("disagreement"), mapped(QStringLiteral("disagreement")), 2);
    disagreement.localAtPreview = local(*disagreement.mapping, 5, 1, true);
    TrackerImportRemoteItem unmatched{QStringLiteral("unmatched"), remote(QStringLiteral("unmatched")), std::nullopt,
                                      5, false, true, false, false, std::nullopt};
    TrackerImportRemoteItem unsupported = item(QStringLiteral("unsupported"), mapped(QStringLiteral("unsupported")), 5);
    unsupported.supported = false;
    TrackerImportRemoteItem duplicate = item(QStringLiteral("duplicate"), mapped(QStringLiteral("duplicate")), 5);
    duplicate.duplicate = true;

    const QList<TrackerImportRemoteItem> items{newly, exact, advance, disagreement, unmatched, unsupported, duplicate};
    QVERIFY(installMappings(&mappings, items));
    TrackerImportStore store(*profile, &mappings, &connections);
    const auto preview = store.createPreview(draft(items));
    QVERIFY(preview.has_value());
    const auto replayedPreview = store.createPreview(draft(items));
    QVERIFY(replayedPreview.has_value());
    QCOMPARE(replayedPreview->batchId, preview->batchId);
    QCOMPARE(store.batches().size(), 1);
    TrackerImportBatchDraft changedPage = draft(items);
    changedPage.proposedCursor = QStringLiteral("cursor-2");
    QVERIFY(!store.createPreview(changedPage).has_value());
    changedPage = draft(items);
    changedPage.baseCursor = QStringLiteral("cursor-before");
    QVERIFY(!store.createPreview(changedPage).has_value());
    QCOMPARE(owner.calls, 0);
    QCOMPARE(preview->items.size(), 7);
    QCOMPARE(preview->items.at(0).classification, TrackerImportClassification::NewProgress);
    QCOMPARE(preview->items.at(1).classification, TrackerImportClassification::ExactMatch);
    QCOMPARE(preview->items.at(2).classification, TrackerImportClassification::RemoteAdvance);
    QCOMPARE(preview->items.at(3).classification, TrackerImportClassification::Disagreement);
    QCOMPARE(preview->items.at(4).classification, TrackerImportClassification::NeedsMatching);
    QCOMPARE(preview->items.at(5).classification, TrackerImportClassification::Unsupported);
    QCOMPARE(preview->items.at(6).classification, TrackerImportClassification::Duplicate);
    QVERIFY(!store.confirm(preview->batchId));
    QVERIFY(!store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")).has_value());
}

void TrackerImportPreviewTest::aggregateCountsWithoutExactProgressTargetsStayNonMutating()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    FakeOwner owner;

    TrackerImportRemoteItem countOnly = item(
        QStringLiteral("frieren-series"), mapped(QStringLiteral("frieren-series")), 8);
    countOnly.exactProgressTarget.reset();
    countOnly.localAtPreview = local(*countOnly.mapping, 3, 2);
    QVERIFY(installMappings(&mappings, {countOnly}));
    TrackerImportStore store(*profile, &mappings, &connections);

    const auto preview = store.createPreview(draft({countOnly}));
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.first().classification,
             TrackerImportClassification::Unsupported);
    QCOMPARE(preview->items.first().state, TrackerImportItemState::NonMutating);
    QVERIFY(store.confirm(preview->batchId));
    QVERIFY(store.applyConfirmed(preview->batchId, &owner));

    QCOMPARE(owner.calls, 0);
    QCOMPARE(owner.current.value(countOnly.mapping->canonical.canonicalMediaId).progress, 0);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));
}

void TrackerImportPreviewTest::productionProgressOwnerWritesExactEffectAndDurableOperationReceipt()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString progressPath = root.filePath(QStringLiteral("progress.ini"));
    ProgressStore progress(progressPath);
    TrackerProgressImportOwner owner(&progress);
    const TrackerTitleMapping mapping = mapped(QStringLiteral("exact-episode"));
    const TrackerImportProgressTarget target{
        mapping.canonical.canonicalMediaId, QStringLiteral("video"),
        QStringLiteral("show:1:4"), 1.0, true};

    TrackerImportOwnerApplyResult result = TrackerImportOwnerApplyResult::Failed;
    std::optional<TrackerImportedProgressValue> applied;
    QString error;
    bool finished = false;
    QSignalSpy localMutationSpy(&progress, &ProgressStore::syncDirty);
    owner.applyImportedProgressAsync(
        QStringLiteral("import-operation-1"), mapping, target, std::nullopt, 1, true,
        [&](TrackerImportOwnerApplyResult status,
            std::optional<TrackerImportedProgressValue> value,
            const QString &message) {
            result = status;
            applied = std::move(value);
            error = message;
            finished = true;
        });
    QVERIFY(!finished);
    progress.recordSilent({{QStringLiteral("kind"), QStringLiteral("video")},
                           {QStringLiteral("id"), QStringLiteral("other:1:1")},
                           {QStringLiteral("title"), QStringLiteral("Other title")},
                           {QStringLiteral("progress"), 0.25},
                           {QStringLiteral("updatedAt"), 1}});
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QCOMPARE(result, TrackerImportOwnerApplyResult::Applied);
    QVERIFY(error.isEmpty());
    QVERIFY(applied.has_value());
    QCOMPARE(applied->exactProgressTarget->id, target.id);
    QCOMPARE(progress.get(QStringLiteral("video"), target.id)
                 .value(QStringLiteral("progress")).toDouble(), 1.0);
    QCOMPARE(progress.get(QStringLiteral("video"), QStringLiteral("other:1:1"))
                 .value(QStringLiteral("progress")).toDouble(), 0.25);
    QCOMPARE(localMutationSpy.size(), 1); // unrelated local write only; import itself adds none

    TrackerImportOwnerApplyResult replayResult = TrackerImportOwnerApplyResult::Failed;
    finished = false;
    owner.applyImportedProgressAsync(
        QStringLiteral("import-operation-1"), mapping, target, std::nullopt, 1, true,
        [&](TrackerImportOwnerApplyResult status,
            std::optional<TrackerImportedProgressValue>, const QString &) {
            replayResult = status;
            finished = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QCOMPARE(replayResult, TrackerImportOwnerApplyResult::AlreadyApplied);

    progress.flush();
    ProgressStore reopened(progressPath);
    TrackerProgressImportOwner reopenedOwner(&reopened);
    TrackerImportOwnerApplyResult persistedReplay = TrackerImportOwnerApplyResult::Failed;
    bool persistedReplayFinished = false;
    reopenedOwner.applyImportedProgressAsync(
        QStringLiteral("import-operation-1"), mapping, target, std::nullopt, 1, true,
        [&](TrackerImportOwnerApplyResult status,
            std::optional<TrackerImportedProgressValue>, const QString &) {
            persistedReplay = status;
            persistedReplayFinished = true;
        });
    QVERIFY(persistedReplayFinished);
    QCOMPARE(persistedReplay, TrackerImportOwnerApplyResult::AlreadyApplied);

    TrackerImportOwnerApplyResult mismatchResult = TrackerImportOwnerApplyResult::Failed;
    finished = false;
    TrackerImportProgressTarget mismatch = target;
    mismatch.id = QStringLiteral("show:1:5");
    reopenedOwner.applyImportedProgressAsync(
        QStringLiteral("import-operation-1"), mapping, mismatch, std::nullopt, 1, true,
        [&](TrackerImportOwnerApplyResult status,
            std::optional<TrackerImportedProgressValue>, const QString &) {
            mismatchResult = status;
            finished = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QCOMPARE(mismatchResult, TrackerImportOwnerApplyResult::Failed);
}

void TrackerImportPreviewTest::productionOwnerRejectsStaleSameKeyAndPreservesUnrelatedProgress()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    ProgressStore progress(root.filePath(QStringLiteral("progress.ini")));
    const TrackerTitleMapping mapping = mapped(QStringLiteral("stale-episode"));
    const TrackerImportProgressTarget originalTarget{
        mapping.canonical.canonicalMediaId, QStringLiteral("video"),
        QStringLiteral("show:1:4"), 0.4, false};
    QVariantMap localEntry{{QStringLiteral("kind"), QStringLiteral("video")},
                           {QStringLiteral("id"), originalTarget.id},
                           {QStringLiteral("title"), QStringLiteral("Native title")},
                           {QStringLiteral("progress"), 0.4},
                           {QStringLiteral("watched"), false},
                           {QStringLiteral("updatedAt"), 12}};
    QVERIFY(progress.applySyncedEntry(localEntry));
    TrackerProgressImportOwner owner(&progress);
    const auto expected = owner.currentProgress(mapping, originalTarget);
    QVERIFY(expected.has_value());
    QVERIFY(!expected->ownerRevisionToken.isEmpty());

    // Same timestamp, progress, and watched state, but a different exact
    // record/provenance revision must still be rejected before the write.
    QVariantMap sameKeyUpdate = localEntry;
    sameKeyUpdate.insert(QStringLiteral("_trackerOrigin"), QStringLiteral("native_local"));
    sameKeyUpdate.insert(QStringLiteral("caption"), QStringLiteral("Same progress, changed metadata"));
    QVERIFY(progress.applySyncedEntry(sameKeyUpdate));
    const TrackerImportProgressTarget newerTarget{
        mapping.canonical.canonicalMediaId, QStringLiteral("video"),
        originalTarget.id, 0.8, false};
    TrackerImportOwnerApplyResult status = TrackerImportOwnerApplyResult::Failed;
    bool finished = false;
    owner.applyImportedProgressAsync(
        QStringLiteral("stale-same-key-operation"), mapping, newerTarget,
        expected, 8, false,
        [&](TrackerImportOwnerApplyResult result,
            std::optional<TrackerImportedProgressValue>, const QString &) {
            status = result;
            finished = true;
        });
    QVERIFY(finished);
    QCOMPARE(status, TrackerImportOwnerApplyResult::Stale);
    QCOMPARE(progress.get(QStringLiteral("video"), originalTarget.id)
                 .value(QStringLiteral("progress")).toDouble(), 0.4);
}

void TrackerImportPreviewTest::productionOwnerWriterFailureDoesNotPublishOrAcknowledge()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    ProgressStore progress(root.filePath(QStringLiteral("failure.ini")));
    TrackerProgressImportOwner owner(&progress);
    const TrackerTitleMapping mapping = mapped(QStringLiteral("writer-failure"));
    const TrackerImportProgressTarget target{
        mapping.canonical.canonicalMediaId, QStringLiteral("video"),
        QStringLiteral("show:1:2"), 0.6, false};
    progress.forceNextTrackerImportWriteFailureForTesting();
    TrackerImportOwnerApplyResult result = TrackerImportOwnerApplyResult::Applied;
    bool finished = false;
    owner.applyImportedProgressAsync(
        QStringLiteral("writer-failure-operation"), mapping, target,
        std::nullopt, 6, false,
        [&](TrackerImportOwnerApplyResult status,
            std::optional<TrackerImportedProgressValue>, const QString &) {
            result = status;
            finished = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QCOMPARE(result, TrackerImportOwnerApplyResult::Failed);
    QVERIFY(progress.get(QStringLiteral("video"), target.id).isEmpty());
    QVERIFY(!progress.healthy());
}

void TrackerImportPreviewTest::asyncConfirmationWaitsForDurableOwnerBeforeCursorAdvance()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem imported = item(
        QStringLiteral("async-import"), mapped(QStringLiteral("async-import")), 5);
    QVERIFY(installMappings(&mappings, {imported}));
    TrackerImportStore imports(*profile, &mappings, &connections);
    const auto preview = imports.createPreview(draft({imported}));
    QVERIFY(preview.has_value());
    QVERIFY(imports.resolve(preview->batchId, preview->items.first().itemId,
                            TrackerImportResolution::UseProviderProgress));
    QVERIFY(imports.confirm(preview->batchId));

    ProgressStore progress(root.filePath(QStringLiteral("progress.ini")));
    TrackerProgressImportOwner owner(&progress);
    QSignalSpy nativeCompletionSpy(&progress, &ProgressStore::completionCrossed);
    QSignalSpy accountEchoSpy(&progress, &ProgressStore::syncDirty);
    bool finished = false;
    bool succeeded = false;
    QString error;
    imports.applyConfirmedAsync(preview->batchId, &owner,
        [&](bool accepted, const QString &message) {
            succeeded = accepted;
            error = message;
            finished = true;
        });

    const auto applying = imports.batch(preview->batchId);
    QVERIFY(applying.has_value());
    QCOMPARE(applying->items.first().state, TrackerImportItemState::Applying);
    QVERIFY(!imports.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42"))
                 .has_value());
    QVERIFY(progress.get(imported.exactProgressTarget->kind,
                         imported.exactProgressTarget->id).isEmpty());
    QVERIFY(!finished);

    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QVERIFY2(succeeded, qPrintable(error));
    const auto applied = imports.batch(preview->batchId);
    QVERIFY(applied.has_value());
    QCOMPARE(applied->items.first().state, TrackerImportItemState::Applied);
    QCOMPARE(imports.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));
    QCOMPARE(progress.get(imported.exactProgressTarget->kind,
                          imported.exactProgressTarget->id)
                 .value(QStringLiteral("progress")).toDouble(), 0.5);
    QCOMPARE(nativeCompletionSpy.size(), 0);
    QCOMPARE(accountEchoSpy.size(), 0);
}

void TrackerImportPreviewTest::asyncMappingChangeReturnsThePersistedItemToReview()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem imported = item(
        QStringLiteral("async-remapped"), mapped(QStringLiteral("async-remapped")), 5);
    QVERIFY(installMappings(&mappings, {imported}));
    TrackerImportStore imports(*profile, &mappings, &connections);
    const auto preview = imports.createPreview(draft({imported}));
    QVERIFY(preview.has_value());
    QVERIFY(imports.resolve(preview->batchId, preview->items.first().itemId,
                            TrackerImportResolution::UseProviderProgress));
    QVERIFY(imports.confirm(preview->batchId));

    QVERIFY(mappings.upsert(imported.remote,
        {QStringLiteral("ct1:async-remapped-corrected"), QStringLiteral("series"),
         QStringLiteral("theatre:async-remapped-corrected"), QStringLiteral("Corrected title")},
        TrackerMappingProvenance::UserConfirmed));
    FakeOwner owner;
    bool finished = false;
    bool succeeded = true;
    imports.applyConfirmedAsync(preview->batchId, &owner,
        [&](bool accepted, const QString &) {
            succeeded = accepted;
            finished = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QVERIFY(!succeeded);
    QCOMPARE(owner.calls, 0);
    QVERIFY(!imports.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")).has_value());

    const auto returnedToReview = imports.batch(preview->batchId);
    QVERIFY(returnedToReview.has_value());
    QVERIFY(!returnedToReview->confirmed);
    QCOMPARE(returnedToReview->items.first().classification,
             TrackerImportClassification::NeedsMatching);
    QCOMPARE(returnedToReview->items.first().resolution, TrackerImportResolution::None);
    QCOMPARE(returnedToReview->items.first().state, TrackerImportItemState::ReviewRequired);
    QVERIFY(imports.resolve(preview->batchId, preview->items.first().itemId,
                            TrackerImportResolution::LeaveUnresolved));
    QVERIFY(imports.confirm(preview->batchId));
    finished = false;
    imports.applyConfirmedAsync(preview->batchId, &owner,
        [&](bool accepted, const QString &) {
            succeeded = accepted;
            finished = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QVERIFY(succeeded);
    QCOMPARE(owner.calls, 0);
    QCOMPARE(imports.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));
}

void TrackerImportPreviewTest::productionProgressSnapshotDoesNotLookLikeAnUnrelatedLocalChange()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem first = item(
        QStringLiteral("production-snapshot"), mapped(QStringLiteral("production-snapshot")), 3);
    first.exactProgressTarget = exactTarget(*first.mapping, 0.3);
    QVERIFY(installMappings(&mappings, {first}));
    TrackerImportStore imports(*profile, &mappings, &connections);
    ProgressStore progress(root.filePath(QStringLiteral("progress.ini")));
    TrackerProgressImportOwner owner(&progress);

    const auto initial = imports.createPreview(draft({first}));
    QVERIFY(initial.has_value());
    QVERIFY(imports.resolve(initial->batchId, initial->items.first().itemId,
                            TrackerImportResolution::UseProviderProgress));
    QVERIFY(imports.confirm(initial->batchId));
    bool finished = false;
    bool succeeded = false;
    imports.applyConfirmedAsync(initial->batchId, &owner,
        [&](bool accepted, const QString &) {
            succeeded = accepted;
            finished = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QVERIFY(succeeded);

    const auto settled = imports.batch(initial->batchId);
    QVERIFY(settled.has_value());
    QVERIFY(settled->items.first().localAfterSettlement.has_value());
    QCOMPARE(settled->items.first().localAfterSettlement->progress, 3);
    const auto current = owner.currentProgress(*first.mapping, *first.exactProgressTarget);
    QVERIFY(current.has_value());
    QCOMPARE(current->progress, 0);
    QCOMPARE(current->ownerRevisionToken,
             settled->items.first().localAfterSettlement->ownerRevisionToken);

    TrackerImportRemoteItem next = first;
    next.progress = 4;
    next.exactProgressTarget = exactTarget(*next.mapping, 0.4);
    next.localAtPreview = current;
    TrackerImportBatchDraft nextPage = draft({next});
    nextPage.snapshotId = QStringLiteral("production-snapshot-next");
    nextPage.proposedCursor = QStringLiteral("cursor-2");
    nextPage.baseCursor = QStringLiteral("cursor-1");
    nextPage.initialImport = false;
    const auto refreshed = imports.createPreview(nextPage);
    QVERIFY(refreshed.has_value());
    QCOMPARE(refreshed->items.first().classification,
             TrackerImportClassification::RemoteAdvance);
}

void TrackerImportPreviewTest::missingBatchIdsAreRejectedSafely()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportStore store(*profile, &mappings, &connections);
    QVERIFY(!store.resolve(QStringLiteral("missing-batch"), QStringLiteral("missing-item"),
                           TrackerImportResolution::UseProviderProgress));
    QVERIFY(!store.confirm(QStringLiteral("missing-batch")));
    QCOMPARE(store.batches().size(), 0);
}

void TrackerImportPreviewTest::forgedAndStaleMappingsCannotCreateOrApplyAProgressImport()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem remoteItem = item(QStringLiteral("mapped"), mapped(QStringLiteral("mapped")), 5);
    QVERIFY(installMappings(&mappings, {remoteItem}));
    TrackerImportStore store(*profile, &mappings, &connections);

    TrackerImportRemoteItem forged = remoteItem;
    forged.mapping->canonical.canonicalMediaId = QStringLiteral("ct1:forged");
    QVERIFY(!store.createPreview(draft({forged})).has_value());

    const auto preview = store.createPreview(draft({remoteItem}));
    QVERIFY(preview.has_value());
    QVERIFY(store.resolve(preview->batchId, QStringLiteral("mapped"), TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(preview->batchId));
    QVERIFY(mappings.upsert(remoteItem.remote,
                            {QStringLiteral("ct1:changed"), QStringLiteral("series"),
                             QStringLiteral("theatre:changed"), QStringLiteral("Changed")},
                            TrackerMappingProvenance::UserConfirmed));
    FakeOwner owner;
    QVERIFY(!store.applyConfirmed(preview->batchId, &owner));
    QCOMPARE(owner.applied.size(), 0);

    TrackerImportRemoteItem remapped = remoteItem;
    remapped.mapping = *mappings.mapping(remoteItem.remote);
    remapped.exactProgressTarget = exactTarget(*remapped.mapping, 0.5);
    const auto revalidated = store.createPreview(draft({remapped}));
    QVERIFY(revalidated.has_value());
    QCOMPARE(revalidated->items.first().remote.mapping->canonical.canonicalMediaId,
             QStringLiteral("ct1:changed"));
    QCOMPARE(revalidated->items.first().state, TrackerImportItemState::ReviewRequired);
    QVERIFY(revalidated->items.first().operationId != preview->items.first().operationId);
    QVERIFY(store.resolve(revalidated->batchId, QStringLiteral("mapped"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(revalidated->batchId));
    QVERIFY(store.applyConfirmed(revalidated->batchId, &owner));
    QCOMPARE(owner.applied.size(), 1);
}

void TrackerImportPreviewTest::ownerReceiptCannotBeReusedAfterMappingChanges()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem imported = item(QStringLiteral("title"), mapped(QStringLiteral("title")), 5);
    QVERIFY(installMappings(&mappings, {imported}));
    TrackerImportStore store(*profile, &mappings, &connections);
    const auto preview = store.createPreview(draft({imported}));
    QVERIFY(preview.has_value());
    QVERIFY(store.resolve(preview->batchId, QStringLiteral("title"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(preview->batchId));

    FakeOwner owner;
    owner.failAfterFirstReceipt = true;
    QVERIFY(!store.applyConfirmed(preview->batchId, &owner));
    QCOMPARE(owner.applied.size(), 1);
    const QString oldOperationId = preview->items.first().operationId;
    const QString oldCanonicalId = imported.mapping->canonical.canonicalMediaId;

    QVERIFY(mappings.upsert(imported.remote,
                            {QStringLiteral("ct1:correct-title"), QStringLiteral("series"),
                             QStringLiteral("theatre:correct-title"), QStringLiteral("Correct title")},
                            TrackerMappingProvenance::UserConfirmed));
    owner.failAfterFirstReceipt = false;
    QVERIFY(!store.applyConfirmed(preview->batchId, &owner));
    imported.mapping = *mappings.mapping(imported.remote);
    imported.exactProgressTarget = exactTarget(*imported.mapping, 0.5);
    const auto remapped = store.createPreview(draft({imported}));
    QVERIFY(remapped.has_value());
    QVERIFY(remapped->items.first().operationId != oldOperationId);
    QCOMPARE(remapped->items.first().state, TrackerImportItemState::ReviewRequired);
    QVERIFY(store.resolve(remapped->batchId, QStringLiteral("title"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(remapped->batchId));
    QVERIFY(store.applyConfirmed(remapped->batchId, &owner));
    QCOMPARE(owner.applied.size(), 2);
    QCOMPARE(owner.current.value(oldCanonicalId).progress, 5);
    QCOMPARE(owner.current.value(imported.mapping->canonical.canonicalMediaId).progress, 5);
}

void TrackerImportPreviewTest::confirmationHonoursPerItemExceptionsAndAdvancesCursorOnlyWhenSettled()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    FakeOwner owner;
    TrackerImportRemoteItem newly = item(QStringLiteral("new"), mapped(QStringLiteral("new")), 5);
    TrackerImportRemoteItem advance = item(QStringLiteral("advance"), mapped(QStringLiteral("advance")), 5);
    advance.localAtPreview = local(*advance.mapping, 2, 1);
    TrackerImportRemoteItem conflict = item(QStringLiteral("conflict"), mapped(QStringLiteral("conflict")), 7);
    conflict.localAtPreview = local(*conflict.mapping, 5, 1, true);
    conflict.contradictsNativeHistory = true;
    owner.current.insert(advance.mapping->canonical.canonicalMediaId, *advance.localAtPreview);
    owner.current.insert(conflict.mapping->canonical.canonicalMediaId, *conflict.localAtPreview);
    const QList<TrackerImportRemoteItem> items{newly, advance, conflict};
    QVERIFY(installMappings(&mappings, items));
    TrackerImportStore store(*profile, &mappings, &connections);
    const auto preview = store.createPreview(draft(items));
    QVERIFY(preview.has_value());

    QVERIFY(store.resolveAll(preview->batchId, TrackerImportClassification::NewProgress,
                             TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.resolveAll(preview->batchId, TrackerImportClassification::RemoteAdvance,
                             TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.resolve(preview->batchId, QStringLiteral("conflict"),
                          TrackerImportResolution::KeepColosseum));
    QVERIFY(store.confirm(preview->batchId));
    QString applyError;
    QVERIFY2(store.applyConfirmed(preview->batchId, &owner, &applyError), qPrintable(applyError));
    QCOMPARE(owner.applied.size(), 2);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));
}

void TrackerImportPreviewTest::staleLocalRevisionReturnsTheItemToReviewWithoutApplying()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    FakeOwner owner;
    TrackerImportRemoteItem advance = item(QStringLiteral("advance"), mapped(QStringLiteral("advance")), 5);
    advance.localAtPreview = local(*advance.mapping, 2, 1);
    QVERIFY(installMappings(&mappings, {advance}));
    TrackerImportStore store(*profile, &mappings, &connections);
    const auto preview = store.createPreview(draft({advance}));
    QVERIFY(preview.has_value());
    QVERIFY(store.resolve(preview->batchId, QStringLiteral("advance"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(preview->batchId));
    owner.current.insert(advance.mapping->canonical.canonicalMediaId, local(*advance.mapping, 3, 2));
    QVERIFY(!store.applyConfirmed(preview->batchId, &owner));
    const auto reopened = store.batch(preview->batchId);
    QVERIFY(reopened.has_value());
    QCOMPARE(reopened->items.first().classification, TrackerImportClassification::Disagreement);
    QCOMPARE(reopened->items.first().state, TrackerImportItemState::ReviewRequired);
    QCOMPARE(owner.applied.size(), 0);
    QVERIFY(!store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")).has_value());
}

void TrackerImportPreviewTest::crashBeforeOwnerReceiptCannotOverwriteNewerLocalProgress()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem advance = item(QStringLiteral("advance"), mapped(QStringLiteral("advance")), 5);
    advance.localAtPreview = local(*advance.mapping, 2, 1);
    QVERIFY(installMappings(&mappings, {advance}));
    FakeOwner owner;
    owner.current.insert(advance.mapping->canonical.canonicalMediaId, *advance.localAtPreview);
    owner.failBeforeFirstReceipt = true;
    QString batchId;
    {
        TrackerImportStore store(*profile, &mappings, &connections);
        const auto preview = store.createPreview(draft({advance}));
        QVERIFY(preview.has_value());
        batchId = preview->batchId;
        QVERIFY(store.resolve(batchId, QStringLiteral("advance"), TrackerImportResolution::UseProviderProgress));
        QVERIFY(store.confirm(batchId));
        QVERIFY(!store.applyConfirmed(batchId, &owner));
        QCOMPARE(owner.applied.size(), 0);
    }
    owner.failBeforeFirstReceipt = false;
    owner.current.insert(advance.mapping->canonical.canonicalMediaId, local(*advance.mapping, 3, 2));
    TrackerMappingStore reopenedMappings(*profile);
    TrackerConnectionStore reopenedConnections(*profile);
    TrackerImportStore reopened(*profile, &reopenedMappings, &reopenedConnections);
    QVERIFY(!reopened.recover(&owner));
    const auto recovered = reopened.batch(batchId);
    QVERIFY(recovered.has_value());
    QCOMPARE(recovered->items.first().classification, TrackerImportClassification::Disagreement);
    QCOMPARE(recovered->items.first().state, TrackerImportItemState::ReviewRequired);
    QVERIFY(!recovered->confirmed);
    QCOMPARE(owner.applied.size(), 0);
    QCOMPARE(owner.current.value(advance.mapping->canonical.canonicalMediaId).progress, 3);
}

void TrackerImportPreviewTest::interruptedOwnerReceiptRecoversIdempotentlyAfterReopen()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerImportRemoteItem newly = item(QStringLiteral("new"), mapped(QStringLiteral("new")), 5);
    FakeOwner owner;
    owner.failAfterFirstReceipt = true;
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    QVERIFY(installMappings(&mappings, {newly}));
    QString batchId;
    {
        TrackerImportStore store(*profile, &mappings, &connections);
        const auto preview = store.createPreview(draft({newly}));
        QVERIFY(preview.has_value());
        batchId = preview->batchId;
        QVERIFY(store.resolve(batchId, QStringLiteral("new"), TrackerImportResolution::UseProviderProgress));
        QVERIFY(store.confirm(batchId));
        QVERIFY(!store.applyConfirmed(batchId, &owner));
        QCOMPARE(owner.applied.size(), 1);
        QVERIFY(!store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")).has_value());
    }
    owner.failAfterFirstReceipt = false;
    TrackerMappingStore reopenedMappings(*profile);
    TrackerConnectionStore reopenedConnections(*profile);
    TrackerImportStore reopened(*profile, &reopenedMappings, &reopenedConnections);
    QVERIFY(reopened.recover(&owner));
    QCOMPARE(owner.applied.size(), 1);
    QCOMPARE(reopened.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));
}

void TrackerImportPreviewTest::failedPersistenceCannotAdvanceReviewOrConfirmation()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem newly = item(QStringLiteral("new"), mapped(QStringLiteral("new")), 5);
    QVERIFY(installMappings(&mappings, {newly}));
    TrackerImportStore store(*profile, &mappings, &connections);
    const auto preview = store.createPreview(draft({newly}));
    QVERIFY(preview.has_value());

    store.forcePersistenceFailureForTesting(true);
    QVERIFY(!store.resolve(preview->batchId, QStringLiteral("new"), TrackerImportResolution::UseProviderProgress));
    const auto unchanged = store.batch(preview->batchId);
    QVERIFY(unchanged.has_value());
    QCOMPARE(unchanged->items.first().resolution, TrackerImportResolution::None);
    QCOMPARE(unchanged->items.first().state, TrackerImportItemState::ReviewRequired);

    store.forcePersistenceFailureForTesting(false);
    QVERIFY(store.resolve(preview->batchId, QStringLiteral("new"), TrackerImportResolution::UseProviderProgress));
    store.forcePersistenceFailureForTesting(true);
    QVERIFY(!store.confirm(preview->batchId));
    const auto unconfirmed = store.batch(preview->batchId);
    QVERIFY(unconfirmed.has_value());
    QVERIFY(!unconfirmed->confirmed);

    store.forcePersistenceFailureForTesting(false);
    QVERIFY(store.confirm(preview->batchId));
}

void TrackerImportPreviewTest::bulkResolutionPersistsAtomically()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem first = item(QStringLiteral("first"), mapped(QStringLiteral("first")), 5);
    TrackerImportRemoteItem second = item(QStringLiteral("second"), mapped(QStringLiteral("second")), 5);
    QVERIFY(installMappings(&mappings, {first, second}));
    TrackerImportStore store(*profile, &mappings, &connections);
    const auto preview = store.createPreview(draft({first, second}));
    QVERIFY(preview.has_value());

    store.forcePersistenceFailureForTesting(true);
    QVERIFY(!store.resolveAll(preview->batchId, TrackerImportClassification::NewProgress,
                              TrackerImportResolution::UseProviderProgress));
    const auto unchanged = store.batch(preview->batchId);
    QVERIFY(unchanged.has_value());
    QCOMPARE(unchanged->items.at(0).state, TrackerImportItemState::ReviewRequired);
    QCOMPARE(unchanged->items.at(1).state, TrackerImportItemState::ReviewRequired);

    store.forcePersistenceFailureForTesting(false);
    QVERIFY(store.resolveAll(preview->batchId, TrackerImportClassification::NewProgress,
                             TrackerImportResolution::UseProviderProgress));
    const auto settled = store.batch(preview->batchId);
    QVERIFY(settled.has_value());
    QCOMPARE(settled->items.at(0).state, TrackerImportItemState::AwaitingApply);
    QCOMPARE(settled->items.at(1).state, TrackerImportItemState::AwaitingApply);
}

void TrackerImportPreviewTest::routinePullAppliesOnlySafeAdvancesAndPersistsReviewItems()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    FakeOwner owner;
    TrackerImportRemoteItem advance = item(QStringLiteral("advance"), mapped(QStringLiteral("advance")), 5);
    advance.localAtPreview = local(*advance.mapping, 2, 1);
    TrackerImportRemoteItem lower = item(QStringLiteral("lower"), mapped(QStringLiteral("lower")), 2);
    lower.localAtPreview = local(*lower.mapping, 5, 1, true);
    TrackerImportRemoteItem unmatched{QStringLiteral("unmatched"), remote(QStringLiteral("unmatched")), std::nullopt,
                                      5, false, true, false, false, std::nullopt};
    owner.current.insert(advance.mapping->canonical.canonicalMediaId, *advance.localAtPreview);
    owner.current.insert(lower.mapping->canonical.canonicalMediaId, *lower.localAtPreview);
    QVERIFY(installMappings(&mappings, {advance, lower, unmatched}));
    TrackerImportStore store(*profile, &mappings, &connections);

    TrackerImportBatchDraft prematurePull = draft({advance});
    prematurePull.snapshotId = QStringLiteral("premature-routine");
    prematurePull.proposedCursor = QStringLiteral("premature-cursor");
    prematurePull.baseCursor.clear();
    prematurePull.initialImport = false;
    const auto prematurePreview = store.createPreview(prematurePull);
    QVERIFY(prematurePreview.has_value());
    QVERIFY(!store.applyRoutineSafe(prematurePreview->batchId, &owner, true));
    QCOMPARE(owner.applied.size(), 0);
    QVERIFY(!store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")).has_value());

    const auto initial = store.createPreview(draft({advance, lower}));
    QVERIFY(initial.has_value());
    QVERIFY(store.resolve(initial->batchId, QStringLiteral("advance"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.resolve(initial->batchId, QStringLiteral("lower"),
                          TrackerImportResolution::KeepColosseum));
    QVERIFY(store.confirm(initial->batchId));
    QVERIFY(store.applyConfirmed(initial->batchId, &owner));
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));

    advance.progress = 6;
    advance.exactProgressTarget = exactTarget(*advance.mapping, 0.6);
    advance.localAtPreview = owner.currentProgress(*advance.mapping);
    TrackerImportRemoteItem routineLower = lower;
    routineLower.progress = 1;
    routineLower.exactProgressTarget = exactTarget(*routineLower.mapping, 0.1);
    routineLower.localAtPreview = owner.currentProgress(*routineLower.mapping);
    TrackerImportBatchDraft pull = draft({advance, routineLower, unmatched});
    pull.snapshotId = QStringLiteral("routine-snapshot");
    pull.proposedCursor = QStringLiteral("cursor-2");
    pull.baseCursor = QStringLiteral("cursor-1");
    pull.initialImport = false;
    const auto preview = store.createPreview(pull);
    QVERIFY(preview.has_value());

    QVERIFY(!store.applyRoutineSafe(preview->batchId, &owner, false));
    QCOMPARE(owner.applied.size(), 1);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));

    QVERIFY(store.applyRoutineSafe(preview->batchId, &owner, true));
    QCOMPARE(owner.applied.size(), 2);
    const auto settled = store.batch(preview->batchId);
    QVERIFY(settled.has_value());
    QCOMPARE(settled->items.at(0).state, TrackerImportItemState::Applied);
    QCOMPARE(settled->items.at(1).state, TrackerImportItemState::Unresolved);
    QCOMPARE(settled->items.at(2).state, TrackerImportItemState::Unresolved);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-2")));
}

void TrackerImportPreviewTest::routinePullDoesNotAutoApplyFractionOrCompletionRegressions()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    FakeOwner owner;
    TrackerImportRemoteItem initialItem = item(
        QStringLiteral("initial"), mapped(QStringLiteral("initial")), 2);
    TrackerImportRemoteItem higherButIncomplete = item(
        QStringLiteral("higher-but-incomplete"), mapped(QStringLiteral("higher-but-incomplete")), 8);
    TrackerImportRemoteItem lowerButComplete = item(
        QStringLiteral("lower-but-complete"), mapped(QStringLiteral("lower-but-complete")), 4);
    higherButIncomplete.localAtPreview = local(*higherButIncomplete.mapping, 6, 1);
    higherButIncomplete.localAtPreview->completed = true;
    higherButIncomplete.localAtPreview->exactProgressTarget->completed = true;
    lowerButComplete.exactProgressTarget->completed = true;
    lowerButComplete.completed = true;
    lowerButComplete.localAtPreview = local(*lowerButComplete.mapping, 6, 1);
    owner.current.insert(higherButIncomplete.mapping->canonical.canonicalMediaId,
                         *higherButIncomplete.localAtPreview);
    owner.current.insert(lowerButComplete.mapping->canonical.canonicalMediaId,
                         *lowerButComplete.localAtPreview);
    QVERIFY(installMappings(&mappings, {initialItem, higherButIncomplete, lowerButComplete}));
    TrackerImportStore store(*profile, &mappings, &connections);

    const auto initial = store.createPreview(draft({initialItem}));
    QVERIFY(initial.has_value());
    QVERIFY(store.resolve(initial->batchId, initial->items.first().itemId,
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(initial->batchId));
    QVERIFY(store.applyConfirmed(initial->batchId, &owner));
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));
    const int writesBeforeRoutinePull = owner.calls;

    TrackerImportBatchDraft pull = draft({higherButIncomplete, lowerButComplete});
    pull.snapshotId = QStringLiteral("routine-monotonicity");
    pull.proposedCursor = QStringLiteral("cursor-2");
    pull.baseCursor = QStringLiteral("cursor-1");
    pull.initialImport = false;
    const auto preview = store.createPreview(pull);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.at(0).classification, TrackerImportClassification::Disagreement);
    QCOMPARE(preview->items.at(1).classification, TrackerImportClassification::Disagreement);
    QVERIFY(store.applyRoutineSafe(preview->batchId, &owner, true));
    QCOMPARE(owner.calls, writesBeforeRoutinePull);
    QCOMPARE(owner.current.value(higherButIncomplete.mapping->canonical.canonicalMediaId)
                 .exactProgressTarget->fraction, 0.6);
    QVERIFY(owner.current.value(higherButIncomplete.mapping->canonical.canonicalMediaId)
                .exactProgressTarget->completed);
    QCOMPARE(owner.current.value(lowerButComplete.mapping->canonical.canonicalMediaId)
                 .exactProgressTarget->fraction, 0.6);
    QVERIFY(!owner.current.value(lowerButComplete.mapping->canonical.canonicalMediaId)
                 .exactProgressTarget->completed);
    const auto settled = store.batch(preview->batchId);
    QVERIFY(settled.has_value());
    QCOMPARE(settled->items.at(0).state, TrackerImportItemState::Unresolved);
    QCOMPARE(settled->items.at(1).state, TrackerImportItemState::Unresolved);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-2")));
}

void TrackerImportPreviewTest::bothSidesChangingSinceLastSyncRequiresReview()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem first = item(QStringLiteral("series"), mapped(QStringLiteral("series")), 5);
    QVERIFY(installMappings(&mappings, {first}));
    TrackerImportStore store(*profile, &mappings, &connections);
    FakeOwner owner;

    const auto initial = store.createPreview(draft({first}));
    QVERIFY(initial.has_value());
    QVERIFY(store.resolve(initial->batchId, QStringLiteral("series"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(initial->batchId));
    QVERIFY(store.applyConfirmed(initial->batchId, &owner));
    QCOMPARE(owner.current.value(first.mapping->canonical.canonicalMediaId).progress, 5);

    TrackerImportRemoteItem changedOnBothSides = first;
    changedOnBothSides.progress = 7;
    changedOnBothSides.exactProgressTarget = exactTarget(*changedOnBothSides.mapping, 0.7);
    changedOnBothSides.localAtPreview = local(*changedOnBothSides.mapping, 6, 2);
    owner.current.insert(changedOnBothSides.mapping->canonical.canonicalMediaId,
                         *changedOnBothSides.localAtPreview);
    TrackerImportBatchDraft next = draft({changedOnBothSides});
    next.snapshotId = QStringLiteral("snapshot-2");
    next.proposedCursor = QStringLiteral("cursor-2");
    next.baseCursor = QStringLiteral("cursor-1");
    next.initialImport = false;
    const auto preview = store.createPreview(next);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.first().classification, TrackerImportClassification::Disagreement);

    const int callsBeforePull = owner.calls;
    QVERIFY(store.applyRoutineSafe(preview->batchId, &owner, true));
    QCOMPARE(owner.calls, callsBeforePull);
    const auto pending = store.batch(preview->batchId);
    QVERIFY(pending.has_value());
    QCOMPARE(pending->items.first().state, TrackerImportItemState::Unresolved);
    QCOMPARE(owner.current.value(first.mapping->canonical.canonicalMediaId).progress, 6);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-2")));

    QVERIFY(!store.resolve(preview->batchId, QStringLiteral("series"),
                           TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.resolve(preview->batchId, QStringLiteral("series"),
                          TrackerImportResolution::KeepColosseum));
    QVERIFY(store.applyConfirmed(preview->batchId, &owner));
    QCOMPARE(owner.current.value(first.mapping->canonical.canonicalMediaId).progress, 6);

    TrackerImportRemoteItem fresh = changedOnBothSides;
    fresh.progress = 8;
    fresh.exactProgressTarget = exactTarget(*fresh.mapping, 0.8);
    fresh.localAtPreview = owner.currentProgress(*fresh.mapping);
    TrackerImportBatchDraft freshSnapshot = draft({fresh});
    freshSnapshot.snapshotId = QStringLiteral("snapshot-3");
    freshSnapshot.proposedCursor = QStringLiteral("cursor-3");
    freshSnapshot.baseCursor = QStringLiteral("cursor-2");
    freshSnapshot.initialImport = false;
    const auto refreshed = store.createPreview(freshSnapshot);
    QVERIFY(refreshed.has_value());
    QCOMPARE(refreshed->items.first().classification, TrackerImportClassification::RemoteAdvance);
    QVERIFY(store.resolve(refreshed->batchId, QStringLiteral("series"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(refreshed->batchId));
    QVERIFY(store.applyConfirmed(refreshed->batchId, &owner));
    QCOMPARE(owner.current.value(first.mapping->canonical.canonicalMediaId).progress, 8);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-3")));
}

void TrackerImportPreviewTest::oldUnresolvedPayloadRequiresFreshSnapshotBeforeApplying()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem conflict = item(QStringLiteral("series"), mapped(QStringLiteral("series")), 7);
    conflict.localAtPreview = local(*conflict.mapping, 5, 1);
    QVERIFY(installMappings(&mappings, {conflict}));
    TrackerImportStore store(*profile, &mappings, &connections);
    FakeOwner owner;
    owner.current.insert(conflict.mapping->canonical.canonicalMediaId, *conflict.localAtPreview);

    const auto first = store.createPreview(draft({conflict}));
    QVERIFY(first.has_value());
    QVERIFY(store.resolve(first->batchId, QStringLiteral("series"),
                          TrackerImportResolution::LeaveUnresolved));
    QVERIFY(store.confirm(first->batchId));
    QVERIFY(store.applyConfirmed(first->batchId, &owner));
    const auto queued = store.batch(first->batchId);
    QVERIFY(queued.has_value());
    QVERIFY(queued->cursorCommitted);
    QVERIFY(!store.resolve(first->batchId, QStringLiteral("series"),
                           TrackerImportResolution::UseProviderProgress));
    QCOMPARE(owner.current.value(conflict.mapping->canonical.canonicalMediaId).progress, 5);
}

void TrackerImportPreviewTest::freshMatchedSnapshotSupersedesUnmatchedQueue()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem unmatched{QStringLiteral("remote-title"), remote(QStringLiteral("late-match")),
                                      std::nullopt, 4, false, true, false, false, std::nullopt};
    TrackerImportStore store(*profile, &mappings, &connections);
    FakeOwner owner;

    const auto first = store.createPreview(draft({unmatched}));
    QVERIFY(first.has_value());
    QVERIFY(store.resolve(first->batchId, QStringLiteral("remote-title"),
                          TrackerImportResolution::LeaveUnresolved));
    QVERIFY(store.confirm(first->batchId));
    QVERIFY(store.applyConfirmed(first->batchId, &owner));
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));

    const TrackerRemoteMediaKey key = unmatched.remote;
    const TrackerCanonicalTitleCandidate canonical{QStringLiteral("ct1:late-match"), QStringLiteral("series"),
                                                    QStringLiteral("theatre:late-match"), QStringLiteral("Late Match")};
    QVERIFY(mappings.upsert(key, canonical, TrackerMappingProvenance::UserConfirmed));
    TrackerImportRemoteItem matched = unmatched;
    matched.mapping = mappings.mapping(key);
    QVERIFY(matched.mapping.has_value());
    matched.progress = 6;
    matched.exactProgressTarget = exactTarget(*matched.mapping, 0.6);
    TrackerImportBatchDraft fresh = draft({matched});
    fresh.snapshotId = QStringLiteral("incomplete-match-snapshot");
    fresh.proposedCursor = QStringLiteral("cursor-2");
    fresh.baseCursor = QStringLiteral("cursor-1");
    fresh.initialImport = false;
    fresh.pageComplete = false;

    const auto incomplete = store.createPreview(fresh);
    QVERIFY(incomplete.has_value());
    const auto stillQueued = store.batch(first->batchId);
    QVERIFY(stillQueued.has_value());
    QCOMPARE(stillQueued->items.first().state, TrackerImportItemState::Unresolved);
    QVERIFY(!store.confirm(incomplete->batchId));

    fresh.snapshotId = QStringLiteral("fresh-match-snapshot");
    fresh.pageComplete = true;

    const auto review = store.createPreview(fresh);
    QVERIFY(review.has_value());
    QCOMPARE(review->items.first().classification, TrackerImportClassification::Disagreement);
    const auto superseded = store.batch(first->batchId);
    QVERIFY(superseded.has_value());
    QCOMPARE(superseded->items.first().state, TrackerImportItemState::Superseded);
    QCOMPARE(owner.applied.size(), 0);

    QVERIFY(store.resolve(review->batchId, QStringLiteral("remote-title"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(review->batchId));
    QVERIFY(store.applyConfirmed(review->batchId, &owner));
    QCOMPARE(owner.current.value(canonical.canonicalMediaId).progress, 6);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-2")));
}

void TrackerImportPreviewTest::importedProgressPreservesNativeHistoryWitness()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem conflict = item(QStringLiteral("witnessed"), mapped(QStringLiteral("witnessed")), 7);
    conflict.localAtPreview = local(*conflict.mapping, 5, 1, true);
    conflict.contradictsNativeHistory = true;
    QVERIFY(installMappings(&mappings, {conflict}));
    TrackerImportStore store(*profile, &mappings, &connections);
    FakeOwner owner;
    owner.current.insert(conflict.mapping->canonical.canonicalMediaId, *conflict.localAtPreview);

    const auto preview = store.createPreview(draft({conflict}));
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.first().classification, TrackerImportClassification::Disagreement);
    QVERIFY(store.resolve(preview->batchId, QStringLiteral("witnessed"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(preview->batchId));
    QVERIFY(store.applyConfirmed(preview->batchId, &owner));
    const auto imported = owner.current.value(conflict.mapping->canonical.canonicalMediaId);
    QCOMPARE(imported.progress, 7);
    QVERIFY(imported.nativeWitnessedHistory);
}

void TrackerImportPreviewTest::staleConnectionGenerationCannotApplyOrAdvanceCursor()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections, 1));
    TrackerImportRemoteItem newly = item(QStringLiteral("new"), mapped(QStringLiteral("new")), 5);
    QVERIFY(installMappings(&mappings, {newly}));
    TrackerImportStore store(*profile, &mappings, &connections);
    const auto preview = store.createPreview(draft({newly}));
    QVERIFY(preview.has_value());
    QVERIFY(store.resolve(preview->batchId, QStringLiteral("new"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(preview->batchId));

    FakeOwner owner;
    QVERIFY(installConnection(&connections, 2));
    QVERIFY(!store.applyConfirmed(preview->batchId, &owner));
    QVERIFY(store.recover(&owner));
    QCOMPARE(owner.calls, 0);
    QVERIFY(!store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")).has_value());
    const auto unchanged = store.batch(preview->batchId);
    QVERIFY(unchanged.has_value());
    QCOMPARE(unchanged->items.first().state, TrackerImportItemState::AwaitingApply);
    QVERIFY(!store.createPreview(draft({newly})).has_value());
}

void TrackerImportPreviewTest::settledOldGenerationDoesNotBreakReconnectRecovery()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections, 1));
    TrackerImportRemoteItem newly = item(QStringLiteral("new"), mapped(QStringLiteral("new")), 5);
    QVERIFY(installMappings(&mappings, {newly}));
    TrackerImportStore store(*profile, &mappings, &connections);
    FakeOwner owner;

    const auto preview = store.createPreview(draft({newly}));
    QVERIFY(preview.has_value());
    QVERIFY(store.resolve(preview->batchId, QStringLiteral("new"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(preview->batchId));
    QVERIFY(store.applyConfirmed(preview->batchId, &owner));
    QCOMPARE(owner.calls, 1);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));

    QVERIFY(installConnection(&connections, 2));
    QVERIFY(store.recover(&owner));
    QCOMPARE(owner.calls, 1);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));
}

void TrackerImportPreviewTest::replayingAnOlderSettledPageCannotRegressTheCursor()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportRemoteItem newly = item(QStringLiteral("new"), mapped(QStringLiteral("new")), 5);
    QVERIFY(installMappings(&mappings, {newly}));
    TrackerImportStore store(*profile, &mappings, &connections);
    FakeOwner owner;

    const auto first = store.createPreview(draft({newly}));
    QVERIFY(first.has_value());
    QVERIFY(store.resolve(first->batchId, QStringLiteral("new"),
                          TrackerImportResolution::UseProviderProgress));
    QVERIFY(store.confirm(first->batchId));
    QVERIFY(store.applyConfirmed(first->batchId, &owner));
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-1")));

    TrackerImportBatchDraft secondDraft = draft({});
    secondDraft.snapshotId = QStringLiteral("snapshot-2");
    secondDraft.baseCursor = QStringLiteral("cursor-1");
    secondDraft.proposedCursor = QStringLiteral("cursor-2");
    secondDraft.initialImport = false;
    const auto second = store.createPreview(secondDraft);
    QVERIFY(second.has_value());
    QVERIFY(store.applyRoutineSafe(second->batchId, &owner, true));
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-2")));

    QVERIFY(store.applyConfirmed(first->batchId, &owner));
    QVERIFY(store.recover(&owner));
    QCOMPARE(owner.applied.size(), 1);
    QCOMPARE(store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")),
             std::optional<QString>(QStringLiteral("cursor-2")));
}

void TrackerImportPreviewTest::failedPageAndRemoteAbsenceCannotAdvanceOrDeleteLocalProgress()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    TrackerConnectionStore connections(*profile);
    QVERIFY(installConnection(&connections));
    TrackerImportStore store(*profile, &mappings, &connections);
    FakeOwner owner;
    const TrackerTitleMapping mapping = mapped(QStringLiteral("local-only"));
    owner.current.insert(mapping.canonical.canonicalMediaId, local(mapping, 9, 4, true));
    TrackerImportBatchDraft failed = draft({});
    failed.pageComplete = false;
    const auto preview = store.createPreview(failed);
    QVERIFY(preview.has_value());
    QVERIFY(!store.confirm(preview->batchId));
    QVERIFY(!store.applyConfirmed(preview->batchId, &owner));
    QCOMPARE(owner.current.value(mapping.canonical.canonicalMediaId).progress, 9);
    QVERIFY(!store.confirmedCursor(TrackerProviderId::Simkl, QStringLiteral("42")).has_value());
}

QTEST_GUILESS_MAIN(TrackerImportPreviewTest)

#include "tst_tracker_import_preview.moc"
