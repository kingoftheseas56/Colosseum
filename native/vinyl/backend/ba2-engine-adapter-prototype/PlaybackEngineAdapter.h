#pragma once

#include "VinylPlaybackSession.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class PlaybackLoadInputErrorCode {
    None,
    InvalidResource,
    InvalidHeader,
    TooManyHeaders,
    InvalidPlaybackWindow,
};

struct PlaybackLoadInputError final {
    PlaybackLoadInputErrorCode code = PlaybackLoadInputErrorCode::None;
    QString field;
    QString detail;
};

QString playbackLoadInputErrorCodeName(PlaybackLoadInputErrorCode code);

template <typename T>
struct PlaybackLoadInputResult final {
    std::optional<T> value;
    PlaybackLoadInputError error;

    bool ok() const noexcept { return value.has_value(); }

    static PlaybackLoadInputResult success(T value)
    {
        PlaybackLoadInputResult out;
        out.value = std::move(value);
        return out;
    }

    static PlaybackLoadInputResult failure(PlaybackLoadInputErrorCode code,
                                           QString field,
                                           QString detail = QString())
    {
        PlaybackLoadInputResult out;
        out.error = PlaybackLoadInputError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};

struct PlaybackRequestHeader final {
    QString name;
    QString value;

    bool operator==(const PlaybackRequestHeader &other) const noexcept
    {
        return name == other.name && value == other.value;
    }
};

class PlaybackWindow final
{
public:
    static PlaybackLoadInputResult<PlaybackWindow> create(
        qint64 startMs,
        std::optional<qint64> endMs = std::nullopt);

    qint64 startMs() const noexcept { return m_startMs; }
    const std::optional<qint64> &endMs() const noexcept { return m_endMs; }

    std::optional<qint64> durationMs() const noexcept
    {
        if (!m_endMs.has_value())
            return std::nullopt;
        return *m_endMs - m_startMs;
    }

    bool operator==(const PlaybackWindow &other) const noexcept
    {
        return m_startMs == other.m_startMs && m_endMs == other.m_endMs;
    }

private:
    PlaybackWindow(qint64 startMs, std::optional<qint64> endMs)
        : m_startMs(startMs)
        , m_endMs(endMs)
    {
    }

    qint64 m_startMs = 0;
    std::optional<qint64> m_endMs;
};

// Maps three distinct clocks without manufacturing alignment:
// physical media time <-> track-relative playback time <-> recording time.
// Recording time is known only when recordingTimeOffsetMs is known.
// Semantics: recordingMs = trackRelativeMs + recordingTimeOffsetMs.
class PlaybackTimeTransform final
{
public:
    explicit PlaybackTimeTransform(
        std::optional<PlaybackWindow> playbackWindow = std::nullopt,
        std::optional<qint64> recordingTimeOffsetMs = std::nullopt)
        : m_playbackWindow(std::move(playbackWindow))
        , m_recordingTimeOffsetMs(recordingTimeOffsetMs)
    {
    }

    const std::optional<PlaybackWindow> &playbackWindow() const noexcept
    {
        return m_playbackWindow;
    }

    const std::optional<qint64> &recordingTimeOffsetMs() const noexcept
    {
        return m_recordingTimeOffsetMs;
    }

    qint64 physicalStartMs() const noexcept;
    std::optional<qint64> trackDurationMs() const noexcept;
    std::optional<qint64> trackDurationForPhysicalDuration(
        qint64 physicalDurationMs) const noexcept;

    std::optional<qint64> physicalPositionForTrack(
        qint64 trackPositionMs) const noexcept;
    std::optional<qint64> trackPositionForPhysical(
        qint64 physicalPositionMs) const noexcept;

    std::optional<qint64> recordingPositionForTrack(
        qint64 trackPositionMs) const noexcept;
    std::optional<qint64> trackPositionForRecording(
        qint64 recordingPositionMs) const noexcept;

private:
    std::optional<PlaybackWindow> m_playbackWindow;
    std::optional<qint64> m_recordingTimeOffsetMs;
};

class PlaybackLoadInput final
{
public:
    static PlaybackLoadInputResult<PlaybackLoadInput> create(
        QString resource,
        QVector<PlaybackRequestHeader> requestHeaders = {},
        std::optional<PlaybackWindow> playbackWindow = std::nullopt);

    const QString &resource() const noexcept { return m_resource; }

    const QVector<PlaybackRequestHeader> &requestHeaders() const noexcept
    {
        return m_requestHeaders;
    }

    const std::optional<PlaybackWindow> &playbackWindow() const noexcept
    {
        return m_playbackWindow;
    }

private:
    PlaybackLoadInput(QString resource,
                      QVector<PlaybackRequestHeader> requestHeaders,
                      std::optional<PlaybackWindow> playbackWindow)
        : m_resource(std::move(resource))
        , m_requestHeaders(std::move(requestHeaders))
        , m_playbackWindow(std::move(playbackWindow))
    {
    }

    QString m_resource;
    QVector<PlaybackRequestHeader> m_requestHeaders;
    std::optional<PlaybackWindow> m_playbackWindow;
};

class PlaybackSeekCommand final
{
public:
    PlaybackSeekCommand(PlaybackSeekRequest request, qint64 targetMs)
        : m_request(std::move(request))
        , m_targetMs(targetMs)
    {
    }

    const PlaybackSeekRequest &request() const noexcept { return m_request; }
    const PlaybackWorkToken &workToken() const noexcept
    {
        return m_request.workToken();
    }
    quint64 sequence() const noexcept { return m_request.sequence(); }
    qint64 targetMs() const noexcept { return m_targetMs; }

private:
    PlaybackSeekRequest m_request;
    qint64 m_targetMs = 0;
};

class PlaybackEngineCommands
{
public:
    virtual ~PlaybackEngineCommands() = default;

    virtual void load(const PlaybackWorkToken &token,
                      const PlaybackLoadInput &input) = 0;
    virtual void play(const PlaybackWorkToken &token) = 0;
    virtual void pause(const PlaybackWorkToken &token) = 0;

    // Canonical Wave 4 command: integer track-relative milliseconds.
    // The compatibility overload keeps earlier isolated prototypes buildable;
    // new concrete engines must override this overload to avoid seconds leakage.
    virtual void seek(const PlaybackSeekCommand &command)
    {
        seek(command.request());
    }

    virtual void seek(const PlaybackSeekRequest &request) = 0;
    virtual PlaybackMutationStatus validateSeek(const PlaybackWorkToken &, qint64) const
    { return PlaybackMutationStatus::success(false); }
    virtual void stop(const PlaybackWorkToken &token) = 0;
};

class PlaybackEngineAdapter final
{
public:
    PlaybackEngineAdapter(PlaybackSession &session,
                          PlaybackEngineCommands &engine)
        : m_session(session)
        , m_engine(engine)
    {
    }

    PlaybackSessionResult<PlaybackWorkToken> beginOccurrence(
        QueueOccurrence occurrence,
        const PlaybackLoadInput &input,
        bool playWhenReady = true);

    PlaybackMutationStatus play();
    PlaybackMutationStatus pause();

    // Canonical Wave 4 domain boundary.
    PlaybackSessionResult<PlaybackSeekRequest> seek(qint64 targetMs);

    // Compatibility only for pre-Wave-4 isolated prototypes. It normalizes
    // through integer milliseconds before command dispatch.
    PlaybackSessionResult<PlaybackSeekRequest> seek(double targetSeconds);

    PlaybackMutationStatus stop();

    std::optional<qint64> positionMs() const noexcept;
    std::optional<qint64> durationMs() const noexcept;

    PlaybackMutationStatus engineLoaded(const PlaybackWorkToken &token);
    PlaybackMutationStatus enginePauseObserved(const PlaybackWorkToken &token,
                                               bool paused);

    // Canonical Wave 4 callbacks use track-relative integer milliseconds.
    PlaybackMutationStatus enginePositionObserved(const PlaybackWorkToken &token,
                                                  qint64 positionMs);
    PlaybackMutationStatus engineDurationObserved(const PlaybackWorkToken &token,
                                                  qint64 durationMs);
    PlaybackMutationStatus engineSeekApplied(
        const PlaybackSeekCommand &command,
        qint64 observedPositionMs);
    PlaybackMutationStatus engineSeekApplied(
        const PlaybackSeekRequest &request,
        qint64 observedPositionMs);

    // Compatibility only for earlier isolated prototypes.
    PlaybackMutationStatus enginePositionObserved(const PlaybackWorkToken &token,
                                                  double seconds);
    PlaybackMutationStatus engineDurationObserved(const PlaybackWorkToken &token,
                                                  double seconds);
    PlaybackMutationStatus engineSeekApplied(
        const PlaybackSeekRequest &request,
        double observedPositionSeconds);

    PlaybackMutationStatus validateWorkToken(const PlaybackWorkToken &token) const;
    PlaybackMutationStatus engineSeekFailed(const PlaybackSeekRequest &request, QString detail);
    PlaybackMutationStatus engineSeekCancelled(const PlaybackSeekRequest &request);

    PlaybackMutationStatus engineEnded(const PlaybackWorkToken &token);
    PlaybackMutationStatus engineFailed(const PlaybackWorkToken &token,
                                        PlaybackFailure failure);

private:
    void dispatchLoadedIntent(const PlaybackWorkToken &token);

    PlaybackSession &m_session;
    PlaybackEngineCommands &m_engine;
};

} // namespace vinyl
