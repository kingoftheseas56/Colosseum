// tests/comicreader_cache_harness.cpp
//
// Comic Reader (Agent 1, plan 2026-07-23) — Task 3 fixtures.
// The pinned, budgeted LRU page cache: (generation, page) keyed, bytesUsed is
// the exact sum of QImage::sizeInBytes() over live entries, LRU eviction never
// touches a pinned (visible) page even if that leaves the cache over budget.
// House CHECK idiom: collect every failure (never abort), print each FAIL, then
// print exactly COMICREADER_CACHE_OK iff zero failures, else return 1.

#include "comicreader/ComicReaderPageCache.h"

#include <QImage>
#include <QVector>

#include <cstdio>

using namespace comicreader;

static int g_failures = 0;
#define CHECK(cond, label)                                        \
    do {                                                          \
        if (!(cond)) {                                            \
            std::fprintf(stderr, "FAIL: %s\n", (label));          \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

// --- fixture builders --------------------------------------------------------

constexpr qint64 kMiB = 1024LL * 1024;
constexpr qint64 kImageBytes = 4 * kMiB;   // 1024*1024*4 == 4 MiB exactly
constexpr qint64 kTestBudget = 12 * kMiB;  // ~3 images fit

// 1024x1024 ARGB32 filled with a distinct color so replace/round-trip checks
// can tell two synthetic pages apart, not just their size.
static QImage synthetic(QRgb color) {
    QImage img(1024, 1024, QImage::Format_ARGB32);
    img.fill(color);
    return img;
}

int main() {
    // ── Fixture 1: get() changes LRU order ───────────────────────────────────
    // insert A,B,C (12 MiB budget, exactly 3 fit); get(A) makes A most-recent;
    // insert D exceeds budget by one image → the evicted one must be the
    // oldest UNPINNED, which after the get is B (order was A,B,C -> get(A) ->
    // B,C,A) — A, C, D all survive.
    {
        ComicReaderPageCache cache(kTestBudget);
        cache.insert(1, 0, synthetic(qRgb(255, 0, 0)));   // A
        cache.insert(1, 1, synthetic(qRgb(0, 255, 0)));   // B
        cache.insert(1, 2, synthetic(qRgb(0, 0, 255)));   // C
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F1 three images exactly fill budget, no eviction yet");

        CHECK(cache.get(1, 0).has_value(), "F1 get(A) present before touch");
        cache.insert(1, 3, synthetic(qRgb(255, 255, 0))); // D, pushes over budget

        CHECK(!cache.get(1, 1).has_value(), "F1 B (oldest unpinned after A's touch) is evicted");
        CHECK(cache.get(1, 0).has_value(), "F1 A survives (was touched most-recent)");
        CHECK(cache.get(1, 2).has_value(), "F1 C survives");
        CHECK(cache.get(1, 3).has_value(), "F1 D survives (just inserted)");
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F1 back within budget after one eviction");
    }

    // ── Fixture 2: oldest unpinned evicts first, no gets in between ──────────
    // Plain FIFO-under-budget-pressure case: insert A,B,C,D in order with no
    // touches → A (strictly oldest) is the one evicted.
    {
        ComicReaderPageCache cache(kTestBudget);
        cache.insert(2, 0, synthetic(qRgb(1, 1, 1)));   // A
        cache.insert(2, 1, synthetic(qRgb(2, 2, 2)));   // B
        cache.insert(2, 2, synthetic(qRgb(3, 3, 3)));   // C
        cache.insert(2, 3, synthetic(qRgb(4, 4, 4)));   // D, forces eviction

        CHECK(!cache.get(2, 0).has_value(), "F2 A (strictly oldest) evicted first");
        CHECK(cache.get(2, 1).has_value(), "F2 B survives");
        CHECK(cache.get(2, 2).has_value(), "F2 C survives");
        CHECK(cache.get(2, 3).has_value(), "F2 D survives");
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F2 exactly 3 images worth after eviction");
    }

    // ── Fixture 3: pinned visible pages survive full pressure ────────────────
    // Pin A (the oldest); pump B,C,D,E through afterward. Every eviction must
    // pick the oldest UNPINNED entry, never A, even though A is always the
    // least-recently-touched key in the list.
    {
        ComicReaderPageCache cache(kTestBudget);
        cache.insert(3, 0, synthetic(qRgb(10, 0, 0)));  // A
        cache.setPinned(3, {0});                        // pin A
        cache.insert(3, 1, synthetic(qRgb(0, 10, 0)));  // B
        cache.insert(3, 2, synthetic(qRgb(0, 0, 10)));  // C — bytesUsed now == budget, no eviction yet
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F3 A+B+C exactly at budget");

        cache.insert(3, 3, synthetic(qRgb(10, 10, 0))); // D — evicts oldest unpinned (B)
        CHECK(cache.get(3, 0).has_value(), "F3 A (pinned) survives D's insert");
        CHECK(!cache.get(3, 1).has_value(), "F3 B (oldest unpinned) evicted for D");
        CHECK(cache.get(3, 2).has_value(), "F3 C survives D's insert");
        CHECK(cache.get(3, 3).has_value(), "F3 D present");

        cache.insert(3, 4, synthetic(qRgb(0, 10, 10))); // E — evicts oldest unpinned (C, since B already gone)
        CHECK(cache.get(3, 0).has_value(), "F3 A (pinned) survives E's insert too");
        CHECK(!cache.get(3, 2).has_value(), "F3 C (next-oldest unpinned) evicted for E");
        CHECK(cache.get(3, 3).has_value(), "F3 D survives E's insert");
        CHECK(cache.get(3, 4).has_value(), "F3 E present");
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F3 stays at 3 images worth: A(pinned)+D+E");
    }

    // ── Fixture 3b: an all-pinned generation MAY exceed budget ───────────────
    // Every page pinned before any insert happens (pin recorded ahead of the
    // decode, exactly like the visible+neighbor pin at chapter-open). Five 4
    // MiB images at a 12 MiB budget: with nothing evictable, bytesUsed must be
    // allowed to exceed the budget rather than drop a pinned page.
    {
        ComicReaderPageCache cache(kTestBudget);
        cache.setPinned(4, {0, 1, 2, 3, 4}); // pin ahead of insert
        cache.insert(4, 0, synthetic(qRgb(1, 0, 0)));
        cache.insert(4, 1, synthetic(qRgb(2, 0, 0)));
        cache.insert(4, 2, synthetic(qRgb(3, 0, 0)));
        cache.insert(4, 3, synthetic(qRgb(4, 0, 0)));
        cache.insert(4, 4, synthetic(qRgb(5, 0, 0)));

        CHECK(cache.bytesUsed() == 5 * kImageBytes, "F3b all-pinned: nothing evicted, bytesUsed exceeds budget");
        CHECK(cache.bytesUsed() > kTestBudget, "F3b bytesUsed is actually over the budget line");
        for (int p = 0; p < 5; ++p)
            CHECK(cache.get(4, p).has_value(), "F3b every pinned page still gettable");
    }

    // ── Fixture 4: setPinned replaces the pinned set for that generation ─────
    // Pin {0,1}, then re-pin to just {1}: page 0 becomes evictable again.
    {
        ComicReaderPageCache cache(kTestBudget);
        cache.setPinned(5, {0, 1});
        cache.insert(5, 0, synthetic(qRgb(9, 9, 9)));
        cache.insert(5, 1, synthetic(qRgb(8, 8, 8)));
        cache.insert(5, 2, synthetic(qRgb(7, 7, 7)));
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F4 three images at budget, no eviction yet");

        cache.setPinned(5, {1}); // page 0 no longer pinned
        cache.insert(5, 3, synthetic(qRgb(6, 6, 6))); // forces one eviction

        CHECK(!cache.get(5, 0).has_value(), "F4 page 0 evicted once its pin was dropped");
        CHECK(cache.get(5, 1).has_value(), "F4 page 1 (still pinned) survives");
        CHECK(cache.get(5, 2).has_value(), "F4 page 2 survives");
        CHECK(cache.get(5, 3).has_value(), "F4 page 3 (just inserted) survives");
    }

    // ── Fixture 4b: pinning is per-generation, not per-page-index ────────────
    // Pin page 0 in gen A only. Insert page 0 in BOTH gen A and gen B (same
    // page index, different generations sharing one cache-wide budget). Gen
    // A's page 0 must survive pressure; gen B's page 0 — same index, no pin
    // of its own — must be just as evictable as any other unpinned entry.
    // This locks the invariant Task 7 relies on: a pin never leaks across
    // generations by page-index coincidence.
    {
        ComicReaderPageCache cache(kTestBudget);
        constexpr quint64 genA = 40;
        constexpr quint64 genB = 41;
        cache.setPinned(genA, {0}); // pin page 0 for gen A only; gen B has no pins at all

        cache.insert(genA, 0, synthetic(qRgb(40, 0, 0))); // pinned, oldest
        cache.insert(genB, 0, synthetic(qRgb(0, 41, 0))); // unpinned, same page index as the pin above
        cache.insert(genA, 1, synthetic(qRgb(40, 1, 0))); // unpinned
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F4b three images at budget, no eviction yet");

        cache.insert(genB, 2, synthetic(qRgb(0, 41, 2))); // forces one eviction

        CHECK(cache.get(genA, 0).has_value(), "F4b gen A page 0 (pinned) survives the pressure");
        CHECK(!cache.get(genB, 0).has_value(), "F4b gen B page 0 (unpinned, same index) is the one evicted");
        CHECK(cache.get(genA, 1).has_value(), "F4b gen A page 1 survives");
        CHECK(cache.get(genB, 2).has_value(), "F4b gen B page 2 (just inserted) survives");
    }

    // ── Fixture 5: clearGeneration is generation-isolated ────────────────────
    {
        ComicReaderPageCache cache(kTestBudget);
        cache.insert(10, 0, synthetic(qRgb(20, 0, 0))); // gen A
        cache.insert(10, 1, synthetic(qRgb(20, 1, 0))); // gen A
        cache.insert(11, 0, synthetic(qRgb(0, 20, 0))); // gen B
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F5 three live entries across two generations");

        cache.clearGeneration(10);

        CHECK(!cache.get(10, 0).has_value(), "F5 gen A page 0 gone after clearGeneration(A)");
        CHECK(!cache.get(10, 1).has_value(), "F5 gen A page 1 gone after clearGeneration(A)");
        CHECK(cache.get(11, 0).has_value(), "F5 gen B untouched by clearGeneration(A)");
        CHECK(cache.bytesUsed() == 1 * kImageBytes, "F5 bytesUsed reflects only gen B's surviving entry");
    }

    // ── Fixture 6: duplicate insert at the same (gen,page) replaces, no double count ──
    {
        ComicReaderPageCache cache(kTestBudget);
        const QImage first = synthetic(qRgb(1, 2, 3));
        const QImage second = synthetic(qRgb(4, 5, 6));
        cache.insert(20, 0, first);
        CHECK(cache.bytesUsed() == kImageBytes, "F6 one entry after first insert");

        cache.insert(20, 0, second); // same key, different pixels
        CHECK(cache.bytesUsed() == kImageBytes, "F6 re-insert at same key does not double-count bytes");
        const auto got = cache.get(20, 0);
        CHECK(got.has_value(), "F6 replaced entry is present");
        CHECK(got.has_value() && *got == second, "F6 replaced entry holds the NEW image, not the old one");
    }

    // ── Fixture 7: setBudget shrink evicts immediately, unpinned first ───────
    {
        ComicReaderPageCache cache(kTestBudget);
        cache.insert(30, 0, synthetic(qRgb(30, 0, 0))); // A
        cache.insert(30, 1, synthetic(qRgb(30, 1, 0))); // B
        cache.insert(30, 2, synthetic(qRgb(30, 2, 0))); // C
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F7 three images at the 12 MiB test budget");

        cache.setBudget(1 * kImageBytes); // shrink 12 MiB -> 4 MiB (test-scale 512->256 analog)

        CHECK(cache.bytesUsed() == 1 * kImageBytes, "F7 shrink evicts immediately down to the new budget");
        CHECK(!cache.get(30, 0).has_value(), "F7 A (oldest) evicted by the shrink");
        CHECK(!cache.get(30, 1).has_value(), "F7 B (next oldest) evicted by the shrink");
        CHECK(cache.get(30, 2).has_value(), "F7 C (most recent) survives the shrink");
    }

    // ── Fixture 7b: shrink respects a pin even when the pinned page is oldest ──
    {
        ComicReaderPageCache cache(kTestBudget);
        cache.insert(31, 0, synthetic(qRgb(31, 0, 0))); // A — oldest
        cache.setPinned(31, {0});                        // pin A after insert
        cache.insert(31, 1, synthetic(qRgb(31, 1, 0))); // B
        cache.insert(31, 2, synthetic(qRgb(31, 2, 0))); // C
        CHECK(cache.bytesUsed() == 3 * kImageBytes, "F7b three images before shrink");

        cache.setBudget(1 * kImageBytes); // shrink to fit only one image

        CHECK(cache.get(31, 0).has_value(), "F7b pinned A survives the shrink despite being oldest");
        CHECK(!cache.get(31, 1).has_value(), "F7b B evicted by the shrink");
        CHECK(!cache.get(31, 2).has_value(), "F7b C evicted by the shrink too (both unpinned)");
        CHECK(cache.bytesUsed() == 1 * kImageBytes, "F7b bytesUsed == just the pinned survivor");
    }

    // ── Fixture 8: default budget is the 512 MiB house number ────────────────
    {
        ComicReaderPageCache cache; // default ctor argument
        CHECK(cache.bytesUsed() == 0, "F8 fresh cache starts empty");
    }

    if (g_failures == 0) {
        std::puts("COMICREADER_CACHE_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
