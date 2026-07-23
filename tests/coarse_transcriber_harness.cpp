// CoarseTranscriber harness — coarse English speech-to-text over the bundled CPU
// whisper.cpp (whisper-cli.exe), used as MATCHING EVIDENCE only (never displayed).
//
// Chains the real AudiobookAnalysisDecoder: it decodes the known-text fixture
// speech.wav to a 16 kHz mono f32 PcmWindow, then CoarseTranscriber writes that
// window to a temp WAV and runs whisper-cli. The assertions are containment, not
// character-exact, because ASR is not exact: the concatenated transcript must carry
// the sentence's content words, offsets must be monotonic, and the model-integrity
// and cancellation gates must behave.
//
// Every decode/transcribe runs inside a REAL work::BackgroundWorkCoordinator job, so
// it uses the same WorkContext the pipeline gets (its members are coordinator-private
// — there is no other honest way to obtain one). A self-cancelling job proves the
// pre-spawn cancel bails without launching whisper and without hanging.
//
// Fixture: tests/fixtures/alignment/audio/speech.wav — Windows SAPI TTS of
// "the quick brown fox jumps over the lazy dog" (deterministic, known text). It was
// generated offline with:
//   powershell -NoProfile -Command "Add-Type -AssemblyName System.Speech; \
//     $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; $s.Rate = -1; \
//     $s.SetOutputToWaveFile('<abs>/speech.wav'); \
//     $s.Speak('the quick brown fox jumps over the lazy dog'); $s.Dispose()"
// Exit code = verdict.

#include "alignment/AudiobookAnalysisDecoder.h"
#include "alignment/CoarseTranscriber.h"
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
#ifndef COARSE_MODEL_DIR
#define COARSE_MODEL_DIR "."
#endif

// Run `body(ctx)` as one coordinator job; block up to timeoutMs. Returns true iff the
// job function completed within the deadline (i.e. no hang). The promise is a
// shared_ptr so a (never-expected) timeout can't dangle the worker thread.
static bool runInJob(work::BackgroundWorkCoordinator &coord, const QString &id,
                     std::function<void(work::WorkContext &)> body,
                     int timeoutMs) {
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
        QStringLiteral(ALIGNMENT_AUDIO_DIR) + QStringLiteral("/speech.wav");
    const QString modelPath =
        QStringLiteral(COARSE_MODEL_DIR) + QStringLiteral("/ggml-base.en.bin");
    CHECK(QFile::exists(wav), "fixture speech.wav present");
    CHECK(QFile::exists(modelPath), "coarse model ggml-base.en.bin present");

    QTemporaryDir cacheDir;
    CHECK(cacheDir.isValid(), "temp cache root");

    AudiobookAnalysisDecoder decoder(cacheDir.path());
    CoarseTranscriber transcriber(modelPath);
    work::BackgroundWorkCoordinator coord(1);

    // ── 1. decode the known-text fixture to a 16 kHz mono f32 window ─────────────
    PcmWindow pcm;
    const bool decoded = runInJob(coord, QStringLiteral("decode"),
        [&](work::WorkContext &ctx) {
            pcm = decoder.decodeWindow(wav, 0, 4000, QStringLiteral("k-speech"), ctx);
        }, 30000);
    CHECK(decoded, "decode job returned within deadline (no hang)");
    CHECK(pcm.ok, "fixture decoded ok");
    CHECK(!pcm.samples.isEmpty(), "decoded window has samples");

    // ── 2. transcribe: real whisper-cli, one CPU pass (base.en ~ tens of s) ──────
    QList<CoarseSegment> segments;
    FailureCode segFailure = FailureCode::EditionMismatch; // sentinel != None
    const bool transcribed = runInJob(coord, QStringLiteral("transcribe"),
        [&](work::WorkContext &ctx) {
            segments = transcriber.transcribe(pcm, ctx, &segFailure);
        }, 180000);
    CHECK(transcribed, "transcribe job returned within 180s (no hang)");
    CHECK(segFailure == FailureCode::None, "clean transcription sets no failure");
    CHECK(!segments.isEmpty(), "transcription yields at least one segment");

    // Concatenated, lowercased transcript must CARRY the content words (containment,
    // not an exact string — ASR is not character-exact).
    QString transcript;
    for (const CoarseSegment &s : segments)
        transcript += s.text + QLatin1Char(' ');
    const QString lower = transcript.toLower();
    std::fprintf(stderr, "transcript: %s\n", transcript.trimmed().toUtf8().constData());
    for (const char *word : {"quick", "brown", "fox", "lazy", "dog"}) {
        CHECK(lower.contains(QLatin1String(word)),
              QByteArray("transcript contains '") .append(word).append('\'').constData());
    }

    // Confidence is the fixed coarse default (whisper emits none per segment).
    if (!segments.isEmpty())
        CHECK(segments.first().confidence == 1.0, "segment confidence defaults to 1.0");

    // Monotonic non-decreasing offsets: each startMs <= endMs, and neither bound
    // regresses across successive segments.
    bool monotonic = true;
    for (int i = 0; i < segments.size(); ++i) {
        if (segments[i].startMs > segments[i].endMs) monotonic = false;
        if (i > 0 && (segments[i].startMs < segments[i - 1].startMs
                      || segments[i].endMs < segments[i - 1].endMs))
            monotonic = false;
    }
    CHECK(monotonic, "segment offsets are monotonic non-decreasing");

    // ── 3. model_missing: a nonexistent model path -> ModelMissing, empty ────────
    QTemporaryDir missingDir;
    CHECK(missingDir.isValid(), "temp dir for missing-model case");
    const QString missingModel =
        QDir(missingDir.path()).filePath(QStringLiteral("nope/ggml-base.en.bin"));
    CoarseTranscriber missingTranscriber(missingModel);
    QList<CoarseSegment> missingSegs;
    FailureCode missingFailure = FailureCode::None;
    runInJob(coord, QStringLiteral("missing"), [&](work::WorkContext &ctx) {
        missingSegs = missingTranscriber.transcribe(pcm, ctx, &missingFailure);
    }, 30000);
    CHECK(missingFailure == FailureCode::ModelMissing,
          "nonexistent model -> ModelMissing");
    CHECK(missingSegs.isEmpty(), "nonexistent model -> no segments");

    // ── 4. model_checksum_failed: manifest carries the REAL sha, model file is
    //       truncated garbage -> checksum mismatch -> ModelChecksumFailed, empty ──
    QTemporaryDir badDir;
    CHECK(badDir.isValid(), "temp dir for checksum-failed case");
    {
        QFile mf(QDir(badDir.path()).filePath(QStringLiteral("manifest.json")));
        CHECK(mf.open(QIODevice::WriteOnly), "write bad-case manifest.json");
        mf.write("{\"schema\":1,\"modelId\":\"whisper-base.en\","
                 "\"file\":\"ggml-base.en.bin\","
                 "\"sha256\":\"a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002\"}");
        mf.close();
        QFile bin(QDir(badDir.path()).filePath(QStringLiteral("ggml-base.en.bin")));
        CHECK(bin.open(QIODevice::WriteOnly), "write truncated model copy");
        bin.write("not the real ggml model bytes"); // sha will not match
        bin.close();
    }
    CoarseTranscriber badTranscriber(
        QDir(badDir.path()).filePath(QStringLiteral("ggml-base.en.bin")));
    QList<CoarseSegment> badSegs;
    FailureCode badFailure = FailureCode::None;
    runInJob(coord, QStringLiteral("checksum"), [&](work::WorkContext &ctx) {
        badSegs = badTranscriber.transcribe(pcm, ctx, &badFailure);
    }, 30000);
    CHECK(badFailure == FailureCode::ModelChecksumFailed,
          "wrong-checksum model -> ModelChecksumFailed");
    CHECK(badSegs.isEmpty(), "wrong-checksum model -> no segments");

    // ── 5. cancellation: checkpoint false BEFORE spawn -> {} , no whisper, no hang.
    //       The job cancels ITSELF (deterministic — the token is set before
    //       transcribe reaches its pre-spawn checkpoint). ─────────────────────────
    QList<CoarseSegment> cancelledSegs;
    FailureCode cancelFailure = FailureCode::None;
    auto cdone = std::make_shared<std::promise<void>>();
    auto cfut = cdone->get_future();
    const QString cancelId = QStringLiteral("transcribe-cancel");
    coord.submit({cancelId, 100}, [&, cdone](work::WorkContext &ctx) {
        coord.cancel(cancelId); // arm this job's own cancel token
        cancelledSegs = transcriber.transcribe(pcm, ctx, &cancelFailure);
        cdone->set_value();
        return work::WorkResult::Cancelled;
    });
    const bool cancelReturned =
        cfut.wait_for(std::chrono::seconds(15)) == std::future_status::ready;
    CHECK(cancelReturned, "cancelled transcribe returned within 15s (no hang)");
    CHECK(cancelledSegs.isEmpty(), "cancelled transcribe yields no segments");
    CHECK(cancelFailure == FailureCode::None, "cancel is not a model/engine failure");

    if (g_failed) {
        std::fprintf(stderr, "VERDICT: FAIL\n");
        return 1;
    }
    std::fprintf(stdout, "PASS native coarse English transcription and cancellation\n");
    std::fprintf(stdout, "VERDICT: PASS\n");
    return 0;
}
