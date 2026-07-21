#include "Player2Session.h"

#include "player2/video/D3D11VideoPipeline.h"

#include <QtCore/QVariantMap>

#include <algorithm>

namespace Colosseum::Player2 {

Player2Session::Player2Session(QObject *parent)
    : QObject(parent), m_audioPipeline(&m_audioSink)
{
    m_demux.setAudioPipeline(&m_audioPipeline);
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
        emit audioDiagnosticsChanged();
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
QString Player2Session::audioDevice() const { return m_audioPipeline.deviceName(); }
QString Player2Session::audioFormat() const
{
    const AudioFormat format = m_audioPipeline.outputFormat();
    return QStringLiteral("%1 Hz / %2 ch / float32").arg(format.sampleRate).arg(format.channels);
}
double Player2Session::audioQueueMs() const
{
    const AudioFormat format = m_audioPipeline.outputFormat();
    return format.sampleRate > 0
        ? m_audioPipeline.queueDepthFrames() * 1000.0 / format.sampleRate : 0.0;
}
float Player2Session::volume() const noexcept { return m_volume; }
bool Player2Session::muted() const noexcept { return m_muted; }
AudioClockSnapshot Player2Session::audioClock() const { return m_audioPipeline.clock(); }
quint64 Player2Session::audioUnderruns() const { return m_audioPipeline.underrunCount(); }

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
    m_audioPipeline.flush(next);
    emit generationChanged();
    if (!transition(Player2State::Opening))
        return;
    m_demux.open(request, next);
}

void Player2Session::close()
{
    const quint64 next = m_generation.advance();
    emit generationChanged();
    m_audioPipeline.flush(next);
    m_demux.cancel();
    if (m_videoPipeline)
        m_videoPipeline->flush(next);
    resetMediaProperties();
    transition(Player2State::Idle);
}

void Player2Session::play() { transition(Player2State::Playing); }
void Player2Session::pause() { transition(Player2State::Paused); }

void Player2Session::setVolume(float linear)
{
    const float bounded = std::clamp(linear, 0.0f, 1.0f);
    if (m_volume == bounded)
        return;
    m_volume = bounded;
    m_audioPipeline.setVolume(bounded);
    emit volumeChanged();
}

void Player2Session::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;
    m_audioPipeline.setMuted(muted);
    emit mutedChanged();
}

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
