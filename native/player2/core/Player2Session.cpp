#include "Player2Session.h"

#include "player2/video/D3D11VideoPipeline.h"

#include <QtCore/QVariantMap>

#include <algorithm>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Colosseum::Player2 {

Player2Session::Player2Session(QObject *parent)
    : QObject(parent), m_audioPipeline(&m_audioSink)
{
    m_demux.setAudioPipeline(&m_audioPipeline);
    m_demux.setTiming(&m_playbackClock, &m_frameScheduler);
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
                                        {QStringLiteral("title"), stream.title},
                                        {QStringLiteral("default"), stream.isDefault},
                                        {QStringLiteral("forced"), stream.isForced}});
        }
        m_chapters.clear();
        for (const DemuxChapter &chapter : metadata.chapters) {
            m_chapters.append(QVariantMap{{QStringLiteral("index"), chapter.index},
                                          {QStringLiteral("start"), chapter.startUs / 1'000'000.0},
                                          {QStringLiteral("end"), chapter.endUs / 1'000'000.0},
                                          {QStringLiteral("title"), chapter.title}});
        }
        emit tracksChanged();
        emit chaptersChanged();
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
    connect(&m_demux, &DemuxSession::seekCompleted, this,
            [this](quint64 generation, double actualSeconds) {
        if (!m_generation.accepts(generation))
            return;
        // A seek is the one path allowed to move position backward.
        m_position = actualSeconds;
        emit positionChanged();
        transition(m_postSeekState);
        emit seekCompleted(generation, actualSeconds);
    });
    connect(&m_demux, &DemuxSession::audioTrackChanged, this,
            [this](quint64 generation, int streamIndex) {
        if (!m_generation.accepts(generation))
            return;
        emit audioTrackChanged(generation, streamIndex);
    });
    connect(&m_demux, &DemuxSession::audioNormalizationChanged, this,
            [this](quint64 generation, int) {
        if (!m_generation.accepts(generation))
            return;
        emit audioDiagnosticsChanged(); // latency may have changed with the mode
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
QVariantList Player2Session::chapters() const { return m_chapters; }
QVariantList Player2Session::audioTracks() const
{
    QVariantList out;
    for (const QVariant &entry : m_tracks) {
        if (entry.toMap().value(QStringLiteral("type")).toString() == QStringLiteral("audio"))
            out.append(entry);
    }
    return out;
}
QVariantList Player2Session::subtitleTracks() const
{
    QVariantList out;
    for (const QVariant &entry : m_tracks) {
        if (entry.toMap().value(QStringLiteral("type")).toString() == QStringLiteral("subtitle"))
            out.append(entry);
    }
    return out;
}
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
NormalizationMode Player2Session::normalizationMode() const noexcept { return m_normalizationMode; }
double Player2Session::normalizationLatencyMs() const
{
    return m_audioPipeline.normalizationLatencyUs() / 1000.0;
}
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
    m_playbackClock.invalidate();
    m_frameScheduler.reset();
    emit generationChanged();
    if (!transition(Player2State::Opening))
        return;
    m_demux.open(request, next);
    // Carry the chosen loudness mode into the new session (default Smooth needs no command).
    if (m_normalizationMode != NormalizationMode::Smooth)
        m_demux.requestNormalizationMode(static_cast<int>(m_normalizationMode));
}

void Player2Session::close()
{
    const quint64 next = m_generation.advance();
    emit generationChanged();
    m_audioPipeline.flush(next);
    m_playbackClock.invalidate();
    m_frameScheduler.reset();
    m_demux.cancel();
    if (m_videoPipeline)
        m_videoPipeline->flush(next);
    resetMediaProperties();
    transition(Player2State::Idle);
}

void Player2Session::play()
{
    const bool wasPaused = m_state.state() == Player2State::Paused;
    if (!transition(Player2State::Playing))
        return;
    if (wasPaused)
        m_demux.requestResume();
}

void Player2Session::pause()
{
    if (m_state.state() != Player2State::Playing && m_state.state() != Player2State::Buffering)
        return;
    if (!transition(Player2State::Paused))
        return;
    m_demux.requestPause();
}

bool Player2Session::hasActiveMedia() const noexcept
{
    switch (m_state.state()) {
    case Player2State::Playing:
    case Player2State::Paused:
    case Player2State::Buffering:
    case Player2State::Seeking:
    case Player2State::Ended:
        return true;
    default:
        return false;
    }
}

void Player2Session::seekExact(double seconds)
{
    if (!hasActiveMedia())
        return;
    m_postSeekState = m_state.state() == Player2State::Paused ? Player2State::Paused
                                                              : Player2State::Playing;
    if (!transition(Player2State::Seeking))
        return;
    const quint64 next = m_generation.advance();
    emit generationChanged();
    const qint64 targetUs = static_cast<qint64>(std::max(0.0, seconds) * 1'000'000.0);
    m_demux.requestSeek(targetUs, next, m_postSeekState == Player2State::Playing);
}

void Player2Session::seekRelative(double seconds)
{
    seekExact(m_position + seconds);
}

void Player2Session::frameStep(int frames)
{
    if (!hasActiveMedia())
        return;
    // A frame step always resolves to a paused view of the target frame.
    m_postSeekState = Player2State::Paused;
    if (!transition(Player2State::Seeking))
        return;
    const quint64 next = m_generation.advance();
    emit generationChanged();
    m_demux.requestFrameStep(frames, next);
}

void Player2Session::selectAudioTrack(const QString &trackId)
{
    if (!hasActiveMedia())
        return;
    bool ok = false;
    const int streamIndex = trackId.toInt(&ok);
    if (!ok)
        return;
    const quint64 next = m_generation.advance();
    emit generationChanged();
    m_demux.requestSelectAudioTrack(streamIndex, next);
}

void Player2Session::setNormalizationMode(NormalizationMode mode)
{
    if (m_normalizationMode == mode)
        return;
    m_normalizationMode = mode;
    emit normalizationModeChanged();
    // The filter graph is worker-owned; the change is applied live through the command channel.
    m_demux.requestNormalizationMode(static_cast<int>(mode));
}

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
    if (!m_chapters.isEmpty()) {
        m_chapters.clear();
        emit chaptersChanged();
    }
}

} // namespace Colosseum::Player2
