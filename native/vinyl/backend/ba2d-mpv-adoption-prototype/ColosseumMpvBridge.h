#pragma once

#include "PlaybackEngineAdapter.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

// Concrete mpv boundary helpers. Seconds exist only on this side of the
// bridge; Vinyl-facing time remains integer milliseconds.
double mpvSecondsFromMilliseconds(qint64 milliseconds) noexcept;
std::optional<qint64> millisecondsFromMpvSeconds(double seconds) noexcept;
QString mpvPerFilePlaybackWindowOptions(
    const std::optional<PlaybackWindow> &playbackWindow);

class ColosseumMpvTimeState final
{
public:
    void bind(const PlaybackWorkToken &token,
              std::optional<PlaybackWindow> playbackWindow);
    void clearFor(const PlaybackWorkToken &token) noexcept;

    std::optional<PlaybackTimeTransform> transformFor(
        const PlaybackWorkToken &token) const;

private:
    std::optional<PlaybackWorkToken> m_token;
    std::optional<PlaybackTimeTransform> m_transform;
};

class ColosseumMpvPlayerPort
{
public:
    virtual ~ColosseumMpvPlayerPort() = default;

    virtual void loadFile(const PlaybackWorkToken &token,
                          const QString &resource,
                          const QString &perFileOptions) = 0;
    virtual void loadFileWithHeaders(
        const PlaybackWorkToken &token,
        const QString &resource,
        const QVector<PlaybackRequestHeader> &headers,
        const QString &perFileOptions) = 0;
    virtual void setPaused(const PlaybackWorkToken &token, bool paused) = 0;

    // physicalSeconds is the concrete mpv absolute timeline.
    virtual void seekExact(const PlaybackSeekCommand &command,
                           double physicalSeconds) = 0;
    virtual void stop(const PlaybackWorkToken &token) = 0;
};

class ColosseumMpvCommands final : public PlaybackEngineCommands
{
public:
    ColosseumMpvCommands(ColosseumMpvPlayerPort &player,
                         ColosseumMpvTimeState &timeState)
        : m_player(player)
        , m_timeState(timeState)
    {
    }

    void load(const PlaybackWorkToken &token,
              const PlaybackLoadInput &input) override;
    void play(const PlaybackWorkToken &token) override;
    void pause(const PlaybackWorkToken &token) override;
    void seek(const PlaybackSeekCommand &command) override;
    void seek(const PlaybackSeekRequest &request) override;
    PlaybackMutationStatus validateSeek(const PlaybackWorkToken &token, qint64 targetMs) const override;
    void stop(const PlaybackWorkToken &token) override;

private:
    ColosseumMpvPlayerPort &m_player;
    ColosseumMpvTimeState &m_timeState;
};

class ColosseumMpvCallbacks final
{
public:
    ColosseumMpvCallbacks(PlaybackEngineAdapter &adapter,
                          ColosseumMpvTimeState &timeState)
        : m_adapter(adapter)
        , m_timeState(timeState)
    {
    }

    PlaybackMutationStatus loaded(const PlaybackWorkToken &token);
    PlaybackMutationStatus pauseObserved(const PlaybackWorkToken &token,
                                         bool paused);

    // These inputs are raw concrete-mpv seconds. They are converted here and
    // never escape upward as domain time.
    PlaybackMutationStatus positionObserved(const PlaybackWorkToken &token,
                                            double physicalSeconds);
    PlaybackMutationStatus durationObserved(const PlaybackWorkToken &token,
                                            double physicalDurationSeconds);
    PlaybackMutationStatus seekApplied(const PlaybackSeekCommand &command,
                                       double physicalObservedSeconds);

    // Compatibility for the pre-Wave-4 seek probe.
    PlaybackMutationStatus seekApplied(const PlaybackSeekRequest &request,
                                       double physicalObservedSeconds);

    PlaybackMutationStatus seekFailed(const PlaybackSeekCommand &command, QString detail);
    PlaybackMutationStatus seekCancelled(const PlaybackSeekCommand &command);

    PlaybackMutationStatus ended(const PlaybackWorkToken &token);
    PlaybackMutationStatus failed(const PlaybackWorkToken &token,
                                  QString code,
                                  QString detail);

private:
    PlaybackMutationStatus invalidConcreteTime(
        const QString &field,
        const QString &detail) const;

    PlaybackEngineAdapter &m_adapter;
    ColosseumMpvTimeState &m_timeState;
};

} // namespace vinyl
