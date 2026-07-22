// native/guided/PanelPlanner.cpp
#include "guided/PanelPlanner.h"

#include <algorithm>

namespace guided {
namespace {

// Intersection-over-union of two normalized boxes. 0 when disjoint or degenerate.
double iou(const NormalizedRect& a, const NormalizedRect& b) {
    const double ax2 = a.x + a.width;
    const double ay2 = a.y + a.height;
    const double bx2 = b.x + b.width;
    const double by2 = b.y + b.height;
    const double ix1 = std::max(a.x, b.x);
    const double iy1 = std::max(a.y, b.y);
    const double ix2 = std::min(ax2, bx2);
    const double iy2 = std::min(ay2, by2);
    const double iw = ix2 - ix1;
    const double ih = iy2 - iy1;
    if (iw <= 0.0 || ih <= 0.0)
        return 0.0;
    const double inter = iw * ih;
    const double uni = a.area() + b.area() - inter;
    if (uni <= 0.0)
        return 0.0;
    return inter / uni;
}

// Step 1 — NMS / de-dup over the raw set: drop invalid/degenerate boxes, sort by
// confidence DESC (id ASC tie-break for determinism), then greedily drop any box
// whose IoU with an already-kept box is >= 0.60 (the higher-confidence duplicate
// is already kept).
QVector<Detection> suppressInvalidAndDuplicates(const QVector<Detection>& raw) {
    QVector<Detection> survivors;
    survivors.reserve(raw.size());
    for (const Detection& d : raw) {
        if (d.box.isValid() && d.box.area() > 0.0)
            survivors.append(d);
    }
    std::sort(survivors.begin(), survivors.end(),
              [](const Detection& a, const Detection& b) {
                  if (a.confidence != b.confidence)
                      return a.confidence > b.confidence;   // DESC
                  return a.id < b.id;                       // ASC — deterministic
              });
    QVector<Detection> kept;
    kept.reserve(survivors.size());
    for (const Detection& cand : survivors) {
        bool duplicate = false;
        for (const Detection& k : kept) {
            if (iou(cand.box, k.box) >= 0.60) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            kept.append(cand);
    }
    return kept;
}

// Step 2 — panel steps only: Panel kind, confidence >= 0.25. Text is ignored for
// ordering.
QVector<Detection> keepPanels(const QVector<Detection>& accepted) {
    QVector<Detection> panels;
    panels.reserve(accepted.size());
    for (const Detection& d : accepted) {
        if (d.kind == DetectionKind::Panel && d.confidence >= 0.25)
            panels.append(d);
    }
    return panels;
}

// Step 4a — TRUE if any pair of kept panels materially overlaps (IoU > 0.15).
// Clean tiling leaves panels disjoint (IoU 0); survivors that still overlap mean
// the layout is not a clean tiling.
bool hasUnresolvedOverlap(const QVector<Detection>& panels) {
    for (int i = 0; i < panels.size(); ++i) {
        for (int j = i + 1; j < panels.size(); ++j) {
            if (iou(panels[i].box, panels[j].box) > 0.15)
                return true;
        }
    }
    return false;
}

// Step 4b — FALSE if total panel area > 1.80 (heavy overlap => bad detection) or
// (with more than one panel) a single panel covers > 0.98 of the canvas.
bool coveragePlausible(const QVector<Detection>& panels) {
    double total = 0.0;
    for (const Detection& p : panels)
        total += p.box.area();
    if (total > 1.80)
        return false;
    if (panels.size() > 1) {
        for (const Detection& p : panels) {
            if (p.box.area() > 0.98)
                return false;
        }
    }
    return true;
}

// Step 5 — group panels into reading rows. Sort by y (id tie-break), then walk:
// start a new row when a panel's vertical center drops more than 0.18 below the
// current row's reference center, or its vertical span no longer overlaps the
// row. Rows come out ordered top (smallest y) to bottom.
QVector<QVector<Detection>> partitionRows(const QVector<Detection>& panels) {
    constexpr double kRowTolerance = 0.18;

    QVector<Detection> sorted = panels;
    std::sort(sorted.begin(), sorted.end(),
              [](const Detection& a, const Detection& b) {
                  if (a.box.y != b.box.y)
                      return a.box.y < b.box.y;
                  return a.id < b.id;
              });

    QVector<QVector<Detection>> rows;
    double refCenterY = 0.0;   // center of the row's first (reference) panel
    double rowTop = 0.0;       // union vertical span of the current row
    double rowBottom = 0.0;
    for (const Detection& p : sorted) {
        const double centerY = p.box.center().y;
        const double top = p.box.y;
        const double bottom = p.box.y + p.box.height;

        bool startNew = rows.isEmpty();
        if (!rows.isEmpty()) {
            const bool tooFarBelow = (centerY - refCenterY) > kRowTolerance;
            const bool noOverlap = (top >= rowBottom) || (bottom <= rowTop);
            if (tooFarBelow || noOverlap)
                startNew = true;
        }

        if (startNew) {
            rows.append(QVector<Detection>{p});
            refCenterY = centerY;
            rowTop = top;
            rowBottom = bottom;
        } else {
            rows.last().append(p);
            rowTop = std::min(rowTop, top);
            rowBottom = std::max(rowBottom, bottom);
        }
    }
    return rows;
}

// Step 6 — order within each row by x (ascending for Ltr, descending for Rtl;
// equal x breaks by id ASC), then concatenate rows top-to-bottom.
QVector<Detection> orderRows(const QVector<QVector<Detection>>& rows,
                             ReadingDirection direction) {
    QVector<Detection> ordered;
    for (const QVector<Detection>& srcRow : rows) {
        QVector<Detection> row = srcRow;
        std::sort(row.begin(), row.end(),
                  [direction](const Detection& a, const Detection& b) {
                      if (a.box.x != b.box.x) {
                          return direction == ReadingDirection::Ltr
                                     ? a.box.x < b.box.x
                                     : a.box.x > b.box.x;
                      }
                      return a.id < b.id;   // equal x -> id ASC
                  });
        for (const Detection& d : row)
            ordered.append(d);
    }
    return ordered;
}

QVector<Detection> orderedPanels(const CanvasSpec& canvas,
                                 const QVector<Detection>& panels) {
    return orderRows(partitionRows(panels), canvas.direction);
}

// Step 7 — the ordering must contain every kept panel exactly once.
bool orderingComplete(const QVector<Detection>& ordered,
                      const QVector<Detection>& panels) {
    if (ordered.size() != panels.size())
        return false;
    QVector<int> a;
    QVector<int> b;
    a.reserve(ordered.size());
    b.reserve(panels.size());
    for (const Detection& d : ordered) a.append(d.id);
    for (const Detection& d : panels)  b.append(d.id);
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

// Common path stamping. modelVersion is left empty — the analysis service stamps
// the real model version later; the planner never invents one.
void stamp(GuidedPath& path, const CanvasSpec& canvas) {
    path.canvasFingerprint = canvas.fingerprint;
    path.plannerVersion = QStringLiteral("guided-v1");
    path.modelVersion = QString();
}

PathStep overviewStep() {
    PathStep step;
    step.kind = StepKind::Overview;
    step.sourcePanelId = -1;
    step.camera = NormalizedRect{0.0, 0.0, 1.0, 1.0};
    step.holdSecondsAt1x = 0.0;
    step.transitionSecondsAt1x = 0.0;
    step.plannerConfidence = 0.0;
    return step;
}

// Fallback: exactly one whole-page Overview, Fallback outcome + the given reason.
GuidedPath wholePage(const CanvasSpec& canvas, FallbackCode code) {
    GuidedPath path;
    stamp(path, canvas);
    path.outcome = PlanOutcome::Fallback;
    path.reason = code;
    path.steps.append(overviewStep());
    return path;
}

// Deliberate whole page (user override): one Overview, but Trusted / reason None.
GuidedPath deliberateWholePage(const CanvasSpec& canvas) {
    GuidedPath path;
    stamp(path, canvas);
    path.outcome = PlanOutcome::Trusted;
    path.reason = FallbackCode::None;
    path.steps.append(overviewStep());
    return path;
}

// Step 8 — Overview bookend, one Panel step per ordered panel, Overview bookend.
GuidedPath buildTrustedPath(const CanvasSpec& canvas,
                            const QVector<Detection>& ordered) {
    GuidedPath path;
    stamp(path, canvas);
    path.outcome = PlanOutcome::Trusted;
    path.reason = FallbackCode::None;
    path.steps.append(overviewStep());
    for (const Detection& d : ordered) {
        PathStep step;
        step.kind = StepKind::Panel;
        step.sourcePanelId = d.id;
        step.camera = d.box;
        step.holdSecondsAt1x = 0.0;         // framing/timing refined in Task 5
        step.transitionSecondsAt1x = 0.0;
        step.plannerConfidence = d.confidence;
        path.steps.append(step);
    }
    path.steps.append(overviewStep());
    return path;
}

} // namespace

GuidedPath PanelPlanner::plan(const CanvasSpec& canvas,
                              const QVector<Detection>& raw) const {
    const QVector<Detection> accepted = suppressInvalidAndDuplicates(raw);
    const QVector<Detection> panels = keepPanels(accepted);
    if (panels.isEmpty())
        return wholePage(canvas, FallbackCode::NoPanels);
    if (!coveragePlausible(panels) || hasUnresolvedOverlap(panels))
        return wholePage(canvas, FallbackCode::LayoutAmbiguous);
    const QVector<Detection> ordered = orderedPanels(canvas, panels);
    if (!orderingComplete(ordered, panels))
        return wholePage(canvas, FallbackCode::LayoutAmbiguous);
    return buildTrustedPath(canvas, ordered);
}

GuidedPath PanelPlanner::rebuild(const CanvasSpec& canvas,
                                 const QVector<Detection>& raw,
                                 OverrideKind override) const {
    switch (override) {
    case OverrideKind::None:
        return plan(canvas, raw);
    case OverrideKind::WholePage:
        return deliberateWholePage(canvas);
    case OverrideKind::DetectedPanels: {
        // Force the ordered-panel path, bypassing the no-panels/ambiguity guards
        // that plan() applies — best-effort ordering of whatever survives NMS.
        const QVector<Detection> accepted = suppressInvalidAndDuplicates(raw);
        const QVector<Detection> panels = keepPanels(accepted);
        if (panels.isEmpty())
            return wholePage(canvas, FallbackCode::NoPanels);
        const QVector<Detection> ordered = orderedPanels(canvas, panels);
        return buildTrustedPath(canvas, ordered);
    }
    }
    return plan(canvas, raw);
}

} // namespace guided
