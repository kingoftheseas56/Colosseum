// ComickVolumeGrouper.h — pure logic mirrored 1:1 from the Python reference
// (colosseum-volume-db/comick_volume_db/volume_builder.py). Comick chapter rows
// -> majority-voted chapter->volume assignment -> ordered volume ranges -> the
// completeness gate. This is the app's live-scrape core; the Python is the batch
// core. If the two ever diverge, the same series renders differently depending on
// whether it came from the database or a live scrape — keep them mirrored,
// test-for-test.
//
// The gate is doctrine, not tuning: a series qualifies for tankoban mode ONLY when
// its volumes form one unbroken run with real, non-overlapping, fully-covered
// chapter ranges. Everything else falls back to the flat chapter list. NEVER add
// interpolation or estimation here — permanently rejected.
//
// Rows arrive from ALL languages, so one chapter number turns up many times and
// uploaders occasionally disagree about its volume; majorityAssign settles that per
// chapter before grouping. Rows with no `vol` are ignored by the assignment (they
// become the app's "Latest chapters" shelf, derived live elsewhere) but they are
// still real chapters, so gateVolumes counts them when it checks coverage.

#pragma once

#include <QList>
#include <QMap>
#include <QString>

namespace tankoban::manga::comick {

// A chapter or volume label parsed into a sortable key. The part after the dot is
// an ORDINAL, not a fraction: "315.9" < "315.10" < "315.11", because those are the
// 9th, 10th and 11th side chapters of 315. Read as floats, "315.10" == "315.1" —
// two different chapters collapsed into one and one of them silently dropped.
// `subDigits` carries the source's exact spelling so formatChapterKey() can hand the label
// back byte-for-byte ("110.30" must not come back as "110.3").
//
// ORDERING/EQUALITY NOTE — deliberate, and it mirrors the Python: the Python key is
// the tuple `(whole, sub_ordinal, sub_digits)`, and Python compares tuples
// element-by-element, so the digit STRING is the third comparison term and part of
// dict/set identity. "315.09" and "315.9" therefore parse to keys that are equal on
// (whole, ordinal) but are still two DISTINCT entries, ordered "315.09" < "315.9" by
// string compare. We reproduce that exactly rather than dropping subDigits from the
// comparison, because dropping it would merge two Python-distinct chapters into one
// — the very float-collision defect the ordinal key exists to prevent.
// What is CHECKED here is only that the two implementations agree. Whether such a
// pair actually occurs is a separate question, and the honest answer is: it does not
// occur in today's corpus. Measured 2026-07-29 across the four Comick fixtures
// (berserk/bleach/death-note/yani-neko, 1467 distinct chapter keys) plus the 447-key
// My Hero Academia all-language pull — zero (whole, ordinal) pairs carry two
// spellings. Berserk does use zero-padded sub-digits ("0.01".."0.09"), so the
// round-trip half of subDigits is load-bearing today; the tie-break half is
// insurance that costs nothing and keeps us bit-identical to the batch pipeline.
struct ChapterKey {
    int whole = 0;
    int subOrdinal = -1;      // -1 = a whole chapter, which sorts before its side chapters
    QString subDigits;        // "" for a whole chapter; "02" preserves "25.02"

    bool isSideChapter() const { return subOrdinal != -1; }

    bool operator<(const ChapterKey& other) const;
    bool operator==(const ChapterKey& other) const;
    bool operator!=(const ChapterKey& other) const { return !(*this == other); }
    bool operator<=(const ChapterKey& other) const { return !(other < *this); }
    bool operator>(const ChapterKey& other) const { return other < *this; }
    bool operator>=(const ChapterKey& other) const { return !(*this < other); }
};

// One Comick chapter row, any language, with the source's raw labels. Comick sends
// these as JSON strings or null; a null and an empty string behave identically here
// (both fail parseChapterKey, so neither votes), exactly as in the Python.
struct ChapterRow {
    QString chap;
    QString vol;
};

struct VolumeRange {
    int number = 0;
    QString chapterStart;     // the source's own label
    QString chapterEnd;
};

struct GateVerdict {
    bool qualified = false;
    QString reason;
};

// Parse a chapter/volume label into a sortable key. Returns false when the label
// isn't a plain number — mirrors the Python regex `^-?\d+(?:\.\d+)?$` applied to the
// stripped label. `out` may be null when the caller only wants the validity answer.
bool parseChapterKey(const QString& raw, ChapterKey* out);

// The source's own label, byte for byte: "7", "110.5", "110.30", "25.02".
QString formatChapterKey(const ChapterKey& key);

// {chapter -> volume} — each chapter goes to the volume the most rows voted for,
// then lone stray tags are pulled back to their neighbours. Rows missing chap or vol
// don't vote. A dead tie means the sources genuinely contradict each other, so the
// chapter is left UNASSIGNED (absent from the map) rather than guessed — the hole is
// then the gate's to judge.
QMap<ChapterKey, int> majorityAssign(const QList<ChapterRow>& rows);

// Ordered volume records, ascending by number, each spanning its lowest to highest
// assigned chapter. Chapters with no volume are simply not here.
QList<VolumeRange> groupVolumes(const QList<ChapterRow>& rows);

// True when the earliest chapter number is fractional (e.g. Berserk's 0.01 prologue),
// which means Comick's numbering is offset from WeebCentral's integers and the ranges
// would not join. True as well when nothing parses at all.
bool numberingIsOddball(const QList<ChapterRow>& rows);

// (qualified, reason). Qualified = the mapped volumes are a complete, honest shelf,
// judged against the source rows they came from. Anything else -> the app shows the
// flat chapter list instead. NEVER soften this into estimation.
GateVerdict gateVolumes(const QList<VolumeRange>& vols, bool numberingQuirk,
                        const QList<ChapterRow>& rows);

} // namespace tankoban::manga::comick
