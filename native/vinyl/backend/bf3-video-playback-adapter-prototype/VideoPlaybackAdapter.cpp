#include "VideoPlaybackAdapter.h"

namespace vinyl {

QString videoPlaybackAdapterErrorCodeName(
    VideoPlaybackAdapterErrorCode code)
{
    switch (code) {
    case VideoPlaybackAdapterErrorCode::None:
        return QStringLiteral("none");
    case VideoPlaybackAdapterErrorCode::RecordingMismatch:
        return QStringLiteral("recording_mismatch");
    case VideoPlaybackAdapterErrorCode::CandidateNotEligible:
        return QStringLiteral("candidate_not_eligible");
    case VideoPlaybackAdapterErrorCode::InvalidResolutionRequest:
        return QStringLiteral("invalid_resolution_request");
    case VideoPlaybackAdapterErrorCode::InvalidResolvedTransport:
        return QStringLiteral("invalid_resolved_transport");
    }
    return QStringLiteral("unknown");
}

VideoPlaybackAdapterResult<VideoPlaybackSelection>
VideoPlaybackSelection::create(
    RecordingMbid expectedRecording,
    const RelatedVideoCandidate &candidate)
{
    if (candidate.recordingMbid() != expectedRecording) {
        return VideoPlaybackAdapterResult<VideoPlaybackSelection>::failure(
            VideoPlaybackAdapterErrorCode::RecordingMismatch,
            QStringLiteral("candidate.recordingMbid"),
            QStringLiteral(
                "Selected video candidate belongs to a different Recording."));
    }

    auto selectable =
        SelectableSourceCandidate::fromQualifiedVideo(candidate);
    if (!selectable.ok()) {
        return VideoPlaybackAdapterResult<VideoPlaybackSelection>::failure(
            VideoPlaybackAdapterErrorCode::CandidateNotEligible,
            QStringLiteral("candidate.qualification"),
            selectable.error.detail);
    }

    return VideoPlaybackAdapterResult<VideoPlaybackSelection>::success(
        VideoPlaybackSelection(
            std::move(*selectable.value),
            candidate.key(),
            candidate.provenance(),
            candidate.relationshipKind()));
}

VideoPlaybackAdapterResult<ResolvedVideoTransport>
ResolvedVideoTransport::create(
    SourceResolutionRequest request,
    ResolvedPlaybackResourceKind resourceKind,
    QString resource,
    qint64 resolvedAtEpochMs,
    std::optional<qint64> expiresAtEpochMs,
    QVector<PlaybackRequestHeader> requestHeaders,
    std::optional<qint64> recordingTimeOffsetMs)
{
    if (request.transportKind() != SourceTransportKind::Video
        || request.sourceKind() != SourceKind::ExternalResolver) {
        return VideoPlaybackAdapterResult<ResolvedVideoTransport>::failure(
            VideoPlaybackAdapterErrorCode::InvalidResolutionRequest,
            QStringLiteral("request"),
            QStringLiteral(
                "BF3 accepts only W4-C video/external-resolver requests."));
    }

    auto source = ResolvedTrackSource::create(
        request.recordingMbid(),
        request.candidateKey(),
        request.sourceKind(),
        SourceResolutionPath::ExternalResolver,
        resourceKind,
        std::move(resource),
        std::move(requestHeaders),
        std::nullopt,
        recordingTimeOffsetMs,
        resolvedAtEpochMs,
        expiresAtEpochMs);
    if (!source.ok()) {
        return VideoPlaybackAdapterResult<ResolvedVideoTransport>::failure(
            VideoPlaybackAdapterErrorCode::InvalidResolvedTransport,
            source.error.field,
            source.error.detail);
    }

    return VideoPlaybackAdapterResult<ResolvedVideoTransport>::success(
        ResolvedVideoTransport(
            std::move(request),
            std::move(*source.value)));
}

VideoPlaybackAdapterResult<bool>
VideoPlaybackAdapter::dispatch(
    const SourceResolutionRequest &request)
{
    if (request.transportKind() != SourceTransportKind::Video
        || request.sourceKind() != SourceKind::ExternalResolver) {
        return VideoPlaybackAdapterResult<bool>::failure(
            VideoPlaybackAdapterErrorCode::InvalidResolutionRequest,
            QStringLiteral("request"),
            QStringLiteral(
                "BF3 may dispatch only W4-C video/external-resolver work."));
    }

    m_resolver.resolve(request);
    return VideoPlaybackAdapterResult<bool>::success(true);
}

} // namespace vinyl
