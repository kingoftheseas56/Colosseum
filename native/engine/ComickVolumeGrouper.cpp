// ComickVolumeGrouper.cpp — see the header. Every function here is a line-for-line
// port of colosseum-volume-db/comick_volume_db/volume_builder.py; the comments carry
// that file's reasoning across, including what it CHECKS versus what it ASSUMES.
#include "engine/ComickVolumeGrouper.h"

#include <algorithm>

namespace tankoban::manga::comick {

namespace {

bool isAsciiDigit(QChar c)
{
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

// A volume is a contiguous run of chapters, so if the chapters either side of one
// chapter are both in the same volume, that chapter is in it too — whatever a single
// row claims. Where a chapter disagrees with two neighbours that agree with each
// other, it takes theirs. Both real cases are one mis-tagged row against 4-12
// agreeing ones: Naruto 459.3 tagged volume 50 between two volume-49 chapters,
// Berserk 106.5 tagged volume 13 between two volume-15 chapters.
//
// The chapter that moves is chosen POSITIONALLY — the one in the middle — not by
// vote weight. So a well-attested chapter flanked by two single-vote neighbours that
// happen to agree would be the one to move. That has never fired in this corpus
// (2 chapters moved in total, both the lone stray row), and the contiguous-run
// argument above holds regardless of who has more votes, but the code is not weighing
// evidence and should not be read as if it were.
//
// This also keeps the run monotonic, which matters because physical volumes are
// sequential — a later chapter is never bound into an earlier book. (Capping each
// chapter by the lowest volume claimed after it is monotonic too, but resolves the
// wrong way: Berserk's single stray row would drag 16 well-attested chapters down
// into volume 13 with it. Measured 2026-07-29.)
//
// Nothing is invented: an UNASSIGNED chapter stays unassigned — it is not even in
// `keys`, so its neighbours are the adjacent ASSIGNED chapters — and a real hole
// still reaches the gate. Anything this can't settle leaves the run non-monotonic,
// which shows up as overlapping spans — and the gate refuses those, so it can never
// reach the shelf.
//
// Note the deliberate asymmetry, mirrored from the Python — and note that only ONE
// half of it carries behaviour:
//
//   * `before` reads the RUNNING `settled` map, so a correction cascades forward down
//     a run. This half is LOAD-BEARING. Pointing it at `assign` instead looks like a
//     harmless tidy-up and silently moves book boundaries: rows 1->vol 1, 2->vol 2,
//     3->vol 1, 4->vol 2 group as vol 1 = 1-3, vol 2 = 4-4 here, and as vol 1 = 1-2,
//     vol 2 = 3-4 once "tidied". The harness pins exactly that shape.
//   * `after` reads the ORIGINAL `assign`, but iteration i only ever writes
//     settled[keys[i]], so by the time it is read keys[i+1] has never been touched and
//     settled[next] == assign[next] holds always. This half is INERT: it mirrors the
//     Python's shape, it is not protecting anything. (Measured 2026-07-29: swapping it
//     changed 0 of 20,000 random assignments, against 3,098 for the `before` half.)
//
// So do not "symmetrise" the two — they are not doing the same job, and the one that
// looks redundant is the one that isn't.
QMap<ChapterKey, int> fixStrayTags(const QMap<ChapterKey, int>& assign)
{
    const QList<ChapterKey> keys = assign.keys();   // QMap::keys() is ascending == Python's sorted()
    QMap<ChapterKey, int> settled = assign;
    for (int i = 1; i + 1 < keys.size(); ++i) {
        const int before = settled.value(keys.at(i - 1));
        const int after = assign.value(keys.at(i + 1));
        if (before == after && settled.value(keys.at(i)) != before)
            settled[keys.at(i)] = before;
    }
    return settled;
}

} // namespace

bool ChapterKey::operator<(const ChapterKey& other) const
{
    // Python tuple order: (whole, sub_ordinal, sub_digits), element by element.
    if (whole != other.whole)
        return whole < other.whole;
    if (subOrdinal != other.subOrdinal)
        return subOrdinal < other.subOrdinal;
    return QString::compare(subDigits, other.subDigits) < 0;
}

bool ChapterKey::operator==(const ChapterKey& other) const
{
    return whole == other.whole && subOrdinal == other.subOrdinal
        && subDigits == other.subDigits;
}

// Mirrors the Python `^-?\d+(?:\.\d+)?$` against the stripped label, hand-rolled so a
// pure-logic module needs no regex engine.
//
// Two knowingly narrower edges versus Python, neither reachable from Comick data
// (largest real label is 3 digits). Both are pinned by contracts in the harness:
//   * digits are ASCII only. Python's `re` `\d` and `int()` both accept other Unicode
//     decimal digits (Arabic-Indic, etc.); QString::toInt does not, so accepting them
//     in the pattern would only move the failure. ASCII-only is the honest line.
//   * a label whose number overflows a 32-bit int is rejected here and accepted by
//     Python's arbitrary-precision int.
// A rejected label casts no vote, which is the conservative direction in every case
// but ONE, and that one is worth knowing: an ordinal past int32 ("1.99999999999")
// rejects the whole label, so a series whose EARLIEST chapter carries such an ordinal
// loses its fractional origin here. Python calls that a numbering quirk and withholds
// the shelf; this shows one. That is the only place a narrowing is permissive rather
// than conservative — recorded, not fixed, because it cannot arise from Comick data.
bool parseChapterKey(const QString& raw, ChapterKey* out)
{
    const QString label = raw.trimmed();
    const int n = label.size();
    int i = 0;
    if (i < n && label.at(i) == QLatin1Char('-'))
        ++i;
    const int wholeDigitsStart = i;
    while (i < n && isAsciiDigit(label.at(i)))
        ++i;
    if (i == wholeDigitsStart)
        return false;                       // `\d+` needs at least one digit
    const int wholeEnd = i;

    QString sub;
    if (i < n) {
        if (label.at(i) != QLatin1Char('.'))
            return false;
        ++i;
        const int subStart = i;
        while (i < n && isAsciiDigit(label.at(i)))
            ++i;
        if (i == subStart || i != n)
            return false;                   // `\.\d+` then end of string
        sub = label.mid(subStart);
    }

    bool ok = false;
    const int whole = label.left(wholeEnd).toInt(&ok);   // left() keeps the sign
    if (!ok)
        return false;

    int subOrdinal = -1;
    if (!sub.isEmpty()) {
        subOrdinal = sub.toInt(&ok);
        if (!ok)
            return false;
    }

    if (out) {
        out->whole = whole;
        out->subOrdinal = subOrdinal;
        out->subDigits = sub;
    }
    return true;
}

QString formatChapterKey(const ChapterKey& key)
{
    // Keyed off the DIGITS, not the ordinal — same as the Python's `if sub`.
    if (key.subDigits.isEmpty())
        return QString::number(key.whole);
    return QString::number(key.whole) + QLatin1Char('.') + key.subDigits;
}

QMap<ChapterKey, int> majorityAssign(const QList<ChapterRow>& rows)
{
    QMap<ChapterKey, QMap<int, int>> votes;   // chapter -> volume number -> row count
    for (const ChapterRow& row : rows) {
        ChapterKey volKey;
        ChapterKey chapKey;
        if (!parseChapterKey(row.vol, &volKey) || !parseChapterKey(row.chap, &chapKey))
            continue;
        // Only the volume's WHOLE part votes: a "1.5" volume tag is a vote for book 1.
        votes[chapKey][volKey.whole] += 1;
    }

    QMap<ChapterKey, int> assign;
    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it) {
        const QMap<int, int>& perVolume = it.value();
        int most = 0;
        for (auto vote = perVolume.constBegin(); vote != perVolume.constEnd(); ++vote)
            most = std::max(most, vote.value());
        int winner = 0;
        int winners = 0;
        for (auto vote = perVolume.constBegin(); vote != perVolume.constEnd(); ++vote) {
            if (vote.value() == most) {
                winner = vote.key();
                ++winners;
            }
        }
        if (winners == 1)
            assign.insert(it.key(), winner);
    }
    return fixStrayTags(assign);
}

QList<VolumeRange> groupVolumes(const QList<ChapterRow>& rows)
{
    const QMap<ChapterKey, int> assign = majorityAssign(rows);

    QMap<int, QList<ChapterKey>> buckets;   // volume number -> its chapter keys
    for (auto it = assign.constBegin(); it != assign.constEnd(); ++it)
        buckets[it.value()].append(it.key());

    QList<VolumeRange> vols;
    vols.reserve(buckets.size());
    for (auto it = buckets.begin(); it != buckets.end(); ++it) {   // QMap<int,...>: ascending
        QList<ChapterKey>& chaps = it.value();
        std::sort(chaps.begin(), chaps.end());
        VolumeRange range;
        range.number = it.key();
        range.chapterStart = formatChapterKey(chaps.first());
        range.chapterEnd = formatChapterKey(chaps.last());
        vols.append(range);
    }
    return vols;
}

// True when the earliest chapter number is fractional (e.g. Berserk's 0.01 prologue).
// A fractional START means Comick's numbering is offset from WeebCentral's integer
// chapters, so the ranges need Phase-1 normalization before the join lines up. Titles
// that start on a clean integer (Bleach=1, Death Note=0) join directly. Mid-series
// sub-chapters (27.2) are NOT flagged — they bucket into their volume's range fine.
// (Data-grounded 2026-07-25: sampled fixtures are 95-100% integer; the only real join
// risk is a fractional start-offset, which this detects.)
bool numberingIsOddball(const QList<ChapterRow>& rows)
{
    bool any = false;
    ChapterKey lowest;
    for (const ChapterRow& row : rows) {
        ChapterKey key;
        if (!parseChapterKey(row.chap, &key))
            continue;
        if (!any || key < lowest) {
            lowest = key;
            any = true;
        }
    }
    if (!any)
        return true;
    return lowest.isSideChapter();
}

// Checks, in order:
//   1. no numbering quirk;
//   2. at least one volume;
//   3. the first volume is 0 or 1;
//   4. the volume numbers are one unbroken run;
//   5. no volume's chapter span runs into the next one's;
//   6. COVERAGE — every whole chapter between the first volume's chapterStart and the
//      last volume's chapterEnd is assigned to some volume.
//
// Coverage is one rule doing three jobs: it catches chapters stranded at the seam
// BETWEEN two volumes (Vinland Saga 210-218, untagged in every language, while the
// volume numbers 1..29 read perfectly), and chapters swallowed INSIDE a volume whose
// span was stretched across a hole by two distant anchors (volume 2 tagged on chapters
// 11 and 20 only, 12-19 tagged by nobody — which is the sparse-anchor interpolation
// this whole gate exists to refuse), and it removes the old pairwise loop's blind spot
// before the first volume by defining the range explicitly.
//
// Deliberately OUT of scope: chapters after the last volume's end — the legitimate
// uncollected tail of an ongoing series, which the app surfaces as "Latest chapters" —
// and chapters before the first volume's start, e.g. Bleach's untagged chapter 0
// one-shot, which genuinely is in no book.
//
// Coverage needs to know which chapters are ASSIGNED, which the collapsed spans can't
// say, so it re-derives the assignment from `rows` (the same row list groupVolumes was
// given; the derivation is pure, so this is the same map, not a second opinion).
//
// Only WHOLE chapters are checked for coverage. An untagged SIDE chapter (168.5)
// between two volumes is assumed to be an extra that was never bound into either book
// — e.g. Bleach volume 19 ends at 168 and volume 20 starts at 169, back to back, with
// an untagged "168.5" between them. That assumption is an inference from today's
// corpus, NOT something this code verifies: all it actually checks is whether the label
// has a dot. Side chapters can be real volume content (Bleach volume 36 is
// 315.1-315.9), so a long untagged run of them would slip through. The longest run
// observed anywhere in the corpus is 2 (measured 2026-07-29), against the 9 it would
// take to matter. If that ever grows, revisit this with the evidence rather than a
// guess.
GateVerdict gateVolumes(const QList<VolumeRange>& vols, bool numberingQuirk,
                        const QList<ChapterRow>& rows)
{
    if (numberingQuirk)
        return {false, QStringLiteral("numbering quirk (fractional chapter origin)")};
    if (vols.isEmpty())
        return {false, QStringLiteral("no mapped volumes")};

    QList<VolumeRange> ordered = vols;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const VolumeRange& a, const VolumeRange& b) {
                         return a.number < b.number;
                     });

    const int firstNumber = ordered.first().number;
    if (firstNumber != 0 && firstNumber != 1)
        return {false, QStringLiteral("first mapped volume is %1, not 0/1").arg(firstNumber)};
    for (int i = 1; i < ordered.size(); ++i) {
        if (ordered.at(i).number != ordered.at(i - 1).number + 1)
            return {false, QStringLiteral("gap after volume %1").arg(ordered.at(i - 1).number)};
    }

    struct Span {
        int number = 0;
        ChapterKey start;
        ChapterKey end;
    };
    QList<Span> spans;
    spans.reserve(ordered.size());
    for (const VolumeRange& vol : ordered) {
        Span span;
        span.number = vol.number;
        if (!parseChapterKey(vol.chapterStart, &span.start) || !parseChapterKey(vol.chapterEnd, &span.end))
            continue;                       // an unparseable span is skipped, not judged
        spans.append(span);
    }
    if (spans.isEmpty())
        return {true, QString()};

    for (int i = 1; i < spans.size(); ++i) {
        // Python: reject when `start_b <= end_a`.
        if (!(spans.at(i - 1).end < spans.at(i).start)) {
            return {false, QStringLiteral("volume %1 span overlaps volume %2")
                               .arg(spans.at(i - 1).number)
                               .arg(spans.at(i).number)};
        }
    }

    const ChapterKey firstChapter = spans.first().start;
    const ChapterKey lastChapter = spans.last().end;
    const QMap<ChapterKey, int> assigned = majorityAssign(rows);

    // A set, not a list: one chapter arrives on many rows (one per language), and the
    // count in the reason has to be chapters, not rows.
    QList<ChapterKey> stranded;
    for (const ChapterRow& row : rows) {
        ChapterKey key;
        if (!parseChapterKey(row.chap, &key))
            continue;
        if (key.isSideChapter())
            continue;
        if (key < firstChapter || lastChapter < key)
            continue;
        if (assigned.contains(key))
            continue;
        stranded.append(key);
    }
    std::sort(stranded.begin(), stranded.end());
    stranded.erase(std::unique(stranded.begin(), stranded.end()), stranded.end());

    if (!stranded.isEmpty()) {
        return {false, QStringLiteral("%1 chapter(s) in no volume (first: %2)")
                           .arg(static_cast<int>(stranded.size()))
                           .arg(formatChapterKey(stranded.first()))};
    }
    return {true, QString()};
}

} // namespace tankoban::manga::comick
