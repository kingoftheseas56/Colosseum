#include "VideoPlaybackAdapter.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdio>
#include <cstdlib>

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
T mustRelated(const vinyl::RelatedVideoResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustVideo(const vinyl::VideoPlaybackAdapterResult<T> &result,
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

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "fixture Recording MBID should parse");
}

vinyl::ProfileScopeId profile(const char *value)
{
    return mustDomain(
        vinyl::ProfileScopeId::parse(QString::fromLatin1(value)),
        "fixture profile should parse");
}

vinyl::QueueOccurrenceId occurrenceId(const char *value)
{
    return mustQueue(
        vinyl::QueueOccurrenceId::parse(QString::fromLatin1(value)),
        "fixture occurrence id should parse");
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

vinyl::RelatedVideoCandidate videoCandidate(
    const vinyl::RecordingMbid &recordingMbid,
    const char *provider,
    const char *item,
    vinyl::RelatedVideoRelationshipKind kind,
    QVector<vinyl::RelationshipEvidence> evidence)
{
    return mustRelated(
        vinyl::RelatedVideoCandidate::create(
            recordingMbid,
            mustDomain(
                vinyl::ProviderId::parse(QString::fromLatin1(provider)),
                "video provider should parse"),
            mustDomain(
                vinyl::ProviderItemId::parse(QString::fromLatin1(item)),
                "video item should parse"),
            kind,
            std::move(evidence),
            vinyl::RelatedVideoDiscoveryMethod::ProviderStructuredRelation),
        "video candidate should construct");
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
        tokens.push_back(token);
        resources.push_back(input.resource());
    }

    void play(const vinyl::PlaybackWorkToken &) override {}
    void pause(const vinyl::PlaybackWorkToken &) override {}
    void seek(const vinyl::PlaybackSeekRequest &) override {}
    void stop(const vinyl::PlaybackWorkToken &) override {}

    QVector<vinyl::PlaybackWorkToken> tokens;
    QVector<QString> resources;
};

class FakeResolver final : public vinyl::VideoTransportResolverCommands
{
public:
    void resolve(
        const vinyl::SourceResolutionRequest &request) override
    {
        requests.push_back(request);
    }

    QVector<vinyl::SourceResolutionRequest> requests;
};

struct Context final {
    vinyl::VinylQueue queue;
    vinyl::PlaybackSession playback;
    FakeEngine engine;
    vinyl::PlaybackEngineAdapter playbackAdapter;
    EmptyInputProvider inputProvider;
    vinyl::QueueSessionCoordinator coordinator;
    vinyl::ReferenceSourceKindScorer scorer;
    vinyl::SourceProfileMemory memory;
    vinyl::SourceResolutionSession sourceSession;
    FakeResolver resolver;
    vinyl::VideoPlaybackAdapter videoAdapter;

    explicit Context(vinyl::ProfileScopeId profileId)
        : playbackAdapter(playback, engine)
        , coordinator(
              queue,
              playback,
              playbackAdapter,
              inputProvider)
        , memory(std::move(profileId))
        , sourceSession(
              queue,
              playback,
              coordinator,
              vinyl::SourceSelectionPolicy::vinylReference(),
              scorer,
              memory,
              memory.profileId())
        , videoAdapter(resolver)
    {
    }
};

void requireInvariant(const Context &ctx, const char *message)
{
    const auto active = ctx.queue.activeOccurrenceId();
    const auto &bound = ctx.playback.activeOccurrence();
    require(
        !bound.has_value()
            || (active.has_value()
                && bound->occurrenceId() == *active),
        message);
}

void prepare(Context &ctx,
             const QVector<vinyl::QueueOccurrence> &items,
             const vinyl::QueueOccurrenceId &active)
{
    for (const auto &item : items)
        require(ctx.queue.append(item).ok, "queue append should succeed");
    require(ctx.queue.activate(active).ok, "queue activate should succeed");
    require(ctx.coordinator.start(true).ok,
            "missing input should enter coordinator pending state");
    require(ctx.coordinator.pendingInput().has_value(),
            "coordinator should expose pending source intent");
    requireInvariant(ctx, "initial pending state should preserve invariant");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "BF3 W4-C harness must execute on Qt 6.11.1 exactly");

    const RecordingMbid canonical =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid other =
        recording("22222222-2222-4222-8222-222222222222");

    const RelatedVideoCandidate official = videoCandidate(
        canonical,
        "YouTube.Music",
        "22222222-2222-4222-8222-222222222222",
        RelatedVideoRelationshipKind::OfficialMusicVideo,
        {{
            RelationshipEvidenceKind::ProviderOfficialDesignation,
            RelationshipEvidenceStrength::Strong,
            QStringLiteral("structured official designation"),
        }});

    const VideoPlaybackSelection selection = mustVideo(
        VideoPlaybackSelection::create(canonical, official),
        "qualified BF2 official video should enter BF3/W4-C");

    require(selection.recordingMbid() == canonical,
            "RecordingMbid must remain canonical across BF2 -> BF3");
    require(selection.sourceCandidate().transportKind()
                == SourceTransportKind::Video,
            "BF3 selection should become a W4-C video candidate");
    require(selection.sourceCandidate().sourceKind()
                == SourceKind::ExternalResolver,
            "qualified video should use external-resolver source kind");
    require(selection.sourceCandidate().candidateKey().providerId().value()
                == QStringLiteral("youtube.music"),
            "video ProviderId must remain normalized provenance only");
    require(selection.sourceCandidate().candidateKey().candidateId().value()
                == QStringLiteral("22222222-2222-4222-8222-222222222222"),
            "MBID-looking video item must remain opaque provider item provenance");
    require(selection.recordingMbid() != other,
            "provider item value must never replace canonical RecordingMbid");

    const auto wrongRecording =
        VideoPlaybackSelection::create(other, official);
    require(!wrongRecording.ok()
                && wrongRecording.error.code
                    == VideoPlaybackAdapterErrorCode::RecordingMismatch,
            "BF3 must fail closed on Recording mismatch");

    const RelatedVideoCandidate weak = videoCandidate(
        canonical,
        "video.search",
        "weak",
        RelatedVideoRelationshipKind::AlternatePerformance,
        {{
            RelationshipEvidenceKind::TextSearchSimilarity,
            RelationshipEvidenceStrength::Weak,
            QStringLiteral("text-only match"),
        }});
    require(!VideoPlaybackSelection::create(canonical, weak).ok(),
            "weak related-video candidate must remain ineligible for direct switch");

    const RelatedVideoCandidate live = videoCandidate(
        canonical,
        "concert.archive",
        "live",
        RelatedVideoRelationshipKind::LivePerformance,
        {{
            RelationshipEvidenceKind::ProviderRelationshipMetadata,
            RelationshipEvidenceStrength::Strong,
            QStringLiteral("provider says live performance"),
        }});
    require(live.qualification() == RelatedVideoQualification::Qualified,
            "live fixture should be strongly qualified");
    require(!VideoPlaybackSelection::create(canonical, live).ok(),
            "qualified live performance must still be ineligible for Audio | Video");

    {
        Context ctx(profile("bf3-normal"));
        const QueueOccurrence q1 = occurrence("bf3-q1", canonical);
        prepare(ctx, {q1}, q1.occurrenceId());

        const SourceResolutionRequest request = mustResolution(
            ctx.sourceSession.beginForActive(
                {selection.sourceCandidate()},
                true,
                SourceResolutionReason::ExplicitSwitch),
            "BF3 should consume a BB5-owned request identity");

        require(mustVideo(
                    ctx.videoAdapter.dispatch(request),
                    "BF3 dispatch should accept BB5 video request"),
                "dispatch should report success");
        require(ctx.resolver.requests.size() == 1
                    && ctx.resolver.requests.first() == request,
                "yt-dlp/external resolver port must receive exact BB5 request");

        const ResolvedVideoTransport transport = mustVideo(
            ResolvedVideoTransport::create(
                request,
                ResolvedPlaybackResourceKind::HttpUrl,
                QStringLiteral("https://cdn.invalid/video.mp4?sig=fresh"),
                1000,
                5000,
                {{QStringLiteral("Referer"),
                  QStringLiteral("https://provider.invalid/")}},
                -3000),
            "BF3 should adapt external output into ResolvedTrackSource");

        require(transport.source().recordingMbid() == canonical,
                "resolved video transport must retain canonical RecordingMbid");
        require(transport.source().sourceCandidate()
                    == request.candidateKey(),
                "resolved video transport must retain BB5 candidate provenance");
        require(transport.source().resolutionPath()
                    == SourceResolutionPath::ExternalResolver,
                "yt-dlp output must remain external-resolver transport material");
        require(transport.source().recordingTimeOffsetMs().has_value()
                    && *transport.source().recordingTimeOffsetMs() == -3000,
                "video recording-time offset must remain explicit transport timing");

        const auto accepted = ctx.sourceSession.acceptResolved(
            request,
            transport.source(),
            1500);
        require(accepted.action
                    == SourceResolutionAction::PlaybackRestarted,
                "BF3 result should re-enter the general BB5 session");
        require(accepted.replacement.has_value()
                    && accepted.replacement->path
                        == SourceReplacementPath::CoordinatorRestartActive,
                "BF3 must not bypass W4-B restartActive");
        require(ctx.engine.resources.size() == 1
                    && ctx.engine.resources.first()
                        == QStringLiteral(
                            "https://cdn.invalid/video.mp4?sig=fresh"),
                "accepted video should reach BA2 only through coordinator restart");
        requireInvariant(ctx, "normal BF3 handoff must preserve invariant");

        const auto expiry = ctx.sourceSession.refreshIfExpired(5000);
        require(expiry.action == SourceResolutionAction::NeedResolution
                    && expiry.nextRequest.has_value(),
                "video expiry should be owned by the same BB5 re-resolution path");
        require(expiry.nextRequest->transportKind()
                    == SourceTransportKind::Video,
                "video expiry refresh must retain video request kind");
        require(expiry.nextRequest->requestSequence()
                    > request.requestSequence(),
                "video expiry refresh must get a fresh BB5 request identity");
    }

    {
        Context ctx(profile("bf3-supersede"));
        const QueueOccurrence q1 = occurrence("bf3-super-q1", canonical);
        prepare(ctx, {q1}, q1.occurrenceId());

        const SourceResolutionRequest oldRequest = mustResolution(
            ctx.sourceSession.beginForActive(
                {selection.sourceCandidate()},
                true,
                SourceResolutionReason::ExplicitSwitch),
            "first video request should begin");
        require(ctx.videoAdapter.dispatch(oldRequest).ok(),
                "first video request should dispatch");

        const SourceResolutionRequest newRequest = mustResolution(
            ctx.sourceSession.beginForActive(
                {selection.sourceCandidate()},
                true,
                SourceResolutionReason::ExplicitSwitch),
            "new BB5 session should supersede older video request");
        require(newRequest.requestSequence() > oldRequest.requestSequence(),
                "superseding video request must have newer identity");

        const ResolvedVideoTransport late = mustVideo(
            ResolvedVideoTransport::create(
                oldRequest,
                ResolvedPlaybackResourceKind::HttpUrl,
                QStringLiteral("https://cdn.invalid/stale.mp4"),
                1000,
                5000),
            "late video transport should be structurally valid");
        const auto stale = ctx.sourceSession.acceptResolved(
            oldRequest,
            late.source(),
            1500);
        require(stale.action == SourceResolutionAction::IgnoredStale,
                "late video success must obey generic BB5 stale-request fence");
        require(ctx.sourceSession.pendingRequest().has_value()
                    && *ctx.sourceSession.pendingRequest() == newRequest,
                "late BF3 output must not disturb newer BB5 request");
        requireInvariant(ctx, "superseded BF3 result must preserve invariant");
    }

    {
        Context ctx(profile("bf3-skip"));
        const QueueOccurrence q1 = occurrence("bf3-skip-q1", canonical);
        const QueueOccurrence q2 = occurrence("bf3-skip-q2", canonical);
        prepare(ctx, {q1, q2}, q1.occurrenceId());

        const SourceResolutionRequest request = mustResolution(
            ctx.sourceSession.beginForActive(
                {selection.sourceCandidate()},
                true,
                SourceResolutionReason::ExplicitSwitch),
            "video skip-race request should begin");
        require(ctx.videoAdapter.dispatch(request).ok(),
                "video skip-race request should dispatch");
        require(ctx.coordinator.skip().ok,
                "skip during video resolve should change active occurrence");
        requireInvariant(ctx, "skip during video resolve must preserve invariant");

        const ResolvedVideoTransport late = mustVideo(
            ResolvedVideoTransport::create(
                request,
                ResolvedPlaybackResourceKind::HttpUrl,
                QStringLiteral("https://cdn.invalid/late-after-skip.mp4"),
                2000,
                7000),
            "late skipped video transport should remain structurally valid");
        const auto stale = ctx.sourceSession.acceptResolved(
            request,
            late.source(),
            2500);
        require(stale.action == SourceResolutionAction::IgnoredStale,
                "skip during video resolve must invalidate old resolver output");
        require(ctx.queue.activeOccurrenceId().has_value()
                    && *ctx.queue.activeOccurrenceId() == q2.occurrenceId(),
                "late BF3 output must not retarget BA1 after skip");
        requireInvariant(ctx, "late BF3 output after skip must preserve invariant");
    }

    {
        Context ctx(profile("bf3-expired"));
        const QueueOccurrence q1 = occurrence("bf3-expired-q1", canonical);
        prepare(ctx, {q1}, q1.occurrenceId());
        const SourceResolutionRequest request = mustResolution(
            ctx.sourceSession.beginForActive(
                {selection.sourceCandidate()},
                true),
            "expired-video request should begin");

        const ResolvedVideoTransport expired = mustVideo(
            ResolvedVideoTransport::create(
                request,
                ResolvedPlaybackResourceKind::HttpUrl,
                QStringLiteral("https://cdn.invalid/expired.mp4"),
                1000,
                1500),
            "expired video transport should be structurally representable");
        const auto result = ctx.sourceSession.acceptResolved(
            request,
            expired.source(),
            1500);
        require(result.action == SourceResolutionAction::NeedResolution
                    && result.nextRequest.has_value(),
                "expired BF3 output must never play and must re-resolve via BB5");
        require(result.nextRequest->reason()
                    == SourceResolutionReason::ExpiryRefresh,
                "expired BF3 output should use typed expiry refresh reason");
        require(ctx.engine.resources.isEmpty(),
                "expired BF3 transport must never reach BA2");
    }

    require(videoPlaybackAdapterErrorCodeName(
                VideoPlaybackAdapterErrorCode::InvalidResolutionRequest)
                == QStringLiteral("invalid_resolution_request"),
            "BF3 errors should retain stable provider-neutral names");

    qInfo().noquote()
        << "VINYL_BF3_W4C_VIDEO_PLAYBACK_ADAPTER_OK checks=" << g_checks;
    return 0;
}
