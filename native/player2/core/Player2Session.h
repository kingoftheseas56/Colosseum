#pragma once

#include "DemuxSession.h"
#include "PlaybackGeneration.h"
#include "Player2StateMachine.h"
#include "FrameScheduler.h"
#include "PlaybackClock.h"
#include "player2/audio/AudioPipeline.h"
#include "player2/audio/WASAPIAudioSink.h"
#include "player2/diagnostics/PlaybackDiagnostics.h"
#include "player2/platform/windows/DeviceRecovery.h"

#include <QtCore/QObject>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtGui/QImage>

#include <atomic>
#include <deque>
#include <vector>

namespace Colosseum::Player2 {

class D3D11VideoPipeline;

// Build an ARGB32 QImage from a bitmap subtitle cue's tightly-packed native-endian 0xAARRGGBB pixels.
// Deep-copies (the cue's buffer is transient); returns a null image for an empty/degenerate cue.
QImage subtitleImageFromRgba(const QByteArray &rgba, int width, int height);

// The index of the cue that should be on screen at nowUs (startUs <= now < endUs) among a start-ordered
// list, or -1 if none. The last match wins, so a newer overlapping cue takes over. This is what gates
// subtitle display on the playback clock instead of decode-arrival (cues decode ~seconds ahead).
qsizetype activeSubtitleCueIndex(const std::vector<SubtitleCue> &cues, qint64 nowUs);
void capOpenBitmapCues(std::vector<SubtitleCue> *cues, qint64 atUs);

// Player2Session is also the recovery target: it owns the pipeline/sink/demux, so it is the one
// place that may rebuild a lost device and reopen the current media. The coordinator drives it.
class Player2Session final : public QObject, public IRecoverableTarget
{
    Q_OBJECT
    Q_PROPERTY(Player2State state READ state NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(quint64 generation READ generation NOTIFY generationChanged)
    // True while the SOURCE is waiting on bytes (NetworkState Buffering or Recovering), carried
    // alongside the state instead of through it. A seek into not-yet-downloaded torrent bytes is a
    // deliberate wait on one held-open connection, but networkStateTarget() keeps the session in
    // Seeking on purpose (letting it reach Buffering would let the recovery preempt m_postSeekState
    // and start playback before seekCompleted lands). So the state alone cannot tell the chrome that
    // a wait is in progress — this flag can. Always false for local files: no source, no callback.
    Q_PROPERTY(bool networkStalled READ networkStalled NOTIFY networkStalledChanged)
    // How far the stream is buffered, in seconds on the timeline, or -1 when the question does not
    // apply (local file, or an origin that never declared its length). The seek bar's cache strip
    // binds to this; -1 hides the strip instead of inventing a fill.
    Q_PROPERTY(double bufferedSeconds READ bufferedSeconds NOTIFY bufferedSecondsChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QString audioFormat READ audioFormat NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(double audioQueueMs READ audioQueueMs NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(NormalizationMode normalizationMode READ normalizationMode
               WRITE setNormalizationMode NOTIFY normalizationModeChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(double normalizationLatencyMs READ normalizationLatencyMs
               NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(double subDelay READ subDelay WRITE setSubDelay NOTIFY subDelayChanged)
    Q_PROPERTY(double audioDelay READ audioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)
    Q_PROPERTY(QString videoAspect READ videoAspect WRITE setVideoAspect NOTIFY videoFillChanged)
    Q_PROPERTY(double panscan READ panscan WRITE setPanscan NOTIFY videoFillChanged)
    Q_PROPERTY(double videoZoom READ videoZoom WRITE setVideoZoom NOTIFY videoFillChanged)
    // The active subtitle text for the QML SubtitleLayer to paint. C++ decides WHEN a cue is on
    // screen (set on cue arrival, cleared by a C++ timer for the cue's duration); QML only paints it.
    Q_PROPERTY(QString subtitleText READ subtitleText NOTIFY subtitleTextChanged)
    // The active bitmap (PGS/DVD) subtitle region: { id, x, y, width, height, canvasWidth,
    // canvasHeight }, empty when none. QML renders the picture via the "player2subtitle" image
    // provider (keyed by id) and positions it with the rect; C++ owns cue lifetime, same as text.
    Q_PROPERTY(QVariantMap subtitleBitmap READ subtitleBitmap NOTIFY subtitleBitmapChanged)

public:
    explicit Player2Session(QObject *parent = nullptr);
    ~Player2Session() override;

    Player2State state() const noexcept;
    double position() const noexcept;
    double duration() const noexcept;
    QVariantList tracks() const;
    QVariantList audioTracks() const;
    QVariantList subtitleTracks() const;
    QVariantList chapters() const;
    quint64 generation() const noexcept;
    bool networkStalled() const noexcept;
    double bufferedSeconds() const noexcept;
    void setVideoPipeline(D3D11VideoPipeline *pipeline);
    QString audioDevice() const;
    QString audioFormat() const;
    double audioQueueMs() const;
    float volume() const noexcept;
    bool muted() const noexcept;
    NormalizationMode normalizationMode() const noexcept;
    double speed() const noexcept;
    double normalizationLatencyMs() const;
    double subDelay() const noexcept;
    double audioDelay() const noexcept;
    QString videoAspect() const;
    double panscan() const noexcept;
    double videoZoom() const noexcept;
    AudioClockSnapshot audioClock() const;
    quint64 audioUnderruns() const;
    // A typed, stable diagnostics snapshot aggregating video, audio, colour, state and recovery.
    PlaybackDiagnostics diagnosticsSnapshot() const;
    // The same snapshot as a plain map for QML (the stats overlay). Typed fields, fixed schema.
    Q_INVOKABLE QVariantMap diagnostics() const;
    QString subtitleText() const;
    QVariantMap subtitleBitmap() const;
    // The current bitmap subtitle image for the "player2subtitle" provider. Thread-safe (the render
    // thread calls it); returns a null image if `id` no longer matches the active cue.
    QImage subtitleImageForProvider(const QString &id) const;

    // IRecoverableTarget — driven only by the recovery coordinator, never called directly.
    bool rebuildDevice(DeviceLostReason reason, QString *error) override;
    bool reopenAtSavedPosition(QString *error) override;

public slots:
    void open(const PlaybackRequest &request);
    void close();
    void play();
    void pause();
    void seekExact(double seconds);
    void seekRelative(double seconds);
    void frameStep(int frames);
    void selectAudioTrack(const QString &trackId);
    void selectSubtitleTrack(const QString &trackId);
    void setNormalizationMode(NormalizationMode mode);
    void setSpeed(double speed);
    void setSubDelay(double seconds);
    void setAudioDelay(double seconds);
    void setVideoAspect(const QString &aspect);
    void setPanscan(double value);
    void setVideoZoom(double value);
    void setVolume(float linear);
    void setMuted(bool muted);

signals:
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void tracksChanged();
    void chaptersChanged();
    void generationChanged();
    void networkStalledChanged();
    void bufferedSecondsChanged();
    void errorOccurred(const Player2Error &error);
    void demuxEnded(DemuxEndReason reason);
    void packetAccepted(quint64 generation, const DemuxPacketInfo &packet);
    void seekCompleted(quint64 generation, double actualSeconds);
    void audioTrackChanged(quint64 generation, int streamIndex);
    void subtitleTrackChanged(quint64 generation, int streamIndex);
    void subtitleCue(quint64 generation, const SubtitleCue &cue);
    void audioDiagnosticsChanged();
    void volumeChanged();
    void mutedChanged();
    void normalizationModeChanged();
    void speedChanged();
    void subDelayChanged();
    void audioDelayChanged();
    void videoFillChanged();
    void subtitleTextChanged();
    void subtitleBitmapChanged();

private:
    bool transition(Player2State state);
    void setNetworkStalled(bool stalled);
    void resetMediaProperties();
    bool hasActiveMedia() const noexcept;
    void attemptDeviceRecovery(const Player2Error &error);

    PlaybackGeneration m_generation;
    Player2StateMachine m_state;
    WASAPIAudioSink m_audioSink;
    AudioPipeline m_audioPipeline;
    PlaybackClock m_playbackClock;
    FrameScheduler m_frameScheduler;
    DemuxSession m_demux;
    D3D11VideoPipeline *m_videoPipeline = nullptr;
    DeviceRecoveryCoordinator m_recovery;
    PlaybackRequest m_lastRequest;
    std::atomic_bool m_shuttingDown{false};
    bool m_networkStalled = false;
    double m_bufferedSeconds = -1.0;
    double m_bufferedFrontierSec = 0.0; // furthest packet timestamp demuxed since the last seek
    double m_bufferedAheadSec = 0.0;    // un-demuxed ring, converted at the OBSERVED bitrate
    quint64 m_bufferedSampleTick = 0;   // samples the transport every Nth packet, not every packet
    int m_recoveryAttempts = 0;
    double m_position = 0.0;
    double m_duration = 0.0;
    QVariantList m_tracks;
    QVariantList m_chapters;
    Player2State m_postSeekState = Player2State::Playing;
    // What the demux worker was TOLD to restore when the seek lands. The worker latches this at
    // seek time (DemuxSession's run()-local seekResumePlaying) and applies it in landSeek, so a
    // play/pause pressed DURING the seek cannot reach it — m_postSeekState alone would leave the
    // session saying Paused while the engine kept playing. Comparing the two at completion is what
    // reconciles them.
    bool m_seekToldResumePlaying = true;
    NormalizationMode m_normalizationMode = NormalizationMode::Smooth;
    double m_speed = 1.0;
    double m_subDelay = 0.0;
    double m_audioDelay = 0.0;
    QString m_subtitleText;
    void clearSubtitleBitmap();     // drop the painted bitmap cue (mirrors the text clear paths)
    void evaluateSubtitles();       // ~25Hz: show the cue whose window holds the playback clock
    void applyActiveSubtitle(const SubtitleCue *cue); // publish/clear the on-screen cue (deduped)
    void flushSubtitles();          // drop the painted cue + all buffered cues (seek/track/reset)
    QVariantMap m_subtitleBitmap;   // active bitmap region for QML (empty when none)
    QImage m_subtitleImage;         // the RGBA picture the provider serves (guarded across threads)
    mutable QMutex m_subtitleImageMutex;
    int m_subtitleImageId = 0;      // bumped per bitmap cue so the QML Image source reloads
    std::vector<SubtitleCue> m_cueBuffer; // cues decode ahead of playback; held until their clock time
    QTimer m_subtitleTick;          // heartbeat that gates subtitle display on the playback clock
    bool m_hasActiveCue = false;    // dedup: is a cue currently published?
    qint64 m_activeCueStartUs = 0;  // ...and its start, so the same cue is not re-emitted every tick
    bool m_activeCueIsBitmap = false;
    QString m_videoAspect;
    double m_panscan = 0.0;
    double m_videoZoom = 0.0;
    float m_volume = 1.0f;
    bool m_muted = false;
};

} // namespace Colosseum::Player2
