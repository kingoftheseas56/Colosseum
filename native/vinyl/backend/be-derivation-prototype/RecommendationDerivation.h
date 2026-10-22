#pragma once

#include "VinylListenEventLedger.h"
#include "FeedbackTasteState.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl::recommendation {

inline constexpr int kRecommendationDerivationVersion = 1;
inline constexpr int kRecommendationExposureSchemaVersion = 2;

enum class DerivationErrorCode {
    None,
    InvalidConfiguration,
    ProfileMismatch,
    InvalidObservation,
    InvalidExposureId,
    InvalidExposure,
    ConflictingExposure,
};

QString derivationErrorCodeName(DerivationErrorCode code);

struct DerivationError final {
    DerivationErrorCode code = DerivationErrorCode::None;
    QString field;
    QString detail;
};

template <typename T>
struct DerivationResult final {
    std::optional<T> value;
    DerivationError error;

    bool ok() const noexcept { return value.has_value(); }

    static DerivationResult success(T value)
    {
        DerivationResult out;
        out.value = std::move(value);
        return out;
    }

    static DerivationResult failure(DerivationErrorCode code,
                                    QString field,
                                    QString detail = QString())
    {
        DerivationResult out;
        out.error = DerivationError{
            code,
            std::move(field),
            std::move(detail),
        };
        return out;
    }
};

struct DerivationMutationStatus final {
    bool ok = true;
    bool changed = false;
    DerivationError error;

    static DerivationMutationStatus success(bool changed = true)
    {
        DerivationMutationStatus out;
        out.changed = changed;
        return out;
    }

    static DerivationMutationStatus failure(DerivationErrorCode code,
                                            QString field,
                                            QString detail = QString())
    {
        DerivationMutationStatus out;
        out.ok = false;
        out.error = DerivationError{
            code,
            std::move(field),
            std::move(detail),
        };
        return out;
    }
};

struct RecommendationDerivationConfig final {
    int version = kRecommendationDerivationVersion;
    qint64 nowMs = 0;
    qint64 recentWindowMs = 30LL * 24LL * 60LL * 60LL * 1000LL;
    qint64 earlySkipPositionMs = 30LL * 1000LL;
    // Caller must explicitly assert complete profile history for the ranking scope.
    // Missing, disabled, truncated or partially imported history remains unknown.
    bool historyComplete = false;
};

class EntityHistoryAggregate final
{
public:
    explicit EntityHistoryAggregate(RecommendationEntityRef entity)
        : m_entity(std::move(entity))
    {
    }

    const RecommendationEntityRef &entity() const noexcept { return m_entity; }
    int playCount() const noexcept { return m_playCount; }
    const std::optional<qint64> &lastPlayedAtMs() const noexcept
    {
        return m_lastPlayedAtMs;
    }
    int recentCompletionCount() const noexcept { return m_recentCompletionCount; }
    int recentSkipCount() const noexcept { return m_recentSkipCount; }
    int naturalCompletionCount() const noexcept { return m_naturalCompletionCount; }
    int earlySkipCount() const noexcept { return m_earlySkipCount; }
    int passiveAutoplayCompletionCount() const noexcept
    {
        return m_passiveAutoplayCompletionCount;
    }

private:
    RecommendationEntityRef m_entity;
    int m_playCount = 0;
    std::optional<qint64> m_lastPlayedAtMs;
    int m_recentCompletionCount = 0;
    int m_recentSkipCount = 0;
    int m_naturalCompletionCount = 0;
    int m_earlySkipCount = 0;
    int m_passiveAutoplayCompletionCount = 0;

    friend DerivationResult<class RecommendationDerivationOutput>
    deriveRecommendationInputs(const ListenEventLedger &ledger,
                               const RecommendationDerivationConfig &config);
};

class RecommendationDerivationOutput final
{
public:
    RecommendationDerivationOutput(
        ProfileScopeId profileId,
        int version,
        QVector<BehavioralSignalObservation> observations,
        QVector<EntityHistoryAggregate> aggregates,
        bool historyComplete = false)
        : m_profileId(std::move(profileId))
        , m_version(version)
        , m_observations(std::move(observations))
        , m_aggregates(std::move(aggregates))
        , m_historyComplete(historyComplete)
    {
    }

    const ProfileScopeId &profileId() const noexcept { return m_profileId; }
    int version() const noexcept { return m_version; }
    bool historyComplete() const noexcept { return m_historyComplete; }
    const QVector<BehavioralSignalObservation> &observations() const noexcept
    {
        return m_observations;
    }
    const QVector<EntityHistoryAggregate> &aggregates() const noexcept
    {
        return m_aggregates;
    }

    const EntityHistoryAggregate *findAggregate(
        const RecommendationEntityRef &entity) const;

private:
    ProfileScopeId m_profileId;
    int m_version = kRecommendationDerivationVersion;
    QVector<BehavioralSignalObservation> m_observations;
    QVector<EntityHistoryAggregate> m_aggregates;
    bool m_historyComplete = false;
};

DerivationResult<RecommendationDerivationOutput> deriveRecommendationInputs(
    const ListenEventLedger &ledger,
    const RecommendationDerivationConfig &config);

enum class ExposureEvidenceKind {
    CandidateGenerator,
    ExplicitTaste,
    BehavioralHistory,
    HistoryAggregate,
    CanonicalRelationship,
    CatalogueContext,
    DiscoveryEvidence,
    DiversityPolicy,
    ExplorationPolicy,
    FatiguePolicy,
};

QString exposureEvidenceKindName(ExposureEvidenceKind kind);

class RecommendationExposureId final
{
public:
    static DerivationResult<RecommendationExposureId> parse(
        const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const RecommendationExposureId &other) const noexcept
    {
        return m_value == other.m_value;
    }

    bool operator!=(const RecommendationExposureId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit RecommendationExposureId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class ExposureEvidenceRef final
{
public:
    static DerivationResult<ExposureEvidenceRef> create(
        ExposureEvidenceKind kind,
        QString sourceId,
        std::optional<RecommendationEntityRef> relatedEntity = std::nullopt);

    ExposureEvidenceKind kind() const noexcept { return m_kind; }
    const QString &sourceId() const noexcept { return m_sourceId; }
    const std::optional<RecommendationEntityRef> &relatedEntity() const noexcept
    {
        return m_relatedEntity;
    }

    bool operator==(const ExposureEvidenceRef &other) const noexcept
    {
        return m_kind == other.m_kind
            && m_sourceId == other.m_sourceId
            && m_relatedEntity == other.m_relatedEntity;
    }

private:
    ExposureEvidenceRef(ExposureEvidenceKind kind,
                        QString sourceId,
                        std::optional<RecommendationEntityRef> relatedEntity)
        : m_kind(kind)
        , m_sourceId(std::move(sourceId))
        , m_relatedEntity(std::move(relatedEntity))
    {
    }

    ExposureEvidenceKind m_kind;
    QString m_sourceId;
    std::optional<RecommendationEntityRef> m_relatedEntity;
};

class RecommendationExposureRecord final
{
public:
    static DerivationResult<RecommendationExposureRecord> create(
        ProfileScopeId profileId,
        RecommendationExposureId exposureId,
        RecommendationEntityRef entity,
        qint64 recommendedAtMs,
        int rankPosition,
        QString rankingConfigVersion,
        QVector<ExposureEvidenceRef> evidence);

    int schemaVersion() const noexcept
    {
        return kRecommendationExposureSchemaVersion;
    }
    const ProfileScopeId &profileId() const noexcept { return m_profileId; }
    const RecommendationExposureId &exposureId() const noexcept
    {
        return m_exposureId;
    }
    const RecommendationEntityRef &entity() const noexcept { return m_entity; }
    qint64 recommendedAtMs() const noexcept { return m_recommendedAtMs; }
    int rankPosition() const noexcept { return m_rankPosition; }
    const QString &rankingConfigVersion() const noexcept
    {
        return m_rankingConfigVersion;
    }
    const QVector<ExposureEvidenceRef> &evidence() const noexcept
    {
        return m_evidence;
    }

    bool samePayload(const RecommendationExposureRecord &other) const noexcept;

private:
    RecommendationExposureRecord(
        ProfileScopeId profileId,
        RecommendationExposureId exposureId,
        RecommendationEntityRef entity,
        qint64 recommendedAtMs,
        int rankPosition,
        QString rankingConfigVersion,
        QVector<ExposureEvidenceRef> evidence)
        : m_profileId(std::move(profileId))
        , m_exposureId(std::move(exposureId))
        , m_entity(std::move(entity))
        , m_recommendedAtMs(recommendedAtMs)
        , m_rankPosition(rankPosition)
        , m_rankingConfigVersion(std::move(rankingConfigVersion))
        , m_evidence(std::move(evidence))
    {
    }

    ProfileScopeId m_profileId;
    RecommendationExposureId m_exposureId;
    RecommendationEntityRef m_entity;
    qint64 m_recommendedAtMs = 0;
    int m_rankPosition = 0;
    QString m_rankingConfigVersion;
    QVector<ExposureEvidenceRef> m_evidence;
};

class RecommendationExposureLog final
{
public:
    explicit RecommendationExposureLog(ProfileScopeId profileId)
        : m_profileId(std::move(profileId))
    {
    }

    const ProfileScopeId &profileId() const noexcept { return m_profileId; }
    const QVector<RecommendationExposureRecord> &records() const noexcept
    {
        return m_records;
    }

    DerivationMutationStatus append(RecommendationExposureRecord record);

private:
    ProfileScopeId m_profileId;
    QVector<RecommendationExposureRecord> m_records;
};

} // namespace vinyl::recommendation
