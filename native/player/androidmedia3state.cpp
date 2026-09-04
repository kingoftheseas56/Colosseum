#include "androidmedia3state.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Colosseum::Player {
namespace {

double secondsFromMs(qint64 value)
{
    return std::max<qint64>(0, value) / 1000.0;
}

double normalizedPercent(double value)
{
    if (!std::isfinite(value) || value < 0.0)
        return -1.0;
    return std::clamp(value, 0.0, 100.0);
}

double normalizedSpeed(double value)
{
    return std::isfinite(value) && value > 0.0 ? value : 1.0;
}

double normalizedFinite(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

QVariantMap trackToMap(const Media3TrackRow &row, bool audio)
{
    QVariantMap out;
    const QString fallback = audio ? QStringLiteral("Audio track") : QStringLiteral("Subtitle");
    const QString title = row.title.trimmed().isEmpty() ? fallback : row.title.trimmed();
    out.insert(QStringLiteral("id"), row.id);
    out.insert(QStringLiteral("title"), title);
    out.insert(QStringLiteral("label"), title);
    out.insert(QStringLiteral("lang"), row.lang);
    out.insert(QStringLiteral("codec"), row.codec);
    out.insert(QStringLiteral("channels"), row.channels);
    out.insert(QStringLiteral("selected"), row.selected);
    out.insert(QStringLiteral("external"), row.external);
    out.insert(QStringLiteral("forced"), row.forced);
    out.insert(QStringLiteral("default"), row.isDefault);
    out.insert(QStringLiteral("hearingImpaired"), row.hearingImpaired);
    return out;
}

bool isTerminal(const AndroidMedia3Snapshot &snapshot)
{
    return snapshot.ended || snapshot.errored;
}

} // namespace

quint64 AndroidMedia3State::beginLoad(const QString &url, const QVariantMap &headers)
{
    if (m_generation == std::numeric_limits<quint64>::max())
        qFatal("AndroidMedia3State source generation exhausted");
    ++m_generation;
    resetSourceScopedState(url, headers);
    return m_generation;
}

void AndroidMedia3State::resetSourceScopedState(const QString &url, const QVariantMap &headers)
{
    m_snapshot = AndroidMedia3Snapshot{};
    m_snapshot.currentUrl = url;
    m_snapshot.headers = headers;
    m_pendingVideoWidth = 0;
    m_pendingVideoHeight = 0;
    m_firstFramePublished = false;
    clearSeek();
    m_terminalResumeBlocked = false;
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    m_chapters.clear();
    m_subtitleCues.clear();
}

bool AndroidMedia3State::accepts(quint64 generation) const
{
    return generation != 0 && generation == m_generation;
}

QVariantMap AndroidMedia3State::capabilities() const
{
    QVariantMap caps;
    caps.insert(QStringLiteral("frameStepping"), false);
    caps.insert(QStringLiteral("frameCapture"), false);
    caps.insert(QStringLiteral("gifCapture"), false);
    caps.insert(QStringLiteral("audioDelay"), false);
    caps.insert(QStringLiteral("subtitleDelay"), false);
    caps.insert(QStringLiteral("videoTransform"), false);
    caps.insert(QStringLiteral("loudnessNormalization"), false);
    caps.insert(QStringLiteral("playbackStats"), false);
    caps.insert(QStringLiteral("subtitleStyling"), false);
    caps.insert(QStringLiteral("audioOutputRefresh"), false);
    caps.insert(QStringLiteral("pictureInPicture"), false);
    caps.insert(QStringLiteral("subtitleCueOverlay"), true);
    return caps;
}

bool AndroidMedia3State::markReady(quint64 generation)
{
    if (!accepts(generation) || isTerminal(m_snapshot) || m_snapshot.ready)
        return false;
    m_snapshot.ready = true;
    return true;
}

bool AndroidMedia3State::noteVideoSize(quint64 generation, int width, int height)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    m_pendingVideoWidth = std::max(0, width);
    m_pendingVideoHeight = std::max(0, height);
    if (m_firstFramePublished) {
        m_snapshot.decodedWidth = m_pendingVideoWidth;
        m_snapshot.decodedHeight = m_pendingVideoHeight;
    }
    return true;
}

bool AndroidMedia3State::markFirstFrame(quint64 generation)
{
    if (!accepts(generation) || isTerminal(m_snapshot) || m_firstFramePublished)
        return false;
    m_firstFramePublished = true;
    m_snapshot.decodedWidth = m_pendingVideoWidth;
    m_snapshot.decodedHeight = m_pendingVideoHeight;
    return true;
}

bool AndroidMedia3State::resetVideoSurface()
{
    const bool changed = m_firstFramePublished || m_snapshot.decodedWidth != 0
            || m_snapshot.decodedHeight != 0;
    m_firstFramePublished = false;
    m_snapshot.decodedWidth = 0;
    m_snapshot.decodedHeight = 0;
    return changed;
}

bool AndroidMedia3State::markEnded(quint64 generation)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    m_snapshot.ended = true;
    m_snapshot.paused = true;
    m_snapshot.userWantsPlay = false;
    m_snapshot.hostPauseOwned = false;
    clearSeek();
    return true;
}

bool AndroidMedia3State::markError(quint64 generation, const QString &code, const QString &message)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    m_snapshot.errored = true;
    m_snapshot.errorCode = code;
    m_snapshot.errorMessage = message;
    m_snapshot.paused = true;
    m_snapshot.userWantsPlay = false;
    m_snapshot.hostPauseOwned = false;
    clearSeek();
    return true;
}

bool AndroidMedia3State::updateTimeline(quint64 generation, qint64 positionMs,
                                        qint64 durationMs, qint64 bufferedMs,
                                        double bufferingPercent, bool paused,
                                        int volume, bool muted, double speed)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    m_snapshot.positionSec = secondsFromMs(positionMs);
    m_snapshot.durationSec = secondsFromMs(durationMs);
    m_snapshot.bufferedSec = secondsFromMs(bufferedMs);
    if (m_snapshot.durationSec > 0.0)
        m_snapshot.bufferedSec = std::min(m_snapshot.bufferedSec, m_snapshot.durationSec);
    m_snapshot.bufferingPercent = normalizedPercent(bufferingPercent);
    m_snapshot.paused = paused;
    m_snapshot.volume = std::clamp(volume, 0, 100);
    m_snapshot.muted = muted;
    m_snapshot.speed = normalizedSpeed(speed);
    clearSeekIfSettled(std::max<qint64>(0, positionMs));
    return true;
}

bool AndroidMedia3State::beginSeek(quint64 generation, qint64 targetMs, qint64 nowMs)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    qint64 clampedTarget = std::max<qint64>(0, targetMs);
    if (m_snapshot.durationSec > 0.0) {
        const qint64 durationMs = static_cast<qint64>(m_snapshot.durationSec * 1000.0);
        clampedTarget = std::min(clampedTarget, durationMs);
    }
    m_snapshot.seeking = true;
    m_seekStartedAtMs = nowMs;
    m_seekAcceptedPositionMs = clampedTarget;
    m_seekDiscontinuitySeen = false;
    return true;
}

bool AndroidMedia3State::noteSeekDiscontinuity(quint64 generation, qint64 acceptedPositionMs)
{
    if (!accepts(generation) || isTerminal(m_snapshot) || !m_snapshot.seeking)
        return false;
    m_seekAcceptedPositionMs = std::max<qint64>(0, acceptedPositionMs);
    m_seekDiscontinuitySeen = true;
    return true;
}

bool AndroidMedia3State::notePlayerReady(quint64 generation, qint64 positionMs)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    m_snapshot.positionSec = secondsFromMs(positionMs);
    if (m_snapshot.seeking && m_seekDiscontinuitySeen)
        clearSeek();
    return true;
}

bool AndroidMedia3State::expireSeek(qint64 nowMs)
{
    if (!m_snapshot.seeking || nowMs < m_seekStartedAtMs)
        return false;
    if (nowMs - m_seekStartedAtMs < 3000)
        return false;
    clearSeek();
    return true;
}

bool AndroidMedia3State::clearSeekIfSettled(qint64 currentPositionMs)
{
    if (!m_snapshot.seeking || !m_seekDiscontinuitySeen)
        return false;
    if (qAbs(currentPositionMs - m_seekAcceptedPositionMs) > 500)
        return false;
    clearSeek();
    return true;
}

void AndroidMedia3State::clearSeek()
{
    m_snapshot.seeking = false;
    m_seekStartedAtMs = 0;
    m_seekAcceptedPositionMs = 0;
    m_seekDiscontinuitySeen = false;
}

bool AndroidMedia3State::replaceTracks(quint64 generation, const QList<Media3TrackRow> &tracks)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    QVariantList audio;
    QVariantList subtitles;
    for (const Media3TrackRow &row : tracks) {
        if (row.id.startsWith(QStringLiteral("a:")))
            audio.append(trackToMap(row, true));
        else if (row.id.startsWith(QStringLiteral("s:")))
            subtitles.append(trackToMap(row, false));
    }
    m_audioTracks = std::move(audio);
    m_subtitleTracks = std::move(subtitles);
    return true;
}

bool AndroidMedia3State::replaceChapters(quint64 generation, const QList<Media3ChapterRow> &chapters)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;

    struct NormalizedChapter {
        QString title;
        double startSec = 0.0;
    };
    QList<NormalizedChapter> normalized;
    normalized.reserve(chapters.size());
    for (const Media3ChapterRow &row : chapters) {
        const QString title = row.title.trimmed().isEmpty() ? QStringLiteral("Chapter") : row.title.trimmed();
        const double start = std::max(0.0, normalizedFinite(row.startSec, 0.0));
        normalized.append({title, start});
    }
    std::sort(normalized.begin(), normalized.end(), [](const NormalizedChapter &a, const NormalizedChapter &b) {
        if (a.startSec != b.startSec)
            return a.startSec < b.startSec;
        return a.title < b.title;
    });

    QVariantList out;
    QString lastTitle;
    double lastStart = -1.0;
    bool haveLast = false;
    for (const NormalizedChapter &row : normalized) {
        if (haveLast && row.title == lastTitle && qAbs(row.startSec - lastStart) < 0.000001)
            continue;
        QVariantMap map;
        map.insert(QStringLiteral("title"), row.title);
        map.insert(QStringLiteral("startSec"), row.startSec);
        out.append(map);
        lastTitle = row.title;
        lastStart = row.startSec;
        haveLast = true;
    }
    m_chapters = std::move(out);
    return true;
}

bool AndroidMedia3State::replaceSubtitleCues(quint64 generation, const QList<Media3CueRow> &cues)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    QVariantList out;
    out.reserve(cues.size());
    for (const Media3CueRow &row : cues) {
        QVariantMap map = row.extras;
        map.insert(QStringLiteral("text"), row.text);
        const double position = normalizedFinite(row.position, -1.0);
        map.insert(QStringLiteral("position"), position < 0.0 ? -1.0 : std::clamp(position, 0.0, 1.0));
        map.insert(QStringLiteral("line"), normalizedFinite(row.line, -1.0));
        const double size = normalizedFinite(row.size, -1.0);
        map.insert(QStringLiteral("size"), size < 0.0 ? -1.0 : std::clamp(size, 0.0, 1.0));
        map.insert(QStringLiteral("alignment"), row.alignment);
        map.insert(QStringLiteral("positionAnchor"), row.positionAnchor);
        map.insert(QStringLiteral("lineAnchor"), row.lineAnchor);
        map.insert(QStringLiteral("zIndex"), row.zIndex);
        map.insert(QStringLiteral("spans"), row.spans);
        out.append(map);
    }
    m_subtitleCues = std::move(out);
    return true;
}

bool AndroidMedia3State::noteUserPlay(quint64 generation)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    m_snapshot.userWantsPlay = true;
    m_terminalResumeBlocked = false;
    if (m_snapshot.hostPlaybackSuppressed) {
        m_snapshot.hostPauseOwned = true;
        m_snapshot.paused = true;
    } else {
        m_snapshot.paused = false;
    }
    return true;
}

bool AndroidMedia3State::noteUserPause(quint64 generation)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    m_snapshot.userWantsPlay = false;
    m_snapshot.hostPauseOwned = false;
    m_snapshot.paused = true;
    return true;
}

bool AndroidMedia3State::noteStopped(quint64 generation)
{
    if (!accepts(generation))
        return false;
    m_snapshot.userWantsPlay = false;
    m_snapshot.hostPauseOwned = false;
    m_snapshot.hostPlaybackSuppressed = false;
    m_snapshot.paused = true;
    m_terminalResumeBlocked = true;
    clearSeek();
    return true;
}

bool AndroidMedia3State::noteTerminalAudioFocusLoss(quint64 generation)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return false;
    m_snapshot.hostPauseOwned = false;
    m_snapshot.paused = true;
    m_terminalResumeBlocked = true;
    return true;
}

HostPlaybackAction AndroidMedia3State::setHostPlaybackSuppressed(quint64 generation, bool suppressed)
{
    if (!accepts(generation) || isTerminal(m_snapshot))
        return HostPlaybackAction::None;
    if (m_snapshot.hostPlaybackSuppressed == suppressed)
        return HostPlaybackAction::None;

    m_snapshot.hostPlaybackSuppressed = suppressed;
    if (suppressed) {
        if (m_snapshot.userWantsPlay && !m_snapshot.paused) {
            m_snapshot.hostPauseOwned = true;
            m_snapshot.paused = true;
            return HostPlaybackAction::Pause;
        }
        return HostPlaybackAction::None;
    }

    const bool mayResume = m_snapshot.hostPauseOwned
            && m_snapshot.userWantsPlay
            && !m_terminalResumeBlocked;
    m_snapshot.hostPauseOwned = false;
    if (!mayResume)
        return HostPlaybackAction::None;
    m_snapshot.paused = false;
    return HostPlaybackAction::Resume;
}

} // namespace Colosseum::Player
