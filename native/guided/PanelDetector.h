// native/guided/PanelDetector.h
//
// The detector seam for the Panel-Aware Guided Comic Reader (Agent 1). This header
// is deliberately ONNX-free: it is the pure interface between the analysis service
// and whatever produces panel/text detections for a decoded canvas image. Agent 0
// implements the real `PanelDetectorOnnx : IPanelDetector` behind ONNX Runtime;
// tests provide a fake. detect() runs on a background WORKER thread, so an
// implementation must be self-contained and thread-safe.
#pragma once

#include "guided/GuidedTypes.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QImage>
#include <QSize>
#include <QVector>

namespace guided {

struct DetectorResult {
    QVector<Detection> detections;   // normalized to SOURCE coords; NO ordering
    FallbackCode error = FallbackCode::None;
    QSize inputTensorSize;
    QSize sourceSize;
};

class IPanelDetector {
public:
    virtual ~IPanelDetector() = default;

    // Detect panels/text on a decoded canvas image. `context` lets a long inference
    // cooperate with pause/cancel/pressure via context.checkpoint()/shouldYield().
    virtual DetectorResult detect(const QImage& source, work::WorkContext& context) = 0;
};

} // namespace guided
