#pragma once

#include "VinylDomainTypes.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class RecommendationCandidateErrorCode {
    None,
    InvalidCanonicalKind,
    InvalidGeneratorId,
    InvalidProviderProvenance,
    InvalidProviderScore,
    MissingEvidence,
    InvalidEvidence,
    InvalidSeedContext,
};

struct RecommendationCandidateError final {
    RecommendationCandidateErrorCode code =
        RecommendationCandidateErrorCode::None;
    QString field;
    QString detail;
};

QString recommendationCandidateErrorCodeName(
    RecommendationCandidateErrorCode code);

template <typename T>
struct RecommendationCandidateResult final {
    std::optional<T> value;
    RecommendationCandidateError error;

    bool ok() const noexcept { return value.has_value(); }

    static RecommendationCandidateResult success(T value)
    {
        RecommendationCandidateResult out;
        out.value = std::move(value);
        return out;
    }

    static RecommendationCandidateResult failure(
        RecommendationCandidateErrorCode code,
        QString field,
        QString detail = QString())
    {
        RecommendationCandidateResult out;
        out.error = RecommendationCandidateError{
            code,
            std::move(field),
            std::move(detail),
        };
        return out;
    }
};

class RecommendationGeneratorId final
{
public:
    static RecommendationCandidateResult<RecommendationGeneratorId> parse(
        const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const RecommendationGeneratorId &other) const noexcept
    {
        return m_value == other.m_value;
    }

    bool operator!=(const RecommendationGeneratorId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit RecommendationGeneratorId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

enum class RecommendationSourceFamily {
    Collaborative,
    Graph,
    Sonic,
    Editorial,
    Session,
    History,
};

QString recommendationSourceFamilyName(RecommendationSourceFamily family);

struct RecommendationCanonicalContext final {
    std::optional<ArtistMbid> artistMbid;
    std::optional<ReleaseGroupMbid> releaseGroupMbid;
};

class RecommendationSeed final
{
public:
    static RecommendationCandidateResult<RecommendationSeed> create(
        CanonicalEntityRef entity,
        QString context);

    const CanonicalEntityRef &entity() const noexcept { return m_entity; }
    const QString &context() const noexcept { return m_context; }

private:
    RecommendationSeed(CanonicalEntityRef entity, QString context)
        : m_entity(std::move(entity))
        , m_context(std::move(context))
    {
    }

    CanonicalEntityRef m_entity;
    QString m_context;
};

enum class RecommendationEvidenceKind {
    CollaborativePrior,
    CanonicalRelationship,
    SonicSimilarity,
    EditorialSelection,
    SessionFit,
    HistorySignal,
};

QString recommendationEvidenceKindName(RecommendationEvidenceKind kind);

class RecommendationReasonEvidence final
{
public:
    static RecommendationCandidateResult<RecommendationReasonEvidence> create(
        RecommendationEvidenceKind kind,
        QString detail,
        std::optional<CanonicalEntityRef> relatedCanonicalEntity =
            std::nullopt);

    RecommendationEvidenceKind kind() const noexcept { return m_kind; }
    const QString &detail() const noexcept { return m_detail; }

    const std::optional<CanonicalEntityRef> &relatedCanonicalEntity() const
        noexcept
    {
        return m_relatedCanonicalEntity;
    }

private:
    RecommendationReasonEvidence(
        RecommendationEvidenceKind kind,
        QString detail,
        std::optional<CanonicalEntityRef> relatedCanonicalEntity)
        : m_kind(kind)
        , m_detail(std::move(detail))
        , m_relatedCanonicalEntity(std::move(relatedCanonicalEntity))
    {
    }

    RecommendationEvidenceKind m_kind;
    QString m_detail;
    std::optional<CanonicalEntityRef> m_relatedCanonicalEntity;
};

class RecommendationCandidateProvenance final
{
public:
    static RecommendationCandidateResult<RecommendationCandidateProvenance>
    create(
        RecommendationGeneratorId generatorId,
        RecommendationSourceFamily sourceFamily,
        std::optional<ProviderId> providerId = std::nullopt,
        std::optional<ProviderItemId> providerCandidateId = std::nullopt,
        std::optional<QString> providerModelId = std::nullopt,
        std::optional<RecommendationSeed> seed = std::nullopt);

    const RecommendationGeneratorId &generatorId() const noexcept
    {
        return m_generatorId;
    }

    RecommendationSourceFamily sourceFamily() const noexcept
    {
        return m_sourceFamily;
    }

    const std::optional<ProviderId> &providerId() const noexcept
    {
        return m_providerId;
    }

    const std::optional<ProviderItemId> &providerCandidateId() const noexcept
    {
        return m_providerCandidateId;
    }

    const std::optional<QString> &providerModelId() const noexcept
    {
        return m_providerModelId;
    }

    const std::optional<RecommendationSeed> &seed() const noexcept
    {
        return m_seed;
    }

private:
    RecommendationCandidateProvenance(
        RecommendationGeneratorId generatorId,
        RecommendationSourceFamily sourceFamily,
        std::optional<ProviderId> providerId,
        std::optional<ProviderItemId> providerCandidateId,
        std::optional<QString> providerModelId,
        std::optional<RecommendationSeed> seed)
        : m_generatorId(std::move(generatorId))
        , m_sourceFamily(sourceFamily)
        , m_providerId(std::move(providerId))
        , m_providerCandidateId(std::move(providerCandidateId))
        , m_providerModelId(std::move(providerModelId))
        , m_seed(std::move(seed))
    {
    }

    RecommendationGeneratorId m_generatorId;
    RecommendationSourceFamily m_sourceFamily;
    std::optional<ProviderId> m_providerId;
    std::optional<ProviderItemId> m_providerCandidateId;
    std::optional<QString> m_providerModelId;
    std::optional<RecommendationSeed> m_seed;
};

class RecommendationContribution final
{
public:
    static RecommendationCandidateResult<RecommendationContribution> create(
        RecommendationCanonicalContext canonicalContext,
        RecommendationCandidateProvenance provenance,
        std::optional<double> rawProviderScore,
        QVector<RecommendationReasonEvidence> evidence);

    const RecommendationCanonicalContext &canonicalContext() const noexcept
    {
        return m_canonicalContext;
    }

    const RecommendationCandidateProvenance &provenance() const noexcept
    {
        return m_provenance;
    }

    const std::optional<double> &rawProviderScore() const noexcept
    {
        return m_rawProviderScore;
    }

    const QVector<RecommendationReasonEvidence> &evidence() const noexcept
    {
        return m_evidence;
    }

private:
    RecommendationContribution(
        RecommendationCanonicalContext canonicalContext,
        RecommendationCandidateProvenance provenance,
        std::optional<double> rawProviderScore,
        QVector<RecommendationReasonEvidence> evidence)
        : m_canonicalContext(std::move(canonicalContext))
        , m_provenance(std::move(provenance))
        , m_rawProviderScore(rawProviderScore)
        , m_evidence(std::move(evidence))
    {
    }

    RecommendationCanonicalContext m_canonicalContext;
    RecommendationCandidateProvenance m_provenance;
    std::optional<double> m_rawProviderScore;
    QVector<RecommendationReasonEvidence> m_evidence;
};

class RecommendationCandidateSet;

class RecommendationCandidate final
{
public:
    static RecommendationCandidateResult<RecommendationCandidate> create(
        CanonicalEntityRef canonical,
        RecommendationContribution contribution);

    const CanonicalEntityRef &canonicalEntity() const noexcept
    {
        return m_canonical;
    }

    bool isDirectlyPlayable() const noexcept
    {
        return m_canonical.kind() == CanonicalEntityKind::Recording;
    }

    const QVector<RecommendationContribution> &contributions() const noexcept
    {
        return m_contributions;
    }

private:
    RecommendationCandidate(
        CanonicalEntityRef canonical,
        QVector<RecommendationContribution> contributions)
        : m_canonical(std::move(canonical))
        , m_contributions(std::move(contributions))
    {
    }

    CanonicalEntityRef m_canonical;
    QVector<RecommendationContribution> m_contributions;

    friend class RecommendationCandidateSet;
};

class RecommendationCandidateSet final
{
public:
    static RecommendationCandidateSet normalize(
        QVector<RecommendationCandidate> candidates);

    const QVector<RecommendationCandidate> &candidates() const noexcept
    {
        return m_candidates;
    }

private:
    explicit RecommendationCandidateSet(
        QVector<RecommendationCandidate> candidates)
        : m_candidates(std::move(candidates))
    {
    }

    QVector<RecommendationCandidate> m_candidates;
};

} // namespace vinyl
