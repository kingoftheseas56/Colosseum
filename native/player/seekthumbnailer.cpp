#include "seekthumbnailer.h"

#include "mpvitem.h"

#include <QByteArray>

namespace {
constexpr double kBucketSecs = 5.0;
constexpr int kStallMs = 10000;   // torrent-HTTP seek can stall; never leave a zombie ffmpeg
}

SeekThumbnailer::SeekThumbnailer(QObject *parent)
    : QObject(parent)
{
    m_stallTimer.setSingleShot(true);
    m_stallTimer.setInterval(kStallMs);
    connect(&m_stallTimer, &QTimer::timeout, this, &SeekThumbnailer::killJob);
}

SeekThumbnailer::~SeekThumbnailer()
{
    killJob();
}

qint64 SeekThumbnailer::bucketOf(double timeSec)
{
    if (timeSec < 0)
        timeSec = 0;
    return static_cast<qint64>(timeSec / kBucketSecs) * static_cast<qint64>(kBucketSecs);
}

void SeekThumbnailer::request(const QUrl &source, double timeSec)
{
    if (source.isEmpty())
        return;
    if (source != m_source) {
        reset();
        m_source = source;
    }
    const qint64 bucket = bucketOf(timeSec);
    if (const QString *hit = m_cache.object(bucket)) {
        Q_EMIT thumbReady(static_cast<double>(bucket), *hit);
        return;
    }
    // Harbor's liveness rules (thumb-preview.tsx): while the exact frame loads, serve the
    // NEAREST cached neighbour so a sweeping cursor sees motion, and NEVER kill an
    // in-flight fetch — each completed frame fills the cache; the newest hover waits as
    // the single pending slot (latest-wins). Killing on every bucket change is what made
    // sweeps complete nothing and freeze on frame #1 (2026-07-18).
    if (const QString *near = nearestCached(bucket))
        Q_EMIT thumbReady(static_cast<double>(bucket), *near);
    if (m_proc) {
        if (m_jobBucket != bucket)
            m_pendingBucket = bucket;
        return;
    }
    startJob(bucket);
}

const QString *SeekThumbnailer::nearestCached(qint64 bucket) const
{
    constexpr qint64 kBucket = static_cast<qint64>(kBucketSecs);
    for (qint64 k = 1; k <= 6; ++k) {           // ±30s window, same as Harbor's
        if (const QString *hit = m_cache.object(bucket + k * kBucket))
            return hit;
        if (const QString *hit = m_cache.object(bucket - k * kBucket))
            return hit;
    }
    return nullptr;
}

void SeekThumbnailer::reset()
{
    killJob();
    m_pendingBucket = -1;
    m_cache.clear();
    m_source = QUrl();
}

void SeekThumbnailer::startJob(qint64 bucket)
{
    const QString ffmpeg = MpvItem::findFfmpeg();
    if (ffmpeg.isEmpty())
        return;                   // silent fallback: tooltip stays timestamp-only

    m_jobBucket = bucket;
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SeekThumbnailer::onJobFinished);

    const QString src = m_source.isLocalFile() ? m_source.toLocalFile() : m_source.toString();
    const QStringList args {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-ss"), QString::number(bucket),
        QStringLiteral("-i"), src,
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-vf"), QStringLiteral("scale=320:-2"),
        QStringLiteral("-f"), QStringLiteral("mjpeg"),
        QStringLiteral("pipe:1"),
    };
    m_proc->start(ffmpeg, args);
    m_stallTimer.start();
}

void SeekThumbnailer::killJob()
{
    m_stallTimer.stop();
    if (!m_proc)
        return;
    m_proc->disconnect(this);     // a killed job must not file its corpse as a result
    m_proc->kill();
    m_proc->deleteLater();
    m_proc = nullptr;
    m_jobBucket = -1;
}

void SeekThumbnailer::onJobFinished(int exitCode, QProcess::ExitStatus status)
{
    m_stallTimer.stop();
    QProcess *proc = m_proc;
    const qint64 bucket = m_jobBucket;
    m_proc = nullptr;
    m_jobBucket = -1;
    if (!proc)
        return;
    proc->deleteLater();
    // Failure (dead stream / unseekable spot) files nothing — but ALWAYS falls through to
    // chain the pending hover, or one bad frame would stall the whole pipeline.
    const QByteArray jpeg = (exitCode == 0 && status == QProcess::NormalExit)
                                ? proc->readAllStandardOutput() : QByteArray();
    if (!jpeg.isEmpty()) {
        auto *url = new QString(QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64()));
        m_cache.insert(bucket, url);
        Q_EMIT thumbReady(static_cast<double>(bucket), *url);
    }
    // Chain the newest hover that queued up while this frame was extracting.
    if (m_pendingBucket >= 0) {
        const qint64 next = m_pendingBucket;
        m_pendingBucket = -1;
        if (!m_cache.object(next))
            startJob(next);
    }
}
