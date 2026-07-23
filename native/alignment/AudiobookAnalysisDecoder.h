#pragma once

// AudiobookAnalysisDecoder — the read-along's ear on the audiobook file.
//
// One small thing: hand it an audio file and a [startMs, endMs) window and it
// returns that window decoded to the exact shape the alignment engine listens in
// — 16 kHz, mono, 32-bit float PCM — using the app-bundled ffmpeg.exe. Nothing
// here transcribes or aligns; it only turns a compressed audiobook into the raw
// samples the coarse (whisper) and fine (CTC) stages consume, one bounded window
// at a time so a long book never loads whole.
//
// Two honesty guarantees:
//   • Resumable + cheap to repeat. A decoded window is cached atomically on disk
//     under <cacheRoot>/<cacheKey>/<startMs>-<endMs>.pcm (write-temp-then-rename),
//     so a re-request after a pause/restart reads the cache and spawns no process.
//   • It yields. The decode reads ffmpeg's stdout in bounded chunks and consults
//     the WorkContext between reads, so a cancel/pause from the shared background
//     coordinator stops it promptly instead of blocking the worker.
//
// A corrupt or unreadable input is never guessed around: ffmpeg failing (nonzero
// exit or empty output on a real window) returns ok=false with AudioDecodeFailed,
// the frozen wire code the store persists.
//
// probeChapters() reads each ordered audio file's duration (ffmpeg's Duration:
// line) and lays the files end-to-end into contiguous absolute [startMs, endMs)
// spans — the chapter timeline the rest of the pipeline schedules against.
//
// Consumes Qt Core only (QProcess lives in Core). Not a QObject — plain, testable
// subprocess work. The service (Task 3+) is the QML-facing façade.
//
// Design authority: docs/superpowers/plans/2026-07-22-audiobook-epub-read-along.md.
// [Agent 2 (Claude), biblio]

#include "AlignmentTypes.h"

#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <QtGlobal>

#include <atomic>

// WorkContext is passed by reference only — a forward declaration keeps this
// header Core-light; the .cpp pulls in the full coordinator.
namespace work { class WorkContext; }

namespace alignment {

// One decoded audio window in the engine's canonical listening format. `ok` is the
// gate: when false, `failure` says why (Cancelled decodes leave it None — a cancel
// is not a decode failure) and `samples` is empty. `sha256` is the hex digest of
// the raw f32le bytes, so an identical window decoded twice is provably identical.
struct PcmWindow {
    int sampleRate = 16000;
    int channels = 1;
    QVector<float> samples;
    qint64 startMs = 0;
    qint64 endMs = 0;
    bool ok = false;
    alignment::FailureCode failure = alignment::FailureCode::None;
    QString sha256; // hex of the raw f32le bytes
};

// One audiobook file placed on the absolute chapter timeline. Files are ordered as
// given; chapter i begins where chapter i-1 ended.
struct AudioChapter {
    int index = 0;
    QString file;
    qint64 startMs = 0;
    qint64 endMs = 0;
    qint64 durationMs = 0;
};

class AudiobookAnalysisDecoder {
public:
    // cacheRoot empty -> <AppDataLocation>/alignment/cache. The harness injects a
    // QTemporaryDir so a test run never touches the real cache.
    explicit AudiobookAnalysisDecoder(const QString &cacheRoot = QString());

    // Decode [startMs, endMs) of `file` to 16 kHz mono f32 PCM. Reads ffmpeg's
    // stdout in bounded chunks, calling ctx.checkpoint() between reads; if it goes
    // false the decode stops promptly and returns ok=false (failure=None). A cached
    // window for (cacheKey, startMs, endMs) is served from disk with no ffmpeg
    // spawn. ffmpeg failure / nonzero exit / empty output -> ok=false,
    // failure=AudioDecodeFailed.
    PcmWindow decodeWindow(const QString &file, qint64 startMs, qint64 endMs,
                           const QString &cacheKey, work::WorkContext &ctx) const;

    // Probe each ordered file's duration and lay them into contiguous absolute
    // spans. Ordering is the input file order. A file whose duration can't be read
    // contributes a zero-length span (durationMs = 0) rather than breaking the run.
    QList<AudioChapter> probeChapters(const QStringList &files) const;

    // How many ffmpeg processes this decoder has spawned. Lets a test prove cache
    // reuse costs zero new processes.
    qint64 spawnCount() const { return m_spawnCount.load(); }

private:
    QString resolveFfmpeg() const;                 // bundled tools/ffmpeg.exe, else PATH
    QString cachePath(const QString &cacheKey, qint64 startMs, qint64 endMs) const;

    QString m_cacheRoot;
    mutable std::atomic<qint64> m_spawnCount{0};
};

} // namespace alignment
