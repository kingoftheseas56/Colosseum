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
    if (m_proc) {
        if (m_jobBucket == bucket)
            return;               // already fetching this exact frame
        killJob();                // latest-wins: the hover moved on
    }
    startJob(bucket);
}

void SeekThumbnailer::reset()
{
    killJob();
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
    if (exitCode != 0 || status != QProcess::NormalExit)
        return;                   // dead stream / unseekable source: tooltip stays timestamp-only
    const QByteArray jpeg = proc->readAllStandardOutput();
    if (jpeg.isEmpty())
        return;
    auto *url = new QString(QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64()));
    m_cache.insert(bucket, url);
    Q_EMIT thumbReady(static_cast<double>(bucket), *url);
}
