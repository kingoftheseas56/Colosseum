#pragma once

// EpubSequenceMatcher — line coarse narration up against the authoritative EPUB text.
//
// One small thing: given the book's canonical text and a chapter's coarse recognized
// speech, decide WHERE the narration is reading in the book — as a handful of trusted,
// strictly forward-moving anchors, with every stretch in between honestly labelled.
// It never forces a fit: if the audio and the book are different editions (or a
// different book), it says so and returns nothing rather than inventing an alignment.
//
// How it decides, in plain terms:
//   1. Tokenize both sides into canonical word tokens (the book's `canonical` stream
//      is already folded; the transcript is folded the same way). Book tokens keep
//      their canonical offsets; transcript tokens keep an interpolated audio time.
//   2. Find RARE, unambiguous phrases (short shingles that occur exactly once on each
//      side) — a repeated common phrase is a poor anchor and is skipped.
//   3. Lock those shared phrases into anchors, but only in a strictly non-decreasing
//      chain in BOTH audio time AND canonical position. A committed anchor can never
//      move backward — this is what resolves a repeated phrase to the right occurrence.
//   4. Require SEVERAL separated anchors. Too few / too weak => reject (edition
//      mismatch), never a forced partial alignment.
//   5. Between locked anchors run a BOUNDED (banded) alignment only — never a full
//      O(n*m) global pass — and classify every span explicitly: Aligned, BookOnly
//      (text the narration skipped), AudioOnly (credits/music with no text), or
//      Uncertain (couldn't resolve). Nothing is guessed.
//
// Pure and deterministic: no models, no network, no I/O, no state. The service (Task 3)
// feeds it EpubTextIndexer output plus CoarseTranscriber output and hands the MatchPlan
// to the forced aligner (Task 11), which only ever runs inside the trusted Aligned runs.
// Consumes Qt Core only.
//
// [Agent 2 (Claude), biblio]

#include "AlignmentTypes.h"
#include "CoarseTypes.h"
#include "EpubTextIndexer.h"

#include <QString>
#include <QList>

namespace alignment {

// A locked correspondence: at audio time `audioMs` the narration is reading the
// canonical span [canonicalStart, canonicalEnd) of spine document `spineHref`. Anchors
// are the trustworthy skeleton of a MatchPlan; committed anchors never move backward.
struct Anchor {
    qint64 audioMs = 0;
    QString spineHref;
    qint64 canonicalStart = 0;
    qint64 canonicalEnd = 0;
    double score = 0.0;
};

// The chapter's audio window. The matcher does NOT require a one-to-one chapter<->spine
// mapping; the hint only scopes the audio timeline it partitions. One audio chapter may
// cover part of a spine document or cross several of them.
struct ChapterHint {
    qint64 audioStartMs = 0;
    qint64 audioEndMs = 0;
};

// The result of matching. When `matched` is true, `anchors` is a monotonic chain and
// `regions` fully partitions the audio window into explicitly-classified spans. When
// `matched` is false, `rejectReason` explains why (the deliberate edition-mismatch
// rejection) and no forced anchors are produced.
struct MatchPlan {
    bool matched = false;
    QList<Anchor> anchors;
    QList<RegionRecord> regions;
    double confidence = 0.0;
    QString rejectReason;
};

class EpubSequenceMatcher {
public:
    // Pure, const, no state, no I/O. Deterministic for a given input.
    MatchPlan match(const EpubIndex &index,
                    const QList<CoarseSegment> &segments,
                    const ChapterHint &hint) const;
};

} // namespace alignment
