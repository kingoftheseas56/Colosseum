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

#include "mpvproperties.h"

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
    observeProperty(QStringLiteral("seeking"), MPV_FORMAT_FLAG);
    observeProperty(QStringLiteral("chapter-list"), MPV_FORMAT_NODE);

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
    setProperty(QStringLiteral("input-default-bindings"), QStringLiteral("no"));
    setProperty(QStringLiteral("input-cursor"), QStringLiteral("no"));
    setProperty(QStringLiteral("osc"), QStringLiteral("no"));
    setProperty(QStringLiteral("osd-level"), QStringLiteral("0"));
    setProperty(QStringLiteral("volume-max"), QStringLiteral("600"));
    // Loudness normalization (Hemanth 2026-07-18): movie/anime mixes keep huge headroom, so
    // "100%" (unity) feels quiet next to players that amplify. dynaudnorm lifts quiet passages
    // toward a consistent loudness live, without loudnorm's internal 192kHz resample; the UI
    // volume bar stays a plain 0..100 with 100 = normalized loudness.
    // dynaudnorm rev 1/2 fell short (2026-07-18): peak-normalizing stops at the first transient,
    // so low-RMS movie mixes stayed quiet next to PotPlayer, which raises AVERAGE loudness and
    // limits peaks. loudnorm does exactly that (EBU R128 live mode): I=-14 LUFS = hot streaming
    // target, TP=-1.5 true-peak ceiling. Costs its internal 192kHz resample; accepted trade.
    setProperty(QStringLiteral("af"), QStringLiteral("loudnorm=I=-14:TP=-1.5:LRA=11"));
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
}

MpvItem::~MpvItem()
{
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
            const QString code = mapEndFileErrorCode(reason);
            Q_EMIT playbackError(code, reason);
        }
    }, Qt::QueuedConnection);

    connect(mpvController(), &MpvController::videoReconfig,
            this, &MpvItem::videoReconfig, Qt::QueuedConnection);

    connect(mpvController(), &MpvController::asyncReply,
            this, &MpvItem::onAsyncReply, Qt::QueuedConnection);
}

void MpvItem::onPropertyChanged(const QString &property, const QVariant &value)
{
    if (property == MpvProperties::self()->MediaTitle) {
        Q_EMIT mediaTitleChanged();

    } else if (property == MpvProperties::self()->Position) {
        m_formattedPosition = formatTime(value.toDouble());
        Q_EMIT positionChanged();

    } else if (property == MpvProperties::self()->Duration) {
        m_formattedDuration = formatTime(value.toDouble());
        Q_EMIT durationChanged();

    } else if (property == MpvProperties::self()->Pause) {
        Q_EMIT pauseChanged();

    } else if (property == MpvProperties::self()->Volume) {
        Q_EMIT volumeChanged();

    } else if (property == MpvProperties::self()->Mute) {
        Q_EMIT muteChanged();

    } else if (property == MpvProperties::self()->Speed) {
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

    } else if (property == QLatin1String("seeking")) {
        m_coreSeeking = value.toBool();
        Q_EMIT coreSeekingChanged();

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

// Maps mpv end-file reasons into stable, small error codes for the QML recovery layer.
//
// HONEST LIMITATION (2026-07-07): the vendored mpvqt MpvController::endFile forwards only the
// COARSE reason enum ("eof"/"stop"/"error"/...), never mpv_event_end_file.error's descriptive
// text. So the substring branches below can never match on today's inputs and this always
// returns "unknown". That is INTENTIONALLY harmless: PlayerPage.handlePlaybackFailure ignores
// the code, so stream recovery works identically. What is NOT yet delivered is the spec's
// "better error messages" — that stays generic until mpvqt is patched to forward prop->error
// (via mpv_error_string), at which point this mapper starts producing real codes with no other
// change. Left in place as that plug-point; not expanded, since it is inert until then. [Feature 3]
QString MpvItem::mapEndFileErrorCode(const QString &reason) const
{
    const QString r = reason.toLower();
    if (r.contains(QStringLiteral("network")) ||
        r.contains(QStringLiteral("http")) ||
        r.contains(QStringLiteral("timeout")) ||
        r.contains(QStringLiteral("connection"))) {
        return QStringLiteral("network");
    }
    if (r.contains(QStringLiteral("codec")) ||
        r.contains(QStringLiteral("unsupported"))) {
        return QStringLiteral("codec");
    }
    if (r.contains(QStringLiteral("decode")) ||
        r.contains(QStringLiteral("demux")) ||
        r.contains(QStringLiteral("no streams"))) {
        return QStringLiteral("decode");
    }
    if (r.contains(QStringLiteral("file")) ||
        r.contains(QStringLiteral("open")) ||
        r.contains(QStringLiteral("source"))) {
        return QStringLiteral("source");
    }
    return QStringLiteral("unknown");
}

void MpvItem::loadFile(const QString &file)
{
    auto url = QUrl::fromUserInput(file);
    if (m_currentUrl != url) {
        m_currentUrl = url;
        Q_EMIT currentUrlChanged();
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

QVariant MpvItem::mpvProperty(const QString &name)
{
    static const QSet<QString> allowedStatsProperties = {
        QStringLiteral("video-bitrate"),
        QStringLiteral("audio-bitrate"),
        QStringLiteral("frame-drop-count"),
        QStringLiteral("vo-drop-frame-count"),
        QStringLiteral("estimated-vf-fps"),
        QStringLiteral("container-fps"),
        QStringLiteral("video-codec"),
        QStringLiteral("audio-codec"),
        QStringLiteral("hwdec-current"),
        QStringLiteral("cache-buffering-state"),
        QStringLiteral("width"),
        QStringLiteral("height"),
    };
    const QString key = name.trimmed();
    if (!allowedStatsProperties.contains(key))
        return QVariant();
    return getProperty(key);
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
    return getProperty(MpvProperties::self()->Position).toDouble();
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
    return getProperty(MpvProperties::self()->Duration).toDouble();
}

double MpvItem::cacheTime() const
{
    return m_cacheTime;
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
    return getProperty(MpvProperties::self()->Pause).toBool();
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
    return getProperty(MpvProperties::self()->Volume).toInt();
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
    const double value = getProperty(MpvProperties::self()->Speed).toDouble();
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

QString MpvItem::findFfmpeg() const
{
    const QString exe = QStringLiteral("ffmpeg.exe");
    const QString appPath = QCoreApplication::applicationDirPath();
    const QString local = QDir(appPath).filePath(exe);
    if (QFileInfo::exists(local))
        return local;
    const QString tools = QDir(appPath).filePath(QStringLiteral("tools/ffmpeg.exe"));
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

#include "moc_mpvitem.cpp"
