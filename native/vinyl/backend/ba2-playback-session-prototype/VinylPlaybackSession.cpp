#include "VinylPlaybackSession.h"

#include <QChar>

#include <cmath>
#include <limits>

namespace vinyl {

QString playbackStateName(PlaybackState state)
{
    switch (state) {
    case PlaybackState::Idle:
        return QStringLiteral("idle");
    case PlaybackState::Loading:
        return QStringLiteral("loading");
    case PlaybackState::Playing:
        return QStringLiteral("playing");
    case PlaybackState::Paused:
        return QStringLiteral("paused");
    case PlaybackState::Ended:
        return QStringLiteral("ended");
    case PlaybackState::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

QString playbackSessionErrorCodeName(PlaybackSessionErrorCode code)
{
    switch (code) {
    case PlaybackSessionErrorCode::None:
        return QStringLiteral("none");
    case PlaybackSessionErrorCode::NoActiveOccurrence:
        return QStringLiteral("no_active_occurrence");
    case PlaybackSessionErrorCode::InvalidState:
        return QStringLiteral("invalid_state");
    case PlaybackSessionErrorCode::InvalidTimeValue:
        return QStringLiteral("invalid_time_value");
    case PlaybackSessionErrorCode::InvalidFailure:
        return QStringLiteral("invalid_failure");
    case PlaybackSessionErrorCode::StaleAsyncWork:
        return QStringLiteral("stale_async_work");
    case PlaybackSessionErrorCode::SupersededSeek:
        return QStringLiteral("superseded_seek");
    case PlaybackSessionErrorCode::SeekFailed:
        return QStringLiteral("seek_failed");
    case PlaybackSessionErrorCode::CounterExhausted:
        return QStringLiteral("counter_exhausted");
    }
    return QStringLiteral("unknown");
}

bool PlaybackSession::validTime(double seconds) noexcept
{
    return std::isfinite(seconds) && seconds >= 0.0;
}

bool PlaybackSession::validFailureCode(const QString &code) noexcept
{
    if (code.isEmpty() || code.size() > 128 || code.trimmed() != code)
        return false;

    for (const QChar ch : code) {
        if (ch.isNull() || ch.category() == QChar::Other_Control)
            return false;
    }
    return true;
}

PlaybackMutationStatus PlaybackSession::validateToken(
    const PlaybackWorkToken &token) const
{
    if (!m_activeToken.has_value() || token != *m_activeToken) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::StaleAsyncWork,
            QStringLiteral("workToken"),
            QStringLiteral(
                "Async playback work does not match the active occurrence generation."));
    }

    return PlaybackMutationStatus::success(false);
}

PlaybackMutationStatus PlaybackSession::requireControllableState(
    const QString &operation) const
{
    if (!m_activeOccurrence.has_value() || !m_activeToken.has_value()) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::NoActiveOccurrence,
            QStringLiteral("activeOccurrence"),
            operation + QStringLiteral(" requires an active queue occurrence."));
    }

    if (m_state == PlaybackState::Loading
        || m_state == PlaybackState::Playing
        || m_state == PlaybackState::Paused) {
        return PlaybackMutationStatus::success(false);
    }

    return PlaybackMutationStatus::failure(
        PlaybackSessionErrorCode::InvalidState,
        QStringLiteral("state"),
        operation + QStringLiteral(" is not valid after the active lifecycle terminated."));
}

PlaybackSessionResult<PlaybackWorkToken> PlaybackSession::beginOccurrence(
    QueueOccurrence occurrence,
    bool playWhenReady)
{
    if (m_generation == std::numeric_limits<quint64>::max()) {
        return PlaybackSessionResult<PlaybackWorkToken>::failure(
            PlaybackSessionErrorCode::CounterExhausted,
            QStringLiteral("generation"),
            QStringLiteral("Playback generation cannot wrap."));
    }

    ++m_generation;
    m_seekSequence = 0;

    PlaybackWorkToken token(occurrence.occurrenceId(), m_generation);
    m_activeOccurrence = std::move(occurrence);
    m_activeToken = token;
    m_state = PlaybackState::Loading;
    m_playWhenReady = playWhenReady;
    m_positionSeconds.reset();
    m_durationSeconds.reset();
    m_pendingSeek.reset();
    m_failure.reset();

    return PlaybackSessionResult<PlaybackWorkToken>::success(std::move(token));
}

PlaybackMutationStatus PlaybackSession::play()
{
    const PlaybackMutationStatus valid =
        requireControllableState(QStringLiteral("play"));
    if (!valid.ok)
        return valid;

    if (m_state == PlaybackState::Loading) {
        if (m_playWhenReady)
            return PlaybackMutationStatus::success(false);
        m_playWhenReady = true;
        return PlaybackMutationStatus::success();
    }

    if (m_state == PlaybackState::Playing)
        return PlaybackMutationStatus::success(false);

    m_state = PlaybackState::Playing;
    m_playWhenReady = true;
    return PlaybackMutationStatus::success();
}

PlaybackMutationStatus PlaybackSession::pause()
{
    const PlaybackMutationStatus valid =
        requireControllableState(QStringLiteral("pause"));
    if (!valid.ok)
        return valid;

    if (m_state == PlaybackState::Loading) {
        if (!m_playWhenReady)
            return PlaybackMutationStatus::success(false);
        m_playWhenReady = false;
        return PlaybackMutationStatus::success();
    }

    if (m_state == PlaybackState::Paused)
        return PlaybackMutationStatus::success(false);

    m_state = PlaybackState::Paused;
    m_playWhenReady = false;
    return PlaybackMutationStatus::success();
}

PlaybackMutationStatus PlaybackSession::stop()
{
    if (m_state == PlaybackState::Idle
        && !m_activeOccurrence.has_value()
        && !m_activeToken.has_value()) {
        return PlaybackMutationStatus::success(false);
    }

    m_state = PlaybackState::Idle;
    m_playWhenReady = false;
    m_seekSequence = 0;
    m_activeOccurrence.reset();
    m_activeToken.reset();
    m_positionSeconds.reset();
    m_durationSeconds.reset();
    m_pendingSeek.reset();
    m_failure.reset();

    return PlaybackMutationStatus::success();
}

PlaybackMutationStatus PlaybackSession::markLoaded(
    const PlaybackWorkToken &token)
{
    const PlaybackMutationStatus valid = validateToken(token);
    if (!valid.ok)
        return valid;

    if (m_state == PlaybackState::Loading) {
        m_state = m_playWhenReady
            ? PlaybackState::Playing
            : PlaybackState::Paused;
        return PlaybackMutationStatus::success();
    }

    if (m_state == PlaybackState::Playing
        || m_state == PlaybackState::Paused) {
        return PlaybackMutationStatus::success(false);
    }

    return PlaybackMutationStatus::failure(
        PlaybackSessionErrorCode::InvalidState,
        QStringLiteral("state"),
        QStringLiteral("A load completion cannot revive a terminated lifecycle."));
}

PlaybackMutationStatus PlaybackSession::observePause(
    const PlaybackWorkToken &token,
    bool paused)
{
    const PlaybackMutationStatus tokenStatus = validateToken(token);
    if (!tokenStatus.ok)
        return tokenStatus;

    const PlaybackMutationStatus stateStatus =
        requireControllableState(QStringLiteral("pause observation"));
    if (!stateStatus.ok)
        return stateStatus;

    if (m_state == PlaybackState::Loading) {
        const bool nextPlayWhenReady = !paused;
        if (m_playWhenReady == nextPlayWhenReady)
            return PlaybackMutationStatus::success(false);
        m_playWhenReady = nextPlayWhenReady;
        return PlaybackMutationStatus::success();
    }

    const PlaybackState target =
        paused ? PlaybackState::Paused : PlaybackState::Playing;
    if (m_state == target)
        return PlaybackMutationStatus::success(false);

    m_state = target;
    m_playWhenReady = !paused;
    return PlaybackMutationStatus::success();
}

PlaybackMutationStatus PlaybackSession::observePosition(
    const PlaybackWorkToken &token,
    double seconds)
{
    const PlaybackMutationStatus tokenStatus = validateToken(token);
    if (!tokenStatus.ok)
        return tokenStatus;

    const PlaybackMutationStatus stateStatus =
        requireControllableState(QStringLiteral("position observation"));
    if (!stateStatus.ok)
        return stateStatus;

    if (!validTime(seconds)) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("positionSeconds"),
            QStringLiteral("Playback position must be finite and non-negative."));
    }

    const bool changed =
        !m_positionSeconds.has_value() || *m_positionSeconds != seconds;
    m_positionSeconds = seconds;
    return PlaybackMutationStatus::success(changed);
}

PlaybackMutationStatus PlaybackSession::observeDuration(
    const PlaybackWorkToken &token,
    double seconds)
{
    const PlaybackMutationStatus tokenStatus = validateToken(token);
    if (!tokenStatus.ok)
        return tokenStatus;

    const PlaybackMutationStatus stateStatus =
        requireControllableState(QStringLiteral("duration observation"));
    if (!stateStatus.ok)
        return stateStatus;

    if (!validTime(seconds)) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("durationSeconds"),
            QStringLiteral("Playback duration must be finite and non-negative."));
    }

    const bool changed =
        !m_durationSeconds.has_value() || *m_durationSeconds != seconds;
    m_durationSeconds = seconds;
    return PlaybackMutationStatus::success(changed);
}

PlaybackSessionResult<PlaybackSeekRequest> PlaybackSession::requestSeek(
    double targetSeconds)
{
    const PlaybackMutationStatus stateStatus =
        requireControllableState(QStringLiteral("seek"));
    if (!stateStatus.ok) {
        return PlaybackSessionResult<PlaybackSeekRequest>::failure(
            stateStatus.error.code,
            stateStatus.error.field,
            stateStatus.error.detail);
    }

    if (!validTime(targetSeconds)) {
        return PlaybackSessionResult<PlaybackSeekRequest>::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("targetSeconds"),
            QStringLiteral("Seek target must be finite and non-negative."));
    }

    if (m_seekSequence == std::numeric_limits<quint64>::max()) {
        return PlaybackSessionResult<PlaybackSeekRequest>::failure(
            PlaybackSessionErrorCode::CounterExhausted,
            QStringLiteral("seekSequence"),
            QStringLiteral("Seek sequence cannot wrap inside one generation."));
    }

    ++m_seekSequence;
    PlaybackSeekRequest request(*m_activeToken, m_seekSequence, targetSeconds);
    m_pendingSeek = request;
    return PlaybackSessionResult<PlaybackSeekRequest>::success(
        std::move(request));
}

PlaybackMutationStatus PlaybackSession::markSeekApplied(
    const PlaybackSeekRequest &request,
    double observedPositionSeconds)
{
    const PlaybackMutationStatus tokenStatus =
        validateToken(request.workToken());
    if (!tokenStatus.ok)
        return tokenStatus;

    const PlaybackMutationStatus stateStatus =
        requireControllableState(QStringLiteral("seek completion"));
    if (!stateStatus.ok)
        return stateStatus;

    if (!m_pendingSeek.has_value()
        || m_pendingSeek->sequence() != request.sequence()) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::SupersededSeek,
            QStringLiteral("seekSequence"),
            QStringLiteral(
                "Seek completion is older than the active seek request."));
    }

    if (!validTime(observedPositionSeconds)) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidTimeValue,
            QStringLiteral("observedPositionSeconds"),
            QStringLiteral("Applied seek position must be finite and non-negative."));
    }

    m_positionSeconds = observedPositionSeconds;
    m_pendingSeek.reset();
    return PlaybackMutationStatus::success();
}

PlaybackMutationStatus PlaybackSession::cancelSeek(const PlaybackSeekRequest &request)
{
    const auto valid = validateToken(request.workToken());
    if (!valid.ok) return valid;
    if (!m_pendingSeek.has_value() || m_pendingSeek->sequence() != request.sequence())
        return PlaybackMutationStatus::failure(PlaybackSessionErrorCode::SupersededSeek, QStringLiteral("seekSequence"));
    m_pendingSeek.reset();
    return PlaybackMutationStatus::success();
}

PlaybackMutationStatus PlaybackSession::markEnded(
    const PlaybackWorkToken &token)
{
    const PlaybackMutationStatus tokenStatus = validateToken(token);
    if (!tokenStatus.ok)
        return tokenStatus;

    if (m_state == PlaybackState::Ended)
        return PlaybackMutationStatus::success(false);

    if (m_state == PlaybackState::Failed) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidState,
            QStringLiteral("state"),
            QStringLiteral("EOF cannot replace the terminal failure state."));
    }

    const PlaybackMutationStatus stateStatus =
        requireControllableState(QStringLiteral("end"));
    if (!stateStatus.ok)
        return stateStatus;

    m_state = PlaybackState::Ended;
    m_playWhenReady = false;
    m_pendingSeek.reset();
    return PlaybackMutationStatus::success();
}

PlaybackMutationStatus PlaybackSession::markFailed(
    const PlaybackWorkToken &token,
    PlaybackFailure failure)
{
    const PlaybackMutationStatus tokenStatus = validateToken(token);
    if (!tokenStatus.ok)
        return tokenStatus;

    if (m_state == PlaybackState::Failed)
        return PlaybackMutationStatus::success(false);

    if (m_state == PlaybackState::Ended) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidState,
            QStringLiteral("state"),
            QStringLiteral("Failure cannot replace a completed lifecycle."));
    }

    const PlaybackMutationStatus stateStatus =
        requireControllableState(QStringLiteral("failure"));
    if (!stateStatus.ok)
        return stateStatus;

    if (!validFailureCode(failure.code) || failure.detail.size() > 4096) {
        return PlaybackMutationStatus::failure(
            PlaybackSessionErrorCode::InvalidFailure,
            QStringLiteral("failure"),
            QStringLiteral(
                "Failure code must be a bounded non-control token and detail is bounded."));
    }

    m_state = PlaybackState::Failed;
    m_playWhenReady = false;
    m_pendingSeek.reset();
    m_failure = std::move(failure);
    return PlaybackMutationStatus::success();
}

} // namespace vinyl
