// MpvItem implementation — lifted 1:1 from KDE mpvqt's video-player example.
#include "mpvitem.h"

#include <MpvController>
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
    setProperty(QStringLiteral("embeddedfonts"), QStringLiteral("yes"));
    setProperty(QStringLiteral("sub-font-provider"), QStringLiteral("auto"));
    setProperty(QStringLiteral("hwdec"), QStringLiteral("auto-safe"));

    setPropertyAsync(QStringLiteral("volume"), 100, static_cast<int>(MpvItem::AsyncIds::SetVolume));
    getPropertyAsync(MpvProperties::self()->Volume, static_cast<int>(MpvItem::AsyncIds::GetVolume));
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
            this, &MpvItem::endFile, Qt::QueuedConnection);

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

QString MpvItem::formatTime(const double time)
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

#include "moc_mpvitem.cpp"
