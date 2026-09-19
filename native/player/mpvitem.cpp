// MpvItem implementation — lifted 1:1 from KDE mpvqt's video-player example.
#include "mpvitem.h"

#include <MpvController>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QStandardPaths>
#include <QUrl>
#include <QtMath>
#include <QEvent>
#include <QQuickWindow>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>

#include "mpvproperties.h"
#include "http_header_fields.h"

namespace {
// Player 1.5 wid mode: the item owns NO video — mpv paints the native window underneath the
// translucent Qt window. The FBO still exists (QQuickFramebufferObject contract) but only
// needs to clear to transparent; mpv's render context is never created, so mpv never enters
// the Qt scene's frame budget at all.
class EmptyPresentationRenderer : public QQuickFramebufferObject::Renderer {
public:
    void render() override
    {
        QOpenGLFunctions gl;
        gl.initializeOpenGLFunctions();
        gl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl.glClear(GL_COLOR_BUFFER_BIT);
    }
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        return new QOpenGLFramebufferObject(size, format);
    }
};
} // namespace

QQuickFramebufferObject::Renderer *MpvItem::createRenderer() const
{
    if (widPresentationEnabled())
        return new EmptyPresentationRenderer;
    return MpvAbstractItem::createRenderer();
}

MpvItem::MpvItem(QQuickItem *parent)
    : MpvAbstractItem(parent)
{
    observeProperty(MpvProperties::self()->MediaTitle, MPV_FORMAT_STRING);
    observeProperty(MpvProperties::self()->Position, MPV_FORMAT_DOUBLE);
    observeProperty(MpvProperties::self()->Duration, MPV_FORMAT_DOUBLE);
    observeProperty(MpvProperties::self()->Pause, MPV_FORMAT_FLAG);
    observeProperty(MpvProperties::self()->Volume, MPV_FORMAT_INT64);
    observeProperty(MpvProperties::self()->Mute, MPV_FORMAT_FLAG);
    observeProperty(MpvProperties::self()->Speed, MPV_FORMAT_DOUBLE);
    observeProperty(MpvProperties::self()->TrackList, MPV_FORMAT_NODE);
    observeProperty(MpvProperties::self()->AudioId, MPV_FORMAT_STRING);
    observeProperty(MpvProperties::self()->SubtitleId, MPV_FORMAT_STRING);
    observeProperty(MpvProperties::self()->AudioDelay, MPV_FORMAT_DOUBLE);
    observeProperty(MpvProperties::self()->SubDelay, MPV_FORMAT_DOUBLE);
    observeProperty(MpvProperties::self()->Panscan, MPV_FORMAT_DOUBLE);
    observeProperty(MpvProperties::self()->VideoZoom, MPV_FORMAT_DOUBLE);
    observeProperty(MpvProperties::self()->VideoAspectOverride, MPV_FORMAT_STRING);
    observeProperty(QStringLiteral("demuxer-cache-time"), MPV_FORMAT_DOUBLE);
    observeProperty(QStringLiteral("cache-buffering-state"), MPV_FORMAT_DOUBLE);
    observeProperty(QStringLiteral("seeking"), MPV_FORMAT_FLAG);
    observeProperty(QStringLiteral("chapter-list"), MPV_FORMAT_NODE);
    // Decoded-frame truth (J1-Video-Seam): the same two properties
    // MediaAdmissionProbe.cpp:59-60 observes on its own headless handle, mirrored here
    // on the live playing instance. Not part of the mpvproperties.h constant set —
    // ad-hoc string literals are the established pattern for this constructor's later
    // additions (see demuxer-cache-time/seeking/chapter-list just above).
    observeProperty(QStringLiteral("dwidth"), MPV_FORMAT_INT64);
    observeProperty(QStringLiteral("dheight"), MPV_FORMAT_INT64);

    setupConnections();

    // network-friendly defaults for streaming a torrent over local HTTP
    setProperty(QStringLiteral("cache"), QStringLiteral("yes"));
    setProperty(QStringLiteral("cache-pause"), QStringLiteral("yes"));
    setProperty(QStringLiteral("cache-secs"), QStringLiteral("60"));
    setProperty(QStringLiteral("demuxer-readahead-secs"), QStringLiteral("60"));
    setProperty(QStringLiteral("demuxer-max-bytes"), QStringLiteral("128MiB"));
    setProperty(QStringLiteral("demuxer-max-back-bytes"), QStringLiteral("32MiB"));
    setProperty(QStringLiteral("network-timeout"), QStringLiteral("600"));
    setProperty(QStringLiteral("stream-buffer-size"), QStringLiteral("32MiB"));
    setProperty(QStringLiteral("stream-lavf-o"),
                QStringLiteral("reconnect=1,reconnect_streamed=1,reconnect_delay_max=10,reconnect_on_network_error=1"));
    setProperty(QStringLiteral("user-agent"), QStringLiteral("VLC/3.0.20 LibVLC/3.0.20"));
    // Keep the ytdl hook OFF. Colosseum resolves its own sources and never wants yt-dlp; left on,
    // ytdl_hook would intercept http(s) URLs and install its OWN http-header-fields, clobbering the
    // Referer/Origin headers loadFileWithHeaders sets — silently, and only in the real app. (slice 1)
    setProperty(QStringLiteral("ytdl"), QStringLiteral("no"));
    setProperty(QStringLiteral("input-default-bindings"), QStringLiteral("no"));
    setProperty(QStringLiteral("input-cursor"), QStringLiteral("no"));
    setProperty(QStringLiteral("osc"), QStringLiteral("no"));
    setProperty(QStringLiteral("osd-level"), QStringLiteral("0"));
    setProperty(QStringLiteral("volume-max"), QStringLiteral("600"));
    // Loudness normalization is now a user choice applied via setAudioNormalization(), and
    // DEFAULTS OFF (smooth). The 2026-07-20 stutter audit proved always-on dynamic loudnorm
    // was the primary cause of dropped frames on weak hardware: it upsamples to 192kHz for
    // true-peak detection at ~65x the audio CPU. QML re-applies the persisted mode on load.
    // (docs/superpowers/specs/2026-07-20-colosseum-playback-stutter-audit.md)
    // Paused+minimized is our normal parked state (pause-on-minimize), and Windows reclaims
    // idle WASAPI streams (device switch / power-save / another player grabbing the device) —
    // the session then resumes into a dead AO with no sound until app restart (Hemanth,
    // long-standing). Feeding silence while paused keeps the stream alive and unreclaimed.
    setProperty(QStringLiteral("audio-stream-silence"), QStringLiteral("yes"));
    setProperty(QStringLiteral("embeddedfonts"), QStringLiteral("yes"));
    setProperty(QStringLiteral("sub-font-provider"), QStringLiteral("auto"));
    setProperty(QStringLiteral("hwdec"), QStringLiteral("auto-safe"));

    setPropertyAsync(QStringLiteral("volume"), 100, static_cast<int>(MpvItem::AsyncIds::SetVolume));
    getPropertyAsync(MpvProperties::self()->Volume, static_cast<int>(MpvItem::AsyncIds::GetVolume));
    m_gifTimer.setInterval(200);
    connect(&m_gifTimer, &QTimer::timeout, this, &MpvItem::gifCaptureFrame);

    m_nativeSyncTimer.setSingleShot(true);
    m_nativeSyncTimer.setInterval(16);
    connect(&m_nativeSyncTimer, &QTimer::timeout, this, &MpvItem::syncNativeVideoWindow);

    // Player 1.5 wid spike: PRESENTATION swap only (see class comment in mpvitem.h).
    if (widPresentationEnabled()) {
#ifdef Q_OS_WIN
        if (m_nativeVideoWindow.create()) {
            setProperty(QStringLiteral("wid"), static_cast<qint64>(m_nativeVideoWindow.wid()));
            // mpvqt's controller pre-sets vo=libmpv; overridden here BEFORE the first load —
            // with no render context ever created (createRenderer below), mpv picks its own
            // native swapchain under the video window. hwdec auto (not copy-back auto-safe):
            // gpu-next's D3D11 context takes d3d11va zero-copy.
            setProperty(QStringLiteral("vo"), QStringLiteral("gpu-next"));
            setProperty(QStringLiteral("hwdec"), QStringLiteral("auto"));
            connect(this, &QQuickItem::windowChanged, this, &MpvItem::onWindowChanged);
        } else {
            qWarning("[player15] native video window creation failed; falling back to Qt presentation");
        }
#endif
    }

    armDropProbeIfRequested();
}

MpvItem::~MpvItem()
{
    // Drop-ruler evidence lands even when the app is closed mid-measure-window.
    logDropProbeFinal();
    // App quitting mid-encode: our parented QProcess would otherwise be killed mid-write,
    // leaving a truncated .gif posing as a saved clip and an orphaned %TEMP% frames dir.
    if (m_gifEncoding && m_gifEncodeProc) {
        m_gifEncodeProc->disconnect(this);           // our lambdas must not fire during teardown
        m_gifEncodeProc->kill();
        m_gifEncodeProc->waitForFinished(2000);      // teardown-only wait; Windows: cleanGifTemp fails while
                                                     // ffmpeg holds frames open, so kill-and-wait MUST precede cleanup
        if (!m_gifEncodeOutPath.isEmpty())
            QFile::remove(m_gifEncodeOutPath);        // never leave a truncated gif posing as a saved clip
        cleanGifTemp();
    }
}

bool MpvItem::widPresentationEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("COLOSSEUM_MPV_WID");
    return enabled;
}

void MpvItem::onWindowChanged(QQuickWindow *window)
{
    if (!widPresentationEnabled())
        return;
    if (m_filteredWindow)
        m_filteredWindow->removeEventFilter(this);
    m_filteredWindow = window;
    if (window) {
        window->installEventFilter(this);
        syncNativeVideoWindow();
    }
}

bool MpvItem::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_filteredWindow && widPresentationEnabled()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Expose:
        case QEvent::Show:
        case QEvent::WindowStateChange:
        case QEvent::WinIdChange:
        case QEvent::ScreenChangeInternal:
            // Coalesced: window drags spam Move/Resize; one sync per frame is plenty.
            m_nativeSyncTimer.start();
            break;
        default:
            break;
        }
    }
    return MpvAbstractItem::eventFilter(watched, event);
}

void MpvItem::syncNativeVideoWindow()
{
#ifdef Q_OS_WIN
    if (!widPresentationEnabled() || !m_nativeVideoWindow.isValid() || !m_filteredWindow)
        return;
    const WId mainWinId = m_filteredWindow->winId();
    if (!mainWinId)
        return;
    if (m_filteredWindow->windowState() & Qt::WindowMinimized) {
        m_nativeVideoWindow.hide();
        return;
    }
    m_nativeVideoWindow.placeUnder(
        reinterpret_cast<MpvNativeWindowHandle>(static_cast<quintptr>(mainWinId)));
#endif
}

void MpvItem::armDropProbeIfRequested()
{
    const QByteArray raw = qgetenv("COLOSSEUM_MPV_DROP_PROBE");
    if (raw.isEmpty())
        return;
    const QList<QByteArray> parts = raw.split(',');
    bool okW = false, okM = false;
    const int warmup = parts.value(0).toInt(&okW);
    const int measure = parts.value(1).toInt(&okM);
    if (!okW || !okM || warmup < 0 || measure <= 0)
        return;
    m_dropProbeWarmupSecs = warmup;
    m_dropProbeMeasureSecs = measure;
    qWarning().noquote()
        << QStringLiteral("[player15] drop probe armed warmup=%1 measure=%2 (async poll)").arg(warmup).arg(measure);
    // ASYNC POLLING, not events and not sync getProperty: property-change delivery was dead
    // in spike smokes (only the initial observation burst arrives; pos/dur never reach QML in
    // isolated ABBA runs, wid or not) and sync getProperty() returned ErrorReturn on handles
    // that were visibly playing, while the async reply path demonstrably works (the stats
    // batch populates the card). The probe therefore polls time-pos via getPropertyAsync and
    // samples the drop counters the same way. (2026-09-20 spike finding, needs its own
    // investigation.)
    m_dropProbePollTimer.setInterval(1000);
    connect(&m_dropProbePollTimer, &QTimer::timeout, this, [this]() {
        if (!m_dropProbeAnchored) {
            getPropertyAsync(QStringLiteral("time-pos"),
                             static_cast<int>(AsyncIds::ProbeTimePos));
            return;
        }
        const qint64 elapsedMs = m_dropProbeAnchor.elapsed();
        if (m_dropProbeStart < 0
                && elapsedMs >= qint64(m_dropProbeWarmupSecs) * 1000) {
            getPropertyAsync(QStringLiteral("frame-drop-count"),
                             static_cast<int>(AsyncIds::ProbeDropStart));
        }
        if (elapsedMs >= qint64(m_dropProbeWarmupSecs + m_dropProbeMeasureSecs) * 1000) {
            m_dropProbePollTimer.stop();
            getPropertyAsync(QStringLiteral("frame-drop-count"),
                             static_cast<int>(AsyncIds::ProbeDropFinal));
        }
    });
    m_dropProbePollTimer.start();
}

void MpvItem::logDropProbeFinal(qint64 outputEndSample)
{
    if (m_dropProbeFinalLogged || m_dropProbeWarmupSecs < 0 || m_dropProbeStart < 0)
        return;
    m_dropProbeFinalLogged = true;
    qWarning().noquote()
        << QStringLiteral("MPV_DROP_PROBE RESULT {\"outputStart\":%1,\"outputEnd\":%2}")
               .arg(m_dropProbeStart)
               .arg(outputEndSample);
}

void MpvItem::setupConnections()
{
    connect(mpvController(), &MpvController::propertyChanged,
            this, &MpvItem::onPropertyChanged, Qt::QueuedConnection);

    connect(mpvController(), &MpvController::fileStarted,
            this, &MpvItem::fileStarted, Qt::QueuedConnection);

    connect(mpvController(), &MpvController::fileLoaded,
            this, &MpvItem::fileLoaded, Qt::QueuedConnection);

    connect(mpvController(), &MpvController::endFile,
            this, [this](const QString &reason) {
        Q_EMIT endFile(reason);
        if (reason == QLatin1String("error") || reason == QLatin1String("other")) {
            // Installed MpvQt exposes only the coarse end-file reason string here;
            // it does not forward mpv_event_end_file.error. Report that limitation
            // honestly instead of manufacturing network/codec/decode classifications.
            Q_EMIT playbackError(QStringLiteral("unknown"), reason);
        }
    }, Qt::QueuedConnection);

    connect(mpvController(), &MpvController::videoReconfig,
            this, &MpvItem::videoReconfig, Qt::QueuedConnection);

    connect(mpvController(), &MpvController::asyncReply,
            this, &MpvItem::onAsyncReply, Qt::QueuedConnection);
}

void MpvItem::onPropertyChanged(const QString &property, const QVariant &value)
{
    // TEMP diagnostic (2026-09-20 spike): first 5 time-pos deliveries, with type + value.
    if (m_diagEventLogCount < 5 && property == MpvProperties::self()->Position) {
        ++m_diagEventLogCount;
        qWarning().noquote()
            << QStringLiteral("[player15] time-pos delivery %1: type=%2 valid=%3 value=%4")
                   .arg(m_diagEventLogCount)
                   .arg(QString::fromLatin1(value.typeName()),
                        value.isValid() ? QStringLiteral("yes") : QStringLiteral("no"),
                        value.toString());
    }
    if (property == MpvProperties::self()->MediaTitle) {
        Q_EMIT mediaTitleChanged();

    } else if (property == MpvProperties::self()->Position) {
        const double next = value.toDouble();
        // A jump means a seek, not a playback tick: emit at once so a scrub never looks laggy.
        const bool jumped = qAbs(next - m_cachedPosition) > 0.5;
        m_cachedPosition = next;
        m_formattedPosition = formatTime(next);
        // The CACHE is always current above; only the signal is rate-limited. Re-notifying QML more
        // than ~10x/sec re-evaluates every binding that reads mpv.position (27 of them in
        // PlayerPage.qml) for motion no eye can see — the same needless-reactive-work shape as the
        // Continue-row cascade that was the 2026-07-29 video stutter.
        if (jumped || !m_positionEmitClock.isValid() || m_positionEmitClock.elapsed() >= 100) {
            m_positionEmitClock.restart();
            Q_EMIT positionChanged();
        }

    } else if (property == MpvProperties::self()->Duration) {
        m_cachedDuration = value.toDouble();
        m_formattedDuration = formatTime(m_cachedDuration);
        Q_EMIT durationChanged();

    } else if (property == MpvProperties::self()->Pause) {
        m_cachedPause = value.toBool();
        Q_EMIT pauseChanged();

    } else if (property == MpvProperties::self()->Volume) {
        m_cachedVolume = value.toInt();
        Q_EMIT volumeChanged();

    } else if (property == MpvProperties::self()->Mute) {
        Q_EMIT muteChanged();

    } else if (property == MpvProperties::self()->Speed) {
        m_cachedSpeed = value.toDouble();
        Q_EMIT speedChanged();

    } else if (property == MpvProperties::self()->TrackList) {
        m_trackList = value.toList();
        Q_EMIT trackListChanged();

    } else if (property == MpvProperties::self()->AudioId) {
        Q_EMIT audioTrackChanged();
        Q_EMIT trackListChanged();

    } else if (property == MpvProperties::self()->SubtitleId) {
        Q_EMIT subtitleTrackChanged();
        Q_EMIT trackListChanged();

    } else if (property == MpvProperties::self()->AudioDelay) {
        Q_EMIT audioDelayChanged();

    } else if (property == MpvProperties::self()->SubDelay) {
        Q_EMIT subDelayChanged();

    } else if (property == MpvProperties::self()->Panscan
               || property == MpvProperties::self()->VideoZoom
               || property == MpvProperties::self()->VideoAspectOverride) {
        Q_EMIT videoFillChanged();

    } else if (property == QLatin1String("demuxer-cache-time")) {
        m_cacheTime = value.toDouble();
        Q_EMIT cacheTimeChanged();

    } else if (property == QLatin1String("cache-buffering-state")) {
        m_cacheBufferingState = qBound(0.0, value.toDouble(), 100.0);
        Q_EMIT cacheBufferingStateChanged();

    } else if (property == QLatin1String("seeking")) {
        m_coreSeeking = value.toBool();
        Q_EMIT coreSeekingChanged();

    } else if (property == QLatin1String("dwidth")) {
        // dwidth/dheight arrive as two independent property-change events for the same
        // decode moment; each is applied and notified as it lands rather than paired,
        // matching MediaAdmissionProbe.cpp:125-141's own dw/dh bookkeeping.
        const int next = static_cast<int>(value.toLongLong());
        if (next != m_decodedWidth) {
            m_decodedWidth = next;
            Q_EMIT decodedDimensionsChanged();
        }

    } else if (property == QLatin1String("dheight")) {
        const int next = static_cast<int>(value.toLongLong());
        if (next != m_decodedHeight) {
            m_decodedHeight = next;
            Q_EMIT decodedDimensionsChanged();
        }

    } else if (property == QLatin1String("chapter-list")) {
        QVariantList out;
        const QVariantList raw = value.toList();
        for (const QVariant &entry : raw) {
            const QVariantMap m = entry.toMap();
            const double start = m.value(QStringLiteral("time")).toDouble();
            QString title = m.value(QStringLiteral("title")).toString().trimmed();
            if (title.isEmpty())
                title = QStringLiteral("Chapter");
            QVariantMap normalized;
            normalized.insert(QStringLiteral("title"), title);
            normalized.insert(QStringLiteral("startSec"), start);
            out.append(normalized);
        }
        m_chapters = out;
        Q_EMIT chaptersChanged();
    }
}

void MpvItem::onAsyncReply(const QVariant &data, mpv_event event)
{
    // Drop-probe async reads first (see armDropProbeIfRequested for why async).
    const int probeReplyId = static_cast<int>(event.reply_userdata);
    if (probeReplyId == static_cast<int>(AsyncIds::ProbeTimePos)) {
        if (!m_dropProbeAnchored && data.canConvert<double>()
                && data.toDouble() > 1.0) {
            m_dropProbeAnchored = true;
            m_dropProbeAnchor.start();
            qWarning().noquote() << QStringLiteral("[player15] drop probe anchored at playback");
        }
        return;
    }
    if (probeReplyId == static_cast<int>(AsyncIds::ProbeDropStart)) {
        if (m_dropProbeStart < 0)
            m_dropProbeStart = data.toLongLong();
        return;
    }
    if (probeReplyId == static_cast<int>(AsyncIds::ProbeDropFinal)) {
        logDropProbeFinal(data.toLongLong());
        return;
    }
    // Stats batch replies encode their property index above the small enum ids.
    const int replyId = probeReplyId;
    const int batchBase = static_cast<int>(AsyncIds::StatsBatch);
    if (replyId >= batchBase && replyId < batchBase + m_statsBatchProperties.size()) {
        m_statsBatchValues.insert(m_statsBatchProperties.at(replyId - batchBase), data);
        if (--m_statsBatchPending <= 0) {
            m_statsBatchPending = 0;
            Q_EMIT playbackStatsReady(m_statsBatchValues);
            m_statsBatchValues.clear();
            m_statsBatchProperties.clear();
        }
        return;
    }
    switch (static_cast<AsyncIds>(event.reply_userdata)) {
    case AsyncIds::None:
    case AsyncIds::SetVolume:
        break;
    case AsyncIds::GetVolume:
    case AsyncIds::ExpandText:
        Q_UNUSED(data)
        break;
    }
}

QString MpvItem::formatTime(const double time) const
{
    int totalNumberOfSeconds = static_cast<int>(time);
    int seconds = totalNumberOfSeconds % 60;
    int minutes = (totalNumberOfSeconds / 60) % 60;
    int hours = (totalNumberOfSeconds / 60 / 60);

    QString timeString =
        QStringLiteral("%1:%2:%3").arg(hours, 2, 10, QLatin1Char('0')).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));

    return timeString;
}

void MpvItem::loadFile(const QString &file)
{
    // Clear any headers a prior loadFileWithHeaders installed BEFORE this load, so a header-carrying
    // source cannot leak its Referer/Origin into the next stream. This is why the clear lives here
    // and not in the caller. First line, before the same-URL short-circuit, so it runs on every
    // call including a same-URL reload. Empty node array = reset, not a no-op (verified in MpvQt
    // setNode + mpv option semantics). (Theatre House HTTP Source, slice 1.)
    setProperty(QStringLiteral("http-header-fields"), QStringList());
    issueLoadFile(file);
}

void MpvItem::loadFileWithHeaders(const QString &url, const QVariantMap &headers)
{
    // Install the addon-supplied Referer/Origin as mpv's http-header-fields, then load. A string
    // list marshals to an MPV_FORMAT_NODE_ARRAY (one entry per header) — comma-safe. The formatting
    // (and third-party-JSON injection guards) live in httpHeaderFieldsList so they are unit-tested
    // without an mpv instance. loadFile clears the field again on the next plain load.
    setProperty(QStringLiteral("http-header-fields"), httpHeaderFieldsList(headers));
    issueLoadFile(url);
}

void MpvItem::issueLoadFile(const QString &file)
{
    auto url = QUrl::fromUserInput(file);
    if (m_currentUrl != url) {
        m_currentUrl = url;
        Q_EMIT currentUrlChanged();
    }

    // A fresh `loadfile ... replace` starts a brand-new decode every time this runs, even when
    // the URL is unchanged (a replay). mpv only fires dwidth/dheight property-change events on a
    // VALUE CHANGE, so a same-size reload would otherwise never re-announce them — leaving the
    // stale prior dimensions reading "decoded" before the new file has decoded anything. Reset
    // here, before the command goes out, so decodedWidth/decodedHeight always start at zero for
    // this load (J1-Video-Seam's "ready_resets_on_unload"-style guarantee, at the native layer).
    if (m_decodedWidth != 0 || m_decodedHeight != 0) {
        m_decodedWidth = 0;
        m_decodedHeight = 0;
        Q_EMIT decodedDimensionsChanged();
    }

    command(QStringList() << QStringLiteral("loadfile")
                          << m_currentUrl.toString(QUrl::PreferLocalFile)
                          << QStringLiteral("replace"));
}

void MpvItem::seekExact(double value)
{
    command(QStringList() << QStringLiteral("seek")
                          << QString::number(qMax(0.0, value), 'f', 3)
                          << QStringLiteral("absolute")
                          << QStringLiteral("exact"));
}

void MpvItem::seekStep(double delta)
{
    seekExact(position() + delta);
}

void MpvItem::frameStep()
{
    command(QStringList() << QStringLiteral("frame-step"));
}

void MpvItem::frameBackStep()
{
    command(QStringList() << QStringLiteral("frame-back-step"));
}

void MpvItem::addSubtitle(const QString &url, const QString &title, const QString &lang, bool select)
{
    if (url.isEmpty())
        return;
    // mpv: sub-add <url> [<flags> [<title> [<lang>]]]. mpv loads http(s) URLs itself.
    const QString flag = select ? QStringLiteral("select") : QStringLiteral("auto");
    const QString t = !title.isEmpty() ? title
                      : (!lang.isEmpty() ? lang : QStringLiteral("Subtitle"));
    QStringList cmd;
    cmd << QStringLiteral("sub-add") << url << flag << t;
    if (!lang.isEmpty())
        cmd << lang;
    command(cmd);
}

void MpvItem::setSubOption(const QString &key, const QVariant &value)
{
    const QString name = key.trimmed();
    if (name.isEmpty() || !name.startsWith(QStringLiteral("sub-"))) {
        return;
    }
    setProperty(name, value);
}

// Loudness normalization mode (2026-07-20 audit). "off" = smooth default (no filter);
// "light" = dynaudnorm, a cheap adaptive normalizer with no 192kHz upsample; "full" =
// EBU R128 loudnorm, best consistency but the CPU cost that stutters weak hardware.
// `af` is a live global mpv option, so this applies to the currently-playing file too.
void MpvItem::setAudioNormalization(const QString &mode)
{
    QString af;
    if (mode == QStringLiteral("full")) {
        af = QStringLiteral("loudnorm=I=-14:TP=-1.5:LRA=11");
    } else if (mode == QStringLiteral("light")) {
        // Louder-than-default so quiet dialogue lifts: max gain 15x + light compression.
        af = QStringLiteral("dynaudnorm=m=15:s=9");
    }
    // "off" / anything else -> empty -> no audio filter.
    setProperty(QStringLiteral("af"), af);
}

QVariant MpvItem::mpvProperty(const QString &name)
{
    static const QSet<QString> allowedStatsProperties = {
        QStringLiteral("video-bitrate"),
        QStringLiteral("audio-bitrate"),
        // The two drop counters the stats card prints as "decoder / output". mpv's naming is a
        // trap: `frame-drop-count` is the OUTPUT (VO) drop count, and the decoder's own drops are
        // `decoder-frame-drop-count` — exactly the mapping statsPayload() above uses. There is no
        // `vo-drop-frame-count` property in mpv; asking for it returns an ErrorReturn QVariant,
        // which reaches QML as a non-null object and renders as "NaN" (2026-07-29). It is kept in
        // the allowlist only because PlayerEngineP2 uses that string as its own internal key for
        // the Player-2 scheduler's late-drop count; the mpv path must never request it.
        QStringLiteral("frame-drop-count"),
        QStringLiteral("decoder-frame-drop-count"),
        QStringLiteral("vo-drop-frame-count"),
        QStringLiteral("estimated-vf-fps"),
        QStringLiteral("container-fps"),
        QStringLiteral("video-codec"),
        QStringLiteral("audio-codec"),
        QStringLiteral("hwdec-current"),
        QStringLiteral("cache-buffering-state"),
        QStringLiteral("width"),
        QStringLiteral("height"),
        // Pause info card quality line (2026-07-20): channels + HDR transfer.
        QStringLiteral("audio-params/channel-count"),
        QStringLiteral("video-params/transfer"),
    };
    const QString key = name.trimmed();
    if (!allowedStatsProperties.contains(key))
        return QVariant();
    return getProperty(key);
}

void MpvItem::requestPlaybackStatsAsync()
{
    // The periodic stats-card batch. Same properties the card consumes via mpvProperty(),
    // minus the two pause-card-only extras and minus `vo-drop-frame-count` (an mpv-invalid
    // name the allowlist carries only for PlayerEngineP2 — see the comment above). Kept in
    // deliberate lockstep with PlayerPage.applyPlaybackStats(): every key here must be a key
    // that mapping consumes.
    static const QStringList statsBatchProperties = {
        QStringLiteral("video-bitrate"),
        QStringLiteral("audio-bitrate"),
        QStringLiteral("frame-drop-count"),
        QStringLiteral("decoder-frame-drop-count"),
        QStringLiteral("estimated-vf-fps"),
        QStringLiteral("container-fps"),
        QStringLiteral("video-codec"),
        QStringLiteral("audio-codec"),
        QStringLiteral("hwdec-current"),
        QStringLiteral("cache-buffering-state"),
        QStringLiteral("width"),
        QStringLiteral("height"),
    };
    if (m_statsBatchPending > 0)
        return;   // a batch is still landing; the next tick owns the fresher read
    m_statsBatchProperties = statsBatchProperties;
    m_statsBatchValues.clear();
    m_statsBatchPending = statsBatchProperties.size();
    for (int i = 0; i < statsBatchProperties.size(); ++i) {
        getPropertyAsync(statsBatchProperties.at(i),
                         static_cast<int>(AsyncIds::StatsBatch) + i);
    }
}

QString MpvItem::captureFrame(const QString &title, const QString &subtitle)
{
    const QString dir = captureDirectory();
    QDir().mkpath(dir);
    const QString fileName = QStringLiteral("%1 - %2.png")
                                 .arg(captureBaseName(title, subtitle),
                                      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
    const QString path = QDir(dir).filePath(fileName);
    setProperty(QStringLiteral("screenshot-format"), QStringLiteral("png"));
    setProperty(QStringLiteral("screenshot-png-compression"), 3);
    setProperty(QStringLiteral("screenshot-sw"), QStringLiteral("yes"));
    command(QStringList() << QStringLiteral("screenshot-to-file")
                          << QDir::toNativeSeparators(path)
                          << QStringLiteral("video"));
    return QDir::toNativeSeparators(path);
}

void MpvItem::revealCaptureFolder(const QString &path)
{
    QString folder;
    if (!path.trimmed().isEmpty())
        folder = QFileInfo(path).absoluteDir().absolutePath();
    if (folder.isEmpty())
        folder = captureDirectory();
    if (!folder.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

bool MpvItem::startGifRecording()
{
    if (m_gifEncoding)      // one encode at a time; the temp dir is FFmpeg's until it finishes
        return false;
    if (m_gifRecording)
        return true;
    cleanGifTemp();
    m_gifTempDir = QDir::temp().filePath(QStringLiteral("colosseum-gif-%1")
                                             .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(m_gifTempDir))
        return false;
    m_gifFrame = 0;
    m_gifStartedAt = QDateTime::currentMSecsSinceEpoch();
    m_gifRecording = true;
    setProperty(QStringLiteral("screenshot-format"), QStringLiteral("png"));
    setProperty(QStringLiteral("screenshot-png-compression"), 3);
    setProperty(QStringLiteral("screenshot-sw"), QStringLiteral("yes"));
    gifCaptureFrame();
    m_gifTimer.start();
    return true;
}

void MpvItem::stopGifRecording(const QString &title, const QString &subtitle)
{
    if (!m_gifRecording)    // not-recording is a no-op, not a failure — no toast on stray calls
        return;
    m_gifTimer.stop();
    m_gifRecording = false;
    if (m_gifFrame < 2) {
        cleanGifTemp();
        Q_EMIT gifFailed();
        return;
    }

    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty()) {
        cleanGifTemp();
        Q_EMIT gifFailed();
        return;
    }

    const QString outDir = gifOutputDirectory();
    QDir().mkpath(outDir);
    const QString outPath = QDir(outDir).filePath(QStringLiteral("%1 - %2.gif")
                                                     .arg(captureBaseName(title, subtitle),
                                                          QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"))));
    const QString inputPattern = QDir(m_gifTempDir).filePath(QStringLiteral("frame_%05d.png"));

    QStringList args;
    args << QStringLiteral("-y")
         << QStringLiteral("-framerate") << QStringLiteral("5")
         << QStringLiteral("-i") << QDir::toNativeSeparators(inputPattern)
         << QStringLiteral("-vf") << QStringLiteral("fps=10,scale=640:-2:flags=lanczos")
         << QDir::toNativeSeparators(outPath);

    if (m_gifEncoding) {          // one encode at a time; no queue in v1
        // do NOT clean temp here — a live encode owns the frame dir
        Q_EMIT gifFailed();
        return;
    }

    m_gifEncoding = true;
    Q_EMIT gifEncodingChanged();

    // Async encode: the app stays responsive; QML holds its "encoding" state until
    // gifSaved/gifFailed lands. FailedToStart never reaches finished(), so exactly one
    // of the two handlers below cleans up and emits. The dtor also watches this handle
    // so a quit mid-encode leaves no truncated gif and no orphaned temp frames.
    auto *ff = new QProcess(this);
    m_gifEncodeProc = ff;
    m_gifEncodeOutPath = outPath;
    connect(ff, &QProcess::finished, this,
            [this, ff, outPath](int code, QProcess::ExitStatus st) {
        m_gifEncodeProc = nullptr;
        m_gifEncodeOutPath.clear();
        ff->deleteLater();
        cleanGifTemp();
        m_gifEncoding = false;
        Q_EMIT gifEncodingChanged();
        if (st == QProcess::NormalExit && code == 0)
            Q_EMIT gifSaved(QDir::toNativeSeparators(outPath));
        else
            Q_EMIT gifFailed();
    });
    connect(ff, &QProcess::errorOccurred, this,
            [this, ff](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart)   // finished() covers the rest
            return;
        m_gifEncodeProc = nullptr;
        m_gifEncodeOutPath.clear();
        ff->deleteLater();
        cleanGifTemp();
        m_gifEncoding = false;
        Q_EMIT gifEncodingChanged();
        Q_EMIT gifFailed();
    });
    ff->start(ffmpeg, args);
}

void MpvItem::abortGifRecording()
{
    if (m_gifEncoding)      // encode owns the temp frames; nothing left to abort anyway
        return;
    m_gifTimer.stop();
    m_gifRecording = false;
    cleanGifTemp();
}

QString MpvItem::mediaTitle()
{
    return getProperty(MpvProperties::self()->MediaTitle).toString();
}

double MpvItem::position()
{
    return m_cachedPosition;   // cached from the observer; no blocking call into the mpv core
}

void MpvItem::setPosition(double value)
{
    if (qFuzzyCompare(value, position())) {
        return;
    }
    setPropertyAsync(MpvProperties::self()->Position, value);
}

double MpvItem::duration()
{
    return m_cachedDuration;   // cached from the observer; no blocking call into the mpv core
}

double MpvItem::cacheTime() const
{
    return m_cacheTime;
}

double MpvItem::cacheBufferingState() const
{
    return m_cacheBufferingState;
}

bool MpvItem::coreSeeking() const
{
    return m_coreSeeking;
}

bool MpvItem::gifEncoding() const
{
    return m_gifEncoding;
}

bool MpvItem::pause()
{
    return m_cachedPause;   // cached from the observer; no blocking call into the mpv core
}

void MpvItem::setPause(bool value)
{
    if (value == pause()) {
        return;
    }
    setPropertyAsync(MpvProperties::self()->Pause, value);
}

int MpvItem::volume()
{
    return m_cachedVolume;   // cached from the observer; no blocking call into the mpv core
}

void MpvItem::setVolume(int value)
{
    const int next = qBound(0, value, 600);
    if (next == volume()) {
        return;
    }
    setPropertyAsync(MpvProperties::self()->Volume, next);
}

bool MpvItem::mute()
{
    return getProperty(MpvProperties::self()->Mute).toBool();
}

void MpvItem::setMute(bool value)
{
    if (value == mute()) {
        return;
    }
    setPropertyAsync(MpvProperties::self()->Mute, value);
}

double MpvItem::speed()
{
    const double value = m_cachedSpeed;   // cached from the observer
    return value > 0.0 ? value : 1.0;
}

void MpvItem::setSpeed(double value)
{
    const double next = qBound(0.25, std::round(value * 100.0) / 100.0, 3.0);
    if (qFuzzyCompare(next, speed())) {
        return;
    }
    setPropertyAsync(MpvProperties::self()->Speed, next);
}

QString MpvItem::audioTrack()
{
    return stringifyId(getProperty(MpvProperties::self()->AudioId));
}

void MpvItem::setAudioTrack(const QString &value)
{
    const QString id = value.trimmed();
    setPropertyAsync(MpvProperties::self()->AudioId, id.isEmpty() ? QStringLiteral("no") : id);
}

QString MpvItem::subtitleTrack()
{
    return stringifyId(getProperty(MpvProperties::self()->SubtitleId));
}

void MpvItem::setSubtitleTrack(const QString &value)
{
    const QString id = value.trimmed();
    setPropertyAsync(MpvProperties::self()->SubtitleId, id.isEmpty() ? QStringLiteral("no") : id);
}

QVariantList MpvItem::audioTracks() const
{
    return tracksForType(QStringLiteral("audio"));
}

QVariantList MpvItem::subtitleTracks() const
{
    return tracksForType(QStringLiteral("sub"));
}

QVariantList MpvItem::chapters() const
{
    return m_chapters;
}

double MpvItem::audioDelay()
{
    return getProperty(MpvProperties::self()->AudioDelay).toDouble();
}

void MpvItem::setAudioDelay(double value)
{
    setPropertyAsync(MpvProperties::self()->AudioDelay, std::round(value * 100.0) / 100.0);
}

double MpvItem::subDelay()
{
    return getProperty(MpvProperties::self()->SubDelay).toDouble();
}

void MpvItem::setSubDelay(double value)
{
    setPropertyAsync(MpvProperties::self()->SubDelay, std::round(value * 100.0) / 100.0);
}

double MpvItem::panscan()
{
    return getProperty(MpvProperties::self()->Panscan).toDouble();
}

void MpvItem::setPanscan(double value)
{
    setPropertyAsync(MpvProperties::self()->Panscan, qBound(0.0, value, 1.0));
}

double MpvItem::videoZoom()
{
    return getProperty(MpvProperties::self()->VideoZoom).toDouble();
}

void MpvItem::setVideoZoom(double value)
{
    setPropertyAsync(MpvProperties::self()->VideoZoom, qBound(-2.0, value, 2.0));
}

QString MpvItem::videoAspect()
{
    return getProperty(MpvProperties::self()->VideoAspectOverride).toString();
}

void MpvItem::setVideoAspect(const QString &value)
{
    setPropertyAsync(MpvProperties::self()->VideoAspectOverride, value.isEmpty() ? QStringLiteral("-1") : value);
}

QString MpvItem::formattedDuration() const
{
    return m_formattedDuration;
}

QString MpvItem::formattedPosition() const
{
    return m_formattedPosition;
}

QUrl MpvItem::currentUrl() const
{
    return m_currentUrl;
}

int MpvItem::decodedWidth() const
{
    return m_decodedWidth;   // cached from the observer; mirrors MediaAdmissionProbe's dwidth read
}

int MpvItem::decodedHeight() const
{
    return m_decodedHeight;   // cached from the observer; mirrors MediaAdmissionProbe's dheight read
}

QVariantList MpvItem::tracksForType(const QString &type) const
{
    QVariantList out;
    for (const QVariant &entry : m_trackList) {
        QVariantMap track = entry.toMap();
        if (track.value(QStringLiteral("type")).toString() != type) {
            continue;
        }

        const QString id = stringifyId(track.value(QStringLiteral("id")));
        const bool selected = track.value(QStringLiteral("selected")).toBool()
            || (type == QStringLiteral("audio") && id == const_cast<MpvItem *>(this)->audioTrack())
            || (type == QStringLiteral("sub") && id == const_cast<MpvItem *>(this)->subtitleTrack());

        QString title = track.value(QStringLiteral("title")).toString().trimmed();
        if (title.isEmpty()) {
            title = track.value(QStringLiteral("lang")).toString().trimmed();
        }
        if (title.isEmpty()) {
            title = type == QStringLiteral("audio") ? QStringLiteral("Audio track") : QStringLiteral("Subtitle");
        }

        QVariantMap normalized = track;
        normalized.insert(QStringLiteral("id"), id);
        normalized.insert(QStringLiteral("title"), title);
        normalized.insert(QStringLiteral("selected"), selected);
        normalized.insert(QStringLiteral("external"), track.value(QStringLiteral("external")).toBool());
        normalized.insert(QStringLiteral("forced"),
                          track.value(QStringLiteral("forced")).toBool()
                          || title.contains(QStringLiteral("forced"), Qt::CaseInsensitive));
        out.push_back(normalized);
    }
    return out;
}

QString MpvItem::stringifyId(const QVariant &value) const
{
    const QString id = value.toString().trimmed();
    return id == QStringLiteral("no") ? QString() : id;
}

QString MpvItem::captureBaseName(const QString &title, const QString &subtitle) const
{
    QString base = sanitizeCapturePart(title);
    if (base.isEmpty())
        base = sanitizeCapturePart(const_cast<MpvItem *>(this)->mediaTitle());
    if (base.isEmpty())
        base = QStringLiteral("Frame grab");
    const QString sub = sanitizeCapturePart(subtitle);
    if (!sub.isEmpty())
        base += QStringLiteral(" - ") + sub;
    const QString pos = formatTime(m_position).replace(QLatin1Char(':'), QLatin1Char('-'));
    if (!pos.isEmpty())
        base += QStringLiteral(" - ") + pos;
    return base;
}

QString MpvItem::captureDirectory() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("Colosseum"));
}

void MpvItem::gifCaptureFrame()
{
    if (!m_gifRecording || m_gifTempDir.isEmpty())
        return;
    ++m_gifFrame;
    const QString framePath = QDir(m_gifTempDir).filePath(QStringLiteral("frame_%1.png")
                                                             .arg(m_gifFrame, 5, 10, QLatin1Char('0')));
    command(QStringList() << QStringLiteral("screenshot-to-file")
                          << QDir::toNativeSeparators(framePath)
                          << QStringLiteral("video"));
}

QString MpvItem::gifOutputDirectory() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("Colosseum"));
}

QString MpvItem::findFfmpeg()
{
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("ffmpeg.exe");
#else
    const QString exe = QStringLiteral("ffmpeg");
#endif
    const QString appPath = QCoreApplication::applicationDirPath();
    const QString local = QDir(appPath).filePath(exe);
    if (QFileInfo::exists(local))
        return local;
    const QString tools = QDir(appPath).filePath(QStringLiteral("tools/") + exe);
    if (QFileInfo::exists(tools))
        return tools;
    const QString pathHit = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!pathHit.isEmpty())
        return pathHit;
    return QString();
}

void MpvItem::cleanGifTemp()
{
    if (m_gifTempDir.isEmpty())
        return;
    QDir dir(m_gifTempDir);
    if (dir.exists())
        dir.removeRecursively();
    m_gifTempDir.clear();
    m_gifFrame = 0;
    m_gifStartedAt = 0;
}

QString MpvItem::sanitizeCapturePart(const QString &value) const
{
    QString out = value.simplified();
    out.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")), QStringLiteral("_"));
    out = out.trimmed();
    if (out.size() > 90)
        out = out.left(90).trimmed();
    return out;
}
