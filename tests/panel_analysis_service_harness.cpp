// tests/panel_analysis_service_harness.cpp
//
// TDD harness for guided::PanelAnalysisService — the QML facade that orchestrates
// panel analysis on the app-owned BackgroundWorkCoordinator. The crux is the
// deterministic VISIT ORDER: with one worker and unique per-canvas priorities, the
// worker must visit canvases in exactly the reading-priority order around the
// visible canvas. The FakePanelDetector records the visited canvas index (encoded
// into each test image's pixel) under a mutex — it runs on the worker thread.
//
// Exit code is the verdict. Run from native/build-msvc so the deployed
// sqldrivers/qsqlite.dll (+ Qt6Sql.dll) beside the exe are found.
#include "guided/PanelAnalysisService.h"
#include "guided/PanelDetector.h"
#include "guided/PanelMapStore.h"
#include "guided/PanelPlanner.h"
#include "guided/GuidedTypes.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

using namespace guided;

#define CHECK(x, m) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; } } while (0)

// A thread-safe fake: detect() runs on the worker thread. It recovers the canvas
// index encoded in the image's top-left pixel (red channel) and appends it to a
// mutex-guarded call log, then returns a fixed valid 2-panel result so the planner
// produces a Trusted path and publishCanvas succeeds.
class FakePanelDetector : public IPanelDetector {
public:
    DetectorResult detect(const QImage& source, work::WorkContext& /*ctx*/) override {
        const int idx = qRed(source.pixel(0, 0));
        {
            std::lock_guard<std::mutex> g(m_mutex);
            m_order.push_back(idx);
        }
        DetectorResult r;
        r.sourceSize = source.size();
        r.inputTensorSize = QSize(640, 640);
        r.error = FallbackCode::None;
        r.detections = {
            {0, DetectionKind::Panel, {0.0, 0.0, 0.5, 1.0}, 0.9},
            {1, DetectionKind::Panel, {0.5, 0.0, 0.5, 1.0}, 0.9},
        };
        return r;
    }
    std::vector<int> callOrder() const {
        std::lock_guard<std::mutex> g(m_mutex);
        return m_order;
    }
    int callCount() const {
        std::lock_guard<std::mutex> g(m_mutex);
        return static_cast<int>(m_order.size());
    }

private:
    mutable std::mutex m_mutex;
    std::vector<int> m_order;
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir");

    // 12 single-page canvases; each PNG is filled with a colour whose RED channel is
    // the canvas index (0..11) so the fake can recover which canvas it decoded.
    const int N = 12;
    QVariantList model;
    for (int i = 0; i < N; ++i) {
        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(qRgb(i, 100, 200));
        const QString path = dir.filePath(QStringLiteral("canvas-%1.png").arg(i));
        CHECK(img.save(path, "PNG"), "save canvas png");

        QVariantMap c;
        c.insert(QStringLiteral("canvasIndex"), i);
        c.insert(QStringLiteral("pageIndices"), QVariantList{i});
        c.insert(QStringLiteral("readingPageIndices"), QVariantList{i});
        c.insert(QStringLiteral("localFiles"),
                 QVariantList{QUrl::fromLocalFile(path).toString()});
        c.insert(QStringLiteral("kind"), QStringLiteral("single"));
        c.insert(QStringLiteral("width"), 8);
        c.insert(QStringLiteral("height"), 8);
        model.append(c);
    }

    const QString dbPath = dir.filePath(QStringLiteral("panelmap.db"));
    PanelMapStore store(dbPath);
    CHECK(store.open(), "store open");

    FakePanelDetector detector;
    PanelPlanner planner;
    work::BackgroundWorkCoordinator coord(1);
    PanelAnalysisService service(&coord, &detector, &planner, &store);

    // Direct-connected completion counter (worker thread; atomic only) for a
    // race-free drain that does not depend on the event loop.
    std::atomic<int> finished{0};
    QObject::connect(&coord, &work::BackgroundWorkCoordinator::workFinished, &app,
                     [&](const QString&) { finished.fetch_add(1); }, Qt::DirectConnection);

    // Hold the worker so all 12 items are queued AND reprioritized before any runs.
    coord.setPressure(work::Pressure::Suspended);
    service.openEntry(QStringLiteral("chapter-1"), model, /*rtl=*/true);
    service.setVisibleCanvas(QStringLiteral("chapter-1"), 6);
    coord.setPressure(work::Pressure::Normal);

    // (1) VISIT ORDER: for visible=6 over canvases 0..11 the priority scheme visits
    // 6,7,8,9,10,5,0,1,2,3,4,11 — assert the first seven.
    {
        QElapsedTimer t;
        t.start();
        while (detector.callCount() < 7 && t.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(1);
        }
        CHECK(detector.callCount() >= 7, "worker visited >= 7 canvases within 10s");
        const std::vector<int> order = detector.callOrder();
        const int want[7] = {6, 7, 8, 9, 10, 5, 0};
        for (int i = 0; i < 7; ++i)
            CHECK(order[i] == want[i], "visit order matches {6,7,8,9,10,5,0}");
    }

    // Drain: wait for all 12 to complete so teardown is race-free (no work fn is
    // still touching the service when it is destroyed).
    {
        QElapsedTimer t;
        t.start();
        while (finished.load() < N && t.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(1);
        }
        CHECK(finished.load() == N, "all 12 canvases completed (watchdog = scheduling bug)");
    }

    // Let queued stage updates settle so the in-memory read model reflects Ready.
    {
        QElapsedTimer t;
        t.start();
        while (service.jobSummary(QStringLiteral("chapter-1")).value(QStringLiteral("ready")).toInt() < N
               && t.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(1);
        }
    }

    // (2) pauseJob overlays paused=true on the summary.
    service.pauseJob(QStringLiteral("chapter-1"));
    {
        const QVariantMap js = service.jobSummary(QStringLiteral("chapter-1"));
        CHECK(js.value(QStringLiteral("paused")).toBool(), "jobSummary paused==true after pauseJob");
        CHECK(js.value(QStringLiteral("total")).toInt() == N, "jobSummary total==12");
        CHECK(js.value(QStringLiteral("currentCanvas")).toInt() == 6, "jobSummary currentCanvas==6");
        CHECK(js.value(QStringLiteral("ready")).toInt() == N, "jobSummary ready==12 after settle");
    }

    // (3) A finished canvas is published + resolvable through the store.
    QString fp6;
    {
        const QVariantList details = service.canvasDetails(QStringLiteral("chapter-1"));
        CHECK(details.size() == N, "canvasDetails one row per canvas");
        for (const QVariant& dv : details) {
            const QVariantMap d = dv.toMap();
            if (d.value(QStringLiteral("canvasIndex")).toInt() == 6)
                fp6 = d.value(QStringLiteral("fingerprint")).toString();
        }
        CHECK(!fp6.isEmpty(), "canvas 6 has a fingerprint");

        CacheKey key;
        key.fingerprint = fp6;
        key.modelVersion = QStringLiteral("");
        key.plannerVersion = QStringLiteral("guided-v1");
        key.direction = ReadingDirection::Rtl;
        const LookupResult r = store.lookup(key);
        CHECK(r.found, "store.lookup finds the published canvas 6 path");
        CHECK(r.stage == CanvasStage::Ready, "canvas 6 is Ready in the store");

        const QVariantMap pm = service.pathForCanvas(QStringLiteral("chapter-1"), 6);
        CHECK(!pm.isEmpty(), "pathForCanvas returns the serialized path map");
        CHECK(pm.value(QStringLiteral("outcome")).toString() == QStringLiteral("trusted"),
              "canvas 6 planned to a trusted path");
    }

    // (4) Recovery (nice-to-have): a fresh store over the same DB still finds it.
    {
        PanelMapStore store2(dbPath);
        CHECK(store2.open(), "second store opens same db");
        CacheKey key;
        key.fingerprint = fp6;
        key.modelVersion = QStringLiteral("");
        key.plannerVersion = QStringLiteral("guided-v1");
        key.direction = ReadingDirection::Rtl;
        CHECK(store2.lookup(key).found, "published canvas 6 survives store reconstruction");
    }

    std::puts("PANEL_ANALYSIS_SERVICE_OK");
    return 0;
}
