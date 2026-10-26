#include "VinylBoundaryValidation.h"
#include "PlaybackEngineAdapter.h"

#include <QChar>

#include <cmath>
#include <limits>

namespace vinyl {
namespace {

bool isAsciiHeaderTokenChar(QChar ch)
{
    const ushort u = ch.unicode();
    const bool alpha = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
    const bool digit = u >= '0' && u <= '9';
    if (alpha || digit)
        return true;

    switch (u) {
    case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
    case '+': case '-': case '.': case '^': case '_': case 0x60: case '|':
    case '~':
        return true;
    default:
        return false;
    }
}

bool hasForbiddenResourceChar(const QString &text)
{
    for (const QChar ch : text) {
        if (ch.isNull() || ch.category() == QChar::Other_Control)
            return true;
    }
    return false;
}

bool hasForbiddenHeaderValueChar(const QString &text)
{
    for (const QChar ch : text) {
        if (ch.isNull() || ch == QLatin1Char('\r') || ch == QLatin1Char('\n'))
            return true;
        if (ch.category() == QChar::Other_Control && ch != QLatin1Char('\t'))
            return true;
    }
    return false;
}

std::optional<qint64> checkedAdd(qint64 a, qint64 b) noexcept
{
    if ((b > 0 && a > std::numeric_limits<qint64>::max() - b)
        || (b < 0 && a < std::numeric_limits<qint64>::min() - b)) {
        return std::nullopt;
    }
    return a + b;
}

std::optional<qint64> secondsToMs(double seconds) noexcept
{
    return boundary::milliseconds(seconds);
}

double msToSeconds(qint64 milliseconds) noexcept
{
    return static_cast<double>(milliseconds) / 1000.0;
}

} // namespace

QString playbackLoadInputErrorCodeName(PlaybackLoadInputErrorCode code)
{
    switch (code) {
    case PlaybackLoadInputErrorCode::None:
        return QStringLiteral("none");
    case PlaybackLoadInputErrorCode::InvalidResource:
        return QStringLiteral("invalid_resource");
    case PlaybackLoadInputErrorCode::InvalidHeader:
        return QStringLiteral("invalid_header");
    case PlaybackLoadInputErrorCode::TooManyHeaders:
        return QStringLiteral("too_many_headers");
    case PlaybackLoadInputErrorCode::InvalidPlaybackWindow:
        return QStringLiteral("invalid_playback_window");
    }
    return QStringLiteral("unknown");
}

PlaybackLoadInputResult<PlaybackWindow> PlaybackWindow::create(
    qint64 startMs,
    std::optional<qint64> endMs)
{
    if (startMs < 0) {
        return PlaybackLoadInputResult<PlaybackWindow>::failure(
            PlaybackLoadInputErrorCode::InvalidPlaybackWindow,
            QStringLiteral("playbackWindow.startMs"),
            QStringLiteral("Playback window start must be non-negative."));
    }

    if (endMs.has_value() && *endMs <= startMs) {
        return PlaybackLoadInputResult<PlaybackWindow>::failure(
            PlaybackLoadInputErrorCode::InvalidPlaybackWindow,
            QStringLiteral("playbackWindow.endMs"),
            QStringLiteral("Playback window end must be strictly later than start."));
    }

    return PlaybackLoadInputResult<PlaybackWindow>::success(
        PlaybackWindow(startMs, endMs));
}

qint64 PlaybackTimeTransform::physicalStartMs() const noexcept
{
    return m_playbackWindow.has_value()
        ? m_playbackWindow->startMs()
        : 0;
}

std::optional<qint64> PlaybackTimeTransform::trackDurationMs() const noexcept
{
    return m_playbackWindow.has_value()
        ? m_playbackWindow->durationMs()
        : std::nullopt;
}

std::optional<qint64> PlaybackTimeTransform::trackDurationForPhysicalDuration(
    qint64 physicalDurationMs) const noexcept
{
    if (physicalDurationMs < 0)
        return std::nullopt;

    if (m_playbackWindow.has_value()
        && m_playbackWindow->endMs().has_value()) {
        return m_playbackWindow->durationMs();
    }

    const qint64 start = physicalStartMs();
    if (physicalDurationMs < start)
        return std::nullopt;
    return physicalDurationMs - start;
}

std::optional<qint64> PlaybackTimeTransform::physicalPositionForTrack(
    qint64 trackPositionMs) const noexcept
{
    if (trackPositionMs < 0)
        return std::nullopt;

    const auto duration = trackDurationMs();
    if (duration.has_value() && trackPositionMs > *duration)
        return std::nullopt;

    return checkedAdd(physicalStartMs(), trackPositionMs);
}

std::optional<qint64> PlaybackTimeTransform::trackPositionForPhysical(
    qint64 physicalPositionMs) const noexcept
{
    const qint64 start = physicalStartMs();
    if (physicalPositionMs < start)
        return std::nullopt;

    qint64 trackMs = physicalPositionMs - start;
    const auto duration = trackDurationMs();
    if (duration.has_value() && trackMs > *duration)
        trackMs = *duration;
    return trackMs;
}

std::optional<qint64> PlaybackTimeTransform::recordingPositionForTrack(
    qint64 trackPositionMs) const noexcept
{
    if (trackPositionMs < 0 || !m_recordingTimeOffsetMs.has_value())
        return std::nullopt;
    const auto recording = checkedAdd(
        trackPositionMs,
        *m_recordingTimeOffsetMs);
    if (!recording.has_value() || *recording < 0)
        return std::nullopt;
    return recording;
}

std::optional<qint64> PlaybackTimeTransform::trackPositionForRecording(
    qint64 recordingPositionMs) const noexcept
{
    if (!m_recordingTimeOffsetMs.has_value())
        return std::nullopt;

    if (*m_recordingTimeOffsetMs == std::numeric_limits<qint64>::min())
        return std::nullopt;

    const auto track = checkedAdd(
        recordingPositionMs,
        -*m_recordingTimeOffsetMs);
    if (!track.has_value() || *track < 0)
        return std::nullopt;

    const auto duration = trackDurationMs();
    if (duration.has_value() && *track > *duration)
        return std::nullopt;
    return track;
}

PlaybackLoadInputResult<PlaybackLoadInput> PlaybackLoadInput::create(
    QString resource,
    QVector<PlaybackRequestHeader> requestHeaders,
    std::optional<PlaybackWindow> playbackWindow)
{
    if (resource.isEmpty()
        || resource.size() > 8192
        || hasForbiddenResourceChar(resource)) {
        return PlaybackLoadInputResult<PlaybackLoadInput>::failure(
            PlaybackLoadInputErrorCode::InvalidResource,
            QStringLiteral("resource"),
            QStringLiteral(
                "Playback resource must be non-empty, bounded, and contain no control characters."));
    }

    if (requestHeaders.size() > 64) {
        return PlaybackLoadInputResult<PlaybackLoadInput>::failure(
            PlaybackLoadInputErrorCode::TooManyHeaders,
            QStringLiteral("requestHeaders"),
            QStringLiteral("At most 64 request headers may be supplied."));
    }

    for (qsizetype i = 0; i < requestHeaders.size(); ++i) {
        const PlaybackRequestHeader &header = requestHeaders.at(i);
        if (header.name.isEmpty() || header.name.size() > 256) {
            return PlaybackLoadInputResult<PlaybackLoadInput>::failure(
                PlaybackLoadInputErrorCode::InvalidHeader,
                QStringLiteral("requestHeaders[%1].name").arg(i),
                QStringLiteral("Header name must contain 1..256 characters."));
        }

        for (const QChar ch : header.name) {
            if (!isAsciiHeaderTokenChar(ch)) {
                return PlaybackLoadInputResult<PlaybackLoadInput>::failure(
                    PlaybackLoadInputErrorCode::InvalidHeader,
                    QStringLiteral("requestHeaders[%1].name").arg(i),
                    QStringLiteral("Header name must be an HTTP token."));
            }
        }

        if (header.value.size() > 4096
            || hasForbiddenHeaderValueChar(header.value)) {
            return PlaybackLoadInputResult<PlaybackLoadInput>::failure(
                PlaybackLoadInputErrorCode::InvalidHeader,
                QStringLiteral("requestHeaders[%1].value").arg(i),
                QStringLiteral(
                    "Header value must be bounded and contain no line breaks or unsafe control characters."));
        }
    }

    return PlaybackLoadInputResult<PlaybackLoadInput>::success(
        PlaybackLoadInput(
            std::move(resource),
            std::move(requestHeaders),
            std::move(playbackWindow)));
}

PlaybackSessionResult<PlaybackWorkToken> PlaybackEngineAdapter::beginOccurrence(
    QueueOccurrence occurrence,
    const PlaybackLoadInput &input,
    bool playWhenReady)
{
    auto started =
        m_session.beginOccurrence(std::move(occurrence), playWhenReady);
    if (!started.ok())
        return started;

    m_engine.load(*started.value, input);
    return started;
}

PlaybackMutationStatus PlaybackEngineAdapter::play()
{
    const PlaybackMutationStatus status = m_session.play();
    if (!status.ok || !status.changed)
        return status;

    if (m_session.state() == PlaybackState::Playing
        && m_session.activeWorkToken().has_value()) {
        m_engine.play(*m_session.activeWorkToken());
    }

    return status;
}

PlaybackMutationStatus PlaybackEngineAdapter::pause()
{
    const PlaybackMutationStatus status = m_session.pause();
    if (!status.ok || !status.changed)
        return status;

    if (m_session.state() == PlaybackState::Paused
        && m_session.activeWorkToken().has_value()) {
        m_engine.pause(*m_session.activeWorkToken());
    }

    return status;
}

PlaybackSessionResult<PlaybackSeekRequest> PlaybackEngineAdapter::seek(
    qint64 targetMs)
{
    if (targetMs < 0) {
        return PlaybackSessionResult<PlaybackSeekRequest>::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("targetMs"),
            QStringLiteral("Seek target must be non-negative integer milliseconds."));
    }

    if (!m_session.activeWorkToken().has_value())
        return PlaybackSessionResult<PlaybackSeekRequest>::failure(PlaybackSessionErrorCode::NoActiveOccurrence, QStringLiteral("workToken"));
    const auto eligible = m_engine.validateSeek(*m_session.activeWorkToken(), targetMs);
    if (!eligible.ok)
        return PlaybackSessionResult<PlaybackSeekRequest>::failure(eligible.error.code, eligible.error.field, eligible.error.detail);
    auto requested = m_session.requestSeek(msToSeconds(targetMs));
    if (!requested.ok())
        return requested;

    m_engine.seek(PlaybackSeekCommand(*requested.value, targetMs));
    return requested;
}

PlaybackSessionResult<PlaybackSeekRequest> PlaybackEngineAdapter::seek(
    double targetSeconds)
{
    const auto targetMs = secondsToMs(targetSeconds);
    if (!targetMs.has_value()) {
        return PlaybackSessionResult<PlaybackSeekRequest>::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("targetSeconds"),
            QStringLiteral("Seek target must convert to non-negative integer milliseconds."));
    }
    return seek(*targetMs);
}

PlaybackMutationStatus PlaybackEngineAdapter::stop()
{
    const std::optional<PlaybackWorkToken> token =
        m_session.activeWorkToken();

    const PlaybackMutationStatus status = m_session.stop();
    if (!status.ok || !status.changed || !token.has_value())
        return status;

    m_engine.stop(*token);
    return status;
}

std::optional<qint64> PlaybackEngineAdapter::positionMs() const noexcept
{
    if (!m_session.positionSeconds().has_value())
        return std::nullopt;
    return secondsToMs(*m_session.positionSeconds());
}

std::optional<qint64> PlaybackEngineAdapter::durationMs() const noexcept
{
    if (!m_session.durationSeconds().has_value())
        return std::nullopt;
    return secondsToMs(*m_session.durationSeconds());
}

void PlaybackEngineAdapter::dispatchLoadedIntent(
    const PlaybackWorkToken &token)
{
    switch (m_session.state()) {
    case PlaybackState::Playing:
        m_engine.play(token);
        break;
    case PlaybackState::Paused:
        m_engine.pause(token);
        break;
    case PlaybackState::Idle:
    case PlaybackState::Loading:
    case PlaybackState::Ended:
    case PlaybackState::Failed:
        break;
    }
}

PlaybackMutationStatus PlaybackEngineAdapter::engineLoaded(
    const PlaybackWorkToken &token)
{
    const PlaybackMutationStatus status = m_session.markLoaded(token);
    if (status.ok && status.changed)
        dispatchLoadedIntent(token);
    return status;
}

PlaybackMutationStatus PlaybackEngineAdapter::enginePauseObserved(
    const PlaybackWorkToken &token,
    bool paused)
{
    return m_session.observePause(token, paused);
}

PlaybackMutationStatus PlaybackEngineAdapter::enginePositionObserved(
    const PlaybackWorkToken &token,
    qint64 positionMs)
{
    if (positionMs < 0) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("positionMs"),
            QStringLiteral("Playback position must be non-negative integer milliseconds."));
    }
    return m_session.observePosition(token, msToSeconds(positionMs));
}

PlaybackMutationStatus PlaybackEngineAdapter::engineDurationObserved(
    const PlaybackWorkToken &token,
    qint64 durationMs)
{
    if (durationMs < 0) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("durationMs"),
            QStringLiteral("Playback duration must be non-negative integer milliseconds."));
    }
    return m_session.observeDuration(token, msToSeconds(durationMs));
}

PlaybackMutationStatus PlaybackEngineAdapter::engineSeekApplied(
    const PlaybackSeekCommand &command,
    qint64 observedPositionMs)
{
    return engineSeekApplied(command.request(), observedPositionMs);
}

PlaybackMutationStatus PlaybackEngineAdapter::engineSeekApplied(
    const PlaybackSeekRequest &request,
    qint64 observedPositionMs)
{
    if (observedPositionMs < 0) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("observedPositionMs"),
            QStringLiteral("Applied seek position must be non-negative integer milliseconds."));
    }
    return m_session.markSeekApplied(
        request,
        msToSeconds(observedPositionMs));
}

PlaybackMutationStatus PlaybackEngineAdapter::enginePositionObserved(
    const PlaybackWorkToken &token,
    double seconds)
{
    const auto milliseconds = secondsToMs(seconds);
    if (!milliseconds.has_value()) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("positionSeconds"),
            QStringLiteral("Playback position must convert to non-negative integer milliseconds."));
    }
    return enginePositionObserved(token, *milliseconds);
}

PlaybackMutationStatus PlaybackEngineAdapter::engineDurationObserved(
    const PlaybackWorkToken &token,
    double seconds)
{
    const auto milliseconds = secondsToMs(seconds);
    if (!milliseconds.has_value()) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("durationSeconds"),
            QStringLiteral("Playback duration must convert to non-negative integer milliseconds."));
    }
    return engineDurationObserved(token, *milliseconds);
}

PlaybackMutationStatus PlaybackEngineAdapter::engineSeekApplied(
    const PlaybackSeekRequest &request,
    double observedPositionSeconds)
{
    const auto milliseconds = secondsToMs(observedPositionSeconds);
    if (!milliseconds.has_value()) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("observedPositionSeconds"),
            QStringLiteral("Applied seek position must convert to non-negative integer milliseconds."));
    }
    return engineSeekApplied(request, *milliseconds);
}

PlaybackMutationStatus PlaybackEngineAdapter::validateWorkToken(const PlaybackWorkToken &token) const
{
    if (!m_session.activeWorkToken().has_value() || *m_session.activeWorkToken() != token)
        return PlaybackMutationStatus::failure(PlaybackSessionErrorCode::StaleAsyncWork, QStringLiteral("workToken"));
    return PlaybackMutationStatus::success(false);
}

PlaybackMutationStatus PlaybackEngineAdapter::engineSeekCancelled(const PlaybackSeekRequest &request)
{
    return m_session.cancelSeek(request);
}

PlaybackMutationStatus PlaybackEngineAdapter::engineSeekFailed(const PlaybackSeekRequest &request, QString detail)
{
    auto status = m_session.cancelSeek(request);
    if (!status.ok) return status;
    status.ok = false;
    status.error = {PlaybackSessionErrorCode::SeekFailed, QStringLiteral("seek"), detail.left(4096)};
    return status;
}

PlaybackMutationStatus PlaybackEngineAdapter::engineEnded(
    const PlaybackWorkToken &token)
{
    return m_session.markEnded(token);
}

PlaybackMutationStatus PlaybackEngineAdapter::engineFailed(
    const PlaybackWorkToken &token,
    PlaybackFailure failure)
{
    return m_session.markFailed(token, std::move(failure));
}

} // namespace vinyl
