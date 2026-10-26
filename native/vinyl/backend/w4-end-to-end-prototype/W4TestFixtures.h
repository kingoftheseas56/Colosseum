#pragma once

#include "SourceResolutionSession.h"
#include "VinylListenEventLedger.h"
#include "VinylSessionPersistence.h"

#include <QVector>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

namespace w4h {

inline int g_checks = 0;

inline void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::fflush(stderr);
    std::exit(1);
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustQueue(const vinyl::QueueResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustCandidate(const vinyl::SourceCandidateResult<T> &result,
                const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustExact(const vinyl::ExactTrackContractResult<T> &result,
            const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustResolution(const vinyl::SourceResolutionResult<T> &result,
                 const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustMpv(const vinyl::MpvPlaybackAdapterResult<T> &result,
          const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustListen(const vinyl::ListenLedgerResult<T> &result,
             const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPlayback(const vinyl::PlaybackSessionResult<T> &result,
               const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPersist(const vinyl::SessionPersistenceResult<T> &result,
              const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

inline vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "fixture RecordingMbid should parse");
}

inline vinyl::ProfileScopeId profile(const char *value)
{
    return mustDomain(
        vinyl::ProfileScopeId::parse(QString::fromLatin1(value)),
        "fixture ProfileScopeId should parse");
}

inline vinyl::PlayInstanceId playInstance(const char *value)
{
    return mustDomain(
        vinyl::PlayInstanceId::parse(QString::fromLatin1(value)),
        "fixture PlayInstanceId should parse");
}

inline vinyl::ListenSessionId listenSession(const char *value)
{
    return mustListen(
        vinyl::ListenSessionId::parse(QString::fromLatin1(value)),
        "fixture ListenSessionId should parse");
}

inline vinyl::ListenSourceTraceId sourceTrace(const char *value)
{
    return mustListen(
        vinyl::ListenSourceTraceId::parse(QString::fromLatin1(value)),
        "fixture ListenSourceTraceId should parse");
}

inline vinyl::QueueOccurrenceId occurrenceId(const char *value)
{
    return mustQueue(
        vinyl::QueueOccurrenceId::parse(QString::fromLatin1(value)),
        "fixture QueueOccurrenceId should parse");
}

inline vinyl::EnqueueBatchId batchId(const char *value)
{
    return mustQueue(
        vinyl::EnqueueBatchId::parse(QString::fromLatin1(value)),
        "fixture EnqueueBatchId should parse");
}

inline vinyl::QueueOccurrence occurrence(
    const char *id,
    const vinyl::RecordingMbid &recordingMbid,
    vinyl::EnqueueOriginKind originKind = vinyl::EnqueueOriginKind::Album)
{
    return mustQueue(
        vinyl::QueueOccurrence::create(
            occurrenceId(id),
            recordingMbid,
            std::nullopt,
            vinyl::EnqueueOrigin(
                originKind,
                batchId(originKind == vinyl::EnqueueOriginKind::Radio
                            ? "batch-radio"
                            : "batch-album"))),
        "fixture occurrence should construct");
}

inline vinyl::SourceProviderId sourceProvider(const char *value)
{
    return mustCandidate(
        vinyl::SourceProviderId::parse(QString::fromLatin1(value)),
        "fixture source provider should parse");
}

inline vinyl::SourceCandidateId sourceCandidateId(const char *value)
{
    return mustCandidate(
        vinyl::SourceCandidateId::parse(QString::fromLatin1(value)),
        "fixture source candidate id should parse");
}

inline vinyl::SourceProviderDescriptor sourceDescriptor(
    const char *provider,
    vinyl::SourceKind kind)
{
    using namespace vinyl;
    SourceProviderCapabilities capabilities =
        SourceProviderCapability::DiscoverCandidates;
    capabilities |= SourceProviderCapability::ResolveExactSource;
    switch (kind) {
    case SourceKind::LocalFile:
        capabilities |= SourceProviderCapability::LocalFileSources;
        break;
    case SourceKind::Http:
        capabilities |= SourceProviderCapability::HttpSources;
        break;
    case SourceKind::PeerToPeer:
        capabilities |= SourceProviderCapability::PeerToPeerSources;
        break;
    case SourceKind::ExternalResolver:
        capabilities |= SourceProviderCapability::ExternalResolverSources;
        break;
    case SourceKind::ProviderMediated:
        capabilities |= SourceProviderCapability::ProviderMediatedSources;
        break;
    case SourceKind::Unknown:
        break;
    }
    return mustCandidate(
        SourceProviderDescriptor::create(
            sourceProvider(provider),
            capabilities),
        "fixture source provider descriptor should construct");
}

inline vinyl::SelectableSourceCandidate exactAudio(
    const vinyl::RecordingMbid &recordingMbid,
    const char *provider,
    const char *item,
    int bitrate,
    bool lossless,
    vinyl::SourceKind kind = vinyl::SourceKind::Http)
{
    using namespace vinyl;
    SourceCandidateFacts facts;
    facts.bitrateKbps = bitrate;
    facts.sampleRateHz = lossless ? 96000 : 48000;
    facts.bitDepth = lossless ? 24 : 16;
    facts.lossless = lossless;
    facts.title = QStringLiteral("W4-H Fixture Track");

    const NormalizedSourceCandidate candidate = mustCandidate(
        NormalizedSourceCandidate::create(
            recordingMbid,
            sourceDescriptor(provider, kind),
            sourceCandidateId(item),
            SourceDiscoveryMethod::CanonicalMapping,
            kind,
            CandidateResolutionMode::Refreshable,
            std::move(facts)),
        "fixture normalized source should construct");

    const RequestedTrackIdentity request = mustExact(
        RequestedTrackIdentity::create(recordingMbid, std::nullopt),
        "fixture exact request should construct");

    CandidateCanonicalEvidence evidence;
    evidence.recordingMbid = recordingMbid;
    const CandidateTrackEntry entry = mustExact(
        CandidateTrackEntry::create(
            mustExact(
                ManifestEntryId::parse(
                    QStringLiteral("entry-")
                    + candidate.provenance().key().candidateId().value()),
                "fixture manifest entry id should parse"),
            evidence),
        "fixture manifest entry should construct");

    const AlbumSourceManifest manifest = mustExact(
        AlbumSourceManifest::create(
            candidate.provenance().key(),
            {entry}),
        "fixture exact manifest should construct");

    return mustResolution(
        SelectableSourceCandidate::fromExactAudio(
            candidate,
            request,
            manifest),
        "fixture exact audio candidate should be selectable");
}

inline vinyl::ResolvedTrackSource resolved(
    const vinyl::SourceResolutionRequest &request,
    const QString &resource,
    qint64 resolvedAt,
    std::optional<qint64> expiresAt = std::nullopt,
    std::optional<qint64> recordingOffset = std::nullopt)
{
    using namespace vinyl;
    return mustMpv(
        ResolvedTrackSource::create(
            request.recordingMbid(),
            request.candidateKey(),
            request.sourceKind(),
            request.transportKind() == SourceTransportKind::Video
                ? SourceResolutionPath::ExternalResolver
                : SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::HttpUrl,
            resource,
            {},
            std::nullopt,
            recordingOffset,
            resolvedAt,
            expiresAt),
        "fixture resolved transport should construct");
}

class EmptyInputProvider final : public vinyl::PlaybackLoadInputProvider
{
public:
    std::optional<vinyl::PlaybackLoadInput> inputFor(
        const vinyl::QueueOccurrence &) override
    {
        return std::nullopt;
    }
};

class FixedInputProvider final : public vinyl::PlaybackLoadInputProvider
{
public:
    explicit FixedInputProvider(vinyl::PlaybackLoadInput input)
        : m_input(std::move(input))
    {
    }

    std::optional<vinyl::PlaybackLoadInput> inputFor(
        const vinyl::QueueOccurrence &) override
    {
        return m_input;
    }

private:
    vinyl::PlaybackLoadInput m_input;
};

class FakeEngine final : public vinyl::PlaybackEngineCommands
{
public:
    struct Load final {
        vinyl::PlaybackWorkToken token;
        vinyl::PlaybackLoadInput input;
    };

    void load(const vinyl::PlaybackWorkToken &token,
              const vinyl::PlaybackLoadInput &input) override
    {
        loads.push_back({token, input});
    }

    void play(const vinyl::PlaybackWorkToken &token) override
    {
        plays.push_back(token);
    }

    void pause(const vinyl::PlaybackWorkToken &token) override
    {
        pauses.push_back(token);
    }

    void seek(const vinyl::PlaybackSeekRequest &request) override
    {
        seeks.push_back(request.workToken());
    }

    void stop(const vinyl::PlaybackWorkToken &token) override
    {
        stops.push_back(token);
    }

    QVector<Load> loads;
    QVector<vinyl::PlaybackWorkToken> plays;
    QVector<vinyl::PlaybackWorkToken> pauses;
    QVector<vinyl::PlaybackWorkToken> seeks;
    QVector<vinyl::PlaybackWorkToken> stops;
};

struct HarnessContext final {
    vinyl::VinylQueue queue;
    vinyl::PlaybackSession playback;
    FakeEngine engine;
    vinyl::PlaybackEngineAdapter adapter;
    EmptyInputProvider inputProvider;
    vinyl::QueueSessionCoordinator coordinator;
    vinyl::ReferenceSourceKindScorer scorer;
    vinyl::SourceProfileMemory memory;
    vinyl::SourceResolutionSession sources;

    explicit HarnessContext(vinyl::ProfileScopeId profileId)
        : adapter(playback, engine)
        , coordinator(queue, playback, adapter, inputProvider)
        , memory(profileId)
        , sources(
              queue,
              playback,
              coordinator,
              vinyl::SourceSelectionPolicy::vinylReference(),
              scorer,
              memory,
              std::move(profileId))
    {
    }
};

inline void requireInvariant(const HarnessContext &ctx, const char *message)
{
    const auto queueActive = ctx.queue.activeOccurrenceId();
    const auto &playbackActive = ctx.playback.activeOccurrence();
    const bool valid =
        !playbackActive.has_value()
        || (queueActive.has_value()
            && playbackActive->occurrenceId() == *queueActive);
    require(valid, message);
}

inline vinyl::PlaybackWorkToken activeToken(const HarnessContext &ctx)
{
    require(ctx.playback.activeWorkToken().has_value(),
            "active playback token should exist");
    return *ctx.playback.activeWorkToken();
}

inline bool hasEndCause(const vinyl::QueueSessionStatus &status,
                        vinyl::OccurrenceEndCause cause)
{
    for (const auto &fact : status.transitionFacts) {
        if (const auto *ended =
                std::get_if<vinyl::OccurrenceEnded>(&fact)) {
            if (ended->cause == cause)
                return true;
        }
    }
    return false;
}

inline bool hasAwaiting(const vinyl::QueueSessionStatus &status)
{
    for (const auto &fact : status.transitionFacts) {
        if (std::holds_alternative<vinyl::AwaitingInput>(fact))
            return true;
    }
    return false;
}

} // namespace w4h
