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
    Q_PROPERTY(quint64 generation READ generation NOTIFY generationChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QString audioFormat READ audioFormat NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(double audioQueueMs READ audioQueueMs NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)

public:
    explicit Player2Session(QObject *parent = nullptr);
    ~Player2Session() override;

    Player2State state() const noexcept;
    double position() const noexcept;
    double duration() const noexcept;
    QVariantList tracks() const;
    quint64 generation() const noexcept;
    void setVideoPipeline(D3D11VideoPipeline *pipeline);
    QString audioDevice() const;
    QString audioFormat() const;
    double audioQueueMs() const;
    float volume() const noexcept;
    bool muted() const noexcept;
    AudioClockSnapshot audioClock() const;
    quint64 audioUnderruns() const;

public slots:
    void open(const PlaybackRequest &request);
    void close();
    void play();
    void pause();
    void setVolume(float linear);
    void setMuted(bool muted);

signals:
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void tracksChanged();
    void generationChanged();
    void errorOccurred(const Player2Error &error);
    void demuxEnded(DemuxEndReason reason);
    void packetAccepted(quint64 generation, const DemuxPacketInfo &packet);
    void audioDiagnosticsChanged();
    void volumeChanged();
    void mutedChanged();

private:
    bool transition(Player2State state);
    void resetMediaProperties();

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
    float m_volume = 1.0f;
    bool m_muted = false;
};

} // namespace Colosseum::Player2
