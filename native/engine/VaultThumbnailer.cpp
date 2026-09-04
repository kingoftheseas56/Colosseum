#include "VaultThumbnailer.h"

#include "VaultCacheKey.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr int kStallMs = 10000;          // same bound as SeekThumbnailer — never leave a zombie ffmpeg
constexpr int kMaxInFlight = 3;          // small bounded concurrency, not "one big batch of forks"
constexpr double kOffsetFraction = 0.10; // "~10% into the file"
constexpr double kOffsetFloorSec = 3.0;  // "floored to a few seconds in" — skip black/logo lead-in
constexpr double kFallbackOffsetSec = 60.0; // duration unknown (spec-mandated fixed fallback)

QString findFfmpeg()
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
    return pathHit;
}

} // namespace

VaultThumbnailer::VaultThumbnailer(QString cacheDir, QObject* parent)
    : QObject(parent), m_cacheDir(std::move(cacheDir))
{
    QDir().mkpath(thumbsDir());
}

VaultThumbnailer::~VaultThumbnailer()
{
    const QList<QProcess*> running = m_jobs.keys();
    for (QProcess* proc : running) {
        const Job job = m_jobs.value(proc);
        proc->disconnect(this);
        proc->kill();
        proc->deleteLater();
        if (job.stallTimer)
            job.stallTimer->deleteLater();
        QFile::remove(job.tmpPath);
    }
    m_jobs.clear();
}

QString VaultThumbnailer::thumbsDir() const
{
    return QDir(m_cacheDir).filePath(QStringLiteral("thumbs"));
}

QString VaultThumbnailer::outputPathForKey(const QString& key) const
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(thumbsDir()).filePath(QString::fromLatin1(hash) + QStringLiteral(".jpg"));
}

// ~10% into the file, floored to a few seconds so a fixed logo/black lead-in never
// leads the still. A duration so short that the floor would overshoot it (test
// fixtures are often 1-2s) still lands the offset safely INSIDE the clip rather than
// past EOF, where ffmpeg would decode nothing.
double VaultThumbnailer::offsetSecondsForDuration(double durationSec)
{
    if (durationSec <= 0.0)
        return kFallbackOffsetSec; // unknown duration: spec-mandated fixed fallback
    double offset = std::max(durationSec * kOffsetFraction, kOffsetFloorSec);
    if (offset >= durationSec)
        offset = durationSec / 2.0;
    return offset;
}

QString VaultThumbnailer::cachedThumbPath(const QString& path, qint64 size, qint64 mtimeMs) const
{
    const QString key = VaultCacheKey::make(path, size, mtimeMs);
    const QString out = outputPathForKey(key);
    return QFileInfo::exists(out) ? out : QString();
}

QString VaultThumbnailer::requestThumb(const QString& path, qint64 size, qint64 mtimeMs,
                                        double knownDurationSec)
{
    const QString key = VaultCacheKey::make(path, size, mtimeMs);
    const QString out = outputPathForKey(key);

    // Idempotent cache-hit short-circuit — the negative control disables exactly this
    // branch to prove the "no re-spawn on a hit" test actually depends on it.
    if (QFileInfo::exists(out))
        return out;

    if (m_activeKeys.contains(key))
        return QString(); // already grabbing (running or queued) — caller waits on thumbReady

    const double offset = offsetSecondsForDuration(knownDurationSec);
    m_activeKeys.insert(key);
    if (m_jobs.size() >= kMaxInFlight) {
        m_pending.enqueue({key, path, offset});
        return QString();
    }
    startJob(key, path, offset);
    return QString();
}

void VaultThumbnailer::startJob(const QString& key, const QString& sourcePath, double offsetSec)
{
    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty()) {
        m_activeKeys.remove(key); // silent fallback: no thumb, same contract as SeekThumbnailer
        return;
    }

    QDir().mkpath(thumbsDir());
    const QString out = outputPathForKey(key);
    const QString tmp = out + QStringLiteral(".tmp");
    QFile::remove(tmp);

    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    auto* stallTimer = new QTimer(this);
    stallTimer->setSingleShot(true);
    stallTimer->setInterval(kStallMs);
    connect(stallTimer, &QTimer::timeout, this, [this, proc]() { killJob(proc); });

    m_jobs.insert(proc, Job{key, tmp, out, stallTimer});

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc](int exitCode, QProcess::ExitStatus status) {
                finishJob(proc, exitCode, status);
            });

    const QStringList args {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"),
        QStringLiteral("-ss"), QString::number(offsetSec, 'f', 2),
        QStringLiteral("-i"), sourcePath,
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-vf"), QStringLiteral("scale=320:-2"),
        QStringLiteral("-f"), QStringLiteral("mjpeg"),
        tmp,
    };
    proc->start(ffmpeg, args);
    stallTimer->start();
}

void VaultThumbnailer::killJob(QProcess* proc)
{
    if (!m_jobs.contains(proc))
        return;
    const Job job = m_jobs.take(proc);
    proc->disconnect(this);   // a killed job must not file its corpse as a result
    proc->kill();
    proc->deleteLater();
    if (job.stallTimer)
        job.stallTimer->deleteLater();
    m_activeKeys.remove(job.key);
    QFile::remove(job.tmpPath);
    pumpQueue();
}

void VaultThumbnailer::finishJob(QProcess* proc, int exitCode, QProcess::ExitStatus status)
{
    if (!m_jobs.contains(proc))
        return; // already reaped by the stall timer
    const Job job = m_jobs.take(proc);
    if (job.stallTimer) {
        job.stallTimer->stop();
        job.stallTimer->deleteLater();
    }
    proc->deleteLater();
    m_activeKeys.remove(job.key);

    const bool ok = exitCode == 0 && status == QProcess::NormalExit
        && QFileInfo::exists(job.tmpPath) && QFileInfo(job.tmpPath).size() > 0;
    if (ok) {
        QFile::remove(job.outPath); // clear any stale leftover before the promote-rename
        if (QFile::rename(job.tmpPath, job.outPath))
            emit thumbReady(job.key, job.outPath);
        else
            QFile::remove(job.tmpPath); // honest failure — no half-written file left behind
    } else {
        QFile::remove(job.tmpPath); // dead stream / bad seek offset files nothing, ever
    }
    pumpQueue();
}

void VaultThumbnailer::pumpQueue()
{
    while (!m_pending.isEmpty() && m_jobs.size() < kMaxInFlight) {
        const PendingRequest req = m_pending.dequeue();
        // A same-key request may have completed elsewhere while this one waited in queue.
        const QString out = outputPathForKey(req.key);
        if (QFileInfo::exists(out)) {
            m_activeKeys.remove(req.key);
            continue;
        }
        startJob(req.key, req.sourcePath, req.offsetSec);
    }
}
