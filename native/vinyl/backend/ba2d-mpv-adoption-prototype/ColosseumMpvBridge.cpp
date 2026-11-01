#include "VinylBoundaryValidation.h"
#include "ColosseumMpvBridge.h"

#include <cmath>
#include <limits>

namespace vinyl {

double mpvSecondsFromMilliseconds(qint64 milliseconds) noexcept
{
    return static_cast<double>(milliseconds) / 1000.0;
}

std::optional<qint64> millisecondsFromMpvSeconds(double seconds) noexcept
{
    return boundary::milliseconds(seconds);
}

QString mpvPerFilePlaybackWindowOptions(
    const std::optional<PlaybackWindow> &playbackWindow)
{
    if (!playbackWindow.has_value())
        return QString();

    QString options = QStringLiteral("start=%1")
        .arg(QString::number(
            mpvSecondsFromMilliseconds(playbackWindow->startMs()),
            'f',
            3));

    if (playbackWindow->endMs().has_value()) {
        options += QStringLiteral(",end=%1")
            .arg(QString::number(
                mpvSecondsFromMilliseconds(*playbackWindow->endMs()),
                'f',
                3));
    }

    return options;
}

void ColosseumMpvTimeState::bind(
    const PlaybackWorkToken &token,
    std::optional<PlaybackWindow> playbackWindow)
{
    m_token = token;
    m_transform = PlaybackTimeTransform(std::move(playbackWindow));
}

void ColosseumMpvTimeState::clearFor(
    const PlaybackWorkToken &token) noexcept
{
    if (!m_token.has_value() || *m_token != token)
        return;
    m_token.reset();
    m_transform.reset();
}

std::optional<PlaybackTimeTransform>
ColosseumMpvTimeState::transformFor(
    const PlaybackWorkToken &token) const
{
    if (!m_token.has_value()
        || !m_transform.has_value()
        || *m_token != token) {
        return std::nullopt;
    }
    return m_transform;
}

void ColosseumMpvCommands::load(
    const PlaybackWorkToken &token,
    const PlaybackLoadInput &input)
{
    m_timeState.bind(token, input.playbackWindow());

    const QString options =
        mpvPerFilePlaybackWindowOptions(input.playbackWindow());

    if (input.requestHeaders().isEmpty()) {
        m_player.loadFile(token, input.resource(), options);
        return;
    }

    m_player.loadFileWithHeaders(
        token,
        input.resource(),
        input.requestHeaders(),
        options);
}

void ColosseumMpvCommands::play(const PlaybackWorkToken &token)
{
    m_player.setPaused(token, false);
}

void ColosseumMpvCommands::pause(const PlaybackWorkToken &token)
{
    m_player.setPaused(token, true);
}

PlaybackMutationStatus ColosseumMpvCommands::validateSeek(const PlaybackWorkToken &token, qint64 targetMs) const
{
    const auto transform = m_timeState.transformFor(token);
    if (!transform.has_value())
        return PlaybackMutationStatus::failure(PlaybackSessionErrorCode::InvalidState, QStringLiteral("playbackWindow"), QStringLiteral("No transform for active work."));
    if (!transform->physicalPositionForTrack(targetMs).has_value())
        return PlaybackMutationStatus::failure(PlaybackSessionErrorCode::InvalidTimeValue, QStringLiteral("targetMs"), QStringLiteral("Seek is outside the playback window."));
    return PlaybackMutationStatus::success(false);
}

void ColosseumMpvCommands::seek(
    const PlaybackSeekCommand &command)
{
    const auto transform =
        m_timeState.transformFor(command.workToken());
    if (!transform.has_value())
        return;

    const auto physicalMs =
        transform->physicalPositionForTrack(command.targetMs());
    if (!physicalMs.has_value())
        return;

    m_player.seekExact(
        command,
        mpvSecondsFromMilliseconds(*physicalMs));
}

void ColosseumMpvCommands::seek(
    const PlaybackSeekRequest &request)
{
    const auto targetMs =
        millisecondsFromMpvSeconds(request.targetSeconds());
    if (!targetMs.has_value())
        return;

    seek(PlaybackSeekCommand(request, *targetMs));
}

void ColosseumMpvCommands::stop(const PlaybackWorkToken &token)
{
    m_timeState.clearFor(token);
    m_player.stop(token);
}

PlaybackMutationStatus ColosseumMpvCallbacks::loaded(
    const PlaybackWorkToken &token)
{
    return m_adapter.engineLoaded(token);
}

PlaybackMutationStatus ColosseumMpvCallbacks::pauseObserved(
    const PlaybackWorkToken &token,
    bool paused)
{
    return m_adapter.enginePauseObserved(token, paused);
}

PlaybackMutationStatus ColosseumMpvCallbacks::invalidConcreteTime(
    const QString &field,
    const QString &detail) const
{
    return PlaybackMutationStatus::failure(
        PlaybackSessionErrorCode::InvalidTimeValue,
        field,
        detail);
}

PlaybackMutationStatus ColosseumMpvCallbacks::positionObserved(
    const PlaybackWorkToken &token,
    double physicalSeconds)
{
    const auto transform = m_timeState.transformFor(token);
    if (!transform.has_value()) {
        const auto valid = m_adapter.validateWorkToken(token);
        if (!valid.ok) return valid;
        return PlaybackMutationStatus::failure(PlaybackSessionErrorCode::InvalidState, QStringLiteral("playbackWindow"));
    }

    const auto physicalMs =
        millisecondsFromMpvSeconds(physicalSeconds);
    if (!physicalMs.has_value()) {
        return invalidConcreteTime(
            QStringLiteral("physicalPositionSeconds"),
            QStringLiteral(
                "mpv position must be finite, non-negative, and millisecond-representable."));
    }

    const auto trackMs =
        transform->trackPositionForPhysical(*physicalMs);
    if (!trackMs.has_value()) {
        return invalidConcreteTime(
            QStringLiteral("physicalPositionSeconds"),
            QStringLiteral(
                "mpv position could not be mapped into the active playback window."));
    }

    return m_adapter.enginePositionObserved(token, *trackMs);
}

PlaybackMutationStatus ColosseumMpvCallbacks::durationObserved(
    const PlaybackWorkToken &token,
    double physicalDurationSeconds)
{
    const auto transform = m_timeState.transformFor(token);
    if (!transform.has_value()) {
        const auto valid = m_adapter.validateWorkToken(token);
        if (!valid.ok) return valid;
        return PlaybackMutationStatus::failure(PlaybackSessionErrorCode::InvalidState, QStringLiteral("playbackWindow"));
    }

    const auto physicalMs =
        millisecondsFromMpvSeconds(physicalDurationSeconds);
    if (!physicalMs.has_value()) {
        return invalidConcreteTime(
            QStringLiteral("physicalDurationSeconds"),
            QStringLiteral(
                "mpv duration must be finite, non-negative, and millisecond-representable."));
    }

    const auto trackDurationMs =
        transform->trackDurationForPhysicalDuration(*physicalMs);
    if (!trackDurationMs.has_value()) {
        return invalidConcreteTime(
            QStringLiteral("physicalDurationSeconds"),
            QStringLiteral(
                "mpv duration could not be mapped into the active playback window."));
    }

    return m_adapter.engineDurationObserved(
        token,
        *trackDurationMs);
}

PlaybackMutationStatus ColosseumMpvCallbacks::seekApplied(
    const PlaybackSeekCommand &command,
    double physicalObservedSeconds)
{
    const auto transform =
        m_timeState.transformFor(command.workToken());
    if (!transform.has_value()) {
        const auto valid = m_adapter.validateWorkToken(command.workToken());
        if (!valid.ok) return valid;
        return PlaybackMutationStatus::failure(PlaybackSessionErrorCode::InvalidState, QStringLiteral("playbackWindow"));
    }

    const auto physicalMs =
        millisecondsFromMpvSeconds(physicalObservedSeconds);
    if (!physicalMs.has_value()) {
        return invalidConcreteTime(
            QStringLiteral("physicalObservedSeconds"),
            QStringLiteral(
                "mpv seek completion must be finite, non-negative, and millisecond-representable."));
    }

    const auto trackMs =
        transform->trackPositionForPhysical(*physicalMs);
    if (!trackMs.has_value()) {
        return invalidConcreteTime(
            QStringLiteral("physicalObservedSeconds"),
            QStringLiteral(
                "mpv seek completion could not be mapped into the active playback window."));
    }

    return m_adapter.engineSeekApplied(command, *trackMs);
}

PlaybackMutationStatus ColosseumMpvCallbacks::seekApplied(
    const PlaybackSeekRequest &request,
    double physicalObservedSeconds)
{
    const auto targetMs =
        millisecondsFromMpvSeconds(request.targetSeconds());
    if (!targetMs.has_value()) {
        return invalidConcreteTime(
            QStringLiteral("seekRequest.targetSeconds"),
            QStringLiteral(
                "Legacy seek target cannot be represented as integer milliseconds."));
    }

    return seekApplied(
        PlaybackSeekCommand(request, *targetMs),
        physicalObservedSeconds);
}

PlaybackMutationStatus ColosseumMpvCallbacks::seekFailed(const PlaybackSeekCommand &command, QString detail)
{
    return m_adapter.engineSeekFailed(command.request(), std::move(detail));
}
PlaybackMutationStatus ColosseumMpvCallbacks::seekCancelled(const PlaybackSeekCommand &command)
{
    return m_adapter.engineSeekCancelled(command.request());
}

PlaybackMutationStatus ColosseumMpvCallbacks::ended(
    const PlaybackWorkToken &token)
{
    return m_adapter.engineEnded(token);
}

PlaybackMutationStatus ColosseumMpvCallbacks::failed(
    const PlaybackWorkToken &token,
    QString code,
    QString detail)
{
    return m_adapter.engineFailed(
        token,
        PlaybackFailure{std::move(code), std::move(detail)});
}

} // namespace vinyl
