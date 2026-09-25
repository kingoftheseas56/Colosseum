#include "account/ProfilePaths.h"
#include "trackers/TrackerConnectionStore.h"
#include "trackers/TrackerCredentialVault.h"
#include "trackers/TrackerDeliveryStore.h"
#include "trackers/TrackerHistoryEvidenceStore.h"
#include "trackers/TrackerImportStore.h"
#include "trackers/TrackerLifecycleCoordinator.h"
#include "trackers/TrackerMappingStore.h"
#include "trackers/TrackerScrobbleStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <functional>

namespace {

constexpr auto kAccountProfileId = "11111111-1111-4111-8111-111111111111";

class FakeVault final : public TrackerCredentialVault
{
public:
    bool isAvailable() const override { return available; }

    bool hasReusableCredential(const TrackerCredentialSlot &slot, qint64 nowMs) const override
    {
        const auto credential = loadForProfile(slot.profileId, slot.providerId);
        return credential && credential->slot.remoteAccountId == slot.remoteAccountId
            && trackerCredentialIsReusable(*credential, nowMs);
    }

    bool saveAndVerify(const TrackerCredential &credential) override
    {
        if (!available || credential.slot.profileId == failSaveForProfile)
            return false;
        if (beforeSave)
            beforeSave(credential);
        credentials.insert(key(credential.slot.profileId, credential.slot.providerId), credential);
        return true;
    }

    std::optional<TrackerCredential> loadForProfile(
        const QString &profileId, TrackerProviderId providerId) const override
    {
        const auto found = credentials.constFind(key(profileId, providerId));
        return found == credentials.cend()
            ? std::nullopt : std::optional<TrackerCredential>(found.value());
    }

    bool clearForProfile(const QString &profileId, TrackerProviderId providerId) override
    {
        ++clearCount;
        clearedProviders.append(providerId);
        if (failClear || (rejectUnsupportedClear && providerId != TrackerProviderId::Simkl))
            return false;
        credentials.remove(key(profileId, providerId));
        return true;
    }

    static QString key(const QString &profileId, TrackerProviderId providerId)
    {
        return profileId + QChar(0x1f) + trackerProviderKey(providerId);
    }

    bool available = true;
    bool failClear = false;
    bool rejectUnsupportedClear = false;
    QString failSaveForProfile;
    std::function<void(const TrackerCredential &)> beforeSave;
    int clearCount = 0;
    QList<TrackerProviderId> clearedProviders;
    QHash<QString, TrackerCredential> credentials;
};

TrackerCredential credential(const QString &profileId, const QString &remoteAccountId,
                             TrackerProviderId providerId = TrackerProviderId::Simkl)
{
    return {{profileId, providerId, remoteAccountId},
            QByteArrayLiteral("access"), QByteArrayLiteral("refresh"),
            5000, 10000, {QStringLiteral("media:read"), QStringLiteral("media:write")}};
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

TrackerCanonicalTitleCandidate frieren()
{
    return {QStringLiteral("ct1:49f10000-0000-4000-8000-000000000001"),
            QStringLiteral("series"), QStringLiteral("theatre:frieren"),
            QStringLiteral("Frieren: Beyond Journey's End")};
}

TrackerImportedHistoryEvidence importedEvidence(const TrackerTitleMapping &mapping)
{
    TrackerImportedHistoryEvidence evidence{mapping, TrackerImportedEventKind::Completion,
        QStringLiteral("simkl-event-1"), QStringLiteral("snapshot-1"), 1500};
    evidence.timestampSource = TrackerEvidenceTimestampSource::ProviderEvent;
    evidence.timestampPrecision = TrackerEvidenceTimestampPrecision::ExactMillisecond;
    evidence.eventPayloadFingerprint = QStringLiteral("completion:s1e1");
    return evidence;
}

class FakeDeliverySource final : public TrackerDeliverySource
{
public:
    QList<TrackerDeliveryFact> currentCommittedFacts() const override { return facts; }

    bool isDurablyCurrent(const TrackerDeliveryFact &fact) const override
    {
        return std::any_of(facts.cbegin(), facts.cend(), [&fact](const auto &current) {
            return current.canonicalMediaId == fact.canonicalMediaId
                && current.kind == fact.kind && current.sourceRevision == fact.sourceRevision
                && current.contentFingerprint == fact.contentFingerprint;
        });
    }

    QList<TrackerDeliveryFact> facts;
};

TrackerDeliveryFact reconnectProgressFact()
{
    const auto title = frieren();
    return {title.canonicalMediaId, title.historyKind, title.historyId,
            TrackerDeliveryFactKind::Progress, 3, {}, 6,
            QStringLiteral("native-progress-fingerprint"),
            TrackerDeliveryOrigin::NativeLocal, TrackerMediaDomain::Television};
}

} // namespace

class TrackerLifecycleTest final : public QObject
{
    Q_OBJECT

private slots:
    void reconnectPullsThenReconcilesThenResumes();
    void reconnectStopsWhenBindingChangesDuringPull();
    void reconnectStopsWhenBindingChangesDuringReconciliation();
    void reconnectAfterUnlinkResumesSafePendingWork();
    void changedIdentityDoesNotRunReconnectActions();
    void moveRebindsCredentialWithoutMovingCanonicalOrQueuedState();
    void failedMovePreservesSourceBindingAndCredential();
    void adoptPrivateStateMovesOwnerJournalsAndIsIdempotent();
    void adoptedPendingExportNeedsDestinationReview();
    void adoptPrivateStateRecoversCredentialSaveInterruption();
    void movePreservesDestinationScrobblePreference();
    void movingConnectionReservesAccountBeforeReleasingSource();
    void interruptedMoveResumesFromTransferPendingState();
    void disconnectCancelIsNoOpAndKeepPausedIsLocalOnly();
    void credentialClearFailureDoesNotPartiallyDisconnect();
    void directDisconnectRefusesInflightScrobbleUntilProtectiveCloseAndReconnect();
    void disconnectFailsClosedWhenQueueJournalIsUnreadable();
    void partialDiscardLeavesPausedWorkAndSupportsCleanupRetry();
    void disconnectRejectsInterruptedTransferReservation();
    void removeImportedHistoryIsSourceOnlyAndSuppressesReimport();
    void profileRemovalDeletesTrackerPrivateStateOnly();
    void profileRemovalUsesSupportedVaultNamespace();
};

void TrackerLifecycleTest::reconnectPullsThenReconcilesThenResumes()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    TrackerConnectionStore connections(profile);
    QVERIFY(connections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        4, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    QStringList order;
    const TrackerReconnectActions actions{
        [&](const TrackerConnection &connection, QString *) {
            if (connection.connectionGeneration != 4)
                return false;
            order.append(QStringLiteral("pull"));
            return true;
        },
        [&](const TrackerConnection &, QString *) {
            order.append(QStringLiteral("reconcile"));
            return true;
        },
        [&](const TrackerConnection &, QString *) {
            order.append(QStringLiteral("resume"));
            return true;
        }};

    QVERIFY(TrackerLifecycleCoordinator::reconnect(
        profile, TrackerProviderId::Simkl, QStringLiteral("12345"), actions));
    QCOMPARE(order, QStringList({QStringLiteral("pull"), QStringLiteral("reconcile"),
                                 QStringLiteral("resume")}));
}

void TrackerLifecycleTest::reconnectStopsWhenBindingChangesDuringPull()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    TrackerConnectionStore initial(profile);
    QVERIFY(initial.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        4, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));

    bool reconcileCalled = false;
    bool resumeCalled = false;
    const TrackerReconnectActions actions{
        [&](const TrackerConnection &connection, QString *error) {
            TrackerConnectionStore changed(profile);
            return changed.setDisconnected(TrackerProviderId::Simkl,
                connection.remoteAccountId, connection.connectionGeneration, error);
        },
        [&](const TrackerConnection &, QString *) { reconcileCalled = true; return true; },
        [&](const TrackerConnection &, QString *) { resumeCalled = true; return true; }};

    QString error;
    QVERIFY(!TrackerLifecycleCoordinator::reconnect(
        profile, TrackerProviderId::Simkl, QStringLiteral("12345"), actions, &error));
    QVERIFY(error.contains(QStringLiteral("changed")));
    QVERIFY(!reconcileCalled);
    QVERIFY(!resumeCalled);
}

void TrackerLifecycleTest::reconnectStopsWhenBindingChangesDuringReconciliation()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    TrackerConnectionStore initial(profile);
    QVERIFY(initial.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        4, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));

    bool resumeCalled = false;
    const TrackerReconnectActions actions{
        [](const TrackerConnection &, QString *) { return true; },
        [&](const TrackerConnection &connection, QString *error) {
            TrackerConnectionStore changed(profile);
            return changed.setDisconnected(TrackerProviderId::Simkl,
                connection.remoteAccountId, connection.connectionGeneration, error);
        },
        [&](const TrackerConnection &, QString *) { resumeCalled = true; return true; }};

    QString error;
    QVERIFY(!TrackerLifecycleCoordinator::reconnect(
        profile, TrackerProviderId::Simkl, QStringLiteral("12345"), actions, &error));
    QVERIFY(error.contains(QStringLiteral("changed")));
    QVERIFY(!resumeCalled);
}

void TrackerLifecycleTest::reconnectAfterUnlinkResumesSafePendingWork()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    constexpr auto accountId = "12345";
    constexpr quint64 oldGeneration = 8;
    constexpr quint64 newGeneration = 9;
    const TrackerProviderCapabilities capabilities =
        TrackerProviderCapability::ReadHistory | TrackerProviderCapability::ReadProgress
        | TrackerProviderCapability::WriteProgress | TrackerProviderCapability::Scrobble;
    TrackerConnectionStore connections(profile);
    QVERIFY(connections.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        oldGeneration, 1000, capabilities, TrackerConnectionState::Connected}));

    TrackerMappingStore mappings(profile);
    const auto title = frieren();
    const QString remoteMediaId = QStringLiteral("simkl-frieren");
    QVERIFY(mappings.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
                             remoteMediaId}, title,
                            TrackerMappingProvenance::UserConfirmed));
    FakeDeliverySource source;
    source.facts = {reconnectProgressFact()};
    const TrackerRemoteDeliverySnapshot remoteSnapshot{
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), oldGeneration,
        QStringLiteral("reconnect-snapshot"), 1500, true,
        {{remoteMediaId, TrackerDeliveryFactKind::Progress, {}, false, false,
          QStringLiteral("absent"), {}}}};
    TrackerDeliveryStore delivery(profile, &mappings, &connections);
    const auto preview = delivery.createExportPreview(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), oldGeneration,
        source.facts, remoteSnapshot, &source);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.size(), 1);
    QVERIFY(preview->items.first().eligible);
    QVERIFY(delivery.confirmExport(preview->previewId,
        {preview->items.first().itemId}, remoteSnapshot, &source, 1600));
    QCOMPARE(delivery.operations().size(), 1);
    const QString deliveryId = delivery.operations().first().operationId;

    TrackerScrobbleStore scrobble(profile);
    QVERIFY(scrobble.setEnabled(TrackerProviderId::Simkl,
                                QString::fromLatin1(accountId), true));
    TrackerScrobbleIntent staleStart;
    staleStart.operationId = QStringLiteral("offline-playback-start");
    staleStart.providerId = TrackerProviderId::Simkl;
    staleStart.remoteAccountId = QString::fromLatin1(accountId);
    staleStart.connectionGeneration = oldGeneration;
    staleStart.mappingRevision = 1;
    staleStart.canonicalMediaId = QStringLiteral("movie:offline-film");
    staleStart.remoteMediaId = QStringLiteral("simkl-offline-film");
    staleStart.playbackSessionId = QStringLiteral("offline-session");
    staleStart.playbackGeneration = 2;
    staleStart.transitionSequence = 1;
    staleStart.action = TrackerScrobbleAction::Start;
    staleStart.createdAtMs = 1550;
    const QString staleStartId = staleStart.operationId;
    QVERIFY(scrobble.recordIntent(staleStart));
    QVERIFY(scrobble.deferWithoutAttempt(
        staleStartId, TrackerScrobbleReason::ProviderUnavailable));

    TrackerScrobbleIntent confirmedStart = staleStart;
    confirmedStart.operationId = QStringLiteral("confirmed-playback-start");
    confirmedStart.playbackSessionId = QStringLiteral("confirmed-session");
    confirmedStart.createdAtMs = 1580;
    QVERIFY(scrobble.recordIntent(confirmedStart));
    QVERIFY(scrobble.markDelivering(confirmedStart.operationId));
    QVERIFY(scrobble.recordAttemptResult(
        confirmedStart.operationId, TrackerScrobbleAttemptResult::Succeeded));

    TrackerScrobbleIntent intent = staleStart;
    intent.operationId = QStringLiteral("offline-playback-close");
    intent.playbackSessionId = confirmedStart.playbackSessionId;
    intent.transitionSequence = 2;
    intent.action = TrackerScrobbleAction::Stop;
    intent.closesSession = true;
    intent.createdAtMs = 1600;
    const QString scrobbleId = intent.operationId;
    QVERIFY(scrobble.recordIntent(intent));
    QVERIFY(scrobble.markDelivering(scrobbleId));
    QVERIFY(scrobble.recordAttemptResult(
        scrobbleId, TrackerScrobbleAttemptResult::KnownNotApplied));

    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(profile.profileId(), QString::fromLatin1(accountId))));
    QVERIFY(TrackerLifecycleCoordinator::disconnect(profile, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::KeepPaused, vault));
    QCOMPARE(TrackerDeliveryStore(profile, &mappings, &connections)
                 .operations().first().connectionGeneration, oldGeneration);

    QVERIFY(vault.saveAndVerify(credential(profile.profileId(), QString::fromLatin1(accountId))));
    TrackerConnectionStore reauthenticated(profile);
    QVERIFY(reauthenticated.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        newGeneration, 2000, capabilities, TrackerConnectionState::Connected}));

    QStringList order;
    QList<TrackerDeliveryOperation> readyAtResume;
    QList<TrackerDeliveryOperation> operationsAtResume;
    bool exportEnabledAtResume = false;
    quint64 scrobbleGenerationAtResume = 0;
    QString scrobbleIdAtResume;
    TrackerScrobbleState scrobbleStateAtResume = TrackerScrobbleState::NeedsAttention;
    int scrobbleAttemptCountAtResume = 0;
    const TrackerReconnectActions actions{
        [&](const TrackerConnection &connection, QString *) {
            order.append(QStringLiteral("pull"));
            return connection.connectionGeneration == newGeneration;
        },
        [&](const TrackerConnection &connection, QString *) {
            order.append(QStringLiteral("reconcile"));
            return connection.connectionGeneration == newGeneration;
        },
        [&](const TrackerConnection &connection, QString *) {
            order.append(QStringLiteral("resume"));
            TrackerMappingStore currentMappings(profile);
            TrackerConnectionStore currentConnections(profile);
            TrackerDeliveryStore currentDelivery(profile, &currentMappings,
                                                 &currentConnections);
            readyAtResume = currentDelivery.readyOperations(2500);
            operationsAtResume = currentDelivery.operations();
            exportEnabledAtResume = currentDelivery.providerSendEnabled(
                TrackerProviderId::Simkl, QString::fromLatin1(accountId));
            TrackerScrobbleStore currentScrobble(profile);
            for (const TrackerScrobbleIntent &currentIntent : currentScrobble.intents()) {
                if (currentIntent.playbackSessionId == intent.playbackSessionId
                    && currentIntent.closesSession) {
                    scrobbleGenerationAtResume = currentIntent.connectionGeneration;
                    scrobbleIdAtResume = currentIntent.operationId;
                    scrobbleStateAtResume = currentIntent.state;
                    scrobbleAttemptCountAtResume = currentIntent.attemptCount;
                    break;
                }
            }
            return connection.connectionGeneration == newGeneration;
        }};

    QVERIFY(TrackerLifecycleCoordinator::reconnect(
        profile, TrackerProviderId::Simkl, QString::fromLatin1(accountId), actions));
    QCOMPARE(order, QStringList({QStringLiteral("pull"), QStringLiteral("reconcile"),
                                 QStringLiteral("resume")}));
    QCOMPARE(operationsAtResume.size(), 1);
    QCOMPARE(operationsAtResume.first().state, TrackerDeliveryState::Pending);
    QCOMPARE(operationsAtResume.first().connectionGeneration, newGeneration);
    QVERIFY(exportEnabledAtResume);
    QCOMPARE(readyAtResume.size(), 1);
    QVERIFY(readyAtResume.first().operationId != deliveryId);
    QCOMPARE(readyAtResume.first().connectionGeneration, newGeneration);
    QCOMPARE(scrobbleGenerationAtResume, newGeneration);
    QVERIFY(scrobbleIdAtResume != scrobbleId);
    QCOMPARE(scrobbleStateAtResume, TrackerScrobbleState::Pending);
    QCOMPARE(scrobbleAttemptCountAtResume, 1);
    const TrackerScrobbleStore finalScrobble(profile);
    const auto staleStartAtResume = finalScrobble.intent(staleStartId);
    QVERIFY(staleStartAtResume.has_value());
    QCOMPARE(staleStartAtResume->state, TrackerScrobbleState::Superseded);
    QCOMPARE(staleStartAtResume->reason, TrackerScrobbleReason::StalePlayback);
    QCOMPARE(staleStartAtResume->connectionGeneration, oldGeneration);
}

void TrackerLifecycleTest::changedIdentityDoesNotRunReconnectActions()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    TrackerConnectionStore connections(profile);
    QVERIFY(connections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        1, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    int calls = 0;
    const TrackerReconnectActions actions{
        [&](const TrackerConnection &, QString *) { ++calls; return true; },
        [&](const TrackerConnection &, QString *) { ++calls; return true; },
        [&](const TrackerConnection &, QString *) { ++calls; return true; }};

    QString error;
    QVERIFY(!TrackerLifecycleCoordinator::reconnect(
        profile, TrackerProviderId::Simkl, QStringLiteral("54321"), actions, &error));
    QVERIFY(error.contains(QStringLiteral("Change account")));
    QCOMPARE(calls, 0);
}

void TrackerLifecycleTest::moveRebindsCredentialWithoutMovingCanonicalOrQueuedState()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths source = ProfilePaths::localOnly(root.path());
    const auto destination = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(destination.has_value());
    TrackerConnectionStore sourceConnections(source);
    QVERIFY(sourceConnections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        4, 1000, TrackerProviderCapability::ReadHistory
            | TrackerProviderCapability::WriteProgress,
        TrackerConnectionState::Connected}));
    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(source.profileId(), QStringLiteral("12345"))));

    TrackerMappingStore sourceMappings(source);
    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, QStringLiteral("12345"),
                                       QStringLiteral("simkl-progress-1")};
    QVERIFY(sourceMappings.upsert(remote, frieren(),
                                  TrackerMappingProvenance::UserConfirmed));
    FakeDeliverySource sourceFacts;
    sourceFacts.facts = {reconnectProgressFact()};
    TrackerDeliveryStore sourceDelivery(source, &sourceMappings, &sourceConnections);
    const TrackerRemoteDeliverySnapshot sourceRemoteSnapshot{
        TrackerProviderId::Simkl, QStringLiteral("12345"), 4,
        QStringLiteral("move-export-snapshot"), 1500, true,
        {{QStringLiteral("simkl-progress-1"), TrackerDeliveryFactKind::Progress,
          {}, false, false, QStringLiteral("absent"), {}}}};
    const auto sourceExportPreview = sourceDelivery.createExportPreview(
        TrackerProviderId::Simkl, QStringLiteral("12345"), 4,
        sourceFacts.facts, sourceRemoteSnapshot, &sourceFacts);
    QVERIFY(sourceExportPreview.has_value());
    QVERIFY(sourceDelivery.confirmExport(sourceExportPreview->previewId,
        {sourceExportPreview->items.first().itemId}, sourceRemoteSnapshot,
        &sourceFacts, 1600));
    QVERIFY(sourceDelivery.hasFirstExportConsent(
        TrackerProviderId::Simkl, QStringLiteral("12345")));
    QVERIFY(sourceDelivery.providerSendEnabled(
        TrackerProviderId::Simkl, QStringLiteral("12345")));

    QVERIFY(writeFile(source.historyIniPath(), QByteArrayLiteral("native-history")));
    QVERIFY(writeFile(source.activityDbPath(), QByteArrayLiteral("native-activity")));
    TrackerScrobbleStore sourceScrobble(source);
    QVERIFY(sourceScrobble.setEnabled(TrackerProviderId::Simkl, QStringLiteral("12345"), true));
    TrackerScrobbleIntent oldIntent;
    oldIntent.operationId = QStringLiteral("source-only-intent");
    oldIntent.providerId = TrackerProviderId::Simkl;
    oldIntent.remoteAccountId = QStringLiteral("12345");
    oldIntent.connectionGeneration = 4;
    oldIntent.mappingRevision = 1;
    oldIntent.canonicalMediaId = QStringLiteral("movie:source-only");
    oldIntent.remoteMediaId = QStringLiteral("752138");
    oldIntent.playbackSessionId = QStringLiteral("source-session");
    oldIntent.playbackGeneration = 1;
    oldIntent.transitionSequence = 1;
    oldIntent.action = TrackerScrobbleAction::Start;
    oldIntent.createdAtMs = 1500;
    QVERIFY(sourceScrobble.recordIntent(oldIntent));
    const QByteArray historyBefore = readFile(source.historyIniPath());
    const QByteArray activityBefore = readFile(source.activityDbPath());

    QVERIFY(TrackerLifecycleCoordinator::moveConnection(
        source, *destination, TrackerProviderId::Simkl, vault, 2000));
    TrackerConnectionStore sourceAfter(source);
    TrackerConnectionStore destinationAfter(*destination);
    QCOMPARE(sourceAfter.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);
    const auto moved = destinationAfter.connection(TrackerProviderId::Simkl);
    QVERIFY(moved.has_value());
    QCOMPARE(moved->state, TrackerConnectionState::Connected);
    QCOMPARE(moved->remoteAccountId, QStringLiteral("12345"));
    QCOMPARE(moved->connectionGeneration, quint64(5));
    QVERIFY(!vault.loadForProfile(source.profileId(), TrackerProviderId::Simkl));
    QCOMPARE(vault.loadForProfile(destination->profileId(), TrackerProviderId::Simkl)
                 ->slot.remoteAccountId, QStringLiteral("12345"));
    TrackerMappingStore destinationMappings(*destination);
    TrackerConnectionStore destinationConnections(*destination);
    TrackerDeliveryStore destinationDelivery(
        *destination, &destinationMappings, &destinationConnections);
    QVERIFY(!destinationDelivery.hasFirstExportConsent(
        TrackerProviderId::Simkl, QStringLiteral("12345")));
    QVERIFY(!destinationDelivery.providerSendEnabled(
        TrackerProviderId::Simkl, QStringLiteral("12345")));
    QCOMPARE(readFile(source.historyIniPath()), historyBefore);
    QCOMPARE(readFile(source.activityDbPath()), activityBefore);
    TrackerScrobbleStore sourceScrobbleAfter(source);
    TrackerScrobbleStore destinationScrobbleAfter(*destination);
    QVERIFY(!sourceScrobbleAfter.enabled(TrackerProviderId::Simkl, QStringLiteral("12345")));
    QVERIFY(!destinationScrobbleAfter.enabled(TrackerProviderId::Simkl, QStringLiteral("12345")));
    QCOMPARE(sourceScrobbleAfter.intents().size(), 1);
    QVERIFY(destinationScrobbleAfter.intents().isEmpty());
}

void TrackerLifecycleTest::failedMovePreservesSourceBindingAndCredential()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths source = ProfilePaths::localOnly(root.path());
    const auto destination = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(destination.has_value());
    TrackerConnectionStore sourceConnections(source);
    QVERIFY(sourceConnections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        1, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(source.profileId(), QStringLiteral("12345"))));
    vault.failSaveForProfile = destination->profileId();

    QVERIFY(!TrackerLifecycleCoordinator::moveConnection(
        source, *destination, TrackerProviderId::Simkl, vault, 2000));
    TrackerConnectionStore sourceAfter(source);
    QCOMPARE(sourceAfter.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    QVERIFY(vault.loadForProfile(source.profileId(), TrackerProviderId::Simkl).has_value());
    QVERIFY(!TrackerConnectionStore(*destination).connection(TrackerProviderId::Simkl).has_value());
}

void TrackerLifecycleTest::adoptPrivateStateMovesOwnerJournalsAndIsIdempotent()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths source = ProfilePaths::localOnly(root.path());
    const auto destination = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(destination.has_value());
    constexpr auto accountId = "12345";
    constexpr quint64 sourceGeneration = 4;

    TrackerConnectionStore sourceConnections(source);
    QVERIFY(sourceConnections.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        sourceGeneration, 1000,
        TrackerProviderCapability::ReadHistory | TrackerProviderCapability::ReadProgress
            | TrackerProviderCapability::WriteProgress | TrackerProviderCapability::WriteCompletion
            | TrackerProviderCapability::Scrobble,
        TrackerConnectionState::Connected}));
    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(source.profileId(), QString::fromLatin1(accountId))));

    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl,
        QString::fromLatin1(accountId), QStringLiteral("simkl-title-1")};
    TrackerMappingStore sourceMappings(source);
    QVERIFY(sourceMappings.upsert(remote, frieren(),
                                  TrackerMappingProvenance::UserConfirmed));
    const auto mapping = sourceMappings.mapping(remote);
    QVERIFY(mapping.has_value());
    TrackerHistoryEvidenceStore sourceEvidence(source, &sourceMappings);
    QVERIFY(sourceEvidence.record(importedEvidence(*mapping)));

    TrackerImportStore sourceImports(source, &sourceMappings, &sourceConnections);
    const TrackerImportRemoteItem importItem{
        QStringLiteral("simkl-import-item-1"), remote, mapping, 3, false,
        true, false, false, std::nullopt};
    const TrackerImportBatchDraft importDraft{
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), sourceGeneration,
        QStringLiteral("import-snapshot-1"), QStringLiteral("import-cursor-1"),
        true, true, {importItem}};
    const auto importPreview = sourceImports.createPreview(importDraft);
    QVERIFY(importPreview.has_value());

    FakeDeliverySource deliverySource;
    const TrackerDeliveryFact fact = reconnectProgressFact();
    deliverySource.facts.append(fact);
    TrackerDeliveryStore sourceDelivery(source, &sourceMappings, &sourceConnections);
    const TrackerRemoteDeliverySnapshot remoteSnapshot{
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), sourceGeneration,
        QStringLiteral("export-snapshot-1"), 1500, true,
        {{QStringLiteral("simkl-title-1"), TrackerDeliveryFactKind::Progress,
          {}, false, false, QStringLiteral("absent"), {}}}};
    const auto exportPreview = sourceDelivery.createExportPreview(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), sourceGeneration,
        {fact}, remoteSnapshot, &deliverySource);
    QVERIFY(exportPreview.has_value());
    QVERIFY(sourceDelivery.confirmExport(exportPreview->previewId,
        {exportPreview->items.first().itemId}, remoteSnapshot, &deliverySource, 1600));
    const auto queuedId = sourceDelivery.operations().first().operationId;
    QVERIFY(sourceDelivery.markDelivering(queuedId, &deliverySource, 1700));

    TrackerScrobbleStore sourceScrobble(source);
    QVERIFY(sourceScrobble.setEnabled(TrackerProviderId::Simkl,
                                      QString::fromLatin1(accountId), true));
    TrackerScrobbleIntent intent;
    intent.operationId = QStringLiteral("source-scrobble-intent");
    intent.providerId = TrackerProviderId::Simkl;
    intent.remoteAccountId = QString::fromLatin1(accountId);
    intent.connectionGeneration = sourceGeneration;
    intent.mappingRevision = mapping->revision;
    intent.canonicalMediaId = mapping->canonical.canonicalMediaId;
    intent.remoteMediaId = remote.remoteMediaId;
    intent.playbackSessionId = QStringLiteral("source-playback-session");
    intent.playbackGeneration = 1;
    intent.transitionSequence = 1;
    intent.action = TrackerScrobbleAction::Start;
    intent.progressHundredths = 2500;
    intent.createdAtMs = 1750;
    QVERIFY(sourceScrobble.recordIntent(intent));
    QVERIFY(sourceScrobble.markDelivering(intent.operationId));

    QVERIFY(writeFile(source.historyIniPath(), QByteArrayLiteral("source-native-history")));
    QVERIFY(writeFile(source.activityDbPath(), QByteArrayLiteral("source-native-activity")));
    QVERIFY(writeFile(destination->historyIniPath(), QByteArrayLiteral("account-native-history")));
    QVERIFY(writeFile(destination->activityDbPath(), QByteArrayLiteral("account-native-activity")));
    const QByteArray sourceHistory = readFile(source.historyIniPath());
    const QByteArray sourceActivity = readFile(source.activityDbPath());
    const QByteArray destinationHistory = readFile(destination->historyIniPath());
    const QByteArray destinationActivity = readFile(destination->activityDbPath());

    QString adoptionError;
    QVERIFY2(TrackerLifecycleCoordinator::adoptPrivateState(
                 source, *destination, vault, 2000, &adoptionError),
             qPrintable(adoptionError));

    const auto moved = TrackerConnectionStore(*destination)
                           .connection(TrackerProviderId::Simkl);
    QVERIFY(moved.has_value());
    QCOMPARE(moved->state, TrackerConnectionState::Connected);
    QCOMPARE(moved->remoteAccountId, QString::fromLatin1(accountId));
    QCOMPARE(moved->connectionGeneration, quint64(sourceGeneration + 1));
    QCOMPARE(TrackerConnectionStore(source)
                 .connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);
    QVERIFY(!vault.loadForProfile(source.profileId(), TrackerProviderId::Simkl));
    QVERIFY(vault.loadForProfile(destination->profileId(), TrackerProviderId::Simkl)
                .has_value());

    TrackerMappingStore destinationMappings(*destination);
    QVERIFY(destinationMappings.mapping(remote).has_value());
    TrackerHistoryEvidenceStore destinationEvidence(*destination, &destinationMappings);
    QCOMPARE(destinationEvidence.contributions().size(), 1);
    TrackerConnectionStore destinationConnections(*destination);
    TrackerImportStore destinationImports(
        *destination, &destinationMappings, &destinationConnections);
    QCOMPARE(destinationImports.batches().size(), 1);
    QCOMPARE(destinationImports.batches().first().batchId, importPreview->batchId);
    TrackerDeliveryStore destinationDelivery(
        *destination, &destinationMappings, &destinationConnections);
    QCOMPARE(destinationDelivery.operations().size(), 1);
    QCOMPARE(destinationDelivery.operations().first().operationId, queuedId);
    QCOMPARE(destinationDelivery.operations().first().remoteAccountId,
             QString::fromLatin1(accountId));
    QCOMPARE(destinationDelivery.operations().first().connectionGeneration,
             sourceGeneration);
    QCOMPARE(destinationDelivery.operations().first().state,
             TrackerDeliveryState::UnknownOutcome);
    QCOMPARE(destinationDelivery.operations().first().reason,
             TrackerDeliveryReason::AcknowledgementLost);
    QVERIFY(!destinationDelivery.hasFirstExportConsent(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId)));
    QVERIFY(!destinationDelivery.providerSendEnabled(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId)));
    QString consentError;
    QVERIFY(!destinationDelivery.setProviderSendEnabled(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), true, &consentError));
    QVERIFY(consentError.contains(QStringLiteral("First local-export review")));
    FakeDeliverySource destinationFacts;
    destinationFacts.facts = {fact};
    int recoveredDeliveryCount = -1;
    QVERIFY(destinationDelivery.recoverSourceGap(
        &destinationFacts, &recoveredDeliveryCount));
    QCOMPARE(recoveredDeliveryCount, 0);
    QCOMPARE(destinationDelivery.operations().size(), 1);
    const TrackerRemoteDeliverySnapshot destinationRemoteSnapshot{
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), sourceGeneration + 1,
        QStringLiteral("destination-export-snapshot"), 2100, true,
        {{QStringLiteral("simkl-title-1"), TrackerDeliveryFactKind::Progress,
          {}, false, false, QStringLiteral("absent"), {}}}};
    const auto destinationExportPreview = destinationDelivery.createExportPreview(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), sourceGeneration + 1,
        {fact}, destinationRemoteSnapshot, &destinationFacts);
    QVERIFY(destinationExportPreview.has_value());
    QVERIFY(destinationDelivery.confirmExport(destinationExportPreview->previewId,
        {destinationExportPreview->items.first().itemId}, destinationRemoteSnapshot,
        &destinationFacts, 2200));
    QVERIFY(destinationDelivery.hasFirstExportConsent(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId)));
    QVERIFY(destinationDelivery.providerSendEnabled(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId)));
    TrackerScrobbleStore destinationScrobble(*destination);
    QVERIFY(!destinationScrobble.enabled(TrackerProviderId::Simkl,
                                         QString::fromLatin1(accountId)));
    QCOMPARE(destinationScrobble.intents().size(), 1);
    QCOMPARE(destinationScrobble.intents().first().remoteAccountId,
             QString::fromLatin1(accountId));
    QCOMPARE(destinationScrobble.intents().first().connectionGeneration,
             sourceGeneration);
    QCOMPARE(destinationScrobble.intents().first().state,
             TrackerScrobbleState::UnknownOutcome);
    QCOMPARE(destinationScrobble.intents().first().reason,
             TrackerScrobbleReason::AcknowledgementLost);
    QVERIFY(readFile(destination->profileRoot()
                     + QStringLiteral("/tracker-private-adoption.json"))
                .contains(QByteArrayLiteral("\"phase\":\"verified\"")));
    QCOMPARE(readFile(source.historyIniPath()), sourceHistory);
    QCOMPARE(readFile(source.activityDbPath()), sourceActivity);
    QCOMPARE(readFile(destination->historyIniPath()), destinationHistory);
    QCOMPARE(readFile(destination->activityDbPath()), destinationActivity);
}

void TrackerLifecycleTest::adoptedPendingExportNeedsDestinationReview()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths source = ProfilePaths::localOnly(root.path());
    const auto destination = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(destination.has_value());
    constexpr auto accountId = "12345";
    const auto capabilities = TrackerProviderCapability::ReadHistory
        | TrackerProviderCapability::ReadProgress | TrackerProviderCapability::WriteProgress;

    TrackerConnectionStore sourceConnections(source);
    TrackerConnectionStore destinationConnections(*destination);
    QVERIFY(destinationConnections.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        2, 1000, capabilities, TrackerConnectionState::Connected}));

    const TrackerRemoteMediaKey sourceRemote{TrackerProviderId::Simkl,
        QString::fromLatin1(accountId), QStringLiteral("simkl-source-progress")};
    const TrackerRemoteMediaKey destinationRemote{TrackerProviderId::Simkl,
        QString::fromLatin1(accountId), QStringLiteral("simkl-destination-progress")};
    const TrackerCanonicalTitleCandidate destinationTitle{
        QStringLiteral("ct1:49f10000-0000-4000-8000-000000000002"),
        QStringLiteral("movie"), QStringLiteral("movie:destination-title"),
        QStringLiteral("Destination title")};
    TrackerMappingStore sourceMappings(source);
    TrackerMappingStore destinationMappings(*destination);
    QVERIFY(destinationMappings.upsert(destinationRemote, destinationTitle,
                                       TrackerMappingProvenance::UserConfirmed));

    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(source.profileId(), QString::fromLatin1(accountId))));
    FakeDeliverySource destinationFacts;
    const TrackerDeliveryFact destinationFact{
        destinationTitle.canonicalMediaId, destinationTitle.historyKind,
        destinationTitle.historyId, TrackerDeliveryFactKind::Progress, 3, {}, 1,
        QStringLiteral("destination-native-fingerprint"),
        TrackerDeliveryOrigin::NativeLocal, TrackerMediaDomain::Movie};
    destinationFacts.facts = {destinationFact};

    TrackerDeliveryStore destinationDelivery(
        *destination, &destinationMappings, &destinationConnections);
    const TrackerRemoteDeliverySnapshot destinationSnapshot{
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), 2,
        QStringLiteral("destination-export"), 1700, true,
        {{destinationRemote.remoteMediaId, TrackerDeliveryFactKind::Progress,
          {}, false, false, QStringLiteral("absent"), {}}}};
    const auto destinationPreview = destinationDelivery.createExportPreview(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), 2,
        destinationFacts.facts, destinationSnapshot, &destinationFacts);
    QVERIFY(destinationPreview.has_value());
    QVERIFY(destinationDelivery.confirmExport(destinationPreview->previewId,
        {destinationPreview->items.first().itemId}, destinationSnapshot,
        &destinationFacts, 1800));
    QCOMPARE(destinationDelivery.discardKnownUnsent(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId)), 1);
    QVERIFY(destinationConnections.setDisconnected(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), 2));

    QVERIFY(sourceConnections.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        4, 1000, capabilities, TrackerConnectionState::Connected}));
    QVERIFY(sourceMappings.upsert(sourceRemote, frieren(),
                                  TrackerMappingProvenance::UserConfirmed));
    FakeDeliverySource sourceFacts;
    sourceFacts.facts = {reconnectProgressFact()};
    TrackerDeliveryStore sourceDelivery(source, &sourceMappings, &sourceConnections);
    const TrackerRemoteDeliverySnapshot sourceSnapshot{
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), 4,
        QStringLiteral("source-export"), 1500, true,
        {{sourceRemote.remoteMediaId, TrackerDeliveryFactKind::Progress,
          {}, false, false, QStringLiteral("absent"), {}}}};
    const auto sourcePreview = sourceDelivery.createExportPreview(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), 4,
        sourceFacts.facts, sourceSnapshot, &sourceFacts);
    QVERIFY(sourcePreview.has_value());
    QVERIFY(sourceDelivery.confirmExport(sourcePreview->previewId,
        {sourcePreview->items.first().itemId}, sourceSnapshot, &sourceFacts, 1600));
    const QString sourceOperationId = sourceDelivery.operations().first().operationId;

    QString adoptionError;
    QVERIFY2(TrackerLifecycleCoordinator::adoptPrivateState(
                 source, *destination, vault, 2000, &adoptionError),
             qPrintable(adoptionError));

    TrackerMappingStore adoptedMappings(*destination);
    TrackerConnectionStore adoptedConnections(*destination);
    const auto adoptedConnection = adoptedConnections.connection(TrackerProviderId::Simkl);
    QVERIFY(adoptedConnection.has_value());
    QCOMPARE(adoptedConnection->state, TrackerConnectionState::Connected);
    QCOMPARE(adoptedConnection->connectionGeneration, quint64(5));
    TrackerDeliveryStore adoptedDelivery(
        *destination, &adoptedMappings, &adoptedConnections);
    QVERIFY(adoptedDelivery.hasFirstExportConsent(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId)));
    QVERIFY(adoptedDelivery.providerSendEnabled(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId)));
    const auto adoptedReceipt = adoptedDelivery.operation(sourceOperationId);
    QVERIFY(adoptedReceipt.has_value());
    QCOMPARE(adoptedReceipt->state, TrackerDeliveryState::NeedsAttention);
    QCOMPARE(adoptedReceipt->reason, TrackerDeliveryReason::DestinationReviewRequired);
    QVERIFY(adoptedReceipt->adoptedReceipt);

    destinationFacts.facts.append(sourceFacts.facts.first());
    QVERIFY(adoptedDelivery.resumeKnownUnsentAfterReconnect(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        adoptedConnection->connectionGeneration));
    QVERIFY(adoptedDelivery.readyOperations(3000).isEmpty());
    QVERIFY(!adoptedDelivery.markDelivering(
        sourceOperationId, &destinationFacts, 3000));

    const TrackerRemoteDeliverySnapshot reviewedSnapshot{
        TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        adoptedConnection->connectionGeneration, QStringLiteral("destination-review"),
        3100, true,
        {{sourceRemote.remoteMediaId, TrackerDeliveryFactKind::Progress,
          {}, false, false, QStringLiteral("absent"), {}},
         {destinationRemote.remoteMediaId, TrackerDeliveryFactKind::Progress,
          {}, true, true, QStringLiteral("destination-current"),
          QStringLiteral("Destination already matches")}}};
    QString reviewError;
    const auto reviewedPreview = adoptedDelivery.createExportPreview(
        TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        adoptedConnection->connectionGeneration,
        destinationFacts.facts, reviewedSnapshot, &destinationFacts, &reviewError);
    QVERIFY2(reviewedPreview.has_value(), qPrintable(reviewError));
    const auto sourceItem = std::find_if(reviewedPreview->items.cbegin(),
        reviewedPreview->items.cend(), [&sourceFacts](const auto &item) {
            return item.fact.canonicalMediaId == sourceFacts.facts.first().canonicalMediaId;
        });
    QVERIFY(sourceItem != reviewedPreview->items.cend());
    QVERIFY(adoptedDelivery.confirmExport(reviewedPreview->previewId,
        {sourceItem->itemId}, reviewedSnapshot, &destinationFacts, 3200));
    const auto destinationOwnedReady = adoptedDelivery.readyOperations(3300);
    QCOMPARE(destinationOwnedReady.size(), 1);
    QVERIFY(destinationOwnedReady.first().operationId != sourceOperationId);
    QVERIFY(!destinationOwnedReady.first().adoptedReceipt);
    QVERIFY(adoptedDelivery.markDelivering(
        destinationOwnedReady.first().operationId, &destinationFacts, 3300));
}

void TrackerLifecycleTest::adoptPrivateStateRecoversCredentialSaveInterruption()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths source = ProfilePaths::localOnly(root.path());
    const auto destination = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(destination.has_value());
    constexpr auto accountId = "12345";
    TrackerConnectionStore sourceConnections(source);
    QVERIFY(sourceConnections.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        4, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(source.profileId(), QString::fromLatin1(accountId))));
    vault.failSaveForProfile = destination->profileId();

    QString adoptionError;
    QVERIFY(!TrackerLifecycleCoordinator::adoptPrivateState(
        source, *destination, vault, 2000, &adoptionError));
    QVERIFY(!adoptionError.isEmpty());
    QCOMPARE(TrackerConnectionStore(source)
                 .connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);
    const auto pending = TrackerConnectionStore(*destination)
                             .connection(TrackerProviderId::Simkl);
    QVERIFY(pending.has_value());
    QCOMPARE(pending->state, TrackerConnectionState::TransferPending);
    QVERIFY(vault.loadForProfile(source.profileId(), TrackerProviderId::Simkl)
                .has_value());
    QVERIFY(!vault.loadForProfile(destination->profileId(), TrackerProviderId::Simkl));
    const QByteArray pendingCheckpoint = readFile(
        destination->profileRoot() + QStringLiteral("/tracker-private-adoption.json"));
    QVERIFY(pendingCheckpoint.contains(QByteArrayLiteral("\"phase\":\"transferring\"")));
    QVERIFY(!pendingCheckpoint.contains(QByteArrayLiteral("\"phase\":\"verified\"")));

    vault.failSaveForProfile.clear();
    adoptionError.clear();
    QVERIFY2(TrackerLifecycleCoordinator::adoptPrivateState(
                 source, *destination, vault, 2100, &adoptionError),
             qPrintable(adoptionError));
    QCOMPARE(TrackerConnectionStore(*destination)
                 .connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    QVERIFY(!vault.loadForProfile(source.profileId(), TrackerProviderId::Simkl));
    QVERIFY(vault.loadForProfile(destination->profileId(), TrackerProviderId::Simkl)
                .has_value());
}

void TrackerLifecycleTest::movePreservesDestinationScrobblePreference()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths source = ProfilePaths::localOnly(root.path());
    const auto destination = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(destination.has_value());
    TrackerConnectionStore sourceConnections(source);
    QVERIFY(sourceConnections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        4, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(source.profileId(), QStringLiteral("12345"))));
    TrackerScrobbleStore sourceScrobble(source);
    TrackerScrobbleStore destinationScrobble(*destination);
    QVERIFY(sourceScrobble.setEnabled(TrackerProviderId::Simkl,
                                      QStringLiteral("12345"), true));
    QVERIFY(destinationScrobble.setEnabled(TrackerProviderId::Simkl,
                                           QStringLiteral("12345"), false));

    QVERIFY(TrackerLifecycleCoordinator::moveConnection(
        source, *destination, TrackerProviderId::Simkl, vault, 2000));
    QCOMPARE(TrackerConnectionStore(source).connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);
    QCOMPARE(TrackerConnectionStore(*destination)
                 .connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    QVERIFY(!vault.loadForProfile(source.profileId(), TrackerProviderId::Simkl).has_value());
    QVERIFY(!TrackerScrobbleStore(source).enabled(
        TrackerProviderId::Simkl, QStringLiteral("12345")));
    QVERIFY(!TrackerScrobbleStore(*destination).enabled(
        TrackerProviderId::Simkl, QStringLiteral("12345")));
}

void TrackerLifecycleTest::movingConnectionReservesAccountBeforeReleasingSource()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths source = ProfilePaths::localOnly(root.path());
    const auto destination = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    const auto competingProfile = ProfilePaths::account(
        QStringLiteral("22222222-2222-4222-8222-222222222222"), root.path());
    QVERIFY(destination.has_value());
    QVERIFY(competingProfile.has_value());
    TrackerConnectionStore sourceConnections(source);
    QVERIFY(sourceConnections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        4, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    TrackerConnectionStore competingConnections(*competingProfile);
    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(source.profileId(), QStringLiteral("12345"))));
    bool competingClaimSucceeded = false;
    vault.beforeSave = [&](const TrackerCredential &attempted) {
        if (attempted.slot.profileId != destination->profileId())
            return;
        competingClaimSucceeded = competingConnections.upsert({
            TrackerProviderId::Simkl, QStringLiteral("12345"), 1, 1500, {},
            TrackerConnectionState::Connected});
    };

    QVERIFY(TrackerLifecycleCoordinator::moveConnection(
        source, *destination, TrackerProviderId::Simkl, vault, 2000));
    QVERIFY(!competingClaimSucceeded);
    QCOMPARE(TrackerConnectionStore(source).connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);
    QCOMPARE(TrackerConnectionStore(*destination).connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    QVERIFY(!TrackerConnectionStore(*competingProfile)
                 .connection(TrackerProviderId::Simkl).has_value());
}

void TrackerLifecycleTest::interruptedMoveResumesFromTransferPendingState()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths source = ProfilePaths::localOnly(root.path());
    const auto destination = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(destination.has_value());
    TrackerConnectionStore sourceConnections(source);
    TrackerConnectionStore destinationConnections(*destination);
    QVERIFY(sourceConnections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        4, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    QVERIFY(destinationConnections.beginTransferFrom(source, {
        TrackerProviderId::Simkl, QStringLiteral("12345"), 9, 1500,
        TrackerProviderCapability::ReadHistory, TrackerConnectionState::TransferPending}));
    QVERIFY(sourceConnections.setDisconnected(TrackerProviderId::Simkl,
        QStringLiteral("12345"), 4));

    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(source.profileId(), QStringLiteral("12345"))));
    TrackerCredential movedCredential = credential(destination->profileId(), QStringLiteral("12345"));
    QVERIFY(vault.saveAndVerify(movedCredential));
    TrackerScrobbleStore sourceScrobble(source);
    QVERIFY(sourceScrobble.setEnabled(TrackerProviderId::Simkl, QStringLiteral("12345"), true));

    QVERIFY(TrackerLifecycleCoordinator::moveConnection(
        source, *destination, TrackerProviderId::Simkl, vault, 2000));
    QCOMPARE(TrackerConnectionStore(*destination)
                 .connection(TrackerProviderId::Simkl)->connectionGeneration, quint64(9));
    QCOMPARE(TrackerConnectionStore(*destination)
                 .connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    QVERIFY(!vault.loadForProfile(source.profileId(), TrackerProviderId::Simkl));
    QVERIFY(!TrackerScrobbleStore(*destination)
                 .enabled(TrackerProviderId::Simkl, QStringLiteral("12345")));
}

void TrackerLifecycleTest::disconnectCancelIsNoOpAndKeepPausedIsLocalOnly()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    TrackerConnectionStore initial(profile);
    QVERIFY(initial.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        8, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(profile.profileId(), QStringLiteral("12345"))));

    QVERIFY(!TrackerLifecycleCoordinator::disconnect(profile, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::Cancel, vault));
    QCOMPARE(TrackerConnectionStore(profile).connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    QVERIFY(vault.loadForProfile(profile.profileId(), TrackerProviderId::Simkl).has_value());

    QVERIFY(TrackerLifecycleCoordinator::disconnect(profile, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::KeepPaused, vault));
    const auto disconnected = TrackerConnectionStore(profile).connection(TrackerProviderId::Simkl);
    QVERIFY(disconnected.has_value());
    QCOMPARE(disconnected->state, TrackerConnectionState::Disconnected);
    QCOMPARE(disconnected->connectionGeneration, quint64(8));
    QVERIFY(!vault.loadForProfile(profile.profileId(), TrackerProviderId::Simkl));
}

void TrackerLifecycleTest::credentialClearFailureDoesNotPartiallyDisconnect()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    TrackerConnectionStore connections(profile);
    QVERIFY(connections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        3, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    FakeVault vault;
    const TrackerCredential saved = credential(profile.profileId(), QStringLiteral("12345"));
    QVERIFY(vault.saveAndVerify(saved));
    vault.failClear = true;

    QVERIFY(!TrackerLifecycleCoordinator::disconnect(profile, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::KeepPaused, vault));
    QCOMPARE(TrackerConnectionStore(profile).connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    QCOMPARE(vault.loadForProfile(profile.profileId(), TrackerProviderId::Simkl)
                 ->accessToken, saved.accessToken);
    QCOMPARE(vault.clearCount, 1);
}

void TrackerLifecycleTest::directDisconnectRefusesInflightScrobbleUntilProtectiveCloseAndReconnect()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    constexpr auto accountId = "12345";
    constexpr quint64 firstGeneration = 8;
    constexpr quint64 nextGeneration = 9;
    const TrackerProviderCapabilities capabilities = TrackerProviderCapability::Scrobble;
    TrackerConnectionStore connections(profile);
    QVERIFY(connections.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        firstGeneration, 1000, capabilities, TrackerConnectionState::Connected}));
    TrackerMappingStore mappings(profile);
    TrackerDeliveryStore delivery(profile, &mappings, &connections);
    TrackerScrobbleStore scrobble(profile);
    QVERIFY(scrobble.setEnabled(TrackerProviderId::Simkl,
                                QString::fromLatin1(accountId), true));

    TrackerScrobbleIntent start;
    start.operationId = QStringLiteral("in-flight-start");
    start.providerId = TrackerProviderId::Simkl;
    start.remoteAccountId = QString::fromLatin1(accountId);
    start.connectionGeneration = firstGeneration;
    start.mappingRevision = 1;
    start.canonicalMediaId = QStringLiteral("movie:protective-close");
    start.remoteMediaId = QStringLiteral("simkl-protective-close");
    start.playbackSessionId = QStringLiteral("protective-session");
    start.playbackGeneration = 1;
    start.transitionSequence = 1;
    start.action = TrackerScrobbleAction::Start;
    start.progressHundredths = 120;
    start.createdAtMs = 1100;
    QVERIFY(scrobble.recordIntent(start));
    QVERIFY(scrobble.markDelivering(start.operationId));

    TrackerScrobbleIntent close = start;
    close.operationId = QStringLiteral("queued-protective-close");
    close.transitionSequence = 2;
    close.action = TrackerScrobbleAction::Stop;
    close.progressHundredths = 180;
    close.closesSession = true;
    close.createdAtMs = 1200;
    QVERIFY(scrobble.recordIntent(close));

    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(profile.profileId(), QString::fromLatin1(accountId))));
    QString error;
    QVERIFY(!TrackerLifecycleCoordinator::disconnect(profile, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::KeepPaused, vault, connections, mappings, delivery,
        scrobble, &error));
    QVERIFY(error.contains(QStringLiteral("sending")) || error.contains(QStringLiteral("in flight")));
    QCOMPARE(connections.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    QVERIFY(vault.loadForProfile(profile.profileId(), TrackerProviderId::Simkl).has_value());
    QCOMPARE(scrobble.intent(start.operationId)->state, TrackerScrobbleState::Delivering);
    QCOMPARE(scrobble.intent(close.operationId)->state, TrackerScrobbleState::Pending);

    QVERIFY(scrobble.recordAttemptResult(start.operationId,
        TrackerScrobbleAttemptResult::Succeeded));
    QVERIFY(scrobble.markDelivering(close.operationId));
    QVERIFY(scrobble.recordAttemptResult(close.operationId,
        TrackerScrobbleAttemptResult::Succeeded));
    QVERIFY(TrackerLifecycleCoordinator::disconnect(profile, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::KeepPaused, vault, connections, mappings, delivery,
        scrobble));
    QCOMPARE(connections.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);

    QVERIFY(vault.saveAndVerify(credential(profile.profileId(), QString::fromLatin1(accountId))));
    QVERIFY(connections.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        nextGeneration, 2000, capabilities, TrackerConnectionState::Connected}));
    QStringList reconnectOrder;
    const TrackerReconnectActions actions{
        [&](const TrackerConnection &, QString *) {
            reconnectOrder.append(QStringLiteral("pull"));
            return true;
        },
        [&](const TrackerConnection &, QString *) {
            reconnectOrder.append(QStringLiteral("reconcile"));
            return true;
        },
        [&](const TrackerConnection &, QString *) {
            reconnectOrder.append(QStringLiteral("resume"));
            return true;
        }};
    QVERIFY(TrackerLifecycleCoordinator::reconnect(profile, TrackerProviderId::Simkl,
        QString::fromLatin1(accountId), actions, &error));
    QCOMPARE(reconnectOrder, QStringList({QStringLiteral("pull"),
        QStringLiteral("reconcile"), QStringLiteral("resume")}));
    QCOMPARE(scrobble.intent(start.operationId)->state, TrackerScrobbleState::Succeeded);
    QCOMPARE(scrobble.intent(close.operationId)->state, TrackerScrobbleState::Succeeded);
}

void TrackerLifecycleTest::disconnectFailsClosedWhenQueueJournalIsUnreadable()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    constexpr auto accountId = "12345";
    TrackerConnectionStore connections(profile);
    QVERIFY(connections.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        8, 1000, TrackerProviderCapability::Scrobble, TrackerConnectionState::Connected}));
    TrackerMappingStore mappings(profile);
    TrackerDeliveryStore delivery(profile, &mappings, &connections);

    QFile corruptedScrobbleStore(TrackerScrobbleStore::storagePath(profile));
    QVERIFY(corruptedScrobbleStore.open(QIODevice::WriteOnly));
    QCOMPARE(corruptedScrobbleStore.write("not-json"), qint64(8));
    corruptedScrobbleStore.close();
    TrackerScrobbleStore scrobble(profile);
    QVERIFY(!scrobble.healthy());

    FakeVault vault;
    const TrackerCredential saved = credential(profile.profileId(), QString::fromLatin1(accountId));
    QVERIFY(vault.saveAndVerify(saved));
    QString error;
    QVERIFY(!TrackerLifecycleCoordinator::disconnect(profile, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::KeepPaused, vault, connections, mappings, delivery,
        scrobble, &error));
    QCOMPARE(connections.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Connected);
    QCOMPARE(vault.loadForProfile(profile.profileId(), TrackerProviderId::Simkl)
                 ->accessToken, saved.accessToken);
    QCOMPARE(vault.clearCount, 0);
    QVERIFY(!error.isEmpty());
}

void TrackerLifecycleTest::partialDiscardLeavesPausedWorkAndSupportsCleanupRetry()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    constexpr auto accountId = "12345";
    constexpr quint64 generation = 8;
    const TrackerProviderCapabilities capabilities = TrackerProviderCapability::WriteProgress
        | TrackerProviderCapability::Scrobble;
    TrackerConnectionStore connections(profile);
    QVERIFY(connections.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
        generation, 1000, capabilities, TrackerConnectionState::Connected}));
    TrackerMappingStore mappings(profile);
    const QString remoteMediaId = QStringLiteral("simkl-frieren");
    QVERIFY(mappings.upsert({TrackerProviderId::Simkl, QString::fromLatin1(accountId),
                             remoteMediaId}, frieren(),
                            TrackerMappingProvenance::UserConfirmed));
    FakeDeliverySource source;
    source.facts = {reconnectProgressFact()};
    const TrackerRemoteDeliverySnapshot remoteSnapshot{
        TrackerProviderId::Simkl, QString::fromLatin1(accountId), generation,
        QStringLiteral("partial-discard-snapshot"), 1500, true,
        {{remoteMediaId, TrackerDeliveryFactKind::Progress, {}, false, false,
          QStringLiteral("absent"), {}}}};
    TrackerDeliveryStore delivery(profile, &mappings, &connections);
    const auto preview = delivery.createExportPreview(TrackerProviderId::Simkl,
        QString::fromLatin1(accountId), generation, source.facts, remoteSnapshot, &source);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->items.size(), 1);
    QVERIFY(preview->items.first().eligible);
    QVERIFY(delivery.confirmExport(preview->previewId,
        {preview->items.first().itemId}, remoteSnapshot, &source, 1600));
    QCOMPARE(delivery.operations().size(), 1);

    TrackerScrobbleStore scrobble(profile);
    QVERIFY(scrobble.setEnabled(TrackerProviderId::Simkl,
                                QString::fromLatin1(accountId), true));
    TrackerScrobbleIntent queued;
    queued.operationId = QStringLiteral("queued-start-for-partial-discard");
    queued.providerId = TrackerProviderId::Simkl;
    queued.remoteAccountId = QString::fromLatin1(accountId);
    queued.connectionGeneration = generation;
    queued.mappingRevision = 1;
    queued.canonicalMediaId = QStringLiteral("movie:partial-discard");
    queued.remoteMediaId = QStringLiteral("simkl-partial-discard");
    queued.playbackSessionId = QStringLiteral("partial-discard-session");
    queued.playbackGeneration = 1;
    queued.transitionSequence = 1;
    queued.action = TrackerScrobbleAction::Start;
    queued.progressHundredths = 120;
    queued.createdAtMs = 1700;
    QVERIFY(scrobble.recordIntent(queued));

    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(profile.profileId(), QString::fromLatin1(accountId))));
    const QString scrobblePath = TrackerScrobbleStore::storagePath(profile);
    const QString savedScrobblePath = root.path() + QStringLiteral("/scrobble-store.saved");
    QVERIFY(QFile::rename(scrobblePath, savedScrobblePath));
    QVERIFY(QDir().mkdir(scrobblePath));

    QString error;
    QVERIFY(!TrackerLifecycleCoordinator::disconnect(profile, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::DiscardKnownUnsent, vault, connections, mappings,
        delivery, scrobble, &error));
    QCOMPARE(connections.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);
    QVERIFY(!vault.loadForProfile(profile.profileId(), TrackerProviderId::Simkl));
    QVERIFY(delivery.operations().isEmpty());
    QCOMPARE(scrobble.intents().size(), 1);
    QVERIFY(error.contains(QStringLiteral("paused")));

    QVERIFY(QDir(scrobblePath).removeRecursively());
    QVERIFY(QFile::rename(savedScrobblePath, scrobblePath));
    TrackerScrobbleStore recoveredScrobble(profile);
    QCOMPARE(recoveredScrobble.intents().size(), 1);
    QVERIFY(TrackerLifecycleCoordinator::disconnect(profile, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::DiscardKnownUnsent, vault, connections, mappings,
        delivery, recoveredScrobble, &error));
    QCOMPARE(connections.connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::Disconnected);
    QCOMPARE(recoveredScrobble.intent(queued.operationId)->state,
             TrackerScrobbleState::Superseded);
    QCOMPARE(recoveredScrobble.intent(queued.operationId)->reason,
             TrackerScrobbleReason::UserDiscarded);
}

void TrackerLifecycleTest::disconnectRejectsInterruptedTransferReservation()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths source = ProfilePaths::localOnly(root.path());
    const auto destination = ProfilePaths::account(
        QString::fromLatin1(kAccountProfileId), root.path());
    QVERIFY(destination.has_value());
    TrackerConnectionStore sourceConnections(source);
    TrackerConnectionStore destinationConnections(*destination);
    QVERIFY(sourceConnections.upsert({TrackerProviderId::Simkl, QStringLiteral("12345"),
        4, 1000, TrackerProviderCapability::ReadHistory,
        TrackerConnectionState::Connected}));
    QVERIFY(destinationConnections.beginTransferFrom(source, {
        TrackerProviderId::Simkl, QStringLiteral("12345"), 5, 1500,
        TrackerProviderCapability::ReadHistory, TrackerConnectionState::TransferPending}));
    QVERIFY(sourceConnections.setDisconnected(TrackerProviderId::Simkl,
        QStringLiteral("12345"), 4));

    FakeVault vault;
    QVERIFY(vault.saveAndVerify(credential(destination->profileId(), QStringLiteral("12345"))));
    QString error;
    QVERIFY(!TrackerLifecycleCoordinator::disconnect(*destination, TrackerProviderId::Simkl,
        TrackerDisconnectChoice::KeepPaused, vault, &error));
    QVERIFY(error.contains(QStringLiteral("move")) || error.contains(QStringLiteral("transfer")));
    QCOMPARE(TrackerConnectionStore(*destination).connection(TrackerProviderId::Simkl)->state,
             TrackerConnectionState::TransferPending);
    QVERIFY(vault.loadForProfile(destination->profileId(), TrackerProviderId::Simkl).has_value());
    QCOMPARE(vault.clearCount, 0);
}

void TrackerLifecycleTest::removeImportedHistoryIsSourceOnlyAndSuppressesReimport()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    TrackerMappingStore mappings(profile);
    const TrackerRemoteMediaKey remote{TrackerProviderId::Simkl, QStringLiteral("12345"),
                                       QStringLiteral("simkl-title-1")};
    QVERIFY(mappings.upsert(remote, frieren(), TrackerMappingProvenance::ExactProviderIdentity));
    TrackerHistoryEvidenceStore evidence(profile, &mappings);
    const TrackerTitleMapping mapping = *mappings.mapping(remote);
    QVERIFY(evidence.record(importedEvidence(mapping)));
    QVERIFY(writeFile(profile.historyIniPath(), QByteArrayLiteral("native-history")));
    QVERIFY(writeFile(profile.activityDbPath(), QByteArrayLiteral("native-activity")));
    const QByteArray historyBefore = readFile(profile.historyIniPath());
    const QByteArray activityBefore = readFile(profile.activityDbPath());

    QCOMPARE(TrackerLifecycleCoordinator::removeImportedHistory(
        profile, TrackerProviderId::Simkl, QStringLiteral("12345")), 1);
    TrackerHistoryEvidenceStore reopened(profile, &mappings);
    QVERIFY(reopened.contributions().isEmpty());
    QVERIFY(reopened.sourceRemovalSuppressed(TrackerProviderId::Simkl, QStringLiteral("12345")));
    QVERIFY(!reopened.record(importedEvidence(mapping)));
    QCOMPARE(readFile(profile.historyIniPath()), historyBefore);
    QCOMPARE(readFile(profile.activityDbPath()), activityBefore);
}

void TrackerLifecycleTest::profileRemovalDeletesTrackerPrivateStateOnly()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeVault vault;
    const QList<TrackerProviderId> providers{TrackerProviderId::Simkl};
    for (const TrackerProviderId provider : providers) {
        QVERIFY(vault.saveAndVerify(credential(profile.profileId(), QStringLiteral("12345"), provider)));
    }
    const QString historyPath = profile.historyIniPath();
    const QString activityPath = profile.activityDbPath();
    const QString ratingsReviewsPath = QDir(profile.profileRoot()).filePath(
        QStringLiteral("ratings-reviews.json"));
    QVERIFY(writeFile(historyPath, QByteArrayLiteral("canonical-history")));
    QVERIFY(writeFile(activityPath, QByteArrayLiteral("canonical-activity")));
    QVERIFY(writeFile(ratingsReviewsPath, QByteArrayLiteral("arc49-state")));
    const QStringList trackerFiles{
        TrackerConnectionStore::storagePath(profile),
        TrackerMappingStore::storagePath(profile),
        TrackerHistoryEvidenceStore::storagePath(profile),
        TrackerImportStore::storagePath(profile),
        TrackerDeliveryStore::storagePath(profile),
        TrackerScrobbleStore::storagePath(profile)};
    for (const QString &path : trackerFiles)
        QVERIFY(writeFile(path, QByteArrayLiteral("tracker-private")));
    const QByteArray historyBefore = readFile(historyPath);
    const QByteArray activityBefore = readFile(activityPath);
    const QByteArray ratingsReviewsBefore = readFile(ratingsReviewsPath);

    QVERIFY(TrackerLifecycleCoordinator::removeProfilePrivateStateForPermanentDeletion(profile, vault));
    for (const QString &path : trackerFiles)
        QVERIFY(!QFileInfo::exists(path));
    for (const TrackerProviderId provider : providers)
        QVERIFY(!vault.loadForProfile(profile.profileId(), provider));
    QCOMPARE(readFile(historyPath), historyBefore);
    QCOMPARE(readFile(activityPath), activityBefore);
    QCOMPARE(readFile(ratingsReviewsPath), ratingsReviewsBefore);
}

void TrackerLifecycleTest::profileRemovalUsesSupportedVaultNamespace()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const ProfilePaths profile = ProfilePaths::localOnly(root.path());
    FakeVault vault;
    vault.rejectUnsupportedClear = true;
    const QString trackerPath = TrackerConnectionStore::storagePath(profile);
    QVERIFY(writeFile(trackerPath, QByteArrayLiteral("tracker-private")));
    QVERIFY(vault.saveAndVerify(credential(profile.profileId(), QStringLiteral("12345"))));

    QVERIFY(TrackerLifecycleCoordinator::removeProfilePrivateStateForPermanentDeletion(profile, vault));
    QCOMPARE(vault.clearedProviders, QList<TrackerProviderId>{TrackerProviderId::Simkl});
    QVERIFY(!vault.loadForProfile(profile.profileId(), TrackerProviderId::Simkl));
    QVERIFY(!QFileInfo::exists(trackerPath));
}

QTEST_APPLESS_MAIN(TrackerLifecycleTest)

#include "tst_tracker_lifecycle.moc"
