// tests/comicreader_coupling_harness.cpp
//
// Comic Reader (Agent 1, plan 2026-07-23) — Task 5 fixtures.
// Auto-coupling probe: pure edge-continuity scoring + phase decision. House
// CHECK idiom: collect every failure (never abort), print each FAIL, then
// print exactly COMICREADER_COUPLING_OK iff zero failures, else return 1.

#include "comicreader/ComicReaderCoupling.h"
#include "comicreader/ComicReaderTypes.h"

#include <QColor>
#include <QImage>
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

static bool near(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) <= eps;
}

// A vertical gradient image: black (top) -> white (bottom), full width solid
// per row so every column (in particular the scaled edge column) carries the
// same luminance for that row.
static QImage verticalGradient(int w, int h, bool reversed) {
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        double t = h > 1 ? static_cast<double>(y) / (h - 1) : 0.0;
        if (reversed)
            t = 1.0 - t;
        const int v = static_cast<int>(std::lround(t * 255.0));
        for (int x = 0; x < w; ++x)
            img.setPixelColor(x, y, QColor(v, v, v));
    }
    return img;
}

// A vertically-uniform, HORIZONTALLY split image: bright (255) on one half,
// dark (0) on the other, with a generous margin either side of the midline so
// nearest-neighbor downscaling to kSampleW=8 unambiguously lands each target
// edge column (x=0 and x=7) inside the intended half. Unlike verticalGradient
// (uniform across x), this fixture is sensitive to WHICH column is sampled —
// it pins that edgeContinuityCost reads left's RIGHT-most column vs right's
// LEFT-most column, not some other column or a transposed pairing.
static QImage halfBright(int w, int h, bool brightOnLeftHalf) {
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool inLeftHalf = x < w / 2;
            const bool bright = (inLeftHalf == brightOnLeftHalf);
            const int v = bright ? 255 : 0;
            img.setPixelColor(x, y, QColor(v, v, v));
        }
    }
    return img;
}

int main() {
    // ── 1. Seamless continuity is cheap ───────────────────────────────────────
    {
        // left's right edge and right's left edge carry the SAME gradient, so
        // after scaling to the 8x96 sample the touching columns match closely.
        const QImage left = verticalGradient(64, 96, false);
        const QImage right = verticalGradient(64, 96, false);
        const double cost = edgeContinuityCost(left, right);
        CHECK(cost < 0.08, "seamless gradient cost < 0.08");
        std::fprintf(stderr, "measured seamless cost = %f\n", cost);
    }

    // ── 2. Discontinuity is costly ────────────────────────────────────────────
    {
        const QImage left = verticalGradient(64, 96, false);
        const QImage right = verticalGradient(64, 96, true); // reversed
        const double cost = edgeContinuityCost(left, right);
        CHECK(cost > 0.3, "reversed gradient cost > 0.3");
        std::fprintf(stderr, "measured discontinuity cost = %f\n", cost);
    }

    // ── 3. Null image → cost 1.0 ──────────────────────────────────────────────
    {
        const QImage left = verticalGradient(64, 96, false);
        const QImage null;
        CHECK(near(edgeContinuityCost(null, left), 1.0), "null left -> 1.0");
        CHECK(near(edgeContinuityCost(left, null), 1.0), "null right -> 1.0");
        CHECK(near(edgeContinuityCost(null, null), 1.0), "null both -> 1.0");
    }

    // ── 4. chooseCouplingPhase picks Shifted when clearly lower ───────────────
    {
        const QVector<double> normal = {0.5, 0.5};
        const QVector<double> shifted = {0.05, 0.05};
        const CouplingVerdict v = chooseCouplingPhase(normal, shifted);
        CHECK(v.phase == CouplingPhase::Shifted, "clearly-lower shifted -> Shifted");
        const double expected = std::abs(1.0 - 0.1) / 1.1;
        CHECK(near(v.confidence, expected, 1e-9), "confidence matches |1.0-0.1|/1.1");
    }

    // ── 5. Tie → Normal, confidence 0 ─────────────────────────────────────────
    {
        const QVector<double> normal = {0.3, 0.3};
        const QVector<double> shifted = {0.3, 0.3};
        const CouplingVerdict v = chooseCouplingPhase(normal, shifted);
        CHECK(v.phase == CouplingPhase::Normal, "tie -> Normal");
        CHECK(near(v.confidence, 0.0, 1e-9), "tie -> confidence 0");
    }

    // ── 6. Below-floor difference → Normal despite shifted nominally lower ───
    {
        const QVector<double> normal = {0.50};
        const QVector<double> shifted = {0.46};
        const CouplingVerdict v = chooseCouplingPhase(normal, shifted);
        const double expected = std::abs(0.50 - 0.46) / 0.96;
        CHECK(near(expected, 0.0416666667, 1e-6), "sanity: expected confidence ~0.0417");
        CHECK(v.confidence < 0.12, "below-floor confidence < 0.12");
        CHECK(v.phase == CouplingPhase::Normal, "below-floor -> Normal even though shifted lower");
    }

    // ── 7. confidence clamped to [0,1], exact formula for a known case ───────
    {
        // ns=0, ss=0 would be the empty-vector case (handled in 8); here use a
        // large asymmetric pair to check the formula directly and clamp bound.
        const QVector<double> normal = {10.0};
        const QVector<double> shifted = {0.0};
        const CouplingVerdict v = chooseCouplingPhase(normal, shifted);
        const double expected = std::abs(10.0 - 0.0) / 10.0; // = 1.0
        CHECK(near(v.confidence, expected, 1e-9), "confidence exact for known case");
        CHECK(v.confidence >= 0.0 && v.confidence <= 1.0, "confidence clamped to [0,1]");
        CHECK(v.phase == CouplingPhase::Shifted, "ss<ns and confidence>=0.12 -> Shifted");
    }

    // ── 8. Empty inputs → {Normal, 0.0} ───────────────────────────────────────
    {
        const CouplingVerdict v = chooseCouplingPhase(QVector<double>{}, QVector<double>{});
        CHECK(v.phase == CouplingPhase::Normal, "empty inputs -> Normal");
        CHECK(near(v.confidence, 0.0, 1e-9), "empty inputs -> confidence 0");
    }

    // ── 9. Horizontally-distinct edge fixture: matching touching edges → LOW ──
    // left is bright ONLY in its right-most columns, right is bright ONLY in
    // its left-most columns. The correct touching columns (left x=7, right
    // x=0) are BOTH bright → seamless → low cost. A bug that read the WRONG
    // column off either side (e.g. left's x=0, which is dark here) would hit a
    // bright/dark mismatch and blow this past the low-cost bound.
    {
        const QImage left = halfBright(64, 96, /*brightOnLeftHalf=*/false);  // bright on RIGHT half
        const QImage right = halfBright(64, 96, /*brightOnLeftHalf=*/true); // bright on LEFT half
        const double cost = edgeContinuityCost(left, right);
        CHECK(cost < 0.05, "matching touching edges -> low cost");
        std::fprintf(stderr, "measured matching-edge cost = %f\n", cost);
    }

    // ── 10. Transposition-catching variant: correct edges DON'T match → HIGH ─
    // Both images are bright-on-LEFT-half (same construction). Read CORRECTLY,
    // left's touching column is x=7 (dark, since left's bright half is on the
    // LEFT) and right's touching column is x=0 (bright, since right's bright
    // half is on the LEFT too) → mismatch → high cost. A "reads left's x=0
    // instead of x=7" bug would instead compare left's x=0 (bright) to right's
    // x=0 (bright) and wrongly report low cost — this fixture catches exactly
    // that column-transposition bug.
    {
        const QImage left = halfBright(64, 96, /*brightOnLeftHalf=*/true);
        const QImage right = halfBright(64, 96, /*brightOnLeftHalf=*/true);
        const double cost = edgeContinuityCost(left, right);
        CHECK(cost > 0.9, "correct edges mismatch (transposition-catching) -> high cost");
        std::fprintf(stderr, "measured transposition-catching cost = %f\n", cost);
    }

    // ── 11. Unequal-length coupling fixture: means diverge from sums ─────────
    // normal has 4 samples (mean 0.5), shifted has 3 (mean 0.4). A sum-based
    // impl would compute ns=2.0, ss=1.2, confidence=0.8/3.2=0.25 -> wrongly
    // Shifted. The lineage-faithful MEAN aggregation gives confidence
    // 0.1/0.9 = 0.1111... < 0.12 -> Normal, matching QTGroundWork exactly.
    {
        const QVector<double> normal = {0.5, 0.5, 0.5, 0.5};
        const QVector<double> shifted = {0.4, 0.4, 0.4};
        const CouplingVerdict v = chooseCouplingPhase(normal, shifted);
        const double expected = std::abs(0.5 - 0.4) / (0.5 + 0.4);
        CHECK(near(expected, 0.1111111111, 1e-6), "sanity: expected confidence ~0.1111");
        CHECK(v.confidence < 0.12, "unequal-length mean confidence below floor");
        CHECK(v.phase == CouplingPhase::Normal, "unequal-length -> Normal (mean, not sum)");
        CHECK(near(v.confidence, expected, 1e-9), "unequal-length confidence matches mean formula");
    }

    // ── 12. One-empty fixture → {Normal, 0.0} ────────────────────────────────
    // Only one phase had decodable samples; the reference never decides from a
    // one-sided probe (it bails to Normal/retry), so this must not fall
    // through to comparing a real mean against an implicit zero.
    {
        const QVector<double> normal = {0.5, 0.5};
        const QVector<double> shifted = {};
        const CouplingVerdict v = chooseCouplingPhase(normal, shifted);
        CHECK(v.phase == CouplingPhase::Normal, "one-empty (shifted) -> Normal");
        CHECK(near(v.confidence, 0.0, 1e-9), "one-empty (shifted) -> confidence 0");

        const CouplingVerdict v2 = chooseCouplingPhase(QVector<double>{}, normal);
        CHECK(v2.phase == CouplingPhase::Normal, "one-empty (normal) -> Normal");
        CHECK(near(v2.confidence, 0.0, 1e-9), "one-empty (normal) -> confidence 0");
    }

    if (g_failures == 0) {
        std::puts("COMICREADER_COUPLING_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
