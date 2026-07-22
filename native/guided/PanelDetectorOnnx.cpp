// native/guided/PanelDetectorOnnx.cpp — see PanelDetectorOnnx.h.
#include "guided/PanelDetectorOnnx.h"

#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>

namespace guided {
namespace {

FallbackCode fromManifestError(models::ManifestError e) {
    switch (e) {
    case models::ManifestError::None:            return FallbackCode::None;
    case models::ManifestError::ChecksumFailed:  return FallbackCode::ModelChecksumFailed;
    case models::ManifestError::ManifestMissing:
    case models::ManifestError::ManifestInvalid:
    case models::ManifestError::FileMissing:     return FallbackCode::ModelMissing;
    }
    return FallbackCode::ModelMissing;
}

} // namespace

PanelDetectorOnnx::Letterbox PanelDetectorOnnx::letterboxFor(QSize s, int side) {
    const double W = s.width(), H = s.height();
    if (W <= 0.0 || H <= 0.0 || side <= 0)
        return {1.0, 0.0, 0.0};
    const double scale = std::min(static_cast<double>(side) / W, static_cast<double>(side) / H);
    const double newW = std::round(W * scale);
    const double newH = std::round(H * scale);
    return {scale, (side - newW) / 2.0, (side - newH) / 2.0};
}

NormalizedRect PanelDetectorOnnx::toSourceNormalized(double x1, double y1, double x2, double y2,
                                                     QSize sourceSize, int side) {
    const double W = sourceSize.width(), H = sourceSize.height();
    NormalizedRect out;
    if (W <= 0.0 || H <= 0.0)
        return out;
    const Letterbox lb = letterboxFor(sourceSize, side);
    const double scale = lb.scale > 0.0 ? lb.scale : 1.0;
    auto sx = [&](double v) { return std::clamp((v - lb.padX) / scale, 0.0, W); };
    auto sy = [&](double v) { return std::clamp((v - lb.padY) / scale, 0.0, H); };
    const double a = sx(x1), b = sy(y1), c = sx(x2), d = sy(y2);
    out.x = a / W;
    out.y = b / H;
    out.width = std::max(0.0, c - a) / W;
    out.height = std::max(0.0, d - b) / H;
    return out;
}

PanelDetectorOnnx::PanelDetectorOnnx(const QString& manifestPath) {
    // 1) Load + integrity-check the manifest BEFORE we ever touch the model bytes.
    models::ManifestError merr = models::ManifestError::None;
    m_manifest = models::ModelManifest::load(manifestPath, &merr);
    if (!m_manifest || merr != models::ManifestError::None) {
        m_status = fromManifestError(merr == models::ManifestError::None
                                         ? models::ManifestError::ManifestInvalid : merr);
        return;
    }
    const models::ManifestError cerr = m_manifest->validateChecksum();
    if (cerr != models::ManifestError::None) {
        m_status = fromManifestError(cerr);
        return;
    }

    // 2) Domain fields ride in `extra`.
    const QJsonObject extra = m_manifest->extra;
    m_side = extra.value("input").toObject().value("width").toInt(640);
    if (m_side <= 0) m_side = 640;
    m_threshold = extra.value("confidenceThreshold").toDouble(0.25);

    // 3) Create the CPU session (single-threaded, fully optimized). Fail closed on any throw.
    try {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "guided-panel-detector");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const std::wstring wpath = m_manifest->filePath().toStdWString();
        m_session = std::make_unique<Ort::Session>(*m_env, wpath.c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        m_inputName = m_session->GetInputNameAllocated(0, alloc).get();
        m_outputName = m_session->GetOutputNameAllocated(0, alloc).get();

        // Reject anything but the frozen [.,N,6] NMS contract before we trust a run.
        const auto shape = m_session->GetOutputTypeInfo(0)
                               .GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 3 || shape[2] != 6) {
            m_session.reset();
            m_status = FallbackCode::InferenceFailed;
            return;
        }
    } catch (const Ort::Exception&) {
        m_session.reset();
        m_env.reset();
        m_status = FallbackCode::InferenceFailed;
        return;
    }

    m_status = FallbackCode::None;
}

PanelDetectorOnnx::~PanelDetectorOnnx() = default;

DetectorResult PanelDetectorOnnx::detect(const QImage& source, work::WorkContext& context) {
    DetectorResult result;
    result.inputTensorSize = QSize(m_side, m_side);
    result.sourceSize = source.size();

    if (m_status != FallbackCode::None || !m_session) {
        result.error = m_status == FallbackCode::None ? FallbackCode::ModelMissing : m_status;
        return result;
    }
    // Cooperate with pause/cancel/pressure before the heavy step. The service also
    // checkpoints around us; a cancel here just yields an empty (whole-page) result.
    if (!context.checkpoint())
        return result;
    if (source.isNull()) {
        result.error = FallbackCode::ImageDecodeFailed;
        return result;
    }

    const QImage rgb = source.convertToFormat(QImage::Format_RGB888);
    const int W = rgb.width(), H = rgb.height();
    const Letterbox lb = letterboxFor(QSize(W, H), m_side);
    const int newW = static_cast<int>(std::round(W * lb.scale));
    const int newH = static_cast<int>(std::round(H * lb.scale));
    const int padX = static_cast<int>(std::round(lb.padX));
    const int padY = static_cast<int>(std::round(lb.padY));
    const QImage scaled = rgb.scaled(std::max(1, newW), std::max(1, newH),
                                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // NCHW, 0..1, gray (114) letterbox padding — matches the Ultralytics export.
    const int plane = m_side * m_side;
    std::vector<float> input(static_cast<size_t>(3) * plane, 114.0f / 255.0f);
    for (int y = 0; y < scaled.height(); ++y) {
        const int dy = y + padY;
        if (dy < 0 || dy >= m_side) continue;
        const uchar* line = scaled.constScanLine(y);
        for (int x = 0; x < scaled.width(); ++x) {
            const int dx = x + padX;
            if (dx < 0 || dx >= m_side) continue;
            const uchar* px = line + x * 3;   // RGB
            const int base = dy * m_side + dx;
            input[base] = px[0] / 255.0f;                 // R plane
            input[plane + base] = px[1] / 255.0f;          // G plane
            input[2 * plane + base] = px[2] / 255.0f;      // B plane
        }
    }

    try {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 4> ishape{1, 3, m_side, m_side};
        Ort::Value in = Ort::Value::CreateTensor<float>(
            mem, input.data(), input.size(), ishape.data(), ishape.size());
        const char* inNames[] = {m_inputName.c_str()};
        const char* outNames[] = {m_outputName.c_str()};
        auto outputs = m_session->Run(Ort::RunOptions{nullptr}, inNames, &in, 1, outNames, 1);

        const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        const int rows = shape.size() == 3 ? static_cast<int>(shape[1]) : 0;
        const int stride = shape.size() == 3 ? static_cast<int>(shape[2]) : 6;
        const float* out = outputs[0].GetTensorData<float>();
        for (int i = 0; i < rows; ++i) {
            const float* row = out + static_cast<size_t>(i) * stride;
            const double conf = row[4];
            if (conf < m_threshold) continue;
            Detection d;
            d.id = static_cast<int>(result.detections.size());
            d.kind = (static_cast<int>(row[5] + 0.5) == 1) ? DetectionKind::Text
                                                           : DetectionKind::Panel;
            d.box = toSourceNormalized(row[0], row[1], row[2], row[3], QSize(W, H), m_side);
            d.confidence = conf;
            if (d.box.width > 0.0 && d.box.height > 0.0)
                result.detections.push_back(d);
        }
    } catch (const Ort::Exception&) {
        result.detections.clear();
        result.error = FallbackCode::InferenceFailed;
        return result;
    }

    return result;
}

} // namespace guided
