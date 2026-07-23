// AudiobookAnalysisDecoder harness — bounded 16 kHz mono float decode over the
// app-bundled ffmpeg.exe, with a resumable on-disk cache and a corrupt-input path.
//
// Runs every decode inside a REAL work::BackgroundWorkCoordinator job, so it uses
// the same WorkContext the pipeline gets (its members are coordinator-private —
// there is no other honest way to obtain one). A self-cancelling job proves bounded
// cancellation; a spawn counter proves cache reuse spawns zero processes.
//
// Fixture: tests/fixtures/alignment/audio/exact.wav — 2 s / 16 kHz / mono sine,
// so a [0, 2000) ms window decodes to ~32000 float samples. Exit code = verdict.

#include "alignment/AudiobookAnalysisDecoder.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>

using namespace alignment;

static bool g_failed = false;
static void check(bool ok, const char *msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); g_failed = true; }
}
#define CHECK(cond, msg) check((cond), (msg))

#ifndef ALIGNMENT_AUDIO_DIR
#define ALIGNMENT_AUDIO_DIR "."
#endif

// Run `body(ctx)` as one coordinator job; block up to timeoutMs. Returns true iff
// the job function completed within the deadline (i.e. no hang). The promise is a
// shared_ptr so a (never-expected) timeout can't dangle the worker thread.
static bool runInJob(work::BackgroundWorkCoordinator &coord, const QString &id,
                     std::function<void(work::WorkContext &)> body,
                     int timeoutMs = 30000) {
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    coord.submit({id, 100}, [body, done](work::WorkContext &ctx) {
        body(ctx);
        done->set_value();
        return work::WorkResult::Completed;
    });
    return fut.wait_for(std::chrono::milliseconds(timeoutMs))
           == std::future_status::ready;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    const QString wav =
        QStringLiteral(ALIGNMENT_AUDIO_DIR) + QStringLiteral("/exact.wav");
    CHECK(QFile::exists(wav), "fixture exact.wav present");

    QTemporaryDir cacheDir;
    CHECK(cacheDir.isValid(), "temp cache root");
    QTemporaryDir workDir;
    CHECK(workDir.isValid(), "temp work dir");

    AudiobookAnalysisDecoder decoder(cacheDir.path());
    work::BackgroundWorkCoordinator coord(1);

    // ── 1. bounded 16 kHz mono decode of a [0, 2000) ms window ────────────────
    PcmWindow w1;
    const bool ran1 = runInJob(coord, QStringLiteral("decode-1"),
        [&](work::WorkContext &ctx) {
            w1 = decoder.decodeWindow(wav, 0, 2000, QStringLiteral("k-det1"), ctx);
        });
    CHECK(ran1, "decode job returned within deadline (no hang)");
    CHECK(w1.ok, "window decoded ok");
    CHECK(w1.sampleRate == 16000, "sample rate 16000");
    CHECK(w1.channels == 1, "mono");
    // 2 s * 16000 Hz = 32000 samples; allow a small resampler edge tolerance.
    CHECK(w1.samples.size() >= 31000 && w1.samples.size() <= 33000,
          "sample count ~= 32000 for a 2s window");
    CHECK(!w1.sha256.isEmpty(), "sha256 computed");

    // ── 2. determinism: same window, DIFFERENT key -> a second real ffmpeg run
    //       yields the identical digest (self-consistent, not a frozen oracle) ──
    PcmWindow w2;
    const bool ran2 = runInJob(coord, QStringLiteral("decode-2"),
        [&](work::WorkContext &ctx) {
            w2 = decoder.decodeWindow(wav, 0, 2000, QStringLiteral("k-det2"), ctx);
        });
    CHECK(ran2, "second decode returned (no hang)");
    CHECK(w2.ok, "second window decoded ok");
    CHECK(w2.sha256 == w1.sha256, "same window twice -> IDENTICAL sha256 (deterministic)");
    CHECK(w2.samples == w1.samples, "same window twice -> identical samples");

    // ── 3. cache reuse: same key, second call spawns NO ffmpeg, byte-identical ─
    PcmWindow c1;
    runInJob(coord, QStringLiteral("cache-1"), [&](work::WorkContext &ctx) {
        c1 = decoder.decodeWindow(wav, 0, 2000, QStringLiteral("k-cache"), ctx);
    });
    CHECK(c1.ok, "cache-priming decode ok");
    const QString cachedFile =
        QDir(QDir(cacheDir.path()).filePath(QStringLiteral("k-cache")))
            .filePath(QStringLiteral("0-2000.pcm"));
    CHECK(QFile::exists(cachedFile), "cache file written atomically");

    const qint64 spawnBefore = decoder.spawnCount();
    PcmWindow c2;
    const bool ranC2 = runInJob(coord, QStringLiteral("cache-2"),
        [&](work::WorkContext &ctx) {
            c2 = decoder.decodeWindow(wav, 0, 2000, QStringLiteral("k-cache"), ctx);
        });
    const qint64 spawnAfter = decoder.spawnCount();
    CHECK(ranC2, "cache reuse returned (no hang)");
    CHECK(c2.ok, "cache reuse window ok");
    CHECK(spawnAfter == spawnBefore, "cache reuse spawned NO new ffmpeg process");
    CHECK(c2.samples == c1.samples, "cache reuse -> byte-identical samples");
    CHECK(c2.sha256 == c1.sha256, "cache reuse -> identical sha256");

    // ── 4. bounded cancellation: checkpoint goes false, decode yields ok=false
    //       without hanging. The job cancels ITSELF (deterministic — the cancel
    //       token is set before decodeWindow reads past the first chunk). ────────
    PcmWindow cancelled;
    auto cdone = std::make_shared<std::promise<void>>();
    auto cfut = cdone->get_future();
    const QString cancelId = QStringLiteral("decode-cancel");
    coord.submit({cancelId, 100}, [&, cdone](work::WorkContext &ctx) {
        coord.cancel(cancelId);   // arm this job's own cancel token
        cancelled = decoder.decodeWindow(wav, 0, 2000, QStringLiteral("k-cancel"), ctx);
        cdone->set_value();
        return work::WorkResult::Cancelled;
    });
    const bool cancelReturned =
        cfut.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
    CHECK(cancelReturned, "cancelled decode returned within 10s (no hang)");
    CHECK(!cancelled.ok, "cancelled decode yields ok=false");
    CHECK(cancelled.failure == FailureCode::None, "cancel is not a decode failure");

    // ── 5. corrupt input -> ok=false, AudioDecodeFailed ───────────────────────
    const QString garbage = QDir(workDir.path()).filePath(QStringLiteral("garbage.wav"));
    {
        QFile g(garbage);
        CHECK(g.open(QIODevice::WriteOnly), "write garbage fixture");
        g.write("this is not audio, only garbage bytes\x00\x01\x02\x03", 41);
        g.close();
    }
    PcmWindow bad;
    runInJob(coord, QStringLiteral("decode-bad"), [&](work::WorkContext &ctx) {
        bad = decoder.decodeWindow(garbage, 0, 2000, QStringLiteral("k-bad"), ctx);
    });
    CHECK(!bad.ok, "corrupt input -> ok=false");
    CHECK(bad.failure == FailureCode::AudioDecodeFailed,
          "corrupt input -> AudioDecodeFailed");

    PcmWindow missing;
    runInJob(coord, QStringLiteral("decode-missing"), [&](work::WorkContext &ctx) {
        missing = decoder.decodeWindow(
            QStringLiteral("Z:/no/such/file.m4b"), 0, 2000,
            QStringLiteral("k-missing"), ctx);
    });
    CHECK(!missing.ok, "nonexistent input -> ok=false");
    CHECK(missing.failure == FailureCode::AudioDecodeFailed,
          "nonexistent input -> AudioDecodeFailed");

    // ── 6. probeChapters: two files -> two ordered, contiguous absolute spans ──
    const QString file1 = QDir(workDir.path()).filePath(QStringLiteral("ch1.wav"));
    const QString file2 = QDir(workDir.path()).filePath(QStringLiteral("ch2.wav"));
    CHECK(QFile::copy(wav, file1), "copy fixture -> ch1.wav");
    CHECK(QFile::copy(wav, file2), "copy fixture -> ch2.wav");
    const QList<AudioChapter> chapters = decoder.probeChapters({file1, file2});
    CHECK(chapters.size() == 2, "two files -> two chapters");
    if (chapters.size() == 2) {
        CHECK(chapters[0].index == 0 && chapters[1].index == 1, "chapters in input order");
        CHECK(chapters[0].startMs == 0, "chapter 0 starts at 0");
        CHECK(chapters[0].durationMs >= 1900 && chapters[0].durationMs <= 2100,
              "chapter 0 duration ~= 2000ms");
        CHECK(chapters[0].endMs == chapters[0].startMs + chapters[0].durationMs,
              "chapter 0 span self-consistent");
        CHECK(chapters[1].startMs == chapters[0].endMs,
              "chapter 1 starts where chapter 0 ended (contiguous)");
        CHECK(chapters[1].endMs == chapters[1].startMs + chapters[1].durationMs,
              "chapter 1 span self-consistent");
    }

    if (g_failed) {
        std::fprintf(stderr, "VERDICT: FAIL\n");
        return 1;
    }
    std::fprintf(stdout, "PASS bounded 16k mono decode, cache, cancel, corrupt input\n");
    std::fprintf(stdout, "VERDICT: PASS\n");
    return 0;
}
