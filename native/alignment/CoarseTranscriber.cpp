#include "CoarseTranscriber.h"

#include "models/ModelManifest.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>

namespace alignment {
namespace {

// Bounded run constants. whisper-cli is opaque (one WAV -> one JSON), so the wait is
// polled: a cancel between polls kills the child promptly, and a hard wall keeps a
// wedged process from ever hanging the worker. base.en on a bounded window is tens
// of seconds on CPU; the wall is a generous safety net, not a normal deadline.
constexpr int    kStartTimeoutMs = 15000;
constexpr int    kPollMs         = 250;
constexpr qint64 kMaxRunMs       = 150000; // hard wall so the loop can't hang
constexpr int    kKillWaitMs     = 2000;

// Append a little-endian unsigned value (2 or 4 bytes) to a byte buffer.
void appendLe(QByteArray &out, quint32 value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
}

// Write the window's 16 kHz mono float samples as a canonical 16 kHz mono s16 WAV —
// the shape whisper-cli reads. f32 [-1,1] is clamped and scaled to s16; the header
// is the standard 44-byte PCM RIFF/WAVE. Returns false if the file can't be written.
bool writeMono16kS16Wav(const QString &path, const QVector<float> &samples)
{
    constexpr quint32 sampleRate = 16000;
    constexpr quint16 channels = 1;
    constexpr quint16 bitsPerSample = 16;
    const quint32 blockAlign = channels * (bitsPerSample / 8);
    const quint32 byteRate = sampleRate * blockAlign;
    const quint32 dataBytes = static_cast<quint32>(samples.size()) * (bitsPerSample / 8);

    QByteArray buf;
    buf.reserve(44 + static_cast<int>(dataBytes));

    buf.append("RIFF", 4);
    appendLe(buf, 36 + dataBytes, 4); // RIFF chunk size
    buf.append("WAVE", 4);

    buf.append("fmt ", 4);
    appendLe(buf, 16, 4);               // fmt chunk size
    appendLe(buf, 1, 2);                // audioFormat = PCM
    appendLe(buf, channels, 2);
    appendLe(buf, sampleRate, 4);
    appendLe(buf, byteRate, 4);
    appendLe(buf, blockAlign, 2);
    appendLe(buf, bitsPerSample, 2);

    buf.append("data", 4);
    appendLe(buf, dataBytes, 4);

    for (float v : samples) {
        const float c = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
        const auto s = static_cast<qint16>(std::lround(c * 32767.0f));
        appendLe(buf, static_cast<quint32>(static_cast<quint16>(s)), 2);
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const qint64 written = f.write(buf);
    f.close();
    return written == buf.size();
}

} // namespace

CoarseTranscriber::CoarseTranscriber(QString modelPath, QString whisperExe)
    : m_modelPath(std::move(modelPath)), m_whisperExe(std::move(whisperExe))
{
}

QString CoarseTranscriber::resolveWhisper() const
{
    if (!m_whisperExe.isEmpty())
        return m_whisperExe;
    const QString bundled = QCoreApplication::applicationDirPath()
                            + QStringLiteral("/tools/whisper/whisper-cli.exe");
    if (QFileInfo::exists(bundled))
        return bundled;
    // Fall back to a plain whisper-cli on PATH only when the bundled one is absent.
    return QStringLiteral("whisper-cli");
}

FailureCode CoarseTranscriber::validateModel() const
{
    // The manifest.json lives beside the model file. Load it, then stream the model
    // through SHA-256 against the manifest's digest. A missing/invalid manifest can't
    // vouch for integrity, so we degrade to a bare existence check on the model file.
    const QString manifestPath =
        QDir(QFileInfo(m_modelPath).absolutePath()).filePath(QStringLiteral("manifest.json"));

    models::ManifestError merr = models::ManifestError::None;
    const std::optional<models::ModelManifest> manifest =
        models::ModelManifest::load(manifestPath, &merr);

    if (manifest) {
        switch (manifest->validateChecksum()) {
        case models::ManifestError::FileMissing:
            return FailureCode::ModelMissing;
        case models::ManifestError::ChecksumFailed:
            return FailureCode::ModelChecksumFailed;
        default:
            return FailureCode::None;
        }
    }

    // No usable manifest — treat an absent model file as ModelMissing.
    return QFileInfo::exists(m_modelPath) ? FailureCode::None : FailureCode::ModelMissing;
}

QList<CoarseSegment> CoarseTranscriber::transcribe(const PcmWindow &window,
                                                   work::WorkContext &ctx,
                                                   FailureCode *failure) const
{
    if (failure)
        *failure = FailureCode::None;

    // ── Integrity gate: never spend a transcription pass on a bad model ──────────
    const FailureCode modelFailure = validateModel();
    if (modelFailure != FailureCode::None) {
        if (failure)
            *failure = modelFailure;
        return {};
    }

    // ── Cancel BEFORE spawn: a cancel that arrives first launches no whisper ─────
    if (!ctx.checkpoint())
        return {};

    // ── Stage the window as a temp 16 kHz mono s16 WAV whisper-cli can read ──────
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return {};
    const QString wavPath = QDir(tmp.path()).filePath(QStringLiteral("in.wav"));
    const QString outBase = QDir(tmp.path()).filePath(QStringLiteral("out"));
    const QString jsonPath = outBase + QStringLiteral(".json");
    if (!writeMono16kS16Wav(wavPath, window.samples))
        return {};

    // ── Run the bundled CPU whisper-cli (one pass, bounded + yielding) ───────────
    const QStringList args = {
        QStringLiteral("-m"),  m_modelPath,
        QStringLiteral("-f"),  wavPath,
        QStringLiteral("-l"),  QStringLiteral("en"),
        QStringLiteral("-oj"),
        QStringLiteral("-of"), outBase,
        QStringLiteral("-nt"),
    };

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(resolveWhisper(), args, QIODevice::ReadOnly);
    if (!proc.waitForStarted(kStartTimeoutMs))
        return {}; // engine could not start — not a model problem

    QElapsedTimer wall;
    wall.start();
    for (;;) {
        if (proc.waitForFinished(kPollMs))
            break; // finished (normal or crashed) within this poll
        if (proc.state() == QProcess::NotRunning)
            break;
        if (!ctx.checkpoint()) { // cancel arrived during the run
            proc.kill();
            proc.waitForFinished(kKillWaitMs);
            return {};
        }
        if (wall.elapsed() > kMaxRunMs) { // hard wall — never hang
            proc.kill();
            proc.waitForFinished(kKillWaitMs);
            return {};
        }
    }

    const bool failedExit = proc.exitStatus() != QProcess::NormalExit
                            || proc.exitCode() != 0;
    if (failedExit)
        return {}; // engine failure — *failure stays None (not a model problem)

    // A cancel that landed after a fast finish still discards the (stale) result.
    if (!ctx.checkpoint())
        return {};

    // ── Parse whisper's JSON: transcription[] with ms offsets + text ─────────────
    QFile jf(jsonPath);
    if (!jf.open(QIODevice::ReadOnly))
        return {}; // no JSON (e.g. silent window) — zero segments, not a failure
    const QByteArray raw = jf.readAll();
    jf.close();

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    const QJsonArray transcription =
        doc.object().value(QStringLiteral("transcription")).toArray();

    QList<CoarseSegment> segments;
    segments.reserve(transcription.size());
    for (const QJsonValue &v : transcription) {
        const QJsonObject seg = v.toObject();
        const QJsonObject offsets = seg.value(QStringLiteral("offsets")).toObject();

        CoarseSegment out;
        // offsets.from / offsets.to are already in milliseconds.
        out.startMs = static_cast<qint64>(offsets.value(QStringLiteral("from")).toDouble());
        out.endMs   = static_cast<qint64>(offsets.value(QStringLiteral("to")).toDouble());
        out.text    = seg.value(QStringLiteral("text")).toString().trimmed();
        // whisper emits no per-segment confidence — coarse evidence carries a fixed 1.0.
        out.confidence = 1.0;

        if (out.text.isEmpty())
            continue; // a blank/silent span is not matching evidence
        segments.append(out);
    }

    return segments;
}

} // namespace alignment
