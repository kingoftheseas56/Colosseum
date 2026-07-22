// Offscreen harness for guided::PanelDetectorOnnx (Task 6). Proves the load-bearing,
// deterministic plumbing — NOT detection quality (which is real-data / eyes-on):
//   * the checksum gate fails CLOSED (a one-byte-flipped model -> model_checksum_failed
//     before any inference, never a crash);
//   * the letterbox reversal maps 640-space boxes back to source-normalized coords exactly;
//   * a real inference runs on the CPU, returns only in-range normalized boxes, and is
//     deterministic (same image twice -> identical result).
//
// Built only under COLOSSEUM_ENABLE_ONNX. Prints PANEL_DETECTOR_OK / exit 0 on success.
#include "guided/PanelDetectorOnnx.h"
#include "models/ModelManifest.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <string>

static std::string g_fail;
static void expect(bool cond, const char* msg) {
    if (!cond && g_fail.empty()) g_fail = msg;
}
static bool nearly(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }

static QImage makePanelGrid(int w, int h) {
    QImage img(w, h, QImage::Format_RGB888);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setPen(QPen(Qt::black, 6));
    const int mx = w / 20, my = h / 20, g = w / 40;
    const int pw = (w - 2 * mx - g) / 2, ph = (h - 2 * my - g) / 2;
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c)
            p.drawRect(mx + c * (pw + g), my + r * (ph + g), pw, ph);
    p.end();
    return img;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    using namespace guided;

    const QString manifestPath =
        argc > 1 ? QString::fromLocal8Bit(argv[1])
                 : QStringLiteral("resources/models/guided/manifest.json");

    // 1) Letterbox reversal — exact math, no model. Portrait, landscape-pad, and downscale.
    {
        // 640x320 source, side 640: scale 1, padY 160. Full 640-box -> full source.
        auto r = PanelDetectorOnnx::toSourceNormalized(0, 160, 640, 480, QSize(640, 320), 640);
        expect(nearly(r.x, 0) && nearly(r.y, 0) && nearly(r.width, 1) && nearly(r.height, 1),
               "landscape full-frame reversal must be (0,0,1,1)");
        auto r2 = PanelDetectorOnnx::toSourceNormalized(100, 200, 300, 260, QSize(640, 320), 640);
        expect(nearly(r2.x, 100.0 / 640) && nearly(r2.y, 40.0 / 320)
                   && nearly(r2.width, 200.0 / 640) && nearly(r2.height, 60.0 / 320),
               "landscape inner-box reversal must land on source pixels");
        // 320x640 portrait: scale 1, padX 160.
        auto r3 = PanelDetectorOnnx::toSourceNormalized(160, 0, 480, 640, QSize(320, 640), 640);
        expect(nearly(r3.x, 0) && nearly(r3.y, 0) && nearly(r3.width, 1) && nearly(r3.height, 1),
               "portrait full-frame reversal must be (0,0,1,1)");
        // 1280x640 wide (downscale by 0.5): scale .5, padY 160.
        auto r4 = PanelDetectorOnnx::toSourceNormalized(0, 160, 640, 480, QSize(1280, 640), 640);
        expect(nearly(r4.x, 0) && nearly(r4.y, 0) && nearly(r4.width, 1) && nearly(r4.height, 1),
               "downscaled full-frame reversal must be (0,0,1,1)");
    }

    // 2) The real model loads + validates.
    PanelDetectorOnnx good(manifestPath);
    expect(good.status() == FallbackCode::None, "the bundled model must load and validate");

    // 3) Checksum gate: copy manifest + model to a temp dir, flip ONE model byte -> fail closed.
    QTemporaryDir tmp;
    expect(tmp.isValid(), "temp dir");
    FallbackCode corruptStatus = FallbackCode::None;
    if (good.status() == FallbackCode::None && tmp.isValid()) {
        const QString dir = QFileInfo(manifestPath).absolutePath();
        // load the manifest to learn the model filename
        auto mf = models::ModelManifest::load(manifestPath);
        const QString file = mf ? mf->file : QStringLiteral("manga_panel_detector_fp32.onnx");
        QFile::copy(manifestPath, tmp.filePath("manifest.json"));
        const QString srcModel = dir + "/" + file;
        const QString dstModel = tmp.filePath(file);
        QFile::copy(srcModel, dstModel);
        // flip one byte deep in the model
        QFile f(dstModel);
        if (f.open(QIODevice::ReadWrite)) {
            const qint64 pos = f.size() / 2;
            f.seek(pos);
            char b; f.getChar(&b);
            f.seek(pos);
            f.putChar(static_cast<char>(b ^ 0xFF));
            f.close();
        }
        PanelDetectorOnnx corrupt(tmp.filePath("manifest.json"));
        corruptStatus = corrupt.status();
        expect(corruptStatus == FallbackCode::ModelChecksumFailed,
               "a one-byte-flipped model must fail with model_checksum_failed before inference");
    }

    // 4) Real inference (on a worker thread, via the coordinator) — in-range + deterministic.
    if (good.status() == FallbackCode::None) {
        const QImage grid = makePanelGrid(900, 1300);
        DetectorResult a, b;
        FallbackCode corruptDetectErr = FallbackCode::None;

        work::BackgroundWorkCoordinator q(1);
        QEventLoop loop;
        QObject::connect(&q, &work::BackgroundWorkCoordinator::workFinished, &loop,
                         [&](const QString&) { loop.quit(); });
        q.submit({"detect", 100}, [&](work::WorkContext& ctx) {
            a = good.detect(grid, ctx);
            b = good.detect(grid, ctx);
            PanelDetectorOnnx corrupt(tmp.filePath("manifest.json"));
            corruptDetectErr = corrupt.detect(grid, ctx).error;
            return work::WorkResult::Completed;
        });
        loop.exec();

        expect(a.error == FallbackCode::None, "inference on a real image must succeed");
        expect(corruptDetectErr == FallbackCode::ModelChecksumFailed,
               "detect() on a corrupt model must return model_checksum_failed");
        // every detection normalized to source, inside [0,1], with positive extent
        bool inRange = true;
        for (const auto& d : a.detections) {
            if (d.box.x < -1e-6 || d.box.y < -1e-6 || d.box.width <= 0 || d.box.height <= 0
                || d.box.x + d.box.width > 1.0 + 1e-4 || d.box.y + d.box.height > 1.0 + 1e-4)
                inRange = false;
        }
        expect(inRange, "every detection must be a valid source-normalized box in [0,1]");
        // determinism: identical count + identical first box
        bool det = a.detections.size() == b.detections.size();
        if (det && !a.detections.empty())
            det = nearly(a.detections[0].box.x, b.detections[0].box.x, 1e-9)
                  && nearly(a.detections[0].box.width, b.detections[0].box.width, 1e-9);
        expect(det, "CPU inference must be deterministic");
        std::fprintf(stderr, "[panel_detector] %lld detections on the synthetic grid\n",
                     static_cast<long long>(a.detections.size()));
    }

    if (!g_fail.empty()) {
        std::printf("PANEL_DETECTOR_FAIL: %s\n", g_fail.c_str());
        return 1;
    }
    std::puts("PANEL_DETECTOR_OK");
    return 0;
}
