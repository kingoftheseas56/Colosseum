// native/guided/PanelPlanner.h
//
// The pure, deterministic guided-reading planner for the Panel-Aware Guided
// Comic Reader (Agent 1). No I/O, no Qt GUI, no SQL, no randomness, no time:
// identical inputs -> byte-identical GuidedPath (serializePath is byte-stable),
// always. It turns raw panel/text detections on a canvas into an ordered guided
// path (Overview -> panel steps -> Overview), or a whole-page fallback when the
// layout is missing, ambiguous, or the machine is deliberately overridden.
#pragma once

#include "guided/GuidedTypes.h"

#include <QString>
#include <QVector>

namespace guided {

// guided-v1 timing profile — the pure, closed-form dwell/transition curves the
// planner stamps onto every step. Constants are frozen (matched by the camera
// harness); the two methods are total functions with no I/O, time, or state.
struct PlannerProfile {
    QString version;             // "guided-v1"
    double overviewHold = 0.0;   // 0.8 — dwell on each whole-page Overview bookend

    // Hold seconds at 1x zoom for a panel (or one of its internal stops):
    //   areaWeight = 1.2 * sqrt(clamp(areaRatio,0,1))
    //   textWeight = min(4.5, 0.65*textCount + 2.0*clamp(textAreaRatio,0,1))
    //   return clamp(1.5 + areaWeight + textWeight, 1.5, 8.0)
    double panelHold(double areaRatio, int textCount, double textAreaRatio) const;

    // Transition seconds between two cameras:
    //   d = min(1.0, normalizedCenterTravel + 0.35*abs(log2(max(1.0, scaleRatio))))
    //   return clamp(0.35 + 0.45*d, 0.35, 0.8)
    double transition(double normalizedCenterTravel, double scaleRatio) const;
};

class PanelPlanner {
public:
    // The frozen guided-v1 profile: {version:"guided-v1", overviewHold:0.8}.
    static PlannerProfile guidedV1();

    // Machine plan from raw detections. See PanelPlanner.cpp for the exact
    // NMS -> panels -> ambiguity guard -> row-order -> trusted-path pipeline.
    GuidedPath plan(const CanvasSpec& canvas, const QVector<Detection>& raw) const;

    // Rebuild with a user override applied:
    //   None           -> identical to plan()
    //   WholePage      -> one deliberate Overview step (Trusted, reason None)
    //   DetectedPanels -> force the ordered-panel path even where plan() would
    //                     have fallen back to whole-page; whole-page only when
    //                     there are literally no panels to order.
    GuidedPath rebuild(const CanvasSpec& canvas, const QVector<Detection>& raw,
                       OverrideKind override) const;
};

} // namespace guided
