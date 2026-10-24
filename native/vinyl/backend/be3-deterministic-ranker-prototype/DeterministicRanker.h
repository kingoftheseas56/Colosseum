#pragma once

#include "RecommendationCandidateContract.h"
#include "FeedbackTasteState.h"
#include "RecommendationDerivation.h"
#include "VinylContextGraph.h"
#include "VinylCatalogueTypes.h"
#include "DiscoveryEnrichment.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl::recommendation {

enum class RankerErrorCode {
    None,
    InvalidConfiguration,
    ProfileMismatch,
};

QString rankerErrorCodeName(RankerErrorCode code);

struct RankerError final {
    RankerErrorCode code = RankerErrorCode::None;
    QString field;
    QString detail;
};

template <typename T>
struct RankerResult final {
    std::optional<T> value;
    RankerError error;

    bool ok() const noexcept { return value.has_value(); }

    static RankerResult success(T value)
    {
        RankerResult out;
        out.value = std::move(value);
        return out;
    }

    static RankerResult failure(RankerErrorCode code,
                                QString field,
                                QString detail = QString())
    {
        RankerResult out;
        out.error = RankerError{
            code,
            std::move(field),
            std::move(detail),
        };
        return out;
    }
};

struct RawScoreCalibration final {
    RecommendationGeneratorId generatorId;
    double multiplier = 0.0;
    qint64 maxAbsContribution = 0;
};

struct DeterministicRankerConfig final {
    QString version;
    qint64 nowMs = 0;
    int maxResults = 20;

    qint64 generatorSupportWeight = 20;
    qint64 explicitPositiveWeight = 1000;
    qint64 explicitNegativeWeight = -1000;
    qint64 playCountWeight = 45;
    int maxPlayCount = 5;
    qint64 recentCompletionWeight = 35;
    int maxRecentCompletionCount = 4;
    qint64 earlySkipWeight = -140;
    int maxEarlySkipCount = 3;
    qint64 relationshipWeight = 90;
    int maxRelationshipCount = 3;
    qint64 discoveryWeight = 30;
    int maxDiscoveryCount = 3;
    qint64 explorationBonus = 55;
    qint64 exposurePenalty = -110;
    qint64 exposureWindowMs = 14LL * 24LL * 60LL * 60LL * 1000LL;
    int maxRecentExposureCount = 4;
    qint64 repeatedArtistPenalty = -130;
    qint64 repeatedReleaseGroupPenalty = -80;
    int maxDiversityRepeatCount = 3;
    // Opt-in Home policy; existing callers retain their established ranking.
    qint64 seedAffinityWeight = 0;
    int maxPerArtist = 0;
    int maxRecordingsPerReleaseGroup = 0;

    QVector<RawScoreCalibration> rawScoreCalibrations;
};

enum class RankEvidenceKind {
    CandidateGeneratorSupport,
    CalibratedGeneratorScore,
    ExplicitPositive,
    ExplicitNegative,
    PlayCount,
    RecentCompletion,
    EarlySkip,
    CanonicalRelationship,
    DiscoveryPresence,
    ExplorationBonus,
    ExposureFatigue,
    DiversityArtistAdjustment,
    DiversityReleaseGroupAdjustment,
    SeedAffinity,
};

QString rankEvidenceKindName(RankEvidenceKind kind);

struct RankExplanationEvidence final {
    RankEvidenceKind kind = RankEvidenceKind::CandidateGeneratorSupport;
    QString sourceId;
    qint64 scoreDelta = 0;
    std::optional<RecommendationEntityRef> relatedEntity;

    bool operator==(const RankExplanationEvidence &other) const noexcept
    {
        return kind == other.kind
            && sourceId == other.sourceId
            && scoreDelta == other.scoreDelta
            && relatedEntity == other.relatedEntity;
    }
};

struct SeedAffinityEvidence final {
    RecommendationEntityRef entity;
    RecommendationEntityRef seed;
    double multiplier = 1.0;
};

struct RankerFeatureContext final {
    std::optional<context::ContextGraph> contextGraph;
    QVector<catalogue::Artist> artists;
    QVector<catalogue::ReleaseGroup> releaseGroups;
    QVector<catalogue::Recording> recordings;
    QVector<discovery::DiscoveryCollection> discoveryCollections;
    QVector<SeedAffinityEvidence> seedAffinities;
};

class RankedRecommendation final
{
public:
    RankedRecommendation(RecommendationEntityRef entity,
                         bool directlyPlayable,
                         qint64 score,
                         QVector<RankExplanationEvidence> explanation)
        : m_entity(std::move(entity))
        , m_directlyPlayable(directlyPlayable)
        , m_score(score)
        , m_explanation(std::move(explanation))
    {
    }

    const RecommendationEntityRef &entity() const noexcept { return m_entity; }
    bool directlyPlayable() const noexcept { return m_directlyPlayable; }
    qint64 score() const noexcept { return m_score; }
    const QVector<RankExplanationEvidence> &explanation() const noexcept
    {
        return m_explanation;
    }

private:
    RecommendationEntityRef m_entity;
    bool m_directlyPlayable = false;
    qint64 m_score = 0;
    QVector<RankExplanationEvidence> m_explanation;
};

class RankerOutput final
{
public:
    RankerOutput(QString configVersion,
                 QVector<RankedRecommendation> ranked,
                 QVector<RecommendationEntityRef> suppressed)
        : m_configVersion(std::move(configVersion))
        , m_ranked(std::move(ranked))
        , m_suppressed(std::move(suppressed))
    {
    }

    const QString &configVersion() const noexcept { return m_configVersion; }
    const QVector<RankedRecommendation> &ranked() const noexcept
    {
        return m_ranked;
    }
    const QVector<RecommendationEntityRef> &suppressed() const noexcept
    {
        return m_suppressed;
    }

private:
    QString m_configVersion;
    QVector<RankedRecommendation> m_ranked;
    QVector<RecommendationEntityRef> m_suppressed;
};

RankerResult<RankerOutput> rankDeterministically(
    const RecommendationCandidateSet &candidates,
    const TasteProfileState &taste,
    const RecommendationDerivationOutput &history,
    const RecommendationExposureLog &exposureLog,
    const RankerFeatureContext &features,
    const DeterministicRankerConfig &config);

} // namespace vinyl::recommendation
