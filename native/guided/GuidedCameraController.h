// native/guided/GuidedCameraController.h
//
// GuidedCameraController — the Panel Step / Auto Read state machine for the
// Panel-Aware Guided Comic Reader (Agent 1). Panel Step (manual) and Auto Read
// (timed) BOTH drive the SAME serialized GuidedPath — there is exactly one
// ordering authority (the path's step vector), so the two modes can never
// disagree. Manual advance/retreat and the timed hold-deadline both funnel
// through the same advance() logic.
//
// Auto Read hold deadlines are scheduled on an INJECTED clock (IGuidedClock) so
// the whole machine is deterministically testable with no wall-time: the harness
// injects a FakeGuidedClock and advances virtual milliseconds by hand. Production
// injects (or defaults to) a RealGuidedClock backed by a single-shot QTimer.
#pragma once

#include "guided/GuidedTypes.h"

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QVariantMap>

#include <functional>
#include <memory>

class QTimer;

namespace guided {

// Abstract clock the controller schedules its Auto Read hold deadlines on.
// One deadline at a time: schedule() replaces any previously-scheduled callback.
class IGuidedClock {
public:
    virtual ~IGuidedClock() = default;
    // Schedule `cb` to fire after `ms` (cancels any previously-scheduled callback).
    // ms<=0 fires on the next advance/immediately per the implementation.
    virtual void schedule(int ms, std::function<void()> cb) = 0;
    virtual void cancel() = 0;
};

// Production clock: a single-shot QTimer. Needs a running Qt event loop to fire —
// never exercised by the deterministic harness (which injects a FakeGuidedClock).
class RealGuidedClock : public IGuidedClock {
public:
    explicit RealGuidedClock(QObject* parent = nullptr);
    ~RealGuidedClock() override;
    void schedule(int ms, std::function<void()> cb) override;
    void cancel() override;

private:
    QTimer* m_timer = nullptr;               // owned
    std::function<void()> m_cb;
};

class GuidedCameraController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRectF cameraRect READ cameraRect NOTIFY cameraTargetChanged)
    Q_PROPERTY(int transitionMs READ transitionMs NOTIFY cameraTargetChanged)
    Q_PROPERTY(int stepIndex READ stepIndex NOTIFY stepChanged)
    Q_PROPERTY(bool autoRead READ autoRead NOTIFY autoReadChanged)
    Q_PROPERTY(bool interrupted READ interrupted NOTIFY interruptedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(int stopAnimationGeneration READ stopAnimationGeneration
                   NOTIFY stopAnimationGenerationChanged)
    Q_PROPERTY(int canvasIndex READ canvasIndex WRITE setCanvasIndex NOTIFY canvasIndexChanged)
public:
    enum class InterruptionReason { Wheel, Drag, Pinch, Scrub, Navigation };
    Q_ENUM(InterruptionReason)

    // clock == nullptr → owns a RealGuidedClock. Never auto-starts Auto Read.
    explicit GuidedCameraController(IGuidedClock* clock = nullptr, QObject* parent = nullptr);
    ~GuidedCameraController() override;

    QRectF cameraRect() const;
    int transitionMs() const;
    int stepIndex() const { return m_stepIndex; }
    bool autoRead() const { return m_autoRead; }
    bool interrupted() const { return m_interrupted; }
    double speed() const { return m_speed; }
    void setSpeed(double s);
    int stopAnimationGeneration() const { return m_stopAnimationGeneration; }
    int canvasIndex() const { return m_canvasIndex; }
    void setCanvasIndex(int idx);

    Q_INVOKABLE void setPath(const QVariantMap& serializedPath, int preferredStep = 0);
    Q_INVOKABLE void advance();
    Q_INVOKABLE void retreat();
    Q_INVOKABLE void startAutoRead();
    Q_INVOKABLE void pauseAutoRead();
    Q_INVOKABLE void interrupt(int reason, const QPointF& viewportCenter);
    Q_INVOKABLE void resumeAutoRead();
    Q_INVOKABLE QVariantMap sessionState() const;
    Q_INVOKABLE void restoreSession(const QVariantMap& state);

signals:
    void requestNextCanvas();
    void requestPreviousCanvas();
    void cameraTargetChanged();
    void stepChanged();
    void autoReadChanged();
    void interruptedChanged();
    void speedChanged();
    void stopAnimationGenerationChanged();
    void canvasIndexChanged();

private:
    void scheduleHold();          // arm the current step's Auto Read hold deadline
    void onHoldDeadline();        // fired by the clock when the hold elapses
    int nearestPanelStep(const QPointF& center) const;  // -1 if no panel/internal steps

    IGuidedClock* m_clock = nullptr;             // injected or points at m_ownedClock
    std::unique_ptr<IGuidedClock> m_ownedClock;  // created when constructed with null

    GuidedPath m_path;
    int m_stepIndex = 0;
    bool m_autoRead = false;
    bool m_interrupted = false;
    double m_speed = 1.0;
    int m_stopAnimationGeneration = 0;
    int m_canvasIndex = 0;
    QPointF m_manualCenter;                       // where the last interrupt froze the view
};

} // namespace guided
