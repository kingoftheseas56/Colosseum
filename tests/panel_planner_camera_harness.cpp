// tests/panel_planner_camera_harness.cpp
//
// Contract gate for the guided-v1 timing profile + restrained camera framing
// added to guided::PanelPlanner (Task 5). Pure and deterministic: inline
// CanvasSpec + Detection fixtures, no I/O, no network, no fixture files.
//
// Verifies:
//   * PlannerProfile::guidedV1() identity + the two closed-form timing curves
//     (panelHold / transition) at their interesting boundaries;
//   * restrained framing through plan(): a TALL panel yields exactly one
//     internal stop, a text-dense panel is hard-capped at two internal stops;
//   * every emitted camera rect stays inside the [0,1]x[0,1] canvas.
#include "guided/GuidedTypes.h"
#include "guided/PanelPlanner.h"

#include <QString>
#include <QVector>

#include <cmath>
#include <cstdio>

using namespace guided;

#define CHECK(x, m) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; } } while (0)

namespace {

bool fuzzy(double a, double b) { return std::abs(a - b) < 1e-9; }

int countKind(const GuidedPath& path, StepKind kind) {
    int n = 0;
    for (const PathStep& s : path.steps)
        if (s.kind == kind)
            ++n;
    return n;
}

Detection panel(int id, double x, double y, double w, double h, double conf) {
    Detection d;
    d.id = id;
    d.kind = DetectionKind::Panel;
    d.box = NormalizedRect{x, y, w, h};
    d.confidence = conf;
    return d;
}

Detection text(int id, double x, double y, double w, double h, double conf) {
    Detection d;
    d.id = id;
    d.kind = DetectionKind::Text;
    d.box = NormalizedRect{x, y, w, h};
    d.confidence = conf;
    return d;
}

int run() {
    const PanelPlanner planner;
    const PlannerProfile p = PanelPlanner::guidedV1();

    // --- profile identity -----------------------------------------------------
    CHECK(p.version == QLatin1String("guided-v1"), "guidedV1 version");
    CHECK(fuzzy(p.overviewHold, 0.8), "guidedV1 overviewHold == 0.8");

    // --- panelHold: interior point + both clamps ------------------------------
    CHECK(fuzzy(p.panelHold(0.25, 2, 0.10),
                1.5 + 1.2 * std::sqrt(0.25) + 0.65 * 2 + 2.0 * 0.10),
          "panelHold(0.25,2,0.10) == 3.6");
    // NOTE: with textWeight capped at min(4.5,...), the reachable maximum is
    // 1.5 + 1.2 + 4.5 = 7.2; the 8.0 upper clamp is headroom never hit by the
    // frozen weights. panelHold(1.0,100,1.0) therefore lands at 7.2, not 8.0.
    CHECK(fuzzy(p.panelHold(1.0, 100, 1.0), 7.2), "panelHold saturates at 7.2");
    CHECK(fuzzy(p.panelHold(0.0, 0, 0.0), 1.5), "panelHold(0,0,0) lower clamp 1.5");

    // --- transition: both clamps ----------------------------------------------
    CHECK(fuzzy(p.transition(1.0, 1.0), 0.8), "transition(1.0,1.0) upper clamp 0.8");
    CHECK(fuzzy(p.transition(0.0, 1.0), 0.35), "transition(0.0,1.0) lower clamp 0.35");

    // --- TALL panel: exactly ONE internal stop --------------------------------
    // aspect = 0.15/0.8 = 0.1875 <= 1/2.2 -> verticalTraversal(2) = top + 1 stop.
    {
        CanvasSpec canvas;
        canvas.fingerprint = QStringLiteral("sha256:tall");
        canvas.direction = ReadingDirection::Ltr;
        QVector<Detection> dets{ panel(0, 0.40, 0.10, 0.15, 0.80, 0.90) };
        const GuidedPath path = planner.plan(canvas, dets);
        CHECK(path.outcome == PlanOutcome::Trusted, "tall panel is trusted");
        CHECK(countKind(path, StepKind::InternalStop) == 1,
              "tall panel yields exactly one internal stop");
        CHECK(countKind(path, StepKind::Panel) == 1, "tall panel yields one panel step");
    }

    // --- text-dense panel: internal stops HARD-CAPPED at 2 --------------------
    // Normal aspect (1.0) + >=3 well-separated text clusters -> cappedTextTraversal.
    {
        CanvasSpec canvas;
        canvas.fingerprint = QStringLiteral("sha256:textdense");
        canvas.direction = ReadingDirection::Ltr;
        QVector<Detection> dets{
            panel(0, 0.10, 0.10, 0.80, 0.80, 0.90),
            text(1, 0.15, 0.15, 0.10, 0.05, 0.80),
            text(2, 0.60, 0.20, 0.10, 0.05, 0.80),
            text(3, 0.30, 0.60, 0.10, 0.05, 0.80),
            text(4, 0.65, 0.70, 0.10, 0.05, 0.80),
        };
        const GuidedPath path = planner.plan(canvas, dets);
        CHECK(path.outcome == PlanOutcome::Trusted, "text-dense panel is trusted");
        CHECK(countKind(path, StepKind::InternalStop) <= 2,
              "text-dense panel internal stops hard-capped at 2");
        CHECK(countKind(path, StepKind::Panel) == 1, "text-dense panel yields one panel step");
    }

    // --- SPREAD-style wide canvas: every camera rect stays inside the canvas ---
    {
        CanvasSpec canvas;
        canvas.fingerprint = QStringLiteral("sha256:spread");
        canvas.kind = CanvasKind::Spread;
        canvas.direction = ReadingDirection::Ltr;
        QVector<Detection> dets{
            panel(0, 0.05, 0.05, 0.70, 0.25, 0.95),   // wide -> horizontalTraversal
            panel(1, 0.05, 0.40, 0.40, 0.40, 0.90),
            panel(2, 0.55, 0.40, 0.40, 0.40, 0.88),   // reaches x+w = 0.95
        };
        const GuidedPath path = planner.plan(canvas, dets);
        CHECK(path.outcome == PlanOutcome::Trusted, "spread is trusted");
        constexpr double eps = 1e-9;
        for (const PathStep& s : path.steps) {
            const NormalizedRect& r = s.camera;
            CHECK(r.x >= -eps, "spread camera x >= 0");
            CHECK(r.y >= -eps, "spread camera y >= 0");
            CHECK(r.x + r.width <= 1.0 + eps, "spread camera x+width <= 1");
            CHECK(r.y + r.height <= 1.0 + eps, "spread camera y+height <= 1");
        }
    }

    std::puts("PANEL_PLANNER_CAMERA_OK");
    return 0;
}

} // namespace

int main() { return run(); }
