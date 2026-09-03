#include "QtEngineFsTimerScheduler.h"

#include <QTimer>

#include <utility>

namespace Colosseum::Server::EngineFs {

QtEngineFsTimerScheduler::~QtEngineFsTimerScheduler()
{
    const auto timers = timers_.values();
    timers_.clear();
    for (QTimer* timer : timers) {
        timer->stop();
        delete timer;
    }
}

IEngineFsTimerScheduler::TimerId QtEngineFsTimerScheduler::schedule(
    std::chrono::milliseconds delay,
    std::function<void()> callback)
{
    const TimerId id = ++nextId_;
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    timers_.insert(id, timer);

    QObject::connect(timer, &QTimer::timeout, timer,
                     [this, id, timer, callback = std::move(callback)]() mutable {
        timers_.remove(id);
        timer->deleteLater();
        callback();
    });
    timer->start(delay);
    return id;
}

void QtEngineFsTimerScheduler::cancel(TimerId id)
{
    QTimer* timer = timers_.take(id);
    if (!timer)
        return;
    timer->stop();
    delete timer;
}

} // namespace Colosseum::Server::EngineFs
