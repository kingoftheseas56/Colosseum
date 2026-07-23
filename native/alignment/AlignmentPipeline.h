#pragma once

// AlignmentPipeline — the REAL audiobook↔EPUB read-along processor: the one place
// the five alignment components are wired into a single chapter-processing function.
//
// One small thing: it owns the EPUB text indexer, the audio decoder, the coarse
// (whisper) transcriber, the sequence matcher, and the precise (ONNX CTC) forced
// aligner, and exposes ONE thing — makeProcessor() — that returns the
// alignment::ChapterProcessor the AudioTextAlignmentService schedules per chapter.
// For a single ChapterInput it walks the approved stage chain and fills a
// ChapterResult the service hands to AlignmentStore::publishReadyChapter():
//
//   Preparing    index the paired EPUB once per book (cached), fold to canonical text.
//   Transcribing decode the chapter audio to a 16 kHz mono window, then whisper it to a
//                low-resolution transcript — MATCHING EVIDENCE only, never displayed.
//   Matching     line that transcript up against the canonical book text as trusted,
//                strictly-forward anchors; a wrong edition is rejected, not forced.
//   Aligning     inside each trusted Aligned run, force-align the audio TO the known
//                EPUB words with the CTC model, producing per-word/per-sentence ms cues.
//
// The pipeline COMPOSES; it does not re-decide anything the components already decide.
// The matcher's verdict is the only discriminator: if it does not match (a wrong edition,
// or a chapter whose audio and text do not correspond), the pipeline returns NO cues and
// the store records an honest CouldntSync — nothing is guessed. On a match it force-aligns
// each Aligned region and lets the store's >=80% coverage gate decide Ready.
//
// Threading. The processor runs OFF the GUI thread on the shared
// work::BackgroundWorkCoordinator's single worker. The forced aligner serialises its
// Ort::Run internally; the per-book EpubIndex cache is guarded by a mutex; the decoder and
// transcriber hold no cross-call mutable state. The store WRITE is the service's, through a
// worker-local AlignmentStore — never this class. checkpoint() is consulted between the
// bounded stages (and windows/regions) so a pause/cancel stops promptly.
//
// Design authority: docs/superpowers/plans/2026-07-22-audiobook-epub-read-along.md (Task 12).
// [Agent 2 (Claude), biblio]

#include "AlignmentTypes.h"
#include "AudioTextAlignmentService.h"   // ChapterProcessor / ChapterInput / ChapterResult / ChapterReport
#include "AudiobookAnalysisDecoder.h"
#include "CoarseTranscriber.h"
#include "EnglishForcedAligner.h"
#include "EpubSequenceMatcher.h"
#include "EpubTextIndexer.h"

#include <QHash>
#include <QMutex>
#include <QString>

namespace work { class WorkContext; }

namespace alignment {

class AlignmentPipeline {
public:
    // coarseModelPath: whisper ggml model (…/coarse/ggml-base.en.bin), validated by its
    //   sibling manifest.json. forcedModelPath: ONNX CTC model
    //   (…/forced/wav2vec2_base_960h.onnx), validated + session-created once here.
    // whisperExe empty -> the bundled tools/whisper/whisper-cli.exe (else PATH).
    // cacheRoot empty -> the decoder's default app cache; a test injects a temp dir.
    AlignmentPipeline(QString coarseModelPath, QString forcedModelPath,
                      QString whisperExe = QString(), QString cacheRoot = QString());

    // The seam the AudioTextAlignmentService consumes. The returned std::function shares
    // this pipeline by pointer — keep the pipeline alive for the function's lifetime.
    ChapterProcessor makeProcessor();

    // FailureCode::None once the forced-aligner model loaded + validated. A test can assert
    // the heavy ONNX model is ready before running a chapter.
    FailureCode alignerStatus() const { return m_aligner.status(); }

private:
    // Get-or-build the canonical index for one EPUB path (indexed once per book, reused
    // across its chapters). Thread-safe via m_indexMutex.
    EpubIndex indexFor(const QString &bookPath);

    // Force-align each trusted Aligned region of a matched plan, carrying the non-aligned
    // regions (BookOnly / AudioOnly / Uncertain) through verbatim. Fills `out`; returns the
    // stage result (Cancelled on a mid-flight cancel, Failed on a terminal engine failure).
    work::WorkResult alignMatchedRegions(const EpubIndex &index, const MatchPlan &plan,
                                         const ChapterInput &in, work::WorkContext &ctx,
                                         ChapterResult &out);

    QString m_coarseModelPath;
    QString m_forcedModelPath;

    EpubTextIndexer m_indexer;
    AudiobookAnalysisDecoder m_decoder;
    CoarseTranscriber m_coarse;
    EpubSequenceMatcher m_matcher;
    EnglishForcedAligner m_aligner;

    QMutex m_indexMutex;
    QHash<QString, EpubIndex> m_indexCache;
};

} // namespace alignment
