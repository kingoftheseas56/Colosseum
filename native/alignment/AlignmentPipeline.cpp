// native/alignment/AlignmentPipeline.cpp — see AlignmentPipeline.h.
#include "alignment/AlignmentPipeline.h"

#include "work/BackgroundWorkCoordinator.h"

#include <QString>
#include <QStringList>

namespace alignment {

namespace {

// The canonical text of one region, sliced straight out of its spine document. The EPUB is
// the only source of displayed words; the region only points INTO it.
static QString canonicalTextOf(const EpubIndex &index, const QString &spineHref,
                               qint64 canonicalStart, qint64 canonicalEnd) {
    if (canonicalStart < 0 || canonicalEnd <= canonicalStart) return QString();
    for (const SpineDocument &d : index.documents) {
        if (d.href != spineHref) continue;
        const qint64 len = canonicalEnd - canonicalStart;
        if (canonicalStart + len > d.canonical.size()) return QString();
        return d.canonical.mid(static_cast<int>(canonicalStart), static_cast<int>(len));
    }
    return QString();
}

} // namespace

AlignmentPipeline::AlignmentPipeline(QString coarseModelPath, QString forcedModelPath,
                                     QString whisperExe, QString cacheRoot)
    : m_coarseModelPath(std::move(coarseModelPath)),
      m_forcedModelPath(std::move(forcedModelPath)),
      m_decoder(cacheRoot),
      m_coarse(m_coarseModelPath, std::move(whisperExe)),
      m_aligner(m_forcedModelPath) {}

EpubIndex AlignmentPipeline::indexFor(const QString &bookPath) {
    QMutexLocker lock(&m_indexMutex);
    auto it = m_indexCache.constFind(bookPath);
    if (it != m_indexCache.constEnd())
        return it.value();
    const EpubIndex idx = m_indexer.index(bookPath);
    m_indexCache.insert(bookPath, idx);
    return idx;
}

work::WorkResult AlignmentPipeline::alignMatchedRegions(const EpubIndex &index,
                                                        const MatchPlan &plan,
                                                        const ChapterInput &in,
                                                        work::WorkContext &ctx,
                                                        ChapterResult &out) {
    const QString file = in.localFiles.first();
    const QString cacheKey = QStringLiteral("%1_c%2").arg(in.pairId).arg(in.chapterIndex);

    double confSum = 0.0;
    int confCnt = 0;
    for (const RegionRecord &r : plan.regions) {
        if (r.kind != RegionKind::Aligned) {
            // Carry every honest gap (BookOnly / AudioOnly / Uncertain) through verbatim.
            out.regions.append(r);
            continue;
        }
        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;

        const QString text = canonicalTextOf(index, r.spineHref, r.canonicalStart, r.canonicalEnd);
        if (text.isEmpty()) {
            // No resolvable text for a supposedly-aligned run: keep it honest, not guessed.
            out.regions.append({RegionKind::Uncertain, r.startMs, r.endMs,
                                r.spineHref, r.canonicalStart, r.canonicalEnd});
            continue;
        }

        // A degenerate or inverted audio window (endMs <= startMs) can fall out of a
        // BookOnly-gap boundary in the matcher's region partition — there is literally
        // nothing to decode. Carry the region as an honest Uncertain gap rather than
        // hard-failing the whole chapter (a single unresolvable region is not a
        // chapter-wide failure). [Deeper fix: clamp the gap-boundary times in the
        // EpubSequenceMatcher region partition — tracked as its own tested pass.]
        if (r.endMs <= r.startMs) {
            out.regions.append({RegionKind::Uncertain, r.startMs, r.endMs,
                                r.spineHref, r.canonicalStart, r.canonicalEnd});
            continue;
        }

        PcmWindow sub = m_decoder.decodeWindow(file, r.startMs, r.endMs, cacheKey, ctx);
        if (!sub.ok) {
            if (!ctx.checkpoint()) return work::WorkResult::Cancelled;   // cancelled mid-decode
            // One region we can't decode is unresolved, not a chapter-wide failure — carry it
            // as Uncertain and keep aligning the rest (same policy as a failed forced-align).
            out.regions.append({RegionKind::Uncertain, r.startMs, r.endMs,
                                r.spineHref, r.canonicalStart, r.canonicalEnd});
            continue;
        }

        CanonicalPassage passage;
        passage.spineHref = r.spineHref;
        passage.canonicalStart = r.canonicalStart;
        passage.text = text;

        const ForcedAlignmentResult ar = m_aligner.align(sub, passage, ctx);
        if (!ar.ok) {
            if (ar.failure == FailureCode::None) {
                if (!ctx.checkpoint()) return work::WorkResult::Cancelled; // pre-inference cancel
            }
            if (ar.failure == FailureCode::ModelMissing
                || ar.failure == FailureCode::ModelChecksumFailed
                || ar.failure == FailureCode::AlignmentFailed) {
                out.failure = ar.failure;
                return work::WorkResult::Failed;
            }
            // Otherwise treat this run as unresolved rather than fabricating cues.
            out.regions.append({RegionKind::Uncertain, r.startMs, r.endMs,
                                r.spineHref, r.canonicalStart, r.canonicalEnd});
            continue;
        }

        // Re-ordinal onto the chapter-global sentence numbering, and shift the aligner's
        // chapter-local times onto the pair timeline.
        const int base = out.sentences.size();
        for (SentenceCue s : ar.sentences) {
            s.ordinal += base;
            s.startMs += in.audioStartMs;
            s.endMs += in.audioStartMs;
            out.sentences.append(s);
        }
        for (WordCue w : ar.words) {
            w.sentenceOrdinal += base;
            w.startMs += in.audioStartMs;
            w.endMs += in.audioStartMs;
            out.words.append(w);
        }
        confSum += ar.confidence * ar.words.size();
        confCnt += ar.words.size();
    }

    out.confidence = confCnt > 0 ? confSum / confCnt : 0.0;
    return work::WorkResult::Completed;
}

ChapterProcessor AlignmentPipeline::makeProcessor() {
    return [this](const ChapterInput &in, work::WorkContext &ctx,
                  const ChapterReport &report, ChapterResult &out) -> work::WorkResult {
        // ── Preparing: index the paired EPUB once, reuse across its chapters ──────
        report(Stage::Preparing, 0.1);
        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;
        const EpubIndex index = indexFor(in.bookPath);
        if (!index.ok || index.documents.isEmpty()) {
            out.failure = FailureCode::EpubIndexFailed;
            return work::WorkResult::Failed;
        }

        // ── Transcribing: decode the chapter audio, then coarse-transcribe it ─────
        report(Stage::Transcribing, 0.3);
        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;
        if (in.localFiles.isEmpty()) {
            out.failure = FailureCode::AudioDecodeFailed;
            return work::WorkResult::Failed;
        }
        const QString file = in.localFiles.first();
        qint64 durationMs = (in.audioEndMs > in.audioStartMs) ? (in.audioEndMs - in.audioStartMs) : 0;
        if (durationMs <= 0) {
            const auto chs = m_decoder.probeChapters({file});
            if (!chs.isEmpty()) durationMs = chs.first().durationMs;
        }
        if (durationMs <= 0) {
            out.failure = FailureCode::AudioDecodeFailed;
            return work::WorkResult::Failed;
        }
        const QString cacheKey = QStringLiteral("%1_c%2").arg(in.pairId).arg(in.chapterIndex);
        const PcmWindow full = m_decoder.decodeWindow(file, 0, durationMs, cacheKey, ctx);
        if (!full.ok) {
            if (!ctx.checkpoint()) return work::WorkResult::Cancelled; // cancelled mid-decode
            out.failure = (full.failure != FailureCode::None) ? full.failure
                                                             : FailureCode::AudioDecodeFailed;
            return work::WorkResult::Failed;
        }
        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;

        FailureCode coarseFail = FailureCode::None;
        const QList<CoarseSegment> segments = m_coarse.transcribe(full, ctx, &coarseFail);
        if (coarseFail == FailureCode::ModelMissing || coarseFail == FailureCode::ModelChecksumFailed) {
            out.failure = coarseFail;
            return work::WorkResult::Failed;
        }
        if (segments.isEmpty()) {
            if (!ctx.checkpoint()) return work::WorkResult::Cancelled; // cancelled before/at whisper
            // A silent or unrecognised window is no evidence — publish nothing (CouldntSync).
            return work::WorkResult::Completed;
        }

        // ── Matching: line the transcript up against the canonical book text ──────
        report(Stage::Matching, 0.6);
        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;
        ChapterHint hint;
        hint.audioStartMs = 0;
        hint.audioEndMs = durationMs;
        const MatchPlan plan = m_matcher.match(index, segments, hint);
        if (!plan.matched) {
            // Wrong edition / non-corresponding audio: nothing to align. Empty cues run
            // through the store's Ready gate as an honest CouldntSync — never a forced fit.
            return work::WorkResult::Completed;
        }

        // ── Aligning: precise per-word/per-sentence CTC timings per Aligned region ─
        report(Stage::Aligning, 0.9);
        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;
        return alignMatchedRegions(index, plan, in, ctx, out);
    };
}

} // namespace alignment
