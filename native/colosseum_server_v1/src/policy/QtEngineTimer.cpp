#include "server1/policy/QtEngineTimer.h"
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <utility>

namespace server1::policy {
namespace {
class QtLoopTimer final : public EngineTimer,
                        public std::enable_shared_from_this<QtLoopTimer> {
public:
    QtLoopTimer(QObject *owner, EngineContinuation callback, EngineTimerError onError, bool repeating)
        : timer_(new QTimer), lane_(QThread::currentThread()), owner_(owner), callback_(std::move(callback)), onError_(std::move(onError)), repeating_(repeating) {
        static std::atomic<std::uint64_t> next{1};
        id_ = next.fetch_add(1, std::memory_order_relaxed);
        timer_->setSingleShot(!repeating_);
        timer_->setTimerType(Qt::PreciseTimer);
    }
    ~QtLoopTimer() override {
        cancel();
        // Callback may release its own last external handle. Do not delete the
        // QObject while its timeout event is still being dispatched.
        if (timer_) timer_->deleteLater();
    }
    void start(int delay) {
        std::weak_ptr<QtLoopTimer> weak = shared_from_this();
        QObject::connect(owner_, &QObject::destroyed, timer_, [weak] {
            if (const auto self = weak.lock()) self->cancel();
        });
        QObject::connect(timer_, &QTimer::timeout, timer_, [weak] {
            const auto self = weak.lock();
            if (!self || self->cancelled_.load()) return;
            if (!self->repeating_) self->cancelled_.store(true);
            try {
                if (self->repeating_) {
                    // Retain the same callable: copying loses mutable local state.
                    // self keeps its storage alive even if the callback releases
                    // the last external handle or cancels its own timer.
                    if (self->callback_) self->callback_();
                } else {
                    auto callback = std::move(self->callback_);
                    if (callback) callback();
                }
            } catch (...) {
                self->cancel();
                const auto error = std::current_exception();
                try { self->onError_(error); }
                catch (...) { qFatal("Qt engine timer error sink threw"); }
            }
        });
        timer_->start(delay);
    }
    void cancel() noexcept override {
        cancelled_.store(true);
        // The timer has no QObject parent: only this handle schedules its
        // deletion, so it remains valid throughout cross-thread cancellation.
        if (QThread::currentThread() == lane_) timer_->stop();
        else QMetaObject::invokeMethod(timer_, "stop", Qt::QueuedConnection);
    }
    bool active() const noexcept override {
        return !cancelled_.load();
    }
    std::uint64_t id() const noexcept override { return id_; }
private:
    QTimer *timer_;
    QThread *lane_;
    QPointer<QObject> owner_;
    EngineContinuation callback_;
    EngineTimerError onError_;
    std::uint64_t id_ = 0;
    std::atomic_bool cancelled_{false};
    bool repeating_ = false;
};
}
EngineAfter qtEngineTimerFactory(QObject *owner, EngineTimerError onError, bool repeating) {
    if (!onError) throw std::invalid_argument("Qt engine timer requires an error sink");
    const QPointer<QObject> context(owner);
    return [context, onError = std::move(onError), repeating](std::uint64_t delay, EngineContinuation callback)
        -> std::shared_ptr<EngineTimer> {
        if (!context || !callback) return {};
        if (context->thread() != QThread::currentThread())
            throw std::logic_error("Qt engine timer requires the owning app lane");
        if (repeating && delay == 0)
            throw std::invalid_argument("Qt repeating engine timer requires a positive interval");
        if (delay > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            throw std::out_of_range("Qt engine timer delay exceeds supported range");
        auto timer = std::make_shared<QtLoopTimer>(context, std::move(callback), onError, repeating);
        timer->start(static_cast<int>(delay));
        return timer;
    };
}
EngineAfter qtEngineAfter(QObject *owner, EngineTimerError onError) {
    return qtEngineTimerFactory(owner, std::move(onError), false);
}
EngineRepeat qtEngineRepeat(QObject *owner, EngineTimerError onError) {
    return qtEngineTimerFactory(owner, std::move(onError), true);
}
}
