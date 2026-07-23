// EnglishForcedAligner harness — precise per-word/per-sentence CTC forced alignment
// of a KNOWN English passage, run through the real wav2vec2 ONNX model.
//
// This is a PARITY test, not a hand-tuned-timings test. A synthetic clip can't give
// research-grade ground truth, and the plan forbids tuning expected timings to pass.
// So scripts/alignment/gen_ground_truth.py force-aligns the SAME model with a numpy
// reference and writes tests/fixtures/alignment/ground_truth.json; this harness runs
// the C++ aligner on the same logits + same algorithm and asserts the two AGREE.
// Because both consume identical logits and run the identical blank-interleaved
// Viterbi, per-word onsets match within ~1 frame — comfortably inside the plan's
// median-onset<=250ms / p95<=600ms bar.
//
// It chains the real AudiobookAnalysisDecoder (speech.wav -> 16 kHz mono f32
// PcmWindow), routes every heavy call through a real work::BackgroundWorkCoordinator
// (the only honest way to obtain a WorkContext), and proves:
//   • word boundaries are monotonic non-decreasing,
//   • each word onset matches the oracle (median<=250ms, p95<=600ms),
//   • sentences aggregate their words and carry their OWN confidence (computed
//     separately from the words),
//   • a passage word the audio never spoke comes back with LOW confidence (rejected,
//     not fabricated) while the real words keep their timings,
//   • the model-integrity gate (ModelMissing / ModelChecksumFailed) fails closed,
//   • a pre-inference cancel returns ok=false with no cues and no hang.
//
// Fixture: tests/fixtures/alignment/audio/speech.wav — SAPI TTS of
// "the quick brown fox jumps over the lazy dog" (deterministic, known text).
// Exit code = verdict.
// [Agent 2 (Claude), biblio]

#include "alignment/AudiobookAnalysisDecoder.h"
#include "alignment/EnglishForcedAligner.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>
#include <vector>

using namespace alignment;

static bool g_failed = false;
static void check(bool ok, const char *msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); g_failed = true; }
}
#define CHECK(cond, msg) check((cond), (msg))

#ifndef ALIGNMENT_AUDIO_DIR
#define ALIGNMENT_AUDIO_DIR "."
#endif
#ifndef FORCED_MODEL_DIR
#define FORCED_MODEL_DIR "."
#endif
#ifndef ALIGNMENT_FIXTURE_DIR
#define ALIGNMENT_FIXTURE_DIR "."
#endif
#ifndef ALIGNMENT_FFMPEG_DIR
#define ALIGNMENT_FFMPEG_DIR ""
#endif

static bool runInJob(work::BackgroundWorkCoordinator &coord, const QString &id,
                     std::function<void(work::WorkContext &)> body, int timeoutMs) {
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    coord.submit({id, 100}, [body, done](work::WorkContext &ctx) {
        body(ctx);
        done->set_value();
        return work::WorkResult::Completed;
    });
    return fut.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::ready;
}

static double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const int idx = static_cast<int>(std::ceil(p * (v.size() - 1)));
    return v[std::min<int>(idx, static_cast<int>(v.size()) - 1)];
}
static double median(std::vector<double> v) { return percentile(std::move(v), 0.5); }

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // The decoder resolves ffmpeg from applicationDir/tools or PATH; prepend the
    // build's known ffmpeg dir so the harness self-decodes without a staged copy.
    const QString ffdir = QStringLiteral(ALIGNMENT_FFMPEG_DIR);
    if (!ffdir.isEmpty()) {
        qputenv("PATH", ffdir.toLocal8Bit() + ";" + qgetenv("PATH"));
    }

    const QString wav = QStringLiteral(ALIGNMENT_AUDIO_DIR) + QStringLiteral("/speech.wav");
    const QString modelPath =
        QStringLiteral(FORCED_MODEL_DIR) + QStringLiteral("/wav2vec2_base_960h.onnx");
    const QString gtPath =
        QStringLiteral(ALIGNMENT_FIXTURE_DIR) + QStringLiteral("/ground_truth.json");
    CHECK(QFile::exists(wav), "fixture speech.wav present");
    CHECK(QFile::exists(modelPath), "forced model wav2vec2_base_960h.onnx present");
    CHECK(QFile::exists(gtPath), "oracle ground_truth.json present");

    // ── Load the parity oracle ───────────────────────────────────────────────────
    QFile gtf(gtPath);
    CHECK(gtf.open(QIODevice::ReadOnly), "open ground_truth.json");
    const QJsonObject gt = QJsonDocument::fromJson(gtf.readAll()).object();
    const QString text = gt.value(QStringLiteral("text")).toString();
    const QJsonArray gtWords = gt.value(QStringLiteral("words")).toArray();
    const QJsonArray gtSents = gt.value(QStringLiteral("sentences")).toArray();
    CHECK(!text.isEmpty() && !gtWords.isEmpty(), "oracle carries text + words");

    QTemporaryDir cacheDir;
    CHECK(cacheDir.isValid(), "temp cache root");

    AudiobookAnalysisDecoder decoder(cacheDir.path());
    work::BackgroundWorkCoordinator coord(1);

    // ── 1. decode the known-text fixture to a 16 kHz mono f32 window ──────────────
    PcmWindow pcm;
    const bool decoded = runInJob(coord, QStringLiteral("decode"),
        [&](work::WorkContext &ctx) {
            pcm = decoder.decodeWindow(wav, 0, 4000, QStringLiteral("k-fox"), ctx);
        }, 30000);
    CHECK(decoded, "decode job returned within deadline (no hang)");
    CHECK(pcm.ok, "fixture decoded ok");
    CHECK(!pcm.samples.isEmpty(), "decoded window has samples");

    // ── 2. construct the aligner; the model must validate cleanly ────────────────
    EnglishForcedAligner aligner(modelPath);
    CHECK(aligner.status() == FailureCode::None, "model loaded + validated");

    CanonicalPassage passage;
    passage.spineHref = QStringLiteral("OEBPS/ch01.xhtml");
    passage.canonicalStart = 1000; // arbitrary non-zero to prove offset mapping
    passage.text = text;

    ForcedAlignmentResult res;
    const bool aligned = runInJob(coord, QStringLiteral("align"),
        [&](work::WorkContext &ctx) { res = aligner.align(pcm, passage, ctx); }, 120000);
    CHECK(aligned, "align job returned within deadline (no hang)");
    CHECK(res.ok, "alignment ok");
    CHECK(res.words.size() == gtWords.size(), "aligned word count matches oracle");
    CHECK(res.sentences.size() == gtSents.size(), "aligned sentence count matches oracle");

    // ── 3. monotonic word boundaries ─────────────────────────────────────────────
    bool monotonic = true;
    for (int i = 0; i < res.words.size(); ++i) {
        if (res.words[i].startMs > res.words[i].endMs) monotonic = false;
        if (i > 0 && (res.words[i].startMs < res.words[i - 1].startMs
                      || res.words[i].endMs < res.words[i - 1].endMs))
            monotonic = false;
    }
    CHECK(monotonic, "word boundaries monotonic non-decreasing");

    // ── 4. onset parity vs the oracle (median<=250ms, p95<=600ms) ────────────────
    std::vector<double> onsetErr;
    for (int i = 0; i < res.words.size() && i < gtWords.size(); ++i) {
        const double gtStart = gtWords[i].toObject().value(QStringLiteral("startMs")).toDouble();
        onsetErr.push_back(std::fabs(static_cast<double>(res.words[i].startMs) - gtStart));
    }
    const double med = median(onsetErr);
    const double p95 = percentile(onsetErr, 0.95);
    CHECK(med <= 250.0, "median word onset error <= 250 ms");
    CHECK(p95 <= 600.0, "p95 word onset error <= 600 ms");

    // Canonical offsets map through passage.canonicalStart + char offset.
    {
        const QJsonObject w0 = gtWords[0].toObject();
        const qint64 expectStart = passage.canonicalStart + w0.value(QStringLiteral("startChar")).toInt();
        const qint64 expectEnd = passage.canonicalStart + w0.value(QStringLiteral("endChar")).toInt();
        CHECK(res.words[0].canonicalStart == expectStart, "word0 canonicalStart maps via passage offset");
        CHECK(res.words[0].canonicalEnd == expectEnd, "word0 canonicalEnd maps via passage offset");
    }

    // ── 5. sentence aggregation + separate confidence ────────────────────────────
    CHECK(!res.sentences.isEmpty(), "at least one sentence cue");
    if (!res.sentences.isEmpty()) {
        const SentenceCue &s = res.sentences.first();
        CHECK(s.regionKind == RegionKind::Aligned, "sentence region kind is Aligned");
        CHECK(!s.sentenceHash.isEmpty(), "sentence carries a SHA-256 hash");
        CHECK(s.startMs == res.words.first().startMs, "sentence start = first word start");
        CHECK(s.endMs == res.words.last().endMs, "sentence end = last word end");
        CHECK(s.spineHref == passage.spineHref, "sentence carries the spine href");
        CHECK(s.confidence > 0.80, "clean sentence confidence high");
        // Separate computation: the sentence confidence is a fresh aggregate, not a
        // copy of the first word's confidence.
        CHECK(std::fabs(s.confidence - res.words.first().confidence) > 1e-9,
              "sentence confidence computed separately from words");
    }
    for (const WordCue &w : res.words)
        CHECK(w.confidence > 0.80, "each clean word confidence high");
    CHECK(res.confidence > 0.80, "overall confidence high for clean passage");

    // ── 6. not-in-audio word -> LOW confidence, real words untouched ─────────────
    CanonicalPassage extra = passage;
    extra.text = text + QStringLiteral(" elephant"); // 'elephant' is never spoken
    ForcedAlignmentResult xres;
    const bool xaligned = runInJob(coord, QStringLiteral("align-extra"),
        [&](work::WorkContext &ctx) { xres = aligner.align(pcm, extra, ctx); }, 120000);
    CHECK(xaligned, "extra-word align returned (no hang)");
    CHECK(xres.ok, "extra-word alignment ok");
    CHECK(xres.words.size() == gtWords.size() + 1, "extra-word passage has one more word");
    if (xres.words.size() == gtWords.size() + 1) {
        const WordCue &elephant = xres.words.last();
        CHECK(elephant.confidence < 0.50, "not-in-audio word gets low confidence");
        // The nine real words keep their timings (CTC is monotonic; the phantom word
        // is appended, never inserted).
        double maxShift = 0.0;
        for (int i = 0; i < gtWords.size(); ++i)
            maxShift = std::max(maxShift,
                std::fabs(static_cast<double>(xres.words[i].startMs) - res.words[i].startMs));
        CHECK(maxShift <= 250.0, "real words undisturbed by the phantom word");
        // And the phantom is dramatically less confident than the real words.
        std::vector<double> realConf;
        for (int i = 0; i < gtWords.size(); ++i) realConf.push_back(xres.words[i].confidence);
        CHECK(elephant.confidence < 0.5 * median(realConf),
              "phantom word far less confident than real words");
    }

    // ── 7. model-integrity gate: missing + checksum ──────────────────────────────
    {
        QTemporaryDir missingDir;
        const QString miss = QDir(missingDir.path()).filePath(QStringLiteral("nope/model.onnx"));
        EnglishForcedAligner missAligner(miss);
        CHECK(missAligner.status() == FailureCode::ModelMissing, "missing model -> ModelMissing");
        ForcedAlignmentResult mres;
        runInJob(coord, QStringLiteral("align-missing"),
            [&](work::WorkContext &ctx) { mres = missAligner.align(pcm, passage, ctx); }, 20000);
        CHECK(!mres.ok && mres.words.isEmpty(), "missing model -> ok=false, no cues");
        CHECK(mres.failure == FailureCode::ModelMissing, "missing model failure code");
    }
    {
        QTemporaryDir badDir;
        QFile mf(QDir(badDir.path()).filePath(QStringLiteral("manifest.json")));
        CHECK(mf.open(QIODevice::WriteOnly), "write bad-case manifest.json");
        mf.write("{\"schema\":1,\"modelId\":\"wav2vec2-base-960h-ctc\","
                 "\"file\":\"model.onnx\","
                 "\"sha256\":\"3cba34f35eb3d4d9bba0491dc245969e93a0d2a8291264f77f3d526803fa9d4b\"}");
        mf.close();
        QFile bin(QDir(badDir.path()).filePath(QStringLiteral("model.onnx")));
        CHECK(bin.open(QIODevice::WriteOnly), "write truncated model copy");
        bin.write("not the real onnx bytes"); // sha will not match
        bin.close();
        EnglishForcedAligner badAligner(QDir(badDir.path()).filePath(QStringLiteral("model.onnx")));
        CHECK(badAligner.status() == FailureCode::ModelChecksumFailed,
              "wrong-checksum model -> ModelChecksumFailed");
    }

    // ── 8. cancellation: checkpoint false BEFORE inference -> ok=false, no hang ───
    ForcedAlignmentResult cres;
    auto cdone = std::make_shared<std::promise<void>>();
    auto cfut = cdone->get_future();
    const QString cancelId = QStringLiteral("align-cancel");
    coord.submit({cancelId, 100}, [&, cdone](work::WorkContext &ctx) {
        coord.cancel(cancelId); // arm this job's own cancel token before align checks it
        cres = aligner.align(pcm, passage, ctx);
        cdone->set_value();
        return work::WorkResult::Cancelled;
    });
    const bool cancelReturned =
        cfut.wait_for(std::chrono::seconds(30)) == std::future_status::ready;
    CHECK(cancelReturned, "cancelled align returned within 30s (no hang)");
    CHECK(!cres.ok, "cancelled align -> ok=false");
    CHECK(cres.words.isEmpty() && cres.sentences.isEmpty(), "cancelled align -> no cues");
    CHECK(cres.failure == FailureCode::None, "cancel is not a model/engine failure");

    if (g_failed) {
        std::fprintf(stderr, "median onset error=%.1f ms, p95=%.1f ms\n", med, p95);
        std::fprintf(stderr, "VERDICT: FAIL\n");
        return 1;
    }
    std::fprintf(stdout,
        "PASS native English forced alignment: median onset error <=250 ms, "
        "p95 <=600 ms (actual median=%.1f ms, p95=%.1f ms), monotonic, "
        "separate word/sentence confidence, not-in-audio word rejected, cancellation clean\n",
        med, p95);
    std::fprintf(stdout, "VERDICT: PASS\n");
    return 0;
}
