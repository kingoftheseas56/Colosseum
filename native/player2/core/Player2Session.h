#pragma once

#include "DemuxSession.h"
#include "PlaybackGeneration.h"
#include "Player2StateMachine.h"
#include "FrameScheduler.h"
#include "PlaybackClock.h"
#include "player2/audio/AudioPipeline.h"
#include "player2/audio/WASAPIAudioSink.h"

#include <QtCore/QObject>
#include <QtCore/QVariantList>

namespace Colosseum::Player2 {

class D3D11VideoPipeline;

class Player2Session final : public QObject
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
    Q_PROPERTY(QString audioDevice READ audioDevice NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QString audioFormat READ audioFormat NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(double audioQueueMs READ audioQueueMs NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(NormalizationMode normalizationMode READ normalizationMode
               WRITE setNormalizationMode NOTIFY normalizationModeChanged)
    Q_PROPERTY(double normalizationLatencyMs READ normalizationLatencyMs
               NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(double subDelay READ subDelay WRITE setSubDelay NOTIFY subDelayChanged)
    Q_PROPERTY(double audioDelay READ audioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)
    Q_PROPERTY(QString videoAspect READ videoAspect WRITE setVideoAspect NOTIFY videoFillChanged)
    Q_PROPERTY(double panscan READ panscan WRITE setPanscan NOTIFY videoFillChanged)
    Q_PROPERTY(double videoZoom READ videoZoom WRITE setVideoZoom NOTIFY videoFillChanged)

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
    void setVideoPipeline(D3D11VideoPipeline *pipeline);
    QString audioDevice() const;
    QString audioFormat() const;
    double audioQueueMs() const;
    float volume() const noexcept;
    bool muted() const noexcept;
    NormalizationMode normalizationMode() const noexcept;
    double normalizationLatencyMs() const;
    double subDelay() const noexcept;
    double audioDelay() const noexcept;
    QString videoAspect() const;
    double panscan() const noexcept;
    double videoZoom() const noexcept;
    AudioClockSnapshot audioClock() const;
    quint64 audioUnderruns() const;

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
    void subDelayChanged();
    void audioDelayChanged();
    void videoFillChanged();

private:
    bool transition(Player2State state);
    void resetMediaProperties();
    bool hasActiveMedia() const noexcept;

    PlaybackGeneration m_generation;
    Player2StateMachine m_state;
    WASAPIAudioSink m_audioSink;
    AudioPipeline m_audioPipeline;
    PlaybackClock m_playbackClock;
    FrameScheduler m_frameScheduler;
    DemuxSession m_demux;
    D3D11VideoPipeline *m_videoPipeline = nullptr;
    double m_position = 0.0;
    double m_duration = 0.0;
    QVariantList m_tracks;
    QVariantList m_chapters;
    Player2State m_postSeekState = Player2State::Playing;
    NormalizationMode m_normalizationMode = NormalizationMode::Smooth;
    double m_subDelay = 0.0;
    double m_audioDelay = 0.0;
    QString m_videoAspect;
    double m_panscan = 0.0;
    double m_videoZoom = 0.0;
    float m_volume = 1.0f;
    bool m_muted = false;
};

} // namespace Colosseum::Player2
