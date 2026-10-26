#pragma once

#include "VinylQueue.h"

#include <QString>
#include <QtGlobal>

#include <optional>
#include <utility>

namespace vinyl {

enum class PlaybackState {
    Idle,
    Loading,
    Playing,
    Paused,
    Ended,
    Failed,
};

QString playbackStateName(PlaybackState state);

enum class PlaybackSessionErrorCode {
    None,
    NoActiveOccurrence,
    InvalidState,
    InvalidTimeValue,
    InvalidFailure,
    StaleAsyncWork,
    SupersededSeek,
    CounterExhausted,
    SeekFailed,
};

struct PlaybackSessionError final {
    PlaybackSessionErrorCode code = PlaybackSessionErrorCode::None;
    QString field;
    QString detail;
};

QString playbackSessionErrorCodeName(PlaybackSessionErrorCode code);

struct PlaybackMutationStatus final {
    bool ok = true;
    bool changed = false;
    PlaybackSessionError error;

    static PlaybackMutationStatus success(bool changed = true)
    {
        PlaybackMutationStatus status;
        status.changed = changed;
        return status;
    }

    static PlaybackMutationStatus failure(PlaybackSessionErrorCode code,
                                          QString field,
                                          QString detail = QString())
    {
        PlaybackMutationStatus status;
        status.ok = false;
        status.error = PlaybackSessionError{
            code, std::move(field), std::move(detail)};
        return status;
    }
};

template <typename T>
struct PlaybackSessionResult final {
    std::optional<T> value;
    PlaybackSessionError error;

    bool ok() const noexcept { return value.has_value(); }

    static PlaybackSessionResult success(T value)
    {
        PlaybackSessionResult out;
        out.value = std::move(value);
        return out;
    }

    static PlaybackSessionResult failure(PlaybackSessionErrorCode code,
                                         QString field,
                                         QString detail = QString())
    {
        PlaybackSessionResult out;
        out.error = PlaybackSessionError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};

class PlaybackSession;

class PlaybackWorkToken final
{
public:
    const QueueOccurrenceId &occurrenceId() const noexcept
    {
        return m_occurrenceId;
    }

    quint64 generation() const noexcept { return m_generation; }

    bool operator==(const PlaybackWorkToken &other) const noexcept
    {
        return m_occurrenceId == other.m_occurrenceId
            && m_generation == other.m_generation;
    }

    bool operator!=(const PlaybackWorkToken &other) const noexcept
    {
        return !(*this == other);
    }

private:
    friend class PlaybackSession;

    PlaybackWorkToken(QueueOccurrenceId occurrenceId, quint64 generation)
        : m_occurrenceId(std::move(occurrenceId))
        , m_generation(generation)
    {
    }

    QueueOccurrenceId m_occurrenceId;
    quint64 m_generation = 0;
};

struct PlaybackFailure final {
    QString code;
    QString detail;
};

class PlaybackSeekRequest final
{
public:
    const PlaybackWorkToken &workToken() const noexcept { return m_workToken; }
    quint64 sequence() const noexcept { return m_sequence; }
    double targetSeconds() const noexcept { return m_targetSeconds; }

private:
    friend class PlaybackSession;

    PlaybackSeekRequest(PlaybackWorkToken workToken,
                        quint64 sequence,
                        double targetSeconds)
        : m_workToken(std::move(workToken))
        , m_sequence(sequence)
        , m_targetSeconds(targetSeconds)
    {
    }

    PlaybackWorkToken m_workToken;
    quint64 m_sequence = 0;
    double m_targetSeconds = 0.0;
};

class PlaybackSession final
{
public:
    PlaybackState state() const noexcept { return m_state; }
    bool playWhenReady() const noexcept { return m_playWhenReady; }
    quint64 generation() const noexcept { return m_generation; }

    const std::optional<QueueOccurrence> &activeOccurrence() const noexcept
    {
        return m_activeOccurrence;
    }

    const std::optional<PlaybackWorkToken> &activeWorkToken() const noexcept
    {
        return m_activeToken;
    }

    const std::optional<double> &positionSeconds() const noexcept
    {
        return m_positionSeconds;
    }

    const std::optional<double> &durationSeconds() const noexcept
    {
        return m_durationSeconds;
    }

    const std::optional<PlaybackSeekRequest> &pendingSeek() const noexcept
    {
        return m_pendingSeek;
    }

    const std::optional<PlaybackFailure> &failure() const noexcept
    {
        return m_failure;
    }

    PlaybackSessionResult<PlaybackWorkToken> beginOccurrence(
        QueueOccurrence occurrence,
        bool playWhenReady = true);

    PlaybackMutationStatus play();
    PlaybackMutationStatus pause();
    PlaybackMutationStatus stop();

    PlaybackMutationStatus markLoaded(const PlaybackWorkToken &token);
    PlaybackMutationStatus observePause(const PlaybackWorkToken &token,
                                        bool paused);
    PlaybackMutationStatus observePosition(const PlaybackWorkToken &token,
                                           double seconds);
    PlaybackMutationStatus observeDuration(const PlaybackWorkToken &token,
                                           double seconds);

    PlaybackSessionResult<PlaybackSeekRequest> requestSeek(
        double targetSeconds);
    PlaybackMutationStatus markSeekApplied(
        const PlaybackSeekRequest &request,
        double observedPositionSeconds);

    PlaybackMutationStatus cancelSeek(const PlaybackSeekRequest &request);

    PlaybackMutationStatus markEnded(const PlaybackWorkToken &token);
    PlaybackMutationStatus markFailed(const PlaybackWorkToken &token,
                                      PlaybackFailure failure);

private:
    PlaybackMutationStatus validateToken(
        const PlaybackWorkToken &token) const;
    PlaybackMutationStatus requireControllableState(
        const QString &operation) const;
    static bool validTime(double seconds) noexcept;
    static bool validFailureCode(const QString &code) noexcept;

    PlaybackState m_state = PlaybackState::Idle;
    bool m_playWhenReady = false;
    quint64 m_generation = 0;
    quint64 m_seekSequence = 0;

    std::optional<QueueOccurrence> m_activeOccurrence;
    std::optional<PlaybackWorkToken> m_activeToken;
    std::optional<double> m_positionSeconds;
    std::optional<double> m_durationSeconds;
    std::optional<PlaybackSeekRequest> m_pendingSeek;
    std::optional<PlaybackFailure> m_failure;
};

} // namespace vinyl
