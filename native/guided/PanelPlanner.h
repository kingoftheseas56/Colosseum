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

#include <QVector>

namespace guided {

class PanelPlanner {
public:
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
