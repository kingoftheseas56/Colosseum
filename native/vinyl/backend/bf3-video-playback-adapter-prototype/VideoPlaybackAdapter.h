#pragma once

#include "SourceResolutionSession.h"

#include <optional>
#include <utility>

namespace vinyl {

enum class VideoPlaybackAdapterErrorCode {
    None,
    RecordingMismatch,
    CandidateNotEligible,
    InvalidResolutionRequest,
    InvalidResolvedTransport,
};

struct VideoPlaybackAdapterError final {
    VideoPlaybackAdapterErrorCode code = VideoPlaybackAdapterErrorCode::None;
    QString field;
    QString detail;
};

QString videoPlaybackAdapterErrorCodeName(VideoPlaybackAdapterErrorCode code);

template <typename T>
struct VideoPlaybackAdapterResult final {
    std::optional<T> value;
    VideoPlaybackAdapterError error;

    bool ok() const noexcept { return value.has_value(); }

    static VideoPlaybackAdapterResult success(T value)
    {
        VideoPlaybackAdapterResult out;
        out.value = std::move(value);
        return out;
    }

    static VideoPlaybackAdapterResult failure(
        VideoPlaybackAdapterErrorCode code,
        QString field,
        QString detail = QString())
    {
        VideoPlaybackAdapterResult out;
        out.error = VideoPlaybackAdapterError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};

class VideoPlaybackSelection final
{
public:
    static VideoPlaybackAdapterResult<VideoPlaybackSelection> create(
        RecordingMbid expectedRecording,
        const RelatedVideoCandidate &candidate);

    const RecordingMbid &recordingMbid() const noexcept
    {
        return m_sourceCandidate.recordingMbid();
    }

    const RelatedVideoCandidateKey &relatedVideoKey() const noexcept
    {
        return m_relatedVideoKey;
    }

    const RelatedVideoProvenance &provenance() const noexcept
    {
        return m_provenance;
    }

    RelatedVideoRelationshipKind relationshipKind() const noexcept
    {
        return m_relationshipKind;
    }

    const SelectableSourceCandidate &sourceCandidate() const noexcept
    {
        return m_sourceCandidate;
    }

private:
    VideoPlaybackSelection(
        SelectableSourceCandidate sourceCandidate,
        RelatedVideoCandidateKey relatedVideoKey,
        RelatedVideoProvenance provenance,
        RelatedVideoRelationshipKind relationshipKind)
        : m_sourceCandidate(std::move(sourceCandidate))
        , m_relatedVideoKey(std::move(relatedVideoKey))
        , m_provenance(std::move(provenance))
        , m_relationshipKind(relationshipKind)
    {
    }

    SelectableSourceCandidate m_sourceCandidate;
    RelatedVideoCandidateKey m_relatedVideoKey;
    RelatedVideoProvenance m_provenance;
    RelatedVideoRelationshipKind m_relationshipKind;
};

class ResolvedVideoTransport final
{
public:
    static VideoPlaybackAdapterResult<ResolvedVideoTransport> create(
        SourceResolutionRequest request,
        ResolvedPlaybackResourceKind resourceKind,
        QString resource,
        qint64 resolvedAtEpochMs,
        std::optional<qint64> expiresAtEpochMs = std::nullopt,
        QVector<PlaybackRequestHeader> requestHeaders = {},
        std::optional<qint64> recordingTimeOffsetMs = std::nullopt);

    const SourceResolutionRequest &request() const noexcept
    {
        return m_request;
    }

    const ResolvedTrackSource &source() const noexcept
    {
        return m_source;
    }

private:
    ResolvedVideoTransport(SourceResolutionRequest request,
                           ResolvedTrackSource source)
        : m_request(std::move(request))
        , m_source(std::move(source))
    {
    }

    SourceResolutionRequest m_request;
    ResolvedTrackSource m_source;
};

class VideoTransportResolverCommands
{
public:
    virtual ~VideoTransportResolverCommands() = default;

    virtual void resolve(
        const SourceResolutionRequest &request) = 0;
};

class VideoPlaybackAdapter final
{
public:
    explicit VideoPlaybackAdapter(
        VideoTransportResolverCommands &resolver)
        : m_resolver(resolver)
    {
    }

    VideoPlaybackAdapterResult<bool> dispatch(
        const SourceResolutionRequest &request);

private:
    VideoTransportResolverCommands &m_resolver;
};

} // namespace vinyl
