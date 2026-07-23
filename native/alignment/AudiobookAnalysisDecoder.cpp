#include "AudiobookAnalysisDecoder.h"

#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstring>

namespace alignment {
namespace {

// Bounded I/O constants. The chunk is deliberately small so the decode loop
// consults the WorkContext several times across a large window (and so a cancel is
// honored after at most one small read). The read/overall timeouts keep the loop
// from ever hanging on a wedged process.
constexpr int    kChunkBytes     = 32 * 1024;   // 8k float samples per read
constexpr int    kStartTimeoutMs = 15000;
constexpr int    kReadTimeoutMs  = 5000;
constexpr qint64 kMaxDecodeMs    = 120000;      // hard wall so the loop can't hang
constexpr int    kProbeTimeoutMs = 15000;

QString samplesToSha(const QByteArray &rawF32le)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(rawF32le, QCryptographicHash::Sha256).toHex());
}

// Reinterpret a raw little-endian f32 byte buffer as float samples. The bundled
// ffmpeg and this decoder both run on x86-64 (little-endian), so f32le maps
// straight onto the machine's float layout — no per-sample byte-swap needed.
QVector<float> bytesToSamples(const QByteArray &raw)
{
    const int n = static_cast<int>(raw.size() / static_cast<int>(sizeof(float)));
    QVector<float> out(n);
    if (n > 0)
        std::memcpy(out.data(), raw.constData(), static_cast<size_t>(n) * sizeof(float));
    return out;
}

QByteArray samplesToBytes(const QVector<float> &samples)
{
    return QByteArray(reinterpret_cast<const char *>(samples.constData()),
                      static_cast<int>(samples.size() * sizeof(float)));
}

} // namespace

AudiobookAnalysisDecoder::AudiobookAnalysisDecoder(const QString &cacheRoot)
{
    if (!cacheRoot.isEmpty()) {
        m_cacheRoot = cacheRoot;
    } else {
        const QString base =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_cacheRoot = QDir(base).filePath(QStringLiteral("alignment/cache"));
    }
}

QString AudiobookAnalysisDecoder::resolveFfmpeg() const
{
    const QString bundled =
        QCoreApplication::applicationDirPath() + QStringLiteral("/tools/ffmpeg.exe");
    if (QFileInfo::exists(bundled))
        return bundled;
    // Fall back to a plain ffmpeg on PATH only when the bundled one is absent.
    return QStringLiteral("ffmpeg");
}

QString AudiobookAnalysisDecoder::cachePath(const QString &cacheKey,
                                            qint64 startMs, qint64 endMs) const
{
    const QString dir = QDir(m_cacheRoot).filePath(cacheKey);
    return QDir(dir).filePath(
        QStringLiteral("%1-%2.pcm").arg(startMs).arg(endMs));
}

PcmWindow AudiobookAnalysisDecoder::decodeWindow(const QString &file,
                                                 qint64 startMs, qint64 endMs,
                                                 const QString &cacheKey,
                                                 work::WorkContext &ctx) const
{
    PcmWindow win;
    win.startMs = startMs;
    win.endMs = endMs;

    // ── Cache hit: serve from disk, spawn nothing ────────────────────────────
    const QString cacheFile = cachePath(cacheKey, startMs, endMs);
    if (!cacheKey.isEmpty() && QFileInfo::exists(cacheFile)) {
        QFile f(cacheFile);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray raw = f.readAll();
            f.close();
            if (!raw.isEmpty()) {
                win.samples = bytesToSamples(raw);
                win.sha256 = samplesToSha(raw);
                win.ok = true;
                return win;
            }
        }
        // An unreadable/empty cache file falls through to a fresh decode.
    }

    // ── Fresh decode via bundled ffmpeg ──────────────────────────────────────
    const double startSec = static_cast<double>(startMs) / 1000.0;
    const double endSec = static_cast<double>(endMs) / 1000.0;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);

    const QStringList args = {
        QStringLiteral("-nostdin"),
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-ss"), QString::number(startSec, 'f', 3),
        QStringLiteral("-to"), QString::number(endSec, 'f', 3),
        QStringLiteral("-i"), file,
        QStringLiteral("-ac"), QStringLiteral("1"),
        QStringLiteral("-ar"), QStringLiteral("16000"),
        QStringLiteral("-f"), QStringLiteral("f32le"),
        QStringLiteral("pipe:1"),
    };

    m_spawnCount.fetch_add(1);
    proc.start(resolveFfmpeg(), args, QIODevice::ReadOnly);
    if (!proc.waitForStarted(kStartTimeoutMs)) {
        win.ok = false;
        win.failure = FailureCode::AudioDecodeFailed;
        return win;
    }

    QByteArray raw;
    bool cancelled = false;
    QElapsedTimer wall;
    wall.start();

    for (;;) {
        // Make some data (or a process exit) available before reading, bounded.
        if (proc.bytesAvailable() == 0 && proc.state() == QProcess::Running)
            proc.waitForReadyRead(kReadTimeoutMs);

        const QByteArray chunk = proc.read(kChunkBytes); // bounded read
        if (!chunk.isEmpty())
            raw.append(chunk);

        // Yield / cancel point BETWEEN reads.
        if (!ctx.checkpoint()) {
            cancelled = true;
            break;
        }

        if (chunk.isEmpty() && proc.state() == QProcess::NotRunning
            && proc.bytesAvailable() == 0)
            break; // clean end of stream

        if (wall.elapsed() > kMaxDecodeMs)
            break; // hard wall — never hang
    }

    if (cancelled) {
        proc.kill();
        proc.waitForFinished(2000);
        win.ok = false;             // a cancel is not a failure
        win.failure = FailureCode::None;
        win.samples.clear();
        return win;
    }

    proc.waitForFinished(kReadTimeoutMs);
    raw.append(proc.readAllStandardOutput()); // drain anything left

    const bool failedExit = proc.exitStatus() != QProcess::NormalExit
                            || proc.exitCode() != 0;
    if (failedExit || raw.isEmpty()) {
        win.ok = false;
        win.failure = FailureCode::AudioDecodeFailed;
        win.samples.clear();
        return win;
    }

    win.samples = bytesToSamples(raw);
    win.sha256 = samplesToSha(raw);
    win.ok = true;

    // ── Cache the decoded window atomically (temp-then-rename via QSaveFile) ──
    if (!cacheKey.isEmpty()) {
        const QString dir = QDir(m_cacheRoot).filePath(cacheKey);
        QDir().mkpath(dir);
        QSaveFile out(cacheFile);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(raw);
            out.commit(); // atomic rename; a failed commit just skips caching
        }
    }

    return win;
}

QList<AudioChapter> AudiobookAnalysisDecoder::probeChapters(const QStringList &files) const
{
    QList<AudioChapter> chapters;
    // Duration: 00:00:02.00, bitrate: ...  (fraction digits vary across builds)
    static const QRegularExpression durRe(
        QStringLiteral("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)"));

    const QString ffmpeg = resolveFfmpeg();
    qint64 cursor = 0;
    int index = 0;

    for (const QString &file : files) {
        qint64 durationMs = 0;

        QProcess proc;
        m_spawnCount.fetch_add(1);
        // No output file: ffmpeg prints the container Duration on stderr then
        // exits nonzero ("At least one output file...") — the exit is expected.
        proc.start(ffmpeg, {QStringLiteral("-i"), file}, QIODevice::ReadOnly);
        if (proc.waitForStarted(kStartTimeoutMs)) {
            proc.waitForFinished(kProbeTimeoutMs);
            const QString err = QString::fromUtf8(proc.readAllStandardError());
            const QRegularExpressionMatch m = durRe.match(err);
            if (m.hasMatch()) {
                const qint64 h = m.captured(1).toLongLong();
                const qint64 mm = m.captured(2).toLongLong();
                const double ss = m.captured(3).toDouble();
                durationMs = ((h * 60 + mm) * 60) * 1000
                             + static_cast<qint64>(ss * 1000.0 + 0.5);
            }
        }

        AudioChapter ch;
        ch.index = index++;
        ch.file = file;
        ch.startMs = cursor;
        ch.durationMs = durationMs;
        ch.endMs = cursor + durationMs;
        cursor = ch.endMs;
        chapters.append(ch);
    }

    return chapters;
}

} // namespace alignment
