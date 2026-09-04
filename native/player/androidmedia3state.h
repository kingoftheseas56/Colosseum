#ifndef COLOSSEUM_ANDROIDMEDIA3STATE_H
#define COLOSSEUM_ANDROIDMEDIA3STATE_H

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

namespace Colosseum::Player {

enum class HostPlaybackAction {
    None,
    Pause,
    Resume,
};

struct AndroidMedia3Snapshot {
    QString currentUrl;
    QVariantMap headers;
    double positionSec = 0.0;
    double durationSec = 0.0;
    double bufferedSec = 0.0;
    double bufferingPercent = -1.0;
    int decodedWidth = 0;
    int decodedHeight = 0;
    bool paused = true;
    bool muted = false;
    bool seeking = false;
    bool ready = false;
    bool ended = false;
    bool errored = false;
    bool userWantsPlay = false;
    bool hostPlaybackSuppressed = false;
    bool hostPauseOwned = false;
    int volume = 100;
    double speed = 1.0;
    QString errorCode;
    QString errorMessage;
};

struct Media3TrackRow {
    QString id;
    QString title;
    QString lang;
    QString codec;
    QString channels;
    bool selected = false;
    bool external = false;
    bool forced = false;
    bool isDefault = false;
    bool hearingImpaired = false;
};

struct Media3ChapterRow {
    QString title;
    double startSec = 0.0;
};

struct Media3CueRow {
    QString text;
    double position = -1.0;
    double line = -1.0;
    double size = -1.0;
    QString alignment;
    QString positionAnchor;
    QString lineAnchor;
    int zIndex = 0;
    QVariantList spans;
    QVariantMap extras;
};

class AndroidMedia3State final
{
public:
    quint64 beginLoad(const QString &url, const QVariantMap &headers);
    quint64 generation() const { return m_generation; }
    bool accepts(quint64 generation) const;
    const AndroidMedia3Snapshot &snapshot() const { return m_snapshot; }
    QVariantMap capabilities() const;

    bool markReady(quint64 generation);
    bool noteVideoSize(quint64 generation, int width, int height);
    bool markFirstFrame(quint64 generation);
    bool resetVideoSurface();
    bool markEnded(quint64 generation);
    bool markError(quint64 generation, const QString &code, const QString &message);

    bool updateTimeline(quint64 generation, qint64 positionMs, qint64 durationMs,
                        qint64 bufferedMs, double bufferingPercent, bool paused,
                        int volume, bool muted, double speed);

    bool beginSeek(quint64 generation, qint64 targetMs, qint64 nowMs);
    bool noteSeekDiscontinuity(quint64 generation, qint64 acceptedPositionMs);
    bool notePlayerReady(quint64 generation, qint64 positionMs);
    bool expireSeek(qint64 nowMs);

    bool replaceTracks(quint64 generation, const QList<Media3TrackRow> &tracks);
    bool replaceChapters(quint64 generation, const QList<Media3ChapterRow> &chapters);
    bool replaceSubtitleCues(quint64 generation, const QList<Media3CueRow> &cues);
    const QVariantList &audioTracks() const { return m_audioTracks; }
    const QVariantList &subtitleTracks() const { return m_subtitleTracks; }
    const QVariantList &chapters() const { return m_chapters; }
    const QVariantList &subtitleCues() const { return m_subtitleCues; }

    bool noteUserPlay(quint64 generation);
    bool noteUserPause(quint64 generation);
    bool noteStopped(quint64 generation);
    bool noteTerminalAudioFocusLoss(quint64 generation);
    HostPlaybackAction setHostPlaybackSuppressed(quint64 generation, bool suppressed);

private:
    void resetSourceScopedState(const QString &url, const QVariantMap &headers);
    bool clearSeekIfSettled(qint64 currentPositionMs);
    void clearSeek();

    quint64 m_generation = 0;
    AndroidMedia3Snapshot m_snapshot;
    int m_pendingVideoWidth = 0;
    int m_pendingVideoHeight = 0;
    bool m_firstFramePublished = false;

    qint64 m_seekStartedAtMs = 0;
    qint64 m_seekAcceptedPositionMs = 0;
    bool m_seekDiscontinuitySeen = false;

    bool m_terminalResumeBlocked = false;
    QVariantList m_audioTracks;
    QVariantList m_subtitleTracks;
    QVariantList m_chapters;
    QVariantList m_subtitleCues;
};

} // namespace Colosseum::Player

#endif // COLOSSEUM_ANDROIDMEDIA3STATE_H
