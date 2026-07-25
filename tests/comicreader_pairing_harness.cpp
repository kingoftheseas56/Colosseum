// tests/comicreader_pairing_harness.cpp
//
// Comic Reader (Agent 1, plan 2026-07-23) — Task 2 fixtures.
// The pure combinatorial core: canonical double-page pairing + the typed model.
// House CHECK idiom: collect every failure (never abort), print each FAIL, then
// print exactly COMICREADER_PAIRING_OK iff zero failures, else return 1.
//
// Pairing law (Hemanth 2026-07-25): TWO leading singles anchor the book — the cover (index 0) AND
// the first content page (index 1) each ride alone (the first page is the lone recto facing the
// inside cover). Pairing then begins at index 2, so the natural spreads (2+3, 4+5, ...) align and a
// wide spread lands on a pair boundary. A confirmed spread is one full-width unit consuming a parity
// slot; manual override beats detection; phase Shifted nudges parity but never pairs across a spread.

#include "comicreader/ComicReaderPairing.h"
#include "comicreader/ComicReaderTypes.h"

#include <QSize>
#include <QVariantMap>
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

static PageMeta portrait(int i) {
    PageMeta m;
    m.index = i;
    m.sourceSize = QSize(800, 1200);
    m.detectedSpread = false;
    return m;
}

static PageMeta landscape(int i) {
    PageMeta m;
    m.index = i;
    m.sourceSize = QSize(1600, 1200);
    m.detectedSpread = spreadRatioExceeded(m.sourceSize); // true
    return m;
}

// A unit equals the expected {right,left,spread,coverAlone} tuple.
static bool unitEq(const PairUnit& u, int right, int left, bool spread, bool cover) {
    return u.rightIndex == right && u.leftIndex == left
        && u.spread == spread && u.coverAlone == cover;
}

// Every page in [0,pageCount) maps to a unit index whose unit actually contains it.
static bool unitForPageConsistent(const QVector<PairUnit>& units, int pageCount) {
    for (int pg = 0; pg < pageCount; ++pg) {
        const int k = unitForPage(units, pg);
        if (k < 0 || k >= units.size()) return false;
        const PairUnit& u = units[k];
        if (u.rightIndex != pg && u.leftIndex != pg) return false;
    }
    return true;
}

int main() {
    // ── Fixture 1: 5 normal portrait pages → [cover 0][single 1][pair 2+3][single 4] ──
    // Two leading singles (cover + first page), then pairing from index 2; page 4 has no partner.
    {
        const QVector<PageMeta> p = {portrait(0), portrait(1), portrait(2),
                                     portrait(3), portrait(4)};
        const auto u = buildUnits(p, CouplingPhase::Normal);
        CHECK(u.size() == 4, "F1 four units");
        CHECK(unitEq(u[0], 0, -1, false, true), "F1 cover rides alone");
        CHECK(unitEq(u[1], 1, -1, false, false), "F1 first page rides alone");
        CHECK(unitEq(u[2], 2, 3, false, false), "F1 pair 2+3");
        CHECK(unitEq(u[3], 4, -1, false, false), "F1 trailing single 4");
        CHECK(unitForPageConsistent(u, p.size()), "F1 unitForPage every page");
    }

    // ── Fixture 2: spread at page 2 → [cover 0][single 1][spread 2][pair 3+4] ──
    // The two leading singles + the spread's parity slot land pages 3 and 4 as a clean pair — NO
    // orphan singles around the spread (this is exactly the misalignment Hemanth's rule fixes).
    {
        const QVector<PageMeta> p = {portrait(0), portrait(1), landscape(2),
                                     portrait(3), portrait(4)};
        const auto u = buildUnits(p, CouplingPhase::Normal);
        CHECK(u.size() == 4, "F2 four units");
        CHECK(unitEq(u[0], 0, -1, false, true), "F2 cover");
        CHECK(unitEq(u[1], 1, -1, false, false), "F2 first page rides alone");
        CHECK(unitEq(u[2], 2, -1, true, false), "F2 spread 2");
        CHECK(unitEq(u[3], 3, 4, false, false), "F2 pair 3+4 (no orphans around the spread)");
        CHECK(unitForPageConsistent(u, p.size()), "F2 unitForPage every page");
    }

    // ── Fixture 2b: spread at page 1 → [cover 0][spread 1][pair 2+3][single 4] ─
    // The mirror of F2: here the spread's slot DOES re-align parity so pages 2
    // and 3 pair. The asymmetry with F2 (spread at 2 → singles) is exactly the
    // proven lineage behaviour — only the spread ever shifts parity.
    {
        const QVector<PageMeta> p = {portrait(0), landscape(1), portrait(2),
                                     portrait(3), portrait(4)};
        const auto u = buildUnits(p, CouplingPhase::Normal);
        CHECK(u.size() == 4, "F2b four units");
        CHECK(unitEq(u[0], 0, -1, false, true), "F2b cover");
        CHECK(unitEq(u[1], 1, -1, true, false), "F2b spread 1");
        CHECK(unitEq(u[2], 2, 3, false, false), "F2b pair 2+3");
        CHECK(unitEq(u[3], 4, -1, false, false), "F2b single 4");
        CHECK(unitForPageConsistent(u, p.size()), "F2b unitForPage every page");
    }

    // ── Fixture 3: page 0 IS a confirmed spread → [spread 0][single 1][single 2] ──
    // The cover-spread + the lone first page are the two leading singles; page 2 has no partner.
    {
        const QVector<PageMeta> p = {landscape(0), portrait(1), portrait(2)};
        const auto u = buildUnits(p, CouplingPhase::Normal);
        CHECK(u.size() == 3, "F3 three units");
        CHECK(unitEq(u[0], 0, -1, true, false), "F3 spread 0 (not coverAlone)");
        CHECK(unitEq(u[1], 1, -1, false, false), "F3 first page rides alone");
        CHECK(unitEq(u[2], 2, -1, false, false), "F3 trailing single 2");
        CHECK(unitForPageConsistent(u, p.size()), "F3 unitForPage every page");
    }

    // ── Fixture 4: override page 3 forced-spread (page 3 is portrait) ─────────
    // → [cover 0][single 1][single 2][spread 3][single 4]; the two leading singles + the forced
    //   spread at an odd index leave page 2 orphaned (its partner is the spread).
    {
        QVector<PageMeta> p = {portrait(0), portrait(1), portrait(2),
                               portrait(3), portrait(4)};
        p[3].spreadOverride = true; // force spread despite portrait geometry
        const auto u = buildUnits(p, CouplingPhase::Normal);
        CHECK(u.size() == 5, "F4 five units");
        CHECK(unitEq(u[0], 0, -1, false, true), "F4 cover");
        CHECK(unitEq(u[1], 1, -1, false, false), "F4 first page rides alone");
        CHECK(unitEq(u[2], 2, -1, false, false), "F4 single 2 (partner is spread)");
        CHECK(unitEq(u[3], 3, -1, true, false), "F4 spread 3 by override");
        CHECK(unitEq(u[4], 4, -1, false, false), "F4 single 4");
        CHECK(isSpread(p[3]) == true, "F4 override beats portrait detection");
        CHECK(unitForPageConsistent(u, p.size()), "F4 unitForPage every page");
    }

    // ── Fixture 5: override a landscape page forced-normal → ordinary pairing ─
    // page 2 is landscape (detected spread) but overridden normal → it pairs with page 3.
    {
        QVector<PageMeta> p = {portrait(0), portrait(1), landscape(2),
                               portrait(3), portrait(4)};
        p[2].spreadOverride = false; // force normal despite landscape geometry
        CHECK(isSpread(p[2]) == false, "F5 override false beats landscape detection");
        const auto u = buildUnits(p, CouplingPhase::Normal);
        CHECK(u.size() == 4, "F5 four units");
        CHECK(unitEq(u[0], 0, -1, false, true), "F5 cover");
        CHECK(unitEq(u[1], 1, -1, false, false), "F5 first page rides alone");
        CHECK(unitEq(u[2], 2, 3, false, false), "F5 pair 2+3 (page 2 rejoined as normal)");
        CHECK(unitEq(u[3], 4, -1, false, false), "F5 trailing single 4");
        CHECK(unitForPageConsistent(u, p.size()), "F5 unitForPage every page");
    }

    // ── Fixture 6: CouplingPhase::Shifted on 6 normal pages ──────────────────
    // Shifted nudges parity by one ON TOP of the two leading singles:
    // [cover 0][single 1][single 2][pair 3+4][single 5]
    {
        const QVector<PageMeta> p = {portrait(0), portrait(1), portrait(2),
                                     portrait(3), portrait(4), portrait(5)};
        const auto u = buildUnits(p, CouplingPhase::Shifted);
        CHECK(u.size() == 5, "F6 five units");
        CHECK(unitEq(u[0], 0, -1, false, true), "F6 cover");
        CHECK(unitEq(u[1], 1, -1, false, false), "F6 first page rides alone");
        CHECK(unitEq(u[2], 2, -1, false, false), "F6 single 2 (nudge lead)");
        CHECK(unitEq(u[3], 3, 4, false, false), "F6 pair 3+4");
        CHECK(unitEq(u[4], 5, -1, false, false), "F6 trailing single 5");
        CHECK(unitForPageConsistent(u, p.size()), "F6 unitForPage every page");
    }

    // ── Edge fixtures (Task 2 hardening — lock the load-bearing corners) ──────
    // E1 empty entry: no pages → no units. buildUnits must not fabricate a cover.
    {
        const auto u = buildUnits(QVector<PageMeta>{}, CouplingPhase::Normal);
        CHECK(u.isEmpty(), "E1 empty pages → empty units");
    }
    // E2 single-page entry: one portrait → lone cover; one landscape → lone spread.
    {
        const QVector<PageMeta> portraitOnly = {portrait(0)};
        const auto up = buildUnits(portraitOnly, CouplingPhase::Normal);
        CHECK(up.size() == 1, "E2 one portrait → one unit");
        CHECK(unitEq(up[0], 0, -1, false, true), "E2 portrait cover rides alone");
        CHECK(unitForPageConsistent(up, portraitOnly.size()), "E2 portrait unitForPage");

        const QVector<PageMeta> landscapeOnly = {landscape(0)};
        const auto ul = buildUnits(landscapeOnly, CouplingPhase::Normal);
        CHECK(ul.size() == 1, "E2 one landscape → one unit");
        CHECK(unitEq(ul[0], 0, -1, true, false), "E2 landscape cover is a spread unit");
        CHECK(unitForPageConsistent(ul, landscapeOnly.size()), "E2 landscape unitForPage");
    }
    // E3 all spreads: every page is its own full-width unit; nothing ever pairs.
    {
        const QVector<PageMeta> p = {landscape(0), landscape(1), landscape(2)};
        const auto u = buildUnits(p, CouplingPhase::Normal);
        CHECK(u.size() == 3, "E3 three spread units");
        CHECK(unitEq(u[0], 0, -1, true, false), "E3 spread 0");
        CHECK(unitEq(u[1], 1, -1, true, false), "E3 spread 1");
        CHECK(unitEq(u[2], 2, -1, true, false), "E3 spread 2");
        CHECK(unitForPageConsistent(u, p.size()), "E3 unitForPage every page");
    }

    // ── unitForPage clamping ─────────────────────────────────────────────────
    {
        const QVector<PageMeta> p = {portrait(0), portrait(1), portrait(2),
                                     portrait(3), portrait(4)};
        const auto u = buildUnits(p, CouplingPhase::Normal);
        CHECK(unitForPage(u, -7) == 0, "clamp below → first unit");
        CHECK(unitForPage(u, 9999) == u.size() - 1, "clamp above → last unit");
        CHECK(unitForPage(QVector<PairUnit>{}, 3) == 0, "empty units → 0");
    }

    // ── isSpread: detection, override-true, override-false ───────────────────
    {
        CHECK(spreadRatioExceeded(QSize(1300, 1200)) == true, "ratio 1.083 is a spread");
        // 1.08*1200 rounds to exactly 1296.0 in IEEE-754 double on the x64 target
        // (the product's error falls ~0.375 ULP below 1296, rounding down), so the
        // >= holds at the boundary. A future change to the 1.08 constant could move
        // this — re-verify the boundary if it changes.
        CHECK(spreadRatioExceeded(QSize(1296, 1200)) == true, "ratio exactly 1.08 is a spread");
        CHECK(spreadRatioExceeded(QSize(1295, 1200)) == false, "ratio just under 1.08 is not");
        CHECK(spreadRatioExceeded(QSize(1200, 1200)) == false, "square is not a spread");
        CHECK(spreadRatioExceeded(QSize(800, 1200)) == false, "portrait is not a spread");
        CHECK(spreadRatioExceeded(QSize(1600, 0)) == false, "zero height is not a spread");

        PageMeta land;
        land.sourceSize = QSize(1600, 1200);
        land.detectedSpread = spreadRatioExceeded(land.sourceSize);
        CHECK(isSpread(land) == true, "detected landscape is a spread");

        PageMeta forcedSpread;
        forcedSpread.sourceSize = QSize(800, 1200);
        forcedSpread.detectedSpread = false;
        forcedSpread.spreadOverride = true;
        CHECK(isSpread(forcedSpread) == true, "override true forces spread on portrait");

        PageMeta forcedNormal;
        forcedNormal.sourceSize = QSize(1600, 1200);
        forcedNormal.detectedSpread = true;
        forcedNormal.spreadOverride = false;
        CHECK(isSpread(forcedNormal) == false, "override false forces normal on landscape");
    }

    // ── PageError stable enum ↔ string (later tasks depend on these codes) ────
    {
        CHECK(pageErrorToString(PageError::None) == QStringLiteral("none"), "code none");
        CHECK(pageErrorToString(PageError::MissingFile) == QStringLiteral("missing_file"), "code missing_file");
        CHECK(pageErrorToString(PageError::DecodeFailed) == QStringLiteral("decode_failed"), "code decode_failed");
        CHECK(pageErrorToString(PageError::UnsupportedImage) == QStringLiteral("unsupported_image"), "code unsupported_image");
        CHECK(pageErrorFromString(QStringLiteral("missing_file")) == PageError::MissingFile, "parse missing_file");
        CHECK(pageErrorFromString(QStringLiteral("unsupported_image")) == PageError::UnsupportedImage, "parse unsupported_image");
        CHECK(pageErrorFromString(QStringLiteral("who_knows")) == PageError::None, "unknown code → None");
    }

    // ── PageMeta QVariant round-trip: override set, and override absent ───────
    {
        PageMeta a;
        a.index = 4;
        a.localPath = QStringLiteral("/tmp/pages/004.png");
        a.sourceSize = QSize(1280, 1920);
        a.decoded = true;
        a.detectedSpread = false;
        a.spreadOverride = true;
        a.error = PageError::DecodeFailed;
        const PageMeta ra = PageMeta::fromVariantMap(a.toVariantMap());
        CHECK(ra.index == 4, "PageMeta index survives");
        CHECK(ra.localPath == a.localPath, "PageMeta localPath survives");
        CHECK(ra.sourceSize == a.sourceSize, "PageMeta sourceSize survives");
        CHECK(ra.decoded == true, "PageMeta decoded survives");
        CHECK(ra.detectedSpread == false, "PageMeta detectedSpread survives");
        CHECK(ra.spreadOverride.has_value() && *ra.spreadOverride == true, "PageMeta override(set) survives");
        CHECK(ra.error == PageError::DecodeFailed, "PageMeta error survives");

        PageMeta b = portrait(2); // no override
        const PageMeta rb = PageMeta::fromVariantMap(b.toVariantMap());
        CHECK(!rb.spreadOverride.has_value(), "PageMeta override(absent) stays absent");
    }

    // ── PairUnit QVariant round-trip ─────────────────────────────────────────
    {
        PairUnit u;
        u.rightIndex = 3;
        u.leftIndex = 4;
        u.spread = false;
        u.coverAlone = false;
        const PairUnit r = PairUnit::fromVariantMap(u.toVariantMap());
        CHECK(unitEq(r, 3, 4, false, false), "PairUnit round-trip");

        PairUnit spread;
        spread.rightIndex = 7;
        spread.spread = true;
        const PairUnit rs = PairUnit::fromVariantMap(spread.toVariantMap());
        CHECK(unitEq(rs, 7, -1, true, false), "PairUnit spread round-trip");
    }

    if (g_failures == 0) {
        std::puts("COMICREADER_PAIRING_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
