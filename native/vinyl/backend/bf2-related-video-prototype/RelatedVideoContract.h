#pragma once

#include "VinylDomainTypes.h"

#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class RelatedVideoErrorCode {
    None,
    InvalidEvidence,
    InvalidFacts,
    OfficialRelationshipUnproven,
    DuplicateCandidateKey,
    RecordingMismatch,
};

struct RelatedVideoError final {
    RelatedVideoErrorCode code = RelatedVideoErrorCode::None;
    QString field;
    QString detail;
};

QString relatedVideoErrorCodeName(RelatedVideoErrorCode code);

template <typename T>
struct RelatedVideoResult final {
    std::optional<T> value;
    RelatedVideoError error;

    bool ok() const noexcept { return value.has_value(); }

    static RelatedVideoResult success(T value)
    {
        RelatedVideoResult out;
        out.value = std::move(value);
        return out;
    }

    static RelatedVideoResult failure(RelatedVideoErrorCode code,
                                      QString field,
                                      QString detail = QString())
    {
        RelatedVideoResult out;
        out.error = RelatedVideoError{code, std::move(field), std::move(detail)};
        return out;
    }
};

enum class RelatedVideoRelationshipKind {
    OfficialMusicVideo,
    OfficialAudioVisualizer,
    LivePerformance,
    LyricVideo,
    AlternatePerformance,
    OtherUnknown,
};

QString relatedVideoRelationshipKindName(RelatedVideoRelationshipKind kind);

enum class RelationshipEvidenceKind {
    CanonicalMapping,
    ProviderRelationshipMetadata,
    ProviderOfficialDesignation,
    CuratedMapping,
    MetadataMatch,
    TextSearchSimilarity,
    Unknown,
};

QString relationshipEvidenceKindName(RelationshipEvidenceKind kind);

enum class RelationshipEvidenceStrength {
    Unknown,
    Weak,
    Strong,
};

QString relationshipEvidenceStrengthName(RelationshipEvidenceStrength strength);

struct RelationshipEvidence final {
    RelationshipEvidenceKind kind = RelationshipEvidenceKind::Unknown;
    RelationshipEvidenceStrength strength = RelationshipEvidenceStrength::Unknown;
    std::optional<QString> detail;
};

enum class RelatedVideoQualification {
    Unqualified,
    Weak,
    Qualified,
};

QString relatedVideoQualificationName(RelatedVideoQualification qualification);

enum class RelatedVideoDiscoveryMethod {
    CanonicalMapping,
    ProviderStructuredRelation,
    ProviderSearch,
    CuratedMapping,
    ImportedIndex,
};

QString relatedVideoDiscoveryMethodName(RelatedVideoDiscoveryMethod method);

struct RelatedVideoFacts final {
    std::optional<qint64> durationMs;
    std::optional<QString> title;
    std::optional<QString> channel;
};

class RelatedVideoCandidateKey final
{
public:
    RelatedVideoCandidateKey(RecordingMbid recordingMbid,
                             ProviderId providerId,
                             ProviderItemId providerItemId)
        : m_recordingMbid(std::move(recordingMbid))
        , m_providerId(std::move(providerId))
        , m_providerItemId(std::move(providerItemId))
    {
    }

    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const ProviderId &providerId() const noexcept { return m_providerId; }
    const ProviderItemId &providerItemId() const noexcept { return m_providerItemId; }

    QString stableKey() const;

    bool operator==(const RelatedVideoCandidateKey &other) const noexcept
    {
        return m_recordingMbid == other.m_recordingMbid
            && m_providerId == other.m_providerId
            && m_providerItemId == other.m_providerItemId;
    }

    bool operator!=(const RelatedVideoCandidateKey &other) const noexcept
    {
        return !(*this == other);
    }

private:
    RecordingMbid m_recordingMbid;
    ProviderId m_providerId;
    ProviderItemId m_providerItemId;
};

class RelatedVideoProvenance final
{
public:
    RelatedVideoProvenance(ProviderItemRef providerItem,
                           RelatedVideoDiscoveryMethod discoveryMethod)
        : m_providerItem(std::move(providerItem))
        , m_discoveryMethod(discoveryMethod)
    {
    }

    const ProviderItemRef &providerItem() const noexcept { return m_providerItem; }
    RelatedVideoDiscoveryMethod discoveryMethod() const noexcept
    {
        return m_discoveryMethod;
    }

private:
    ProviderItemRef m_providerItem;
    RelatedVideoDiscoveryMethod m_discoveryMethod;
};

class RelatedVideoCandidate final
{
public:
    static RelatedVideoResult<RelatedVideoCandidate> create(
        RecordingMbid recordingMbid,
        ProviderId providerId,
        ProviderItemId providerItemId,
        RelatedVideoRelationshipKind relationshipKind,
        QVector<RelationshipEvidence> evidence,
        RelatedVideoDiscoveryMethod discoveryMethod,
        RelatedVideoFacts facts = {});

    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const RelatedVideoCandidateKey &key() const noexcept { return m_key; }
    const RelatedVideoProvenance &provenance() const noexcept { return m_provenance; }
    RelatedVideoRelationshipKind relationshipKind() const noexcept
    {
        return m_relationshipKind;
    }
    const QVector<RelationshipEvidence> &evidence() const noexcept
    {
        return m_evidence;
    }
    RelationshipEvidenceStrength strongestEvidenceStrength() const noexcept
    {
        return m_strongestEvidenceStrength;
    }
    RelatedVideoQualification qualification() const noexcept { return m_qualification; }
    const RelatedVideoFacts &facts() const noexcept { return m_facts; }

    bool eligibleForAudioVideoSwitch() const noexcept;
    bool eligibleForRelatedVideos() const noexcept;

private:
    RelatedVideoCandidate(RecordingMbid recordingMbid,
                          RelatedVideoCandidateKey key,
                          RelatedVideoProvenance provenance,
                          RelatedVideoRelationshipKind relationshipKind,
                          QVector<RelationshipEvidence> evidence,
                          RelationshipEvidenceStrength strongestEvidenceStrength,
                          RelatedVideoQualification qualification,
                          RelatedVideoFacts facts)
        : m_recordingMbid(std::move(recordingMbid))
        , m_key(std::move(key))
        , m_provenance(std::move(provenance))
        , m_relationshipKind(relationshipKind)
        , m_evidence(std::move(evidence))
        , m_strongestEvidenceStrength(strongestEvidenceStrength)
        , m_qualification(qualification)
        , m_facts(std::move(facts))
    {
    }

    RecordingMbid m_recordingMbid;
    RelatedVideoCandidateKey m_key;
    RelatedVideoProvenance m_provenance;
    RelatedVideoRelationshipKind m_relationshipKind;
    QVector<RelationshipEvidence> m_evidence;
    RelationshipEvidenceStrength m_strongestEvidenceStrength;
    RelatedVideoQualification m_qualification;
    RelatedVideoFacts m_facts;
};

class RelatedVideoCandidateSet final
{
public:
    static RelatedVideoResult<RelatedVideoCandidateSet> create(
        RecordingMbid recordingMbid,
        QVector<RelatedVideoCandidate> candidates);

    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const QVector<RelatedVideoCandidate> &candidates() const noexcept
    {
        return m_candidates;
    }

private:
    RelatedVideoCandidateSet(RecordingMbid recordingMbid,
                             QVector<RelatedVideoCandidate> candidates)
        : m_recordingMbid(std::move(recordingMbid))
        , m_candidates(std::move(candidates))
    {
    }

    RecordingMbid m_recordingMbid;
    QVector<RelatedVideoCandidate> m_candidates;
};

} // namespace vinyl
