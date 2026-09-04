#include "androidmedia3item.h"

#ifdef Q_OS_ANDROID

#include "androidmedia3videonode.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QJniEnvironment>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSGNode>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {
constexpr auto kMedia3HostClass = "org/colosseum/player/Media3PlayerHost";

QMutex g_media3ItemsMutex;
QHash<jlong, QPointer<AndroidMedia3Item>> g_media3Items;

void registerMedia3Item(jlong handle, AndroidMedia3Item *item)
{
    QMutexLocker locker(&g_media3ItemsMutex);
    g_media3Items.insert(handle, item);
}

void unregisterMedia3Item(jlong handle, AndroidMedia3Item *item)
{
    QMutexLocker locker(&g_media3ItemsMutex);
    const QPointer<AndroidMedia3Item> current = g_media3Items.value(handle);
    if (current == item)
        g_media3Items.remove(handle);
}

void postToMedia3Item(jlong handle, std::function<void(AndroidMedia3Item *)> callback)
{
    QPointer<AndroidMedia3Item> item;
    {
        QMutexLocker locker(&g_media3ItemsMutex);
        item = g_media3Items.value(handle);
    }
    if (!item)
        return;
    QMetaObject::invokeMethod(item, [item, callback = std::move(callback)]() mutable {
        if (item)
            callback(item);
    }, Qt::QueuedConnection);
}

QString fromJString(jstring value)
{
    return value ? QJniObject(value).toString() : QString();
}

QJniObject javaHeaders(const QVariantMap &headers)
{
    QJniObject map("java/util/HashMap", "()V");
    if (!map.isValid())
        return {};
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        const QJniObject key = QJniObject::fromString(it.key());
        const QJniObject value = QJniObject::fromString(it.value().toString());
        map.callObjectMethod("put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                             key.object<jobject>(), value.object<jobject>());
    }
    return map;
}

qint64 msFromSeconds(double value)
{
    if (!std::isfinite(value) || value <= 0.0)
        return 0;
    return qint64(value * 1000.0);
}

} // namespace

AndroidMedia3Item::AndroidMedia3Item(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    registerMedia3Item(nativeHandle(), this);

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        qWarning() << "Media3 PlayerItem could not obtain Android context";
        return;
    }
    m_host = QJniObject(kMedia3HostClass, "(Landroid/content/Context;J)V",
                        context.object<jobject>(), nativeHandle());
    if (!m_host.isValid())
        qWarning() << "Media3 PlayerItem could not construct Java host";
}

AndroidMedia3Item::~AndroidMedia3Item()
{
    unregisterMedia3Item(nativeHandle(), this);
    if (m_host.isValid())
        m_host.callMethod<void>("release");
    m_host = {};
}

jlong AndroidMedia3Item::nativeHandle() const
{
    return reinterpret_cast<jlong>(this);
}

QVariantMap AndroidMedia3Item::capabilities() const { return m_state.capabilities(); }
QString AndroidMedia3Item::mediaTitle() const { return m_mediaTitle; }
double AndroidMedia3Item::position() const { return m_state.snapshot().positionSec; }
void AndroidMedia3Item::setPosition(double value) { seekExact(value); }
double AndroidMedia3Item::duration() const { return m_state.snapshot().durationSec; }
bool AndroidMedia3Item::pause() const { return m_state.snapshot().paused; }
int AndroidMedia3Item::volume() const { return m_state.snapshot().volume; }
bool AndroidMedia3Item::mute() const { return m_state.snapshot().muted; }
double AndroidMedia3Item::speed() const { return m_state.snapshot().speed; }
QString AndroidMedia3Item::audioTrack() const { return m_audioTrack; }
QString AndroidMedia3Item::subtitleTrack() const { return m_subtitleTrack; }
QVariantList AndroidMedia3Item::audioTracks() const { return m_state.audioTracks(); }
QVariantList AndroidMedia3Item::subtitleTracks() const { return m_state.subtitleTracks(); }
QVariantList AndroidMedia3Item::chapters() const { return m_state.chapters(); }
QVariantList AndroidMedia3Item::subtitleCues() const { return m_state.subtitleCues(); }
double AndroidMedia3Item::audioDelay() const { return m_audioDelay; }
double AndroidMedia3Item::subDelay() const { return m_subDelay; }
double AndroidMedia3Item::panscan() const { return m_panscan; }
double AndroidMedia3Item::videoZoom() const { return m_videoZoom; }
QString AndroidMedia3Item::videoAspect() const { return m_videoAspect; }
QUrl AndroidMedia3Item::currentUrl() const { return QUrl(m_state.snapshot().currentUrl); }
int AndroidMedia3Item::decodedWidth() const { return m_state.snapshot().decodedWidth; }
int AndroidMedia3Item::decodedHeight() const { return m_state.snapshot().decodedHeight; }
double AndroidMedia3Item::cacheTime() const { return m_state.snapshot().bufferedSec; }
double AndroidMedia3Item::cacheBufferingState() const
{
    const double value = m_state.snapshot().bufferingPercent;
    return value < 0.0 ? 100.0 : value;
}

bool AndroidMedia3Item::coreSeeking() const { return m_state.snapshot().seeking; }
bool AndroidMedia3Item::gifEncoding() const { return false; }

QString AndroidMedia3Item::formatTime(double seconds) const
{
    const int total = std::max(0, int(seconds));
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;
    if (hours > 0)
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

QString AndroidMedia3Item::formattedPosition() const { return formatTime(position()); }
QString AndroidMedia3Item::formattedDuration() const { return formatTime(duration()); }

void AndroidMedia3Item::callHost(const char *method)
{
    if (m_host.isValid())
        m_host.callMethod<void>(method);
}

void AndroidMedia3Item::callHostSeek(qint64 positionMs)
{
    if (m_host.isValid())
        m_host.callMethod<void>("seekTo", "(J)V", jlong(std::max<qint64>(0, positionMs)));
}

void AndroidMedia3Item::callHostTrack(const char *method, const QString &trackId)
{
    if (!m_host.isValid())
        return;
    const QJniObject javaTrackId = QJniObject::fromString(trackId);
    m_host.callMethod<void>(method, "(Ljava/lang/String;)V", javaTrackId.object<jstring>());
}

void AndroidMedia3Item::publishSnapshotChanges(
        const Colosseum::Player::AndroidMedia3Snapshot &before)
{
    const auto &after = m_state.snapshot();
    if (before.currentUrl != after.currentUrl) emit currentUrlChanged();
    if (before.positionSec != after.positionSec) emit positionChanged();
    if (before.durationSec != after.durationSec) emit durationChanged();
    if (before.paused != after.paused) emit pauseChanged();
    if (before.volume != after.volume) emit volumeChanged();
    if (before.muted != after.muted) emit muteChanged();
    if (before.speed != after.speed) emit speedChanged();
    if (before.bufferedSec != after.bufferedSec) emit cacheTimeChanged();
    if (before.bufferingPercent != after.bufferingPercent) emit cacheBufferingStateChanged();
    if (before.seeking != after.seeking) emit coreSeekingChanged();
    if (before.decodedWidth != after.decodedWidth || before.decodedHeight != after.decodedHeight)
        emit decodedDimensionsChanged();
}

void AndroidMedia3Item::updateControlSnapshot(int newVolume, bool newMuted, double newSpeed)
{
    if (m_state.generation() == 0)
        return;
    const auto snapshot = m_state.snapshot();
    m_state.updateTimeline(m_state.generation(), msFromSeconds(snapshot.positionSec),
                           msFromSeconds(snapshot.durationSec), msFromSeconds(snapshot.bufferedSec),
                           snapshot.bufferingPercent, snapshot.paused,
                           newVolume, newMuted, newSpeed);
}

void AndroidMedia3Item::setPause(bool value)
{
    const quint64 generation = m_state.generation();
    if (generation == 0)
        return;
    const auto before = m_state.snapshot();
    if (value)
        m_state.noteUserPause(generation);
    else
        m_state.noteUserPlay(generation);
    publishSnapshotChanges(before);
    callHost(value ? "pause" : "play");
}

void AndroidMedia3Item::setVolume(int value)
{
    const int normalized = std::clamp(value, 0, 100);
    const auto before = m_state.snapshot();
    updateControlSnapshot(normalized, before.muted, before.speed);
    publishSnapshotChanges(before);
    if (m_host.isValid())
        m_host.callMethod<void>("setVolume", "(F)V", jfloat(normalized / 100.0f));
}

void AndroidMedia3Item::setMute(bool value)
{
    const auto before = m_state.snapshot();
    updateControlSnapshot(before.volume, value, before.speed);
    publishSnapshotChanges(before);
    if (m_host.isValid())
        m_host.callMethod<void>("setMuted", "(Z)V", jboolean(value));
}

void AndroidMedia3Item::setSpeed(double value)
{
    const double normalized = std::isfinite(value) && value > 0.0 ? value : 1.0;
    const auto before = m_state.snapshot();
    updateControlSnapshot(before.volume, before.muted, normalized);
    publishSnapshotChanges(before);
    if (m_host.isValid())
        m_host.callMethod<void>("setSpeed", "(F)V", jfloat(normalized));
}

void AndroidMedia3Item::setAudioTrack(const QString &value)
{
    if (m_audioTrack == value)
        return;
    callHostTrack("selectAudioTrack", value);
    m_audioTrack = value;
    emit audioTrackChanged();
}

void AndroidMedia3Item::setSubtitleTrack(const QString &value)
{
    if (m_subtitleTrack == value)
        return;
    callHostTrack("selectSubtitleTrack", value);
    m_subtitleTrack = value;
    emit subtitleTrackChanged();
}

void AndroidMedia3Item::setAudioDelay(double value)
{
    if (m_audioDelay == value) return;
    m_audioDelay = value;
    emit audioDelayChanged();
}

void AndroidMedia3Item::setSubDelay(double value)
{
    if (m_subDelay == value) return;
    m_subDelay = value;
    emit subDelayChanged();
}

void AndroidMedia3Item::setPanscan(double value)
{
    if (m_panscan == value) return;
    m_panscan = value;
    emit videoFillChanged();
}

void AndroidMedia3Item::setVideoZoom(double value)
{
    if (m_videoZoom == value) return;
    m_videoZoom = value;
    emit videoFillChanged();
}

void AndroidMedia3Item::setVideoAspect(const QString &value)
{
    if (m_videoAspect == value) return;
    m_videoAspect = value;
    emit videoFillChanged();
}

void AndroidMedia3Item::loadSource(const QString &url)
{
    loadSource(url, {});
}

void AndroidMedia3Item::loadSource(const QString &url, const QVariantMap &headers)
{
    const auto before = m_state.snapshot();
    const bool hadTracks = !m_state.audioTracks().isEmpty() || !m_state.subtitleTracks().isEmpty();
    const bool hadChapters = !m_state.chapters().isEmpty();
    const bool hadCues = !m_state.subtitleCues().isEmpty();
    const quint64 generation = m_state.beginLoad(url, headers);
    m_state.noteUserPlay(generation);
    m_fileLoadedEmitted = false;
    m_restoreLifecycleEpoch = 0;
    m_surfaceReleased = false;
    m_videoSize = {};
    publishSnapshotChanges(before);
    if (!m_mediaTitle.isEmpty()) {
        m_mediaTitle.clear();
        emit mediaTitleChanged();
    }
    if (!m_audioTrack.isEmpty()) { m_audioTrack.clear(); emit audioTrackChanged(); }
    if (!m_subtitleTrack.isEmpty()) { m_subtitleTrack.clear(); emit subtitleTrackChanged(); }
    if (hadTracks) emit trackListChanged();
    if (hadChapters) emit chaptersChanged();
    if (hadCues) emit subtitleCuesChanged();
    emit fileStarted();
    update();

    if (!m_host.isValid()) {
        const auto failedBefore = m_state.snapshot();
        if (m_state.markError(generation, QStringLiteral("source"),
                              QStringLiteral("Media3 host unavailable"))) {
            publishSnapshotChanges(failedBefore);
            emit playbackError(QStringLiteral("source"), QStringLiteral("Media3 host unavailable"));
            emit endFile(QStringLiteral("error"));
        }
        return;
    }

    const QJniObject javaUrl = QJniObject::fromString(url);
    const QJniObject headerMap = javaHeaders(headers);
    m_host.callMethod<void>("load", "(JLjava/lang/String;Ljava/util/Map;)V",
                            jlong(generation), javaUrl.object<jstring>(), headerMap.object<jobject>());
    callHost("play");
}

void AndroidMedia3Item::stopPlayback()
{
    if (m_state.generation() == 0)
        return;
    const auto before = m_state.snapshot();
    m_state.noteStopped(m_state.generation());
    m_restoreLifecycleEpoch = 0;
    publishSnapshotChanges(before);
    callHost("stopAndClear");
}

void AndroidMedia3Item::setHostLifecycleState(const QString &state)
{
    const QString normalized = state.trimmed().toLower();
    const quint64 generation = m_state.generation();
    if (generation == 0 || normalized == QStringLiteral("inactive"))
        return;

    if (normalized == QStringLiteral("hidden") || normalized == QStringLiteral("suspended")) {
        const auto before = m_state.snapshot();
        const auto action = m_state.beginLifecycleHostStop(generation);
        if (action == Colosseum::Player::HostPlaybackAction::Stop) {
            m_restoreLifecycleEpoch = m_state.snapshot().lifecycleEpoch;
            callHost("stopForLifecycle");
        }
        publishSnapshotChanges(before);
        return;
    }
    if (normalized != QStringLiteral("active") || !m_state.snapshot().hostStopped)
        return;

    const quint64 lifecycleEpoch = m_state.snapshot().lifecycleEpoch;
    const qint64 resumeMs = msFromSeconds(m_state.snapshot().positionSec);
    if (m_state.snapshot().currentUrl.isEmpty() || !m_host.isValid())
        return;
    if (!m_state.noteLifecyclePrepareSubmitted(generation, lifecycleEpoch))
        return;
    m_restoreLifecycleEpoch = lifecycleEpoch;
    callHost("prepareForLifecycleRestore");
    if (resumeMs > 0)
        callHostSeek(resumeMs);
}

void AndroidMedia3Item::setAudioFocusState(const QString &state)
{
    const QString normalized = state.trimmed().toLower();
    if (m_state.generation() == 0)
        return;
    if (normalized == QStringLiteral("loss") || normalized == QStringLiteral("lost")
            || normalized == QStringLiteral("terminal")) {
        const auto before = m_state.snapshot();
        m_state.noteTerminalAudioFocusLoss(m_state.generation());
        publishSnapshotChanges(before);
        callHost("pause");
    }
}

void AndroidMedia3Item::releaseVideoSurface()
{
    const auto before = m_state.snapshot();
    m_state.resetVideoSurface();
    m_surfaceReleased = true;
    publishSnapshotChanges(before);
    update();
}

void AndroidMedia3Item::restoreVideoSurface()
{
    if (!m_surfaceReleased)
        return;
    m_surfaceReleased = false;
    update();
}

void AndroidMedia3Item::applyPlaybackProfile() {}
void AndroidMedia3Item::refreshAudioOutput() {}
QVariant AndroidMedia3Item::playbackStat(const QString &name)
{
    Q_UNUSED(name)
    return {};
}

void AndroidMedia3Item::seekExact(double value)
{
    const quint64 generation = m_state.generation();
    if (generation == 0)
        return;
    const qint64 targetMs = msFromSeconds(value);
    const auto before = m_state.snapshot();
    if (!m_state.beginSeek(generation, targetMs, QDateTime::currentMSecsSinceEpoch()))
        return;
    publishSnapshotChanges(before);
    callHostSeek(targetMs);
}

void AndroidMedia3Item::seekStep(double delta)
{
    seekExact(position() + delta);
}

void AndroidMedia3Item::frameStep() {}
void AndroidMedia3Item::frameBackStep() {}

void AndroidMedia3Item::addSubtitle(const QString &url, const QString &title,
                                    const QString &lang, bool select)
{
    Q_UNUSED(url)
    Q_UNUSED(title)
    Q_UNUSED(lang)
    Q_UNUSED(select)
}
void AndroidMedia3Item::setSubOption(const QString &key, const QVariant &value)
{
    Q_UNUSED(key)
    Q_UNUSED(value)
}

void AndroidMedia3Item::setAudioNormalization(const QString &mode)
{
    Q_UNUSED(mode)
}

QString AndroidMedia3Item::captureFrame(const QString &title, const QString &subtitle)
{
    Q_UNUSED(title)
    Q_UNUSED(subtitle)
    return {};
}

void AndroidMedia3Item::revealCaptureFolder(const QString &path) { Q_UNUSED(path) }
bool AndroidMedia3Item::startGifRecording() { return false; }
void AndroidMedia3Item::stopGifRecording(const QString &title, const QString &subtitle)
{
    Q_UNUSED(title)
    Q_UNUSED(subtitle)
}
void AndroidMedia3Item::abortGifRecording() {}

namespace {
QList<Colosseum::Player::Media3TrackRow> parseTracks(const QString &json)
{
    QList<Colosseum::Player::Media3TrackRow> rows;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject())
        return rows;
    const QJsonArray groups = document.object().value(QStringLiteral("groups")).toArray();
    for (qsizetype groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        const QJsonObject group = groups.at(groupIndex).toObject();
        const int type = group.value(QStringLiteral("type")).toInt();
        const QString prefix = type == 1 ? QStringLiteral("a")
                              : type == 3 ? QStringLiteral("s") : QString();
        if (prefix.isEmpty())
            continue;
        const QJsonArray tracks = group.value(QStringLiteral("tracks")).toArray();
        for (qsizetype trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
            const QJsonObject track = tracks.at(trackIndex).toObject();
            Colosseum::Player::Media3TrackRow row;
            row.id = QStringLiteral("%1:%2:%3").arg(prefix).arg(groupIndex).arg(trackIndex);
            row.title = track.value(QStringLiteral("label")).toString();
            row.lang = track.value(QStringLiteral("language")).toString();
            row.codec = track.value(QStringLiteral("mimeType")).toString();
            row.selected = track.value(QStringLiteral("selected")).toBool();
            rows.append(std::move(row));
        }
    }
    return rows;
}
QList<Colosseum::Player::Media3ChapterRow> parseChapters(const QString &json)
{
    QList<Colosseum::Player::Media3ChapterRow> rows;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject())
        return rows;
    const QJsonArray chapters = document.object().value(QStringLiteral("chapters")).toArray();
    rows.reserve(chapters.size());
    for (const QJsonValue &value : chapters) {
        const QJsonObject chapter = value.toObject();
        Colosseum::Player::Media3ChapterRow row;
        row.title = chapter.value(QStringLiteral("title")).toString();
        row.startSec = std::max(0.0, chapter.value(QStringLiteral("startMs")).toDouble() / 1000.0);
        rows.append(std::move(row));
    }
    return rows;
}

QList<Colosseum::Player::Media3CueRow> parseCues(const QString &json)
{
    QList<Colosseum::Player::Media3CueRow> rows;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject())
        return rows;
    const QJsonArray cues = document.object().value(QStringLiteral("cues")).toArray();
    rows.reserve(cues.size());
    for (const QJsonValue &value : cues) {
        const QJsonObject cue = value.toObject();
        Colosseum::Player::Media3CueRow row;
        row.text = cue.value(QStringLiteral("text")).toString();
        rows.append(std::move(row));
    }
    return rows;
}

QString metadataTitle(const QString &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    return document.isObject()
            ? document.object().value(QStringLiteral("title")).toString().trimmed()
            : QString();
}
} // namespace

void AndroidMedia3Item::handlePlaybackSnapshot(
        quint64 generation, qint64 positionMs, qint64 durationMs,
        qint64 bufferedPositionMs, double bufferedPercentage, bool paused,
        double requestedVolume, bool muted, double speed)
{
    if (!m_state.accepts(generation))
        return;
    const auto before = m_state.snapshot();
    const int volume = std::clamp(int(std::lround(requestedVolume * 100.0)), 0, 100);
    if (!m_state.updateTimeline(generation, positionMs, durationMs, bufferedPositionMs,
                                bufferedPercentage, paused, volume, muted, speed))
        return;
    publishSnapshotChanges(before);
}

void AndroidMedia3Item::handleReady(quint64 generation, qint64 durationMs,
                                    qint64 positionMs, bool playWhenReady)
{
    if (!m_state.accepts(generation))
        return;
    const auto before = m_state.snapshot();
    m_state.markReady(generation);
    m_state.notePlayerReady(generation, positionMs);
    const auto snapshot = m_state.snapshot();
    m_state.updateTimeline(generation, positionMs, durationMs,
                           msFromSeconds(snapshot.bufferedSec), snapshot.bufferingPercent,
                           !playWhenReady, snapshot.volume, snapshot.muted, snapshot.speed);

    Colosseum::Player::HostPlaybackAction lifecycleAction =
            Colosseum::Player::HostPlaybackAction::None;
    if (m_restoreLifecycleEpoch != 0)
        lifecycleAction = m_state.noteLifecycleReady(generation, m_restoreLifecycleEpoch);
    if (!m_state.snapshot().hostStopped)
        m_restoreLifecycleEpoch = 0;
    publishSnapshotChanges(before);

    if (!m_fileLoadedEmitted) {
        m_fileLoadedEmitted = true;
        emit fileLoaded();
    }
    if (lifecycleAction == Colosseum::Player::HostPlaybackAction::Resume)
        callHost("play");
}

void AndroidMedia3Item::handleEnded(quint64 generation)
{
    if (!m_state.accepts(generation))
        return;
    const auto before = m_state.snapshot();
    if (!m_state.markEnded(generation))
        return;
    publishSnapshotChanges(before);
    emit endFile(QStringLiteral("eof"));
}

void AndroidMedia3Item::handleError(quint64 generation, const QString &family,
                                    const QString &code, const QString &message)
{
    if (!m_state.accepts(generation))
        return;
    if (m_restoreLifecycleEpoch != 0
            && m_state.shouldSuppressLifecycleError(generation, m_restoreLifecycleEpoch, family)) {
        qInfo() << "Suppressing Media3 lifecycle teardown error" << family << code;
        return;
    }
    const auto before = m_state.snapshot();
    if (!m_state.markError(generation, code, message))
        return;
    m_restoreLifecycleEpoch = 0;
    publishSnapshotChanges(before);
    emit playbackError(code, message);
    emit endFile(QStringLiteral("error"));
}

void AndroidMedia3Item::handleTimeline(quint64 generation, qint64 durationMs,
                                       bool seekable, bool live)
{
    Q_UNUSED(seekable)
    Q_UNUSED(live)
    if (!m_state.accepts(generation))
        return;
    const auto before = m_state.snapshot();
    const auto snapshot = m_state.snapshot();
    m_state.updateTimeline(generation, msFromSeconds(snapshot.positionSec), durationMs,
                           msFromSeconds(snapshot.bufferedSec), snapshot.bufferingPercent,
                           snapshot.paused, snapshot.volume, snapshot.muted, snapshot.speed);
    publishSnapshotChanges(before);
}

void AndroidMedia3Item::handleVideoSize(quint64 generation, int width, int height,
                                        double pixelRatio)
{
    if (!m_state.accepts(generation))
        return;
    const auto before = m_state.snapshot();
    if (!m_state.noteVideoSize(generation, width, height))
        return;
    const double ratio = std::isfinite(pixelRatio) && pixelRatio > 0.0 ? pixelRatio : 1.0;
    m_videoSize = QSize(std::max(0, int(std::lround(width * ratio))), std::max(0, height));
    publishSnapshotChanges(before);
    emit videoReconfig();
    update();
}

void AndroidMedia3Item::handleFirstFrame(quint64 generation)
{
    if (!m_state.accepts(generation))
        return;
    const auto before = m_state.snapshot();
    if (!m_state.markFirstFrame(generation))
        return;
    publishSnapshotChanges(before);
    emit videoReconfig();
}

void AndroidMedia3Item::handleSeekDiscontinuity(quint64 generation,
                                                qint64 oldPositionMs,
                                                qint64 newPositionMs)
{
    Q_UNUSED(oldPositionMs)
    if (!m_state.accepts(generation))
        return;
    const auto before = m_state.snapshot();
    m_state.noteSeekDiscontinuity(generation, newPositionMs);
    publishSnapshotChanges(before);
}

void AndroidMedia3Item::handleTracks(quint64 generation, const QString &json)
{
    if (!m_state.replaceTracks(generation, parseTracks(json)))
        return;
    emit trackListChanged();

    QString selectedAudio;
    for (const QVariant &entry : m_state.audioTracks()) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("selected")).toBool()) {
            selectedAudio = map.value(QStringLiteral("id")).toString();
            break;
        }
    }
    if (m_audioTrack != selectedAudio) {
        m_audioTrack = selectedAudio;
        emit audioTrackChanged();
    }

    QString selectedSubtitle;
    for (const QVariant &entry : m_state.subtitleTracks()) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("selected")).toBool()) {
            selectedSubtitle = map.value(QStringLiteral("id")).toString();
            break;
        }
    }
    if (m_subtitleTrack != selectedSubtitle) {
        m_subtitleTrack = selectedSubtitle;
        emit subtitleTrackChanged();
    }
}

void AndroidMedia3Item::handleChapters(quint64 generation, const QString &json)
{
    if (!m_state.replaceChapters(generation, parseChapters(json)))
        return;
    emit chaptersChanged();
}

void AndroidMedia3Item::handleMetadata(quint64 generation, const QString &json)
{
    if (!m_state.accepts(generation))
        return;
    const QString title = metadataTitle(json);
    if (m_mediaTitle == title)
        return;
    m_mediaTitle = title;
    emit mediaTitleChanged();
}

void AndroidMedia3Item::handleCues(quint64 generation, const QString &json)
{
    if (!m_state.replaceSubtitleCues(generation, parseCues(json)))
        return;
    emit subtitleCuesChanged();
}

QSGNode *AndroidMedia3Item::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data)
{
    Q_UNUSED(data)
    if (m_surfaceReleased) {
        delete oldNode;
        return nullptr;
    }

    auto *node = static_cast<AndroidMedia3VideoNode *>(oldNode);
    if (!node) {
        const jlong handle = nativeHandle();
        QJniObject host = m_host;
        node = new AndroidMedia3VideoNode(
            handle,
            [handle]() {
                postToMedia3Item(handle, [](AndroidMedia3Item *item) { item->update(); });
            },
            [host](quint64 surfaceGeneration, const QJniObject &surface) mutable {
                Q_UNUSED(surfaceGeneration)
                if (host.isValid() && surface.isValid()) {
                    host.callMethod<void>("setVideoSurface", "(Landroid/view/Surface;)V",
                                          surface.object<jobject>());
                }
            },
            [host](quint64 surfaceGeneration, const QJniObject &surface) mutable {
                Q_UNUSED(surfaceGeneration)
                if (host.isValid() && surface.isValid()) {
                    host.callMethod<void>("clearVideoSurface", "(Landroid/view/Surface;)V",
                                          surface.object<jobject>());
                }
            });
    }
    node->setTargetRect(QRectF(0.0, 0.0, width(), height()));
    node->setVideoSize(m_videoSize);
    return node;
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnPlaybackSnapshot(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation,
        jlong positionMs, jlong durationMs, jlong bufferedPositionMs,
        jdouble bufferedPercentage, jboolean paused, jfloat requestedVolume,
        jboolean muted, jdouble speed)
{
    if (generation <= 0)
        return;
    postToMedia3Item(nativeHandle,
        [generation, positionMs, durationMs, bufferedPositionMs, bufferedPercentage,
         paused, requestedVolume, muted, speed](AndroidMedia3Item *item) {
            item->handlePlaybackSnapshot(
                quint64(generation), qint64(positionMs), qint64(durationMs),
                qint64(bufferedPositionMs), double(bufferedPercentage), paused == JNI_TRUE,
                double(requestedVolume), muted == JNI_TRUE, double(speed));
        });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnReady(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation,
        jlong durationMs, jlong positionMs, jboolean playWhenReady)
{
    if (generation <= 0)
        return;
    postToMedia3Item(nativeHandle,
        [generation, durationMs, positionMs, playWhenReady](AndroidMedia3Item *item) {
            item->handleReady(quint64(generation), qint64(durationMs), qint64(positionMs),
                              playWhenReady == JNI_TRUE);
        });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnEnded(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation)
{
    if (generation <= 0)
        return;
    postToMedia3Item(nativeHandle, [generation](AndroidMedia3Item *item) {
        item->handleEnded(quint64(generation));
    });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnError(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation,
        jstring family, jstring code, jstring message)
{
    if (generation <= 0)
        return;
    const QString familyValue = fromJString(family);
    const QString codeValue = fromJString(code);
    const QString messageValue = fromJString(message);
    postToMedia3Item(nativeHandle,
        [generation, familyValue, codeValue, messageValue](AndroidMedia3Item *item) {
            item->handleError(quint64(generation), familyValue, codeValue, messageValue);
        });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnTimeline(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation,
        jlong durationMs, jboolean seekable, jboolean live)
{
    if (generation <= 0)
        return;
    postToMedia3Item(nativeHandle,
        [generation, durationMs, seekable, live](AndroidMedia3Item *item) {
            item->handleTimeline(quint64(generation), qint64(durationMs),
                                 seekable == JNI_TRUE, live == JNI_TRUE);
        });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnVideoSize(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation,
        jint width, jint height, jfloat pixelRatio)
{
    if (generation <= 0)
        return;
    postToMedia3Item(nativeHandle,
        [generation, width, height, pixelRatio](AndroidMedia3Item *item) {
            item->handleVideoSize(quint64(generation), int(width), int(height), double(pixelRatio));
        });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnFirstFrame(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation)
{
    if (generation <= 0)
        return;
    postToMedia3Item(nativeHandle, [generation](AndroidMedia3Item *item) {
        item->handleFirstFrame(quint64(generation));
    });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnSeekDiscontinuity(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation,
        jlong oldPositionMs, jlong newPositionMs)
{
    if (generation <= 0)
        return;
    postToMedia3Item(nativeHandle,
        [generation, oldPositionMs, newPositionMs](AndroidMedia3Item *item) {
            item->handleSeekDiscontinuity(quint64(generation), qint64(oldPositionMs),
                                          qint64(newPositionMs));
        });
}
extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnTracks(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation, jstring tracksJson)
{
    if (generation <= 0)
        return;
    const QString json = fromJString(tracksJson);
    postToMedia3Item(nativeHandle, [generation, json](AndroidMedia3Item *item) {
        item->handleTracks(quint64(generation), json);
    });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnChapters(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation, jstring chaptersJson)
{
    if (generation <= 0)
        return;
    const QString json = fromJString(chaptersJson);
    postToMedia3Item(nativeHandle, [generation, json](AndroidMedia3Item *item) {
        item->handleChapters(quint64(generation), json);
    });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnMetadata(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation, jstring metadataJson)
{
    if (generation <= 0)
        return;
    const QString json = fromJString(metadataJson);
    postToMedia3Item(nativeHandle, [generation, json](AndroidMedia3Item *item) {
        item->handleMetadata(quint64(generation), json);
    });
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_player_Media3PlayerHost_nativeOnCues(
        JNIEnv *, jclass, jlong nativeHandle, jlong generation, jstring cuesJson)
{
    if (generation <= 0)
        return;
    const QString json = fromJString(cuesJson);
    postToMedia3Item(nativeHandle, [generation, json](AndroidMedia3Item *item) {
        item->handleCues(quint64(generation), json);
    });
}

#endif // Q_OS_ANDROID
