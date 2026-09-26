#include "RatingsReviewsDelivery.h"

#include "RatingsReviewsDeliveryOutbox.h"
#include "RatingsReviewsDeliveryReceiptStore.h"
#include "RatingsReviewsProviderMappingStore.h"
#include "RatingsReviewsStore.h"
#include "ProfilePreferencesStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include <algorithm>

namespace {

QString hashBytes(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool matchesReceiptBinding(const RatingsReviewsDeliveryReceipt &receipt,
                           const RatingsReviewsDeliveryOperation &operation)
{
    return receipt.operationId == operation.operationId
        && receipt.providerId == operation.providerId
        && receipt.operationType == operation.operationType
        && receipt.canonicalKey == operation.canonicalKey
        && receipt.canonicalPayloadDigest == operation.canonicalPayloadDigest
        && receipt.attemptNumber >= 1;
}

bool matchesReceiptHistory(const RatingsReviewsDeliveryReceipt &receipt,
                           const RatingsReviewsDeliveryOperation &operation)
{
    return matchesReceiptBinding(receipt, operation)
        && receipt.attemptNumber <= operation.attemptCount
        && (receipt.attemptNumber == operation.attemptCount
            || !ratingsReviewsDeliveryIsTerminalState(operation.state));
}

QString receiptState(const RatingsReviewsDeliveryReceipt &receipt)
{
    return receipt.status;
}

} // namespace

QString ratingsReviewsCanonicalPayloadDigestV1(
    const RatingsReviewsStore::Record &record)
{
    QJsonArray content;
    content.append(record.identity.world);
    content.append(record.identity.kind);
    content.append(record.identity.mediaId);
    content.append(record.rating ? QJsonValue(*record.rating) : QJsonValue(QJsonValue::Null));
    content.append(record.review ? QJsonValue(*record.review) : QJsonValue(QJsonValue::Null));
    content.append(record.spoiler);
    content.append(record.createdAtMs);
    content.append(record.updatedAtMs);
    return hashBytes(QJsonDocument(content).toJson(QJsonDocument::Compact));
}

RatingsReviewsDelivery::RatingsReviewsDelivery(Clock clock, QObject *parent)
    : QObject(parent)
    , m_clock(std::move(clock))
{
    if (!m_clock)
        m_clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
    setObjectName(QStringLiteral("ratingsReviewsDelivery"));
}

RatingsReviewsDelivery::~RatingsReviewsDelivery()
{
    deactivateProfile();
}

bool RatingsReviewsDelivery::activateProfile(
    const RatingsReviewsDeliveryActivation &activation,
    QString *error)
{
    deactivateProfile();
    if (activation.profileId.isEmpty() || !activation.canonicalStore
        || activation.privatePaths.mappingsPath.isEmpty()
        || activation.privatePaths.outboxPath.isEmpty()
        || activation.privatePaths.receiptsPath.isEmpty()) {
        return setError(error, QStringLiteral("Ratings/Reviews delivery activation requires an explicit writable profile binding."));
    }
    QString storeError;
    auto mappings = std::make_unique<RatingsReviewsProviderMappingStore>(
        activation.privatePaths.mappingsPath);
    auto outbox = std::make_unique<RatingsReviewsDeliveryOutbox>(
        activation.privatePaths.outboxPath);
    auto receipts = std::make_unique<RatingsReviewsDeliveryReceiptStore>(
        activation.privatePaths.receiptsPath);
    if (!activation.canonicalStore->healthy(&storeError)
        || !mappings->healthy(&storeError)
        || !outbox->healthy(&storeError)
        || !receipts->healthy(&storeError)) {
        return setError(error, storeError.isEmpty()
                ? QStringLiteral("Ratings/Reviews delivery private state is unhealthy.") : storeError);
    }

    QList<RatingsReviewsDeliveryOperation> recovered = outbox->operations();
    for (auto &operation : recovered) {
        const auto receipt = receipts->receipt(operation.operationId);
        if (receipt) {
            if (!matchesReceiptHistory(*receipt, operation)) {
                return setError(error, QStringLiteral("Ratings/Reviews receipt binding is contradictory."));
            }
            if (receipt->attemptNumber == operation.attemptCount) {
                operation.state = receiptState(*receipt);
                continue;
            }
            // A receipt for an earlier attempt is history, not terminal
            // evidence for a later durable attempt marker.
        }
        if (operation.state == QLatin1String("inFlight")
            || (operation.state == QLatin1String("retrying")
                && operation.attemptCount > 0)) {
            operation.state = QStringLiteral("unknownOutcome");
        }
    }
    if (!outbox->replaceAll(recovered, &storeError))
        return setError(error, storeError);

    ++m_activeIncarnation;
    m_activeProfileId = activation.profileId;
    m_canonicalStore = activation.canonicalStore;
    m_mappingStore = std::move(mappings);
    m_outbox = std::move(outbox);
    m_receipts = std::move(receipts);
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    m_fixturePhase = m_outbox->operations().isEmpty()
        ? QStringLiteral("ready") : QStringLiteral("rehydrated");
#endif
    replaceProjectionState();

    // A pending intent has never crossed a provider boundary.  Resume it only
    // after the freshly activated profile, canonical record, mapping, and
    // fixture binding still agree; dispatchPending turns every stale binding
    // into needsAttention before it can send.
    for (const auto &operation : m_outbox->operations()) {
        if (operation.state != QLatin1String("pending")
            && operation.state != QLatin1String("retrying")) {
            continue;
        }
        if (!dispatchPending(operation.operationId, &storeError)) {
            deactivateProfile();
            return setError(error, storeError.isEmpty()
                    ? QStringLiteral("Ratings/Reviews delivery recovery failed.")
                    : storeError);
        }
    }
    return true;
}

void RatingsReviewsDelivery::deactivateProfile()
{
    ++m_activeIncarnation;
    if (m_outbox && m_outbox->healthy()) {
        QList<RatingsReviewsDeliveryOperation> candidate = m_outbox->operations();
        bool changed = false;
        for (auto &operation : candidate) {
            if (operation.state == QLatin1String("inFlight")
                && (!m_receipts || !m_receipts->receipt(operation.operationId))) {
                operation.state = QStringLiteral("unknownOutcome");
                changed = true;
            }
        }
        if (changed)
            m_outbox->replaceAll(candidate, nullptr);
    }
    m_mappingStore.reset();
    m_outbox.reset();
    m_receipts.reset();
    m_canonicalStore = nullptr;
    m_activeProfileId.clear();
    replaceProjectionState();
}

bool RatingsReviewsDelivery::active() const { return m_canonicalStore && m_outbox && m_receipts && m_mappingStore; }
QString RatingsReviewsDelivery::activeProfileId() const { return m_activeProfileId; }
quint64 RatingsReviewsDelivery::activeProfileIncarnation() const { return m_activeIncarnation; }

int RatingsReviewsDelivery::pendingCount() const
{
    if (!m_outbox)
        return 0;
    int count = 0;
    for (const auto &operation : m_outbox->operations()) {
        if (operation.state == QLatin1String("pending")
            || operation.state == QLatin1String("retrying")
            || operation.state == QLatin1String("inFlight")) {
            ++count;
        }
    }
    return count;
}

int RatingsReviewsDelivery::unknownCount() const
{
    if (!m_outbox)
        return 0;
    int count = 0;
    for (const auto &operation : m_outbox->operations()) {
        if (operation.state == QLatin1String("unknownOutcome"))
            ++count;
    }
    return count;
}

QVariantList RatingsReviewsDelivery::providerRows() const
{
    QVariantList rows;
    if (!m_outbox)
        return rows;
    for (const auto &operation : m_outbox->operations()) {
        // Deliberately exclude canonical keys, provider media IDs, payload,
        // review text, and remote response fields from this public projection.
        rows.append(QVariantMap{
            {QStringLiteral("providerId"), operation.providerId},
            {QStringLiteral("operationType"), operation.operationType},
            {QStringLiteral("state"), operation.state},
            {QStringLiteral("staleReason"), operation.staleReason},
            {QStringLiteral("attemptCount"), operation.attemptCount}});
    }
    return rows;
}

#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
QString RatingsReviewsDelivery::fixturePhase() const { return m_fixturePhase; }

int RatingsReviewsDelivery::fixtureSendCount(const QString &providerId) const
{
    if (!m_outbox)
        return 0;
    int count = 0;
    for (const auto &operation : m_outbox->operations()) {
        if (operation.providerId == providerId)
            count += operation.attemptCount;
    }
    return count;
}

int RatingsReviewsDelivery::fixtureReconcileCount(const QString &providerId) const
{
    if (!m_receipts)
        return 0;
    int count = 0;
    for (const auto &receipt : m_receipts->receipts()) {
        if (receipt.providerId == providerId && receipt.reconciled)
            ++count;
    }
    return count;
}

int RatingsReviewsDelivery::fixtureARatingSendCount() const
{
    if (!m_outbox)
        return 0;
    int count = 0;
    for (const auto &operation : m_outbox->operations()) {
        if (operation.providerId == QLatin1String("fixture-a")
            && operation.operationType == QLatin1String("rating.set"))
            count += operation.attemptCount;
    }
    return count;
}

int RatingsReviewsDelivery::fixtureAReviewSendCount() const
{
    if (!m_outbox)
        return 0;
    int count = 0;
    for (const auto &operation : m_outbox->operations()) {
        if (operation.providerId == QLatin1String("fixture-a")
            && operation.operationType == QLatin1String("review.set"))
            count += operation.attemptCount;
    }
    return count;
}

int RatingsReviewsDelivery::fixtureBSendCount() const
{
    return fixtureSendCount(QStringLiteral("fixture-b"));
}

int RatingsReviewsDelivery::fixtureBReconcileCount() const
{
    return fixtureReconcileCount(QStringLiteral("fixture-b"));
}

int RatingsReviewsDelivery::fixtureSpoilerBlockedSendCount() const
{
    return fixtureSendCount(QStringLiteral("fixture-spoiler-blocked"));
}

void RatingsReviewsDelivery::updateFixturePhase(bool spoilerBlocked)
{
    if (m_fixturePhase == QLatin1String("rehydrated"))
        return;
    if (spoilerBlocked) {
        m_fixturePhase = QStringLiteral("spoilerBlocked");
        return;
    }
    if (m_fixturePhase == QLatin1String("spoilerBlocked") || !m_outbox)
        return;
    bool aRatingSucceeded = false;
    bool aReviewSucceeded = false;
    bool bRatingUnknown = false;
    bool bRatingSucceeded = false;
    for (const auto &operation : m_outbox->operations()) {
        if (operation.providerId == QLatin1String("fixture-a")
            && operation.operationType == QLatin1String("rating.set")
            && operation.state == QLatin1String("succeeded"))
            aRatingSucceeded = true;
        if (operation.providerId == QLatin1String("fixture-a")
            && operation.operationType == QLatin1String("review.set")
            && operation.state == QLatin1String("succeeded"))
            aReviewSucceeded = true;
        if (operation.providerId == QLatin1String("fixture-b")
            && operation.operationType == QLatin1String("rating.set")) {
            bRatingUnknown = operation.state == QLatin1String("unknownOutcome");
            bRatingSucceeded = operation.state == QLatin1String("succeeded");
        }
    }
    if (aReviewSucceeded)
        m_fixturePhase = QStringLiteral("reviewPublished");
    else if (aRatingSucceeded && bRatingSucceeded && fixtureBReconcileCount() > 0)
        m_fixturePhase = QStringLiteral("reconciledRating");
    else if (aRatingSucceeded && bRatingUnknown)
        m_fixturePhase = QStringLiteral("publishedMixedRating");
}
#endif

RatingsReviewsPublishResult RatingsReviewsDelivery::publishCommitted(
    const RatingsReviewsPublishIntent &intent)
{
    RatingsReviewsPublishResult result;
    bool spoilerBlocked = false;
    if (!active() || intent.canonicalKey.isEmpty()
        || !ratingsReviewsDeliveryIsSha256(intent.canonicalPayloadDigest)) {
        return result;
    }
    const auto record = m_canonicalStore->recordByKey(intent.canonicalKey);
    if (!record || ratingsReviewsCanonicalPayloadDigestV1(*record) != intent.canonicalPayloadDigest)
        return result;

    for (const auto &destination : intent.destinations) {
        RatingsReviewsPublishProviderResult providerResult;
        providerResult.providerId = destination.providerId;
        const auto fixture = m_fixtureProviders.constFind(destination.providerId);
        const auto mapping = mappingForFixtureTitle(
            destination.providerId, intent.canonicalKey, &*record);
        if (fixture == m_fixtureProviders.cend() || !fixture->adapter) {
            providerResult.reason = QStringLiteral("No delivery adapter is available.");
        } else if (!fixture->capability.connected) {
            providerResult.reason = QStringLiteral("Provider is not connected.");
        } else if (!mapping || mapping->status != QLatin1String("matched")
                   || mapping->connectionGeneration != fixture->capability.connectionGeneration) {
            providerResult.reason = QStringLiteral("Provider title mapping is not matched.");
        } else if (destination.reviewSelected && record->spoiler
                   && !fixture->capability.spoilerMetadata) {
            providerResult.reason = QStringLiteral("This provider cannot preserve spoiler protection.");
            spoilerBlocked = true;
        } else {
            QList<RatingsReviewsDeliveryOperation> created;
            const auto createOperation = [&](const QString &type,
                                             QJsonObject payload,
                                             const QString &mapDigest) {
                RatingsReviewsDeliveryOperation operation;
                operation.operationId = newOperationId();
                operation.profileId = m_activeProfileId;
                operation.profileIncarnation = m_activeIncarnation;
                operation.providerId = destination.providerId;
                operation.connectionGeneration = fixture->capability.connectionGeneration;
                operation.operationType = type;
                operation.canonicalKey = intent.canonicalKey;
                operation.canonicalRevision = intent.committedStoreRevision;
                operation.canonicalPayloadDigest = intent.canonicalPayloadDigest;
                operation.mappingProviderMediaId = mapping->providerMediaId;
                operation.conversionMapDigest = mapDigest;
                operation.intentCreatedAtMs = now();
                operation.state = QStringLiteral("pending");
                operation.retryClass = QStringLiteral("manualAfterUnknown");
                operation.adapterIdempotency = QStringLiteral("none");
                operation.safePayload = std::move(payload);
                created.append(operation);
            };
            if (destination.ratingSelected && record->rating && fixture->capability.ratingCapable) {
                QJsonValue translated = fixture->capability.translatedRating.isUndefined()
                    ? QJsonValue(*record->rating) : fixture->capability.translatedRating;
                QString mapDigest = fixture->capability.conversionMapDigest;
                if (m_fixturePreferences) {
                    const auto map = m_fixturePreferences->ratingsReviewsConversionMap(destination.providerId);
                    bool available = false;
                    if (!map) {
                        providerResult.reason = QStringLiteral("No committed rating conversion map is available.");
                    } else {
                        translated = map->translatedOutput(*record->rating, &available);
                        if (!available)
                            providerResult.reason = QStringLiteral("Rating is unavailable on this provider.");
                        else
                            mapDigest = map->digest();
                    }
                }
                if (providerResult.reason.isEmpty()) {
                    QJsonObject payload = {
                        {QStringLiteral("kind"), QStringLiteral("rating")},
                        {QStringLiteral("native_value"), translated},
                        {QStringLiteral("source_rating"), *record->rating}};
                    createOperation(QStringLiteral("rating.set"), payload, mapDigest);
                }
            }
            if (destination.reviewSelected && record->review && fixture->capability.reviewCapable) {
                const QString text = destination.shortenedReview
                    ? *destination.shortenedReview : *record->review;
                if (fixture->capability.reviewTextLimit > 0
                    && text.size() > fixture->capability.reviewTextLimit) {
                    providerResult.reason = QStringLiteral("A user-authored shorter review is required.");
                    created.clear();
                } else {
                    QJsonObject payload = {
                        {QStringLiteral("kind"), QStringLiteral("review")},
                        {QStringLiteral("text"), text},
                        {QStringLiteral("spoiler"), record->spoiler},
                        {QStringLiteral("variant"), destination.shortenedReview
                            ? QStringLiteral("shortened") : QStringLiteral("canonical")},
                        {QStringLiteral("variant_digest"), hashBytes(text.toUtf8())}};
                    createOperation(QStringLiteral("review.set"), payload, QString());
                }
            }
            if (created.isEmpty() && providerResult.reason.isEmpty()) {
                providerResult.reason = QStringLiteral("No selected provider field is eligible.");
            } else if (!created.isEmpty()) {
                for (auto &operation : created) {
                    if (hasUnknownBarrier(operation)) {
                        operation.state = QStringLiteral("needsAttention");
                        operation.staleReason = QStringLiteral("unknown_target_barrier");
                    }
                }
                QString error;
                if (!m_outbox->append(created, &error)) {
                    providerResult.reason = error;
                } else {
                    providerResult.accepted = true;
                    for (const auto &operation : created) {
                        providerResult.operationIds.append(operation.operationId);
                        if (operation.state == QLatin1String("pending"))
                            dispatchPending(operation.operationId, nullptr);
                    }
                }
            }
        }
        result.providers.append(providerResult);
    }
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    updateFixturePhase(spoilerBlocked);
#endif
    replaceProjectionState();
    return result;
}

bool RatingsReviewsDelivery::retryOperation(const QString &operationId, QString *error)
{
    const auto candidate = operation(operationId);
    if (!candidate)
        return setError(error, QStringLiteral("Ratings/Reviews delivery operation was not found."));
    if (candidate->state == QLatin1String("unknownOutcome"))
        return setError(error, QStringLiteral("Unknown delivery outcomes require reconciliation first."));
    if (candidate->state != QLatin1String("needsAttention")
        && candidate->state != QLatin1String("failedTerminal")
        && candidate->state != QLatin1String("retrying")) {
        return setError(error, QStringLiteral("Ratings/Reviews delivery operation is not retryable."));
    }
    RatingsReviewsDeliveryOperation retry = *candidate;
    QString reason;
    if (!operationIsCurrent(retry, &reason)) {
        retry.state = QStringLiteral("needsAttention");
        retry.staleReason = reason;
        const bool replaced = m_outbox->replace(retry, error);
        if (replaced)
            replaceProjectionState();
        return replaced;
    }
    retry.state = QStringLiteral("retrying");
    if (!m_outbox->replace(retry, error))
        return false;
    return dispatchPending(operationId, error);
}

bool RatingsReviewsDelivery::reconcileOperation(const QString &operationId, QString *error)
{
    const auto candidate = operation(operationId);
    if (!candidate || candidate->state != QLatin1String("unknownOutcome"))
        return setError(error, QStringLiteral("Ratings/Reviews delivery operation is not unknown."));
    const auto fixture = m_fixtureProviders.constFind(candidate->providerId);
    if (fixture == m_fixtureProviders.cend() || !fixture->adapter
        || !fixture->capability.connected
        || fixture->capability.connectionGeneration != candidate->connectionGeneration) {
        return setError(error, QStringLiteral("Original provider authority cannot be proven for reconciliation."));
    }
    const QString reconcileProfileId = m_activeProfileId;
    const quint64 reconcileIncarnation = m_activeIncarnation;
    RatingsReviewsDeliveryAdapter *const reconcileAdapter = fixture->adapter;
    const auto outcome = reconcileAdapter->reconcile(*candidate);
    if (!active() || m_activeProfileId != reconcileProfileId
        || m_activeIncarnation != reconcileIncarnation) {
        return true;
    }
    const auto currentFixture = m_fixtureProviders.constFind(candidate->providerId);
    if (currentFixture == m_fixtureProviders.cend()
        || !currentFixture->capability.connected
        || currentFixture->capability.connectionGeneration != candidate->connectionGeneration
        || currentFixture->adapter != reconcileAdapter) {
        // The result came from an authority that is no longer connected.
        // Keep the original attempt unresolved; never certify a new binding.
        auto unresolved = *candidate;
        unresolved.staleReason = QStringLiteral("provider_binding_changed");
        if (!m_outbox->replace(unresolved, error))
            return false;
        replaceProjectionState();
        return true;
    }
    switch (outcome) {
    case RatingsReviewsDeliveryAdapter::ReconcileOutcome::MatchesIntendedState: {
        RatingsReviewsDeliveryAdapter::Result result;
        result.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::Success;
        result.safeProviderStatus = QStringLiteral("reconciled");
        auto settled = *candidate;
        return markTerminal(&settled, QStringLiteral("succeeded"), result, true, error);
    }
    case RatingsReviewsDeliveryAdapter::ReconcileOutcome::ProvesAbsent: {
        if (candidate->adapterIdempotency != QLatin1String("safe"))
            return setError(error, QStringLiteral("Adapter does not certify safe resend after unknown outcome."));
        auto retry = *candidate;
        retry.state = QStringLiteral("retrying");
        if (!m_outbox->replace(retry, error))
            return false;
        return dispatchPending(operationId, error);
    }
    case RatingsReviewsDeliveryAdapter::ReconcileOutcome::NewerOrDifferentState: {
        auto settled = *candidate;
        RatingsReviewsDeliveryAdapter::Result result;
        result.outcome = RatingsReviewsDeliveryAdapter::SendOutcome::NeedsAttention;
        result.safeErrorClass = QStringLiteral("newer_or_different_remote_state");
        return markTerminal(&settled, QStringLiteral("needsAttention"), result, true, error);
    }
    case RatingsReviewsDeliveryAdapter::ReconcileOutcome::Indeterminate:
        replaceProjectionState();
        return true;
    }
    return false;
}

QVariantList RatingsReviewsDelivery::publishDestinations(
    const QString &canonicalKey)
{
    QVariantList rows;
    if (!active() || canonicalKey.isEmpty())
        return rows;

    QStringList providerIds = m_fixtureProviders.keys();
    providerIds.sort();
    const auto record = m_canonicalStore->recordByKey(canonicalKey);
    for (const QString &providerId : providerIds) {
        const FixtureProvider &fixture = m_fixtureProviders.value(providerId);
        const auto mapping = mappingForFixtureTitle(providerId, canonicalKey,
                                                     record ? &*record : nullptr);
        const bool exactFixtureBeforeFirstSave = !record && !mapping
            && m_taggedFixtureIdentity
            && canonicalKey == RatingsReviewsStore::recordKeyForIdentity(
                   *m_taggedFixtureIdentity);
        const bool mappingMatched = exactFixtureBeforeFirstSave || (mapping
            && mapping->status == QLatin1String("matched")
            && mapping->connectionGeneration
                == fixture.capability.connectionGeneration);
        const bool reviewBlockedBySpoiler = record
            && record->spoiler
            && !fixture.capability.spoilerMetadata;
        QString reason;
        if (!fixture.adapter)
            reason = QStringLiteral("No delivery adapter is available.");
        else if (!fixture.capability.connected)
            reason = QStringLiteral("Provider is not connected.");
        else if (!mappingMatched)
            reason = QStringLiteral("Provider title mapping is not matched.");
        else if (reviewBlockedBySpoiler)
            reason = QStringLiteral("This provider cannot preserve spoiler protection.");

        rows.append(QVariantMap{
            {QStringLiteral("providerId"), providerId},
            {QStringLiteral("ratingEligible"), reason.isEmpty()
                && fixture.capability.ratingCapable},
            {QStringLiteral("reviewEligible"), reason.isEmpty()
                && fixture.capability.reviewCapable},
            {QStringLiteral("ratingDefault"), m_fixturePreferences
                && m_fixturePreferences->ratingsReviewsDefaultRatingDestinations().contains(providerId)},
            {QStringLiteral("reviewDefault"), m_fixturePreferences
                && m_fixturePreferences->ratingsReviewsDefaultReviewDestinations().contains(providerId)},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("reviewPublic"), false}});
    }
    return rows;
}

QVariantList RatingsReviewsDelivery::deliveryProjection(const QString &canonicalKey) const
{
    QVariantList rows;
    if (!m_outbox)
        return rows;
    for (const auto &operation : m_outbox->operations()) {
        if (operation.canonicalKey != canonicalKey)
            continue;
        rows.append(QVariantMap{
            {QStringLiteral("operationId"), operation.operationId},
            {QStringLiteral("providerId"), operation.providerId},
            {QStringLiteral("operationType"), operation.operationType},
            {QStringLiteral("state"), operation.state},
            {QStringLiteral("staleReason"), operation.staleReason},
            {QStringLiteral("attemptCount"), operation.attemptCount}});
    }
    return rows;
}

void RatingsReviewsDelivery::setFixtureProviderForTests(
    const QString &providerId,
    const RatingsReviewsDeliveryProviderCapability &capability,
    RatingsReviewsDeliveryAdapter *adapter)
{
    m_fixtureProviders.insert(providerId, {capability, adapter});
}

void RatingsReviewsDelivery::setFixturePreferencesForTests(ProfilePreferencesStore *preferences)
{
    m_fixturePreferences = preferences;
}

void RatingsReviewsDelivery::enableTaggedFixtureMappingForTests(
    const RatingsReviewsStore::Identity &identity)
{
    m_taggedFixtureIdentity = identity;
}

void RatingsReviewsDelivery::clearFixtureProvidersForTests()
{
    m_fixtureProviders.clear();
    m_fixturePreferences = nullptr;
    m_taggedFixtureIdentity.reset();
}

std::optional<RatingsReviewsProviderMapping>
RatingsReviewsDelivery::mappingForFixtureTitle(
    const QString &providerId,
    const QString &canonicalKey,
    const RatingsReviewsStore::Record *record)
{
    if (!m_mappingStore)
        return std::nullopt;
    const auto existing = m_mappingStore->mapping(providerId, canonicalKey);
    if (existing || !m_taggedFixtureIdentity || !record
        || record->identity.world != m_taggedFixtureIdentity->world
        || record->identity.kind != m_taggedFixtureIdentity->kind
        || record->identity.mediaId != m_taggedFixtureIdentity->mediaId) {
        return existing;
    }
    const auto fixture = m_fixtureProviders.constFind(providerId);
    if (fixture == m_fixtureProviders.cend() || !fixture->adapter)
        return std::nullopt;

    RatingsReviewsProviderMapping mapping;
    mapping.providerId = providerId;
    mapping.canonicalKey = canonicalKey;
    mapping.status = QStringLiteral("matched");
    mapping.providerMediaId = QStringLiteral("fixture-%1-title").arg(providerId);
    mapping.resolution = QStringLiteral("exact");
    mapping.confirmedAtMs = now();
    mapping.connectionGeneration = fixture->capability.connectionGeneration;
    if (!m_mappingStore->upsert(mapping, nullptr))
        return std::nullopt;
    return mapping;
}

bool RatingsReviewsDelivery::handoffPrivateState(
    const RatingsReviewsPrivateProfileBinding &source,
    const RatingsReviewsPrivateProfileBinding &destination,
    RatingsReviewsStore *destinationCanonical,
    QString *error)
{
    if (source.profileId.isEmpty() || destination.profileId.isEmpty()
        || !destinationCanonical
        || source.privatePaths.mappingsPath.isEmpty()
        || source.privatePaths.outboxPath.isEmpty()
        || source.privatePaths.receiptsPath.isEmpty()
        || destination.privatePaths.mappingsPath.isEmpty()
        || destination.privatePaths.outboxPath.isEmpty()
        || destination.privatePaths.receiptsPath.isEmpty()) {
        return setError(error, QStringLiteral("Ratings/Reviews private handoff requires explicit source and destination bindings."));
    }
    QString storeError;
    RatingsReviewsProviderMappingStore sourceMappings(source.privatePaths.mappingsPath);
    RatingsReviewsDeliveryOutbox sourceOutbox(source.privatePaths.outboxPath);
    RatingsReviewsDeliveryReceiptStore sourceReceipts(source.privatePaths.receiptsPath);
    RatingsReviewsProviderMappingStore destinationMappings(destination.privatePaths.mappingsPath);
    RatingsReviewsDeliveryOutbox destinationOutbox(destination.privatePaths.outboxPath);
    RatingsReviewsDeliveryReceiptStore destinationReceipts(destination.privatePaths.receiptsPath);
    if (!destinationCanonical->healthy(&storeError)
        || !sourceMappings.healthy(&storeError)
        || !sourceOutbox.healthy(&storeError)
        || !sourceReceipts.healthy(&storeError)
        || !destinationMappings.healthy(&storeError)
        || !destinationOutbox.healthy(&storeError)
        || !destinationReceipts.healthy(&storeError)) {
        return setError(error, storeError.isEmpty()
                ? QStringLiteral("Ratings/Reviews private handoff state is unhealthy.") : storeError);
    }

    QList<RatingsReviewsDeliveryReceipt> receipts = destinationReceipts.receipts();
    for (const auto &sourceReceipt : sourceReceipts.receipts()) {
        bool found = false;
        for (const auto &destinationReceipt : receipts) {
            if (destinationReceipt.operationId == sourceReceipt.operationId) {
                if (ratingsReviewsDeliveryReceiptToJson(destinationReceipt)
                    != ratingsReviewsDeliveryReceiptToJson(sourceReceipt)) {
                    return setError(error, QStringLiteral("Ratings/Reviews private handoff found conflicting receipt identity."));
                }
                found = true;
                break;
            }
        }
        if (!found)
            receipts.append(sourceReceipt);
    }

    QHash<QString, RatingsReviewsDeliveryReceipt> receiptsByOperation;
    for (const auto &receipt : receipts) {
        if (!ratingsReviewsDeliveryIsTerminalState(receipt.status)) {
            return setError(error, QStringLiteral("Ratings/Reviews private handoff found a nonterminal receipt."));
        }
        receiptsByOperation.insert(receipt.operationId, receipt);
    }

    QList<RatingsReviewsProviderMapping> mappings = destinationMappings.mappings();
    for (const auto &sourceMapping : sourceMappings.mappings()) {
        bool found = false;
        for (const auto &destinationMapping : mappings) {
            if (destinationMapping.providerId == sourceMapping.providerId
                && destinationMapping.canonicalKey == sourceMapping.canonicalKey) {
                if (ratingsReviewsDeliveryMappingToJson(destinationMapping)
                    != ratingsReviewsDeliveryMappingToJson(sourceMapping)) {
                    return setError(error, QStringLiteral("Ratings/Reviews private handoff found conflicting provider mapping."));
                }
                found = true;
                break;
            }
        }
        if (!found)
            mappings.append(sourceMapping);
    }

    QList<RatingsReviewsDeliveryOperation> operations = destinationOutbox.operations();
    const auto normalizeForDestination = [&](RatingsReviewsDeliveryOperation *operation) {
        const auto receipt = receiptsByOperation.constFind(operation->operationId);
        if (receipt != receiptsByOperation.cend()) {
            if (!matchesReceiptHistory(*receipt, *operation))
                return setError(error, QStringLiteral("Ratings/Reviews receipt binding is contradictory during private handoff."));
            if (receipt->attemptNumber == operation->attemptCount)
                operation->state = receipt->status;
        }
        if (operation->state == QLatin1String("inFlight")
            || (operation->state == QLatin1String("retrying")
                && operation->attemptCount > 0)) {
            // A request may have crossed the provider boundary. Without a
            // receipt for this attempt, adoption must retain ambiguity.
            operation->state = QStringLiteral("unknownOutcome");
        }

        const auto canonical = destinationCanonical->recordByKey(operation->canonicalKey);
        const bool digestMatches = canonical
            && ratingsReviewsCanonicalPayloadDigestV1(*canonical)
                == operation->canonicalPayloadDigest;
        if (digestMatches) {
            operation->canonicalRevision = destinationCanonical->revision();
        } else {
            if (operation->staleReason.isEmpty())
                operation->staleReason = QStringLiteral("canonical_superseded_by_adoption");
            if (operation->state == QLatin1String("pending")
                || operation->state == QLatin1String("retrying")) {
                operation->state = QStringLiteral("needsAttention");
            }
        }
        return true;
    };

    for (auto &destinationOperation : operations) {
        if (destinationOperation.profileId != destination.profileId) {
            return setError(error, QStringLiteral("Ratings/Reviews destination outbox contains a foreign profile binding."));
        }
        if (!normalizeForDestination(&destinationOperation))
            return false;
    }
    for (auto sourceOperation : sourceOutbox.operations()) {
        sourceOperation.profileId = destination.profileId;
        if (!normalizeForDestination(&sourceOperation))
            return false;

        bool found = false;
        for (auto &destinationOperation : operations) {
            if (destinationOperation.operationId != sourceOperation.operationId)
                continue;
            if (ratingsReviewsDeliveryOperationToJson(destinationOperation)
                != ratingsReviewsDeliveryOperationToJson(sourceOperation)) {
                return setError(error, QStringLiteral("Ratings/Reviews private handoff found conflicting operation identity."));
            }
            found = true;
            break;
        }
        if (!found)
            operations.append(sourceOperation);
    }

    for (const auto &receipt : receipts) {
        bool joined = false;
        for (const auto &operation : operations) {
            if (operation.operationId != receipt.operationId)
                continue;
            if (!matchesReceiptHistory(receipt, operation))
                return setError(error, QStringLiteral("Ratings/Reviews receipt binding is contradictory during private handoff."));
            joined = true;
            break;
        }
        if (!joined)
            return setError(error, QStringLiteral("Ratings/Reviews private handoff found an orphan receipt."));
    }

    // Source files remain untouched until the existing adoption lifecycle has
    // independently proved retirement safe. Destination writes are verified
    // after each atomic owner commit, so a retry is idempotent after a crash.
    if (!destinationMappings.replaceAll(mappings, &storeError)
        || !destinationOutbox.replaceAll(operations, &storeError)
        || !destinationReceipts.replaceAll(receipts, &storeError)) {
        return setError(error, storeError);
    }
    RatingsReviewsProviderMappingStore mappingReadback(destination.privatePaths.mappingsPath);
    RatingsReviewsDeliveryOutbox outboxReadback(destination.privatePaths.outboxPath);
    RatingsReviewsDeliveryReceiptStore receiptReadback(destination.privatePaths.receiptsPath);
    if (!mappingReadback.healthy(&storeError)
        || !outboxReadback.healthy(&storeError)
        || !receiptReadback.healthy(&storeError)
        || mappingReadback.mappings().size() != mappings.size()
        || outboxReadback.operations().size() != operations.size()
        || receiptReadback.receipts().size() != receipts.size()) {
        return setError(error, storeError.isEmpty()
                ? QStringLiteral("Ratings/Reviews private handoff readback failed.") : storeError);
    }

    const auto readMappings = mappingReadback.mappings();
    const auto readOperations = outboxReadback.operations();
    const auto readReceipts = receiptReadback.receipts();
    for (qsizetype index = 0; index < mappings.size(); ++index) {
        if (ratingsReviewsDeliveryMappingToJson(readMappings.at(index))
            != ratingsReviewsDeliveryMappingToJson(mappings.at(index))) {
            return setError(error, QStringLiteral("Ratings/Reviews mapping handoff readback changed data."));
        }
    }
    for (qsizetype index = 0; index < operations.size(); ++index) {
        if (ratingsReviewsDeliveryOperationToJson(readOperations.at(index))
            != ratingsReviewsDeliveryOperationToJson(operations.at(index))) {
            return setError(error, QStringLiteral("Ratings/Reviews outbox handoff readback changed data."));
        }
    }
    for (qsizetype index = 0; index < receipts.size(); ++index) {
        if (ratingsReviewsDeliveryReceiptToJson(readReceipts.at(index))
            != ratingsReviewsDeliveryReceiptToJson(receipts.at(index))) {
            return setError(error, QStringLiteral("Ratings/Reviews receipt handoff readback changed data."));
        }
    }
    return true;
}

bool RatingsReviewsDelivery::dispatchPending(const QString &operationId, QString *error)
{
    const auto candidate = operation(operationId);
    if (!candidate)
        return setError(error, QStringLiteral("Ratings/Reviews delivery operation disappeared."));
    if (candidate->state != QLatin1String("pending")
        && candidate->state != QLatin1String("retrying")) {
        return true;
    }
    QString reason;
    if (!operationIsCurrent(*candidate, &reason)) {
        auto stale = *candidate;
        stale.state = QStringLiteral("needsAttention");
        stale.staleReason = reason;
        const bool replaced = m_outbox->replace(stale, error);
        if (replaced)
            replaceProjectionState();
        return replaced;
    }
    const auto fixture = m_fixtureProviders.constFind(candidate->providerId);
    if (fixture == m_fixtureProviders.cend() || !fixture->adapter)
        return setError(error, QStringLiteral("No deterministic delivery adapter is registered."));
    auto inFlight = *candidate;
    inFlight.state = QStringLiteral("inFlight");
    ++inFlight.attemptCount;
    inFlight.lastAttemptAtMs = now();
    if (!m_outbox->replace(inFlight, error))
        return false;

    const QString dispatchProfileId = m_activeProfileId;
    const quint64 dispatchIncarnation = m_activeIncarnation;
    RatingsReviewsDeliveryAdapter *const dispatchAdapter = fixture->adapter;
    const RatingsReviewsDeliveryAdapter::Result result = dispatchAdapter->send(inFlight);
    if (!active() || m_activeProfileId != dispatchProfileId
        || m_activeIncarnation != dispatchIncarnation) {
        return true;
    }
    const auto currentFixture = m_fixtureProviders.constFind(inFlight.providerId);
    if (currentFixture == m_fixtureProviders.cend()
        || !currentFixture->capability.connected
        || currentFixture->capability.connectionGeneration != inFlight.connectionGeneration
        || currentFixture->adapter != dispatchAdapter) {
        // The request may have crossed the old connection boundary. A reply
        // from that obsolete authority is not a terminal receipt.
        inFlight.state = QStringLiteral("unknownOutcome");
        inFlight.staleReason = QStringLiteral("provider_binding_changed");
        if (!m_outbox->replace(inFlight, error))
            return false;
        replaceProjectionState();
        return true;
    }
    switch (result.outcome) {
    case RatingsReviewsDeliveryAdapter::SendOutcome::Success:
        return markTerminal(&inFlight, QStringLiteral("succeeded"), result, false, error);
    case RatingsReviewsDeliveryAdapter::SendOutcome::KnownProviderFailure:
        return markTerminal(&inFlight, QStringLiteral("failedTerminal"), result, false, error);
    case RatingsReviewsDeliveryAdapter::SendOutcome::NeedsAttention:
        return markTerminal(&inFlight, QStringLiteral("needsAttention"), result, false, error);
    case RatingsReviewsDeliveryAdapter::SendOutcome::TransientBeforeSend:
        inFlight.state = QStringLiteral("retrying");
        if (!m_outbox->replace(inFlight, error))
            return false;
        replaceProjectionState();
        return true;
    case RatingsReviewsDeliveryAdapter::SendOutcome::UnknownAfterDispatch:
        inFlight.state = QStringLiteral("unknownOutcome");
        if (!m_outbox->replace(inFlight, error))
            return false;
        replaceProjectionState();
        return true;
    }
    return false;
}

bool RatingsReviewsDelivery::markTerminal(
    RatingsReviewsDeliveryOperation *operation,
    const QString &state,
    const RatingsReviewsDeliveryAdapter::Result &result,
    bool reconciled,
    QString *error)
{
    RatingsReviewsDeliveryReceipt receipt;
    receipt.operationId = operation->operationId;
    receipt.providerId = operation->providerId;
    receipt.operationType = operation->operationType;
    receipt.canonicalKey = operation->canonicalKey;
    receipt.canonicalPayloadDigest = operation->canonicalPayloadDigest;
    receipt.status = state;
    receipt.attemptNumber = operation->attemptCount;
    receipt.createdAtMs = operation->intentCreatedAtMs;
    receipt.updatedAtMs = now();
    receipt.completedAtMs = receipt.updatedAtMs;
    // Receipts deliberately store a compact safe status, never provider body
    // text.  A deterministic fallback keeps a terminal write durable even
    // when a fixture (or a future adapter) has no richer safe status.
    receipt.safeProviderStatus = result.safeProviderStatus.isEmpty()
        ? state : result.safeProviderStatus;
    receipt.safeErrorClass = result.safeErrorClass;
    receipt.safeRemoteVersion = result.safeRemoteVersion;
    receipt.safeRemoteTimestampMs = result.safeRemoteTimestampMs;
    receipt.reconciled = reconciled;
    if (!m_receipts->upsert(receipt, error))
        return false;
    operation->state = state;
    if (!m_outbox->replace(*operation, error))
        return false;
    replaceProjectionState();
    return true;
}

bool RatingsReviewsDelivery::operationIsCurrent(
    const RatingsReviewsDeliveryOperation &operation,
    QString *reason) const
{
    if (!active() || operation.profileId != m_activeProfileId) {
        if (reason)
            *reason = QStringLiteral("inactive_profile");
        return false;
    }
    const auto fixture = m_fixtureProviders.constFind(operation.providerId);
    const auto mapping = m_mappingStore->mapping(operation.providerId, operation.canonicalKey);
    if (fixture == m_fixtureProviders.cend() || !fixture->adapter
        || !fixture->capability.connected
        || fixture->capability.connectionGeneration != operation.connectionGeneration
        || !mapping || mapping->status != QLatin1String("matched")
        || mapping->connectionGeneration != operation.connectionGeneration
        || mapping->providerMediaId != operation.mappingProviderMediaId) {
        if (reason)
            *reason = QStringLiteral("provider_binding_changed");
        return false;
    }
    const auto record = m_canonicalStore->recordByKey(operation.canonicalKey);
    if (!record || ratingsReviewsCanonicalPayloadDigestV1(*record) != operation.canonicalPayloadDigest) {
        if (reason)
            *reason = QStringLiteral("canonical_payload_changed");
        return false;
    }
    if ((operation.operationType == QLatin1String("rating.set")
         && !fixture->capability.ratingCapable)
        || (operation.operationType == QLatin1String("review.set")
            && (!fixture->capability.reviewCapable
                || (record->spoiler && !fixture->capability.spoilerMetadata)))) {
        if (reason)
            *reason = QStringLiteral("provider_capability_changed");
        return false;
    }
    const auto currentMap = m_fixturePreferences
        ? m_fixturePreferences->ratingsReviewsConversionMap(operation.providerId)
        : std::nullopt;
    if (operation.operationType == QLatin1String("rating.set")
        && (m_fixturePreferences
            ? (!currentMap || currentMap->digest() != operation.conversionMapDigest)
            : fixture->capability.conversionMapDigest != operation.conversionMapDigest)) {
        if (reason)
            *reason = QStringLiteral("conversion_map_changed");
        return false;
    }
    if (hasUnknownBarrier(operation)) {
        if (reason)
            *reason = QStringLiteral("unknown_target_barrier");
        return false;
    }
    return true;
}

bool RatingsReviewsDelivery::hasUnknownBarrier(
    const RatingsReviewsDeliveryOperation &operation) const
{
    if (!m_outbox)
        return false;
    for (const auto &existing : m_outbox->operations()) {
        if (existing.profileId == operation.profileId
            && existing.providerId == operation.providerId
            && existing.canonicalKey == operation.canonicalKey
            && existing.operationType == operation.operationType
            && existing.state == QLatin1String("unknownOutcome")) {
            return true;
        }
    }
    return false;
}

std::optional<RatingsReviewsDeliveryOperation> RatingsReviewsDelivery::operation(
    const QString &operationId) const
{
    return m_outbox ? m_outbox->operation(operationId) : std::nullopt;
}

qint64 RatingsReviewsDelivery::now() const { return m_clock(); }
QString RatingsReviewsDelivery::newOperationId() const { return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower(); }
void RatingsReviewsDelivery::replaceProjectionState()
{
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    updateFixturePhase();
#endif
    emit stateChanged();
}
