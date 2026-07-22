// native/guided/PanelDetectorOnnx.h
//
// The real IPanelDetector: a bundled YOLO26n panel/text detector run on ONNX Runtime
// CPU. Loads + checksum-validates the model manifest BEFORE creating the session (a
// tampered/missing model fails closed with a stable code, never a crash), letterboxes
// the decoded canvas to 640x640, runs inference, and reverses the letterbox so every
// detection comes back normalized to SOURCE coordinates (the seam contract). No ordering
// or planning happens here — that is PanelPlanner's job.
//
// Only compiled when COLOSSEUM_ENABLE_ONNX=ON (the app links ONNX Runtime); the default
// build ships the whole-page Guided shell without it.
#pragma once

#include "guided/PanelDetector.h"
#include "models/ModelManifest.h"

#include <onnxruntime_cxx_api.h>

#include <QSize>
#include <QString>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace guided {

class PanelDetectorOnnx : public IPanelDetector {
public:
    // manifestPath -> resources/models/guided/manifest.json. Construction validates the
    // model and creates the session; if anything is wrong, status() carries the reason
    // and detect() fails closed with it. Never throws.
    explicit PanelDetectorOnnx(const QString& manifestPath);
    ~PanelDetectorOnnx() override;

    DetectorResult detect(const QImage& source, work::WorkContext& context) override;

    // FallbackCode::None once the model loaded + validated + a session exists.
    FallbackCode status() const { return m_status; }

    // Exposed for tests: reverse the 640x640 letterbox for a detection box back to a
    // box normalized to the source image. Pure math, no model needed.
    static NormalizedRect toSourceNormalized(double x1, double y1, double x2, double y2,
                                             QSize sourceSize, int side);

private:
    struct Letterbox { double scale = 1.0; double padX = 0.0; double padY = 0.0; };
    static Letterbox letterboxFor(QSize sourceSize, int side);

    FallbackCode m_status = FallbackCode::ModelMissing;
    int m_side = 640;
    double m_threshold = 0.25;

    std::optional<models::ModelManifest> m_manifest;
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::string m_inputName;
    std::string m_outputName;
};

} // namespace guided
