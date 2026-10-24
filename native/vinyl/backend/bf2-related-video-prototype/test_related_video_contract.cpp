#include "RelatedVideoContract.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <optional>
#include <utility>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (!condition) {
        qCritical().noquote() << "FAIL:" << message;
        std::exit(1);
    }
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustVideo(const vinyl::RelatedVideoResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::ProviderId providerId(const QString &text)
{
    return mustDomain(
        vinyl::ProviderId::parse(text),
        "provider ID should parse");
}

vinyl::ProviderItemId providerItemId(const QString &text)
{
    return mustDomain(
        vinyl::ProviderItemId::parse(text),
        "provider item ID should parse");
}

vinyl::RelationshipEvidence evidence(
    vinyl::RelationshipEvidenceKind kind,
    vinyl::RelationshipEvidenceStrength strength,
    std::optional<QString> detail = std::nullopt)
{
    return vinyl::RelationshipEvidence{
        kind,
        strength,
        std::move(detail),
    };
}

vinyl::RelatedVideoCandidate makeCandidate(
    const vinyl::RecordingMbid &recording,
    const QString &provider,
    const QString &item,
    vinyl::RelatedVideoRelationshipKind relationshipKind,
    QVector<vinyl::RelationshipEvidence> relationshipEvidence,
    vinyl::RelatedVideoDiscoveryMethod discoveryMethod =
        vinyl::RelatedVideoDiscoveryMethod::ProviderSearch,
    vinyl::RelatedVideoFacts facts = {})
{
    return mustVideo(
        vinyl::RelatedVideoCandidate::create(
            recording,
            providerId(provider),
            providerItemId(item),
            relationshipKind,
            std::move(relationshipEvidence),
            discoveryMethod,
            std::move(facts)),
        "related video candidate should normalize");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    const auto recording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "recording MBID should parse");
    const auto otherRecording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "second Recording MBID should parse");

    const auto officialEvidence = QVector<RelationshipEvidence>{
        evidence(
            RelationshipEvidenceKind::ProviderOfficialDesignation,
            RelationshipEvidenceStrength::Strong,
            QStringLiteral("structured official designation")),
    };

    const auto official = makeCandidate(
        recording,
        QStringLiteral("YouTube.Music"),
        QStringLiteral("official-video"),
        RelatedVideoRelationshipKind::OfficialMusicVideo,
        officialEvidence,
        RelatedVideoDiscoveryMethod::ProviderStructuredRelation);

    require(official.recordingMbid() == recording,
            "official video must retain canonical Recording MBID");
    require(official.qualification() == RelatedVideoQualification::Qualified,
            "strong official evidence should qualify the relationship");
    require(official.eligibleForAudioVideoSwitch(),
            "qualified official music video may power Audio | Video");
    require(official.eligibleForRelatedVideos(),
            "qualified official video remains a related-video candidate");

    const QString mbidLookingVideoId =
        QStringLiteral("22222222-2222-4222-8222-222222222222");
    const auto opaqueProviderVideo = makeCandidate(
        recording,
        QStringLiteral("video.provider"),
        mbidLookingVideoId,
        RelatedVideoRelationshipKind::AlternatePerformance,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::TextSearchSimilarity,
                RelationshipEvidenceStrength::Weak),
        });

    require(opaqueProviderVideo.recordingMbid() == recording,
            "provider video ID must never replace Recording MBID");
    require(opaqueProviderVideo.recordingMbid() != otherRecording,
            "MBID-looking provider item must stay provider identity");
    require(opaqueProviderVideo.key().providerItemId().value()
                == mbidLookingVideoId,
            "provider video ID should remain opaque and exact");

    const auto weakSearch = makeCandidate(
        recording,
        QStringLiteral("youtube.search"),
        QStringLiteral("weak-search-hit"),
        RelatedVideoRelationshipKind::AlternatePerformance,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::TextSearchSimilarity,
                RelationshipEvidenceStrength::Weak,
                QStringLiteral("artist and title text overlap")),
        });

    require(weakSearch.qualification() == RelatedVideoQualification::Weak,
            "weak text similarity must remain weak");
    require(!weakSearch.eligibleForAudioVideoSwitch(),
            "weak text similarity must never power Audio | Video");
    require(weakSearch.eligibleForRelatedVideos(),
            "weak text similarity may remain eligible for Related videos");
    require(official.qualification() != weakSearch.qualification(),
            "official relationship and weak search similarity must remain distinct");

    const auto providerVideoA = makeCandidate(
        recording,
        QStringLiteral("video.provider"),
        QStringLiteral("video-a"),
        RelatedVideoRelationshipKind::LyricVideo,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::ProviderRelationshipMetadata,
                RelationshipEvidenceStrength::Strong),
        },
        RelatedVideoDiscoveryMethod::ProviderStructuredRelation);
    const auto providerVideoB = makeCandidate(
        recording,
        QStringLiteral("video.provider"),
        QStringLiteral("video-b"),
        RelatedVideoRelationshipKind::LyricVideo,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::ProviderRelationshipMetadata,
                RelationshipEvidenceStrength::Strong),
        },
        RelatedVideoDiscoveryMethod::ProviderStructuredRelation);

    require(providerVideoA.key() != providerVideoB.key(),
            "two provider videos for one Recording must be separately addressable");
    require(providerVideoA.key().stableKey() != providerVideoB.key().stableKey(),
            "separate provider videos must have separate deterministic keys");

    const auto live = makeCandidate(
        recording,
        QStringLiteral("concert.archive"),
        QStringLiteral("live-1979"),
        RelatedVideoRelationshipKind::LivePerformance,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::ProviderRelationshipMetadata,
                RelationshipEvidenceStrength::Strong,
                QStringLiteral("provider marked live performance")),
        },
        RelatedVideoDiscoveryMethod::ProviderStructuredRelation);

    require(live.relationshipKind() == RelatedVideoRelationshipKind::LivePerformance,
            "live performance must retain its relationship kind");
    require(live.relationshipKind() != RelatedVideoRelationshipKind::OfficialMusicVideo,
            "live performance must not collapse into canonical music video");
    require(live.qualification() == RelatedVideoQualification::Qualified,
            "structured live relation may be strongly qualified");
    require(!live.eligibleForAudioVideoSwitch(),
            "qualified live performance must not silently replace canonical audio");
    require(live.eligibleForRelatedVideos(),
            "qualified live performance should remain related-video eligible");

    const auto unknownOfficial = RelatedVideoCandidate::create(
        recording,
        providerId(QStringLiteral("unknown.provider")),
        providerItemId(QStringLiteral("unknown-official-claim")),
        RelatedVideoRelationshipKind::OfficialMusicVideo,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::Unknown,
                RelationshipEvidenceStrength::Unknown),
        },
        RelatedVideoDiscoveryMethod::ProviderSearch);

    require(!unknownOfficial.ok(),
            "unknown evidence must not construct an official music video");
    require(unknownOfficial.error.code
                == RelatedVideoErrorCode::OfficialRelationshipUnproven,
            "unknown official claim must fail with explicit officiality error");

    const auto strongTextSearch = RelatedVideoCandidate::create(
        recording,
        providerId(QStringLiteral("search.provider")),
        providerItemId(QStringLiteral("overclaimed-search")),
        RelatedVideoRelationshipKind::AlternatePerformance,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::TextSearchSimilarity,
                RelationshipEvidenceStrength::Strong),
        },
        RelatedVideoDiscoveryMethod::ProviderSearch);

    require(!strongTextSearch.ok(),
            "text search must not be promotable to strong evidence");
    require(strongTextSearch.error.code == RelatedVideoErrorCode::InvalidEvidence,
            "overclaimed text search should fail evidence validation");

    const auto forwardSet = mustVideo(
        RelatedVideoCandidateSet::create(
            recording,
            QVector<RelatedVideoCandidate>{providerVideoA, providerVideoB}),
        "forward provider order should normalize");
    const auto reverseSet = mustVideo(
        RelatedVideoCandidateSet::create(
            recording,
            QVector<RelatedVideoCandidate>{providerVideoB, providerVideoA}),
        "reverse provider order should normalize");

    require(forwardSet.candidates().size() == 2,
            "both provider videos should survive candidate-set normalization");
    require(forwardSet.candidates().at(0).key().stableKey()
                == reverseSet.candidates().at(0).key().stableKey(),
            "provider ordering must not carry relationship authority");
    require(forwardSet.candidates().at(1).key().stableKey()
                == reverseSet.candidates().at(1).key().stableKey(),
            "candidate-set canonicalization must be input-order independent");

    const auto deterministicA = makeCandidate(
        recording,
        QStringLiteral("Deterministic.Provider"),
        QStringLiteral("video/id?x=1"),
        RelatedVideoRelationshipKind::LyricVideo,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::CanonicalMapping,
                RelationshipEvidenceStrength::Strong),
        },
        RelatedVideoDiscoveryMethod::CanonicalMapping);
    const auto deterministicB = makeCandidate(
        recording,
        QStringLiteral("deterministic.provider"),
        QStringLiteral("video/id?x=1"),
        RelatedVideoRelationshipKind::LyricVideo,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::CanonicalMapping,
                RelationshipEvidenceStrength::Strong),
        },
        RelatedVideoDiscoveryMethod::CanonicalMapping);

    require(deterministicA.key() == deterministicB.key(),
            "equivalent normalized inputs must produce equal candidate identity");
    require(deterministicA.key().stableKey() == deterministicB.key().stableKey(),
            "candidate identity serialization must be deterministic");
    require(deterministicA.key().stableKey().startsWith(
                QStringLiteral("rv1|11111111-1111-4111-8111-111111111111|deterministic.provider|")),
            "deterministic key must include canonical Recording and provider namespace");

    RelatedVideoFacts facts;
    facts.durationMs = 213456;
    facts.title = QStringLiteral("Fixture Video");
    facts.channel = QStringLiteral("Fixture Channel");
    const auto knownFacts = makeCandidate(
        recording,
        QStringLiteral("facts.provider"),
        QStringLiteral("known-facts"),
        RelatedVideoRelationshipKind::LyricVideo,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::CanonicalMapping,
                RelationshipEvidenceStrength::Strong),
        },
        RelatedVideoDiscoveryMethod::CanonicalMapping,
        facts);

    require(knownFacts.facts().durationMs == std::optional<qint64>{213456},
            "known duration should be preserved");
    require(knownFacts.facts().title
                == std::optional<QString>{QStringLiteral("Fixture Video")},
            "known title should be preserved");
    require(knownFacts.facts().channel
                == std::optional<QString>{QStringLiteral("Fixture Channel")},
            "known channel should be preserved");

    RelatedVideoFacts invalidFacts;
    invalidFacts.durationMs = 0;
    const auto badFacts = RelatedVideoCandidate::create(
        recording,
        providerId(QStringLiteral("facts.provider")),
        providerItemId(QStringLiteral("bad-facts")),
        RelatedVideoRelationshipKind::LyricVideo,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::CanonicalMapping,
                RelationshipEvidenceStrength::Strong),
        },
        RelatedVideoDiscoveryMethod::CanonicalMapping,
        invalidFacts);

    require(!badFacts.ok(),
            "invalid known facts should fail rather than being guessed");
    require(badFacts.error.code == RelatedVideoErrorCode::InvalidFacts,
            "invalid candidate facts should use typed BF2 error");

    const auto otherCandidate = makeCandidate(
        otherRecording,
        QStringLiteral("video.provider"),
        QStringLiteral("other-recording-video"),
        RelatedVideoRelationshipKind::LyricVideo,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::CanonicalMapping,
                RelationshipEvidenceStrength::Strong),
        },
        RelatedVideoDiscoveryMethod::CanonicalMapping);
    const auto mixedSet = RelatedVideoCandidateSet::create(
        recording,
        QVector<RelatedVideoCandidate>{providerVideoA, otherCandidate});

    require(!mixedSet.ok(),
            "candidate set must not mix canonical Recording identities");
    require(mixedSet.error.code == RelatedVideoErrorCode::RecordingMismatch,
            "mixed Recording set should fail with typed mismatch");

    const auto duplicateSet = RelatedVideoCandidateSet::create(
        recording,
        QVector<RelatedVideoCandidate>{providerVideoA, providerVideoA});
    require(!duplicateSet.ok(),
            "duplicate deterministic candidate keys must be rejected");
    require(duplicateSet.error.code == RelatedVideoErrorCode::DuplicateCandidateKey,
            "duplicate candidate key should use typed BF2 error");

    const auto unqualified = makeCandidate(
        recording,
        QStringLiteral("unknown.provider"),
        QStringLiteral("unqualified"),
        RelatedVideoRelationshipKind::OtherUnknown,
        QVector<RelationshipEvidence>{
            evidence(
                RelationshipEvidenceKind::Unknown,
                RelationshipEvidenceStrength::Unknown),
        });

    require(unqualified.qualification() == RelatedVideoQualification::Unqualified,
            "unknown-only evidence must remain unqualified");
    require(!unqualified.eligibleForRelatedVideos(),
            "unqualified mapping should not become a product-facing related video");

    require(relatedVideoErrorCodeName(
                RelatedVideoErrorCode::OfficialRelationshipUnproven)
                == QStringLiteral("official_relationship_unproven"),
            "BF2 errors should expose stable snake_case names");
    require(relatedVideoRelationshipKindName(
                RelatedVideoRelationshipKind::LivePerformance)
                == QStringLiteral("live_performance"),
            "relationship kinds should expose stable provider-neutral names");
    require(relationshipEvidenceKindName(
                RelationshipEvidenceKind::TextSearchSimilarity)
                == QStringLiteral("text_search_similarity"),
            "evidence kinds should expose stable names");
    require(relatedVideoQualificationName(RelatedVideoQualification::Qualified)
                == QStringLiteral("qualified"),
            "qualification should expose stable names");
    require(relatedVideoDiscoveryMethodName(
                RelatedVideoDiscoveryMethod::ProviderStructuredRelation)
                == QStringLiteral("provider_structured_relation"),
            "discovery provenance should expose stable names");

    qInfo().noquote()
        << "VINYL_BF2_RELATED_VIDEO_OK checks=" << g_checks;
    return 0;
}
