#include "Player2Session.h"

#include "player2/video/D3D11VideoPipeline.h"

#include <QtCore/QVariantMap>

#include <algorithm>

namespace Colosseum::Player2 {

Player2Session::Player2Session(QObject *parent)
    : QObject(parent)
{
    connect(&m_demux, &DemuxSession::opened, this,
            [this](quint64 generation, const DemuxMetadata &metadata) {
        if (!m_generation.accepts(generation))
            return;
        m_duration = metadata.durationUs / 1'000'000.0;
        emit durationChanged();
        m_tracks.clear();
        for (const DemuxStreamInfo &stream : metadata.streams) {
            m_tracks.append(QVariantMap{{QStringLiteral("index"), stream.index},
                                        {QStringLiteral("type"), stream.type},
                                        {QStringLiteral("codec"), stream.codec},
                                        {QStringLiteral("language"), stream.language},
                                        {QStringLiteral("title"), stream.title}});
        }
        emit tracksChanged();
        transition(Player2State::Playing);
    });
    connect(&m_demux, &DemuxSession::packetObserved, this,
            [this](quint64 generation, const DemuxPacketInfo &packet) {
        if (!m_generation.accepts(generation))
            return;
        const double seconds = packet.ptsUs / 1'000'000.0;
        if (seconds > m_position) {
            m_position = seconds;
            emit positionChanged();
        }
        emit packetAccepted(generation, packet);
    });
    connect(&m_demux, &DemuxSession::ended, this,
            [this](quint64 generation, DemuxEndReason reason, const Player2Error &error) {
        if (!m_generation.accepts(generation))
            return;
        if (reason == DemuxEndReason::EndOfFile)
            transition(Player2State::Ended);
        else if (reason == DemuxEndReason::Failed) {
            transition(Player2State::Error);
            emit errorOccurred(error);
        }
        emit demuxEnded(reason);
    });
}

Player2Session::~Player2Session()
{
    m_generation.advance();
    m_demux.cancel();
}

Player2State Player2Session::state() const noexcept { return m_state.state(); }
double Player2Session::position() const noexcept { return m_position; }
double Player2Session::duration() const noexcept { return m_duration; }
QVariantList Player2Session::tracks() const { return m_tracks; }
quint64 Player2Session::generation() const noexcept { return m_generation.current(); }

void Player2Session::setVideoPipeline(D3D11VideoPipeline *pipeline)
{
    m_videoPipeline = pipeline;
    m_demux.setVideoPipeline(pipeline);
}

void Player2Session::open(const PlaybackRequest &request)
{
    if (m_state.state() != Player2State::Idle)
        close();
    resetMediaProperties();
    const quint64 next = m_generation.advance();
    if (m_videoPipeline)
        m_videoPipeline->flush(next);
    emit generationChanged();
    if (!transition(Player2State::Opening))
        return;
    m_demux.open(request, next);
}

void Player2Session::close()
{
    m_generation.advance();
    emit generationChanged();
    m_demux.cancel();
    if (m_videoPipeline)
        m_videoPipeline->flush(m_generation.current());
    resetMediaProperties();
    transition(Player2State::Idle);
}

void Player2Session::play() { transition(Player2State::Playing); }
void Player2Session::pause() { transition(Player2State::Paused); }

bool Player2Session::transition(Player2State next)
{
    const StateTransitionResult result = m_state.transitionTo(next);
    if (!result.accepted) {
        if (result.error)
            emit errorOccurred(*result.error);
        return false;
    }
    if (result.changed)
        emit stateChanged();
    return true;
}

void Player2Session::resetMediaProperties()
{
    if (m_position != 0.0) {
        m_position = 0.0;
        emit positionChanged();
    }
    if (m_duration != 0.0) {
        m_duration = 0.0;
        emit durationChanged();
    }
    if (!m_tracks.isEmpty()) {
        m_tracks.clear();
        emit tracksChanged();
    }
}

} // namespace Colosseum::Player2
