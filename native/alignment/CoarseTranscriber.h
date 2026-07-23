#pragma once

// CoarseTranscriber — the read-along's coarse English ear on the narration.
//
// One small thing: hand it a decoded PcmWindow (16 kHz mono float, from
// AudiobookAnalysisDecoder) and it returns a low-resolution, timestamped guess at
// what the narration says over that window — a QList<CoarseSegment>. That transcript
// is MATCHING EVIDENCE only: it is returned to C++ (the EpubSequenceMatcher) and
// NEVER displayed, never inserted into the book, never trusted for word timing. The
// EPUB stays the sole source of shown words; this is only the clue that lines the
// audio up against that authoritative text.
//
// It is a pure adapter over a bundled CPU whisper.cpp binary — not a QObject, no
// state beyond the two paths it was constructed with. It drives the offline,
// deterministic whisper-cli.exe (v1.9.1, CPU-only ggml backend) staged under
// <app>/tools/whisper/. (The app-bundled ffmpeg also carries a whisper filter, but
// that build's ggml uses Adreno-only OpenCL kernels that heap-corrupt on ordinary
// desktop GPUs with no CPU fallback — so the coarse stage drives the standalone CPU
// whisper-cli instead. Same whisper.cpp base.en model, deterministic on every box.)
//
// Two honesty guarantees:
//   • Integrity first. Before spending a transcription pass, the model file is
//     validated with models::ModelManifest against the sibling manifest.json: a
//     missing model maps to FailureCode::ModelMissing, a checksum mismatch to
//     ModelChecksumFailed — the frozen wire codes the store persists — and {} is
//     returned. A genuinely silent window that transcribes to nothing is NOT a
//     failure; it just yields zero segments.
//   • It yields. checkpoint() is consulted before the (one-pass) spawn — a cancel
//     that arrives first returns {} without ever launching whisper — and the wait is
//     polled so a cancel during the run kills the child promptly instead of hanging.
//
// How it runs: whisper-cli takes a WAV file, not raw PCM, so the window's f32
// samples are written to a temp 16 kHz mono s16 WAV (QTemporaryDir, auto-cleaned),
// then `whisper-cli -m <model> -f <wav> -l en -oj -of <base> -nt` writes <base>.json.
// The JSON's `transcription[]` carries `offsets.from`/`offsets.to` already in
// milliseconds and a `text` string; whisper emits no per-segment confidence, so each
// CoarseSegment is given a fixed confidence of 1.0.
//
// Consumes Qt Core only (QProcess lives in Core). The service (Task 3+) is the
// QML-facing façade; this never touches QML.
//
// Design authority: docs/superpowers/plans/2026-07-22-audiobook-epub-read-along.md.
// [Agent 2 (Claude), biblio]

#include "AlignmentTypes.h"
#include "CoarseTypes.h"
#include "AudiobookAnalysisDecoder.h" // PcmWindow

#include <QList>
#include <QString>

// WorkContext is passed by reference only — a forward declaration keeps this header
// Core-light; the .cpp pulls in the full coordinator.
namespace work { class WorkContext; }

namespace alignment {

class CoarseTranscriber {
public:
    // modelPath: the whisper.cpp ggml model file (e.g. .../coarse/ggml-base.en.bin);
    // its sibling manifest.json validates it. whisperExe empty -> the bundled
    // <appDir>/tools/whisper/whisper-cli.exe (else "whisper-cli" on PATH).
    explicit CoarseTranscriber(QString modelPath, QString whisperExe = QString());

    // Transcribe one decoded window to coarse timestamped segments. Validates the
    // model first (ModelMissing / ModelChecksumFailed -> *failure set, {} returned).
    // If ctx.checkpoint() is already false, returns {} without spawning whisper. A
    // whisper failure / missing-or-empty JSON returns {} with *failure left None (an
    // engine hiccup or silent window is not a model problem). The transcript is
    // evidence for C++ only — never surfaced to QML.
    QList<CoarseSegment> transcribe(const PcmWindow &window, work::WorkContext &ctx,
                                    alignment::FailureCode *failure = nullptr) const;

private:
    QString resolveWhisper() const; // bundled tools/whisper/whisper-cli.exe, else PATH
    alignment::FailureCode validateModel() const; // ModelManifest against sibling manifest.json

    QString m_modelPath;
    QString m_whisperExe;
};

} // namespace alignment
