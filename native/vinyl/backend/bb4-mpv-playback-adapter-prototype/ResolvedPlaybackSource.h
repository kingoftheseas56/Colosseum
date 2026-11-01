#pragma once

#include "PlaybackEngineAdapter.h"
#include "SourceCandidateContract.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

struct ExactVirtualTrackSlice;

enum class ResolvedPlaybackResourceKind {
    LocalPath,
    FileUrl,
    HttpUrl,
};

QString resolvedPlaybackResourceKindName(ResolvedPlaybackResourceKind kind);

enum class SourceResolutionPath {
    Direct,
    ExternalResolver,
};

QString sourceResolutionPathName(SourceResolutionPath path);

enum class MpvPlaybackAdapterErrorCode {
    None,
    RecordingMismatch,
    InvalidResourceShape,
    HeadersRequireHttpResource,
    InvalidTimingMetadata,
    InvalidPlaybackWindow,
    PlaybackLoadInputRejected,
};

QString mpvPlaybackAdapterErrorCodeName(MpvPlaybackAdapterErrorCode code);

struct MpvPlaybackAdapterError final {
    MpvPlaybackAdapterErrorCode code = MpvPlaybackAdapterErrorCode::None;
    QString field;
    QString detail;
    std::optional<PlaybackLoadInputErrorCode> loadInputError;
};

template <typename T>
struct MpvPlaybackAdapterResult final {
    std::optional<T> value;
    MpvPlaybackAdapterError error;

    bool ok() const noexcept { return value.has_value(); }

    static MpvPlaybackAdapterResult success(T value)
    {
        MpvPlaybackAdapterResult out;
        out.value = std::move(value);
        return out;
    }

    static MpvPlaybackAdapterResult failure(
        MpvPlaybackAdapterErrorCode code,
        QString field,
        QString detail = QString(),
        std::optional<PlaybackLoadInputErrorCode> loadInputError = std::nullopt)
    {
        MpvPlaybackAdapterResult out;
        out.error = MpvPlaybackAdapterError{
            code,
            std::move(field),
            std::move(detail),
            loadInputError,
        };
        return out;
    }
};

class ResolvedTrackSource final
{
public:
    static MpvPlaybackAdapterResult<ResolvedTrackSource> create(
        RecordingMbid recordingMbid,
        SourceCandidateKey sourceCandidate,
        SourceKind sourceKind,
        SourceResolutionPath resolutionPath,
        ResolvedPlaybackResourceKind resourceKind,
        QString resource,
        QVector<PlaybackRequestHeader> requestHeaders = {},
        std::optional<PlaybackWindow> playbackWindow = std::nullopt,
        std::optional<qint64> recordingTimeOffsetMs = std::nullopt,
        std::optional<qint64> resolvedAtEpochMs = std::nullopt,
        std::optional<qint64> expiresAtEpochMs = std::nullopt);

    static MpvPlaybackAdapterResult<ResolvedTrackSource> createForExactVirtualSlice(
        const ExactVirtualTrackSlice &slice,
        SourceCandidateKey sourceCandidate,
        SourceKind sourceKind,
        SourceResolutionPath resolutionPath,
        ResolvedPlaybackResourceKind resourceKind,
        QString resource,
        QVector<PlaybackRequestHeader> requestHeaders = {},
        std::optional<qint64> recordingTimeOffsetMs = std::nullopt,
        std::optional<qint64> resolvedAtEpochMs = std::nullopt,
        std::optional<qint64> expiresAtEpochMs = std::nullopt);

    const RecordingMbid &recordingMbid() const noexcept
    {
        return m_recordingMbid;
    }

    const SourceCandidateKey &sourceCandidate() const noexcept
    {
        return m_sourceCandidate;
    }

    SourceKind sourceKind() const noexcept { return m_sourceKind; }
    SourceResolutionPath resolutionPath() const noexcept { return m_resolutionPath; }
    ResolvedPlaybackResourceKind resourceKind() const noexcept { return m_resourceKind; }
    const QString &resource() const noexcept { return m_resource; }

    const QVector<PlaybackRequestHeader> &requestHeaders() const noexcept
    {
        return m_requestHeaders;
    }

    const std::optional<PlaybackWindow> &playbackWindow() const noexcept
    {
        return m_playbackWindow;
    }

    const std::optional<qint64> &recordingTimeOffsetMs() const noexcept
    {
        return m_recordingTimeOffsetMs;
    }

    const std::optional<qint64> &resolvedAtEpochMs() const noexcept
    {
        return m_resolvedAtEpochMs;
    }

    const std::optional<qint64> &expiresAtEpochMs() const noexcept
    {
        return m_expiresAtEpochMs;
    }

    bool isExpiredAt(qint64 nowEpochMs) const noexcept;

    PlaybackTimeTransform timeTransform() const
    {
        return PlaybackTimeTransform(
            m_playbackWindow,
            m_recordingTimeOffsetMs);
    }

private:
    ResolvedTrackSource(
        RecordingMbid recordingMbid,
        SourceCandidateKey sourceCandidate,
        SourceKind sourceKind,
        SourceResolutionPath resolutionPath,
        ResolvedPlaybackResourceKind resourceKind,
        QString resource,
        QVector<PlaybackRequestHeader> requestHeaders,
        std::optional<PlaybackWindow> playbackWindow,
        std::optional<qint64> recordingTimeOffsetMs,
        std::optional<qint64> resolvedAtEpochMs,
        std::optional<qint64> expiresAtEpochMs);

    RecordingMbid m_recordingMbid;
    SourceCandidateKey m_sourceCandidate;
    SourceKind m_sourceKind = SourceKind::Unknown;
    SourceResolutionPath m_resolutionPath = SourceResolutionPath::Direct;
    ResolvedPlaybackResourceKind m_resourceKind =
        ResolvedPlaybackResourceKind::LocalPath;
    QString m_resource;
    QVector<PlaybackRequestHeader> m_requestHeaders;
    std::optional<PlaybackWindow> m_playbackWindow;
    std::optional<qint64> m_recordingTimeOffsetMs;
    std::optional<qint64> m_resolvedAtEpochMs;
    std::optional<qint64> m_expiresAtEpochMs;
};

class MpvPlaybackSourceAdapter final
{
public:
    static MpvPlaybackAdapterResult<PlaybackLoadInput> toPlaybackLoadInput(
        const ResolvedTrackSource &source,
        const RecordingMbid &expectedRecordingMbid);
};

} // namespace vinyl
