// tests/comicreader_strip_harness.cpp
//
// Comic Reader (Agent 1, plan 2026-07-23) — Task 6 fixtures.
// ComicReaderStripModel is pure geometry: estimated-vs-real page sizing,
// running tops with a gap, the ±marginScreens decode window, pageAtCenter's
// binary search, and the anti-jump compensation ported from TB2's
// ScrollStripCanvas + TankobanQTGroundWork's comic_reader.py on_page_loaded.
//
// All fixtures share ONE base six-page layout (see makeBasePages/makeOptions)
// so every expected number below is hand-derived once and reused; each block
// gets its own fresh ComicReaderStripModel so no fixture's mutations leak
// into another's expectations.
//
// House CHECK idiom: collect every failure (never abort), print each FAIL,
// then print exactly COMICREADER_STRIP_OK iff zero failures, else return 1.

#include "comicreader/ComicReaderStripModel.h"
#include "comicreader/ComicReaderTypes.h"

#include <QCoreApplication>
#include <QSize>
#include <QVector>

#include <cmath>
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

static bool approx(double a, double b) { return std::fabs(a - b) < 1e-6; }

// --- fixture builders --------------------------------------------------------
//
// Layout (viewportWidth=1000, portraitWidthPct=80, gap=20):
//   page0: portrait, sourceSize(1000,1500) -> ratio 1.5  -> dw=800  dh=1200
//   page1: spread,   sourceSize(2000,1000) -> ratio 0.5  -> dw=1000 dh=500
//   page2: unknown (not yet decoded)       -> ratio 1.5  -> dw=800  dh=1200
//   page3: portrait, sourceSize(800,1000)  -> ratio 1.25 -> dw=800  dh=1000
//   page4: unknown (not yet decoded)       -> ratio 1.5  -> dw=800  dh=1200
//   page5: portrait, sourceSize(1200,1800) -> ratio 1.5  -> dw=800  dh=1200
//
// Running tops (each += displayHeight + gap(20)):
//   top0=0     bottom0=1200
//   top1=1220  bottom1=1720
//   top2=1740  bottom2=2940
//   top3=2960  bottom3=3960
//   top4=3980  bottom4=5180
//   top5=5200  bottom5=6400
//   contentHeight = 6400 (== top5+dh5, no trailing gap)

static ComicReaderStripModel::Options makeOptions() {
    ComicReaderStripModel::Options opt;
    opt.viewportWidth = 1000;
    opt.portraitWidthPct = 80;
    opt.gap = 20;
    return opt;
}

static QVector<PageMeta> makeBasePages() {
    QVector<PageMeta> pages;

    PageMeta p0;
    p0.index = 0;
    p0.sourceSize = QSize(1000, 1500);
    p0.decoded = true;
    p0.detectedSpread = false;
    pages.append(p0);

    PageMeta p1;
    p1.index = 1;
    p1.sourceSize = QSize(2000, 1000);
    p1.decoded = true;
    p1.detectedSpread = true; // 2000 >= 1.08*1000
    pages.append(p1);

    PageMeta p2; // unknown — never decoded
    p2.index = 2;
    pages.append(p2);

    PageMeta p3;
    p3.index = 3;
    p3.sourceSize = QSize(800, 1000);
    p3.decoded = true;
    p3.detectedSpread = false;
    pages.append(p3);

    PageMeta p4; // unknown — never decoded
    p4.index = 4;
    pages.append(p4);

    PageMeta p5;
    p5.index = 5;
    p5.sourceSize = QSize(1200, 1800);
    p5.decoded = true;
    p5.detectedSpread = false;
    pages.append(p5);

    return pages;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ── Fixture 1: unknown pages use the 1600x2400 estimate ─────────────────
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());
        // page2 and page4 are both never-decoded: estimate ratio 2400/1600=1.5,
        // displayWidth = 1000*0.80 = 800, displayHeight = 800*1.5 = 1200 exactly.
        QModelIndex idx2 = model.index(2, 0);
        QModelIndex idx4 = model.index(4, 0);
        CHECK(approx(model.data(idx2, ComicReaderStripModel::DisplayWidthRole).toDouble(), 800.0),
              "F1 page2 (unknown) displayWidth == 800");
        CHECK(approx(model.data(idx2, ComicReaderStripModel::DisplayHeightRole).toDouble(), 1200.0),
              "F1 page2 (unknown) displayHeight == 1200 (1600x2400 estimate scaled)");
        CHECK(approx(model.data(idx4, ComicReaderStripModel::DisplayHeightRole).toDouble(), 1200.0),
              "F1 page4 (unknown) displayHeight == 1200 too");
    }

    // ── Fixture 2: spread pages span the full viewport width ────────────────
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());
        QModelIndex idx1 = model.index(1, 0);
        CHECK(approx(model.data(idx1, ComicReaderStripModel::DisplayWidthRole).toDouble(), 1000.0),
              "F2 page1 (spread) displayWidth == full viewportWidth (1000)");
        CHECK(approx(model.data(idx1, ComicReaderStripModel::DisplayHeightRole).toDouble(), 500.0),
              "F2 page1 (spread) displayHeight == 500 (ratio 0.5 on full width)");
        // Sanity: a portrait neighbor stays at the 80% fraction, not full width.
        QModelIndex idx0 = model.index(0, 0);
        CHECK(approx(model.data(idx0, ComicReaderStripModel::DisplayWidthRole).toDouble(), 800.0),
              "F2 page0 (portrait) displayWidth == 800 (80% of viewport)");
    }

    // ── Fixture 3: exact gap between consecutive tops; contentHeight = sum ──
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());
        CHECK(approx(model.pageTop(0), 0.0), "F3 top0 == 0");
        CHECK(approx(model.pageTop(1), 1220.0), "F3 top1 == 1220");
        CHECK(approx(model.pageTop(2), 1740.0), "F3 top2 == 1740");
        CHECK(approx(model.pageTop(3), 2960.0), "F3 top3 == 2960");
        CHECK(approx(model.pageTop(4), 3980.0), "F3 top4 == 3980");
        CHECK(approx(model.pageTop(5), 5200.0), "F3 top5 == 5200");
        // Gap between every consecutive pair is exactly 20 (top[i+1] - bottom[i]).
        CHECK(approx(model.pageTop(1) - (model.pageTop(0) + 1200.0), 20.0), "F3 gap0->1 == 20");
        CHECK(approx(model.pageTop(3) - (model.pageTop(2) + 1200.0), 20.0), "F3 gap2->3 == 20");
        CHECK(approx(model.pageTop(4) - (model.pageTop(3) + 1000.0), 20.0), "F3 gap3->4 == 20");
        // contentHeight = sum(heights) + (n-1)*gap = 6300 + 100 = 6400, no trailing gap.
        CHECK(approx(model.contentHeight(), 6400.0), "F3 contentHeight == 6400 (sum + inter-page gaps only)");
    }

    // ── Fixture 4: window() returns EXACTLY the ±1.5-screen intersecting set ─
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        // vpHeight=600, top=1800 -> margin=900 -> band=[900,3300].
        // page0 [0,1200) overlaps [900,1200); page1 [1220,1720) fully inside;
        // page2 [1740,2960) fully inside; page3 top=2960<=3300 -> included
        // (overlap [2960,3300)); page4 top=3980>3300 -> excluded.
        QVector<int> w = model.window(1800.0, 600.0, 1.5);
        QVector<int> expected{0, 1, 2, 3};
        CHECK(w == expected, "F4 window(top=1800,vpH=600,1.5) == [0,1,2,3]");

        // Exact-boundary flip, engineered so loadBot lands precisely on page3's
        // top (2960): vpH=400 -> margin=600, band bottom = top + vpH + margin.
        // Case A: loadBot == 2960 exactly -> page3 (top==loadBot) INCLUDED
        // (the model's rule is "top > loadBot breaks", so equality is inside).
        QVector<int> wIn = model.window(1960.0, 400.0, 1.5); // loadBot = 1960+400+600 = 2960
        CHECK(wIn.contains(3), "F4 boundary: page3 top==loadBot is INCLUDED");
        CHECK(!wIn.contains(4), "F4 boundary: page4 (well past loadBot) excluded");

        // Case B: loadBot == 2959.9 (just under page3's top) -> page3 EXCLUDED.
        QVector<int> wOut = model.window(1959.9, 400.0, 1.5); // loadBot = 2959.9
        CHECK(!wOut.contains(3), "F4 boundary: page3 top>loadBot(2959.9) is EXCLUDED");
        CHECK(wOut.contains(2), "F4 boundary: page2 still included just below the cutoff");
    }

    // ── Fixture 5: pageAtCenter matches a hand-computed layout ───────────────
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        CHECK(model.pageAtCenter(0.0, 1200.0) == 0,
              "F5 center=600 (top=0,vpH=1200) -> page0");
        CHECK(model.pageAtCenter(1220.0, 1080.0) == 2,
              "F5 center=1760 (top=1220,vpH=1080) -> page2 [1740,2960)");
        // Exact-boundary: center lands precisely on page3's top (2960) — must
        // resolve to page3, not page2, since bands are half-open [top,bottom).
        CHECK(model.pageAtCenter(2960.0, 0.0) == 3,
              "F5 center==2960 exactly on a page-top boundary -> page3, not page2");
        CHECK(model.pageAtCenter(5200.0, 1200.0) == 5,
              "F5 center=5800 (top=5200,vpH=1200) -> page5 (last page)");
        // Beyond all content: clamps to the last page rather than going out of range.
        CHECK(model.pageAtCenter(10000.0, 0.0) == 5,
              "F5 center far beyond content clamps to last page (5)");
    }

    // ── Fixture 6: anti-jump compensation (accumulate-and-clear) ─────────────
    // takePendingCompensation() DRAINS the accumulator on every call, so each
    // threshold/ordering comparison below gets its OWN fresh model — reusing
    // one model across two queries would have the second always read 0
    // (already cleared by the first), which is exactly Fixture 6a's second
    // assertion below, deliberately, to prove the draining itself.

    // 6a: a page ABOVE the viewport top changes height -> compensation == delta,
    // and a second query right after returns 0 (the accumulator was cleared).
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        PageMeta grown = makeBasePages()[0];
        grown.sourceSize = QSize(1000, 2000); // ratio 2.0 -> dh = 800*2.0 = 1600 (was 1200)
        model.updatePage(grown);

        CHECK(approx(model.data(model.index(0, 0), ComicReaderStripModel::DisplayHeightRole).toDouble(), 1600.0),
              "F6a page0's own height grew to 1600");
        CHECK(approx(model.pageTop(0), 0.0), "F6a page0's own top is unaffected by its own height change");
        // Downstream pages shifted down by +400.
        CHECK(approx(model.pageTop(1), 1620.0), "F6a page1's top shifted by +400 (was 1220)");
        CHECK(approx(model.contentHeight(), 6800.0), "F6a contentHeight grew by +400 (was 6400)");

        // viewportTop=1500 is strictly below page0's (unchanged) top of 0 -> compensate by +400.
        CHECK(approx(model.takePendingCompensation(1500.0), 400.0),
              "F6a compensation == +400 (newHeight 1600 - oldHeight 1200) for a viewport below page0");
        // Draining is unconditional: a second call, even with a viewportTop
        // that would have qualified, now finds nothing left to sum.
        CHECK(approx(model.takePendingCompensation(1500.0), 0.0),
              "F6a a second take right after the first returns 0 (accumulator already cleared)");
    }

    // 6a-boundary: viewportTop == the changed page's own top (not strictly
    // greater) -> no compensation (fresh model: takePendingCompensation drains).
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        PageMeta grown = makeBasePages()[0];
        grown.sourceSize = QSize(1000, 2000); // same +400 growth as 6a
        model.updatePage(grown);

        CHECK(approx(model.takePendingCompensation(0.0), 0.0),
              "F6a-boundary compensation == 0 when viewportTop is at/above the changed page's top");
    }

    // 6b: a page BELOW the viewport top changes height -> compensation == 0.
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        PageMeta grown = makeBasePages()[5];
        grown.sourceSize = QSize(1200, 2400); // ratio 2.0 -> dh = 800*2.0 = 1600 (was 1200)
        model.updatePage(grown);

        CHECK(approx(model.pageTop(5), 5200.0), "F6b page5's own top is unaffected (last page, nothing shifts after it)");
        // viewportTop=1500 is well ABOVE page5's top (5200) -> that page is below
        // the viewport, so no compensation is owed.
        CHECK(approx(model.takePendingCompensation(1500.0), 0.0),
              "F6b compensation == 0 when the changed page is below the viewport");
    }

    // 6b-alt: the SAME kind of update, queried against a viewportTop actually
    // below page5's top, confirms the query is genuinely viewportTop-parameterized
    // (fresh model — a shared one would already be drained by 6b's query).
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        PageMeta grown = makeBasePages()[5];
        grown.sourceSize = QSize(1200, 2400);
        model.updatePage(grown);

        CHECK(approx(model.takePendingCompensation(6000.0), 400.0),
              "F6b-alt same kind of update DOES compensate for a viewport scrolled past page5's top");
    }

    // 6c: a BATCH — two pages both above viewportTop decode to taller sizes via
    // TWO updatePage() calls, then ONE takePendingCompensation() returns the SUM
    // of both deltas, and a second call returns 0 (cleared).
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        PageMeta grown0 = makeBasePages()[0];
        grown0.sourceSize = QSize(1000, 2000); // dh 1200 -> 1600, delta +400, oldTop 0
        model.updatePage(grown0);

        // page1's oldTop at the moment of ITS OWN update already reflects
        // page0's +400 shift (1220 -> 1620) — updatePage() captures oldTop
        // fresh at call time, not from the original rebuild() layout.
        CHECK(approx(model.pageTop(1), 1620.0), "F6c page1's top before its own update reflects page0's earlier shift");

        PageMeta grown1 = makeBasePages()[1];
        grown1.sourceSize = QSize(2000, 1500); // spread stays full width 1000; ratio 0.75 -> dh 750 (was 500), delta +250
        model.updatePage(grown1);

        CHECK(approx(model.data(model.index(1, 0), ComicReaderStripModel::DisplayHeightRole).toDouble(), 750.0),
              "F6c page1 grew to displayHeight 750");

        // Both pages' oldTops (0 and 1620) sit strictly above viewportTop=2000 ->
        // the single query sums BOTH deltas: 400 + 250 = 650.
        CHECK(approx(model.takePendingCompensation(2000.0), 650.0),
              "F6c batch query sums both above-fold deltas (400 + 250 = 650)");
        CHECK(approx(model.takePendingCompensation(2000.0), 0.0),
              "F6c a second take right after returns 0 (accumulator cleared)");
    }

    // 6d: a MIXED batch — one page above the fold and one below — a single
    // query returns ONLY the above-fold delta.
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        PageMeta grown0 = makeBasePages()[0];
        grown0.sourceSize = QSize(1000, 2000); // delta +400, oldTop 0 (above the fold)
        model.updatePage(grown0);

        PageMeta grown5 = makeBasePages()[5];
        grown5.sourceSize = QSize(1200, 2400); // delta +400, oldTop 5600 at call time (below the fold)
        model.updatePage(grown5);

        CHECK(approx(model.pageTop(5), 5600.0), "F6d page5's oldTop at its own update reflects page0's earlier +400 shift");

        // viewportTop=2000 sits above page0's oldTop(0) but below page5's oldTop(5600):
        // only page0's delta (400) qualifies, page5's (400) is excluded.
        CHECK(approx(model.takePendingCompensation(2000.0), 400.0),
              "F6d mixed batch: only the above-fold page's delta is summed (400, not 800)");
    }

    // ── Fixture 7: eviction (ready flips false, no size change) must NOT move geometry ──
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        // Real decode arrives for page2 (was the 1600x2400 estimate at dh=1200):
        // sourceSize(900,900) -> ratio 1.0 -> dw=800, dh=800. Locks in the real size.
        PageMeta decoded = makeBasePages()[2];
        decoded.decoded = true;
        decoded.sourceSize = QSize(900, 900);
        model.updatePage(decoded);

        CHECK(approx(model.data(model.index(2, 0), ComicReaderStripModel::DisplayHeightRole).toDouble(), 800.0),
              "F7 page2 real decode: displayHeight == 800");
        CHECK(approx(model.pageTop(3), 2560.0), "F7 downstream page3 shifted after the real decode (was 2960)");
        CHECK(approx(model.contentHeight(), 6000.0), "F7 contentHeight after real decode == 6000 (was 6400, -400)");
        // Shrinking a page above the fold compensates NEGATIVELY too (the sign
        // just follows delta) — drain it now so the eviction check below starts
        // from an empty accumulator.
        CHECK(approx(model.takePendingCompensation(2000.0), -400.0),
              "F7 the real decode's own shrink (-400) is a valid (negative) compensation, above viewportTop=2000");

        const double topAfterDecode = model.pageTop(2);
        const double heightAfterDecode = model.data(model.index(2, 0), ComicReaderStripModel::DisplayHeightRole).toDouble();
        const double contentHeightAfterDecode = model.contentHeight();
        const double page3TopAfterDecode = model.pageTop(3);

        // Eviction: the SAME page reports decoded=false again (cache pressure
        // dropped its pixels), sourceSize zeroed out by the caller. Since the
        // real size was already locked in, geometry must be untouched.
        PageMeta evicted;
        evicted.index = 2;
        evicted.decoded = false;
        evicted.sourceSize = QSize();
        evicted.detectedSpread = false;
        model.updatePage(evicted);

        CHECK(approx(model.pageTop(2), topAfterDecode), "F7 eviction: page2's top unchanged");
        CHECK(approx(model.data(model.index(2, 0), ComicReaderStripModel::DisplayHeightRole).toDouble(), heightAfterDecode),
              "F7 eviction: page2's displayHeight unchanged (stays 800, NOT reverted to the 1200 estimate)");
        CHECK(approx(model.pageTop(3), page3TopAfterDecode), "F7 eviction: downstream page3's top unchanged");
        CHECK(approx(model.contentHeight(), contentHeightAfterDecode), "F7 eviction: contentHeight unchanged");
        // Ready role does flip, even though geometry didn't move.
        CHECK(model.data(model.index(2, 0), ComicReaderStripModel::ReadyRole).toBool() == false,
              "F7 eviction: ReadyRole flips to false");
        // No height change on this call -> it appended nothing new to the
        // accumulator, which was already drained above -> zero at any viewportTop.
        CHECK(approx(model.takePendingCompensation(1000000.0), 0.0), "F7 eviction produces zero compensation at any viewportTop");
    }

    // ── Fixture 8: data() role values + roleNames() ──────────────────────────
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());

        CHECK(model.rowCount() == 6, "F8 rowCount == 6");

        // Sample row 1 (the spread page): all six roles.
        QModelIndex idx1 = model.index(1, 0);
        CHECK(model.data(idx1, ComicReaderStripModel::PageIndexRole).toInt() == 1, "F8 row1 pageIndex == 1");
        CHECK(approx(model.data(idx1, ComicReaderStripModel::TopRole).toDouble(), 1220.0), "F8 row1 top == 1220");
        CHECK(approx(model.data(idx1, ComicReaderStripModel::DisplayWidthRole).toDouble(), 1000.0), "F8 row1 displayWidth == 1000");
        CHECK(approx(model.data(idx1, ComicReaderStripModel::DisplayHeightRole).toDouble(), 500.0), "F8 row1 displayHeight == 500");
        CHECK(model.data(idx1, ComicReaderStripModel::ReadyRole).toBool() == true, "F8 row1 ready == true (decoded)");
        CHECK(model.data(idx1, ComicReaderStripModel::ErrorCodeRole).toInt() == static_cast<int>(PageError::None),
              "F8 row1 errorCode == None");

        // Sample row 2 (never decoded, then given an error) — a fresh update
        // that keeps decoded=false and sets an error code, confirming ErrorCodeRole
        // reflects it and geometry (still the sticky/estimate size) is untouched.
        PageMeta errored;
        errored.index = 2;
        errored.decoded = false;
        errored.error = PageError::MissingFile;
        model.updatePage(errored);
        QModelIndex idx2 = model.index(2, 0);
        CHECK(model.data(idx2, ComicReaderStripModel::PageIndexRole).toInt() == 2, "F8 row2 pageIndex == 2");
        CHECK(approx(model.data(idx2, ComicReaderStripModel::TopRole).toDouble(), 1740.0), "F8 row2 top unchanged by the error-only update");
        CHECK(approx(model.data(idx2, ComicReaderStripModel::DisplayHeightRole).toDouble(), 1200.0), "F8 row2 displayHeight unchanged (still the estimate)");
        CHECK(model.data(idx2, ComicReaderStripModel::ReadyRole).toBool() == false, "F8 row2 ready == false");
        CHECK(model.data(idx2, ComicReaderStripModel::ErrorCodeRole).toInt() == static_cast<int>(PageError::MissingFile),
              "F8 row2 errorCode == MissingFile");

        // roleNames maps all six roles.
        const auto names = model.roleNames();
        CHECK(names.size() == 6, "F8 roleNames has exactly 6 entries");
        CHECK(names.value(ComicReaderStripModel::PageIndexRole) == "pageIndex", "F8 roleNames PageIndexRole -> pageIndex");
        CHECK(names.value(ComicReaderStripModel::TopRole) == "top", "F8 roleNames TopRole -> top");
        CHECK(names.value(ComicReaderStripModel::DisplayWidthRole) == "displayWidth", "F8 roleNames DisplayWidthRole -> displayWidth");
        CHECK(names.value(ComicReaderStripModel::DisplayHeightRole) == "displayHeight", "F8 roleNames DisplayHeightRole -> displayHeight");
        CHECK(names.value(ComicReaderStripModel::ReadyRole) == "ready", "F8 roleNames ReadyRole -> ready");
        CHECK(names.value(ComicReaderStripModel::ErrorCodeRole) == "errorCode", "F8 roleNames ErrorCodeRole -> errorCode");
    }

    // ── Fixture 9: pageAtCenter on an empty model returns -1, not a phantom page 0 ──
    {
        ComicReaderStripModel model; // never rebuilt: rowCount() == 0
        CHECK(model.rowCount() == 0, "F9 fresh model (never rebuilt) has rowCount == 0");
        CHECK(model.pageAtCenter(0.0, 100.0) == -1, "F9 pageAtCenter on an empty model returns -1");

        model.rebuild(QVector<PageMeta>{}, makeOptions()); // explicit empty rebuild too
        CHECK(model.rowCount() == 0, "F9 rebuild([]) leaves rowCount == 0");
        CHECK(model.pageAtCenter(500.0, 200.0) == -1, "F9 pageAtCenter after an explicit empty rebuild also returns -1");
    }

    // ── Fixture 10: rebuild() rejects a pages[i].index != i feed (hard early-out) ──
    // Debug builds also hit the Q_ASSERT in rebuild(); this harness is built
    // Release (QTFRAMEWORK_BYPASS_LICENSE_CHECK path), where Q_ASSERT compiles
    // out, so this exercises exactly the safety net that matters in that config.
    {
        ComicReaderStripModel model;
        QVector<PageMeta> bad = makeBasePages();
        bad[2].index = 99; // violates pages[i].index == i at i == 2
        model.rebuild(bad, makeOptions());

        CHECK(model.rowCount() == 0, "F10 a pages[i].index != i feed hard-early-outs to an empty model");
        CHECK(approx(model.contentHeight(), 0.0), "F10 contentHeight == 0 for the rejected feed");
        CHECK(model.pageAtCenter(0.0, 100.0) == -1, "F10 pageAtCenter on the rejected feed returns -1 (empty)");

        // A subsequent valid rebuild() recovers normally (the guard doesn't wedge the model).
        model.rebuild(makeBasePages(), makeOptions());
        CHECK(model.rowCount() == 6, "F10 a later valid rebuild() recovers normally");
    }

    // ── Fixture 11: THE 78% LAW (Task 8, overhaul plan 2026-07-28) ───────────
    // Hemanth named this one himself while the Long Strip menu was being
    // designed: "one of the most important features is the potrait width in
    // autoscroll. I hope you're not forgetting about that" — and then confirmed
    // 78 by name. The approved contract is range 40-100, default 78, per series,
    // and LANDSCAPE SPREADS STAY 100% whatever the portrait width says.
    //
    // The default lives in ComicReaderStripModel::Options (78) and in
    // ComicReaderCore (m_portraitWidthPct = 78); the core-side default + the
    // 40/100 clamp are pinned in comicreader_core_harness T14 (this model takes
    // ALREADY-clamped values, which is why the clamp is not re-asserted here).
    // What is pinned HERE is the geometry the number actually produces.
    {
        // The default Options — nobody assigned a width, so this is the shipped answer.
        ComicReaderStripModel::Options fresh;
        CHECK(fresh.portraitWidthPct == 78, "F11 the strip model's DEFAULT portrait width is 78");
        CHECK(fresh.gap == 0, "F11 the strip model's default gap is 0 (Seamless)");
        CHECK(fresh.rotationDegrees == 0, "F11 the strip model's default rotation is 0");

        ComicReaderStripModel::Options opt = fresh;
        opt.viewportWidth = 1000;

        PageMeta portrait;
        portrait.index = 0;
        portrait.sourceSize = QSize(1000, 1500);
        portrait.decoded = true;
        PageMeta spread;
        spread.index = 1;
        spread.sourceSize = QSize(1600, 900);
        spread.decoded = true;
        spread.detectedSpread = true;

        ComicReaderStripModel model;
        model.rebuild({portrait, spread}, opt);

        const auto widthAt = [&](int row) {
            return model.data(model.index(row, 0), ComicReaderStripModel::DisplayWidthRole).toDouble();
        };
        CHECK(std::fabs(widthAt(0) - 780.0) < 0.5, "F11 a portrait page uses 78 percent of the viewport (780 of 1000)");
        CHECK(std::fabs(widthAt(1) - 1000.0) < 0.5, "F11 a landscape SPREAD stays the full viewport width at 78%");

        // ...and it stays full width at every legal portrait width, which is the
        // half of the rule a single-width fixture cannot see.
        model.setLayout(40, 0);
        CHECK(std::fabs(widthAt(0) - 400.0) < 0.5, "F11 a portrait page follows the width down to 40%");
        CHECK(std::fabs(widthAt(1) - 1000.0) < 0.5, "F11 the spread is STILL full width at portrait 40%");
        model.setLayout(100, 0);
        CHECK(std::fabs(widthAt(0) - 1000.0) < 0.5, "F11 a portrait page reaches full width at 100%");
        CHECK(std::fabs(widthAt(1) - 1000.0) < 0.5, "F11 the spread is unchanged at portrait 100%");
    }

    // ── Fixture 12: a width ROUND TRIP restores the column exactly ───────────
    // 78 -> 92 -> 78. The reader's anchor is held by ComicReaderCore::setStripLayout
    // (pinned in comicreader_core_harness T15/T15b, which owns the viewport); what
    // this pins is the GEOMETRY the anchor is computed against — if the column did
    // not come back to the same tops and heights, no anchor arithmetic on top of it
    // could put the reader back where they started.
    {
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), makeOptions());
        model.setLayout(78, 0);

        QVector<double> tops0, heights0;
        for (int i = 0; i < model.rowCount(); ++i) {
            tops0.append(model.pageTop(i));
            heights0.append(model.data(model.index(i, 0), ComicReaderStripModel::DisplayHeightRole).toDouble());
        }
        const double content0 = model.contentHeight();

        model.setLayout(92, 0);
        bool moved = false;
        for (int i = 0; i < model.rowCount(); ++i)
            if (!approx(model.pageTop(i), tops0[i])) { moved = true; break; }
        CHECK(moved, "F12 78 -> 92 actually rescales the column (otherwise the round trip proves nothing)");

        model.setLayout(78, 0);
        bool restored = true;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (!approx(model.pageTop(i), tops0[i])) restored = false;
            if (!approx(model.data(model.index(i, 0), ComicReaderStripModel::DisplayHeightRole).toDouble(),
                        heights0[i]))
                restored = false;
        }
        CHECK(restored, "F12 78 -> 92 -> 78 restores every page's top and height exactly");
        CHECK(approx(model.contentHeight(), content0), "F12 the round trip restores contentHeight exactly");
        CHECK(model.rowCount() == 6, "F12 the round trip is IN PLACE — same rows, no teardown");
    }

    // ── Fixture 13: a quarter turn transposes the BAND, never the verdict ────
    // Task 7 added rotation to the render profile; the provider applies it before
    // it scales, so the page it DELIVERS at 90/270 is the transpose of its source.
    // The strip is the one model-authoritative surface — its delegate heights come
    // from this model, not from the loaded Image — so a band sized off the
    // unrotated source letterboxed every turned page inside a far-too-tall box.
    //
    // The trap this fixture exists to hold shut: transposing the size AND
    // re-running the spread test would call every portrait page a spread at 90
    // degrees and collapse the column to all-full-width. The spread verdict is a
    // statement about the SCAN and must ride through untouched.
    {
        ComicReaderStripModel::Options opt = makeOptions();   // 1000 wide, 80%, gap 20
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), opt);

        const auto wAt = [&](int row) {
            return model.data(model.index(row, 0), ComicReaderStripModel::DisplayWidthRole).toDouble();
        };
        const auto hAt = [&](int row) {
            return model.data(model.index(row, 0), ComicReaderStripModel::DisplayHeightRole).toDouble();
        };
        // page0 portrait 1000x1500 -> dw 800, dh 1200; page1 spread 2000x1000 -> dw 1000, dh 500
        CHECK(approx(hAt(0), 1200.0), "F13 baseline: page0 band is 1200 tall unrotated");
        CHECK(approx(hAt(1), 500.0), "F13 baseline: the spread band is 500 tall unrotated");

        model.setRotation(90);
        // page0's delivered aspect is now 1500x1000 -> dh = 800 * (1000/1500) = 533.33
        CHECK(approx(wAt(0), 800.0), "F13 a quarter turn does NOT change the portrait page's WIDTH (still 80%)");
        CHECK(std::fabs(hAt(0) - (800.0 * 1000.0 / 1500.0)) < 1e-6,
              "F13 a quarter turn transposes the portrait page's band height");
        // ...and the spread is STILL a spread: full width, transposed height.
        CHECK(approx(wAt(1), 1000.0), "F13 the spread is still full width at 90 degrees (the verdict did not move)");
        CHECK(std::fabs(hAt(1) - (1000.0 * 2000.0 / 1000.0)) < 1e-6,
              "F13 the spread's band height transposes too");
        // The inverse of the trap, stated directly: a turned PORTRAIT page must not
        // become full width. If the spread test were re-run on the transposed size
        // (1500x1000 reads as landscape) this is the assertion that would fail.
        CHECK(wAt(0) < wAt(1), "F13 a turned portrait page must NOT be promoted to a spread's full width");

        model.setRotation(180);
        CHECK(approx(hAt(0), 1200.0), "F13 a HALF turn leaves the aspect alone — the band is the unrotated one");
        model.setRotation(270);
        CHECK(std::fabs(hAt(0) - (800.0 * 1000.0 / 1500.0)) < 1e-6, "F13 270 transposes exactly like 90");
        model.setRotation(0);
        CHECK(approx(hAt(0), 1200.0), "F13 back to 0 restores the original band exactly");
        CHECK(approx(model.pageTop(3), 2960.0), "F13 ...and the column's tops come back with it");

        // total for junk / off-grid / negative input, and quarter-turn-identical
        // values are a no-op rather than a needless reflow.
        model.setRotation(-90);
        CHECK(std::fabs(hAt(0) - (800.0 * 1000.0 / 1500.0)) < 1e-6, "F13 -90 folds to 270, not to 0");
        model.setRotation(630);   // 630 == 270 mod 360
        CHECK(std::fabs(hAt(0) - (800.0 * 1000.0 / 1500.0)) < 1e-6, "F13 630 is the same quarter turn as 270");
        model.setRotation(45);    // snaps to 90 -> still an odd quarter turn
        CHECK(std::fabs(hAt(0) - (800.0 * 1000.0 / 1500.0)) < 1e-6, "F13 an off-grid angle snaps to the nearest quarter turn");

        // ── IN PLACE, AND ONLY WHEN THE PICTURE ACTUALLY TURNS ──
        // Two contracts a geometry-only assertion cannot see. First: a reset would tear
        // down every ListView delegate (a visible blink) and zero the bound contentY (a
        // jump to page 1) — for a control that is meant to be previewed live. Second: an
        // angle that names the SAME quarter turn must not reflow the column at all;
        // comparing raw degrees rather than quarter turns looks harmless and produces a
        // needless full-column dataChanged on every equivalent value.
        int dataChanges = 0;
        int resets = 0;
        QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
                         [&dataChanges]() { ++dataChanges; });
        QObject::connect(&model, &QAbstractItemModel::modelReset, &model,
                         [&resets]() { ++resets; });

        model.setRotation(0);
        CHECK(dataChanges == 1, "F13 a real turn emits exactly one dataChanged");
        CHECK(resets == 0, "F13 ...and NEVER a model reset (that would blink the column and snap to page 1)");

        dataChanges = 0;
        model.setRotation(360);    // the same quarter turn as 0, said differently
        CHECK(dataChanges == 0, "F13 an angle naming the SAME quarter turn must not reflow the column");
        model.setRotation(-720);   // ...and so is this
        CHECK(dataChanges == 0, "F13 a negative multiple of a full turn is still the same quarter turn");
        model.setRotation(20);     // snaps to 0 -> still the same quarter turn
        CHECK(dataChanges == 0, "F13 an off-grid angle that snaps to the live quarter turn is a no-op too");
        model.setRotation(180);
        CHECK(dataChanges == 1, "F13 ...but a genuinely different quarter turn does reflow, once");
        CHECK(resets == 0, "F13 still no reset");
    }

    // ── Fixture 14: rotation carried by rebuild(), not only by setRotation ───
    // The render profile SURVIVES an entry crossing, so the next volume has to
    // open already turned. rebuild() takes the angle in Options for exactly that.
    {
        ComicReaderStripModel::Options opt = makeOptions();
        opt.rotationDegrees = 90;
        ComicReaderStripModel model;
        model.rebuild(makeBasePages(), opt);
        const double h0 = model.data(model.index(0, 0), ComicReaderStripModel::DisplayHeightRole).toDouble();
        CHECK(std::fabs(h0 - (800.0 * 1000.0 / 1500.0)) < 1e-6,
              "F14 a rebuild() carrying a quarter turn lays the first column out already turned");
        // ...and setRotation to the SAME angle is then a genuine no-op.
        model.setRotation(90);
        CHECK(std::fabs(model.data(model.index(0, 0), ComicReaderStripModel::DisplayHeightRole).toDouble() - h0) < 1e-6,
              "F14 setRotation to the angle already in force changes nothing");
    }

    if (g_failures == 0) {
        std::puts("COMICREADER_STRIP_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
