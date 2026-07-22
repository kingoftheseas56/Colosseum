// tests/guided_camera_controller_harness.cpp
//
// TDD harness for guided::GuidedCameraController — the Panel Step / Auto Read
// state machine. Both modes drive the SAME serialized GuidedPath, and Auto Read
// timing is deterministic because the controller schedules its hold deadlines on
// an INJECTED clock. This harness injects a FakeGuidedClock and advances virtual
// milliseconds by hand — no wall-time, no event loop needed for the timed path.
//
// Exit code is the verdict: prints GUIDED_CAMERA_CONTROLLER_OK and returns 0 only
// when every assertion holds.
#include "guided/GuidedCameraController.h"
#include "guided/GuidedTypes.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QVariantMap>

#include <cmath>
#include <cstdio>
#include <functional>

using namespace guided;

#define CHECK(x, m) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; } } while (0)

static bool approx(double a, double b) { return std::fabs(a - b) < 1e-6; }

// Deterministic clock: schedule() records one deadline and resets the elapsed
// accumulator; advanceMs() accumulates virtual time and, once it reaches the
// deadline, fires the callback EXACTLY ONCE. The callback may re-schedule from
// inside the fire (Auto Read chains hold→advance→hold) — we clear pending state
// before invoking, so a re-schedule during the fire survives.
class FakeGuidedClock : public IGuidedClock {
public:
    void schedule(int ms, std::function<void()> cb) override {
        m_cb = std::move(cb);
        m_deadline = ms;
        m_elapsed = 0;
        m_pending = true;
    }
    void cancel() override {
        m_pending = false;
        m_cb = nullptr;
    }
    void advanceMs(int ms) {
        if (!m_pending)
            return;
        m_elapsed += ms;
        if (m_elapsed >= m_deadline) {
            auto cb = m_cb;          // copy: cb may re-schedule (rearming m_cb)
            m_pending = false;
            m_cb = nullptr;
            if (cb) cb();
        }
    }
    bool pending() const { return m_pending; }

private:
    std::function<void()> m_cb;
    int m_deadline = 0;
    int m_elapsed = 0;
    bool m_pending = false;
};

// Build the QVariantMap the controller's setPath() expects (same shape
// serializePath produces), from a GuidedPath value.
static QVariantMap toMap(const GuidedPath& p) {
    const QByteArray bytes = serializePath(p);
    return QJsonDocument::fromJson(bytes).object().toVariantMap();
}

// Overview, Panel A (left), Panel B (far right ~{0.72,0.21}), Overview.
static GuidedPath makePath() {
    GuidedPath p;
    p.canvasFingerprint = "sha256:cam";
    p.modelVersion = "panel-yolo26n-x";
    p.plannerVersion = "guided-v1";
    p.outcome = PlanOutcome::Trusted;
    p.steps = {
        {StepKind::Overview,     -1, {0.00, 0.00, 1.0, 1.0}, 0.8, 0.35, 1.00},  // 0
        {StepKind::Panel,         1, {0.00, 0.00, 0.4, 0.4}, 2.4, 0.60, 0.92},  // 1  center (0.20,0.20)
        {StepKind::Panel,         2, {0.52, 0.01, 0.4, 0.4}, 2.0, 0.50, 0.90},  // 2  center (0.72,0.21)
        {StepKind::Overview,     -1, {0.00, 0.00, 1.0, 1.0}, 0.8, 0.35, 1.00},  // 3
    };
    return p;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const GuidedPath path = makePath();
    const QVariantMap map = toMap(path);

    // ── Primary flow: one serialized path, driven by Panel Step then Auto Read ──
    FakeGuidedClock clock;
    GuidedCameraController c(&clock);

    // (1) setPath opens PAUSED on the overview.
    c.setPath(map, 0);
    CHECK(c.stepIndex() == 0, "setPath: stepIndex==0");
    CHECK(!c.autoRead(), "setPath: opens paused (autoRead==false)");
    CHECK(!c.interrupted(), "setPath: not interrupted");
    CHECK(approx(c.cameraRect().x(), 0.0) && approx(c.cameraRect().width(), 1.0),
          "setPath: cameraRect is the overview");
    CHECK(c.transitionMs() == 350, "setPath: transitionMs = round(1000*0.35/1.0)");

    // (2) Panel Step: advance uses the serialized path order.
    c.advance();
    CHECK(c.stepIndex() == 1, "advance -> stepIndex==1 (Panel A)");
    CHECK(approx(c.cameraRect().width(), 0.4) && approx(c.cameraRect().x(), 0.0),
          "advance: cameraRect is Panel A");

    // (3) Auto Read advances the SAME path, speed-scaled. hold[1]=2.4s at 2x = 1200ms.
    c.setSpeed(2.0);
    CHECK(approx(c.speed(), 2.0), "setSpeed(2.0) applied");
    c.startAutoRead();
    CHECK(c.autoRead(), "startAutoRead: autoRead==true");
    const int hold1Ms = static_cast<int>(std::llround(1000.0 * 2.4 / 2.0));  // 1200
    clock.advanceMs(hold1Ms);
    CHECK(c.stepIndex() == 2, "Auto Read hold deadline advanced to stepIndex==2 (Panel B)");
    CHECK(c.autoRead(), "Auto Read still active mid-path");

    // (4) Interrupt: a manual gesture freezes automation, bumps the stop generation.
    const int genBefore = c.stopAnimationGeneration();
    c.interrupt(static_cast<int>(GuidedCameraController::InterruptionReason::Wheel),
                QPointF(0.72, 0.21));
    CHECK(!c.autoRead(), "interrupt: autoRead==false");
    CHECK(c.interrupted(), "interrupt: interrupted==true");
    CHECK(c.stopAnimationGeneration() == genBefore + 1, "interrupt: stopAnimationGeneration++");
    CHECK(!clock.pending(), "interrupt: pending hold cancelled");

    // (5) Resume rejoins at the panel nearest the stored manual center {0.72,0.21}
    //     = Panel B (index 2).
    c.resumeAutoRead();
    CHECK(c.stepIndex() == 2, "resume: nearest panel to {0.72,0.21} is Panel B (index 2)");
    CHECK(c.autoRead(), "resume: autoRead==true");
    CHECK(!c.interrupted(), "resume: interrupted cleared");

    // (5b) Nearest-panel is a REAL selection, not a fallback-to-current mask: move
    //      off Panel B, then resume must jump BACK to it from the stored center.
    c.retreat();
    CHECK(c.stepIndex() == 1, "retreat: back to Panel A (index 1)");
    c.resumeAutoRead();
    CHECK(c.stepIndex() == 2, "resume jumps to nearest panel (index 2), a genuine move");

    // ── Speed clamps to [0.5, 2.0] ──
    {
        FakeGuidedClock ck;
        GuidedCameraController s(&ck);
        s.setPath(map, 0);
        s.setSpeed(5.0);
        CHECK(approx(s.speed(), 2.0), "setSpeed(5.0) clamps to 2.0");
        s.setSpeed(0.1);
        CHECK(approx(s.speed(), 0.5), "setSpeed(0.1) clamps to 0.5");
    }

    // ── Advancing off the final Overview emits requestNextCanvas ──
    {
        FakeGuidedClock ck;
        GuidedCameraController n(&ck);
        int nextCount = 0;
        int prevCount = 0;
        QObject::connect(&n, &GuidedCameraController::requestNextCanvas,
                         [&]() { ++nextCount; });
        QObject::connect(&n, &GuidedCameraController::requestPreviousCanvas,
                         [&]() { ++prevCount; });
        n.setPath(map, 0);
        n.advance();  // 0->1
        n.advance();  // 1->2
        n.advance();  // 2->3 (final Overview)
        CHECK(n.stepIndex() == 3, "advanced to the final step");
        CHECK(nextCount == 0, "no requestNextCanvas before the end");
        n.advance();  // off the end
        CHECK(nextCount == 1, "advancing off the final Overview emits requestNextCanvas");
        CHECK(n.stepIndex() == 3, "stays parked on the final step");

        // retreat off step 0 asks the reader for the previous canvas.
        n.setPath(map, 0);
        n.retreat();  // at step 0
        CHECK(prevCount == 1, "retreat off step 0 emits requestPreviousCanvas");
    }

    // ── sessionState round-trips; restoreSession forces paused ──
    {
        FakeGuidedClock ck;
        GuidedCameraController w(&ck);
        w.setPath(map, 0);
        w.setCanvasIndex(4);
        w.advance();            // stepIndex 1
        w.setSpeed(1.5);
        w.startAutoRead();      // submode "auto"

        const QVariantMap snap = w.sessionState();
        CHECK(snap.value("canvasIndex").toInt() == 4, "sessionState canvasIndex");
        CHECK(snap.value("stepIndex").toInt() == 1, "sessionState stepIndex");
        CHECK(snap.value("submode").toString() == QStringLiteral("auto"),
              "sessionState submode reflects autoRead");
        CHECK(approx(snap.value("speed").toDouble(), 1.5), "sessionState speed");
        CHECK(snap.value("guided").toBool(), "sessionState guided flag");

        FakeGuidedClock ck2;
        GuidedCameraController r(&ck2);
        r.setPath(map, 0);
        r.restoreSession(snap);
        CHECK(!r.autoRead(), "restoreSession forces paused even if submode was auto");
        CHECK(r.canvasIndex() == 4, "restoreSession restores canvasIndex");
        CHECK(r.stepIndex() == 1, "restoreSession restores stepIndex");
        CHECK(approx(r.speed(), 1.5), "restoreSession restores speed");
        CHECK(!ck2.pending(), "restoreSession never arms a hold (paused)");
    }

    std::puts("GUIDED_CAMERA_CONTROLLER_OK");
    return 0;
}
