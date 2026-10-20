#include "RecommendationCandidateContract.h"

#include <QHash>

#include <cmath>

namespace vinyl {

QString recommendationCandidateErrorCodeName(
    RecommendationCandidateErrorCode code)
{
    switch (code) {
    case RecommendationCandidateErrorCode::None:
        return QStringLiteral("none");
    case RecommendationCandidateErrorCode::InvalidCanonicalKind:
        return QStringLiteral("invalid_canonical_kind");
    case RecommendationCandidateErrorCode::InvalidGeneratorId:
        return QStringLiteral("invalid_generator_id");
    case RecommendationCandidateErrorCode::InvalidProviderProvenance:
        return QStringLiteral("invalid_provider_provenance");
    case RecommendationCandidateErrorCode::InvalidProviderScore:
        return QStringLiteral("invalid_provider_score");
    case RecommendationCandidateErrorCode::MissingEvidence:
        return QStringLiteral("missing_evidence");
    case RecommendationCandidateErrorCode::InvalidEvidence:
        return QStringLiteral("invalid_evidence");
    case RecommendationCandidateErrorCode::InvalidSeedContext:
        return QStringLiteral("invalid_seed_context");
    }
    return QStringLiteral("unknown");
}

RecommendationCandidateResult<RecommendationGeneratorId>
RecommendationGeneratorId::parse(const QString &text)
{
    const auto parsed = ProviderId::parse(text);
    if (!parsed.ok()) {
        return RecommendationCandidateResult<RecommendationGeneratorId>::
            failure(
                RecommendationCandidateErrorCode::InvalidGeneratorId,
                QStringLiteral("generator_id"),
                parsed.error.detail);
    }

    return RecommendationCandidateResult<RecommendationGeneratorId>::success(
        RecommendationGeneratorId(parsed.value->value()));
}

QString recommendationSourceFamilyName(RecommendationSourceFamily family)
{
    switch (family) {
    case RecommendationSourceFamily::Collaborative:
        return QStringLiteral("collaborative");
    case RecommendationSourceFamily::Graph:
        return QStringLiteral("graph");
    case RecommendationSourceFamily::Sonic:
        return QStringLiteral("sonic");
    case RecommendationSourceFamily::Editorial:
        return QStringLiteral("editorial");
    case RecommendationSourceFamily::Session:
        return QStringLiteral("session");
    case RecommendationSourceFamily::History:
        return QStringLiteral("history");
    }
    return QStringLiteral("unknown");
}

RecommendationCandidateResult<RecommendationSeed> RecommendationSeed::create(
    CanonicalEntityRef entity,
    QString context)
{
    const QString normalized = context.trimmed();
    if (normalized.isEmpty() || normalized.size() > 256) {
        return RecommendationCandidateResult<RecommendationSeed>::failure(
            RecommendationCandidateErrorCode::InvalidSeedContext,
            QStringLiteral("seed.context"),
            QStringLiteral("seed context must be 1..256 non-whitespace characters"));
    }

    return RecommendationCandidateResult<RecommendationSeed>::success(
        RecommendationSeed(std::move(entity), normalized));
}

QString recommendationEvidenceKindName(RecommendationEvidenceKind kind)
{
    switch (kind) {
    case RecommendationEvidenceKind::CollaborativePrior:
        return QStringLiteral("collaborative_prior");
    case RecommendationEvidenceKind::CanonicalRelationship:
        return QStringLiteral("canonical_relationship");
    case RecommendationEvidenceKind::SonicSimilarity:
        return QStringLiteral("sonic_similarity");
    case RecommendationEvidenceKind::EditorialSelection:
        return QStringLiteral("editorial_selection");
    case RecommendationEvidenceKind::SessionFit:
        return QStringLiteral("session_fit");
    case RecommendationEvidenceKind::HistorySignal:
        return QStringLiteral("history_signal");
    }
    return QStringLiteral("unknown");
}

RecommendationCandidateResult<RecommendationReasonEvidence>
RecommendationReasonEvidence::create(
    RecommendationEvidenceKind kind,
    QString detail,
    std::optional<CanonicalEntityRef> relatedCanonicalEntity)
{
    const QString normalized = detail.trimmed();
    if (normalized.isEmpty() || normalized.size() > 2048) {
        return RecommendationCandidateResult<RecommendationReasonEvidence>::
            failure(
                RecommendationCandidateErrorCode::InvalidEvidence,
                QStringLiteral("evidence.detail"),
                QStringLiteral(
                    "evidence detail must be 1..2048 non-whitespace characters"));
    }

    return RecommendationCandidateResult<RecommendationReasonEvidence>::
        success(
            RecommendationReasonEvidence(
                kind,
                normalized,
                std::move(relatedCanonicalEntity)));
}

RecommendationCandidateResult<RecommendationCandidateProvenance>
RecommendationCandidateProvenance::create(
    RecommendationGeneratorId generatorId,
    RecommendationSourceFamily sourceFamily,
    std::optional<ProviderId> providerId,
    std::optional<ProviderItemId> providerCandidateId,
    std::optional<QString> providerModelId,
    std::optional<RecommendationSeed> seed)
{
    if (providerCandidateId.has_value() && !providerId.has_value()) {
        return RecommendationCandidateResult<
            RecommendationCandidateProvenance>::failure(
                RecommendationCandidateErrorCode::InvalidProviderProvenance,
                QStringLiteral("provenance.provider_candidate_id"),
                QStringLiteral(
                    "provider candidate identity requires provider identity"));
    }

    if (providerModelId.has_value()) {
        const QString normalizedModel = providerModelId->trimmed();
        if (!providerId.has_value() || normalizedModel.isEmpty()
            || normalizedModel.size() > 512) {
            return RecommendationCandidateResult<
                RecommendationCandidateProvenance>::failure(
                    RecommendationCandidateErrorCode::InvalidProviderProvenance,
                    QStringLiteral("provenance.provider_model_id"),
                    QStringLiteral(
                        "provider model identity requires provider identity "
                        "and 1..512 non-whitespace characters"));
        }
        providerModelId = normalizedModel;
    }

    return RecommendationCandidateResult<
        RecommendationCandidateProvenance>::success(
            RecommendationCandidateProvenance(
                std::move(generatorId),
                sourceFamily,
                std::move(providerId),
                std::move(providerCandidateId),
                std::move(providerModelId),
                std::move(seed)));
}

RecommendationCandidateResult<RecommendationContribution>
RecommendationContribution::create(
    RecommendationCanonicalContext canonicalContext,
    RecommendationCandidateProvenance provenance,
    std::optional<double> rawProviderScore,
    QVector<RecommendationReasonEvidence> evidence)
{
    if (rawProviderScore.has_value()
        && !std::isfinite(*rawProviderScore)) {
        return RecommendationCandidateResult<RecommendationContribution>::
            failure(
                RecommendationCandidateErrorCode::InvalidProviderScore,
                QStringLiteral("raw_provider_score"),
                QStringLiteral("provider score must be finite when present"));
    }

    if (evidence.isEmpty()) {
        return RecommendationCandidateResult<RecommendationContribution>::
            failure(
                RecommendationCandidateErrorCode::MissingEvidence,
                QStringLiteral("evidence"),
                QStringLiteral(
                    "a recommendation contribution requires evidence"));
    }

    return RecommendationCandidateResult<RecommendationContribution>::success(
        RecommendationContribution(
            std::move(canonicalContext),
            std::move(provenance),
            rawProviderScore,
            std::move(evidence)));
}

RecommendationCandidateResult<RecommendationCandidate>
RecommendationCandidate::create(
    CanonicalEntityRef canonical,
    RecommendationContribution contribution)
{
    switch (canonical.kind()) {
    case CanonicalEntityKind::Recording:
    case CanonicalEntityKind::ReleaseGroup:
    case CanonicalEntityKind::Artist:
        break;
    case CanonicalEntityKind::Release:
    case CanonicalEntityKind::ReleaseTrack:
    case CanonicalEntityKind::Work:
        return RecommendationCandidateResult<RecommendationCandidate>::failure(
            RecommendationCandidateErrorCode::InvalidCanonicalKind,
            QStringLiteral("canonical"),
            QStringLiteral(
                "recommendation candidates are restricted to Recording, "
                "ReleaseGroup, and Artist"));
    }

    QVector<RecommendationContribution> contributions;
    contributions.push_back(std::move(contribution));

    return RecommendationCandidateResult<RecommendationCandidate>::success(
        RecommendationCandidate(
            std::move(canonical),
            std::move(contributions)));
}

RecommendationCandidateSet RecommendationCandidateSet::normalize(
    QVector<RecommendationCandidate> candidates)
{
    QVector<RecommendationCandidate> normalized;
    normalized.reserve(candidates.size());

    QHash<QString, int> indexByCanonical;

    for (auto &candidate : candidates) {
        const QString key =
            canonicalEntityKindName(candidate.m_canonical.kind())
            + QLatin1Char(':')
            + candidate.m_canonical.mbidString();
        const auto existing = indexByCanonical.constFind(key);

        if (existing == indexByCanonical.constEnd()) {
            const int index = normalized.size();
            indexByCanonical.insert(key, index);
            normalized.push_back(std::move(candidate));
            continue;
        }

        auto &target = normalized[existing.value()];
        for (auto &contribution : candidate.m_contributions)
            target.m_contributions.push_back(std::move(contribution));
    }

    return RecommendationCandidateSet(std::move(normalized));
}

} // namespace vinyl
