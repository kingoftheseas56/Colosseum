#include "RelatedVideoContract.h"

#include <QByteArray>

#include <algorithm>

namespace vinyl {

namespace {

int strengthRank(RelationshipEvidenceStrength strength)
{
    switch (strength) {
    case RelationshipEvidenceStrength::Unknown:
        return 0;
    case RelationshipEvidenceStrength::Weak:
        return 1;
    case RelationshipEvidenceStrength::Strong:
        return 2;
    }
    return 0;
}

bool isOfficialRelationship(RelatedVideoRelationshipKind kind)
{
    return kind == RelatedVideoRelationshipKind::OfficialMusicVideo
        || kind == RelatedVideoRelationshipKind::OfficialAudioVisualizer;
}

bool isStrongRelationEvidence(RelationshipEvidenceKind kind)
{
    switch (kind) {
    case RelationshipEvidenceKind::CanonicalMapping:
    case RelationshipEvidenceKind::ProviderRelationshipMetadata:
    case RelationshipEvidenceKind::ProviderOfficialDesignation:
    case RelationshipEvidenceKind::CuratedMapping:
        return true;
    case RelationshipEvidenceKind::MetadataMatch:
    case RelationshipEvidenceKind::TextSearchSimilarity:
    case RelationshipEvidenceKind::Unknown:
        return false;
    }
    return false;
}

bool isStrongOfficialEvidence(RelationshipEvidenceKind kind)
{
    return kind == RelationshipEvidenceKind::ProviderOfficialDesignation
        || kind == RelationshipEvidenceKind::CuratedMapping;
}

std::optional<RelatedVideoError> validateEvidence(
    const QVector<RelationshipEvidence> &evidence)
{
    for (qsizetype i = 0; i < evidence.size(); ++i) {
        const auto &item = evidence.at(i);

        if (item.kind == RelationshipEvidenceKind::Unknown
            && item.strength != RelationshipEvidenceStrength::Unknown) {
            return RelatedVideoError{
                RelatedVideoErrorCode::InvalidEvidence,
                QStringLiteral("evidence[%1].strength").arg(i),
                QStringLiteral("Unknown evidence cannot claim relationship strength."),
            };
        }

        if (item.kind == RelationshipEvidenceKind::TextSearchSimilarity
            && item.strength == RelationshipEvidenceStrength::Strong) {
            return RelatedVideoError{
                RelatedVideoErrorCode::InvalidEvidence,
                QStringLiteral("evidence[%1].strength").arg(i),
                QStringLiteral("Text-search similarity is weak evidence by contract."),
            };
        }

        if (item.detail && item.detail->trimmed().isEmpty()) {
            return RelatedVideoError{
                RelatedVideoErrorCode::InvalidEvidence,
                QStringLiteral("evidence[%1].detail").arg(i),
                QStringLiteral("Known evidence detail must not be blank."),
            };
        }
    }

    return std::nullopt;
}

std::optional<RelatedVideoError> validateFacts(const RelatedVideoFacts &facts)
{
    if (facts.durationMs && *facts.durationMs <= 0) {
        return RelatedVideoError{
            RelatedVideoErrorCode::InvalidFacts,
            QStringLiteral("facts.durationMs"),
            QStringLiteral("Known video duration must be positive."),
        };
    }

    if (facts.title && facts.title->trimmed().isEmpty()) {
        return RelatedVideoError{
            RelatedVideoErrorCode::InvalidFacts,
            QStringLiteral("facts.title"),
            QStringLiteral("Known video title must not be blank."),
        };
    }

    if (facts.channel && facts.channel->trimmed().isEmpty()) {
        return RelatedVideoError{
            RelatedVideoErrorCode::InvalidFacts,
            QStringLiteral("facts.channel"),
            QStringLiteral("Known video channel must not be blank."),
        };
    }

    return std::nullopt;
}

RelationshipEvidenceStrength strongestEvidence(
    const QVector<RelationshipEvidence> &evidence)
{
    RelationshipEvidenceStrength strongest = RelationshipEvidenceStrength::Unknown;
    for (const auto &item : evidence) {
        if (strengthRank(item.strength) > strengthRank(strongest))
            strongest = item.strength;
    }
    return strongest;
}

bool hasStrongRelationEvidence(const QVector<RelationshipEvidence> &evidence)
{
    for (const auto &item : evidence) {
        if (item.strength == RelationshipEvidenceStrength::Strong
            && isStrongRelationEvidence(item.kind)) {
            return true;
        }
    }
    return false;
}

bool hasStrongOfficialEvidence(const QVector<RelationshipEvidence> &evidence)
{
    for (const auto &item : evidence) {
        if (item.strength == RelationshipEvidenceStrength::Strong
            && isStrongOfficialEvidence(item.kind)) {
            return true;
        }
    }
    return false;
}

bool hasMeaningfulEvidence(const QVector<RelationshipEvidence> &evidence)
{
    for (const auto &item : evidence) {
        if (item.strength == RelationshipEvidenceStrength::Weak
            || item.strength == RelationshipEvidenceStrength::Strong) {
            return true;
        }
    }
    return false;
}

RelatedVideoQualification deriveQualification(
    RelatedVideoRelationshipKind relationshipKind,
    const QVector<RelationshipEvidence> &evidence)
{
    if (relationshipKind != RelatedVideoRelationshipKind::OtherUnknown
        && hasStrongRelationEvidence(evidence)) {
        return RelatedVideoQualification::Qualified;
    }

    if (hasMeaningfulEvidence(evidence))
        return RelatedVideoQualification::Weak;

    return RelatedVideoQualification::Unqualified;
}

} // namespace

QString relatedVideoErrorCodeName(RelatedVideoErrorCode code)
{
    switch (code) {
    case RelatedVideoErrorCode::None:
        return QStringLiteral("none");
    case RelatedVideoErrorCode::InvalidEvidence:
        return QStringLiteral("invalid_evidence");
    case RelatedVideoErrorCode::InvalidFacts:
        return QStringLiteral("invalid_facts");
    case RelatedVideoErrorCode::OfficialRelationshipUnproven:
        return QStringLiteral("official_relationship_unproven");
    case RelatedVideoErrorCode::DuplicateCandidateKey:
        return QStringLiteral("duplicate_candidate_key");
    case RelatedVideoErrorCode::RecordingMismatch:
        return QStringLiteral("recording_mismatch");
    }
    return QStringLiteral("unknown");
}

QString relatedVideoRelationshipKindName(RelatedVideoRelationshipKind kind)
{
    switch (kind) {
    case RelatedVideoRelationshipKind::OfficialMusicVideo:
        return QStringLiteral("official_music_video");
    case RelatedVideoRelationshipKind::OfficialAudioVisualizer:
        return QStringLiteral("official_audio_visualizer");
    case RelatedVideoRelationshipKind::LivePerformance:
        return QStringLiteral("live_performance");
    case RelatedVideoRelationshipKind::LyricVideo:
        return QStringLiteral("lyric_video");
    case RelatedVideoRelationshipKind::AlternatePerformance:
        return QStringLiteral("alternate_performance");
    case RelatedVideoRelationshipKind::OtherUnknown:
        return QStringLiteral("other_unknown");
    }
    return QStringLiteral("unknown");
}

QString relationshipEvidenceKindName(RelationshipEvidenceKind kind)
{
    switch (kind) {
    case RelationshipEvidenceKind::CanonicalMapping:
        return QStringLiteral("canonical_mapping");
    case RelationshipEvidenceKind::ProviderRelationshipMetadata:
        return QStringLiteral("provider_relationship_metadata");
    case RelationshipEvidenceKind::ProviderOfficialDesignation:
        return QStringLiteral("provider_official_designation");
    case RelationshipEvidenceKind::CuratedMapping:
        return QStringLiteral("curated_mapping");
    case RelationshipEvidenceKind::MetadataMatch:
        return QStringLiteral("metadata_match");
    case RelationshipEvidenceKind::TextSearchSimilarity:
        return QStringLiteral("text_search_similarity");
    case RelationshipEvidenceKind::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString relationshipEvidenceStrengthName(RelationshipEvidenceStrength strength)
{
    switch (strength) {
    case RelationshipEvidenceStrength::Unknown:
        return QStringLiteral("unknown");
    case RelationshipEvidenceStrength::Weak:
        return QStringLiteral("weak");
    case RelationshipEvidenceStrength::Strong:
        return QStringLiteral("strong");
    }
    return QStringLiteral("unknown");
}

QString relatedVideoQualificationName(RelatedVideoQualification qualification)
{
    switch (qualification) {
    case RelatedVideoQualification::Unqualified:
        return QStringLiteral("unqualified");
    case RelatedVideoQualification::Weak:
        return QStringLiteral("weak");
    case RelatedVideoQualification::Qualified:
        return QStringLiteral("qualified");
    }
    return QStringLiteral("unknown");
}

QString relatedVideoDiscoveryMethodName(RelatedVideoDiscoveryMethod method)
{
    switch (method) {
    case RelatedVideoDiscoveryMethod::CanonicalMapping:
        return QStringLiteral("canonical_mapping");
    case RelatedVideoDiscoveryMethod::ProviderStructuredRelation:
        return QStringLiteral("provider_structured_relation");
    case RelatedVideoDiscoveryMethod::ProviderSearch:
        return QStringLiteral("provider_search");
    case RelatedVideoDiscoveryMethod::CuratedMapping:
        return QStringLiteral("curated_mapping");
    case RelatedVideoDiscoveryMethod::ImportedIndex:
        return QStringLiteral("imported_index");
    }
    return QStringLiteral("unknown");
}

QString RelatedVideoCandidateKey::stableKey() const
{
    const QByteArray encodedItem = m_providerItemId.value().toUtf8().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    return QStringLiteral("rv1|%1|%2|%3")
        .arg(m_recordingMbid.toString(),
             m_providerId.value(),
             QString::fromLatin1(encodedItem));
}

RelatedVideoResult<RelatedVideoCandidate> RelatedVideoCandidate::create(
    RecordingMbid recordingMbid,
    ProviderId providerId,
    ProviderItemId providerItemId,
    RelatedVideoRelationshipKind relationshipKind,
    QVector<RelationshipEvidence> evidence,
    RelatedVideoDiscoveryMethod discoveryMethod,
    RelatedVideoFacts facts)
{
    if (const auto error = validateEvidence(evidence)) {
        return RelatedVideoResult<RelatedVideoCandidate>::failure(
            error->code,
            error->field,
            error->detail);
    }

    if (const auto error = validateFacts(facts)) {
        return RelatedVideoResult<RelatedVideoCandidate>::failure(
            error->code,
            error->field,
            error->detail);
    }

    if (isOfficialRelationship(relationshipKind)
        && !hasStrongOfficialEvidence(evidence)) {
        return RelatedVideoResult<RelatedVideoCandidate>::failure(
            RelatedVideoErrorCode::OfficialRelationshipUnproven,
            QStringLiteral("relationshipKind"),
            QStringLiteral("Official video relationships require strong official evidence."));
    }

    const auto strongest = strongestEvidence(evidence);
    const auto qualification = deriveQualification(relationshipKind, evidence);

    RelatedVideoCandidateKey key(recordingMbid, providerId, providerItemId);
    RelatedVideoProvenance provenance(
        ProviderItemRef(providerId, providerItemId),
        discoveryMethod);

    return RelatedVideoResult<RelatedVideoCandidate>::success(
        RelatedVideoCandidate(
            std::move(recordingMbid),
            std::move(key),
            std::move(provenance),
            relationshipKind,
            std::move(evidence),
            strongest,
            qualification,
            std::move(facts)));
}

bool RelatedVideoCandidate::eligibleForAudioVideoSwitch() const noexcept
{
    if (m_qualification != RelatedVideoQualification::Qualified)
        return false;

    return m_relationshipKind == RelatedVideoRelationshipKind::OfficialMusicVideo
        || m_relationshipKind == RelatedVideoRelationshipKind::OfficialAudioVisualizer;
}

bool RelatedVideoCandidate::eligibleForRelatedVideos() const noexcept
{
    return m_qualification == RelatedVideoQualification::Qualified
        || m_qualification == RelatedVideoQualification::Weak;
}

RelatedVideoResult<RelatedVideoCandidateSet> RelatedVideoCandidateSet::create(
    RecordingMbid recordingMbid,
    QVector<RelatedVideoCandidate> candidates)
{
    for (const auto &candidate : candidates) {
        if (candidate.recordingMbid() != recordingMbid) {
            return RelatedVideoResult<RelatedVideoCandidateSet>::failure(
                RelatedVideoErrorCode::RecordingMismatch,
                QStringLiteral("recordingMbid"),
                QStringLiteral("Every related-video candidate in a set must retain the same Recording MBID."));
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const RelatedVideoCandidate &left, const RelatedVideoCandidate &right) {
            return left.key().stableKey() < right.key().stableKey();
        });

    for (qsizetype i = 1; i < candidates.size(); ++i) {
        if (candidates.at(i - 1).key() == candidates.at(i).key()) {
            return RelatedVideoResult<RelatedVideoCandidateSet>::failure(
                RelatedVideoErrorCode::DuplicateCandidateKey,
                QStringLiteral("candidates"),
                QStringLiteral("A related-video candidate key may appear only once."));
        }
    }

    return RelatedVideoResult<RelatedVideoCandidateSet>::success(
        RelatedVideoCandidateSet(
            std::move(recordingMbid),
            std::move(candidates)));
}

} // namespace vinyl
