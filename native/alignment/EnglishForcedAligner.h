#pragma once

// EnglishForcedAligner — the read-along's precise ear: per-word and per-sentence
// audio timings for a KNOWN English passage.
//
// One small thing: hand it a decoded PcmWindow (16 kHz mono float) and the exact
// EPUB text that window narrates, and it returns the audio [startMs,endMs) of every
// word and sentence in that text. It does NOT recognise speech open-endedly — the
// transcript is already known (the book is the source of truth); it force-aligns the
// audio TO that transcript. It runs a bundled wav2vec2 CTC acoustic model on ONNX
// Runtime, log-softmaxes the frame logits, and Viterbi-aligns the passage's character
// tokens against them over the standard blank-interleaved CTC state graph. The result
// is monotonic by construction, and each cue carries an honest confidence (the mean
// probability the model placed on the forced path over that cue's frames) so a word
// the narration never actually spoke comes back with LOW confidence instead of a
// fabricated timing.
//
// Two honesty guarantees:
//   • Integrity first. Before any inference the model is validated with
//     models::ModelManifest against the sibling manifest.json: a missing model gives
//     ok=false + FailureCode::ModelMissing, a checksum mismatch ModelChecksumFailed —
//     the frozen wire codes the store persists — and no cues.
//   • It yields. ctx.checkpoint() is consulted before the (heavy) inference step; a
//     cancel that arrives first returns ok=false with no cues and no hang.
//
// Not a QObject. The ONNX session is created once at construction and reused; align()
// is logically const and serialised by an internal mutex, so it is safe to call from
// the shared background worker thread. Only compiled when COLOSSEUM_ENABLE_ONNX=ON
// (the app links ONNX Runtime), exactly like the guided panel detector.
//
// Design authority: docs/superpowers/plans/2026-07-22-audiobook-epub-read-along.md.
// [Agent 2 (Claude), biblio]

#include "AlignmentTypes.h"
#include "AudiobookAnalysisDecoder.h" // PcmWindow
#include "models/ModelManifest.h"

#include <onnxruntime_cxx_api.h>

#include <QList>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// WorkContext is passed by reference only — a forward declaration keeps this header
// Core-light; the .cpp pulls in the full coordinator.
namespace work { class WorkContext; }

namespace alignment {

// The EPUB text to align, anchored in the canonical stream. `text` IS the canonical
// fold (what EpubTextIndexer and the paper both reproduce), so a character offset i
// within `text` maps to canonical offset `canonicalStart + i` directly.
struct CanonicalPassage {
    QString spineHref;
    qint64 canonicalStart = 0;
    QString text;
};

// The outcome of aligning one window to one passage. `ok` is the gate: when false
// (model missing/checksum/inference failure, or a pre-inference cancel) both lists
// are empty and `confidence` is 0. Sentences and words each carry their OWN mean
// confidence (computed separately over their own frames).
struct ForcedAlignmentResult {
    QList<alignment::SentenceCue> sentences;
    QList<alignment::WordCue> words;
    double confidence = 0.0;   // mean aligned-path probability over all letter frames
    bool ok = false;
    alignment::FailureCode failure = alignment::FailureCode::None;
};

class EnglishForcedAligner {
public:
    // modelPath: the exported CTC model file (…/forced/wav2vec2_base_960h.onnx); its
    // sibling manifest.json validates it and its co-located .onnx.data external-weights
    // file is auto-loaded by ONNX Runtime. vocabPath empty -> the sibling vocab.json.
    // Construction validates the model + creates the session; if anything is wrong,
    // status() carries the reason and align() fails closed with it. Never throws.
    explicit EnglishForcedAligner(QString modelPath, QString vocabPath = QString());
    ~EnglishForcedAligner();

    EnglishForcedAligner(const EnglishForcedAligner &) = delete;
    EnglishForcedAligner &operator=(const EnglishForcedAligner &) = delete;

    // Force-align `window` to `passage`. Validates the model, checkpoints (bailing to
    // ok=false with no cues if cancelled), runs inference, log-softmaxes, tokenizes the
    // passage (uppercase letters + '|' word delimiter), Viterbi-aligns over the
    // blank-interleaved CTC graph, and groups token frame-spans into WordCues and
    // SentenceCues with millisecond bounds and per-cue confidence.
    ForcedAlignmentResult align(const PcmWindow &window, const CanonicalPassage &passage,
                                work::WorkContext &ctx) const;

    // FailureCode::None once the model loaded + validated + a session exists; otherwise
    // ModelMissing / ModelChecksumFailed / AlignmentFailed.
    FailureCode status() const { return m_status; }

private:
    FailureCode m_status = FailureCode::ModelMissing;

    QString m_modelPath;
    QString m_vocabPath;

    // Vocabulary: single-character label -> id (uppercase letters, apostrophe, '|'),
    // plus the blank (pad) and word-delimiter ids.
    std::vector<std::string> m_labels;
    int m_blankId = 0;
    int m_delimId = 4;
    // char (uppercased) -> vocab id for the single-character labels.
    std::vector<int> m_charToId; // indexed by unsigned char, -1 when absent

    std::optional<models::ModelManifest> m_manifest;
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::string m_inputName;
    std::string m_outputName;
    mutable std::mutex m_runMutex; // align() is const but serialises Ort::Run

    bool loadVocab();
    int idForChar(QChar c) const; // -1 when the char has no single-char label
};

} // namespace alignment
