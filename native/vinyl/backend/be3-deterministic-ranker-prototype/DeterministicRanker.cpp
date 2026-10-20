#include "DeterministicRanker.h"

#include <QHash>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vinyl::recommendation {
namespace {

qint64 saturatedAdd(qint64 lhs, qint64 rhs)
{
    const auto maximum = std::numeric_limits<qint64>::max();
    const auto minimum = std::numeric_limits<qint64>::min();
    if (rhs > 0 && lhs > maximum - rhs) return maximum;
    if (rhs < 0 && lhs < minimum - rhs) return minimum;
    return lhs + rhs;
}

qint64 saturatedSubtract(qint64 lhs, qint64 rhs)
{
    const auto maximum = std::numeric_limits<qint64>::max();
    const auto minimum = std::numeric_limits<qint64>::min();
    if (rhs > 0 && lhs < minimum + rhs) return minimum;
    if (rhs < 0 && lhs > maximum + rhs) return maximum;
    return lhs - rhs;
}

qint64 weightedCount(qint64 weight, int count)
{
    if (count <= 0) return 0;
    const auto maximum = std::numeric_limits<qint64>::max();
    const auto minimum = std::numeric_limits<qint64>::min();
    if (weight > 0 && weight > maximum / count) return maximum;
    if (weight < 0 && weight < minimum / count) return minimum;
    return weight * count;
}

qint64 calibratedScore(double raw, const RawScoreCalibration &calibration)
{
    const double scaled = raw * calibration.multiplier;
    const double limit = static_cast<double>(calibration.maxAbsContribution);
    // Finite inputs may multiply to infinity. Bound the floating value before
    // any integral conversion, including when the configured cap is qint64::max.
    if (scaled >= limit) return calibration.maxAbsContribution;
    if (scaled <= -limit) return -calibration.maxAbsContribution;
    return qRound64(scaled);
}

QString canonicalKey(const CanonicalEntityRef &entity)
{
    return QStringLiteral("%1:%2")
        .arg(static_cast<int>(entity.kind()))
        .arg(entity.mbidString());
}

QString relatedKey(const std::optional<CanonicalEntityRef> &entity)
{
    return entity.has_value() ? canonicalKey(*entity) : QString();
}

void sortExplanation(QVector<RankExplanationEvidence> &evidence)
{
    std::sort(
        evidence.begin(),
        evidence.end(),
        [](const RankExplanationEvidence &left,
           const RankExplanationEvidence &right) {
            if (left.kind != right.kind)
                return static_cast<int>(left.kind)
                    < static_cast<int>(right.kind);
            if (left.sourceId != right.sourceId)
                return left.sourceId < right.sourceId;
            if (relatedKey(left.relatedEntity) != relatedKey(right.relatedEntity))
                return relatedKey(left.relatedEntity)
                    < relatedKey(right.relatedEntity);
            return left.scoreDelta < right.scoreDelta;
        });
}

const RawScoreCalibration *calibrationFor(
    const QVector<RawScoreCalibration> &calibrations,
    const RecommendationGeneratorId &generatorId)
{
    for (const auto &calibration : calibrations) {
        if (calibration.generatorId == generatorId)
            return &calibration;
    }
    return nullptr;
}

bool suppressedAt(const EntityTasteState *state, qint64 nowMs)
{
    if (state == nullptr)
        return false;
    const auto &suppression = state->suppression();
    return suppression.hidden()
        || suppression.doNotRecommend()
        || suppression.isSnoozedAt(nowMs);
}

bool explicitlyPositive(const TasteProfileState &taste,
                        const CanonicalEntityRef &entity)
{
    const auto *state = taste.find(entity);
    return state != nullptr
        && state->explicitFeedback().hasExplicitMutation()
        && state->explicitFeedback().feedback()
            == ExplicitTasteFeedback::Positive;
}

std::optional<QString> artistKeyFor(
    const RecommendationCandidate &candidate,
    const RankerFeatureContext &features)
{
    QStringList keys;
    const auto &entity = candidate.canonicalEntity();

    if (entity.kind() == CanonicalEntityKind::Artist)
        keys.push_back(entity.mbidString());

    for (const auto &contribution : candidate.contributions()) {
        if (contribution.canonicalContext().artistMbid.has_value())
            keys.push_back(
                contribution.canonicalContext().artistMbid->toString());
    }

    if (entity.kind() == CanonicalEntityKind::ReleaseGroup) {
        for (const auto &releaseGroup : features.releaseGroups) {
            if (releaseGroup.mbid.toString() != entity.mbidString())
                continue;
            for (const auto &credit : releaseGroup.artistCredit)
                keys.push_back(credit.artistMbid.toString());
        }
    } else if (entity.kind() == CanonicalEntityKind::Recording) {
        for (const auto &recording : features.recordings) {
            if (recording.mbid.toString() != entity.mbidString())
                continue;
            for (const auto &credit : recording.artistCredit)
                keys.push_back(credit.artistMbid.toString());
        }
    }

    keys.removeDuplicates();
    std::sort(keys.begin(), keys.end());
    if (keys.isEmpty())
        return std::nullopt;
    return keys.front();
}

std::optional<QString> releaseGroupKeyFor(
    const RecommendationCandidate &candidate)
{
    QStringList keys;
    const auto &entity = candidate.canonicalEntity();

    if (entity.kind() == CanonicalEntityKind::ReleaseGroup)
        keys.push_back(entity.mbidString());

    for (const auto &contribution : candidate.contributions()) {
        if (contribution.canonicalContext().releaseGroupMbid.has_value()) {
            keys.push_back(
                contribution.canonicalContext().releaseGroupMbid->toString());
        }
    }

    keys.removeDuplicates();
    std::sort(keys.begin(), keys.end());
    if (keys.isEmpty())
        return std::nullopt;
    return keys.front();
}

struct BaseCandidate final {
    CanonicalEntityRef entity;
    bool directlyPlayable = false;
    qint64 baseScore = 0;
    QVector<RankExplanationEvidence> explanation;
    std::optional<QString> artistKey;
    std::optional<QString> releaseGroupKey;
};

void addEvidence(BaseCandidate &candidate,
                 RankEvidenceKind kind,
                 QString sourceId,
                 qint64 delta,
                 std::optional<CanonicalEntityRef> related = std::nullopt)
{
    const qint64 next = saturatedAdd(candidate.baseScore, delta);
    const qint64 appliedDelta = saturatedSubtract(next, candidate.baseScore);
    candidate.baseScore = next;
    candidate.explanation.push_back(
        RankExplanationEvidence{
            kind,
            std::move(sourceId),
            appliedDelta,
            std::move(related),
        });
}

QString relationshipSourceId(const context::ContextRelationship &relationship)
{
    const auto jsonKey = [](const QJsonArray &fields) {
        return QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact));
    };
    QStringList evidenceKeys;
    for (const auto &evidence : relationship.evidence) {
        evidenceKeys.push_back(jsonKey(QJsonArray{
            evidence.source, evidence.sourceItem,
            evidence.relationshipType, evidence.assertionKey}));
    }
    std::sort(evidenceKeys.begin(), evidenceKeys.end());
    evidenceKeys.removeDuplicates();
    QStringList creditKeys;
    for (const auto &credit : relationship.artistCredits) {
        QStringList roleKeys;
        for (const auto &role : credit.roles) {
            roleKeys.push_back(jsonKey(QJsonArray{
                context::creditRoleKindName(role.kind), role.sourceRole}));
        }
        std::sort(roleKeys.begin(), roleKeys.end());
        creditKeys.push_back(jsonKey(QJsonArray{
            credit.artistMbid.toString(), credit.creditedName,
            QJsonArray::fromStringList(roleKeys)}));
    }
    std::sort(creditKeys.begin(), creditKeys.end());
    // Preserve the complete relationship and provenance tuple. Separators in
    // provider-owned strings cannot alias another assertion or another edge.
    const QByteArray identity = QJsonDocument(QJsonArray{
        QStringLiteral("vinyl-bf1-affinity-v2"),
        canonicalKey(relationship.source), canonicalKey(relationship.target),
        context::relationshipKindName(relationship.kind),
        context::relationshipDirectionalityName(relationship.directionality),
        QJsonArray::fromStringList(creditKeys),
        QJsonArray::fromStringList(evidenceKeys)}).toJson(QJsonDocument::Compact);
    return QStringLiteral("bf1:v2:%1:sha256:%2")
        .arg(context::relationshipKindName(relationship.kind),
             QString::fromLatin1(QCryptographicHash::hash(
                 identity, QCryptographicHash::Sha256).toHex()));
}

QStringList relationshipEvidenceFor(
    const CanonicalEntityRef &candidate,
    const TasteProfileState &taste,
    const RankerFeatureContext &features)
{
    QStringList sources;
    if (!features.contextGraph.has_value())
        return sources;

    for (const auto &relationship :
         features.contextGraph->relationships()) {
        if (relationship.kind == context::RelationshipKind::Unsupported)
            continue;
        const bool evidenced = std::any_of(relationship.evidence.cbegin(), relationship.evidence.cend(),
            [](const context::RelationshipEvidence &evidence) {
                return !evidence.source.trimmed().isEmpty()
                    && (!evidence.sourceItem.trimmed().isEmpty() || !evidence.assertionKey.trimmed().isEmpty());
            });
        if (!evidenced) continue;
        std::optional<CanonicalEntityRef> other;
        if (relationship.source == candidate)
            other = relationship.target;
        else if (relationship.target == candidate)
            other = relationship.source;
        else
            continue;

        if (!explicitlyPositive(taste, *other))
            continue;

        sources.push_back(relationshipSourceId(relationship));
    }

    sources.removeDuplicates();
    std::sort(sources.begin(), sources.end());
    return sources;
}

QStringList discoveryEvidenceFor(
    const CanonicalEntityRef &candidate,
    const RankerFeatureContext &features)
{
    QStringList sources;

    for (const auto &collection : features.discoveryCollections) {
        for (const auto &item : collection.items()) {
            if (item.entity != candidate)
                continue;

            sources.push_back(
                QStringLiteral("bg2:%1:%2:%3")
                    .arg(
                        collection.sourceProvider().value(),
                        discovery::discoveryKindName(collection.kind()),
                        item.provenance.evidenceId));
        }
    }

    sources.removeDuplicates();
    std::sort(sources.begin(), sources.end());
    return sources;
}

QStringList recentExposureIds(
    const CanonicalEntityRef &candidate,
    const RecommendationExposureLog &exposureLog,
    const DeterministicRankerConfig &config)
{
    QStringList ids;
    const qint64 lowerBound = config.nowMs - config.exposureWindowMs;

    for (const auto &record : exposureLog.records()) {
        if (record.entity() != candidate)
            continue;
        if (record.recommendedAtMs() < lowerBound
            || record.recommendedAtMs() > config.nowMs) {
            continue;
        }
        ids.push_back(record.exposureId().value());
    }

    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool validConfig(const DeterministicRankerConfig &config)
{
    if (config.version.trimmed().isEmpty()
        || config.nowMs <= 0
        || config.maxResults <= 0
        || config.exposureWindowMs <= 0
        || config.maxPlayCount < 0
        || config.maxRecentCompletionCount < 0
        || config.maxEarlySkipCount < 0
        || config.maxRelationshipCount < 0
        || config.maxDiscoveryCount < 0
        || config.maxRecentExposureCount < 0
        || config.maxDiversityRepeatCount < 0) {
        return false;
    }

    QSet<QString> generators;
    for (const auto &calibration : config.rawScoreCalibrations) {
        if (!std::isfinite(calibration.multiplier)
            || calibration.maxAbsContribution <= 0
            || generators.contains(calibration.generatorId.value())) {
            return false;
        }
        generators.insert(calibration.generatorId.value());
    }
    return true;
}

} // namespace

QString rankerErrorCodeName(RankerErrorCode code)
{
    switch (code) {
    case RankerErrorCode::None:
        return QStringLiteral("none");
    case RankerErrorCode::InvalidConfiguration:
        return QStringLiteral("invalid_configuration");
    case RankerErrorCode::ProfileMismatch:
        return QStringLiteral("profile_mismatch");
    }
    return QStringLiteral("unknown");
}

QString rankEvidenceKindName(RankEvidenceKind kind)
{
    switch (kind) {
    case RankEvidenceKind::CandidateGeneratorSupport:
        return QStringLiteral("candidate_generator_support");
    case RankEvidenceKind::CalibratedGeneratorScore:
        return QStringLiteral("calibrated_generator_score");
    case RankEvidenceKind::ExplicitPositive:
        return QStringLiteral("explicit_positive");
    case RankEvidenceKind::ExplicitNegative:
        return QStringLiteral("explicit_negative");
    case RankEvidenceKind::PlayCount:
        return QStringLiteral("play_count");
    case RankEvidenceKind::RecentCompletion:
        return QStringLiteral("recent_completion");
    case RankEvidenceKind::EarlySkip:
        return QStringLiteral("early_skip");
    case RankEvidenceKind::CanonicalRelationship:
        return QStringLiteral("canonical_relationship");
    case RankEvidenceKind::DiscoveryPresence:
        return QStringLiteral("discovery_presence");
    case RankEvidenceKind::ExplorationBonus:
        return QStringLiteral("exploration_bonus");
    case RankEvidenceKind::ExposureFatigue:
        return QStringLiteral("exposure_fatigue");
    case RankEvidenceKind::DiversityArtistAdjustment:
        return QStringLiteral("diversity_artist_adjustment");
    case RankEvidenceKind::DiversityReleaseGroupAdjustment:
        return QStringLiteral("diversity_release_group_adjustment");
    }
    return QStringLiteral("unknown");
}

RankerResult<RankerOutput> rankDeterministically(
    const RecommendationCandidateSet &candidates,
    const TasteProfileState &taste,
    const RecommendationDerivationOutput &history,
    const RecommendationExposureLog &exposureLog,
    const RankerFeatureContext &features,
    const DeterministicRankerConfig &config)
{
    if (!validConfig(config)) {
        return RankerResult<RankerOutput>::failure(
            RankerErrorCode::InvalidConfiguration,
            QStringLiteral("config"),
            QStringLiteral("ranker configuration is invalid"));
    }

    if (taste.profileId() != history.profileId()
        || taste.profileId() != exposureLog.profileId()) {
        return RankerResult<RankerOutput>::failure(
            RankerErrorCode::ProfileMismatch,
            QStringLiteral("profile_id"),
            QStringLiteral(
                "taste, derived history, and exposure log must share one profile"));
    }

    QVector<BaseCandidate> remaining;
    QVector<CanonicalEntityRef> suppressed;

    for (const auto &candidate : candidates.candidates()) {
        const auto &entity = candidate.canonicalEntity();
        const auto *tasteState = taste.find(entity);

        if (suppressedAt(tasteState, config.nowMs)) {
            suppressed.push_back(entity);
            continue;
        }

        BaseCandidate base{
            entity,
            candidate.isDirectlyPlayable(),
            0,
            {},
            artistKeyFor(candidate, features),
            releaseGroupKeyFor(candidate),
        };

        // Ingestion policy: one support award and at most one calibrated award
        // per generator. Its strongest calibrated contribution wins. BE1 retains
        // all provenance; retry/page duplicates cannot multiply rank or reasons.
        QMap<QString, std::optional<qint64>> generatorScores;
        for (const auto &contribution : candidate.contributions()) {
            const auto &generator = contribution.provenance().generatorId();
            auto &best = generatorScores[generator.value()];
            if (contribution.rawProviderScore()) {
                if (const auto *calibration = calibrationFor(config.rawScoreCalibrations, generator)) {
                    const qint64 score = calibratedScore(*contribution.rawProviderScore(), *calibration);
                    if (!best || score > *best) best = score;
                }
            }
        }
        for (auto it = generatorScores.cbegin(); it != generatorScores.cend(); ++it) {
            addEvidence(base, RankEvidenceKind::CandidateGeneratorSupport,
                QStringLiteral("be1:generator:") + it.key(), config.generatorSupportWeight);
            if (it.value()) addEvidence(base, RankEvidenceKind::CalibratedGeneratorScore,
                QStringLiteral("be3:calibration:") + it.key(), *it.value());
        }

        if (tasteState != nullptr
            && tasteState->explicitFeedback().hasExplicitMutation()) {
            const auto feedback = tasteState->explicitFeedback().feedback();
            if (feedback == ExplicitTasteFeedback::Positive) {
                addEvidence(
                    base,
                    RankEvidenceKind::ExplicitPositive,
                    QStringLiteral("be2:explicit:revision:%1")
                        .arg(tasteState->explicitFeedback().revision()),
                    config.explicitPositiveWeight);
            } else if (feedback == ExplicitTasteFeedback::Negative) {
                addEvidence(
                    base,
                    RankEvidenceKind::ExplicitNegative,
                    QStringLiteral("be2:explicit:revision:%1")
                        .arg(tasteState->explicitFeedback().revision()),
                    config.explicitNegativeWeight);
            }
        }

        const auto *aggregate = history.findAggregate(entity);
        if (aggregate != nullptr) {
            const int playCount =
                std::min(aggregate->playCount(), config.maxPlayCount);
            if (playCount > 0) {
                addEvidence(
                    base,
                    RankEvidenceKind::PlayCount,
                    QStringLiteral("be-derivation:v1:play_count:%1")
                        .arg(aggregate->playCount()),
                    weightedCount(config.playCountWeight, playCount));
            }

            const int completions = std::min(
                aggregate->recentCompletionCount(),
                config.maxRecentCompletionCount);
            if (completions > 0) {
                addEvidence(
                    base,
                    RankEvidenceKind::RecentCompletion,
                    QStringLiteral("be-derivation:v1:recent_completion:%1")
                        .arg(aggregate->recentCompletionCount()),
                    weightedCount(config.recentCompletionWeight, completions));
            }

            const int earlySkips = std::min(
                aggregate->earlySkipCount(),
                config.maxEarlySkipCount);
            if (earlySkips > 0) {
                addEvidence(
                    base,
                    RankEvidenceKind::EarlySkip,
                    QStringLiteral("be-derivation:v1:early_skip:%1")
                        .arg(aggregate->earlySkipCount()),
                    weightedCount(config.earlySkipWeight, earlySkips));
            }
        }

        const QStringList relationships =
            relationshipEvidenceFor(entity, taste, features);
        for (int i = 0;
             i < std::min(static_cast<int>(relationships.size()), config.maxRelationshipCount);
             ++i) {
            addEvidence(
                base,
                RankEvidenceKind::CanonicalRelationship,
                relationships[i],
                config.relationshipWeight);
        }

        const QStringList discovery =
            discoveryEvidenceFor(entity, features);
        for (int i = 0;
             i < std::min(static_cast<int>(discovery.size()), config.maxDiscoveryCount);
             ++i) {
            addEvidence(
                base,
                RankEvidenceKind::DiscoveryPresence,
                discovery[i],
                config.discoveryWeight);
        }

        if (history.historyComplete() && (aggregate == nullptr || aggregate->playCount() == 0)) {
            addEvidence(
                base,
                RankEvidenceKind::ExplorationBonus,
                QStringLiteral("be3:policy:exploration:complete-history-no-play"),
                config.explorationBonus);
        }

        const QStringList exposureIds =
            recentExposureIds(entity, exposureLog, config);
        for (int i = 0;
             i < std::min(
                 static_cast<int>(exposureIds.size()),
                 config.maxRecentExposureCount);
             ++i) {
            addEvidence(
                base,
                RankEvidenceKind::ExposureFatigue,
                QStringLiteral("exposure:") + exposureIds[i],
                config.exposurePenalty);
        }

        sortExplanation(base.explanation);
        remaining.push_back(std::move(base));
    }

    std::sort(
        suppressed.begin(),
        suppressed.end(),
        [](const CanonicalEntityRef &left,
           const CanonicalEntityRef &right) {
            return canonicalKey(left) < canonicalKey(right);
        });

    QVector<RankedRecommendation> ranked;
    QHash<QString, int> artistCounts;
    QHash<QString, int> releaseGroupCounts;

    while (!remaining.isEmpty()
           && ranked.size() < config.maxResults) {
        int bestIndex = -1;
        qint64 bestScore = 0;
        QString bestCanonicalKey;
        qint64 bestArtistAdjustment = 0;
        qint64 bestReleaseAdjustment = 0;

        for (int i = 0; i < remaining.size(); ++i) {
            const auto &candidate = remaining[i];

            qint64 artistAdjustment = 0;
            if (candidate.artistKey.has_value()) {
                const int repeats = std::min(
                    artistCounts.value(*candidate.artistKey, 0),
                    config.maxDiversityRepeatCount);
                artistAdjustment =
                    weightedCount(config.repeatedArtistPenalty, repeats);
            }

            qint64 releaseAdjustment = 0;
            if (candidate.releaseGroupKey.has_value()) {
                const int repeats = std::min(
                    releaseGroupCounts.value(
                        *candidate.releaseGroupKey,
                        0),
                    config.maxDiversityRepeatCount);
                releaseAdjustment =
                    weightedCount(config.repeatedReleaseGroupPenalty, repeats);
            }

            const qint64 afterArtist = saturatedAdd(candidate.baseScore, artistAdjustment);
            const qint64 score = saturatedAdd(afterArtist, releaseAdjustment);
            const QString key = canonicalKey(candidate.entity);

            if (bestIndex < 0
                || score > bestScore
                || (score == bestScore && key < bestCanonicalKey)) {
                bestIndex = i;
                bestScore = score;
                bestCanonicalKey = key;
                bestArtistAdjustment = saturatedSubtract(afterArtist, candidate.baseScore);
                bestReleaseAdjustment = saturatedSubtract(score, afterArtist);
            }
        }

        BaseCandidate selected = remaining.takeAt(bestIndex);

        if (bestArtistAdjustment != 0
            && selected.artistKey.has_value()) {
            selected.explanation.push_back(
                RankExplanationEvidence{
                    RankEvidenceKind::DiversityArtistAdjustment,
                    QStringLiteral("be3:diversity:artist:")
                        + *selected.artistKey,
                    bestArtistAdjustment,
                    std::nullopt,
                });
        }
        if (bestReleaseAdjustment != 0
            && selected.releaseGroupKey.has_value()) {
            selected.explanation.push_back(
                RankExplanationEvidence{
                    RankEvidenceKind::DiversityReleaseGroupAdjustment,
                    QStringLiteral("be3:diversity:release_group:")
                        + *selected.releaseGroupKey,
                    bestReleaseAdjustment,
                    std::nullopt,
                });
        }

        sortExplanation(selected.explanation);

        ranked.push_back(
            RankedRecommendation(
                selected.entity,
                selected.directlyPlayable,
                bestScore,
                std::move(selected.explanation)));

        if (selected.artistKey.has_value())
            ++artistCounts[*selected.artistKey];
        if (selected.releaseGroupKey.has_value())
            ++releaseGroupCounts[*selected.releaseGroupKey];
    }

    return RankerResult<RankerOutput>::success(
        RankerOutput(
            config.version.trimmed(),
            std::move(ranked),
            std::move(suppressed)));
}

} // namespace vinyl::recommendation
