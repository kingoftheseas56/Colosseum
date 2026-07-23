#include "HarnessHostServices.h"

#include "player2/video/Player2VideoItem.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QMetaEnum>
#include <QtCore/QUrl>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>

#include <algorithm>
#include <cstdlib>

// The D3D11 header chain (via HarnessHostServices.h -> D3D11VideoPipeline.h) drags in windows.h,
// whose min/max macros clobber std::max; undo them, as the rest of the engine does.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Colosseum::Player2 {

HarnessHostServices::HarnessHostServices(QObject *parent)
    : Player2HostServices(parent)
{
    m_session.setVideoPipeline(&m_pipeline);
    m_frameTimer.setInterval(16);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &HarnessHostServices::produceFrame);
    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        finish(false, QStringLiteral("Player 2 lab gate did not complete in 20 seconds"), 2);
    });
    connect(&m_session, &Player2Session::stateChanged, this, [this] {
        if (m_session.state() == Player2State::Playing)
            m_sawPlaying = true;
        emit metricsChanged();
    });
    connect(&m_session, &Player2Session::durationChanged, this,
            [this] { emit metricsChanged(); });
    connect(&m_session, &Player2Session::tracksChanged, this,
            [this] { emit metricsChanged(); });
    connect(&m_session, &Player2Session::errorOccurred, this,
            [this](const Player2Error &error) { finish(false, error.message, 2); });
}

void HarnessHostServices::requestAdjacentEpisode(const QString &mediaId, int direction)
{
    appendEvent(QStringLiteral("adjacent-episode"),
                QStringLiteral("%1:%2").arg(mediaId).arg(direction));
}

void HarnessHostServices::requestAlternateSources(const QString &mediaId)
{
    appendEvent(QStringLiteral("alternate-sources"), mediaId);
}

void HarnessHostServices::reportProgress(const QString &mediaId, double position, double duration)
{
    appendEvent(QStringLiteral("progress"),
                QStringLiteral("%1:%2/%3").arg(mediaId).arg(position).arg(duration));
}

QString HarnessHostServices::adapter() const { return m_metrics.qtAdapter; }
QString HarnessHostServices::source() const
{
    if (!m_url.isEmpty())
        return m_url;
    return m_filePath.isEmpty() ? QStringLiteral("Synthetic D3D11 frames")
                                : QFileInfo(m_filePath).fileName();
}
QString HarnessHostServices::decodePath() const
{
    return m_metrics.hardwareFormat.isEmpty() ? QStringLiteral("D3D11 shared texture ring")
                                               : m_metrics.hardwareFormat;
}
QString HarnessHostServices::status() const { return m_status; }
quint64 HarnessHostServices::generated() const { return m_metrics.submitted; }
quint64 HarnessHostServices::presented() const { return m_metrics.presented; }
quint64 HarnessHostServices::dropped() const { return m_metrics.producerStarved; }
quint64 HarnessHostServices::late() const { return 0; }
quint64 HarnessHostServices::cpuTransfers() const { return m_metrics.cpuTransfers; }
quint64 HarnessHostServices::deviceErrors() const { return m_metrics.deviceErrors; }
bool HarnessHostServices::adapterMatch() const { return m_metrics.adapterMatch; }
QString HarnessHostServices::sessionState() const
{
    return QMetaEnum::fromType<Player2State>().valueToKey(static_cast<int>(m_session.state()));
}
double HarnessHostServices::duration() const { return m_session.duration(); }
int HarnessHostServices::trackCount() const { return m_session.tracks().size(); }

void HarnessHostServices::setReportPath(const QString &path) { m_reportPath = path; }
void HarnessHostServices::setMinimumRunSeconds(int seconds)
{
    m_minimumRunSeconds = std::max(0, seconds);
}

void HarnessHostServices::setNormalizationMode(NormalizationMode mode)
{
    // Set before the file opens so the session carries the mode into the new playback session.
    m_session.setNormalizationMode(mode);
    m_reportNormalization =
        QString::fromLatin1(QMetaEnum::fromType<NormalizationMode>().valueToKey(
            static_cast<int>(mode)));
}

bool HarnessHostServices::startScenario(const QString &scenario, QString *error)
{
    if (scenario != QStringLiteral("synthetic")) {
        if (error)
            *error = QStringLiteral("Task 4 supports only --scenario synthetic");
        return false;
    }
    m_started = true;
    m_runTimer.start();
    appendEvent(QStringLiteral("scenario-started"), scenario);
    m_frameTimer.start();
    if (!m_reportPath.isEmpty())
        m_watchdog.start(20'000);
    return true;
}

bool HarnessHostServices::startFile(const QString &path, QString *error)
{
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = QStringLiteral("Media file does not exist: %1").arg(path);
        return false;
    }
    m_filePath = QFileInfo(path).absoluteFilePath();
    m_started = true;
    m_runTimer.start();
    appendEvent(QStringLiteral("file-started"), QFileInfo(path).fileName());
    m_frameTimer.start();
    if (!m_reportPath.isEmpty())
        m_watchdog.start((m_minimumRunSeconds + 30) * 1'000);
    emit metricsChanged();
    return true;
}

bool HarnessHostServices::startUrl(const QString &url, const QString &headersJsonPath, bool live,
                                   QString *error)
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme().isEmpty()) {
        if (error)
            *error = QStringLiteral("Invalid stream URL: %1").arg(url);
        return false;
    }
    if (!headersJsonPath.isEmpty()) {
        QFile file(headersJsonPath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Could not read headers JSON: %1").arg(headersJsonPath);
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error)
                *error = QStringLiteral("Headers JSON must be an object of string values");
            return false;
        }
        const QJsonObject object = document.object();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            m_headers.insert(it.key().toUtf8(), it.value().toString().toUtf8());
    }
    m_url = url;
    m_live = live;
    m_started = true;
    m_runTimer.start();
    appendEvent(QStringLiteral("url-started"), parsed.host());
    m_frameTimer.start();
    if (!m_reportPath.isEmpty())
        m_watchdog.start((m_minimumRunSeconds + 40) * 1'000);
    emit metricsChanged();
    return true;
}

void HarnessHostServices::attachVideoItem(QObject *object)
{
    auto *item = qobject_cast<Player2VideoItem *>(object);
    if (!item) {
        finish(false, QStringLiteral("Harness QML supplied an invalid video item"), 2);
        return;
    }
    m_item = item;
    item->setSession(&m_session);
    item->setVideoPipeline(&m_pipeline);
    connect(item, &Player2VideoItem::initializationFailed, this,
            [this](const QString &message) { finish(false, message, 2); });
    item->update();
}

void HarnessHostServices::produceFrame()
{
    if (!m_started || m_finished || !m_item)
        return;
    refreshMetrics();
    m_maxAudioQueueMs = std::max(m_maxAudioQueueMs, m_session.audioQueueMs());
    m_sawAudioClock = m_sawAudioClock || m_session.audioClock().valid;
    if (!m_metrics.adapterMatch) {
        m_item->update();
        return;
    }

    if (hasMedia()) {
        if (!m_fileOpened) {
            m_fileOpened = true;
            if (!m_url.isEmpty()) {
                PlaybackRequest request;
                request.source = QUrl(m_url);
                request.mediaId = QStringLiteral("stream");
                request.title = QStringLiteral("stream");
                request.headers = m_headers;
                request.stream = true;
                request.live = m_live;
                m_session.open(request);
            } else {
                m_session.open(PlaybackRequest{QUrl::fromLocalFile(m_filePath),
                                               QFileInfo(m_filePath).baseName(),
                                               QFileInfo(m_filePath).completeBaseName()});
            }
        }
        m_item->update();
    } else {
        QString error;
        const VideoFrameToken token{1, m_sequence + 1,
                                    static_cast<qint64>(m_sequence * 16'000)};
        if (m_pipeline.submitSyntheticFrame(token, m_sequence * 0.055, &error)) {
            ++m_sequence;
            m_item->update();
        } else if (!error.isEmpty() &&
                   error != QStringLiteral("Video pipeline is not initialized")) {
            finish(false, error, 2);
            return;
        }
    }
    refreshMetrics();
    if (m_metrics.deviceErrors != 0 || m_metrics.cpuTransfers != 0) {
        finish(false, QStringLiteral("Zero-copy diagnostics reported a pipeline violation"), 2);
        return;
    }
    if (!m_reportPath.isEmpty()) {
        const quint64 target = hasMedia() && m_session.duration() < 10.0 ? 30 : 320;
        bool hasAudioTrack = false;
        for (const QVariant &trackValue : m_session.tracks()) {
            if (trackValue.toMap().value(QStringLiteral("type")).toString() == QStringLiteral("audio")) {
                hasAudioTrack = true;
                break;
            }
        }
        const bool audioReady = !hasAudioTrack || m_sawAudioClock;
        const bool minimumRunMet = m_minimumRunSeconds == 0 ||
            (m_runTimer.isValid() && m_runTimer.elapsed() >= m_minimumRunSeconds * 1'000);
        // A streamed source may report an unknown duration (live/no-length), so it is not required.
        const bool durationReady = !m_url.isEmpty() || m_session.duration() > 0.0;
        if ((hasMedia() && minimumRunMet && m_metrics.presented >= target && m_sawPlaying &&
             durationReady && !m_session.tracks().isEmpty() && audioReady) ||
            (!hasMedia() && m_metrics.presented >= target)) {
            finish(true, !hasMedia() ? QStringLiteral("Synthetic D3D11 gate passed")
                                     : (m_url.isEmpty() ? QStringLiteral("Local file gate passed")
                                                        : QStringLiteral("Stream gate passed")), 0);
        }
    }
}

void HarnessHostServices::refreshMetrics()
{
    m_metrics = m_pipeline.diagnostics();
    if (m_metrics.adapterMatch)
        m_status = QStringLiteral("Zero-copy ring active");
    emit metricsChanged();
}

void HarnessHostServices::finish(bool passed, const QString &message, int exitCode)
{
    if (m_finished)
        return;
    m_finished = true;
    m_frameTimer.stop();
    m_watchdog.stop();
    if (hasMedia()) {
        m_reportDuration = m_session.duration();
        m_reportTrackCount = m_session.tracks().size();
        m_reportAudioDevice = m_session.audioDevice();
        m_reportAudioFormat = m_session.audioFormat();
        m_finalAudioQueueMs = m_session.audioQueueMs();
        m_reportAudioUnderruns = m_session.audioUnderruns();
        m_reportAvP95Ms = m_pipeline.schedulingP95AbsoluteErrorUs() / 1000.0;
        m_reportNormalizationLatencyMs = m_session.normalizationLatencyMs();
        for (const QVariant &trackValue : m_session.tracks()) {
            const QVariantMap track = trackValue.toMap();
            if (track.value(QStringLiteral("type")).toString() == QStringLiteral("video")) {
                m_reportVideoCodec = track.value(QStringLiteral("codec")).toString();
                break;
            }
        }
        m_session.close();
    }
    refreshMetrics();
    m_status = message;
    emit metricsChanged();
    appendEvent(passed ? QStringLiteral("scenario-passed") : QStringLiteral("scenario-failed"),
                message);
    if (!m_reportPath.isEmpty()) {
        if (!writeReport(passed, message))
            exitCode = 3;
        QTimer::singleShot(0, qApp, [exitCode] { QCoreApplication::exit(exitCode); });
    }
}

bool HarnessHostServices::writeReport(bool passed, const QString &message) const
{
    const QJsonObject report{
        {QStringLiteral("scenario"), !hasMedia() ? QStringLiteral("synthetic")
                                    : (m_url.isEmpty() ? QStringLiteral("file")
                                                       : QStringLiteral("stream"))},
        {QStringLiteral("passed"), passed},
        {QStringLiteral("message"), message},
        {QStringLiteral("source"), source()},
        {QStringLiteral("decodePath"), decodePath()},
        {QStringLiteral("qtAdapter"), m_metrics.qtAdapter},
        {QStringLiteral("producerAdapter"), m_metrics.producerAdapter},
        {QStringLiteral("adapterMatch"), m_metrics.adapterMatch},
        {QStringLiteral("sharedFences"), m_metrics.sharedFences},
        {QStringLiteral("generated"), static_cast<qint64>(m_metrics.submitted)},
        {QStringLiteral("presented"), static_cast<qint64>(m_metrics.presented)},
        {QStringLiteral("dropped"), static_cast<qint64>(m_metrics.producerStarved)},
        {QStringLiteral("scheduledLateDrops"), static_cast<qint64>(m_metrics.scheduledLateDrops)},
        {QStringLiteral("avP95Ms"), m_reportAvP95Ms},
        {QStringLiteral("late"), 0},
        {QStringLiteral("cpuTransfers"), static_cast<qint64>(m_metrics.cpuTransfers)},
        {QStringLiteral("deviceErrors"), static_cast<qint64>(m_metrics.deviceErrors)}
        ,{QStringLiteral("reachedPlaying"), m_sawPlaying}
        ,{QStringLiteral("duration"), m_reportDuration}
        ,{QStringLiteral("trackCount"), m_reportTrackCount}
        ,{QStringLiteral("videoCodec"), m_reportVideoCodec}
        ,{QStringLiteral("inputFormat"), m_metrics.inputFormat}
        ,{QStringLiteral("audioDevice"), m_reportAudioDevice}
        ,{QStringLiteral("audioFormat"), m_reportAudioFormat}
        ,{QStringLiteral("audioClockValid"), m_sawAudioClock}
        ,{QStringLiteral("audioUnderruns"), static_cast<qint64>(m_reportAudioUnderruns)}
        ,{QStringLiteral("maxAudioQueueMs"), m_maxAudioQueueMs}
        ,{QStringLiteral("finalAudioQueueMs"), m_finalAudioQueueMs}
        ,{QStringLiteral("elapsedSeconds"), m_runTimer.isValid() ? m_runTimer.elapsed() / 1000.0 : 0.0}
        ,{QStringLiteral("finalState"), sessionState()}
        ,{QStringLiteral("normalization"), m_reportNormalization}
        ,{QStringLiteral("normalizationLatencyMs"), m_reportNormalizationLatencyMs}
    };
    const QFileInfo reportInfo(m_reportPath);
    QDir().mkpath(reportInfo.absolutePath());
    QSaveFile file(reportInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0)
        return false;
    return file.commit();
}

void HarnessHostServices::appendEvent(const QString &event, const QString &message) const
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(directory);
    QFile ledger(QDir(directory).filePath(QStringLiteral("events.jsonl")));
    if (!ledger.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    const QJsonObject record{
        {QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("event"), event},
        {QStringLiteral("message"), message}
    };
    ledger.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
    ledger.write("\n");
}

} // namespace Colosseum::Player2
