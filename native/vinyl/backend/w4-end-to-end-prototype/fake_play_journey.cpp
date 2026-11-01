#include "SourceResolutionSession.h"
#include "VinylListenEventLedger.h"
#include "VinylSessionPersistence.h"
#include "W4ComposedIntegration.h"

#include <QCoreApplication>
#include <QDebug>
#include <QVector>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <variant>

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
T mustCandidate(const vinyl::SourceCandidateResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustExact(const vinyl::ExactTrackContractResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustResolution(const vinyl::SourceResolutionResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustMpv(const vinyl::MpvPlaybackAdapterResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPersist(const vinyl::SessionPersistenceResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustListen(const vinyl::ListenLedgerResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
                      "RecordingMbid fixture should parse");
}

vinyl::ProfileScopeId profile(const char *value)
{
    return mustDomain(vinyl::ProfileScopeId::parse(QString::fromLatin1(value)),
                      "ProfileScopeId fixture should parse");
}

vinyl::PlayInstanceId playInstance(const char *value)
{
    return mustDomain(vinyl::PlayInstanceId::parse(QString::fromLatin1(value)),
                      "PlayInstanceId fixture should parse");
}

vinyl::QueueOccurrenceId occurrenceId(const char *value)
{
    return mustQueue(vinyl::QueueOccurrenceId::parse(QString::fromLatin1(value)),
                     "QueueOccurrenceId fixture should parse");
}

vinyl::EnqueueBatchId batchId(const char *value)
{
    return mustQueue(vinyl::EnqueueBatchId::parse(QString::fromLatin1(value)),
                     "EnqueueBatchId fixture should parse");
}

vinyl::ListenSessionId listenSession(const char *value)
{
    return mustListen(vinyl::ListenSessionId::parse(QString::fromLatin1(value)),
                      "ListenSessionId fixture should parse");
}

vinyl::ListenSourceTraceId sourceTrace(const char *value)
{
    return mustListen(vinyl::ListenSourceTraceId::parse(QString::fromLatin1(value)),
                      "ListenSourceTraceId fixture should parse");
}

vinyl::QueueOccurrence occurrence(const char *id,
                                  const vinyl::RecordingMbid &recordingMbid,
                                  vinyl::EnqueueOriginKind originKind)
{
    using namespace vinyl;
    EnqueueOrigin origin(
        originKind,
        batchId(originKind == EnqueueOriginKind::Radio
                    ? "passive-radio-batch"
                    : "album-batch"));
    return mustQueue(
        QueueOccurrence::create(
            occurrenceId(id),
            recordingMbid,
            std::nullopt,
            origin),
        "QueueOccurrence fixture should construct");
}

vinyl::SourceProviderId sourceProvider(const char *value)
{
    return mustCandidate(
        vinyl::SourceProviderId::parse(QString::fromLatin1(value)),
        "source provider fixture should parse");
}

vinyl::SourceCandidateId sourceCandidateId(const char *value)
{
    return mustCandidate(
        vinyl::SourceCandidateId::parse(QString::fromLatin1(value)),
        "source candidate fixture should parse");
}

vinyl::SourceProviderDescriptor sourceDescriptor(
    const char *provider,
    vinyl::SourceKind kind)
{
    using namespace vinyl;
    SourceProviderCapabilities capabilities =
        SourceProviderCapability::DiscoverCandidates;
    capabilities |= SourceProviderCapability::ResolveExactSource;
    if (kind == SourceKind::Http)
        capabilities |= SourceProviderCapability::HttpSources;
    else if (kind == SourceKind::LocalFile)
        capabilities |= SourceProviderCapability::LocalFileSources;
    else if (kind == SourceKind::PeerToPeer)
        capabilities |= SourceProviderCapability::PeerToPeerSources;
    else if (kind == SourceKind::ExternalResolver)
        capabilities |= SourceProviderCapability::ExternalResolverSources;
    else if (kind == SourceKind::ProviderMediated)
        capabilities |= SourceProviderCapability::ProviderMediatedSources;
    return mustCandidate(
        SourceProviderDescriptor::create(
            sourceProvider(provider),
            capabilities),
        "source provider descriptor should construct");
}

vinyl::SelectableSourceCandidate exactAudio(
    const vinyl::RecordingMbid &recordingMbid,
    const char *provider,
    const char *item,
    int bitrate,
    bool lossless)
{
    using namespace vinyl;
    SourceCandidateFacts facts;
    facts.bitrateKbps = bitrate;
    facts.sampleRateHz = lossless ? 96000 : 48000;
    facts.bitDepth = lossless ? 24 : 16;
    facts.lossless = lossless;
    facts.title = QStringLiteral("W4-H fixture");

    const NormalizedSourceCandidate candidate = mustCandidate(
        NormalizedSourceCandidate::create(
            recordingMbid,
            sourceDescriptor(provider, SourceKind::Http),
            sourceCandidateId(item),
            SourceDiscoveryMethod::CanonicalMapping,
            SourceKind::Http,
            CandidateResolutionMode::Refreshable,
            std::move(facts)),
        "normalized audio candidate should construct");

    const RequestedTrackIdentity request = mustExact(
        RequestedTrackIdentity::create(recordingMbid, std::nullopt),
        "exact request should construct");

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
        "manifest entry should construct");
    const AlbumSourceManifest manifest = mustExact(
        AlbumSourceManifest::create(candidate.provenance().key(), {entry}),
        "manifest should construct");

    return mustResolution(
        SelectableSourceCandidate::fromExactAudio(candidate, request, manifest),
        "exact audio candidate should be selectable");
}

vinyl::ResolvedTrackSource resolved(
    const vinyl::SourceResolutionRequest &request,
    const QString &resource,
    qint64 resolvedAt,
    std::optional<qint64> expiresAt = std::nullopt)
{
    using namespace vinyl;
    return mustMpv(
        ResolvedTrackSource::create(
            request.recordingMbid(),
            request.candidateKey(),
            request.sourceKind(),
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::HttpUrl,
            resource,
            {},
            std::nullopt,
            0,
            resolvedAt,
            expiresAt),
        "resolved source should construct");
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
    struct Load final {
        vinyl::PlaybackWorkToken token;
        vinyl::PlaybackLoadInput input;
    };

    void load(const vinyl::PlaybackWorkToken &token,
              const vinyl::PlaybackLoadInput &input) override
    {
        loads.append({token, input});
    }

    void play(const vinyl::PlaybackWorkToken &token) override
    {
        plays.append(token);
    }

    void pause(const vinyl::PlaybackWorkToken &token) override
    {
        pauses.append(token);
    }

    void seek(const vinyl::PlaybackSeekRequest &request) override
    {
        seeks.append(request.workToken());
    }

    void stop(const vinyl::PlaybackWorkToken &token) override
    {
        stops.append(token);
    }

    QVector<Load> loads;
    QVector<vinyl::PlaybackWorkToken> plays;
    QVector<vinyl::PlaybackWorkToken> pauses;
    QVector<vinyl::PlaybackWorkToken> seeks;
    QVector<vinyl::PlaybackWorkToken> stops;
};

struct Harness final
{
    vinyl::VinylQueue queue;
    vinyl::PlaybackSession playback;
    FakeEngine engine;
    vinyl::PlaybackEngineAdapter adapter;
    EmptyInputProvider inputProvider;
    vinyl::QueueSessionCoordinator coordinator;
    vinyl::ReferenceSourceKindScorer scorer;
    vinyl::SourceProfileMemory sourceMemory;
    vinyl::SourceResolutionSession sources;
    vinyl::ListenEventLedger ledger;
    vinyl::ListenEventRecorder recorder;

    explicit Harness(vinyl::ProfileScopeId profileId)
        : adapter(playback, engine)
        , coordinator(queue, playback, adapter, inputProvider)
        , sourceMemory(profileId)
        , sources(
              queue,
              playback,
              coordinator,
              vinyl::SourceSelectionPolicy::vinylReference(),
              scorer,
              sourceMemory,
              profileId)
        , ledger(profileId)
        , recorder(ledger)
    {
    }
};

void requireInvariant(const Harness &h, const char *message)
{
    const auto active = h.queue.activeOccurrenceId();
    const auto &bound = h.playback.activeOccurrence();
    require(!bound.has_value()
                || (active.has_value()
                    && bound->occurrenceId() == *active),
            message);
}

vinyl::PlaybackWorkToken activeToken(const Harness &h)
{
    require(h.playback.activeWorkToken().has_value(),
            "active playback token should exist");
    return *h.playback.activeWorkToken();
}

vinyl::SessionCoordinatorIntent durableCoordinatorIntent(const Harness &h)
{
    using namespace vinyl;
    SessionCoordinatorIntent out;
    out.repeatMode =
        h.coordinator.repeatMode() == CoordinatorRepeatMode::Queue
        ? DurableRepeatMode::Queue
        : DurableRepeatMode::Off;
    if (h.coordinator.pendingInput().has_value()) {
        out.pendingInput = SessionPendingInputIntent{
            h.coordinator.pendingInput()->occurrenceId,
            h.coordinator.pendingInput()->playWhenReady};
    }
    return out;
}

QVector<QString> queueIds(const vinyl::VinylQueue &queue)
{
    QVector<QString> out;
    for (const auto &item : queue.items())
        out.append(item.occurrenceId().value());
    return out;
}

vinyl::RestoredSessionState checkpoint(
    const Harness &h,
    const vinyl::ProfileScopeId &profileId,
    const char *label)
{
    using namespace vinyl;
    const QByteArray bytes = mustPersist(
        VinylSessionPersistence::capture(
            profileId, h.queue, h.playback,
            durableCoordinatorIntent(h)),
        label);
    const QByteArray lower = bytes.toLower();
    require(!lower.contains("\"worktoken\""),
            "BA3 must not persist PlaybackWorkToken");
    require(!lower.contains("\"generation\""),
            "BA3 must not persist playback generation");
    require(!lower.contains("\"seeksequence\""),
            "BA3 must not persist seek sequence");
    require(!lower.contains("\"provider"),
            "BA3 must not persist provider identity");

    RestoredSessionState restored = mustPersist(
        VinylSessionPersistence::restore(profileId, bytes),
        "checkpoint should restore");
    require(queueIds(restored.queue) == queueIds(h.queue),
            "checkpoint must preserve QueueOccurrenceIds and order");
    require(restored.queue.activeOccurrenceId()
                == h.queue.activeOccurrenceId(),
            "checkpoint must preserve exact active occurrence");
    return restored;
}

bool hasEndCause(const vinyl::QueueSessionStatus &status,
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

bool hasAwaiting(const vinyl::QueueSessionStatus &status,
                 const vinyl::QueueOccurrenceId &id)
{
    for (const auto &fact : status.transitionFacts) {
        if (const auto *awaiting =
                std::get_if<vinyl::AwaitingInput>(&fact)) {
            if (awaiting->occurrenceId == id)
                return true;
        }
    }
    return false;
}

int countListenKind(const vinyl::ListenEventLedger &ledger,
                    vinyl::ListenEventKind kind)
{
    int count = 0;
    for (const auto &event : ledger.events()) {
        if (event.kind() == kind)
            ++count;
    }
    return count;
}

void verifyMainJourney()
{
    using namespace vinyl;

    const ProfileScopeId profileA = profile("w4-h-profile-a");
    const ProfileScopeId profileB = profile("w4-h-profile-b");
    const RecordingMbid songA =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid songB =
        recording("22222222-2222-4222-8222-222222222222");
    const RecordingMbid songC =
        recording("33333333-3333-4333-8333-333333333333");

    Harness h(profileA);
    h.coordinator.setRepeatMode(CoordinatorRepeatMode::Queue);

    const QueueOccurrence q1 =
        occurrence("album-occ-1", songA, EnqueueOriginKind::Album);
    const QueueOccurrence q2 =
        occurrence("album-occ-2", songB, EnqueueOriginKind::Album);
    const QueueOccurrence q3 =
        occurrence("album-occ-3", songC, EnqueueOriginKind::Album);
    const QueueOccurrence q4 =
        occurrence("album-occ-4", songC, EnqueueOriginKind::Album);
    require(h.queue.append(q1).ok && h.queue.append(q2).ok
                && h.queue.append(q3).ok && h.queue.append(q4).ok,
            "album occurrences should append");
    require(h.queue.activate(q1.occurrenceId()).ok,
            "first album occurrence should activate");

    const QueueSessionStatus started = h.coordinator.start(true);
    require(started.ok && hasAwaiting(started, q1.occurrenceId()),
            "no immediate source should enter AwaitingInput");
    requireInvariant(h, "AwaitingInput must preserve invariant");

    const RestoredSessionState checkpointA =
        checkpoint(h, profileA, "checkpoint A should capture");
    require(checkpointA.coordinatorIntent.pendingInput.has_value()
                && checkpointA.coordinatorIntent.pendingInput->occurrenceId
                    == q1.occurrenceId()
                && checkpointA.coordinatorIntent.pendingInput->playWhenReady,
            "checkpoint A must retain pending resolution intent");
    require(checkpointA.coordinatorIntent.repeatMode
                == DurableRepeatMode::Queue,
            "checkpoint A must retain repeat intent");
    require(!checkpointA.resumeIntent.has_value(),
            "awaiting checkpoint must not invent live resume work");

    const SelectableSourceCandidate aHigh =
        exactAudio(songA, "provider.a.high", "a-high", 1411, true);
    const SelectableSourceCandidate aLow =
        exactAudio(songA, "provider.a.low", "a-low", 320, false);
    const PlayInstanceId playA = playInstance("play-album-a");
    const SourceResolutionRequest aRequest = mustResolution(
        h.sources.beginForActive(
            {aLow, aHigh}, true,
            SourceResolutionReason::Initial, playA),
        "q1 async source resolution should begin");
    require(aRequest.candidateKey() == aHigh.candidateKey(),
            "selection should choose deterministic best exact source");

    const SourceResolutionOutcome aResolved = h.sources.acceptResolved(
        aRequest,
        resolved(aRequest,
                 QStringLiteral("https://example.invalid/a-high.flac"),
                 1000, 10000),
        1500);
    require(aResolved.action == SourceResolutionAction::PlaybackRestarted,
            "exact q1 source should start through coordinator");
    const PlaybackWorkToken tokenA1 = activeToken(h);
    require(tokenA1.occurrenceId() == q1.occurrenceId(),
            "q1 playback token should bind q1 only");
    require(h.coordinator.engineLoaded(tokenA1).ok,
            "q1 engine load should settle");
    require(h.coordinator.enginePositionObserved(tokenA1, 1.0).ok,
            "q1 position should observe");
    requireInvariant(h, "q1 load must preserve invariant");

    ListenContext contextA1 = mustListen(
        ListenContext::create(
            profileA, listenSession("listen-album"), q1,
            tokenA1, playA),
        "q1 BA4 context should construct");
    require(h.recorder.start(
                contextA1, 10000, 1000, 0, 1000,
                sourceTrace("source-a-high")).ok,
            "q1 BA4 start should record");
    require(h.recorder.sample(
                tokenA1, 11000, 2000, 1000, 1000).ok,
            "q1 qualified listening should record");

    const SelectableSourceCandidate aAlternate =
        exactAudio(songA, "provider.a.alt", "a-alt", 256, false);
    const SourceResolutionRequest switchRequest = mustResolution(
        h.sources.beginForActive(
            {aAlternate}, true,
            SourceResolutionReason::ExplicitSwitch, playA),
        "same-occurrence source switch should use BB5");
    const SourceResolutionOutcome switched = h.sources.acceptResolved(
        switchRequest,
        resolved(switchRequest,
                 QStringLiteral("https://example.invalid/a-alt.flac"),
                 2000, 10000),
        2500);
    require(switched.action == SourceResolutionAction::PlaybackRestarted
                && switched.replacement.has_value()
                && switched.replacement->occurrenceId == q1.occurrenceId()
                && switched.replacement->recordingMbid == songA
                && switched.replacement->continuationOf.has_value()
                && *switched.replacement->continuationOf == playA
                && hasEndCause(switched.replacement->coordinatorStatus,
                               OccurrenceEndCause::Replaced),
            "same-occurrence replacement must preserve identity and cause");

    const PlaybackWorkToken tokenA2 = activeToken(h);
    require(tokenA2.occurrenceId() == tokenA1.occurrenceId()
                && tokenA2.generation() > tokenA1.generation(),
            "source replacement should mint only fresh live generation");
    require(h.recorder.sourceChanged(
                tokenA1, sourceTrace("source-a-alt"),
                12000, 3000, 1500, 1000).ok,
            "BA4 should record factual source change");

    const PlayInstanceId playAContinuation =
        playInstance("play-album-a-source-restart");
    ListenContext contextA2 = mustListen(
        ListenContext::create(
            profileA, listenSession("listen-album"), q1,
            tokenA2, playAContinuation, playA),
        "replacement context should preserve continuation");
    require(h.recorder.start(
                contextA2, 12000, 3000, 1500, 1000,
                sourceTrace("source-a-alt")).ok,
            "replacement BA4 context should start");
    require(h.coordinator.engineLoaded(tokenA2).ok,
            "replacement engine load should settle");
    require(h.coordinator.enginePositionObserved(tokenA2, 2.5).ok,
            "replacement position should observe");
    require(h.recorder.sample(
                tokenA2, 13000, 4000, 2500, 1000).ok,
            "replacement listening should remain qualified");

    const RestoredSessionState checkpointB =
        checkpoint(h, profileA, "checkpoint B should capture active playback");
    require(checkpointB.resumeIntent.has_value()
                && checkpointB.resumeIntent->occurrenceId()
                    == q1.occurrenceId()
                && checkpointB.resumeIntent->playWhenReady(),
            "checkpoint B must retain active resume intent");

    PlaybackSession freshB;
    const auto freshResultB =
        freshB.beginOccurrence(*checkpointB.queue.activeOccurrence(), true);
    require(freshResultB.ok(),
            "checkpoint B should mint fresh post-restore token");
    const PlaybackWorkToken freshTokenB = *freshResultB.value;
    require(freshTokenB != tokenA2
                && freshTokenB.generation() == 1,
            "restore must not resurrect prior live generation");

    const QByteArray checkpointBBytes = mustPersist(
        VinylSessionPersistence::capture(
            profileA, h.queue, h.playback,
            durableCoordinatorIntent(h)),
        "profile isolation capture should succeed");
    const auto wrongProfile =
        VinylSessionPersistence::restore(profileB, checkpointBBytes);
    require(!wrongProfile.ok()
                && wrongProfile.error.code
                    == SessionPersistenceErrorCode::ProfileMismatch,
            "profile A state must not restore into profile B");

    require(h.recorder.complete(
                tokenA2, 14000, 5000, 3500, 1000).ok,
            "natural q1 completion should be factual");
    const QueueSessionStatus eofA = h.coordinator.engineEnded(tokenA2);
    require(eofA.ok && hasEndCause(eofA, OccurrenceEndCause::Eof),
            "natural EOF should emit typed Eof cause");
    require(h.queue.activeOccurrenceId().has_value()
                && *h.queue.activeOccurrenceId() == q2.occurrenceId()
                && h.coordinator.pendingInput().has_value()
                && h.coordinator.pendingInput()->occurrenceId
                    == q2.occurrenceId(),
            "EOF should advance to q2 and await q2 source");
    requireInvariant(h, "q1 EOF advance must preserve invariant");

    const SelectableSourceCandidate bHigh =
        exactAudio(songB, "provider.b.high", "b-high", 1411, true);
    const SelectableSourceCandidate bMiddle =
        exactAudio(songB, "provider.b.middle", "b-middle", 320, false);
    const SelectableSourceCandidate bLow =
        exactAudio(songB, "provider.b.low", "b-low", 192, false);
    const PlayInstanceId playB = playInstance("play-album-b");
    const SourceResolutionRequest bFirst = mustResolution(
        h.sources.beginForActive(
            {bLow, bMiddle, bHigh}, true,
            SourceResolutionReason::Initial, playB),
        "q2 resolution should begin");
    require(bFirst.candidateKey() == bHigh.candidateKey(),
            "q2 should try deterministic best candidate first");

    const SourceResolutionOutcome bFallback1 =
        h.sources.resolutionFailed(bFirst);
    require(bFallback1.action == SourceResolutionAction::NeedResolution
                && bFallback1.nextRequest.has_value()
                && bFallback1.nextRequest->candidateKey()
                    == bMiddle.candidateKey(),
            "q2 first resolution failure should choose ordered fallback");

    const SourceResolutionOutcome bStarted = h.sources.acceptResolved(
        *bFallback1.nextRequest,
        resolved(*bFallback1.nextRequest,
                 QStringLiteral("https://example.invalid/b-middle.flac"),
                 3000, 12000),
        3500);
    require(bStarted.action == SourceResolutionAction::PlaybackRestarted,
            "q2 fallback source should start playback");
    const PlaybackWorkToken tokenB = activeToken(h);
    require(tokenB.occurrenceId() == q2.occurrenceId(),
            "q2 playback token should bind q2");
    require(h.coordinator.engineLoaded(tokenB).ok,
            "q2 fallback load should settle");
    require(h.coordinator.enginePositionObserved(tokenB, 1.0).ok,
            "q2 position should observe");

    ListenContext contextB = mustListen(
        ListenContext::create(
            profileA, listenSession("listen-album"), q2,
            tokenB, playB),
        "q2 BA4 context should construct");
    require(h.recorder.start(
                contextB, 15000, 6000, 0, 1000,
                sourceTrace("source-b-middle")).ok
                && h.recorder.sample(
                    tokenB, 16000, 7000, 1000, 1000).ok,
            "q2 factual listening should record");

    require(h.queue.shuffle(Q_UINT64_C(0x62A8)).ok,
            "later journey should materialize shuffle state");
    const RestoredSessionState checkpointC =
        checkpoint(h, profileA, "checkpoint C should capture later journey");
    require(checkpointC.queue.activeOccurrenceId().has_value()
                && *checkpointC.queue.activeOccurrenceId()
                    == q2.occurrenceId()
                && checkpointC.queue.isShuffled()
                && checkpointC.queue.shuffleOriginalOrder().has_value(),
            "checkpoint C must retain q2 plus shuffle semantics");
    require(checkpointC.queue.activeOccurrence()->enqueueOrigin().has_value()
                && checkpointC.queue.activeOccurrence()->enqueueOrigin()->kind()
                    == EnqueueOriginKind::Album,
            "checkpoint C must retain album origin");

    PlaybackSession freshC;
    const auto freshResultC =
        freshC.beginOccurrence(*checkpointC.queue.activeOccurrence(), true);
    require(freshResultC.ok()
                && *freshResultC.value != tokenB
                && freshResultC.value->generation() == 1,
            "checkpoint C must mint fresh live identity after restore");

    const QueueSessionStatus playbackFailure = h.coordinator.engineFailed(
        tokenB,
        {QStringLiteral("transport_failed"),
         QStringLiteral("W4-H scripted playback failure")});
    require(playbackFailure.ok
                && hasEndCause(playbackFailure, OccurrenceEndCause::Failed),
            "transport failure should remain typed Failed");
    require(h.recorder.failure(
                tokenB, 16500, 7500, 1500, 1000).ok,
            "BA4 must record playback failure distinctly from user stop");
    requireInvariant(h, "playback failure must preserve invariant");

    const SourceResolutionOutcome bFallback2 =
        h.sources.playbackFailed(tokenB);
    require(bFallback2.action == SourceResolutionAction::NeedResolution
                && bFallback2.nextRequest.has_value()
                && bFallback2.nextRequest->candidateKey()
                    == bLow.candidateKey(),
            "playback failure should try next unattempted fallback");
    require(h.coordinator.pendingInput().has_value()
                && h.coordinator.pendingInput()->occurrenceId
                    == q2.occurrenceId(),
            "playback-failure fallback should re-arm AwaitingInput");

    const SourceResolutionOutcome exhausted =
        h.sources.resolutionFailed(*bFallback2.nextRequest);
    require(exhausted.action == SourceResolutionAction::Terminal
                && exhausted.terminal.has_value()
                && exhausted.terminal->reason
                    == SourceFallbackTerminalReason::ExhaustedCandidates
                && !exhausted.nextRequest.has_value(),
            "fallback exhaustion should be typed and finite");

    const QueueSessionStatus terminalStop = h.coordinator.stop();
    require(terminalStop.ok
                && hasEndCause(terminalStop, OccurrenceEndCause::Stopped),
            "terminal exhaustion should stop honestly");
    requireInvariant(h, "terminal stop must preserve invariant");

    require(countListenKind(h.ledger, ListenEventKind::Completion) >= 1,
            "BA4 should distinguish natural completion");
    require(countListenKind(h.ledger, ListenEventKind::SourceChange) >= 1,
            "BA4 should distinguish source replacement");
    require(countListenKind(h.ledger, ListenEventKind::Failure) >= 1,
            "BA4 should distinguish playback failure from user stop");
    require(countListenKind(h.ledger, ListenEventKind::ListeningSpan) >= 2,
            "BA4 should retain qualified listening spans");
    require(h.ledger.totalListeningMs() > 0,
            "qualified listening time should remain factual");

    bool foundContinuation = false;
    for (const auto &event : h.ledger.events()) {
        if (event.context().playInstanceId() == playAContinuation
            && event.context().continuationOf().has_value()
            && *event.context().continuationOf() == playA) {
            foundContinuation = true;
        }
    }
    require(foundContinuation,
            "replacement must preserve PlayInstance continuation");

    g_checks += w4h::verifyMainJourneyRecommendationFacts(
        h.ledger,
        profileA,
        songA,
        songB);
}

void verifySkipHistory()
{
    using namespace vinyl;
    const ProfileScopeId pscope = profile("w4-h-skip-profile");
    const RecordingMbid a =
        recording("44444444-4444-4444-8444-444444444444");
    const RecordingMbid b =
        recording("55555555-5555-4555-8555-555555555555");
    Harness h(pscope);
    const auto q1 = occurrence("skip-q1", a, EnqueueOriginKind::Album);
    const auto q2 = occurrence("skip-q2", b, EnqueueOriginKind::Album);
    require(h.queue.append(q1).ok && h.queue.append(q2).ok
                && h.queue.activate(q1.occurrenceId()).ok,
            "skip fixture queue should construct");
    require(h.coordinator.start(true).ok,
            "skip fixture should await source");
    const auto source =
        exactAudio(a, "skip.provider", "skip-source", 320, false);
    const auto request = mustResolution(
        h.sources.beginForActive({source}, true),
        "skip fixture resolution should begin");
    require(h.sources.acceptResolved(
                request,
                resolved(request,
                         QStringLiteral("https://example.invalid/skip.flac"),
                         1000, 9000),
                1500).action
                == SourceResolutionAction::PlaybackRestarted,
            "skip fixture should begin playback");
    const PlaybackWorkToken token = activeToken(h);
    require(h.coordinator.engineLoaded(token).ok,
            "skip fixture load should settle");
    const auto context = mustListen(
        ListenContext::create(
            pscope, listenSession("skip-listen"), q1,
            token, playInstance("skip-play")),
        "skip BA4 context should construct");
    require(h.recorder.start(context, 1000, 1000, 0, 1000).ok
                && h.recorder.sample(
                    token, 2000, 2000, 1000, 1000).ok,
            "skip BA4 fixture should record listening");
    const QueueSessionStatus skipped = h.coordinator.skip();
    require(skipped.ok && hasEndCause(skipped, OccurrenceEndCause::UserSkip),
            "coordinator should distinguish UserSkip");
    require(h.recorder.skip(
                token, 2500, 2500, 1500, 1000).ok,
            "BA4 should distinguish skip from stop/completion");
    require(countListenKind(h.ledger, ListenEventKind::Skip) == 1,
            "BA4 skip fact should be explicit");
    requireInvariant(h, "skip fixture must preserve invariant");

    require(h.queue.activeOccurrenceId().has_value()
                && *h.queue.activeOccurrenceId() == q2.occurrenceId()
                && h.coordinator.pendingInput().has_value(),
            "skip should advance to q2 and preserve pending source intent");
    h.sources.invalidate();
    const auto stopSource =
        exactAudio(b, "stop.provider", "stop-source", 320, false);
    const auto stopRequest = mustResolution(
        h.sources.beginForActive({stopSource}, true),
        "stop fixture resolution should begin");
    require(h.sources.acceptResolved(
                stopRequest,
                resolved(stopRequest,
                         QStringLiteral("https://example.invalid/stop.flac"),
                         3000, 9000),
                3500).action
                == SourceResolutionAction::PlaybackRestarted,
            "stop fixture should begin playback");
    const PlaybackWorkToken stopToken = activeToken(h);
    require(h.coordinator.engineLoaded(stopToken).ok,
            "stop fixture load should settle");
    const auto stopContext = mustListen(
        ListenContext::create(
            pscope, listenSession("stop-listen"), q2,
            stopToken, playInstance("stop-play")),
        "stop BA4 context should construct");
    require(h.recorder.start(
                stopContext, 3000, 3000, 0, 1000).ok
                && h.recorder.sample(
                    stopToken, 3500, 3500, 500, 1000).ok,
            "stop BA4 fixture should record listening");
    const QueueSessionStatus stopped = h.coordinator.stop();
    require(stopped.ok
                && hasEndCause(stopped, OccurrenceEndCause::Stopped),
            "coordinator should distinguish explicit Stop");
    require(h.recorder.stop(
                stopToken, 4000, 4000, 1000, 1000).ok,
            "BA4 should distinguish explicit stop from failure and skip");
    require(countListenKind(h.ledger, ListenEventKind::Skip) == 1
                && countListenKind(h.ledger, ListenEventKind::Stop) == 1
                && countListenKind(h.ledger, ListenEventKind::Failure) == 0,
            "skip/stop history fixture must retain exact terminal causes");
    requireInvariant(h, "stop fixture must preserve invariant");
}

void verifyAdditionalRaces()
{
    using namespace vinyl;
    const RecordingMbid a =
        recording("66666666-6666-4666-8666-666666666666");
    const RecordingMbid b =
        recording("77777777-7777-4777-8777-777777777777");
    const auto aHigh =
        exactAudio(a, "race.provider.high", "race-high", 1411, true);
    const auto aLow =
        exactAudio(a, "race.provider.low", "race-low", 320, false);

    {
        Harness h(profile("race-double-eof"));
        const auto q1 =
            occurrence("double-eof-1", a, EnqueueOriginKind::Album);
        const auto q2 =
            occurrence("double-eof-2", b, EnqueueOriginKind::Album);
        require(h.queue.append(q1).ok && h.queue.append(q2).ok
                    && h.queue.activate(q1.occurrenceId()).ok,
                "double EOF queue should construct");
        require(h.coordinator.start(true).ok,
                "double EOF should await source");
        const auto request = mustResolution(
            h.sources.beginForActive({aHigh}, true),
            "double EOF resolution should begin");
        require(h.sources.acceptResolved(
                    request,
                    resolved(request,
                             QStringLiteral("https://example.invalid/eof.flac"),
                             1000, 9000),
                    1500).action
                    == SourceResolutionAction::PlaybackRestarted,
                "double EOF playback should begin");
        const PlaybackWorkToken token = activeToken(h);
        require(h.coordinator.engineLoaded(token).ok,
                "double EOF load should settle");
        const auto first = h.coordinator.engineEnded(token);
        require(first.ok && hasEndCause(first, OccurrenceEndCause::Eof),
                "first EOF should advance once");
        require(h.queue.activeOccurrenceId().has_value()
                    && *h.queue.activeOccurrenceId() == q2.occurrenceId(),
                "first EOF should select q2");
        const auto second = h.coordinator.engineEnded(token);
        require(!second.ok
                    && second.error.code
                        == QueueSessionErrorCode::ActiveOccurrenceMismatch,
                "duplicate stale EOF must be rejected");
        require(h.queue.activeOccurrenceId().has_value()
                    && *h.queue.activeOccurrenceId() == q2.occurrenceId(),
                "duplicate EOF must not advance twice");
        requireInvariant(h, "double EOF race must preserve invariant");
    }

    {
        const ProfileScopeId pscope = profile("race-quit-restore");
        Harness h(pscope);
        const auto q1 =
            occurrence("quit-q1", a, EnqueueOriginKind::Album);
        require(h.queue.append(q1).ok
                    && h.queue.activate(q1.occurrenceId()).ok,
                "quit restore queue should construct");
        require(h.coordinator.start(true).ok,
                "quit restore should enter pending input");
        const auto oldRequest = mustResolution(
            h.sources.beginForActive({aHigh}, true),
            "quit restore resolver request should begin");
        const RestoredSessionState restored =
            checkpoint(h, pscope, "quit while resolving should capture");
        require(restored.coordinatorIntent.pendingInput.has_value()
                    && restored.coordinatorIntent.pendingInput->occurrenceId
                        == q1.occurrenceId()
                    && !restored.resumeIntent.has_value(),
                "quit restore must preserve pending intent without live work");
        PlaybackSession fresh;
        require(fresh.generation() == 0,
                "restored pending state must not resurrect BA2 generation");
        requireInvariant(h, "pre-quit state must preserve invariant");

        Harness resumed(pscope);
        resumed.queue = restored.queue;
        require(resumed.coordinator.start(
                    restored.coordinatorIntent.pendingInput->playWhenReady).ok
                    && resumed.coordinator.pendingInput().has_value(),
                "restored pending intent must re-arm AwaitingInput");
        requireInvariant(
            resumed, "restored pending intent must preserve active-occurrence invariant");

        const auto freshCandidate =
            exactAudio(a, "provider.race.restore", "fresh-after-restore", 640, false);
        const auto freshRequest = mustResolution(
            resumed.sources.beginForActive({freshCandidate}, true),
            "restored pending intent must start a fresh resolve");
        const auto latePreQuit = resumed.sources.acceptResolved(
            oldRequest,
            resolved(oldRequest,
                     QStringLiteral("https://example.invalid/pre-quit-late.flac"),
                     1000, 9000),
            1500);
        require(latePreQuit.action == SourceResolutionAction::IgnoredStale
                    && latePreQuit.error.code
                        == SourceResolutionErrorCode::StaleRequest,
                "late pre-quit resolver output must be rejected as stale");
        require(resumed.sources.pendingRequest().has_value()
                    && *resumed.sources.pendingRequest() == freshRequest,
                "stale pre-quit result must not consume the fresh request");
        requireInvariant(
            resumed, "stale pre-quit result must preserve active-occurrence invariant");

        const auto restoredAccepted = resumed.sources.acceptResolved(
            freshRequest,
            resolved(freshRequest,
                     QStringLiteral("https://example.invalid/post-restore.flac"),
                     2000, 10000),
            2500);
        require(restoredAccepted.action
                    == SourceResolutionAction::PlaybackRestarted,
                "fresh post-restore resolve must restart playback");
        requireInvariant(
            resumed, "fresh post-restore restart must preserve active-occurrence invariant");
        const PlaybackWorkToken restoredToken = activeToken(resumed);
        require(resumed.coordinator.engineLoaded(restoredToken).ok
                    && resumed.playback.state() == PlaybackState::Playing,
                "restored pending intent must reach Playing");
        requireInvariant(
            resumed, "restored playback must preserve active-occurrence invariant");
    }

    {
        Harness h(profile("race-expiry-mutation"));
        const auto q1 =
            occurrence("expiry-race-1", a, EnqueueOriginKind::Album);
        const auto q2 =
            occurrence("expiry-race-2", b, EnqueueOriginKind::Album);
        require(h.queue.append(q1).ok
                    && h.queue.activate(q1.occurrenceId()).ok,
                "expiry mutation queue should construct");
        require(h.coordinator.start(true).ok,
                "expiry mutation should await source");
        const auto request = mustResolution(
            h.sources.beginForActive({aHigh, aLow}, true),
            "expiry mutation initial request should begin");
        require(h.sources.acceptResolved(
                    request,
                    resolved(request,
                             QStringLiteral("https://example.invalid/expiring.flac"),
                             1000, 2000),
                    1500).action
                    == SourceResolutionAction::PlaybackRestarted,
                "expiring source should begin");
        const auto refresh = h.sources.refreshIfExpired(2000);
        require(refresh.action == SourceResolutionAction::NeedResolution
                    && refresh.nextRequest.has_value(),
                "expiry should issue re-resolution");
        require(h.coordinator.playNow(q2).ok,
                "queue mutation should win expiry race");
        const auto late = h.sources.acceptResolved(
            *refresh.nextRequest,
            resolved(*refresh.nextRequest,
                     QStringLiteral("https://example.invalid/late-refresh.flac"),
                     2100, 9000),
            2200);
        require(late.action == SourceResolutionAction::IgnoredStale,
                "expiry result after queue mutation must be stale");
        require(h.queue.activeOccurrenceId().has_value()
                    && *h.queue.activeOccurrenceId() == q2.occurrenceId(),
                "late expiry result must not retarget queue");
        requireInvariant(h, "expiry mutation race must preserve invariant");
    }

    {
        Harness h(profile("race-fallback-after-stop"));
        const auto q1 =
            occurrence("stop-fallback-q1", a, EnqueueOriginKind::Album);
        require(h.queue.append(q1).ok
                    && h.queue.activate(q1.occurrenceId()).ok,
                "fallback after stop queue should construct");
        require(h.coordinator.start(true).ok,
                "fallback after stop should await source");
        const auto first = mustResolution(
            h.sources.beginForActive({aHigh, aLow}, true),
            "fallback after stop first request should begin");
        const auto fallback = h.sources.resolutionFailed(first);
        require(fallback.action == SourceResolutionAction::NeedResolution
                    && fallback.nextRequest.has_value(),
                "fallback after stop should reach fallback request");
        require(h.coordinator.stop().ok,
                "stop should cancel pending fallback ownership");
        const auto late = h.sources.acceptResolved(
            *fallback.nextRequest,
            resolved(*fallback.nextRequest,
                     QStringLiteral("https://example.invalid/late-after-stop.flac"),
                     1000, 9000),
            1500);
        require(late.action == SourceResolutionAction::IgnoredStale,
                "fallback success arriving after stop must be stale");
        require(!h.playback.activeOccurrence().has_value(),
                "fallback after stop must leave BA2 unbound");
        requireInvariant(h, "fallback after stop must preserve invariant");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "W4-H fake journey must execute on Qt 6.11.1 exactly");
    verifyMainJourney();
    verifySkipHistory();
    verifyAdditionalRaces();
    g_checks += w4h::runComposedIntegrationProofs();
    qInfo().noquote()
        << "VINYL_W4_H_FAKE_JOURNEY_OK checks=" << g_checks;
    return 0;
}

