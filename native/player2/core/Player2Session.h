#pragma once

#include "DemuxSession.h"
#include "PlaybackGeneration.h"
#include "Player2StateMachine.h"

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

public:
    explicit Player2Session(QObject *parent = nullptr);
    ~Player2Session() override;

    Player2State state() const noexcept;
    double position() const noexcept;
    double duration() const noexcept;
    QVariantList tracks() const;
    quint64 generation() const noexcept;
    void setVideoPipeline(D3D11VideoPipeline *pipeline);

public slots:
    void open(const PlaybackRequest &request);
    void close();
    void play();
    void pause();

signals:
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void tracksChanged();
    void generationChanged();
    void errorOccurred(const Player2Error &error);
    void demuxEnded(DemuxEndReason reason);
    void packetAccepted(quint64 generation, const DemuxPacketInfo &packet);

private:
    bool transition(Player2State state);
    void resetMediaProperties();

    PlaybackGeneration m_generation;
    Player2StateMachine m_state;
    DemuxSession m_demux;
    D3D11VideoPipeline *m_videoPipeline = nullptr;
    double m_position = 0.0;
    double m_duration = 0.0;
    QVariantList m_tracks;
};

} // namespace Colosseum::Player2
