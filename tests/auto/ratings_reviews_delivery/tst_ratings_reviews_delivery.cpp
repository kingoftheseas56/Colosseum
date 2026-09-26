#include "account/RatingsReviewsDelivery.h"
#include "account/RatingsReviewsDeliveryOutbox.h"
#include "account/RatingsReviewsDeliveryReceiptStore.h"
#include "account/RatingsReviewsProviderMappingStore.h"
#include "account/ProfilePreferencesStore.h"

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <functional>

namespace {

class FakeDeliveryAdapter final : public RatingsReviewsDeliveryAdapter
{
public:
    Result nextResult;
    ReconcileOutcome nextReconcile = ReconcileOutcome::Indeterminate;
    int sends = 0;
    int reconciliations = 0;
    QList<RatingsReviewsDeliveryOperation> sent;
    std::function<void()> duringSend;
    std::function<void()> duringReconcile;

    Result send(const RatingsReviewsDeliveryOperation &operation) override
    {
        ++sends;
        sent.append(operation);
        if (duringSend)
            duringSend();
        return nextResult;
    }

    ReconcileOutcome reconcile(const RatingsReviewsDeliveryOperation &) override
    {
        ++reconciliations;
        if (duringReconcile)
            duringReconcile();
        return nextReconcile;
    }
};

RatingsReviewsStore::Identity fixtureIdentity(const QString &mediaId = QStringLiteral("fixture-provider-read-series"))
{
    return {QStringLiteral("theatre"), QStringLiteral("series"), mediaId};
}

RatingsReviewsDeliveryProviderCapability fixtureCapability(
    bool spoilerMetadata = true)
{
    RatingsReviewsDeliveryProviderCapability capability;
    capability.connected = true;
    capability.connectionGeneration = 7;
    capability.ratingCapable = true;
    capability.reviewCapable = true;
    capability.spoilerMetadata = spoilerMetadata;
    capability.conversionMapDigest = QString(64, QLatin1Char('a'));
    capability.translatedRating = 81;
    return capability;
}

RatingsReviewsDeliveryActivation activation(
    const QTemporaryDir &directory,
    RatingsReviewsStore *store)
{
    return {
        QStringLiteral("local-fixture"),
        {
            directory.filePath(QStringLiteral("mappings.json")),
            directory.filePath(QStringLiteral("outbox.json")),
            directory.filePath(QStringLiteral("receipts.json"))},
        store};
}

bool saveFixtureRecord(RatingsReviewsStore *store,
                       const RatingsReviewsStore::Identity &identity,
                       bool spoiler = false,
                       const QString &review = QStringLiteral("Exact review text."))
{
    return store->saveLocal(identity, 8.5, review, spoiler, nullptr, nullptr);
}

bool writeMapping(const RatingsReviewsDeliveryActivation &value,
                  const QString &provider,
                  const QString &key)
{
    RatingsReviewsProviderMappingStore mappings(value.privatePaths.mappingsPath);
    return mappings.upsert({
        provider,
        key,
        QStringLiteral("matched"),
        QStringLiteral("remote-%1").arg(provider),
        QStringLiteral("exact"),
        1,
        7});
}

RatingsReviewsPublishIntent intentFor(
    RatingsReviewsStore *store,
    const QString &key,
    const QList<RatingsReviewsPublishDestination> &destinations)
{
    const auto record = store->recordByKey(key);
    return {key, store->revision(), ratingsReviewsCanonicalPayloadDigestV1(*record), destinations};
}

} // namespace

class RatingsReviewsDeliveryTest final : public QObject
{
    Q_OBJECT

private slots:
    void R4_payloadDigestUsesFrozenCanonicalContent();
    void O01_intentPersistsBeforeFakeSend();
    void R4_committed_digest_same_title_change_rejects();
    void R4_unrelated_store_revision_does_not_invalidate_unchanged_payload();
    void U01_unknownNeverBlindResends();
    void R6_unknown_preserves_original_intent_after_canonical_edit();
    void R6_unknown_preserves_original_map_after_map_edit();
    void R6_superseding_same_target_is_blocked_peer_provider_progresses();
    void R6_retry_cannot_cross_unknown_target_barrier();
    void R7_prior_attempt_receipt_does_not_block_unknown_retry_recovery();
    void R7_prior_attempt_receipt_survives_private_adoption();
    void G10_withdrawn_capability_blocks_retry_and_recovery();
    void R7_connection_change_during_send_preserves_unknown();
    void R7_connection_change_during_reconcile_preserves_unknown();
    void R3_unmatched_mapping_round_trips_without_writable_identity();
    void R3_legacy_empty_unmatched_fields_load_without_writable_identity();
    void R9_fixture_publish_uses_committed_map_and_default_projection();
    void G10_spoilerIncapableProviderCreatesZeroOperations();
    void R7_terminal_receipt_wins_over_recovered_inflight();
    void R7_pendingRecoveryDispatchesOnlyWithCurrentBinding();
    void R7_pending_resumes_with_fresh_active_incarnation();
    void R7_delayed_old_activation_callback_is_ignored();
    void R8_unknown_remains_unknown_after_profile_rebind();
    void R9_taggedFixtureMappingAcceptsOnlyExactIdentity();
    void R9_joined_fixture_exact_phase_counts();
    void R9_restart_rehydrates_without_republish_or_reseed();
    void R7_corrupt_receipt_binding_fails_closed();
};

void RatingsReviewsDeliveryTest::R4_payloadDigestUsesFrozenCanonicalContent()
{
    RatingsReviewsStore::Record record;
    record.identity = {
        QStringLiteral("theatre"),
        QStringLiteral("series"),
        QStringLiteral("fixture-provider-read-series")};
    record.rating = 8.5;
    record.review = QStringLiteral("Exact review text.");
    record.spoiler = true;
    record.createdAtMs = 42;
    record.updatedAtMs = 84;

    QCOMPARE(
        ratingsReviewsCanonicalPayloadDigestV1(record),
        QStringLiteral("5d92b99e4adda4bdf40d7222d1929d54eba2d579fc86b382951bb8c3b1fff3fd"));
}

void RatingsReviewsDeliveryTest::O01_intentPersistsBeforeFakeSend()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qint64 clock = 10;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [&clock] { return ++clock; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));

    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    adapter.nextResult.safeProviderStatus = QStringLiteral("ok");
    RatingsReviewsDelivery delivery([&clock] { return ++clock; });
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(value));

    const auto result = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    QCOMPARE(result.providers.size(), 1);
    QVERIFY(result.providers.first().accepted);
    QCOMPARE(adapter.sends, 1);
    QCOMPARE(adapter.sent.first().state, QStringLiteral("inFlight"));
    QCOMPARE(adapter.sent.first().attemptCount, 1);

    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    QCOMPARE(outbox.operations().size(), 1);
    QCOMPARE(outbox.operations().first().state, QStringLiteral("succeeded"));
    RatingsReviewsDeliveryReceiptStore receipts(value.privatePaths.receiptsPath);
    QCOMPARE(receipts.receipts().size(), 1);
    QFile receiptFile(value.privatePaths.receiptsPath);
    QVERIFY(receiptFile.open(QIODevice::ReadOnly));
    QVERIFY(!receiptFile.readAll().contains("Exact review text."));
}

void RatingsReviewsDeliveryTest::R4_committed_digest_same_title_change_rejects()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto staleIntent = intentFor(&store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}});
    QVERIFY(store.setRating(fixtureIdentity(), 9.0));
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter adapter;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(value));

    const auto result = delivery.publishCommitted(staleIntent);
    QVERIFY(result.providers.isEmpty());
    QCOMPARE(adapter.sends, 0);
    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    QCOMPARE(outbox.operations().size(), 0);
}

void RatingsReviewsDeliveryTest::R4_unrelated_store_revision_does_not_invalidate_unchanged_payload()
{
    QTemporaryDir directory;
    qint64 clock = 0;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [&clock] { return ++clock; });
    const auto first = fixtureIdentity();
    const auto unrelated = fixtureIdentity(QStringLiteral("unrelated-series"));
    QVERIFY(saveFixtureRecord(&store, first));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(first);
    const auto savedIntent = intentFor(&store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}});
    QVERIFY(saveFixtureRecord(&store, unrelated));
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    RatingsReviewsDelivery delivery([&clock] { return ++clock; });
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(value));

    const auto result = delivery.publishCommitted(savedIntent);
    QCOMPARE(result.providers.size(), 1);
    QVERIFY(result.providers.first().accepted);
    QCOMPARE(adapter.sends, 1);
}

void RatingsReviewsDeliveryTest::U01_unknownNeverBlindResends()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-b"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch;
    adapter.nextReconcile = RatingsReviewsDeliveryAdapter::ReconcileOutcome::Indeterminate;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-b"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(value));
    const auto result = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-b"), true, false, std::nullopt}}));
    QCOMPARE(adapter.sends, 1);
    const QString operationId = result.providers.first().operationIds.first();
    QVERIFY(delivery.reconcileOperation(operationId));
    QCOMPARE(adapter.reconciliations, 1);
    QCOMPARE(adapter.sends, 1);
    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    QCOMPARE(outbox.operation(operationId)->state, QStringLiteral("unknownOutcome"));
}

void RatingsReviewsDeliveryTest::R6_unknown_preserves_original_intent_after_canonical_edit()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-b"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch;
    adapter.nextReconcile = RatingsReviewsDeliveryAdapter::ReconcileOutcome::MatchesIntendedState;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-b"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(value));

    const auto result = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-b"), true, false, std::nullopt}}));
    const QString operationId = result.providers.first().operationIds.first();
    RatingsReviewsDeliveryOutbox originalOutbox(value.privatePaths.outboxPath);
    const auto original = originalOutbox.operation(operationId);
    QVERIFY(original.has_value());
    QVERIFY(store.setRating(fixtureIdentity(), 9.0));
    QVERIFY(delivery.reconcileOperation(operationId));

    RatingsReviewsDeliveryOutbox settledOutbox(value.privatePaths.outboxPath);
    const auto settled = settledOutbox.operation(operationId);
    QVERIFY(settled.has_value());
    QCOMPARE(adapter.sends, 1);
    QCOMPARE(adapter.reconciliations, 1);
    QCOMPARE(settled->canonicalPayloadDigest, original->canonicalPayloadDigest);
    QCOMPARE(settled->canonicalRevision, original->canonicalRevision);
    QCOMPARE(settled->safePayload, original->safePayload);
    QCOMPARE(settled->state, QStringLiteral("succeeded"));
}

void RatingsReviewsDeliveryTest::R6_unknown_preserves_original_map_after_map_edit()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-b"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch;
    adapter.nextReconcile = RatingsReviewsDeliveryAdapter::ReconcileOutcome::Indeterminate;
    RatingsReviewsDelivery delivery;
    auto capability = fixtureCapability();
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-b"), capability, &adapter);
    QVERIFY(delivery.activateProfile(value));

    const auto result = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-b"), true, false, std::nullopt}}));
    const QString operationId = result.providers.first().operationIds.first();
    RatingsReviewsDeliveryOutbox originalOutbox(value.privatePaths.outboxPath);
    const auto original = originalOutbox.operation(operationId);
    QVERIFY(original.has_value());

    RatingsReviewsProviderMappingStore mappings(value.privatePaths.mappingsPath);
    auto changedMapping = *mappings.mapping(QStringLiteral("fixture-b"), key);
    changedMapping.providerMediaId = QStringLiteral("remote-rebound");
    QVERIFY(mappings.upsert(changedMapping));
    capability.conversionMapDigest = QString(64, QLatin1Char('b'));
    capability.translatedRating = 91;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-b"), capability, &adapter);
    QVERIFY(delivery.reconcileOperation(operationId));

    RatingsReviewsDeliveryOutbox unsettledOutbox(value.privatePaths.outboxPath);
    const auto unsettled = unsettledOutbox.operation(operationId);
    QVERIFY(unsettled.has_value());
    QCOMPARE(adapter.sends, 1);
    QCOMPARE(adapter.reconciliations, 1);
    QCOMPARE(unsettled->state, QStringLiteral("unknownOutcome"));
    QCOMPARE(unsettled->mappingProviderMediaId, original->mappingProviderMediaId);
    QCOMPARE(unsettled->conversionMapDigest, original->conversionMapDigest);
    QCOMPARE(unsettled->safePayload, original->safePayload);
}

void RatingsReviewsDeliveryTest::R6_superseding_same_target_is_blocked_peer_provider_progresses()
{
    QTemporaryDir directory;
    qint64 clock = 0;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [&clock] { return ++clock; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));
    QVERIFY(writeMapping(value, QStringLiteral("fixture-b"), key));

    FakeDeliveryAdapter firstAdapter;
    firstAdapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch;
    FakeDeliveryAdapter peerAdapter;
    peerAdapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    RatingsReviewsDelivery delivery([&clock] { return ++clock; });
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &firstAdapter);
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-b"), fixtureCapability(), &peerAdapter);
    QVERIFY(delivery.activateProfile(value));
    const auto first = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    QCOMPARE(firstAdapter.sends, 1);
    QVERIFY(store.setRating(fixtureIdentity(), 9.0));

    const auto second = delivery.publishCommitted(intentFor(
        &store, key,
        {{QStringLiteral("fixture-a"), true, false, std::nullopt},
         {QStringLiteral("fixture-b"), true, false, std::nullopt}}));
    QCOMPARE(second.providers.size(), 2);
    QCOMPARE(firstAdapter.sends, 1);
    QCOMPARE(peerAdapter.sends, 1);

    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    const QString blockedId = second.providers.first().operationIds.first();
    RatingsReviewsDeliveryOutbox readback(value.privatePaths.outboxPath);
    QCOMPARE(readback.operation(blockedId)->state, QStringLiteral("needsAttention"));
    QCOMPARE(readback.operation(blockedId)->staleReason, QStringLiteral("unknown_target_barrier"));
    QCOMPARE(readback.operation(second.providers.last().operationIds.first())->state,
             QStringLiteral("succeeded"));
}

void RatingsReviewsDeliveryTest::R6_retry_cannot_cross_unknown_target_barrier()
{
    QTemporaryDir directory;
    qint64 clock = 0;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [&clock] { return ++clock; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto binding = activation(directory, &store);
    QVERIFY(writeMapping(binding, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch;
    RatingsReviewsDelivery delivery([&clock] { return ++clock; });
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(binding));
    const auto first = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    QVERIFY(first.providers.first().accepted);
    QVERIFY(store.setRating(fixtureIdentity(), 9.0));
    const auto second = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    const QString blockedId = second.providers.first().operationIds.first();
    QVERIFY(delivery.retryOperation(blockedId));
    QCOMPARE(adapter.sends, 1);
    RatingsReviewsDeliveryOutbox outbox(binding.privatePaths.outboxPath);
    QCOMPARE(outbox.operation(blockedId)->state, QStringLiteral("needsAttention"));
    QCOMPARE(outbox.operation(blockedId)->staleReason, QStringLiteral("unknown_target_barrier"));
}

void RatingsReviewsDeliveryTest::R7_prior_attempt_receipt_does_not_block_unknown_retry_recovery()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto binding = activation(directory, &store);
    QVERIFY(writeMapping(binding, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::KnownProviderFailure;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(binding));
    const auto result = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    const QString operationId = result.providers.first().operationIds.first();
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch;
    QVERIFY(delivery.retryOperation(operationId));
    QCOMPARE(adapter.sends, 2);
    delivery.deactivateProfile();
    RatingsReviewsDelivery resumed;
    resumed.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(resumed.activateProfile(binding));
    QCOMPARE(adapter.sends, 2);
    RatingsReviewsDeliveryOutbox outbox(binding.privatePaths.outboxPath);
    QCOMPARE(outbox.operation(operationId)->state, QStringLiteral("unknownOutcome"));
}

void RatingsReviewsDeliveryTest::R7_prior_attempt_receipt_survives_private_adoption()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceRoot = directory.filePath(QStringLiteral("source"));
    const QString destinationRoot = directory.filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(sourceRoot));
    QVERIFY(QDir().mkpath(destinationRoot));
    RatingsReviewsStore sourceCanonical(directory.filePath(QStringLiteral("source-canonical.json")), [] { return 11; });
    RatingsReviewsStore destinationCanonical(directory.filePath(QStringLiteral("destination-canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&sourceCanonical, fixtureIdentity()));
    QVERIFY(saveFixtureRecord(&destinationCanonical, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const RatingsReviewsDeliveryActivation sourceActivation{
        QStringLiteral("local-profile"),
        {sourceRoot + QStringLiteral("/mappings.json"), sourceRoot + QStringLiteral("/outbox.json"),
         sourceRoot + QStringLiteral("/receipts.json")}, &sourceCanonical};
    const RatingsReviewsDeliveryActivation destinationActivation{
        QStringLiteral("account-profile"),
        {destinationRoot + QStringLiteral("/mappings.json"), destinationRoot + QStringLiteral("/outbox.json"),
         destinationRoot + QStringLiteral("/receipts.json")}, &destinationCanonical};
    QVERIFY(writeMapping(sourceActivation, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::KnownProviderFailure;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(sourceActivation));
    const auto published = delivery.publishCommitted(intentFor(
        &sourceCanonical, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    const QString operationId = published.providers.first().operationIds.first();
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch;
    QVERIFY(delivery.retryOperation(operationId));
    QCOMPARE(adapter.sends, 2);
    delivery.deactivateProfile();

    const RatingsReviewsPrivateProfileBinding source{
        sourceActivation.profileId, {}, sourceActivation.privatePaths};
    const RatingsReviewsPrivateProfileBinding destination{
        destinationActivation.profileId, {}, destinationActivation.privatePaths};
    QString error;
    QVERIFY2(RatingsReviewsDelivery::handoffPrivateState(
                 source, destination, &destinationCanonical, &error), qPrintable(error));
    QVERIFY2(RatingsReviewsDelivery::handoffPrivateState(
                 source, destination, &destinationCanonical, &error), qPrintable(error));
    RatingsReviewsDeliveryOutbox outbox(destination.privatePaths.outboxPath);
    RatingsReviewsDeliveryReceiptStore receipts(destination.privatePaths.receiptsPath);
    const auto operation = outbox.operation(operationId);
    const auto receipt = receipts.receipt(operationId);
    QVERIFY(operation.has_value());
    QVERIFY(receipt.has_value());
    QCOMPARE(operation->state, QStringLiteral("unknownOutcome"));
    QCOMPARE(operation->attemptCount, 2);
    QCOMPARE(receipt->attemptNumber, 1);
    RatingsReviewsDelivery resumed;
    resumed.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(resumed.activateProfile(destinationActivation));
    QCOMPARE(adapter.sends, 2);
    QCOMPARE(outbox.operation(operationId)->state, QStringLiteral("unknownOutcome"));
}

void RatingsReviewsDeliveryTest::G10_withdrawn_capability_blocks_retry_and_recovery()
{
    for (int scenario = 0; scenario < 3; ++scenario) {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
        QVERIFY(saveFixtureRecord(&store, fixtureIdentity(), scenario == 2));
        const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
        const auto binding = activation(directory, &store);
        QVERIFY(writeMapping(binding, QStringLiteral("fixture-a"), key));
        FakeDeliveryAdapter adapter;
        adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::KnownProviderFailure;
        RatingsReviewsDelivery delivery;
        auto capability = fixtureCapability();
        delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), capability, &adapter);
        QVERIFY(delivery.activateProfile(binding));
        const bool rating = scenario == 0;
        const auto published = delivery.publishCommitted(intentFor(
            &store, key, {{QStringLiteral("fixture-a"), rating, !rating, std::nullopt}}));
        QVERIFY(published.providers.first().accepted);
        const QString operationId = published.providers.first().operationIds.first();
        QCOMPARE(adapter.sends, 1);
        if (scenario == 0)
            capability.ratingCapable = false;
        else if (scenario == 1)
            capability.reviewCapable = false;
        else
            capability.spoilerMetadata = false;
        delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), capability, &adapter);
        QVERIFY(delivery.retryOperation(operationId));
        QCOMPARE(adapter.sends, 1);
        RatingsReviewsDeliveryOutbox outbox(binding.privatePaths.outboxPath);
        QCOMPARE(outbox.operation(operationId)->state, QStringLiteral("needsAttention"));
        QCOMPARE(outbox.operation(operationId)->staleReason,
                 QStringLiteral("provider_capability_changed"));
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto binding = activation(directory, &store);
    QVERIFY(writeMapping(binding, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::TransientBeforeSend;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(binding));
    const auto published = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    const QString operationId = published.providers.first().operationIds.first();
    QCOMPARE(adapter.sends, 1);
    delivery.deactivateProfile();
    RatingsReviewsDeliveryOutbox interruptedOutbox(binding.privatePaths.outboxPath);
    auto pending = *interruptedOutbox.operation(operationId);
    // Model a crash after the intent commit but before its first send.
    pending.state = QStringLiteral("pending");
    pending.attemptCount = 0;
    pending.lastAttemptAtMs.reset();
    QVERIFY(interruptedOutbox.replace(pending));
    auto withdrawn = fixtureCapability();
    withdrawn.ratingCapable = false;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), withdrawn, &adapter);
    QVERIFY(delivery.activateProfile(binding));
    QCOMPARE(adapter.sends, 1);
    RatingsReviewsDeliveryOutbox outbox(binding.privatePaths.outboxPath);
    QCOMPARE(outbox.operation(operationId)->state, QStringLiteral("needsAttention"));
}

void RatingsReviewsDeliveryTest::R7_connection_change_during_send_preserves_unknown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto binding = activation(directory, &store);
    QVERIFY(writeMapping(binding, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(binding));
    adapter.duringSend = [&] {
        auto changed = fixtureCapability();
        changed.connectionGeneration = 8;
        delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), changed, &adapter);
    };
    const auto published = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    const QString operationId = published.providers.first().operationIds.first();
    QCOMPARE(adapter.sends, 1);
    RatingsReviewsDeliveryOutbox outbox(binding.privatePaths.outboxPath);
    RatingsReviewsDeliveryReceiptStore receipts(binding.privatePaths.receiptsPath);
    QCOMPARE(outbox.operation(operationId)->state, QStringLiteral("unknownOutcome"));
    QCOMPARE(outbox.operation(operationId)->staleReason,
             QStringLiteral("provider_binding_changed"));
    QVERIFY(!receipts.receipt(operationId).has_value());
}

void RatingsReviewsDeliveryTest::R7_connection_change_during_reconcile_preserves_unknown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto binding = activation(directory, &store);
    QVERIFY(writeMapping(binding, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch;
    adapter.nextReconcile = RatingsReviewsDeliveryAdapter::ReconcileOutcome::MatchesIntendedState;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(binding));
    const auto published = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    const QString operationId = published.providers.first().operationIds.first();
    adapter.duringReconcile = [&] {
        auto changed = fixtureCapability();
        changed.connectionGeneration = 8;
        delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), changed, &adapter);
    };
    QVERIFY(delivery.reconcileOperation(operationId));
    QCOMPARE(adapter.reconciliations, 1);
    RatingsReviewsDeliveryOutbox outbox(binding.privatePaths.outboxPath);
    RatingsReviewsDeliveryReceiptStore receipts(binding.privatePaths.receiptsPath);
    QCOMPARE(outbox.operation(operationId)->state, QStringLiteral("unknownOutcome"));
    QCOMPARE(outbox.operation(operationId)->staleReason,
             QStringLiteral("provider_binding_changed"));
    QVERIFY(!receipts.receipt(operationId).has_value());
}

void RatingsReviewsDeliveryTest::R3_unmatched_mapping_round_trips_without_writable_identity()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("mappings.json"));
    for (const QString &status : {QStringLiteral("missing"), QStringLiteral("ambiguous"), QStringLiteral("unsupported")}) {
        RatingsReviewsProviderMappingStore store(path);
        QVERIFY(store.upsert({QStringLiteral("fixture-a"), status, status,
                              QString(), QString(), 0, 7}));
        RatingsReviewsProviderMappingStore reloaded(path);
        QVERIFY(reloaded.healthy());
        const auto mapping = reloaded.mapping(QStringLiteral("fixture-a"), status);
        QVERIFY(mapping.has_value());
        QCOMPARE(mapping->status, status);
        QVERIFY(mapping->providerMediaId.isEmpty());
        QVERIFY(mapping->resolution.isEmpty());
        QVERIFY(reloaded.replaceAll(reloaded.mappings()));
    }
}

void RatingsReviewsDeliveryTest::R3_legacy_empty_unmatched_fields_load_without_writable_identity()
{
    const QJsonObject legacy = {
        {QStringLiteral("version"), 1},
        {QStringLiteral("provider_id"), QStringLiteral("fixture-a")},
        {QStringLiteral("canonical_key"), QStringLiteral("fixture-key")},
        {QStringLiteral("status"), QStringLiteral("missing")},
        {QStringLiteral("provider_media_id"), QString()},
        {QStringLiteral("resolution"), QString()},
        {QStringLiteral("confirmed_at_ms"), 0},
        {QStringLiteral("connection_generation"), 7}};
    const auto mapping = ratingsReviewsDeliveryMappingFromJson(legacy);
    QVERIFY(mapping.has_value());
    QVERIFY(mapping->providerMediaId.isEmpty());
    QVERIFY(mapping->resolution.isEmpty());
    QJsonObject invalid = legacy;
    invalid.insert(QStringLiteral("provider_media_id"), QStringLiteral("remote-title"));
    QVERIFY(!ratingsReviewsDeliveryMappingFromJson(invalid));
}

void RatingsReviewsDeliveryTest::R9_fixture_publish_uses_committed_map_and_default_projection()
{
    QTemporaryDir directory;
    auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    ProfilePreferencesStore preferences(directory.filePath(QStringLiteral("preferences.ini")), hook);
    QVERIFY(preferences.setRatingsReviewsDefaultRatingDestinations(
        {QStringLiteral("fixture-a"), QStringLiteral("fixture-b")}));
    QVERIFY(preferences.setRatingsReviewsDefaultReviewDestinations(
        {QStringLiteral("fixture-a")}));
    const auto map = RatingsReviewsConversionMap::recommended(
        QStringLiteral("fixture-a"), QStringLiteral("fixture-halfpoint-v1"), 1, hook);
    QVERIFY(map.has_value());
    QVERIFY(preferences.setRatingsReviewsConversionMap(*map));

    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto binding = activation(directory, &store);
    QVERIFY(writeMapping(binding, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    RatingsReviewsDelivery delivery;
    auto capability = fixtureCapability();
    capability.translatedRating = 81;
    capability.conversionMapDigest = QString(64, QLatin1Char('a'));
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), capability, &adapter);
    delivery.setFixturePreferencesForTests(&preferences);
    QVERIFY(delivery.activateProfile(binding));
    const QVariantList rows = delivery.publishDestinations(key);
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows.first().toMap().value(QStringLiteral("ratingDefault")).toBool());
    QVERIFY(rows.first().toMap().value(QStringLiteral("reviewDefault")).toBool());
    const auto result = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    QVERIFY(result.providers.first().accepted);
    QCOMPARE(adapter.sent.size(), 1);
    QCOMPARE(adapter.sent.first().safePayload.value(QStringLiteral("native_value")), QJsonValue(8.5));
    QCOMPARE(adapter.sent.first().conversionMapDigest, map->digest());
    QCOMPARE(preferences.ratingsReviewsDefaultRatingDestinations(),
             (QStringList{QStringLiteral("fixture-a"), QStringLiteral("fixture-b")}));
}

void RatingsReviewsDeliveryTest::G10_spoilerIncapableProviderCreatesZeroOperations()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity(), true));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-spoiler-blocked"), key));
    FakeDeliveryAdapter adapter;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(
        QStringLiteral("fixture-spoiler-blocked"), fixtureCapability(false), &adapter);
    QVERIFY(delivery.activateProfile(value));

    const auto result = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-spoiler-blocked"), true, true, std::nullopt}}));
    QCOMPARE(result.providers.size(), 1);
    QVERIFY(!result.providers.first().accepted);
    QVERIFY(result.providers.first().reason.contains(QStringLiteral("spoiler")));
    QCOMPARE(adapter.sends, 0);
    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    QCOMPARE(outbox.operations().size(), 0);
}

void RatingsReviewsDeliveryTest::R7_terminal_receipt_wins_over_recovered_inflight()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter writer;
    writer.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    RatingsReviewsDelivery first;
    first.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &writer);
    QVERIFY(first.activateProfile(value));
    const auto result = first.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    const QString operationId = result.providers.first().operationIds.first();
    first.deactivateProfile();

    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    auto recovered = *outbox.operation(operationId);
    recovered.state = QStringLiteral("inFlight");
    QVERIFY(outbox.replace(recovered));

    FakeDeliveryAdapter afterRestart;
    RatingsReviewsDelivery restarted;
    restarted.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &afterRestart);
    QVERIFY(restarted.activateProfile(value));
    QCOMPARE(afterRestart.sends, 0);
    RatingsReviewsDeliveryOutbox readback(value.privatePaths.outboxPath);
    QCOMPARE(readback.operation(operationId)->state, QStringLiteral("succeeded"));
}

void RatingsReviewsDeliveryTest::R7_pendingRecoveryDispatchesOnlyWithCurrentBinding()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));
    FakeDeliveryAdapter writer;
    writer.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::TransientBeforeSend;
    RatingsReviewsDelivery first;
    first.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &writer);
    QVERIFY(first.activateProfile(value));
    const auto result = first.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    const QString operationId = result.providers.first().operationIds.first();
    first.deactivateProfile();

    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    auto pending = *outbox.operation(operationId);
    pending.state = QStringLiteral("pending");
    QVERIFY(outbox.replace(pending));
    FakeDeliveryAdapter afterRestart;
    afterRestart.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    RatingsReviewsDelivery restarted;
    restarted.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &afterRestart);
    QString activationError;
    QVERIFY2(restarted.activateProfile(value, &activationError), qPrintable(activationError));
    QCOMPARE(afterRestart.sends, 1);
    RatingsReviewsDeliveryOutbox readback(value.privatePaths.outboxPath);
    QCOMPARE(readback.operation(operationId)->state, QStringLiteral("succeeded"));

    QVERIFY(store.setRating(fixtureIdentity(), 9.0));
    auto stalePending = *readback.operation(operationId);
    stalePending.operationId = QStringLiteral("970be045-0844-4a12-94b6-0bf2172e5cc7");
    stalePending.state = QStringLiteral("pending");
    stalePending.attemptCount = 0;
    stalePending.lastAttemptAtMs.reset();
    RatingsReviewsDeliveryOutbox pendingOutbox(value.privatePaths.outboxPath);
    QVERIFY(pendingOutbox.append({stalePending}));
    restarted.deactivateProfile();
    FakeDeliveryAdapter staleRestart;
    RatingsReviewsDelivery stale;
    stale.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &staleRestart);
    QVERIFY(stale.activateProfile(value));
    QCOMPARE(staleRestart.sends, 0);
    RatingsReviewsDeliveryOutbox staleReadback(value.privatePaths.outboxPath);
    QCOMPARE(staleReadback.operation(stalePending.operationId)->state, QStringLiteral("needsAttention"));
    QCOMPARE(staleReadback.operation(stalePending.operationId)->staleReason, QStringLiteral("canonical_payload_changed"));
}

void RatingsReviewsDeliveryTest::R7_pending_resumes_with_fresh_active_incarnation()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));

    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::TransientBeforeSend;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(value));
    const auto result = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    const QString operationId = result.providers.first().operationIds.first();
    QCOMPARE(adapter.sends, 1);
    delivery.deactivateProfile();

    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    auto pending = *outbox.operation(operationId);
    pending.state = QStringLiteral("pending");
    QVERIFY(outbox.replace(pending));

    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    QVERIFY(delivery.activateProfile(value));
    QCOMPARE(adapter.sends, 2);
    RatingsReviewsDeliveryOutbox readback(value.privatePaths.outboxPath);
    QCOMPARE(readback.operation(operationId)->state, QStringLiteral("succeeded"));
}

void RatingsReviewsDeliveryTest::R7_delayed_old_activation_callback_is_ignored()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));

    RatingsReviewsDelivery delivery;
    FakeDeliveryAdapter adapter;
    adapter.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    QVERIFY(delivery.activateProfile(value));
    const quint64 oldIncarnation = delivery.activeProfileIncarnation();
    adapter.duringSend = [&] {
        delivery.deactivateProfile();
        QVERIFY(delivery.activateProfile(value));
        QVERIFY(delivery.activeProfileIncarnation() != oldIncarnation);
    };

    const auto result = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), true, false, std::nullopt}}));
    QVERIFY(!result.providers.isEmpty());
    QCOMPARE(adapter.sends, 1);

    RatingsReviewsDeliveryOutbox readback(value.privatePaths.outboxPath);
    QCOMPARE(readback.operations().size(), 1);
    const auto operation = readback.operations().first();
    QCOMPARE(operation.state, QStringLiteral("unknownOutcome"));
    QCOMPARE(operation.profileIncarnation, oldIncarnation);
    RatingsReviewsDeliveryReceiptStore receipts(value.privatePaths.receiptsPath);
    QVERIFY(!receipts.receipt(operation.operationId).has_value());
}

void RatingsReviewsDeliveryTest::R8_unknown_remains_unknown_after_profile_rebind()
{
    QTemporaryDir directory;
    const auto sourceRoot = directory.filePath(QStringLiteral("source"));
    const auto destinationRoot = directory.filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(sourceRoot));
    QVERIFY(QDir().mkpath(destinationRoot));
    RatingsReviewsStore sourceCanonical(directory.filePath(QStringLiteral("source-canonical.json")), [] { return 11; });
    RatingsReviewsStore destinationCanonical(directory.filePath(QStringLiteral("destination-canonical.json")), [] { return 12; });
    QVERIFY(saveFixtureRecord(&sourceCanonical, fixtureIdentity()));
    QVERIFY(saveFixtureRecord(&destinationCanonical, fixtureIdentity(), false, QStringLiteral("New canonical review.")));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    RatingsReviewsPrivateProfileBinding source{
        QStringLiteral("local-profile"), {},
        {sourceRoot + QStringLiteral("/mappings.json"), sourceRoot + QStringLiteral("/outbox.json"), sourceRoot + QStringLiteral("/receipts.json")}};
    RatingsReviewsPrivateProfileBinding destination{
        QStringLiteral("account-profile"), {},
        {destinationRoot + QStringLiteral("/mappings.json"), destinationRoot + QStringLiteral("/outbox.json"), destinationRoot + QStringLiteral("/receipts.json")}};
    const RatingsReviewsDeliveryActivation sourceActivation{
        source.profileId, source.privatePaths, &sourceCanonical};
    QVERIFY(writeMapping(sourceActivation, QStringLiteral("fixture-a"), key));
    RatingsReviewsDeliveryOutbox sourceOutbox(source.privatePaths.outboxPath);
    RatingsReviewsDeliveryOperation unknown;
    unknown.operationId = QStringLiteral("8c2f6fb1-8db2-4d90-8662-93d809b25910");
    unknown.profileId = source.profileId;
    unknown.profileIncarnation = 1;
    unknown.providerId = QStringLiteral("fixture-a");
    unknown.connectionGeneration = 7;
    unknown.operationType = QStringLiteral("rating.set");
    unknown.canonicalKey = key;
    unknown.canonicalRevision = sourceCanonical.revision();
    unknown.canonicalPayloadDigest = ratingsReviewsCanonicalPayloadDigestV1(*sourceCanonical.recordByKey(key));
    unknown.mappingProviderMediaId = QStringLiteral("remote-fixture-a");
    unknown.conversionMapDigest = QString(64, QLatin1Char('a'));
    unknown.intentCreatedAtMs = 1;
    unknown.state = QStringLiteral("unknownOutcome");
    unknown.attemptCount = 1;
    unknown.retryClass = QStringLiteral("manualAfterUnknown");
    unknown.adapterIdempotency = QStringLiteral("none");
    unknown.safePayload = {{QStringLiteral("kind"), QStringLiteral("rating")},
                           {QStringLiteral("native_value"), 81},
                           {QStringLiteral("source_rating"), 8.5}};
    auto stale = unknown;
    stale.operationId = QStringLiteral("6a8b5f4d-863f-46ae-a31d-890b3ac17054");
    stale.state = QStringLiteral("pending");
    auto terminal = unknown;
    terminal.operationId = QStringLiteral("d3420807-d355-4fba-a764-17961d8b8574");
    terminal.state = QStringLiteral("pending");
    QVERIFY(sourceOutbox.append({unknown, stale, terminal}));

    // A prior partial handoff may already have copied the operation and its
    // terminal receipt. The source can still carry its pre-receipt pending
    // bytes; the receipt, not that stale state byte, owns terminal truth.
    RatingsReviewsDeliveryOutbox destinationOutboxSeed(destination.privatePaths.outboxPath);
    auto copiedTerminal = terminal;
    copiedTerminal.profileId = destination.profileId;
    copiedTerminal.state = QStringLiteral("inFlight");
    QVERIFY(destinationOutboxSeed.append({copiedTerminal}));
    RatingsReviewsDeliveryReceiptStore destinationReceipts(
        destination.privatePaths.receiptsPath);
    RatingsReviewsDeliveryReceipt receipt;
    receipt.operationId = terminal.operationId;
    receipt.providerId = terminal.providerId;
    receipt.operationType = terminal.operationType;
    receipt.canonicalKey = terminal.canonicalKey;
    receipt.canonicalPayloadDigest = terminal.canonicalPayloadDigest;
    receipt.status = QStringLiteral("succeeded");
    receipt.attemptNumber = terminal.attemptCount;
    receipt.createdAtMs = terminal.intentCreatedAtMs;
    receipt.updatedAtMs = terminal.intentCreatedAtMs;
    receipt.completedAtMs = terminal.intentCreatedAtMs;
    receipt.safeProviderStatus = QStringLiteral("ok");
    QVERIFY(destinationReceipts.upsert(receipt));

    QVERIFY(RatingsReviewsDelivery::handoffPrivateState(source, destination, &destinationCanonical));
    QVERIFY(RatingsReviewsDelivery::handoffPrivateState(source, destination, &destinationCanonical));
    RatingsReviewsProviderMappingStore destinationMappings(destination.privatePaths.mappingsPath);
    RatingsReviewsDeliveryOutbox destinationOutboxReadback(destination.privatePaths.outboxPath);
    QCOMPARE(destinationMappings.mappings().size(), 1);
    QCOMPARE(destinationOutboxReadback.operations().size(), 3);
    const auto preservedUnknown = destinationOutboxReadback.operation(unknown.operationId);
    const auto blockedStale = destinationOutboxReadback.operation(stale.operationId);
    const auto preservedTerminal = destinationOutboxReadback.operation(terminal.operationId);
    QCOMPARE(preservedUnknown->profileId, destination.profileId);
    QCOMPARE(preservedUnknown->state, QStringLiteral("unknownOutcome"));
    QCOMPARE(preservedUnknown->profileIncarnation, unknown.profileIncarnation);
    QCOMPARE(preservedUnknown->operationId, unknown.operationId);
    QCOMPARE(preservedUnknown->canonicalRevision, unknown.canonicalRevision);
    QCOMPARE(preservedUnknown->canonicalPayloadDigest, unknown.canonicalPayloadDigest);
    QCOMPARE(preservedUnknown->mappingProviderMediaId, unknown.mappingProviderMediaId);
    QCOMPARE(preservedUnknown->conversionMapDigest, unknown.conversionMapDigest);
    QCOMPARE(preservedUnknown->intentCreatedAtMs, unknown.intentCreatedAtMs);
    QCOMPARE(preservedUnknown->safePayload, unknown.safePayload);
    QCOMPARE(blockedStale->profileId, destination.profileId);
    QCOMPARE(blockedStale->state, QStringLiteral("needsAttention"));
    QCOMPARE(blockedStale->staleReason, QStringLiteral("canonical_superseded_by_adoption"));
    QCOMPARE(preservedTerminal->state, QStringLiteral("succeeded"));
    QCOMPARE(sourceOutbox.operations().first().profileId, source.profileId);
}

void RatingsReviewsDeliveryTest::R9_taggedFixtureMappingAcceptsOnlyExactIdentity()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    const auto fixture = fixtureIdentity();
    const auto unrelated = fixtureIdentity(QStringLiteral("other-series"));
    QVERIFY(saveFixtureRecord(&store, unrelated));
    const auto value = activation(directory, &store);
    FakeDeliveryAdapter adapter;
    RatingsReviewsDelivery delivery;
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), fixtureCapability(), &adapter);
    delivery.enableTaggedFixtureMappingForTests(fixture);
    QVERIFY(delivery.activateProfile(value));

    const QString fixtureKey = RatingsReviewsStore::recordKeyForIdentity(fixture);
    RatingsReviewsProviderMappingStore mappings(value.privatePaths.mappingsPath);
    const auto preSaveDestinations = delivery.publishDestinations(fixtureKey);
    QCOMPARE(preSaveDestinations.size(), 1);
    QVERIFY(preSaveDestinations.first().toMap().value(QStringLiteral("ratingEligible")).toBool());
    QCOMPARE(mappings.mappings().size(), 0);
    QVERIFY(saveFixtureRecord(&store, fixture));
    const auto fixtureDestinations = delivery.publishDestinations(fixtureKey);
    QCOMPARE(fixtureDestinations.size(), 1);
    QVERIFY(fixtureDestinations.first().toMap().value(QStringLiteral("ratingEligible")).toBool());
    RatingsReviewsProviderMappingStore mappingsAfterSave(value.privatePaths.mappingsPath);
    QCOMPARE(mappingsAfterSave.mappings().size(), 1);

    const QString unrelatedKey = RatingsReviewsStore::recordKeyForIdentity(unrelated);
    const auto unrelatedDestinations = delivery.publishDestinations(unrelatedKey);
    QCOMPARE(unrelatedDestinations.size(), 1);
    QVERIFY(!unrelatedDestinations.first().toMap().value(QStringLiteral("ratingEligible")).toBool());
    QCOMPARE(mappingsAfterSave.mappings().size(), 1);
}

void RatingsReviewsDeliveryTest::R9_joined_fixture_exact_phase_counts()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity(), false,
                              QStringLiteral("projection-private-review-sentinel")));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));
    QVERIFY(writeMapping(value, QStringLiteral("fixture-b"), key));
    QVERIFY(writeMapping(value, QStringLiteral("fixture-spoiler-blocked"), key));
    FakeDeliveryAdapter adapterA;
    adapterA.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
    FakeDeliveryAdapter adapterB;
    adapterB.nextResult.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch;
    FakeDeliveryAdapter adapterSpoilerBlocked;
    RatingsReviewsDelivery delivery;
    auto capabilityA = fixtureCapability();
    capabilityA.translatedRating = 81;
    auto capabilityB = fixtureCapability();
    capabilityB.translatedRating = 81;
    const auto capabilitySpoilerBlocked = fixtureCapability(false);
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-a"), capabilityA, &adapterA);
    delivery.setFixtureProviderForTests(QStringLiteral("fixture-b"), capabilityB, &adapterB);
    delivery.setFixtureProviderForTests(
        QStringLiteral("fixture-spoiler-blocked"), capabilitySpoilerBlocked,
        &adapterSpoilerBlocked);
    QVERIFY(delivery.activateProfile(value));
    QCOMPARE(delivery.fixturePhase(), QStringLiteral("ready"));

    const auto ratingResult = delivery.publishCommitted(intentFor(
        &store, key,
        {{QStringLiteral("fixture-a"), true, false, std::nullopt},
         {QStringLiteral("fixture-b"), true, false, std::nullopt}}));
    QCOMPARE(ratingResult.providers.size(), 2);
    QVERIFY(ratingResult.providers.at(0).accepted);
    QVERIFY(ratingResult.providers.at(1).accepted);
    QCOMPARE(adapterA.sends, 1);
    QCOMPARE(adapterB.sends, 1);
    QCOMPARE(delivery.fixturePhase(), QStringLiteral("publishedMixedRating"));
    QCOMPARE(delivery.fixtureARatingSendCount(), 1);
    QCOMPARE(delivery.fixtureAReviewSendCount(), 0);
    QCOMPARE(delivery.fixtureBSendCount(), 1);
    QCOMPARE(delivery.fixtureBReconcileCount(), 0);
    QCOMPARE(delivery.fixtureSpoilerBlockedSendCount(), 0);
    QCOMPARE(delivery.unknownCount(), 1);

    const QString bRatingOperationId = ratingResult.providers.at(1).operationIds.first();
    adapterB.nextReconcile =
        RatingsReviewsDeliveryAdapter::ReconcileOutcome::MatchesIntendedState;
    QVERIFY(delivery.reconcileOperation(bRatingOperationId));
    QCOMPARE(adapterB.sends, 1);
    QCOMPARE(adapterB.reconciliations, 1);
    QCOMPARE(delivery.fixturePhase(), QStringLiteral("reconciledRating"));
    QCOMPARE(delivery.fixtureBSendCount(), 1);
    QCOMPARE(delivery.fixtureBReconcileCount(), 1);
    QCOMPARE(delivery.unknownCount(), 0);

    const auto reviewResult = delivery.publishCommitted(intentFor(
        &store, key, {{QStringLiteral("fixture-a"), false, true, std::nullopt}}));
    QCOMPARE(reviewResult.providers.size(), 1);
    QVERIFY(reviewResult.providers.first().accepted);
    QCOMPARE(adapterA.sends, 2);
    QCOMPARE(delivery.fixturePhase(), QStringLiteral("reviewPublished"));
    QCOMPARE(delivery.fixtureARatingSendCount(), 1);
    QCOMPARE(delivery.fixtureAReviewSendCount(), 1);

    QVERIFY(store.setSpoiler(fixtureIdentity(), true));
    const auto blockedResult = delivery.publishCommitted(intentFor(
        &store, key,
        {{QStringLiteral("fixture-spoiler-blocked"), false, true, std::nullopt}}));
    QCOMPARE(blockedResult.providers.size(), 1);
    QVERIFY(!blockedResult.providers.first().accepted);
    QVERIFY(blockedResult.providers.first().reason.contains(QStringLiteral("spoiler")));
    QCOMPARE(adapterSpoilerBlocked.sends, 0);
    QCOMPARE(delivery.fixturePhase(), QStringLiteral("spoilerBlocked"));
    QCOMPARE(delivery.fixtureSpoilerBlockedSendCount(), 0);
    QCOMPARE(delivery.pendingCount(), 0);
    QCOMPARE(delivery.unknownCount(), 0);

    const QVariantList publicRows = delivery.providerRows();
    QCOMPARE(publicRows.size(), 3);
    for (const QVariant &rowValue : publicRows) {
        const QVariantMap publicRow = rowValue.toMap();
        QVERIFY(!publicRow.contains(QStringLiteral("canonicalKey")));
        QVERIFY(!publicRow.contains(QStringLiteral("providerMediaId")));
        QVERIFY(!publicRow.contains(QStringLiteral("safePayload")));
        QVERIFY(!publicRow.contains(QStringLiteral("review")));
    }
    const QVariantList actionRows = delivery.deliveryProjection(key);
    QCOMPARE(actionRows.size(), 3);
    QVERIFY(actionRows.first().toMap().value(QStringLiteral("operationId")).isValid());
}

void RatingsReviewsDeliveryTest::R9_restart_rehydrates_without_republish_or_reseed()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity(), false,
                              QStringLiteral("persisted private review")));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    QVERIFY(writeMapping(value, QStringLiteral("fixture-a"), key));
    QVERIFY(writeMapping(value, QStringLiteral("fixture-b"), key));
    const auto record = store.recordByKey(key);
    QVERIFY(record.has_value());
    const QString digest = ratingsReviewsCanonicalPayloadDigestV1(*record);

    const auto makeCompletedOperation = [&](const QString &operationId,
                                            const QString &providerId,
                                            const QString &operationType,
                                            const QJsonObject &payload) {
        RatingsReviewsDeliveryOperation operation;
        operation.operationId = operationId;
        operation.profileId = value.profileId;
        operation.profileIncarnation = 4;
        operation.providerId = providerId;
        operation.connectionGeneration = 7;
        operation.operationType = operationType;
        operation.canonicalKey = key;
        operation.canonicalRevision = store.revision();
        operation.canonicalPayloadDigest = digest;
        operation.mappingProviderMediaId = QStringLiteral("remote-%1").arg(providerId);
        operation.conversionMapDigest = operationType == QLatin1String("rating.set")
            ? QString(64, QLatin1Char('a')) : QString();
        operation.intentCreatedAtMs = 1;
        operation.state = QStringLiteral("succeeded");
        operation.attemptCount = 1;
        operation.lastAttemptAtMs = 2;
        operation.retryClass = QStringLiteral("manualAfterUnknown");
        operation.adapterIdempotency = QStringLiteral("none");
        operation.safePayload = payload;
        return operation;
    };
    const QList<RatingsReviewsDeliveryOperation> completedOperations{
        makeCompletedOperation(
            QStringLiteral("1fcaa2f8-2cb5-4ce6-bfac-b075e9af5891"),
            QStringLiteral("fixture-a"), QStringLiteral("rating.set"),
            {{QStringLiteral("kind"), QStringLiteral("rating")},
             {QStringLiteral("native_value"), 81},
             {QStringLiteral("source_rating"), 8.5}}),
        makeCompletedOperation(
            QStringLiteral("df17a03c-f731-467d-8e40-c0f2f1c11fb9"),
            QStringLiteral("fixture-b"), QStringLiteral("rating.set"),
            {{QStringLiteral("kind"), QStringLiteral("rating")},
             {QStringLiteral("native_value"), 81},
             {QStringLiteral("source_rating"), 8.5}}),
        makeCompletedOperation(
            QStringLiteral("bb888261-235e-48a1-a714-cd76077c0707"),
            QStringLiteral("fixture-a"), QStringLiteral("review.set"),
            {{QStringLiteral("kind"), QStringLiteral("review")},
             {QStringLiteral("text"), QStringLiteral("persisted private review")},
             {QStringLiteral("spoiler"), false},
             {QStringLiteral("variant"), QStringLiteral("canonical")},
             {QStringLiteral("variant_digest"), QString(64, QLatin1Char('b'))}})};
    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    QVERIFY(outbox.replaceAll(completedOperations));
    RatingsReviewsDeliveryReceiptStore receipts(value.privatePaths.receiptsPath);
    for (const auto &operation : completedOperations) {
        RatingsReviewsDeliveryReceipt receipt;
        receipt.operationId = operation.operationId;
        receipt.providerId = operation.providerId;
        receipt.operationType = operation.operationType;
        receipt.canonicalKey = operation.canonicalKey;
        receipt.canonicalPayloadDigest = operation.canonicalPayloadDigest;
        receipt.status = QStringLiteral("succeeded");
        receipt.attemptNumber = operation.attemptCount;
        receipt.createdAtMs = operation.intentCreatedAtMs;
        receipt.updatedAtMs = 2;
        receipt.completedAtMs = 2;
        receipt.safeProviderStatus = QStringLiteral("ok");
        receipt.reconciled = operation.providerId == QLatin1String("fixture-b");
        QVERIFY(receipts.upsert(receipt));
    }

    FakeDeliveryAdapter oldA;
    FakeDeliveryAdapter oldB;
    RatingsReviewsDelivery beforeRestart;
    beforeRestart.setFixtureProviderForTests(
        QStringLiteral("fixture-a"), fixtureCapability(), &oldA);
    beforeRestart.setFixtureProviderForTests(
        QStringLiteral("fixture-b"), fixtureCapability(), &oldB);
    QVERIFY(beforeRestart.activateProfile(value));
    beforeRestart.deactivateProfile();

    FakeDeliveryAdapter afterRestartA;
    FakeDeliveryAdapter afterRestartB;
    RatingsReviewsDelivery afterRestart;
    afterRestart.setFixtureProviderForTests(
        QStringLiteral("fixture-a"), fixtureCapability(), &afterRestartA);
    afterRestart.setFixtureProviderForTests(
        QStringLiteral("fixture-b"), fixtureCapability(), &afterRestartB);
    QVERIFY(afterRestart.activateProfile(value));
    QCOMPARE(afterRestart.fixturePhase(), QStringLiteral("rehydrated"));
    QCOMPARE(afterRestart.fixtureARatingSendCount(), 1);
    QCOMPARE(afterRestart.fixtureAReviewSendCount(), 1);
    QCOMPARE(afterRestart.fixtureBSendCount(), 1);
    QCOMPARE(afterRestart.fixtureBReconcileCount(), 1);
    QCOMPARE(afterRestart.fixtureSpoilerBlockedSendCount(), 0);
    QCOMPARE(afterRestart.pendingCount(), 0);
    QCOMPARE(afterRestart.unknownCount(), 0);
    QCOMPARE(afterRestartA.sends, 0);
    QCOMPARE(afterRestartB.sends, 0);
    QCOMPARE(afterRestartA.reconciliations, 0);
    QCOMPARE(afterRestartB.reconciliations, 0);
    QCOMPARE(outbox.operations().size(), 3);
    QCOMPARE(receipts.receipts().size(), 3);
}

void RatingsReviewsDeliveryTest::R7_corrupt_receipt_binding_fails_closed()
{
    QTemporaryDir directory;
    RatingsReviewsStore store(directory.filePath(QStringLiteral("canonical.json")), [] { return 11; });
    QVERIFY(saveFixtureRecord(&store, fixtureIdentity()));
    const QString key = RatingsReviewsStore::recordKeyForIdentity(fixtureIdentity());
    const auto value = activation(directory, &store);
    RatingsReviewsDeliveryOutbox outbox(value.privatePaths.outboxPath);
    RatingsReviewsDeliveryOperation operation;
    operation.operationId = QStringLiteral("12b94b02-c15f-4c07-a28a-9db4d53dd8e0");
    operation.profileId = value.profileId;
    operation.profileIncarnation = 1;
    operation.providerId = QStringLiteral("fixture-a");
    operation.connectionGeneration = 7;
    operation.operationType = QStringLiteral("rating.set");
    operation.canonicalKey = key;
    operation.canonicalRevision = store.revision();
    operation.canonicalPayloadDigest = ratingsReviewsCanonicalPayloadDigestV1(*store.recordByKey(key));
    operation.mappingProviderMediaId = QStringLiteral("remote-fixture-a");
    operation.conversionMapDigest = QString(64, QLatin1Char('a'));
    operation.intentCreatedAtMs = 1;
    operation.state = QStringLiteral("inFlight");
    operation.attemptCount = 1;
    operation.retryClass = QStringLiteral("manualAfterUnknown");
    operation.adapterIdempotency = QStringLiteral("none");
    operation.safePayload = {{QStringLiteral("kind"), QStringLiteral("rating")}, {QStringLiteral("native_value"), 81}, {QStringLiteral("source_rating"), 8.5}};
    QVERIFY(outbox.append({operation}));
    RatingsReviewsDeliveryReceiptStore receipts(value.privatePaths.receiptsPath);
    RatingsReviewsDeliveryReceipt receipt;
    receipt.operationId = operation.operationId;
    receipt.providerId = operation.providerId;
    receipt.operationType = operation.operationType;
    receipt.canonicalKey = QStringLiteral("rr1:wrong");
    receipt.canonicalPayloadDigest = operation.canonicalPayloadDigest;
    receipt.status = QStringLiteral("succeeded");
    receipt.attemptNumber = 1;
    receipt.createdAtMs = 1;
    receipt.updatedAtMs = 1;
    receipt.completedAtMs = 1;
    receipt.safeProviderStatus = QStringLiteral("ok");
    QVERIFY(receipts.upsert(receipt));

    RatingsReviewsDelivery delivery;
    QString error;
    QVERIFY(!delivery.activateProfile(value, &error));
    QVERIFY(error.contains(QStringLiteral("contradictory")));
}

QTEST_GUILESS_MAIN(RatingsReviewsDeliveryTest)

#include "tst_ratings_reviews_delivery.moc"
