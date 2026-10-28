#include "SourceCandidateContract.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;

    qCritical().noquote() << "FAIL:" << message;
    std::exit(1);
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustSource(const vinyl::SourceCandidateResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T, typename = void>
struct HasResolvedUrl : std::false_type {
};

template <typename T>
struct HasResolvedUrl<T, std::void_t<decltype(std::declval<T>().resolvedUrl())>>
    : std::true_type {
};

vinyl::SourceProviderCapabilities httpCapabilities(bool resolves = true)
{
    using namespace vinyl;

    SourceProviderCapabilities capabilities;
    capabilities |= SourceProviderCapability::DiscoverCandidates;
    capabilities |= SourceProviderCapability::HttpSources;
    if (resolves)
        capabilities |= SourceProviderCapability::ResolveExactSource;
    return capabilities;
}

vinyl::NormalizedSourceCandidate makeHttpCandidate(
    const vinyl::RecordingMbid &recording,
    const vinyl::SourceProviderDescriptor &provider,
    const QString &candidateId,
    vinyl::SourceCandidateFacts facts = {},
    vinyl::CandidateResolutionMode mode =
        vinyl::CandidateResolutionMode::Deferred)
{
    using namespace vinyl;

    const auto parsedCandidateId = mustSource(
        SourceCandidateId::parse(candidateId),
        "candidate id should parse");

    return mustSource(
        NormalizedSourceCandidate::create(
            recording,
            provider,
            parsedCandidateId,
            SourceDiscoveryMethod::ProviderSearch,
            SourceKind::Http,
            mode,
            std::move(facts)),
        "candidate should normalize");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    static_assert(!std::is_same<ProviderId, SourceProviderId>::value,
                  "generic and source provider identity must remain distinct");
    static_assert(!std::is_convertible<ProviderId, SourceProviderId>::value,
                  "metadata provider identity must not silently become source identity");
    static_assert(!HasResolvedUrl<NormalizedSourceCandidate>::value,
                  "BB1 candidates must not own exact resolved URLs");

    const auto recording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "recording MBID should parse");
    const auto anotherRecording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "second recording MBID should parse");

    const auto sourceAId = mustSource(
        SourceProviderId::parse(QStringLiteral("Public-HTTP-A")),
        "source provider A should parse");
    const auto sourceBId = mustSource(
        SourceProviderId::parse(QStringLiteral("public-http-b")),
        "source provider B should parse");

    require(sourceAId.value() == QStringLiteral("public-http-a"),
            "source provider IDs should inherit deterministic BA0 normalization");

    const auto providerA = mustSource(
        SourceProviderDescriptor::create(sourceAId, httpCapabilities()),
        "HTTP provider A descriptor should be valid");
    const auto providerB = mustSource(
        SourceProviderDescriptor::create(sourceBId, httpCapabilities()),
        "HTTP provider B descriptor should be valid");

    const auto genericMetadataProvider = mustDomain(
        ProviderId::parse(QStringLiteral("metadata.catalog")),
        "generic metadata provider should parse");
    require(genericMetadataProvider.value() == QStringLiteral("metadata.catalog"),
            "generic provider identity should remain available independently");
    require(providerA.id().value() != genericMetadataProvider.value(),
            "metadata provider identity must not silently force source-provider identity");

    const QString mbidLookingCandidate =
        QStringLiteral("22222222-2222-4222-8222-222222222222");
    const auto canonicalPreservationCandidate = makeHttpCandidate(
        recording,
        providerA,
        mbidLookingCandidate);

    require(canonicalPreservationCandidate.requestedRecordingMbid() == recording,
            "provider candidate must retain the requested Recording MBID");
    require(canonicalPreservationCandidate.requestedRecordingMbid()
                != anotherRecording,
            "candidate ID that looks like an MBID must not replace canonical identity");
    require(canonicalPreservationCandidate.provenance().key().candidateId().value()
                == mbidLookingCandidate,
            "MBID-looking candidate ID must remain opaque provider identity");

    const QString sharedCandidateId = QStringLiteral("shared-local-id");
    const auto providerACandidate = makeHttpCandidate(
        recording,
        providerA,
        sharedCandidateId);
    const auto providerBCandidate = makeHttpCandidate(
        recording,
        providerB,
        sharedCandidateId);

    require(providerACandidate.provenance().key()
                != providerBCandidate.provenance().key(),
            "same local candidate ID from different providers must not collide");
    require(providerACandidate.provenance().key().stableKey()
                != providerBCandidate.provenance().key().stableKey(),
            "deterministic candidate key must include provider namespace");

    const auto reversedSet = mustSource(
        SourceCandidateSet::create(
            recording,
            QVector<NormalizedSourceCandidate>{
                providerBCandidate,
                providerACandidate,
            }),
        "reversed provider output should normalize");
    const auto forwardSet = mustSource(
        SourceCandidateSet::create(
            recording,
            QVector<NormalizedSourceCandidate>{
                providerACandidate,
                providerBCandidate,
            }),
        "forward provider output should normalize");

    require(reversedSet.candidates().size() == 2,
            "cross-provider duplicate local IDs should both survive");
    require(reversedSet.candidates().at(0).provenance().key().stableKey()
                == forwardSet.candidates().at(0).provenance().key().stableKey(),
            "provider output order must not change canonical candidate-set order");
    require(reversedSet.candidates().at(1).provenance().key().stableKey()
                == forwardSet.candidates().at(1).provenance().key().stableKey(),
            "canonical set normalization must be order-independent");

    const SourceCandidateFacts unknownFacts;
    const auto unknownQuality = makeHttpCandidate(
        recording,
        providerA,
        QStringLiteral("unknown-quality"),
        unknownFacts);

    require(!unknownQuality.facts().durationMs.has_value(),
            "unknown duration must stay unknown");
    require(!unknownQuality.facts().bitrateKbps.has_value(),
            "unknown bitrate must stay unknown");
    require(!unknownQuality.facts().codec.has_value(),
            "unknown codec must stay unknown");
    require(!unknownQuality.facts().lossless.has_value(),
            "unknown lossless state must stay unknown");

    SourceCandidateFacts knownFacts;
    knownFacts.durationMs = 201234;
    knownFacts.contentLengthBytes = 12345678;
    knownFacts.bitrateKbps = 320;
    knownFacts.sampleRateHz = 48000;
    knownFacts.bitDepth = 24;
    knownFacts.channels = 2;
    knownFacts.codec = QStringLiteral("aac");
    knownFacts.container = QStringLiteral("mp4");
    knownFacts.mimeType = QStringLiteral("audio/mp4");
    knownFacts.qualityLabel = QStringLiteral("provider_reported_high");
    knownFacts.lossless = false;

    const auto knownQuality = makeHttpCandidate(
        recording,
        providerA,
        QStringLiteral("known-quality"),
        knownFacts,
        CandidateResolutionMode::Refreshable);

    require(knownQuality.facts().bitrateKbps == std::optional<int>{320},
            "known bitrate should be preserved as a fact hint");
    require(knownQuality.resolutionMode() == CandidateResolutionMode::Refreshable,
            "refreshable delayed resolution should be explicit");

    SourceCandidateFacts invalidFacts;
    invalidFacts.bitrateKbps = 0;
    const auto invalidFactsCandidate = NormalizedSourceCandidate::create(
        recording,
        providerA,
        mustSource(SourceCandidateId::parse(QStringLiteral("bad-facts")),
                   "bad-facts id should parse"),
        SourceDiscoveryMethod::ProviderSearch,
        SourceKind::Http,
        CandidateResolutionMode::Deferred,
        invalidFacts);
    require(!invalidFactsCandidate.ok(),
            "fabricated or invalid numeric fact values should fail");
    require(invalidFactsCandidate.error.code
                == SourceCandidateErrorCode::InvalidFactHint,
            "invalid facts should use typed BB1 error");

    const auto malformedProvider =
        SourceProviderId::parse(QStringLiteral("bad source provider"));
    require(!malformedProvider.ok(),
            "malformed source-provider identifier should fail");
    require(malformedProvider.error.code
                == SourceCandidateErrorCode::InvalidSourceProviderId,
            "malformed provider should use typed BB1 error");

    const auto malformedCandidate =
        SourceCandidateId::parse(QStringLiteral(" candidate-with-space"));
    require(!malformedCandidate.ok(),
            "malformed candidate identifier should fail");
    require(malformedCandidate.error.code
                == SourceCandidateErrorCode::InvalidCandidateId,
            "malformed candidate should use typed BB1 error");

    const auto duplicateSet = SourceCandidateSet::create(
        recording,
        QVector<NormalizedSourceCandidate>{
            providerACandidate,
            providerACandidate,
        });
    require(!duplicateSet.ok(),
            "identical provider-namespaced candidate key should be rejected");
    require(duplicateSet.error.code
                == SourceCandidateErrorCode::DuplicateCandidateKey,
            "duplicate key should use typed BB1 error");

    const auto mismatchedRecordingCandidate = makeHttpCandidate(
        anotherRecording,
        providerA,
        QStringLiteral("wrong-recording"));
    const auto mismatchedSet = SourceCandidateSet::create(
        recording,
        QVector<NormalizedSourceCandidate>{
            providerACandidate,
            mismatchedRecordingCandidate,
        });
    require(!mismatchedSet.ok(),
            "candidate set must not mix requested Recording identities");
    require(mismatchedSet.error.code
                == SourceCandidateErrorCode::RecordingMismatch,
            "recording mismatch should use typed BB1 error");

    SourceProviderCapabilities discoveryOnlyCaps;
    discoveryOnlyCaps |= SourceProviderCapability::DiscoverCandidates;
    discoveryOnlyCaps |= SourceProviderCapability::HttpSources;
    const auto discoveryOnlyProvider = mustSource(
        SourceProviderDescriptor::create(
            mustSource(SourceProviderId::parse(QStringLiteral("discovery-only")),
                       "discovery-only provider id should parse"),
            discoveryOnlyCaps),
        "discovery-only provider descriptor should be valid");

    const auto delayedWithoutResolver = NormalizedSourceCandidate::create(
        recording,
        discoveryOnlyProvider,
        mustSource(SourceCandidateId::parse(QStringLiteral("deferred")),
                   "deferred id should parse"),
        SourceDiscoveryMethod::ProviderSearch,
        SourceKind::Http,
        CandidateResolutionMode::Deferred);
    require(!delayedWithoutResolver.ok(),
            "deferred candidate must declare exact-source resolution capability");
    require(delayedWithoutResolver.error.code
                == SourceCandidateErrorCode::ProviderMissingCapability,
            "missing resolver capability should be typed");

    SourceProviderCapabilities localCaps;
    localCaps |= SourceProviderCapability::DiscoverCandidates;
    localCaps |= SourceProviderCapability::LocalFileSources;
    const auto localProvider = mustSource(
        SourceProviderDescriptor::create(
            mustSource(SourceProviderId::parse(QStringLiteral("local.library")),
                       "local provider id should parse"),
            localCaps),
        "local provider descriptor should be valid");

    const auto wrongKind = NormalizedSourceCandidate::create(
        recording,
        localProvider,
        mustSource(SourceCandidateId::parse(QStringLiteral("local-1")),
                   "local id should parse"),
        SourceDiscoveryMethod::LocalScan,
        SourceKind::PeerToPeer,
        CandidateResolutionMode::StableReference);
    require(!wrongKind.ok(),
            "candidate kind must be declared in provider capabilities");
    require(wrongKind.error.code
                == SourceCandidateErrorCode::ProviderMissingCapability,
            "kind/capability mismatch should be typed");

    const auto emptyDescriptor = SourceProviderDescriptor::create(
        mustSource(SourceProviderId::parse(QStringLiteral("empty-provider")),
                   "empty provider id should parse"),
        SourceProviderCapabilities{});
    require(!emptyDescriptor.ok(),
            "provider descriptor without source capabilities should fail");
    require(emptyDescriptor.error.code
                == SourceCandidateErrorCode::InvalidProviderDescriptor,
            "empty descriptor should use typed BB1 error");

    const SourceCandidateFailure failedState{
        SourceCandidateFailureCode::ResolutionFailed,
        true,
        QStringLiteral("fixture resolution failed"),
    };
    const auto failedCandidate = mustSource(
        NormalizedSourceCandidate::create(
            recording,
            providerA,
            mustSource(SourceCandidateId::parse(QStringLiteral("retryable")),
                       "retryable id should parse"),
            SourceDiscoveryMethod::CanonicalMapping,
            SourceKind::Http,
            CandidateResolutionMode::Refreshable,
            {},
            failedState),
        "failed candidate should remain representable");

    require(failedCandidate.failure().has_value(),
            "candidate-layer failure state should be preserved");
    require(failedCandidate.failure()->code
                == SourceCandidateFailureCode::ResolutionFailed,
            "failure reason should be typed");
    require(failedCandidate.failure()->retryable,
            "retryability should be explicit rather than inferred");

    require(sourceCandidateErrorCodeName(
                SourceCandidateErrorCode::InvalidCandidateId)
                == QStringLiteral("invalid_candidate_id"),
            "BB1 errors should expose stable snake_case names");
    require(sourceKindName(SourceKind::PeerToPeer)
                == QStringLiteral("peer_to_peer"),
            "source kinds should expose provider-neutral stable names");
    require(candidateResolutionModeName(CandidateResolutionMode::Refreshable)
                == QStringLiteral("refreshable"),
            "resolution modes should expose stable names");

    qInfo().noquote()
        << "VINYL_BB1_SOURCE_CANDIDATE_OK checks=" << g_checks;
    return 0;
}
