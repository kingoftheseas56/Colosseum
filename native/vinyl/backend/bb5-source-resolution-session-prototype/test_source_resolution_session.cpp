#include "SourceResolutionSession.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
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
T mustRelated(const vinyl::RelatedVideoResult<T> &result,
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

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "fixture RecordingMbid should parse");
}

vinyl::ProfileScopeId profile(const char *value)
{
    return mustDomain(
        vinyl::ProfileScopeId::parse(QString::fromLatin1(value)),
        "fixture ProfileScopeId should parse");
}

vinyl::PlayInstanceId playInstance(const char *value)
{
    return mustDomain(
        vinyl::PlayInstanceId::parse(QString::fromLatin1(value)),
        "fixture PlayInstanceId should parse");
}

vinyl::QueueOccurrenceId occurrenceId(const char *value)
{
    return mustQueue(
        vinyl::QueueOccurrenceId::parse(QString::fromLatin1(value)),
        "fixture QueueOccurrenceId should parse");
}

vinyl::QueueOccurrence occurrence(const char *id,
                                  const vinyl::RecordingMbid &recordingMbid)
{
    return mustQueue(
        vinyl::QueueOccurrence::create(
            occurrenceId(id),
            recordingMbid),
        "fixture occurrence should construct");
}

vinyl::SourceProviderId sourceProvider(const char *value)
{
    return mustCandidate(
        vinyl::SourceProviderId::parse(QString::fromLatin1(value)),
        "fixture source provider should parse");
}

vinyl::SourceCandidateId sourceCandidateId(const char *value)
{
    return mustCandidate(
        vinyl::SourceCandidateId::parse(QString::fromLatin1(value)),
        "fixture source candidate id should parse");
}

vinyl::SourceProviderDescriptor sourceDescriptor(
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

vinyl::NormalizedSourceCandidate normalizedAudio(
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
    facts.title = QStringLiteral("Fixture Track");

    return mustCandidate(
        NormalizedSourceCandidate::create(
            recordingMbid,
            sourceDescriptor(provider, kind),
            sourceCandidateId(item),
            SourceDiscoveryMethod::CanonicalMapping,
            kind,
            CandidateResolutionMode::Refreshable,
            std::move(facts)),
        "fixture audio source candidate should construct");
}

vinyl::SelectableSourceCandidate exactAudio(
    const vinyl::RecordingMbid &recordingMbid,
    const char *provider,
    const char *item,
    int bitrate,
    bool lossless,
    vinyl::SourceKind kind = vinyl::SourceKind::Http)
{
    using namespace vinyl;
    const NormalizedSourceCandidate candidate =
        normalizedAudio(
            recordingMbid,
            provider,
            item,
            bitrate,
            lossless,
            kind);

    const RequestedTrackIdentity request = mustExact(
        RequestedTrackIdentity::create(recordingMbid, std::nullopt),
        "fixture BB2 request should construct");

    CandidateCanonicalEvidence evidence;
    evidence.recordingMbid = recordingMbid;
    const CandidateTrackEntry entry = mustExact(
        CandidateTrackEntry::create(
            mustExact(
                ManifestEntryId::parse(
                    QStringLiteral("entry-")
                    + candidate.provenance().key().candidateId().value()),
                "manifest entry id should parse"),
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
        "exact audio candidate should be selectable");
}

vinyl::RelatedVideoCandidate officialVideo(
    const vinyl::RecordingMbid &recordingMbid,
    const char *provider,
    const char *item)
{
    using namespace vinyl;
    return mustRelated(
        RelatedVideoCandidate::create(
            recordingMbid,
            mustDomain(
                ProviderId::parse(QString::fromLatin1(provider)),
                "video provider should parse"),
            mustDomain(
                ProviderItemId::parse(QString::fromLatin1(item)),
                "video provider item should parse"),
            RelatedVideoRelationshipKind::OfficialMusicVideo,
            {{
                RelationshipEvidenceKind::ProviderOfficialDesignation,
                RelationshipEvidenceStrength::Strong,
                QStringLiteral("fixture official relationship"),
            }},
            RelatedVideoDiscoveryMethod::ProviderStructuredRelation,
            {}),
        "qualified official video should construct");
}

vinyl::SelectableSourceCandidate selectableVideo(
    const vinyl::RecordingMbid &recordingMbid,
    const char *provider,
    const char *item)
{
    return mustResolution(
        vinyl::SelectableSourceCandidate::fromQualifiedVideo(
            officialVideo(recordingMbid, provider, item)),
        "qualified official video should enter W4-C");
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

class FakeEngine final : public vinyl::PlaybackEngineCommands
{
public:
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

    struct Load final {
        vinyl::PlaybackWorkToken token;
        vinyl::PlaybackLoadInput input;
    };

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
        , memory(std::move(profileId))
        , sources(
              queue,
              playback,
              coordinator,
              vinyl::SourceSelectionPolicy::vinylReference(),
              scorer,
              memory,
              memory.profileId())
    {
    }
};

void requireInvariant(const HarnessContext &ctx, const char *message)
{
    const auto queueActive = ctx.queue.activeOccurrenceId();
    const auto &playbackActive = ctx.playback.activeOccurrence();
    const bool valid =
        !playbackActive.has_value()
        || (queueActive.has_value()
            && playbackActive->occurrenceId() == *queueActive);
    require(valid, message);
}

void appendAndActivate(HarnessContext &ctx,
                       const QVector<vinyl::QueueOccurrence> &items,
                       const vinyl::QueueOccurrenceId &activeId)
{
    for (const auto &item : items) {
        require(ctx.queue.append(item).ok,
                "fixture queue append should succeed");
    }
    require(ctx.queue.activate(activeId).ok,
            "fixture queue activation should succeed");
}

void armPending(HarnessContext &ctx, bool playWhenReady = true)
{
    const auto status = ctx.coordinator.start(playWhenReady);
    require(status.ok, "coordinator start should arm missing-source pending state");
    require(ctx.coordinator.pendingInput().has_value(),
            "missing input should produce coordinator pending state");
    requireInvariant(ctx, "arming pending input must preserve BA1/BA2 invariant");
}

vinyl::ResolvedTrackSource resolved(
    const vinyl::SourceResolutionRequest &request,
    const QString &resource,
    qint64 resolvedAt,
    std::optional<qint64> expiresAt = std::nullopt,
    std::optional<qint64> recordingOffset = std::nullopt)
{
    return mustMpv(
        vinyl::ResolvedTrackSource::create(
            request.recordingMbid(),
            request.candidateKey(),
            request.sourceKind(),
            request.transportKind() == vinyl::SourceTransportKind::Video
                ? vinyl::SourceResolutionPath::ExternalResolver
                : vinyl::SourceResolutionPath::Direct,
            vinyl::ResolvedPlaybackResourceKind::HttpUrl,
            resource,
            {},
            std::nullopt,
            recordingOffset,
            resolvedAt,
            expiresAt),
        "resolved fixture transport should construct");
}

vinyl::PlaybackWorkToken activeToken(const HarnessContext &ctx)
{
    require(ctx.playback.activeWorkToken().has_value(),
            "active playback token should exist");
    return *ctx.playback.activeWorkToken();
}

void verifyExactGate(
    const vinyl::RecordingMbid &canonical,
    const vinyl::RecordingMbid &other)
{
    using namespace vinyl;

    const NormalizedSourceCandidate base =
        normalizedAudio(canonical, "gate.provider", "gate-item", 320, false);
    const RequestedTrackIdentity request = mustExact(
        RequestedTrackIdentity::create(canonical, std::nullopt),
        "gate request should construct");

    CandidateCanonicalEvidence exactEvidence;
    exactEvidence.recordingMbid = canonical;
    const CandidateTrackEntry exactA = mustExact(
        CandidateTrackEntry::create(
            mustExact(
                ManifestEntryId::parse(QStringLiteral("a")),
                "entry a should parse"),
            exactEvidence),
        "entry a should construct");
    const CandidateTrackEntry exactB = mustExact(
        CandidateTrackEntry::create(
            mustExact(
                ManifestEntryId::parse(QStringLiteral("b")),
                "entry b should parse"),
            exactEvidence),
        "entry b should construct");

    const AlbumSourceManifest ambiguousManifest = mustExact(
        AlbumSourceManifest::create(
            base.provenance().key(),
            {exactA, exactB}),
        "ambiguous manifest should construct");

    const auto ambiguous =
        SelectableSourceCandidate::fromExactAudio(
            base, request, ambiguousManifest);
    require(!ambiguous.ok()
                && ambiguous.error.code
                    == SourceResolutionErrorCode::ExactResolutionRequired,
            "BB2 Ambiguous result must fail closed before playback selection");

    CandidateCanonicalEvidence wrongEvidence;
    wrongEvidence.recordingMbid = other;
    const CandidateTrackEntry wrongEntry = mustExact(
        CandidateTrackEntry::create(
            mustExact(
                ManifestEntryId::parse(QStringLiteral("wrong")),
                "wrong entry id should parse"),
            wrongEvidence),
        "wrong entry should construct");

    const AlbumSourceManifest rejectedManifest = mustExact(
        AlbumSourceManifest::create(
            base.provenance().key(),
            {wrongEntry}),
        "rejected manifest should construct");

    const auto rejected =
        SelectableSourceCandidate::fromExactAudio(
            base, request, rejectedManifest);
    require(!rejected.ok()
                && rejected.error.code
                    == SourceResolutionErrorCode::ExactResolutionRequired,
            "BB2 Rejected/nonexact result must fail closed before playback selection");
}

void verifyDeterministicSelection(
    const vinyl::RecordingMbid &canonical,
    const vinyl::SelectableSourceCandidate &high,
    const vinyl::SelectableSourceCandidate &low)
{
    using namespace vinyl;
    ReferenceSourceKindScorer scorer;
    SourceProfileMemory memory(profile("selection-profile"));

    const auto orderA = mustResolution(
        SourceSelector::order(
            canonical,
            {low, high},
            SourceSelectionPolicy::vinylReference(),
            scorer,
            memory),
        "first deterministic source order should succeed");
    const auto orderB = mustResolution(
        SourceSelector::order(
            canonical,
            {high, low},
            SourceSelectionPolicy::vinylReference(),
            scorer,
            memory),
        "permuted deterministic source order should succeed");

    require(orderA.size() == 2 && orderB.size() == 2,
            "two exact candidates should remain selectable");
    require(orderA.at(0).candidateKey() == high.candidateKey()
                && orderB.at(0).candidateKey() == high.candidateKey(),
            "source quality ordering must be independent of provider arrival order");
    require(orderA.at(1).candidateKey() == low.candidateKey()
                && orderB.at(1).candidateKey() == low.candidateKey(),
            "fallback order must remain identical under provider permutation");

    const auto equalA = exactAudio(
        canonical, "provider.a", "equal-a", 320, false);
    const auto equalB = exactAudio(
        canonical, "provider.b", "equal-b", 320, false);
    const auto preferredPolicy = mustResolution(
        SourceSelectionPolicy::create(
            {SourceKind::Http},
            {
                equalB.candidateKey().providerId(),
                equalA.candidateKey().providerId(),
            }),
        "explicit provider-preference policy should construct");
    const auto providerOrder = mustResolution(
        SourceSelector::order(
            canonical,
            {equalA, equalB},
            preferredPolicy,
            scorer,
            memory),
        "provider preference should order equal-score candidates");
    require(
        providerOrder.first().candidateKey().providerId()
            == equalB.candidateKey().providerId(),
        "explicit provider preference must be an observable policy input");
}

void verifyProfileMemory(
    const vinyl::RecordingMbid &canonical,
    const vinyl::SelectableSourceCandidate &high,
    const vinyl::SelectableSourceCandidate &low)
{
    using namespace vinyl;
    ReferenceSourceKindScorer scorer;
    SourceProfileMemory profileA(profile("profile-A"));
    SourceProfileMemory profileB(profile("profile-B"));

    require(profileA.markKnownWrong(
                canonical,
                high.candidateKey(),
                SourceMemoryOrigin::User).ok,
            "known-wrong memory should be recordable");

    const auto aOrder = mustResolution(
        SourceSelector::order(
            canonical,
            {high, low},
            SourceSelectionPolicy::vinylReference(),
            scorer,
            profileA),
        "profile A selection should succeed");
    const auto bOrder = mustResolution(
        SourceSelector::order(
            canonical,
            {high, low},
            SourceSelectionPolicy::vinylReference(),
            scorer,
            profileB),
        "profile B selection should succeed");

    require(aOrder.size() == 1
                && aOrder.first().candidateKey() == low.candidateKey(),
            "known-wrong candidate must be excluded deterministically");
    require(bOrder.size() == 2
                && bOrder.first().candidateKey() == high.candidateKey(),
            "profile A source memory must not affect profile B");

    SourceProfileMemory deadMemory(profile("profile-dead"));
    require(deadMemory.markKnownDead(
                canonical,
                low.candidateKey(),
                SourceMemoryOrigin::ConfirmedRuntime).ok,
            "confirmed known-dead memory should be recordable");
    const auto deadOrder = mustResolution(
        SourceSelector::order(
            canonical,
            {high, low},
            SourceSelectionPolicy::vinylReference(),
            scorer,
            deadMemory),
        "known-dead source ordering should succeed");
    require(deadOrder.size() == 1
                && deadOrder.first().candidateKey() == high.candidateKey(),
            "known-dead candidate must be excluded deterministically");

    SourceProfileMemory pinned(profile("profile-pinned"));
    require(pinned.setUserPin(canonical, low.candidateKey()).ok,
            "explicit user source pin should be recordable");
    const auto pinnedOrder = mustResolution(
        SourceSelector::order(
            canonical,
            {high, low},
            SourceSelectionPolicy::vinylReference(),
            scorer,
            pinned),
        "pinned source ordering should succeed");
    require(pinnedOrder.first().candidateKey() == low.candidateKey(),
            "explicit user pin should outrank default quality within allowed candidates");

    require(pinned.markKnownDead(
                canonical,
                high.candidateKey(),
                SourceMemoryOrigin::ConfirmedRuntime).ok,
            "confirmed known-dead state should be recordable separately from transient failure");
    const QString serialized = pinned.toCanonicalTextV1();
    const auto restored = SourceProfileMemory::fromCanonicalTextV1(pinned.profileId(), serialized);
    require(restored.ok(),
            "versioned deterministic source-memory representation should restore");
    require(restored.value->toCanonicalTextV1() == serialized,
            "source-memory v1 serialization should be deterministic and canonical");
    const auto foreign = SourceProfileMemory::fromCanonicalTextV1(profile("different-profile"), serialized);
    require(!foreign.ok() && foreign.error.code == SourceResolutionErrorCode::ProfileMismatch,
            "restore must reject a valid document owned by another expected profile");
    require(restored.value->profileId() == pinned.profileId(),
            "source-memory restore must preserve exact profile scope");
}

void verifyNormalAndFallback(
    const vinyl::RecordingMbid &canonical,
    const vinyl::SelectableSourceCandidate &high,
    const vinyl::SelectableSourceCandidate &low)
{
    using namespace vinyl;

    HarnessContext ctx(profile("normal-profile"));
    const QueueOccurrence q1 = occurrence("q-normal", canonical);
    appendAndActivate(ctx, {q1}, q1.occurrenceId());
    armPending(ctx);

    const PlayInstanceId logicalPlay = playInstance("play-normal");
    const SourceResolutionRequest first = mustResolution(
        ctx.sources.beginForActive(
            {low, high},
            true,
            SourceResolutionReason::Initial,
            logicalPlay),
        "normal async resolution should issue a request");

    require(first.candidateKey() == high.candidateKey(),
            "normal chain should start with deterministic best candidate");
    require(first.occurrenceId() == q1.occurrenceId(),
            "resolution request must fence exact QueueOccurrenceId");

    const auto fallback = ctx.sources.resolutionFailed(first);
    require(fallback.action == SourceResolutionAction::NeedResolution
                && fallback.nextRequest.has_value(),
            "resolve failure should advance to next allowed candidate");
    const SourceResolutionRequest second = *fallback.nextRequest;
    require(second.candidateKey() == low.candidateKey(),
            "resolve failure fallback should use deterministic next candidate");
    require(second.requestSequence() > first.requestSequence(),
            "fallback must issue a fresh monotonic request identity");
    require(!ctx.memory.dispositionFor(
                canonical,
                first.candidateKey()).has_value(),
            "transient resolution failure must not become durable wrong/dead memory");

    const auto lateSuccess = ctx.sources.acceptResolved(
        first,
        resolved(
            first,
            QStringLiteral("https://example.invalid/late-first.flac"),
            1000,
            5000),
        1500);
    require(lateSuccess.action == SourceResolutionAction::IgnoredStale,
            "late success from an older resolver request must be rejected");
    require(ctx.sources.pendingRequest().has_value()
                && *ctx.sources.pendingRequest() == second,
            "late success must not disturb newer fallback request");
    requireInvariant(ctx, "late stale success must preserve BA1/BA2 invariant");

    const auto lateFailure = ctx.sources.resolutionFailed(first);
    require(lateFailure.action == SourceResolutionAction::IgnoredStale,
            "late failure from an older resolver request must be rejected");
    require(ctx.sources.pendingRequest().has_value()
                && *ctx.sources.pendingRequest() == second,
            "late failure must not cancel newer fallback request");

    const auto success = ctx.sources.acceptResolved(
        second,
        resolved(
            second,
            QStringLiteral("https://example.invalid/fallback.flac"),
            2000,
            8000),
        2500);
    require(success.action == SourceResolutionAction::PlaybackRestarted
                && success.replacement.has_value(),
            "fallback source should restart playback through coordinator");
    require(success.replacement->path
                == SourceReplacementPath::CoordinatorRestartActive,
            "same-occurrence fallback must use W4-B restartActive path");
    require(success.replacement->occurrenceId == q1.occurrenceId(),
            "same-occurrence fallback must preserve QueueOccurrenceId");
    require(success.replacement->recordingMbid == canonical,
            "fallback must preserve canonical RecordingMbid");
    require(success.replacement->continuationOf.has_value()
                && *success.replacement->continuationOf == logicalPlay,
            "fallback must expose W4-E continuation identity rather than fabricate a replay");
    require(ctx.engine.loads.size() == 1,
            "accepted fallback should emit exactly one engine load");
    requireInvariant(ctx, "accepted fallback must preserve BA1/BA2 invariant");
}

void verifyPlaybackFailureFallback(
    const vinyl::RecordingMbid &canonical,
    const vinyl::SelectableSourceCandidate &high,
    const vinyl::SelectableSourceCandidate &low)
{
    using namespace vinyl;
    HarnessContext ctx(profile("playback-failure-profile"));
    const QueueOccurrence q1 = occurrence("q-playback-failure", canonical);
    appendAndActivate(ctx, {q1}, q1.occurrenceId());
    armPending(ctx);

    const PlayInstanceId journey = playInstance("play-before-failure");
    const SourceResolutionRequest first = mustResolution(
        ctx.sources.beginForActive(
            {low, high},
            true,
            SourceResolutionReason::Initial,
            journey),
        "playback-failure chain should begin");

    const auto firstAccepted = ctx.sources.acceptResolved(
        first,
        resolved(
            first,
            QStringLiteral("https://example.invalid/first.flac"),
            1000,
            9000),
        1500);
    require(firstAccepted.action
                == SourceResolutionAction::PlaybackRestarted,
            "first source should start before playback-failure fallback");
    const PlaybackWorkToken firstToken = activeToken(ctx);
    require(ctx.coordinator.engineLoaded(firstToken).ok,
            "first source should load before simulated playback failure");
    requireInvariant(ctx, "load completion must preserve invariant");

    const auto failureStatus = ctx.coordinator.engineFailed(
        firstToken,
        {QStringLiteral("decoder_failed"),
         QStringLiteral("fixture playback failure")});
    require(failureStatus.ok,
            "current playback failure should reach coordinator honestly");
    requireInvariant(ctx, "playback failure must preserve invariant");

    const auto fallback =
        ctx.sources.playbackFailed(firstToken);
    require(fallback.action == SourceResolutionAction::NeedResolution
                && fallback.nextRequest.has_value(),
            "selected-source playback failure should trigger ordered fallback");
    require(ctx.coordinator.pendingInput().has_value()
                && ctx.coordinator.pendingInput()->occurrenceId
                    == q1.occurrenceId(),
            "async playback-failure fallback must re-arm W4-B AwaitingInput intent");
    require(fallback.nextRequest->candidateKey() == low.candidateKey(),
            "playback failure must not resurrect already-attempted source");

    const auto duplicateFailure = ctx.sources.playbackFailed(firstToken);
    require(duplicateFailure.action == SourceResolutionAction::IgnoredStale
                && ctx.sources.pendingRequest() == fallback.nextRequest,
            "duplicate current failure cannot exhaust an already pending fallback");
    const auto restarted = ctx.sources.acceptResolved(
        *fallback.nextRequest,
        resolved(
            *fallback.nextRequest,
            QStringLiteral("https://example.invalid/second.flac"),
            2000,
            10000),
        2500);
    require(restarted.action
                == SourceResolutionAction::PlaybackRestarted,
            "next allowed source should recover same occurrence");
    require(activeToken(ctx).occurrenceId() == q1.occurrenceId(),
            "playback-failure fallback must preserve occurrence identity");
    require(activeToken(ctx).generation() > firstToken.generation(),
            "fallback restart must mint a fresh BA2 generation");
    require(restarted.replacement->continuationOf.has_value()
                && *restarted.replacement->continuationOf == journey,
            "playback-failure fallback must preserve continuation link");
    requireInvariant(ctx, "playback fallback restart must preserve invariant");
}

void verifyReplacedPlaybackFailureFence(const vinyl::RecordingMbid &canonical,
                                       const vinyl::SelectableSourceCandidate &high,
                                       const vinyl::SelectableSourceCandidate &low) {
    using namespace vinyl;
    HarnessContext ctx(profile("failure-fence-owner"));
    const auto q = occurrence("failure-fence-occurrence", canonical);
    appendAndActivate(ctx, {q}, q.occurrenceId()); armPending(ctx);
    const auto first = mustResolution(ctx.sources.beginForActive({high}, true), "first transport begins");
    require(ctx.sources.acceptResolved(first, resolved(first, "https://example.invalid/a.flac", 1, 9000), 2).action
                == SourceResolutionAction::PlaybackRestarted, "first transport starts");
    const auto oldToken = activeToken(ctx);
    const auto next = mustResolution(ctx.sources.beginForActive({low}, true, SourceResolutionReason::ExplicitSwitch),
                                     "same occurrence replacement begins");
    require(ctx.sources.acceptResolved(next, resolved(next, "https://example.invalid/b.flac", 1, 9000), 2).action
                == SourceResolutionAction::PlaybackRestarted, "replacement transport starts");
    const auto currentToken = activeToken(ctx);
    require(ctx.coordinator.engineFailed(currentToken, {QStringLiteral("current-failed"), QStringLiteral("fixture")}).ok,
            "replacement failure is accepted by BA2 before delivery to source session");
    const auto late = ctx.sources.playbackFailed(oldToken);
    require(late.action == SourceResolutionAction::IgnoredStale,
            "failure from replaced playback must not consume the current source chain");
    require(activeToken(ctx) == currentToken, "stale failure preserves the replacement playback token");
}

void verifyProfileSessionBinding(const vinyl::RecordingMbid &canonical,
                                const vinyl::SelectableSourceCandidate &high) {
    using namespace vinyl;
    HarnessContext ctx(profile("session-owner"));
    const QueueOccurrence q = occurrence("profile-bound-occurrence", canonical);
    appendAndActivate(ctx, {q}, q.occurrenceId());
    armPending(ctx);
    ctx.memory = SourceProfileMemory(profile("foreign-memory"));
    const auto result = ctx.sources.beginForActive({high}, true);
    require(!result.ok() && sourceResolutionErrorCodeName(result.error.code)
                == QStringLiteral("profile_mismatch"),
            "source session must reject memory reassigned from another profile");
    require(ctx.engine.loads.isEmpty(), "foreign profile cannot start playback");
    HarnessContext during(profile("resolver-owner"));
    appendAndActivate(during, {q}, q.occurrenceId()); armPending(during);
    const auto request = mustResolution(during.sources.beginForActive({high}, true), "valid profile begins");
    during.memory = SourceProfileMemory(profile("reassigned-memory"));
    const auto reply = during.sources.acceptResolved(request,
        resolved(request, QStringLiteral("https://example.invalid/profile.flac"), 1, 9000), 2);
    require(reply.action == SourceResolutionAction::Failed
                && reply.error.code == SourceResolutionErrorCode::ProfileMismatch,
            "resolver completion cannot cross a source memory profile change");
    require(during.engine.loads.isEmpty(), "profile change fences the engine handoff");
}

void verifyExpiry(
    const vinyl::RecordingMbid &canonical,
    const vinyl::SelectableSourceCandidate &high,
    const vinyl::SelectableSourceCandidate &low)
{
    using namespace vinyl;
    HarnessContext ctx(profile("expiry-profile"));
    const QueueOccurrence q1 = occurrence("q-expiry", canonical);
    appendAndActivate(ctx, {q1}, q1.occurrenceId());
    armPending(ctx);

    const SourceResolutionRequest first = mustResolution(
        ctx.sources.beginForActive({high, low}, true),
        "expiry chain should begin");
    require(ctx.sources.acceptResolved(
                first,
                resolved(
                    first,
                    QStringLiteral("https://example.invalid/expiring.flac"),
                    1000,
                    2000),
                1500).action
                == SourceResolutionAction::PlaybackRestarted,
            "fresh expiring transport should start");

    const auto refresh = ctx.sources.refreshIfExpired(2000);
    require(refresh.action == SourceResolutionAction::NeedResolution
                && refresh.nextRequest.has_value(),
            "expiry boundary should trigger fresh re-resolution");
    require(refresh.nextRequest->candidateKey() == first.candidateKey(),
            "expiry refresh should retain candidate identity for first refresh");
    require(refresh.nextRequest->occurrenceId() == first.occurrenceId(),
            "expiry refresh must retain QueueOccurrenceId");
    require(refresh.nextRequest->requestSequence()
                > first.requestSequence(),
            "expiry refresh must issue a fresh request identity");
    require(refresh.nextRequest->reason()
                == SourceResolutionReason::ExpiryRefresh,
            "expiry re-resolution should be explicitly typed");

    const auto duplicateRefresh = ctx.sources.refreshIfExpired(2050);
    require(duplicateRefresh.action == SourceResolutionAction::NoChange,
            "in-flight expiry refresh must be coalesced");
    require(ctx.sources.pendingRequest() == refresh.nextRequest,
            "repeated expiry polling must preserve the pending resolver fence");

    const auto fresh = ctx.sources.acceptResolved(
        *refresh.nextRequest,
        resolved(
            *refresh.nextRequest,
            QStringLiteral("https://example.invalid/refreshed.flac"),
            2100,
            6000),
        2200);
    require(fresh.action == SourceResolutionAction::PlaybackRestarted,
            "fresh expiry re-resolution should replace via coordinator");
    requireInvariant(ctx, "expiry restart must preserve invariant");

    const auto noRefresh = ctx.sources.refreshIfExpired(3000);
    require(noRefresh.action == SourceResolutionAction::NoChange,
            "known unexpired transport should not re-resolve");
}

void verifyBoundedExpiryRetry(
    const vinyl::RecordingMbid &canonical,
    const vinyl::SelectableSourceCandidate &high,
    const vinyl::SelectableSourceCandidate &low)
{
    using namespace vinyl;
    HarnessContext ctx(profile("expiry-loop-profile"));
    const QueueOccurrence q1 = occurrence("q-expiry-loop", canonical);
    appendAndActivate(ctx, {q1}, q1.occurrenceId());
    armPending(ctx);

    const SourceResolutionRequest first = mustResolution(
        ctx.sources.beginForActive({high, low}, true),
        "bounded-expiry chain should begin");

    const auto expiredOnce = ctx.sources.acceptResolved(
        first,
        resolved(
            first,
            QStringLiteral("https://example.invalid/already-expired.flac"),
            1000,
            1500),
        1500);
    require(expiredOnce.action == SourceResolutionAction::NeedResolution
                && expiredOnce.nextRequest.has_value(),
            "first immediately expired result may re-resolve same candidate once");
    require(expiredOnce.nextRequest->candidateKey() == high.candidateKey(),
            "first immediate expiry refresh should retain candidate");

    const auto expiredTwice = ctx.sources.acceptResolved(
        *expiredOnce.nextRequest,
        resolved(
            *expiredOnce.nextRequest,
            QStringLiteral("https://example.invalid/still-expired.flac"),
            1600,
            1700),
        1700);
    require(expiredTwice.action == SourceResolutionAction::NeedResolution
                && expiredTwice.nextRequest.has_value(),
            "second immediately expired result should leave retry loop");
    require(expiredTwice.nextRequest->candidateKey() == low.candidateKey(),
            "bounded expiry logic must fall through to next unattempted candidate");
}

void verifyTerminalExhaustion(
    const vinyl::RecordingMbid &canonical,
    const vinyl::SelectableSourceCandidate &only)
{
    using namespace vinyl;
    HarnessContext ctx(profile("terminal-profile"));
    const QueueOccurrence q1 = occurrence("q-terminal", canonical);
    appendAndActivate(ctx, {q1}, q1.occurrenceId());
    armPending(ctx);

    const SourceResolutionRequest request = mustResolution(
        ctx.sources.beginForActive({only}, true),
        "single-candidate chain should begin");
    const auto terminal = ctx.sources.resolutionFailed(request);
    require(terminal.action == SourceResolutionAction::Terminal
                && terminal.terminal.has_value(),
            "fallback exhaustion must produce a typed terminal result");
    require(terminal.terminal->reason
                == SourceFallbackTerminalReason::ExhaustedCandidates,
            "terminal fallback result should state exhaustion");
    require(terminal.terminal->attemptedCandidateKeys.size() == 1,
            "exhaustion should expose finite ordered attempt evidence");
    require(!terminal.nextRequest.has_value(),
            "exhaustion must have an explicit stop condition and no retry request");

    const auto lateAgain = ctx.sources.resolutionFailed(request);
    require(lateAgain.action == SourceResolutionAction::IgnoredStale,
            "terminal chain must not restart from a repeated old failure");
    require(!lateAgain.nextRequest.has_value(),
            "no retry loop may become unbounded after terminal exhaustion");
}

void verifyAudioVideoContinuation(
    const vinyl::RecordingMbid &canonical,
    const vinyl::SelectableSourceCandidate &audio,
    const vinyl::SelectableSourceCandidate &audio2,
    const vinyl::SelectableSourceCandidate &video)
{
    using namespace vinyl;
    HarnessContext ctx(profile("av-profile"));
    const QueueOccurrence q1 = occurrence("q-av", canonical);
    appendAndActivate(ctx, {q1}, q1.occurrenceId());
    armPending(ctx);

    const SourceResolutionRequest audioRequest = mustResolution(
        ctx.sources.beginForActive(
            {audio},
            true,
            SourceResolutionReason::Initial,
            playInstance("journey-audio")),
        "audio journey should begin");
    require(ctx.sources.acceptResolved(
                audioRequest,
                resolved(
                    audioRequest,
                    QStringLiteral("https://example.invalid/audio.flac"),
                    1000,
                    10000,
                    0),
                1500).action
                == SourceResolutionAction::PlaybackRestarted,
            "audio source should start");

    const SourceResolutionRequest videoRequest = mustResolution(
        ctx.sources.beginForActive(
            {video},
            true,
            SourceResolutionReason::ExplicitSwitch,
            playInstance("journey-audio")),
        "Audio to Video should use the same BB5 request lifecycle");
    require(videoRequest.transportKind() == SourceTransportKind::Video,
            "video switch request should retain explicit transport kind");
    const auto videoResult = ctx.sources.acceptResolved(
        videoRequest,
        resolved(
            videoRequest,
            QStringLiteral("https://example.invalid/video.mp4"),
            2000,
            12000,
            -5000),
        2500);
    require(videoResult.action == SourceResolutionAction::PlaybackRestarted,
            "Audio to Video should replace through restartActive");
    require(videoResult.replacement->recordingMbid == canonical,
            "Audio to Video must preserve canonical RecordingMbid");
    require(videoResult.replacement->occurrenceId == q1.occurrenceId(),
            "Audio to Video must preserve QueueOccurrenceId");
    require(videoResult.replacement->transportKind
                == SourceTransportKind::Video,
            "replacement facts should expose video transport");
    require(videoResult.replacement->continuationOf.has_value()
                && videoResult.replacement->continuationOf->value()
                    == QStringLiteral("journey-audio"),
            "Audio to Video should expose BA4 continuation lineage");

    const SourceResolutionRequest backToAudio = mustResolution(
        ctx.sources.beginForActive(
            {audio2},
            true,
            SourceResolutionReason::ExplicitSwitch,
            playInstance("journey-video")),
        "Video to Audio should reuse BB5 lifecycle");
    const auto audioResult = ctx.sources.acceptResolved(
        backToAudio,
        resolved(
            backToAudio,
            QStringLiteral("https://example.invalid/audio-2.flac"),
            3000,
            13000,
            0),
        3500);
    require(audioResult.action == SourceResolutionAction::PlaybackRestarted,
            "Video to Audio should replace through restartActive");
    require(audioResult.replacement->recordingMbid == canonical,
            "Video to Audio must preserve canonical RecordingMbid");
    require(audioResult.replacement->occurrenceId == q1.occurrenceId(),
            "Video to Audio must preserve QueueOccurrenceId");
    requireInvariant(ctx, "Audio/Video replacements must preserve invariant");
}

void verifyRaces(
    const vinyl::RecordingMbid &canonical,
    const vinyl::SelectableSourceCandidate &high,
    const vinyl::SelectableSourceCandidate &low,
    const vinyl::SelectableSourceCandidate &video)
{
    using namespace vinyl;

    {
        HarnessContext ctx(profile("race-skip-audio"));
        const QueueOccurrence q1 = occurrence("race-a-q1", canonical);
        const QueueOccurrence q2 = occurrence("race-a-q2", canonical);
        appendAndActivate(ctx, {q1, q2}, q1.occurrenceId());
        armPending(ctx);
        const SourceResolutionRequest request = mustResolution(
            ctx.sources.beginForActive({high}, true),
            "audio race request should begin");
        require(ctx.coordinator.skip().ok,
                "skip during audio resolve should succeed");
        requireInvariant(ctx, "skip callback must preserve invariant");
        const auto late = ctx.sources.acceptResolved(
            request,
            resolved(
                request,
                QStringLiteral("https://example.invalid/late-audio.flac"),
                1000,
                5000),
            1500);
        require(late.action == SourceResolutionAction::IgnoredStale,
                "skip during audio resolve must invalidate old result");
        requireInvariant(ctx, "late audio result after skip must preserve invariant");
    }

    {
        HarnessContext ctx(profile("race-skip-video"));
        const QueueOccurrence q1 = occurrence("race-v-q1", canonical);
        const QueueOccurrence q2 = occurrence("race-v-q2", canonical);
        appendAndActivate(ctx, {q1, q2}, q1.occurrenceId());
        armPending(ctx);
        const SourceResolutionRequest request = mustResolution(
            ctx.sources.beginForActive({video}, true),
            "video race request should begin");
        require(ctx.coordinator.skip().ok,
                "skip during video resolve should succeed");
        requireInvariant(ctx, "video skip callback must preserve invariant");
        const auto late = ctx.sources.acceptResolved(
            request,
            resolved(
                request,
                QStringLiteral("https://example.invalid/late-video.mp4"),
                1000,
                5000),
            1500);
        require(late.action == SourceResolutionAction::IgnoredStale,
                "skip during video resolve must invalidate old result");
        requireInvariant(ctx, "late video result after skip must preserve invariant");
    }

    {
        HarnessContext ctx(profile("race-play-now"));
        const QueueOccurrence q1 = occurrence("race-p-q1", canonical);
        appendAndActivate(ctx, {q1}, q1.occurrenceId());
        armPending(ctx);
        const SourceResolutionRequest first = mustResolution(
            ctx.sources.beginForActive({high, low}, true),
            "fallback race should begin");
        const auto fallback = ctx.sources.resolutionFailed(first);
        require(fallback.action == SourceResolutionAction::NeedResolution,
                "fallback race should reach second request");
        const QueueOccurrence qNow = occurrence("race-p-now", canonical);
        require(ctx.coordinator.playNow(qNow).ok,
                "Play Now during fallback should replace active occurrence");
        requireInvariant(ctx, "Play Now callback must preserve invariant");
        const auto late = ctx.sources.acceptResolved(
            *fallback.nextRequest,
            resolved(
                *fallback.nextRequest,
                QStringLiteral("https://example.invalid/late-fallback.flac"),
                1000,
                5000),
            1500);
        require(late.action == SourceResolutionAction::IgnoredStale,
                "Play Now during fallback must reject old source result");
        require(ctx.queue.activeOccurrenceId().has_value()
                    && *ctx.queue.activeOccurrenceId() == qNow.occurrenceId(),
                "old fallback result must never retarget new Play Now occurrence");
        requireInvariant(ctx, "late fallback after Play Now must preserve invariant");
    }

    {
        HarnessContext ctx(profile("race-remove"));
        const QueueOccurrence q1 = occurrence("race-r-q1", canonical);
        const QueueOccurrence q2 = occurrence("race-r-q2", canonical);
        appendAndActivate(ctx, {q1, q2}, q1.occurrenceId());
        armPending(ctx);
        const SourceResolutionRequest request = mustResolution(
            ctx.sources.beginForActive({high}, true),
            "remove-active race should begin");
        require(ctx.coordinator.remove(q1.occurrenceId()).ok,
                "remove active during resolve should succeed");
        requireInvariant(ctx, "remove-active callback must preserve invariant");
        const auto late = ctx.sources.resolutionFailed(request);
        require(late.action == SourceResolutionAction::IgnoredStale,
                "remove-active during resolve must invalidate old failure");
        require(ctx.queue.activeOccurrenceId().has_value()
                    && *ctx.queue.activeOccurrenceId() == q2.occurrenceId(),
                "remove-active should leave BA1-selected successor active");
        requireInvariant(ctx, "late failure after removal must preserve invariant");
    }

    {
        HarnessContext ctx(profile("race-stop"));
        const QueueOccurrence q1 = occurrence("race-s-q1", canonical);
        appendAndActivate(ctx, {q1}, q1.occurrenceId());
        armPending(ctx);
        const SourceResolutionRequest request = mustResolution(
            ctx.sources.beginForActive({high, low}, true),
            "stop race should begin");
        require(ctx.coordinator.stop().ok,
                "stop during resolve should succeed");
        requireInvariant(ctx, "stop callback must preserve invariant");
        const auto late = ctx.sources.resolutionFailed(request);
        require(late.action == SourceResolutionAction::IgnoredStale,
                "stop during resolve must reject old failure and not fallback");
        require(!late.nextRequest.has_value(),
                "stop must prevent late resolver failure from launching fallback");
        require(!ctx.playback.activeOccurrence().has_value(),
                "stop during resolve must leave BA2 unbound");
        requireInvariant(ctx, "late failure after stop must preserve invariant");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "W4-C native harness must execute on Qt 6.11.1 exactly");

    const RecordingMbid canonical =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid other =
        recording("22222222-2222-4222-8222-222222222222");

    const SelectableSourceCandidate high =
        exactAudio(canonical, "provider.high", "high", 1411, true);
    const SelectableSourceCandidate low =
        exactAudio(canonical, "provider.low", "low", 320, false);
    const SelectableSourceCandidate audio2 =
        exactAudio(canonical, "provider.audio2", "audio2", 256, false);
    const SelectableSourceCandidate video =
        selectableVideo(canonical, "youtube.music", "video-1");

    verifyExactGate(canonical, other);
    verifyDeterministicSelection(canonical, high, low);
    if (QCoreApplication::arguments().contains("--profile-binding")) {
        verifyProfileSessionBinding(canonical, high); return 0;
    }
    if (QCoreApplication::arguments().contains("--expiry-coalescing")) {
        verifyExpiry(canonical, high, low); return 0;
    }
    if (QCoreApplication::arguments().contains("--failure-fence")) {
        verifyReplacedPlaybackFailureFence(canonical, high, low); return 0;
    }
    verifyProfileMemory(canonical, high, low);
    verifyNormalAndFallback(canonical, high, low);
    verifyPlaybackFailureFallback(canonical, high, low);
    verifyReplacedPlaybackFailureFence(canonical, high, low);
    verifyProfileSessionBinding(canonical, high);
    verifyExpiry(canonical, high, low);
    verifyBoundedExpiryRetry(canonical, high, low);
    verifyTerminalExhaustion(canonical, high);
    verifyAudioVideoContinuation(canonical, high, audio2, video);
    verifyRaces(canonical, high, low, video);

    require(sourceResolutionErrorCodeName(
                SourceResolutionErrorCode::FallbackExhausted)
                == QStringLiteral("fallback_exhausted"),
            "W4-C errors should expose stable provider-neutral names");
    require(sourceReplacementPathName(
                SourceReplacementPath::CoordinatorRestartActive)
                == QStringLiteral("coordinator_restart_active"),
            "replacement path should make the coordinator seam inspectable");

    qInfo().noquote()
        << "VINYL_BB5_SOURCE_RESOLUTION_SESSION_OK checks=" << g_checks;
    return 0;
}
