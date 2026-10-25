#include "ResolvedPlaybackSource.h"

#include "CueWholeAlbum.h"

#include <QDir>
#include <QUrl>

namespace vinyl {
namespace {

MpvPlaybackAdapterResult<ResolvedTrackSource> invalidShape(QString detail)
{
    return MpvPlaybackAdapterResult<ResolvedTrackSource>::failure(
        MpvPlaybackAdapterErrorCode::InvalidResourceShape,
        QStringLiteral("resource"),
        std::move(detail));
}

bool isValidFileUrl(const QString &resource)
{
    const QUrl url(resource, QUrl::StrictMode);
    return url.isValid()
        && url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) == 0
        && !url.toLocalFile().isEmpty();
}

bool isValidHttpUrl(const QString &resource)
{
    const QUrl url(resource, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        return false;

    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https");
}

} // namespace

QString resolvedPlaybackResourceKindName(ResolvedPlaybackResourceKind kind)
{
    switch (kind) {
    case ResolvedPlaybackResourceKind::LocalPath:
        return QStringLiteral("local_path");
    case ResolvedPlaybackResourceKind::FileUrl:
        return QStringLiteral("file_url");
    case ResolvedPlaybackResourceKind::HttpUrl:
        return QStringLiteral("http_url");
    }
    return QStringLiteral("unknown");
}

QString sourceResolutionPathName(SourceResolutionPath path)
{
    switch (path) {
    case SourceResolutionPath::Direct:
        return QStringLiteral("direct");
    case SourceResolutionPath::ExternalResolver:
        return QStringLiteral("external_resolver");
    }
    return QStringLiteral("unknown");
}

QString mpvPlaybackAdapterErrorCodeName(MpvPlaybackAdapterErrorCode code)
{
    switch (code) {
    case MpvPlaybackAdapterErrorCode::None:
        return QStringLiteral("none");
    case MpvPlaybackAdapterErrorCode::RecordingMismatch:
        return QStringLiteral("recording_mismatch");
    case MpvPlaybackAdapterErrorCode::InvalidResourceShape:
        return QStringLiteral("invalid_resource_shape");
    case MpvPlaybackAdapterErrorCode::HeadersRequireHttpResource:
        return QStringLiteral("headers_require_http_resource");
    case MpvPlaybackAdapterErrorCode::InvalidTimingMetadata:
        return QStringLiteral("invalid_timing_metadata");
    case MpvPlaybackAdapterErrorCode::InvalidPlaybackWindow:
        return QStringLiteral("invalid_playback_window");
    case MpvPlaybackAdapterErrorCode::PlaybackLoadInputRejected:
        return QStringLiteral("playback_load_input_rejected");
    }
    return QStringLiteral("unknown");
}

ResolvedTrackSource::ResolvedTrackSource(
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
    std::optional<qint64> expiresAtEpochMs)
    : m_recordingMbid(std::move(recordingMbid))
    , m_sourceCandidate(std::move(sourceCandidate))
    , m_sourceKind(sourceKind)
    , m_resolutionPath(resolutionPath)
    , m_resourceKind(resourceKind)
    , m_resource(std::move(resource))
    , m_requestHeaders(std::move(requestHeaders))
    , m_playbackWindow(std::move(playbackWindow))
    , m_recordingTimeOffsetMs(recordingTimeOffsetMs)
    , m_resolvedAtEpochMs(resolvedAtEpochMs)
    , m_expiresAtEpochMs(expiresAtEpochMs)
{
}

MpvPlaybackAdapterResult<ResolvedTrackSource> ResolvedTrackSource::create(
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
    std::optional<qint64> expiresAtEpochMs)
{
    switch (resourceKind) {
    case ResolvedPlaybackResourceKind::LocalPath:
        if (!QDir::isAbsolutePath(resource)) {
            return invalidShape(
                QStringLiteral("A resolved local path must be absolute."));
        }
        break;
    case ResolvedPlaybackResourceKind::FileUrl:
        if (!isValidFileUrl(resource)) {
            return invalidShape(
                QStringLiteral("A resolved file URL must be a valid file:// URL."));
        }
        break;
    case ResolvedPlaybackResourceKind::HttpUrl:
        if (!isValidHttpUrl(resource)) {
            return invalidShape(
                QStringLiteral("A resolved HTTP resource must use http:// or https:// and include a host."));
        }
        break;
    }

    if (!requestHeaders.isEmpty()
        && resourceKind != ResolvedPlaybackResourceKind::HttpUrl) {
        return MpvPlaybackAdapterResult<ResolvedTrackSource>::failure(
            MpvPlaybackAdapterErrorCode::HeadersRequireHttpResource,
            QStringLiteral("requestHeaders"),
            QStringLiteral("Request headers are transport material for HTTP resources only."));
    }

    if (resolvedAtEpochMs.has_value() && *resolvedAtEpochMs < 0) {
        return MpvPlaybackAdapterResult<ResolvedTrackSource>::failure(
            MpvPlaybackAdapterErrorCode::InvalidTimingMetadata,
            QStringLiteral("resolvedAtEpochMs"),
            QStringLiteral("Resolution timestamp must be non-negative when known."));
    }

    if (expiresAtEpochMs.has_value()) {
        if (*expiresAtEpochMs < 0) {
            return MpvPlaybackAdapterResult<ResolvedTrackSource>::failure(
                MpvPlaybackAdapterErrorCode::InvalidTimingMetadata,
                QStringLiteral("expiresAtEpochMs"),
                QStringLiteral("Expiry timestamp must be non-negative when known."));
        }
        if (!resolvedAtEpochMs.has_value()) {
            return MpvPlaybackAdapterResult<ResolvedTrackSource>::failure(
                MpvPlaybackAdapterErrorCode::InvalidTimingMetadata,
                QStringLiteral("resolvedAtEpochMs"),
                QStringLiteral("Known expiry requires the resolution timestamp that established it."));
        }
        if (*expiresAtEpochMs <= *resolvedAtEpochMs) {
            return MpvPlaybackAdapterResult<ResolvedTrackSource>::failure(
                MpvPlaybackAdapterErrorCode::InvalidTimingMetadata,
                QStringLiteral("expiresAtEpochMs"),
                QStringLiteral("Known expiry must be later than resolution time."));
        }
    }

    auto loadInput = PlaybackLoadInput::create(
        resource,
        requestHeaders,
        playbackWindow);
    if (!loadInput.ok()) {
        return MpvPlaybackAdapterResult<ResolvedTrackSource>::failure(
            MpvPlaybackAdapterErrorCode::PlaybackLoadInputRejected,
            loadInput.error.field,
            loadInput.error.detail,
            loadInput.error.code);
    }

    return MpvPlaybackAdapterResult<ResolvedTrackSource>::success(
        ResolvedTrackSource(
            std::move(recordingMbid),
            std::move(sourceCandidate),
            sourceKind,
            resolutionPath,
            resourceKind,
            std::move(resource),
            std::move(requestHeaders),
            std::move(playbackWindow),
            recordingTimeOffsetMs,
            resolvedAtEpochMs,
            expiresAtEpochMs));
}

MpvPlaybackAdapterResult<ResolvedTrackSource>
ResolvedTrackSource::createForExactVirtualSlice(
    const ExactVirtualTrackSlice &slice,
    SourceCandidateKey sourceCandidate,
    SourceKind sourceKind,
    SourceResolutionPath resolutionPath,
    ResolvedPlaybackResourceKind resourceKind,
    QString resource,
    QVector<PlaybackRequestHeader> requestHeaders,
    std::optional<qint64> recordingTimeOffsetMs,
    std::optional<qint64> resolvedAtEpochMs,
    std::optional<qint64> expiresAtEpochMs)
{
    auto window = PlaybackWindow::create(slice.startMs, slice.endMs);
    if (!window.ok()) {
        return MpvPlaybackAdapterResult<ResolvedTrackSource>::failure(
            MpvPlaybackAdapterErrorCode::InvalidPlaybackWindow,
            window.error.field,
            window.error.detail,
            window.error.code);
    }

    return create(
        slice.recordingMbid,
        std::move(sourceCandidate),
        sourceKind,
        resolutionPath,
        resourceKind,
        std::move(resource),
        std::move(requestHeaders),
        *window.value,
        recordingTimeOffsetMs,
        resolvedAtEpochMs,
        expiresAtEpochMs);
}

bool ResolvedTrackSource::isExpiredAt(qint64 nowEpochMs) const noexcept
{
    return m_expiresAtEpochMs.has_value()
        && nowEpochMs >= *m_expiresAtEpochMs;
}

MpvPlaybackAdapterResult<PlaybackLoadInput>
MpvPlaybackSourceAdapter::toPlaybackLoadInput(
    const ResolvedTrackSource &source,
    const RecordingMbid &expectedRecordingMbid)
{
    if (source.recordingMbid() != expectedRecordingMbid) {
        return MpvPlaybackAdapterResult<PlaybackLoadInput>::failure(
            MpvPlaybackAdapterErrorCode::RecordingMismatch,
            QStringLiteral("recordingMbid"),
            QStringLiteral("Resolved source RecordingMbid does not match the expected playback recording."));
    }

    auto loadInput = PlaybackLoadInput::create(
        source.resource(),
        source.requestHeaders(),
        source.playbackWindow());
    if (!loadInput.ok()) {
        return MpvPlaybackAdapterResult<PlaybackLoadInput>::failure(
            MpvPlaybackAdapterErrorCode::PlaybackLoadInputRejected,
            loadInput.error.field,
            loadInput.error.detail,
            loadInput.error.code);
    }

    return MpvPlaybackAdapterResult<PlaybackLoadInput>::success(
        std::move(*loadInput.value));
}

} // namespace vinyl
