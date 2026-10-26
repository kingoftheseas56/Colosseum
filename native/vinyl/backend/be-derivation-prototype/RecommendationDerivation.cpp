#include "RecommendationDerivation.h"

#include <QSet>
#include <QCryptographicHash>

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
    return kind == CanonicalEntityKind::Release
        || kind == CanonicalEntityKind::Recording
        || kind == CanonicalEntityKind::ReleaseGroup
        || kind == CanonicalEntityKind::Artist;
}

QString canonicalSortKey(const RecommendationEntityRef &entity)
{
    return entity.stableKey();
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
    const RecommendationEntityRef &entity) const
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

    auto aggregateFor = [&](const RecommendationEntityRef &entity)
        -> EntityHistoryAggregate & {
        for (auto &aggregate : aggregates) {
            if (aggregate.entity() == entity)
                return aggregate;
        }
        aggregates.push_back(EntityHistoryAggregate(entity));
        return aggregates.back();
    };

    auto emitSignal = [&](const ListenEvent &event,
                          const RecommendationEntityRef &entity,
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

        const auto &occurrence = event.context().occurrence();
        const auto &recording = occurrence.canonicalRecording();
        if (!recording.has_value())
            continue;

        const auto entity = RecommendationEntityRef(CanonicalEntityRef::from(*recording));
        auto &aggregate = aggregateFor(entity);

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

DerivationResult<RecommendationDerivationOutput> deriveQualifiedRecommendationInputs(
    const ProfileScopeId &profile, const QVector<QualifiedListeningSession> &input,
    const RecommendationDerivationConfig &config)
{
    if(config.version!=kRecommendationDerivationVersion || config.nowMs<=0
        || config.recentWindowMs<=0 || config.earlySkipPositionMs<0)
        return DerivationResult<RecommendationDerivationOutput>::failure(
            DerivationErrorCode::InvalidConfiguration,"config");
    QVector<EntityHistoryAggregate> aggregates;
    QVector<BehavioralSignalObservation> observations;
    auto aggregateFor=[&](const RecommendationEntityRef &ref)->EntityHistoryAggregate& {
        for(auto &aggregate:aggregates)if(aggregate.entity()==ref)return aggregate;
        aggregates.append(EntityHistoryAggregate(ref));return aggregates.back();
    };
    auto sessions=input;
    std::sort(sessions.begin(),sessions.end(),[](const auto &a,const auto &b){
        return a.atMs==b.atMs?a.sessionId<b.sessionId:a.atMs<b.atMs;
    });
    QSet<QString> seen,replayed;
    for(const auto &session:sessions) {
        if(session.sessionId.isEmpty() || seen.contains(session.sessionId) || session.atMs<=0
            || session.atMs>config.nowMs || session.qualifiedMs<0
            || session.recording.kind()!=CanonicalEntityKind::Recording)continue;
        seen.insert(session.sessionId);
        auto entities=session.contexts;entities.prepend(session.recording);
        QSet<QString> entityKeys;
        const bool qualified=session.qualifiedMs>=10000;
        // Skip is an explicit manual outcome even when playback began through
        // album continuation or autoplay. Start origin controls seed decay.
        const bool skip=(session.origin=="manual" || session.origin=="album-continuation" || session.origin=="autoplay") && session.outcome=="skip"
            && session.positionMs>=0 && session.positionMs<=config.earlySkipPositionMs
            && isRecent(session.outcomeAtMs,config);
        const bool completion=qualified && session.outcome=="natural-eof"
            && session.outcomeAtMs>0 && session.outcomeAtMs<=config.nowMs;
        if(!qualified && !skip)continue;
        for(const auto &entity:entities) {
            if(!isAllowedRecommendationKind(entity.kind()) || entityKeys.contains(entity.stableKey()))continue;
            entityKeys.insert(entity.stableKey());auto &aggregate=aggregateFor(entity);
            auto signal=[&](BehavioralSignalKind kind,qint64 at) {
                const auto text=session.sessionId+":"+entity.stableKey()+":"+behavioralSignalKindName(kind);
                const auto id=BehaviorObservationId::parse("activity-"+QString::fromLatin1(
                    QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256).toHex()));
                if(!id.ok())return;
                const auto observation=BehavioralSignalObservation::create(*id.value,entity,kind,at);
                if(observation.ok())observations.append(*observation.value);
            };
            if(qualified) {
                ++aggregate.m_playCount;
                aggregate.m_lastPlayedAtMs=std::max(aggregate.m_lastPlayedAtMs.value_or(0),session.atMs);
                if(session.origin=="manual") {
                    if(replayed.contains(entity.stableKey()))signal(BehavioralSignalKind::Replay,session.atMs);
                    replayed.insert(entity.stableKey());
                }
            }
            if(completion) {
                if(isRecent(session.outcomeAtMs,config))++aggregate.m_recentCompletionCount;
                if(session.origin=="autoplay") {
                    ++aggregate.m_passiveAutoplayCompletionCount;
                    signal(BehavioralSignalKind::PassiveAutoplayCompletion,session.outcomeAtMs);
                } else if(session.origin=="manual" || session.origin=="album-continuation") {
                    ++aggregate.m_naturalCompletionCount;
                    signal(BehavioralSignalKind::NaturalCompletion,session.outcomeAtMs);
                }
            }
            if(skip){++aggregate.m_recentSkipCount;++aggregate.m_earlySkipCount;}
        }
    }
    // One early exit is weak evidence. Only repeated retained manual skips
    // affect ranking; none is written as explicit Negative feedback.
    for(auto &aggregate:aggregates)if(aggregate.m_earlySkipCount<2)aggregate.m_earlySkipCount=0;
    std::sort(aggregates.begin(),aggregates.end(),[](const auto &a,const auto &b){
        return a.entity().stableKey()<b.entity().stableKey();
    });
    return DerivationResult<RecommendationDerivationOutput>::success(
        RecommendationDerivationOutput(profile,config.version,std::move(observations),std::move(aggregates),false));
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
    std::optional<RecommendationEntityRef> relatedEntity)
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
    RecommendationEntityRef entity,
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
                "exposure entity must be Recording, Release, ReleaseGroup, or Artist"));
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
