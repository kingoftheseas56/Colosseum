// native/guided/GuidedCameraController.cpp
#include "guided/GuidedCameraController.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace guided {

// --- RealGuidedClock : single-shot QTimer ------------------------------------

RealGuidedClock::RealGuidedClock(QObject* parent) {
    m_timer = new QTimer(parent);
    m_timer->setSingleShot(true);
    // Bound to m_timer as receiver so the connection dies with the timer.
    QObject::connect(m_timer, &QTimer::timeout, m_timer, [this]() {
        auto cb = m_cb;      // copy first: cb may re-schedule (reset m_cb) when it runs
        m_cb = nullptr;
        if (cb) cb();
    });
}

RealGuidedClock::~RealGuidedClock() {
    delete m_timer;          // owned; deletion severs the timeout connection
}

void RealGuidedClock::schedule(int ms, std::function<void()> cb) {
    m_timer->stop();
    m_cb = std::move(cb);
    m_timer->start(ms < 0 ? 0 : ms);
}

void RealGuidedClock::cancel() {
    m_timer->stop();
    m_cb = nullptr;
}

// --- GuidedCameraController ---------------------------------------------------

namespace {
int msFromSeconds(double seconds, double speed) {
    // Both hold and transition scale by 1/speed. speed is always clamped >= 0.5.
    return static_cast<int>(std::llround(1000.0 * seconds / speed));
}
}  // namespace

GuidedCameraController::GuidedCameraController(IGuidedClock* clock, QObject* parent)
    : QObject(parent) {
    if (clock) {
        m_clock = clock;
    } else {
        m_ownedClock = std::make_unique<RealGuidedClock>();
        m_clock = m_ownedClock.get();
    }
}

GuidedCameraController::~GuidedCameraController() {
    // Drop any pending deadline so the clock cannot fire into a half-destroyed
    // controller (matters for an injected clock that outlives us).
    if (m_clock) m_clock->cancel();
}

QRectF GuidedCameraController::cameraRect() const {
    if (m_stepIndex < 0 || m_stepIndex >= m_path.steps.size())
        return QRectF();
    const NormalizedRect& c = m_path.steps[m_stepIndex].camera;
    return QRectF(c.x, c.y, c.width, c.height);
}

int GuidedCameraController::transitionMs() const {
    if (m_stepIndex < 0 || m_stepIndex >= m_path.steps.size())
        return 0;
    return msFromSeconds(m_path.steps[m_stepIndex].transitionSecondsAt1x, m_speed);
}

void GuidedCameraController::setSpeed(double s) {
    const double clamped = std::clamp(s, 0.5, 2.0);
    if (qFuzzyCompare(clamped, m_speed))
        return;
    m_speed = clamped;
    emit speedChanged();
    // transitionMs is derived from speed, and any live hold uses the new scaling.
    emit cameraTargetChanged();
}

void GuidedCameraController::setCanvasIndex(int idx) {
    if (m_canvasIndex == idx)
        return;
    m_canvasIndex = idx;
    emit canvasIndexChanged();
}

void GuidedCameraController::setPath(const QVariantMap& serializedPath, int preferredStep) {
    // The QVariantMap is the same shape serializePath() produces; round-trip it
    // back through the strict deserializer so both modes consume one path.
    m_clock->cancel();
    const QByteArray bytes =
        QJsonDocument(QJsonObject::fromVariantMap(serializedPath)).toJson(QJsonDocument::Compact);
    const auto parsed = deserializePath(bytes);
    m_path = parsed.value_or(GuidedPath{});

    const int n = m_path.steps.size();
    m_stepIndex = n > 0 ? std::clamp(preferredStep, 0, n - 1) : 0;
    // Opens PAUSED — never auto-start.
    m_autoRead = false;
    m_interrupted = false;

    emit stepChanged();
    emit cameraTargetChanged();
    emit autoReadChanged();
    emit interruptedChanged();
}

void GuidedCameraController::advance() {
    const int n = m_path.steps.size();
    if (n == 0)
        return;
    if (m_stepIndex < n - 1) {
        ++m_stepIndex;
        emit stepChanged();
        emit cameraTargetChanged();
        if (m_autoRead)
            scheduleHold();       // Auto Read: re-arm the hold for the new step
    } else {
        // Already on the final Overview: the reader decides local-next vs boundary.
        emit requestNextCanvas();
    }
}

void GuidedCameraController::retreat() {
    if (m_stepIndex > 0) {
        --m_stepIndex;
        emit stepChanged();
        emit cameraTargetChanged();
        if (m_autoRead)
            scheduleHold();
    } else {
        emit requestPreviousCanvas();
    }
}

void GuidedCameraController::startAutoRead() {
    if (m_interrupted) {
        m_interrupted = false;
        emit interruptedChanged();
    }
    if (!m_autoRead) {
        m_autoRead = true;
        emit autoReadChanged();
    }
    scheduleHold();
}

void GuidedCameraController::pauseAutoRead() {
    m_clock->cancel();
    if (m_autoRead) {
        m_autoRead = false;
        emit autoReadChanged();
    }
    // Does NOT set interrupted — a plain pause, not a manual takeover.
}

void GuidedCameraController::interrupt(int reason, const QPointF& viewportCenter) {
    Q_UNUSED(reason);
    // A manual gesture (wheel/drag/pinch/scrub/nav) seizes the camera: freeze all
    // automation and remember where the user is looking so Resume can rejoin.
    m_clock->cancel();
    m_autoRead = false;
    m_interrupted = true;
    m_manualCenter = viewportCenter;
    ++m_stopAnimationGeneration;
    emit stopAnimationGenerationChanged();
    emit interruptedChanged();
    emit autoReadChanged();
}

void GuidedCameraController::resumeAutoRead() {
    if (m_interrupted) {
        m_interrupted = false;
        emit interruptedChanged();
    }
    // Rejoin the guided path at the panel nearest to where the user stopped.
    const int idx = nearestPanelStep(m_manualCenter);
    if (idx != m_stepIndex) {
        m_stepIndex = idx;
        emit stepChanged();
        emit cameraTargetChanged();
    }
    if (!m_autoRead) {
        m_autoRead = true;
        emit autoReadChanged();
    }
    scheduleHold();
}

QVariantMap GuidedCameraController::sessionState() const {
    QVariantMap m;
    m.insert(QStringLiteral("canvasIndex"), m_canvasIndex);
    m.insert(QStringLiteral("stepIndex"), m_stepIndex);
    m.insert(QStringLiteral("submode"), m_autoRead ? QStringLiteral("auto")
                                                    : QStringLiteral("step"));
    m.insert(QStringLiteral("speed"), m_speed);
    m.insert(QStringLiteral("guided"), true);
    return m;
}

void GuidedCameraController::restoreSession(const QVariantMap& state) {
    // A reopened reader is ALWAYS paused, whatever submode was snapshotted.
    m_clock->cancel();

    if (state.contains(QStringLiteral("canvasIndex")))
        setCanvasIndex(state.value(QStringLiteral("canvasIndex")).toInt());

    if (state.contains(QStringLiteral("speed")))
        setSpeed(state.value(QStringLiteral("speed")).toDouble());

    int step = state.value(QStringLiteral("stepIndex"), 0).toInt();
    const int n = m_path.steps.size();
    step = n > 0 ? std::clamp(step, 0, n - 1) : std::max(0, step);

    m_stepIndex = step;
    m_autoRead = false;          // forced paused, unconditionally
    m_interrupted = false;

    emit stepChanged();
    emit cameraTargetChanged();
    emit autoReadChanged();
    emit interruptedChanged();
}

void GuidedCameraController::scheduleHold() {
    const int n = m_path.steps.size();
    if (m_stepIndex < 0 || m_stepIndex >= n)
        return;
    const int ms = msFromSeconds(m_path.steps[m_stepIndex].holdSecondsAt1x, m_speed);
    m_clock->schedule(ms, [this]() { onHoldDeadline(); });
}

void GuidedCameraController::onHoldDeadline() {
    if (!m_autoRead)
        return;                  // stale deadline (paused/interrupted between fires)
    const int n = m_path.steps.size();
    if (n == 0)
        return;
    if (m_stepIndex < n - 1) {
        advance();               // increments + re-arms the next hold (autoRead true)
    } else {
        // Held on the final Overview: ask the reader for the next canvas and stop.
        emit requestNextCanvas();
        pauseAutoRead();
    }
}

int GuidedCameraController::nearestPanelStep(const QPointF& center) const {
    int best = -1;
    double bestDist = std::numeric_limits<double>::max();
    for (int i = 0; i < m_path.steps.size(); ++i) {
        const PathStep& s = m_path.steps[i];
        if (s.kind != StepKind::Panel && s.kind != StepKind::InternalStop)
            continue;
        const NormalizedPoint c = s.camera.center();
        const double dx = c.x - center.x();
        const double dy = c.y - center.y();
        const double d = dx * dx + dy * dy;   // squared Euclidean (monotonic)
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best >= 0 ? best : m_stepIndex;    // fallback: stay put if no panels
}

} // namespace guided
