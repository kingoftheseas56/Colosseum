#pragma once

#include "EngineFsControlPlane.h"

#include <QHash>
#include <QObject>

class QTimer;

namespace Colosseum::Server::EngineFs {

class QtEngineFsTimerScheduler final : public QObject, public IEngineFsTimerScheduler
{
public:
    using QObject::QObject;
    ~QtEngineFsTimerScheduler() override;

    TimerId schedule(std::chrono::milliseconds delay,
                     std::function<void()> callback) override;
    void cancel(TimerId id) override;

private:
    QHash<TimerId, QTimer*> timers_;
    TimerId nextId_ = 0;
};

} // namespace Colosseum::Server::EngineFs
