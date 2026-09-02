#include "livestore.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

LiveStore::LiveStore(QObject *parent)
    : QObject(parent)
    , m_defaultRecordingDir(buildDefaultRecordingDir()) {
    m_recordingTimer.setInterval(1000);
    connect(&m_recordingTimer, &QTimer::timeout, this, &LiveStore::updateRecordingProgress);
}

LiveStore::~LiveStore() {
    const auto recorders = m_recorders;
    for (QProcess *process : recorders) {
        if (!process)
            continue;
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(800))
                process->kill();
        }
    }
}

void LiveStore::setLiveChannel(const QVariantMap &channel) {
    const QVariantMap normalized = normalizeChannel(channel);
    if (normalized.value(QStringLiteral("url")).toString().isEmpty()) {
        m_isLive = false;
        m_activeChannel.clear();
        emit changed();
        return;
    }
    m_isLive = true;
    m_activeChannel = normalized;
    if (findChannel(normalized.value(QStringLiteral("id")).toString()) < 0)
        m_channels.append(normalized);
    emit changed();
}

void LiveStore::addChannel(const QVariantMap &channel) {
    const QVariantMap normalized = normalizeChannel(channel);
    if (normalized.value(QStringLiteral("url")).toString().isEmpty())
        return;
    const int idx = findChannel(normalized.value(QStringLiteral("id")).toString());
    if (idx >= 0)
        m_channels[idx] = normalized;
    else
        m_channels.append(normalized);
    emit changed();
}

void LiveStore::setGroup(const QString &group) {
    m_group = group;
    emit changed();
}

void LiveStore::setQuery(const QString &query) {
    m_query = query;
    emit changed();
}

void LiveStore::switchChannel(const QVariantMap &channel) {
    const QVariantMap normalized = normalizeChannel(channel);
    if (normalized.value(QStringLiteral("url")).toString().isEmpty())
        return;
    setLiveChannel(normalized);
    emit channelSwitchRequested(normalized);
}

QString LiveStore::startRecording(const QVariantMap &request) {
    QVariantMap session;
    const QString id = QStringLiteral("dvr-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    QString url = request.value(QStringLiteral("url")).toString();
    if (url.isEmpty())
        url = m_activeChannel.value(QStringLiteral("url")).toString();
    const QString channelName = request.value(QStringLiteral("channelName")).toString();
    const QString title = request.value(QStringLiteral("programTitle")).toString();
    const int duration = request.value(QStringLiteral("durationSec"), 1800).toInt();
    const QString resolvedChannelName = channelName.isEmpty() ? m_activeChannel.value(QStringLiteral("name")).toString() : channelName;
    const QString outputPath = buildOutputPath(request, resolvedChannelName, title);

    session.insert(QStringLiteral("id"), id);
    session.insert(QStringLiteral("url"), url);
    session.insert(QStringLiteral("outputPath"), outputPath);
    session.insert(QStringLiteral("channelName"), resolvedChannelName.isEmpty() ? QStringLiteral("Live channel") : resolvedChannelName);
    session.insert(QStringLiteral("programTitle"), title);
    session.insert(QStringLiteral("startedAtMs"), QDateTime::currentMSecsSinceEpoch());
    session.insert(QStringLiteral("plannedDurationSec"), duration > 0 ? duration : 1800);
    session.insert(QStringLiteral("elapsedSec"), 0);
    session.insert(QStringLiteral("bytesWritten"), 0);
    session.insert(QStringLiteral("state"), QStringLiteral("recording"));
    session.insert(QStringLiteral("error"), QString());
    m_recordings.append(session);
    emit changed();

    if (url.isEmpty()) {
        markRecordingError(id, QStringLiteral("No live stream URL available for DVR recording."));
        return id;
    }

    const QString recorder = locateRecorder();
    if (recorder.isEmpty()) {
        markRecordingError(id, QStringLiteral("mpv is required for DVR recording but was not found."));
        return id;
    }

    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    auto *process = new QProcess(this);
    process->setProgram(recorder);
    process->setArguments(QStringList{
        QStringLiteral("--no-terminal"),
        QStringLiteral("--quiet"),
        QStringLiteral("--idle=no"),
        QStringLiteral("--force-window=no"),
        QStringLiteral("--vo=null"),
        QStringLiteral("--ao=null"),
        QStringLiteral("--cache=yes"),
        QStringLiteral("--network-timeout=60"),
        QStringLiteral("--user-agent=VLC/3.0.20 LibVLC/3.0.20"),
        QStringLiteral("--stream-record=%1").arg(outputPath),
        url
    });
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    connect(process, &QProcess::started, this, [this, id] {
        handleRecorderStarted(id);
    });
    connect(process, &QProcess::finished, this,
            [this, id](int exitCode, QProcess::ExitStatus status) {
                handleRecorderFinished(id, exitCode, status);
            });
    connect(process, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError) {
        handleRecorderError(id);
    });
    m_recorders.insert(id, process);
    process->start();
    startProgressTimer();
    return id;
}

void LiveStore::stopRecording(const QString &id) {
    finishRecording(id);
}

void LiveStore::revealRecording(const QString &id) {
    const int idx = findRecording(id);
    if (idx < 0)
        return;
    const QString path = m_recordings.at(idx).toMap().value(QStringLiteral("outputPath")).toString();
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    const QString folder = info.absoluteDir().absolutePath();
    if (!folder.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

QVariantMap LiveStore::normalizeChannel(const QVariantMap &channel) const {
    QVariantMap out = channel;
    const QString url = out.value(QStringLiteral("url")).toString();
    QString id = out.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = url;
    out.insert(QStringLiteral("id"), id);
    if (!out.contains(QStringLiteral("name")) || out.value(QStringLiteral("name")).toString().isEmpty())
        out.insert(QStringLiteral("name"), QStringLiteral("Live channel"));
    if (!out.contains(QStringLiteral("group")))
        out.insert(QStringLiteral("group"), QStringLiteral("Live TV"));
    return out;
}

int LiveStore::findChannel(const QString &id) const {
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_channels.size(); ++i) {
        if (m_channels.at(i).toMap().value(QStringLiteral("id")).toString() == id)
            return i;
    }
    return -1;
}

int LiveStore::findRecording(const QString &id) const {
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_recordings.size(); ++i) {
        if (m_recordings.at(i).toMap().value(QStringLiteral("id")).toString() == id)
            return i;
    }
    return -1;
}

QString LiveStore::buildDefaultRecordingDir() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    const QString dir = QDir(base).filePath(QStringLiteral("Colosseum DVR"));
    QDir().mkpath(dir);
    return QDir::toNativeSeparators(dir);
}

QString LiveStore::buildOutputPath(const QVariantMap &request, const QString &channelName, const QString &title) const {
    const QString requested = request.value(QStringLiteral("outputPath")).toString();
    if (!requested.isEmpty())
        return QDir::toNativeSeparators(requested);
    QString stem = sanitizeFilePart(title);
    if (stem.isEmpty())
        stem = sanitizeFilePart(channelName);
    if (stem.isEmpty())
        stem = QStringLiteral("recording");
    stem = QStringLiteral("%1_%2").arg(stem, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
    return QDir::toNativeSeparators(QDir(m_defaultRecordingDir).filePath(stem + QStringLiteral(".ts")));
}

QString LiveStore::locateRecorder() const {
    auto resolveExecutable = [](const QString &candidate) -> QString {
        const QString value = candidate.trimmed();
        if (value.isEmpty())
            return QString();
        const QFileInfo info(value);
        if (info.isAbsolute()) {
            return (info.exists() && info.isFile() && info.isExecutable())
                ? info.absoluteFilePath() : QString();
        }
        return QStandardPaths::findExecutable(value);
    };

    const QString override = resolveExecutable(qEnvironmentVariable("COLOSSEUM_MPV"));
    if (!override.isEmpty())
        return override;

    QStringList bundled;
#ifdef Q_OS_WIN
    bundled << QCoreApplication::applicationDirPath() + QStringLiteral("/mpv.exe")
            << QCoreApplication::applicationDirPath() + QStringLiteral("/mpv/mpv.exe")
            << QStringLiteral("C:/mpv/mpv.exe")
            << QStringLiteral("C:/Program Files/mpv/mpv.exe");
#else
    bundled << QCoreApplication::applicationDirPath() + QStringLiteral("/mpv")
            << QCoreApplication::applicationDirPath() + QStringLiteral("/mpv/mpv");
#endif
    for (const QString &candidate : bundled) {
        const QString hit = resolveExecutable(candidate);
        if (!hit.isEmpty())
            return hit;
    }
    return QStandardPaths::findExecutable(QStringLiteral("mpv"));
}

QString LiveStore::sanitizeFilePart(const QString &value) const {
    QString out = value.simplified();
    out.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")), QStringLiteral("_"));
    out = out.trimmed();
    if (out.size() > 80)
        out = out.left(80).trimmed();
    return out;
}

void LiveStore::startProgressTimer() {
    if (!m_recorders.isEmpty() && !m_recordingTimer.isActive())
        m_recordingTimer.start();
}

void LiveStore::updateRecordingProgress() {
    if (m_recorders.isEmpty()) {
        m_recordingTimer.stop();
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool sessionChanged = false;
    const auto ids = m_recorders.keys();
    for (const QString &id : ids) {
        const int idx = findRecording(id);
        if (idx < 0)
            continue;
        QVariantMap session = m_recordings.at(idx).toMap();
        const qint64 started = session.value(QStringLiteral("startedAtMs")).toLongLong();
        const qint64 elapsed = qMax<qint64>(0, (now - started) / 1000);
        const QString outputPath = session.value(QStringLiteral("outputPath")).toString();
        const qint64 bytes = QFileInfo(outputPath).exists() ? QFileInfo(outputPath).size() : 0;
        session.insert(QStringLiteral("elapsedSec"), elapsed);
        session.insert(QStringLiteral("bytesWritten"), bytes);
        m_recordings[idx] = session;
        sessionChanged = true;

        const int planned = session.value(QStringLiteral("plannedDurationSec")).toInt();
        if (planned > 0 && elapsed >= planned)
            finishRecording(id);
    }
    if (sessionChanged)
        emit changed();
}

void LiveStore::finishRecording(const QString &id, const QString &error) {
    const int idx = findRecording(id);
    bool sessionChanged = false;
    if (idx >= 0) {
        QVariantMap session = m_recordings.at(idx).toMap();
        if (session.value(QStringLiteral("state")).toString() == QStringLiteral("recording")) {
            const qint64 started = session.value(QStringLiteral("startedAtMs")).toLongLong();
            const qint64 elapsed = qMax<qint64>(0, (QDateTime::currentMSecsSinceEpoch() - started) / 1000);
            const QString outputPath = session.value(QStringLiteral("outputPath")).toString();
            const qint64 bytes = QFileInfo(outputPath).exists() ? QFileInfo(outputPath).size() : 0;
            session.insert(QStringLiteral("elapsedSec"), elapsed);
            session.insert(QStringLiteral("bytesWritten"), bytes);
            if (error.isEmpty()) {
                session.insert(QStringLiteral("state"), QStringLiteral("done"));
                session.insert(QStringLiteral("error"), QString());
            } else {
                session.insert(QStringLiteral("state"), QStringLiteral("error"));
                session.insert(QStringLiteral("error"), error);
            }
            sessionChanged = true;
            m_recordings[idx] = session;
        }
    }
    if (sessionChanged)
        emit changed();

    if (m_recorders.contains(id))
        requestRecorderStop(id);
    if (m_recorders.isEmpty())
        m_recordingTimer.stop();
}

void LiveStore::markRecordingError(const QString &id, const QString &error) {
    finishRecording(id, error);
}

void LiveStore::handleRecorderStarted(const QString &id) {
    if (m_recorders.contains(id))
        startProgressTimer();
}

void LiveStore::handleRecorderFinished(const QString &id, int exitCode, QProcess::ExitStatus status) {
    QProcess *process = m_recorders.value(id);
    if (!process)
        return;

    const int idx = findRecording(id);
    bool sessionChanged = false;
    if (idx >= 0) {
        QVariantMap session = m_recordings.at(idx).toMap();
        if (session.value(QStringLiteral("state")).toString() == QStringLiteral("recording")) {
            const qint64 started = session.value(QStringLiteral("startedAtMs")).toLongLong();
            const qint64 elapsed = qMax<qint64>(0, (QDateTime::currentMSecsSinceEpoch() - started) / 1000);
            const QString outputPath = session.value(QStringLiteral("outputPath")).toString();
            const qint64 bytes = QFileInfo(outputPath).exists() ? QFileInfo(outputPath).size() : 0;
            const QString error = (status == QProcess::NormalExit && exitCode == 0)
                ? QString() : QStringLiteral("mpv exited unexpectedly.");
            session.insert(QStringLiteral("elapsedSec"), elapsed);
            session.insert(QStringLiteral("bytesWritten"), bytes);
            if (error.isEmpty()) {
                session.insert(QStringLiteral("state"), QStringLiteral("done"));
                session.insert(QStringLiteral("error"), QString());
            } else {
                session.insert(QStringLiteral("state"), QStringLiteral("error"));
                session.insert(QStringLiteral("error"), error);
            }
            m_recordings[idx] = session;
            sessionChanged = true;
        }
    }

    cleanupRecorder(id);
    if (sessionChanged)
        emit changed();
}

void LiveStore::handleRecorderError(const QString &id) {
    if (!m_recorders.contains(id))
        return;
    const int idx = findRecording(id);
    const bool recording = idx >= 0
        && m_recordings.at(idx).toMap().value(QStringLiteral("state")).toString()
               == QStringLiteral("recording");
    if (recording)
        markRecordingError(id, QStringLiteral("Could not start mpv for DVR recording."));
    else
        requestRecorderStop(id);
}

void LiveStore::requestRecorderStop(const QString &id) {
    QProcess *process = m_recorders.value(id);
    if (!process)
        return;
    if (process->state() == QProcess::NotRunning) {
        cleanupRecorder(id);
        return;
    }
    if (m_recorderKillTimers.contains(id))
        return;

    process->terminate();
    auto *killTimer = new QTimer(this);
    killTimer->setSingleShot(true);
    connect(killTimer, &QTimer::timeout, this, [this, id] {
        QProcess *stalled = m_recorders.value(id);
        if (stalled && stalled->state() != QProcess::NotRunning) {
            stalled->kill();
            // Keep ownership until QProcess emits finished(). This lets the normal
            // asynchronous completion path observe the kill and prevents QObject's
            // parent teardown from destroying a still-running child.
            return;
        }
        cleanupRecorder(id);
    });
    m_recorderKillTimers.insert(id, killTimer);
    killTimer->start(1200);
}

void LiveStore::cleanupRecorder(const QString &id) {
    if (QTimer *killTimer = m_recorderKillTimers.take(id)) {
        killTimer->stop();
        killTimer->deleteLater();
    }

    QProcess *process = m_recorders.take(id);
    if (!process)
        return;
    process->disconnect(this);
    process->deleteLater();
    if (m_recorders.isEmpty())
        m_recordingTimer.stop();
}
