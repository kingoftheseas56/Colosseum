#include "HarnessHostServices.h"

#include "player2/video/Player2VideoItem.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMetaEnum>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>

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

bool HarnessHostServices::startScenario(const QString &scenario, QString *error)
{
    if (scenario != QStringLiteral("synthetic")) {
        if (error)
            *error = QStringLiteral("Task 4 supports only --scenario synthetic");
        return false;
    }
    m_started = true;
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
    appendEvent(QStringLiteral("file-started"), QFileInfo(path).fileName());
    m_frameTimer.start();
    if (!m_reportPath.isEmpty())
        m_watchdog.start(20'000);
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
    if (!m_metrics.adapterMatch) {
        m_item->update();
        return;
    }

    if (!m_filePath.isEmpty()) {
        if (!m_fileOpened) {
            m_fileOpened = true;
            m_session.open(PlaybackRequest{QUrl::fromLocalFile(m_filePath),
                                           QFileInfo(m_filePath).baseName(),
                                           QFileInfo(m_filePath).completeBaseName()});
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
        const quint64 target = !m_filePath.isEmpty() && m_session.duration() < 10.0 ? 30 : 320;
        if ((!m_filePath.isEmpty() && m_metrics.presented >= target && m_sawPlaying &&
             m_session.duration() > 0.0 && !m_session.tracks().isEmpty()) ||
            (m_filePath.isEmpty() && m_metrics.presented >= target)) {
            finish(true, m_filePath.isEmpty() ? QStringLiteral("Synthetic D3D11 gate passed")
                                              : QStringLiteral("Local file gate passed"), 0);
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
    if (!m_filePath.isEmpty()) {
        m_reportDuration = m_session.duration();
        m_reportTrackCount = m_session.tracks().size();
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
        {QStringLiteral("scenario"), m_filePath.isEmpty() ? QStringLiteral("synthetic")
                                                          : QStringLiteral("file")},
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
        {QStringLiteral("late"), 0},
        {QStringLiteral("cpuTransfers"), static_cast<qint64>(m_metrics.cpuTransfers)},
        {QStringLiteral("deviceErrors"), static_cast<qint64>(m_metrics.deviceErrors)}
        ,{QStringLiteral("reachedPlaying"), m_sawPlaying}
        ,{QStringLiteral("duration"), m_reportDuration}
        ,{QStringLiteral("trackCount"), m_reportTrackCount}
        ,{QStringLiteral("videoCodec"), m_reportVideoCodec}
        ,{QStringLiteral("inputFormat"), m_metrics.inputFormat}
        ,{QStringLiteral("finalState"), sessionState()}
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
