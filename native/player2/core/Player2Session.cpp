#include "Player2Session.h"

#include "player2/video/D3D11VideoPipeline.h"

#include <QtCore/QJsonObject>
#include <QtCore/QMetaEnum>
#include <QtCore/QVariantMap>

#include <algorithm>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Colosseum::Player2 {

// The performance counter reading the playback clock expects, so the subtitle tick can sample the true
// playback position (the same clock the video is scheduled against).
static qint64 sessionQpcNow()
{
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

Player2Session::Player2Session(QObject *parent)
    : QObject(parent), m_audioPipeline(&m_audioSink),
      // No wall-clock backoff: recovery runs synchronously on the GUI-thread error seam, so it must
      // never sleep the UI. The bounded attempts still hold; a settle-delay + worker thread is a
      // documented hardening follow-on.
      m_recovery(RecoveryPolicy{2, [](int) { return 0; }})
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
            if (error.recoverable && (error.code == Player2ErrorCode::DeviceLost ||
                                      error.code == Player2ErrorCode::AudioDeviceLost))
                attemptDeviceRecovery(error);
            else {
                transition(Player2State::Error);
                emit errorOccurred(error);
            }
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
        flushSubtitles(); // drop the painted cue AND buffered upcoming cues from the old position
        transition(m_postSeekState);
        // A play/pause pressed DURING the seek moved m_postSeekState, but the worker had already
        // latched the state it was told at seek time and restored THAT in landSeek. Correct it now,
        // or the session and the engine disagree (session Paused, audio still running).
        const bool wantPlaying = m_postSeekState == Player2State::Playing;
        if (wantPlaying != m_seekToldResumePlaying) {
            if (wantPlaying)
                m_demux.requestResume();
            else
                m_demux.requestPause();
            m_seekToldResumePlaying = wantPlaying;
        }
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
    connect(&m_demux, &DemuxSession::subtitleTrackChanged, this,
            [this](quint64 generation, int streamIndex) {
        if (!m_generation.accepts(generation))
            return;
        // Turning subtitles off or switching tracks must drop the painted cue AND the buffered upcoming
        // cues of the old track at once, so nothing stale shows.
        flushSubtitles();
        emit subtitleTrackChanged(generation, streamIndex);
    });
    connect(&m_demux, &DemuxSession::subtitleCue, this,
            [this](quint64 generation, const SubtitleCue &cue) {
        if (!m_generation.accepts(generation))
            return;
        // Apply the user's subtitle delay as a timing shift (mpv sub-delay parity).
        SubtitleCue shifted = cue;
        const qint64 shiftUs = static_cast<qint64>(m_subDelay * 1'000'000.0);
        shifted.startUs += shiftUs;
        shifted.endUs += shiftUs;
        emit subtitleCue(generation, shifted);
        // Cues decode seconds AHEAD of playback (read-ahead), so buffer them; the subtitle tick shows
        // each one only when the playback clock reaches its window. Displaying on arrival ran the
        // subtitles early. Text and bitmap cues both flow through the same clock-gated path.
        const bool renderable = shifted.bitmap
            ? (shifted.width > 0 && shifted.height > 0 && !shifted.rgba.isEmpty())
            : !shifted.text.isEmpty();
        if (renderable) {
            m_cueBuffer.push_back(std::move(shifted));
            if (m_cueBuffer.size() > 1024)
                m_cueBuffer.erase(m_cueBuffer.begin());
        }
    });
    // The subtitle tick gates display on the playback clock (not decode-arrival). ~25Hz is well within
    // subtitle timing tolerance; it no-ops until the clock is valid, and pause freezes the clock so the
    // active cue simply holds. Replaces the old show-on-arrival + wall-clock clear timer.
    m_subtitleTick.setInterval(40);
    connect(&m_subtitleTick, &QTimer::timeout, this, &Player2Session::evaluateSubtitles);
    m_subtitleTick.start();
    connect(&m_demux, &DemuxSession::networkStateChanged, this,
            [this](quint64 generation, int stateValue) {
        if (!m_generation.accepts(generation))
            return;
        const NetworkState network = static_cast<NetworkState>(stateValue);
        // Publish the source's own truth alongside the state. The session deliberately does NOT
        // enter Buffering during a seek, so this flag is the only thing that can tell the chrome
        // "we are waiting on bytes" while it stays in Seeking. Every other network state clears it,
        // including Failed and Ended — the flag can never outlive the wait it describes.
        setNetworkStalled(network == NetworkState::Buffering || network == NetworkState::Recovering);
        const std::optional<Player2State> target =
            networkStateTarget(m_state.state(), network);
        if (!target)
            return;
        transition(*target);
        if (network == NetworkState::Failed)
            emit errorOccurred(Player2Error{Player2ErrorCode::NetworkFailed,
                                            QStringLiteral("Network stream failed"), true});
    });
}

Player2Session::~Player2Session()
{
    m_shuttingDown.store(true, std::memory_order_release); // abort any recovery in flight
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
bool Player2Session::networkStalled() const noexcept { return m_networkStalled; }
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
double Player2Session::speed() const noexcept { return m_speed; }
double Player2Session::normalizationLatencyMs() const
{
    return m_audioPipeline.normalizationLatencyUs() / 1000.0;
}
double Player2Session::subDelay() const noexcept { return m_subDelay; }
QString Player2Session::subtitleText() const { return m_subtitleText; }

QImage subtitleImageFromRgba(const QByteArray &rgba, int width, int height)
{
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<qsizetype>(width) * height * 4)
        return QImage();
    // The cue packs native-endian 0xAARRGGBB pixels — exactly QImage::Format_ARGB32. Wrap the
    // transient bytes, then deep-copy so the image outlives the cue buffer.
    return QImage(reinterpret_cast<const uchar *>(rgba.constData()), width, height,
                  QImage::Format_ARGB32)
        .copy();
}

qsizetype activeSubtitleCueIndex(const std::vector<SubtitleCue> &cues, qint64 nowUs)
{
    qsizetype found = -1;
    for (qsizetype i = 0; i < static_cast<qsizetype>(cues.size()); ++i)
        if (cues[i].startUs <= nowUs && nowUs < cues[i].endUs)
            found = i; // last match wins so a newer overlapping cue takes over
    return found;
}

// The ~25Hz heartbeat: show the cue whose window holds the playback clock, clear when none does, and
// drop cues fully in the past. No-ops until the clock is valid (no timeline yet / not playing).
void Player2Session::evaluateSubtitles()
{
    if (!m_playbackClock.valid())
        return;
    const qint64 nowUs = m_playbackClock.positionAt(sessionQpcNow());
    const qsizetype idx = activeSubtitleCueIndex(m_cueBuffer, nowUs);
    applyActiveSubtitle(idx >= 0 ? &m_cueBuffer[static_cast<size_t>(idx)] : nullptr);
    // Prune cues fully in the past (the active cue always has endUs > now, so it is never pruned).
    m_cueBuffer.erase(std::remove_if(m_cueBuffer.begin(), m_cueBuffer.end(),
                                     [nowUs](const SubtitleCue &c) { return c.endUs <= nowUs; }),
                      m_cueBuffer.end());
}

// Publish (or clear) the on-screen cue, skipping redundant re-emits while the same cue stays active.
void Player2Session::applyActiveSubtitle(const SubtitleCue *cue)
{
    if (!cue) {
        if (!m_subtitleText.isEmpty()) {
            m_subtitleText.clear();
            emit subtitleTextChanged();
        }
        clearSubtitleBitmap();
        m_hasActiveCue = false;
        return;
    }
    if (m_hasActiveCue && cue->startUs == m_activeCueStartUs && cue->bitmap == m_activeCueIsBitmap)
        return; // already showing this cue
    m_hasActiveCue = true;
    m_activeCueStartUs = cue->startUs;
    m_activeCueIsBitmap = cue->bitmap;
    if (cue->bitmap) {
        if (!m_subtitleText.isEmpty()) {
            m_subtitleText.clear();
            emit subtitleTextChanged();
        }
        QImage image = subtitleImageFromRgba(cue->rgba, cue->width, cue->height);
        {
            QMutexLocker locker(&m_subtitleImageMutex);
            m_subtitleImage = image;
            ++m_subtitleImageId;
        }
        m_subtitleBitmap = QVariantMap{
            {QStringLiteral("id"), m_subtitleImageId},
            {QStringLiteral("x"), cue->x},
            {QStringLiteral("y"), cue->y},
            {QStringLiteral("width"), cue->width},
            {QStringLiteral("height"), cue->height},
            {QStringLiteral("canvasWidth"), cue->canvasWidth},
            {QStringLiteral("canvasHeight"), cue->canvasHeight}};
        emit subtitleBitmapChanged();
    } else {
        clearSubtitleBitmap();
        m_subtitleText = cue->text;
        emit subtitleTextChanged();
    }
}

// Drop the painted cue and every buffered upcoming cue — used on seek, track switch and reset.
void Player2Session::flushSubtitles()
{
    m_cueBuffer.clear();
    applyActiveSubtitle(nullptr);
}

QVariantMap Player2Session::subtitleBitmap() const { return m_subtitleBitmap; }

QImage Player2Session::subtitleImageForProvider(const QString &id) const
{
    QMutexLocker locker(&m_subtitleImageMutex);
    if (id != QString::number(m_subtitleImageId))
        return QImage();
    return m_subtitleImage;
}

void Player2Session::clearSubtitleBitmap()
{
    if (m_subtitleBitmap.isEmpty())
        return;
    {
        QMutexLocker locker(&m_subtitleImageMutex);
        m_subtitleImage = QImage();
    }
    m_subtitleBitmap.clear();
    emit subtitleBitmapChanged();
}
double Player2Session::audioDelay() const noexcept { return m_audioDelay; }
QString Player2Session::videoAspect() const { return m_videoAspect; }
double Player2Session::panscan() const noexcept { return m_panscan; }
double Player2Session::videoZoom() const noexcept { return m_videoZoom; }
AudioClockSnapshot Player2Session::audioClock() const
{
    AudioClockSnapshot snapshot = m_audioPipeline.clock();
    if (!snapshot.isValidForGeneration(m_generation.current()))
        snapshot.valid = false;
    return snapshot;
}
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
    m_lastRequest = request; // remembered so recovery can reopen the same media at the saved position
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
    // Carry a non-default playback speed into the new session (default 1.0 needs no command).
    if (m_speed != 1.0)
        m_demux.requestSpeed(m_speed);
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
    // Pressed during a seek — including the long, deliberate wait on a torrent's not-yet-downloaded
    // bytes. Steer the seek's landing state instead of transitioning now. Seeking -> Playing IS a
    // legal transition, which is exactly the trap: taking it would start playback before
    // seekCompleted lands and race the seek's own completion. This is the same lever seekExact
    // uses, so a viewer who pauses and then changes their mind during the wait is still honoured.
    if (m_state.state() == Player2State::Seeking) {
        m_postSeekState = Player2State::Playing;
        return;
    }
    const bool wasPaused = m_state.state() == Player2State::Paused;
    if (!transition(Player2State::Playing))
        return;
    // Subtitles ride the playback clock via the tick, so a paused cue holds and resumes on its own —
    // there is no wall-clock remainder to restore here.
    if (wasPaused)
        m_demux.requestResume();
}

void Player2Session::pause()
{
    // Pressed during a seek. The transport shows a live pause button through a stalled seek (the
    // wait on a torrent's missing bytes), so dropping the press would mean a control that looks
    // active and ignores the viewer. Honour it through the seek's own lever: the seek completes
    // PAUSED. No Seeking -> Paused transition here, so nothing preempts seekCompleted.
    if (m_state.state() == Player2State::Seeking) {
        m_postSeekState = Player2State::Paused;
        return;
    }
    if (m_state.state() != Player2State::Playing && m_state.state() != Player2State::Buffering)
        return;
    if (!transition(Player2State::Paused))
        return;
    // The subtitle tick reads the playback clock, which freezes on pause — the active cue simply holds.
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
    m_seekToldResumePlaying = m_postSeekState == Player2State::Playing;
    m_demux.requestSeek(targetUs, next, m_seekToldResumePlaying);
}

void Player2Session::seekRelative(double seconds)
{
    seekExact(m_position + seconds);
}

void Player2Session::frameStep(int frames)
{
    if (!hasActiveMedia())
        return;
    // A frame step always resolves to a paused view of the target frame — and requestFrameStep
    // tells the worker exactly that, so the two already agree.
    m_postSeekState = Player2State::Paused;
    m_seekToldResumePlaying = false;
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

void Player2Session::selectSubtitleTrack(const QString &trackId)
{
    if (!hasActiveMedia())
        return;
    // Empty / "-1" / "off" disables subtitles; otherwise it is a stream index.
    int streamIndex = -1;
    if (!trackId.isEmpty() && trackId != QStringLiteral("off")) {
        bool ok = false;
        const int parsed = trackId.toInt(&ok);
        if (ok)
            streamIndex = parsed;
    }
    // Subtitles do not flush A/V, so no generation advance — the worker keeps the current epoch.
    m_demux.requestSelectSubtitleTrack(streamIndex);
}

void Player2Session::setSubDelay(double seconds)
{
    if (qFuzzyCompare(m_subDelay, seconds))
        return;
    m_subDelay = seconds;
    emit subDelayChanged();
}

void Player2Session::setAudioDelay(double seconds)
{
    if (qFuzzyCompare(m_audioDelay, seconds))
        return;
    m_audioDelay = seconds;
    m_demux.setAudioDelay(static_cast<qint64>(seconds * 1'000'000.0));
    emit audioDelayChanged();
}

void Player2Session::setVideoAspect(const QString &aspect)
{
    if (m_videoAspect == aspect)
        return;
    m_videoAspect = aspect;
    emit videoFillChanged();
}

void Player2Session::setPanscan(double value)
{
    if (qFuzzyCompare(m_panscan, value))
        return;
    m_panscan = value;
    emit videoFillChanged();
}

void Player2Session::setVideoZoom(double value)
{
    if (qFuzzyCompare(m_videoZoom, value))
        return;
    m_videoZoom = value;
    emit videoFillChanged();
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

void Player2Session::setSpeed(double speed)
{
    const double bounded = std::clamp(speed, 0.5, 2.0);
    if (m_speed == bounded) // presets are exact binary doubles (0.5/0.75/1.0/1.25/1.5/1.75/2.0)
        return;
    m_speed = bounded;
    emit speedChanged();
    // The tempo stage + clock rate are worker-owned; applied live through the command channel.
    m_demux.requestSpeed(bounded);
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
    // Nothing is waiting on bytes once playback is over. A terminal error raised by the DEMUX (not
    // by NetworkState::Failed) never clears the source's last Buffering, so measured against the
    // bytes-never-arrive fixture the flag sat true inside Error. Harmless on screen - the transport
    // gates its buffering read on Seeking - but a flag that outlives its wait is a trap for the next
    // reader. Clear it where the wait provably ended.
    if (next == Player2State::Error || next == Player2State::Ended || next == Player2State::Idle)
        setNetworkStalled(false);
    if (result.changed)
        emit stateChanged();
    return true;
}

void Player2Session::setNetworkStalled(bool stalled)
{
    if (m_networkStalled == stalled)
        return;
    m_networkStalled = stalled;
    emit networkStalledChanged();
}

void Player2Session::resetMediaProperties()
{
    // open() and close() both land here, so a stall never survives the media that produced it —
    // a local file opened after a stalled stream starts honestly un-stalled.
    setNetworkStalled(false);
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
    flushSubtitles();
}

void Player2Session::attemptDeviceRecovery(const Player2Error &error)
{
    if (!transition(Player2State::Recovering))
        return;
    const DeviceLostReason reason = error.code == Player2ErrorCode::AudioDeviceLost
        ? DeviceLostReason::AudioEndpointLost : DeviceLostReason::VideoDeviceRemoved;
    const RecoveryOutcome outcome = m_recovery.recover(reason, *this, m_shuttingDown);
    m_recoveryAttempts = outcome.attempts;
    if (outcome.recovered)
        return; // reopenAtSavedPosition re-armed the demux; state flows via the new opened signal
    transition(Player2State::Error);
    emit errorOccurred(Player2Error{outcome.terminalCode, outcome.message, false});
}

bool Player2Session::rebuildDevice(DeviceLostReason reason, QString *error)
{
    // In the isolated lab the D3D11 render device and the audio endpoint are owned by the host
    // (the Qt scene graph / the sink's default-endpoint lifecycle), so the engine cannot rebuild
    // them here yet. Report failure so the coordinator returns a typed terminal error rather than a
    // faked recovery. Wiring a real rebuild is a documented integration-time follow-on; the recovery
    // logic itself is proven by the injectable-target tests.
    if (error)
        *error = QStringLiteral("%1 rebuild requires the host device lifecycle")
                     .arg(deviceLostReasonName(reason));
    return false;
}

bool Player2Session::reopenAtSavedPosition(QString *error)
{
    if (m_shuttingDown.load(std::memory_order_acquire) || m_lastRequest.source.isEmpty()) {
        if (error)
            *error = QStringLiteral("no media to reopen");
        return false;
    }
    PlaybackRequest request = m_lastRequest;
    request.resumeSeconds = m_position;
    // Arm a new generation exactly as open() does: without flushing the pipeline/clock/scheduler to
    // `next` the ring keeps the old generation and rejects every recovered frame (frozen video).
    const quint64 next = m_generation.advance();
    if (m_videoPipeline)
        m_videoPipeline->flush(next);
    m_audioPipeline.flush(next);
    m_playbackClock.invalidate();
    m_frameScheduler.reset();
    emit generationChanged();
    if (!transition(Player2State::Opening)) {
        if (error)
            *error = QStringLiteral("could not re-enter Opening for recovery");
        return false;
    }
    m_demux.open(request, next);
    return true;
}

PlaybackDiagnostics Player2Session::diagnosticsSnapshot() const
{
    PlaybackDiagnostics snapshot;
    snapshot.state = QString::fromLatin1(
        QMetaEnum::fromType<Player2State>().valueToKey(static_cast<int>(m_state.state())));
    snapshot.position = m_position;
    snapshot.duration = m_duration;
    for (const QVariant &entry : m_tracks) {
        const QVariantMap track = entry.toMap();
        if (track.value(QStringLiteral("type")).toString() == QStringLiteral("video")) {
            snapshot.videoCodec = track.value(QStringLiteral("codec")).toString();
            break;
        }
    }
    snapshot.deviceLostReason = deviceLostReasonName(DeviceLostReason::None);
    if (m_videoPipeline) {
        const D3D11VideoPipeline::Diagnostics video = m_videoPipeline->diagnostics();
        snapshot.hardwareFormat = video.hardwareFormat;
        snapshot.inputFormat = video.inputFormat;
        snapshot.colorConversion = video.colorConversion;
        snapshot.adapter = video.qtAdapter;
        snapshot.adapterMatch = video.adapterMatch;
        snapshot.decoded = video.decoded;
        snapshot.submitted = video.submitted;
        snapshot.presented = video.presented;
        snapshot.dropped = video.producerStarved;
        snapshot.scheduledLateDrops = video.scheduledLateDrops;
        snapshot.cpuTransfers = video.cpuTransfers;
        snapshot.deviceErrors = video.deviceErrors;
        snapshot.avErrorMs = video.lastAvErrorUs / 1000.0;
        snapshot.avP95Ms = m_videoPipeline->schedulingP95AbsoluteErrorUs() / 1000.0;
        snapshot.videoDeviceLost = video.deviceLost;
        if (video.deviceLost)
            snapshot.deviceLostReason = deviceLostReasonName(m_videoPipeline->deviceLostReason());
    }
    snapshot.audioDevice = audioDevice();
    snapshot.audioFormat = audioFormat();
    snapshot.audioQueueMs = audioQueueMs();
    snapshot.audioUnderruns = audioUnderruns();
    snapshot.normalizationLatencyMs = normalizationLatencyMs();
    snapshot.recoveryAttempts = m_recoveryAttempts;
    return snapshot;
}

QVariantMap Player2Session::diagnostics() const
{
    return diagnosticsSnapshot().toJson().toVariantMap();
}

} // namespace Colosseum::Player2
