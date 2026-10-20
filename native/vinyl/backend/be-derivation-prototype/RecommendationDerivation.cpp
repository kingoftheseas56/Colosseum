#include "RecommendationDerivation.h"

#include <QSet>

#include <algorithm>

namespace vinyl::recommendation {
namespace {

bool hasControlCharacters(const QString &text)
{
    for (const QChar ch : text) {
        const ushort value = ch.unicode();
        if (value < 0x20 || value == 0x7f)
            return true;
    }
    return false;
}

bool isAllowedRecommendationKind(CanonicalEntityKind kind)
{
    return kind == CanonicalEntityKind::Recording
        || kind == CanonicalEntityKind::ReleaseGroup
        || kind == CanonicalEntityKind::Artist;
}

QString canonicalSortKey(const CanonicalEntityRef &entity)
{
    return QStringLiteral("%1:%2")
        .arg(static_cast<int>(entity.kind()))
        .arg(entity.mbidString());
}

bool isRecent(qint64 occurredAtMs,
              const RecommendationDerivationConfig &config)
{
    if (occurredAtMs <= 0 || occurredAtMs > config.nowMs)
        return false;
    return occurredAtMs >= config.nowMs - config.recentWindowMs;
}

bool explicitNaturalCompletionOrigin(const QueueOccurrence &occurrence)
{
    if (!occurrence.enqueueOrigin().has_value())
        return false;

    switch (occurrence.enqueueOrigin()->kind()) {
    case EnqueueOriginKind::User:
    case EnqueueOriginKind::Album:
    case EnqueueOriginKind::Playlist:
        return true;
    case EnqueueOriginKind::Radio:
    case EnqueueOriginKind::Restore:
        return false;
    }
    return false;
}

bool explicitPassiveOrigin(const QueueOccurrence &occurrence)
{
    return occurrence.enqueueOrigin().has_value()
        && occurrence.enqueueOrigin()->kind() == EnqueueOriginKind::Radio;
}

bool explicitDeliberateQueueOrigin(const QueueOccurrence &occurrence)
{
    return occurrence.enqueueOrigin().has_value()
        && occurrence.enqueueOrigin()->kind() == EnqueueOriginKind::User;
}

} // namespace

QString derivationErrorCodeName(DerivationErrorCode code)
{
    switch (code) {
    case DerivationErrorCode::None:
        return QStringLiteral("none");
    case DerivationErrorCode::InvalidConfiguration:
        return QStringLiteral("invalid_configuration");
    case DerivationErrorCode::ProfileMismatch:
        return QStringLiteral("profile_mismatch");
    case DerivationErrorCode::InvalidObservation:
        return QStringLiteral("invalid_observation");
    case DerivationErrorCode::InvalidExposureId:
        return QStringLiteral("invalid_exposure_id");
    case DerivationErrorCode::InvalidExposure:
        return QStringLiteral("invalid_exposure");
    case DerivationErrorCode::ConflictingExposure:
        return QStringLiteral("conflicting_exposure");
    }
    return QStringLiteral("unknown");
}

const EntityHistoryAggregate *RecommendationDerivationOutput::findAggregate(
    const CanonicalEntityRef &entity) const
{
    for (const auto &aggregate : m_aggregates) {
        if (aggregate.entity() == entity)
            return &aggregate;
    }
    return nullptr;
}

DerivationResult<RecommendationDerivationOutput> deriveRecommendationInputs(
    const ListenEventLedger &ledger,
    const RecommendationDerivationConfig &config)
{
    if (config.version != kRecommendationDerivationVersion) {
        return DerivationResult<RecommendationDerivationOutput>::failure(
            DerivationErrorCode::InvalidConfiguration,
            QStringLiteral("config.version"),
            QStringLiteral("unsupported derivation version"));
    }
    if (config.nowMs <= 0 || config.recentWindowMs <= 0
        || config.earlySkipPositionMs < 0) {
        return DerivationResult<RecommendationDerivationOutput>::failure(
            DerivationErrorCode::InvalidConfiguration,
            QStringLiteral("config"),
            QStringLiteral(
                "nowMs/recentWindowMs must be positive and earlySkipPositionMs non-negative"));
    }

    QVector<EntityHistoryAggregate> aggregates;
    QVector<BehavioralSignalObservation> observations;
    QSet<QString> deliberatelyReplayedRecordings;

    auto aggregateFor = [&](const CanonicalEntityRef &entity)
        -> EntityHistoryAggregate & {
        for (auto &aggregate : aggregates) {
            if (aggregate.entity() == entity)
                return aggregate;
        }
        aggregates.push_back(EntityHistoryAggregate(entity));
        return aggregates.back();
    };

    auto emitSignal = [&](const ListenEvent &event,
                          const CanonicalEntityRef &entity,
                          BehavioralSignalKind kind)
        -> std::optional<DerivationError> {
        const QString idText =
            QStringLiteral("w4g-v1-%1-%2")
                .arg(event.sequence())
                .arg(behavioralSignalKindName(kind));
        const auto parsedId = BehaviorObservationId::parse(idText);
        if (!parsedId.ok()) {
            return DerivationError{
                DerivationErrorCode::InvalidObservation,
                QStringLiteral("observation_id"),
                parsedId.error.detail,
            };
        }

        const auto observation = BehavioralSignalObservation::create(
            *parsedId.value,
            entity,
            kind,
            event.observedAtMs());
        if (!observation.ok()) {
            return DerivationError{
                DerivationErrorCode::InvalidObservation,
                QStringLiteral("observation"),
                observation.error.detail,
            };
        }

        observations.push_back(*observation.value);
        return std::nullopt;
    };

    for (const auto &event : ledger.events()) {
        if (event.context().profileId() != ledger.profileId()) {
            return DerivationResult<RecommendationDerivationOutput>::failure(
                DerivationErrorCode::ProfileMismatch,
                QStringLiteral("event.profile_id"),
                QStringLiteral("BA4 event escaped its profile ledger"));
        }

        const auto entity =
            CanonicalEntityRef::from(event.context().recordingMbid());
        auto &aggregate = aggregateFor(entity);
        const auto &occurrence = event.context().occurrence();

        if (event.kind() == ListenEventKind::Start) {
            if (!event.context().continuationOf().has_value()) {
                ++aggregate.m_playCount;
                aggregate.m_lastPlayedAtMs = event.observedAtMs();

                if (explicitDeliberateQueueOrigin(occurrence)) {
                    const QString key = entity.mbidString();
                    if (deliberatelyReplayedRecordings.contains(key)) {
                        if (const auto error = emitSignal(
                                event,
                                entity,
                                BehavioralSignalKind::Replay)) {
                            return DerivationResult<
                                RecommendationDerivationOutput>::failure(
                                    error->code,
                                    error->field,
                                    error->detail);
                        }
                    }
                    deliberatelyReplayedRecordings.insert(key);

                    if (const auto error = emitSignal(
                            event,
                            entity,
                            BehavioralSignalKind::DeliberateQueue)) {
                        return DerivationResult<
                            RecommendationDerivationOutput>::failure(
                                error->code,
                                error->field,
                                error->detail);
                    }
                }
            }
            continue;
        }

        if (event.kind() == ListenEventKind::Completion) {
            if (isRecent(event.observedAtMs(), config))
                ++aggregate.m_recentCompletionCount;

            if (explicitPassiveOrigin(occurrence)) {
                ++aggregate.m_passiveAutoplayCompletionCount;
                if (const auto error = emitSignal(
                        event,
                        entity,
                        BehavioralSignalKind::PassiveAutoplayCompletion)) {
                    return DerivationResult<
                        RecommendationDerivationOutput>::failure(
                            error->code,
                            error->field,
                            error->detail);
                }
            } else if (explicitNaturalCompletionOrigin(occurrence)) {
                ++aggregate.m_naturalCompletionCount;
                if (const auto error = emitSignal(
                        event,
                        entity,
                        BehavioralSignalKind::NaturalCompletion)) {
                    return DerivationResult<
                        RecommendationDerivationOutput>::failure(
                            error->code,
                            error->field,
                            error->detail);
                }
            }
            continue;
        }

        if (event.kind() == ListenEventKind::Skip) {
            if (isRecent(event.observedAtMs(), config))
                ++aggregate.m_recentSkipCount;

            if (event.positionMs() <= config.earlySkipPositionMs) {
                ++aggregate.m_earlySkipCount;
                if (const auto error = emitSignal(
                        event,
                        entity,
                        BehavioralSignalKind::EarlySkip)) {
                    return DerivationResult<
                        RecommendationDerivationOutput>::failure(
                            error->code,
                            error->field,
                            error->detail);
                }
            }
        }
    }

    std::sort(
        aggregates.begin(),
        aggregates.end(),
        [](const EntityHistoryAggregate &left,
           const EntityHistoryAggregate &right) {
            return canonicalSortKey(left.entity())
                < canonicalSortKey(right.entity());
        });

    return DerivationResult<RecommendationDerivationOutput>::success(
        RecommendationDerivationOutput(
            ledger.profileId(),
            config.version,
            std::move(observations),
            std::move(aggregates),
            config.historyComplete));
}

QString exposureEvidenceKindName(ExposureEvidenceKind kind)
{
    switch (kind) {
    case ExposureEvidenceKind::CandidateGenerator:
        return QStringLiteral("candidate_generator");
    case ExposureEvidenceKind::ExplicitTaste:
        return QStringLiteral("explicit_taste");
    case ExposureEvidenceKind::BehavioralHistory:
        return QStringLiteral("behavioral_history");
    case ExposureEvidenceKind::HistoryAggregate:
        return QStringLiteral("history_aggregate");
    case ExposureEvidenceKind::CanonicalRelationship:
        return QStringLiteral("canonical_relationship");
    case ExposureEvidenceKind::CatalogueContext:
        return QStringLiteral("catalogue_context");
    case ExposureEvidenceKind::DiscoveryEvidence:
        return QStringLiteral("discovery_evidence");
    case ExposureEvidenceKind::DiversityPolicy:
        return QStringLiteral("diversity_policy");
    case ExposureEvidenceKind::ExplorationPolicy:
        return QStringLiteral("exploration_policy");
    case ExposureEvidenceKind::FatiguePolicy:
        return QStringLiteral("fatigue_policy");
    }
    return QStringLiteral("unknown");
}

DerivationResult<RecommendationExposureId> RecommendationExposureId::parse(
    const QString &text)
{
    const QString normalized = text.trimmed();
    if (normalized.isEmpty() || normalized.size() > 128
        || hasControlCharacters(normalized)) {
        return DerivationResult<RecommendationExposureId>::failure(
            DerivationErrorCode::InvalidExposureId,
            QStringLiteral("exposure_id"),
            QStringLiteral(
                "exposure id must be 1..128 non-control characters"));
    }

    return DerivationResult<RecommendationExposureId>::success(
        RecommendationExposureId(normalized));
}

DerivationResult<ExposureEvidenceRef> ExposureEvidenceRef::create(
    ExposureEvidenceKind kind,
    QString sourceId,
    std::optional<CanonicalEntityRef> relatedEntity)
{
    sourceId = sourceId.trimmed();
    if (sourceId.isEmpty() || sourceId.size() > 512
        || hasControlCharacters(sourceId)) {
        return DerivationResult<ExposureEvidenceRef>::failure(
            DerivationErrorCode::InvalidExposure,
            QStringLiteral("evidence.source_id"),
            QStringLiteral(
                "exposure evidence source id must be 1..512 non-control characters"));
    }

    return DerivationResult<ExposureEvidenceRef>::success(
        ExposureEvidenceRef(
            kind,
            std::move(sourceId),
            std::move(relatedEntity)));
}

DerivationResult<RecommendationExposureRecord>
RecommendationExposureRecord::create(
    ProfileScopeId profileId,
    RecommendationExposureId exposureId,
    CanonicalEntityRef entity,
    qint64 recommendedAtMs,
    int rankPosition,
    QString rankingConfigVersion,
    QVector<ExposureEvidenceRef> evidence)
{
    if (!isAllowedRecommendationKind(entity.kind())) {
        return DerivationResult<RecommendationExposureRecord>::failure(
            DerivationErrorCode::InvalidExposure,
            QStringLiteral("entity"),
            QStringLiteral(
                "exposure entity must be Recording, ReleaseGroup, or Artist"));
    }
    rankingConfigVersion = rankingConfigVersion.trimmed();
    if (recommendedAtMs <= 0 || rankPosition <= 0
        || rankingConfigVersion.isEmpty()
        || rankingConfigVersion.size() > 128
        || hasControlCharacters(rankingConfigVersion)
        || evidence.isEmpty()) {
        return DerivationResult<RecommendationExposureRecord>::failure(
            DerivationErrorCode::InvalidExposure,
            QStringLiteral("exposure"),
            QStringLiteral(
                "exposure requires positive time/rank, config version, and evidence"));
    }

    return DerivationResult<RecommendationExposureRecord>::success(
        RecommendationExposureRecord(
            std::move(profileId),
            std::move(exposureId),
            std::move(entity),
            recommendedAtMs,
            rankPosition,
            std::move(rankingConfigVersion),
            std::move(evidence)));
}

bool RecommendationExposureRecord::samePayload(
    const RecommendationExposureRecord &other) const noexcept
{
    return m_profileId == other.m_profileId
        && m_entity == other.m_entity
        && m_recommendedAtMs == other.m_recommendedAtMs
        && m_rankPosition == other.m_rankPosition
        && m_rankingConfigVersion == other.m_rankingConfigVersion
        && m_evidence == other.m_evidence;
}

DerivationMutationStatus RecommendationExposureLog::append(
    RecommendationExposureRecord record)
{
    if (record.profileId() != m_profileId) {
        return DerivationMutationStatus::failure(
            DerivationErrorCode::ProfileMismatch,
            QStringLiteral("profile_id"),
            QStringLiteral(
                "exposure record belongs to a different profile"));
    }

    for (const auto &existing : m_records) {
        if (existing.exposureId() != record.exposureId())
            continue;

        if (existing.samePayload(record))
            return DerivationMutationStatus::success(false);

        return DerivationMutationStatus::failure(
            DerivationErrorCode::ConflictingExposure,
            QStringLiteral("exposure_id"),
            QStringLiteral(
                "same exposure id cannot carry a different payload"));
    }

    m_records.push_back(std::move(record));
    return DerivationMutationStatus::success(true);
}

} // namespace vinyl::recommendation
