// native/guided/PanelPlanner.cpp
#include "guided/PanelPlanner.h"

#include <algorithm>
#include <cmath>

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

// The accepted Text detections — framing (internal stops) reads these; ordering
// never does. Text rides the same NMS pass as panels, so this just filters kind.
QVector<Detection> keepText(const QVector<Detection>& accepted) {
    QVector<Detection> text;
    text.reserve(accepted.size());
    for (const Detection& d : accepted) {
        if (d.kind == DetectionKind::Text)
            text.append(d);
    }
    return text;
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

// ── Restrained camera framing (Task 5) ───────────────────────────────────────
// One panel yields 1..3 camera rects: the first is the Panel step, any others are
// internal stops. Every rect is guaranteed inside the [0,1]x[0,1] canvas — either
// by clamping (containWithMargin) or by construction (traversal windows are
// subrects of a panel that is itself inside the canvas).

// Expand a box by `margin` on every side, then CLAMP the result to the canvas so
// it can never exceed [0,1]. Result is the single "normal" panel framing.
NormalizedRect containWithMargin(const NormalizedRect& box, double margin) {
    const double x = std::clamp(box.x - margin, 0.0, 1.0);
    const double y = std::clamp(box.y - margin, 0.0, 1.0);
    const double right = std::clamp(box.x + box.width + margin, 0.0, 1.0);
    const double bottom = std::clamp(box.y + box.height + margin, 0.0, 1.0);
    return NormalizedRect{x, y, std::max(0.0, right - x), std::max(0.0, bottom - y)};
}

// Full panel height, `n` overlapping windows across the panel width. Each window
// is 60% of the panel width, so consecutive windows overlap. Ordered left→right,
// then reversed for Rtl so the reading-first window comes first. First = Panel.
QVector<NormalizedRect> horizontalTraversal(const NormalizedRect& panel,
                                            ReadingDirection direction, int n) {
    n = std::max(2, n);
    const double winW = panel.width * 0.6;
    QVector<NormalizedRect> leftToRight;
    leftToRight.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / (n - 1);
        const double x = panel.x + (panel.width - winW) * t;
        leftToRight.append(NormalizedRect{x, panel.y, winW, panel.height});
    }
    if (direction == ReadingDirection::Rtl) {
        QVector<NormalizedRect> rightToLeft;
        rightToLeft.reserve(n);
        for (int i = n - 1; i >= 0; --i)
            rightToLeft.append(leftToRight[i]);
        return rightToLeft;
    }
    return leftToRight;
}

// Full panel width, `n` overlapping windows down the panel height. Each window is
// 60% of the panel height. ALWAYS top-first regardless of reading direction; the
// top window becomes the Panel step, the rest descend as internal stops.
QVector<NormalizedRect> verticalTraversal(const NormalizedRect& panel, int n) {
    n = std::max(2, n);
    const double winH = panel.height * 0.6;
    QVector<NormalizedRect> topToBottom;
    topToBottom.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / (n - 1);
        const double y = panel.y + (panel.height - winH) * t;
        topToBottom.append(NormalizedRect{panel.x, y, panel.width, winH});
    }
    return topToBottom;
}

double centerDistance(const NormalizedRect& a, const NormalizedRect& b) {
    const NormalizedPoint ca = a.center();
    const NormalizedPoint cb = b.center();
    const double dx = ca.x - cb.x;
    const double dy = ca.y - cb.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Text detections whose center lies inside the panel, ordered by prominence
// (area DESC, confidence DESC, id ASC), then greedily reduced to a well-separated
// set: a detection joins only if its center is > 0.10 (Euclidean, normalized
// canvas units) from every already-kept center. Deterministic.
QVector<Detection> separatedTextSet(const NormalizedRect& panel,
                                    const QVector<Detection>& text) {
    QVector<Detection> inside;
    inside.reserve(text.size());
    for (const Detection& t : text) {
        if (panel.contains(t.box.center()))
            inside.append(t);
    }
    std::sort(inside.begin(), inside.end(),
              [](const Detection& a, const Detection& b) {
                  const double aa = a.box.area();
                  const double ba = b.box.area();
                  if (aa != ba)
                      return aa > ba;                          // area DESC
                  if (a.confidence != b.confidence)
                      return a.confidence > b.confidence;      // confidence DESC
                  return a.id < b.id;                          // id ASC
              });
    QVector<Detection> kept;
    for (const Detection& t : inside) {
        bool separated = true;
        for (const Detection& k : kept) {
            if (centerDistance(t.box, k.box) <= 0.10) {
                separated = false;
                break;
            }
        }
        if (separated)
            kept.append(t);
    }
    return kept;
}

int separatedTextClusters(const NormalizedRect& panel,
                          const QVector<Detection>& text) {
    return separatedTextSet(panel, text).size();
}

// Panel framing first (the Panel step), then up to `cap-1` internal windows zoomed
// on the most prominent separated text clusters. Chosen by prominence, emitted in
// natural reading order (top-first, then reading direction). Each internal window
// is a 60%×60% subrect of the panel centered on a cluster, clamped to panel bounds
// (hence inside the canvas). Total rects never exceed `cap`.
QVector<NormalizedRect> cappedTextTraversal(const NormalizedRect& panel,
                                            const QVector<Detection>& text,
                                            ReadingDirection direction, int cap) {
    QVector<NormalizedRect> rects;
    rects.append(containWithMargin(panel, 0.045));   // Panel step framing

    const int maxInternal = std::max(0, cap - 1);
    QVector<Detection> clusters = separatedTextSet(panel, text);   // prominence-ordered
    if (clusters.size() > maxInternal)
        clusters.resize(maxInternal);

    // Re-order the chosen clusters into natural reading order for emission.
    std::sort(clusters.begin(), clusters.end(),
              [direction](const Detection& a, const Detection& b) {
                  const NormalizedPoint ca = a.box.center();
                  const NormalizedPoint cb = b.box.center();
                  if (ca.y != cb.y)
                      return ca.y < cb.y;                        // top first
                  if (ca.x != cb.x)
                      return direction == ReadingDirection::Ltr
                                 ? ca.x < cb.x : ca.x > cb.x;
                  return a.id < b.id;
              });

    const double w = panel.width * 0.6;
    const double h = panel.height * 0.6;
    for (const Detection& t : clusters) {
        const NormalizedPoint c = t.box.center();
        const double x = std::clamp(c.x - w / 2.0, panel.x, panel.x + panel.width - w);
        const double y = std::clamp(c.y - h / 2.0, panel.y, panel.y + panel.height - h);
        rects.append(NormalizedRect{x, y, w, h});
    }
    return rects;
}

// The restrained-framing decision for one panel. Hard cap: at most 3 rects.
QVector<NormalizedRect> cameraRects(const NormalizedRect& panel,
                                    const QVector<Detection>& text,
                                    ReadingDirection direction) {
    const double aspect = (panel.height > 0.0) ? panel.width / panel.height : 0.0;
    if (aspect >= 2.2)
        return horizontalTraversal(panel, direction, 2);       // wide  -> 2 windows
    if (aspect <= 1.0 / 2.2)
        return verticalTraversal(panel, 2);                    // tall  -> 2 windows
    if (separatedTextClusters(panel, text) >= 3)
        return cappedTextTraversal(panel, text, direction, 3); // busy  -> panel + <=2
    return QVector<NormalizedRect>{ containWithMargin(panel, 0.045) };  // normal
}

// Width ratio of two cameras, always >= 1 (max/min); 1 when either is degenerate.
double widthScaleRatio(const NormalizedRect& a, const NormalizedRect& b) {
    if (a.width <= 0.0 || b.width <= 0.0)
        return 1.0;
    return std::max(a.width, b.width) / std::min(a.width, b.width);
}

// Step 8 — Overview bookend; per ordered panel a Panel step plus 0..2 InternalStop
// steps from its camera framing; Overview bookend. guided-v1 timing on every step:
// Overview dwell = overviewHold; panel/internal dwell = panelHold(area, text);
// each step's transition is measured against the PREVIOUS step's camera.
GuidedPath buildTrustedPath(const CanvasSpec& canvas,
                            const QVector<Detection>& ordered,
                            const QVector<Detection>& text) {
    const PlannerProfile profile = PanelPlanner::guidedV1();

    GuidedPath path;
    stamp(path, canvas);
    path.outcome = PlanOutcome::Trusted;
    path.reason = FallbackCode::None;

    // Leading Overview bookend — nothing precedes it, so transition(0,1) = 0.35.
    PathStep lead = overviewStep();
    lead.holdSecondsAt1x = profile.overviewHold;
    lead.transitionSecondsAt1x = profile.transition(0.0, 1.0);
    path.steps.append(lead);
    NormalizedRect prevCamera = lead.camera;

    for (const Detection& d : ordered) {
        const NormalizedRect panelBox = d.box;

        // Text metrics for THIS panel (centers inside the panel).
        int textCount = 0;
        double textArea = 0.0;
        for (const Detection& t : text) {
            if (panelBox.contains(t.box.center())) {
                ++textCount;
                textArea += t.box.area();
            }
        }
        const double panelArea = panelBox.area();
        const double textAreaRatio = (panelArea > 0.0)
            ? std::clamp(textArea / panelArea, 0.0, 1.0) : 0.0;
        const double hold = profile.panelHold(panelArea, textCount, textAreaRatio);

        const QVector<NormalizedRect> cameras =
            cameraRects(panelBox, text, canvas.direction);
        for (int i = 0; i < cameras.size(); ++i) {
            PathStep step;
            step.kind = (i == 0) ? StepKind::Panel : StepKind::InternalStop;
            step.sourcePanelId = d.id;
            step.camera = cameras[i];
            step.holdSecondsAt1x = hold;
            step.transitionSecondsAt1x = profile.transition(
                centerDistance(prevCamera, cameras[i]),
                widthScaleRatio(prevCamera, cameras[i]));
            step.plannerConfidence = d.confidence;
            path.steps.append(step);
            prevCamera = cameras[i];
        }
    }

    // Trailing Overview bookend.
    PathStep tail = overviewStep();
    tail.holdSecondsAt1x = profile.overviewHold;
    tail.transitionSecondsAt1x = profile.transition(
        centerDistance(prevCamera, tail.camera),
        widthScaleRatio(prevCamera, tail.camera));
    path.steps.append(tail);
    return path;
}

} // namespace

PlannerProfile PanelPlanner::guidedV1() {
    PlannerProfile profile;
    profile.version = QStringLiteral("guided-v1");
    profile.overviewHold = 0.8;
    return profile;
}

double PlannerProfile::panelHold(double areaRatio, int textCount,
                                 double textAreaRatio) const {
    const double areaWeight = 1.2 * std::sqrt(std::clamp(areaRatio, 0.0, 1.0));
    const double textWeight = std::min(
        4.5, 0.65 * textCount + 2.0 * std::clamp(textAreaRatio, 0.0, 1.0));
    return std::clamp(1.5 + areaWeight + textWeight, 1.5, 8.0);
}

double PlannerProfile::transition(double normalizedCenterTravel,
                                  double scaleRatio) const {
    const double d = std::min(
        1.0,
        normalizedCenterTravel + 0.35 * std::fabs(std::log2(std::max(1.0, scaleRatio))));
    return std::clamp(0.35 + 0.45 * d, 0.35, 0.8);
}

GuidedPath PanelPlanner::plan(const CanvasSpec& canvas,
                              const QVector<Detection>& raw) const {
    const QVector<Detection> accepted = suppressInvalidAndDuplicates(raw);
    const QVector<Detection> panels = keepPanels(accepted);
    const QVector<Detection> text = keepText(accepted);
    if (panels.isEmpty())
        return wholePage(canvas, FallbackCode::NoPanels);
    if (!coveragePlausible(panels) || hasUnresolvedOverlap(panels))
        return wholePage(canvas, FallbackCode::LayoutAmbiguous);
    const QVector<Detection> ordered = orderedPanels(canvas, panels);
    if (!orderingComplete(ordered, panels))
        return wholePage(canvas, FallbackCode::LayoutAmbiguous);
    return buildTrustedPath(canvas, ordered, text);
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
        const QVector<Detection> text = keepText(accepted);
        if (panels.isEmpty())
            return wholePage(canvas, FallbackCode::NoPanels);
        const QVector<Detection> ordered = orderedPanels(canvas, panels);
        return buildTrustedPath(canvas, ordered, text);
    }
    }
    return plan(canvas, raw);
}

} // namespace guided
