// tests/background_work_coordinator_harness.cpp
#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <iostream>
#include <mutex>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
template <typename Predicate>
bool waitUntil(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

void pressureTransitionsWakeBothWaits()
{
    work::BackgroundWorkCoordinator coordinator(1);
    for (int iteration = 0; iteration < 128; ++iteration) {
        coordinator.setPressure(work::Pressure::Suspended);
        coordinator.submit({QStringLiteral("queued-probe"), 1}, [](work::WorkContext &) {
            return work::WorkResult::Completed;
        });
        // Exercise the idle worker's predicate/wait boundary. No subsequent
        // notification is allowed to rescue a lost pressure-resume wakeup.
        std::this_thread::yield();
        coordinator.setPressure(work::Pressure::Normal);
        require(waitUntil([&] {
            return coordinator.status(QStringLiteral("queued-probe")) == work::Status::Completed;
        }), "pressure resume wakes a queued worker without another notification");

        std::atomic_bool checkpointReady{false};
        coordinator.submit({QStringLiteral("checkpoint-probe"), 1}, [&](work::WorkContext &context) {
            // Worker callbacks may change pressure: they must not hold the
            // coordinator mutex while invoking user work.
            coordinator.setPressure(work::Pressure::Suspended);
            checkpointReady.store(true);
            return context.checkpoint() ? work::WorkResult::Completed : work::WorkResult::Cancelled;
        });
        require(waitUntil([&] { return checkpointReady.load(); }), "checkpoint probe started");
        coordinator.setPressure(work::Pressure::Normal);
        require(waitUntil([&] {
            return coordinator.status(QStringLiteral("checkpoint-probe")) == work::Status::Completed;
        }), "pressure resume wakes a running checkpoint without another notification");
    }
}

void cancellationAndShutdownWakeSuspendedCheckpoints()
{
    for (bool shutdown : {false, true}) {
        auto coordinator = std::make_unique<work::BackgroundWorkCoordinator>(1);
        std::atomic_bool checkpointReady{false};
        std::atomic_bool cancelled{false};
        coordinator->submit({QStringLiteral("held-probe"), 1}, [&](work::WorkContext &context) {
            coordinator->setPressure(work::Pressure::Suspended);
            checkpointReady.store(true);
            const bool resumed = context.checkpoint();
            cancelled.store(!resumed);
            return resumed ? work::WorkResult::Completed : work::WorkResult::Cancelled;
        });
        require(waitUntil([&] { return checkpointReady.load(); }), "held checkpoint started");
        if (shutdown) {
            coordinator.reset();
        } else {
            coordinator->cancel(QStringLiteral("held-probe"));
            require(waitUntil([&] {
                return coordinator->status(QStringLiteral("held-probe")) == work::Status::Cancelled;
            }), "cancel wakes a pressure-suspended checkpoint");
        }
        require(cancelled.load(), "cancel and shutdown preserve checkpoint cancellation");
    }
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    work::BackgroundWorkCoordinator q(1);

    // Hold the worker so all three jobs are queued before any runs — deterministic.
    q.setPressure(work::Pressure::Suspended);

    std::mutex orderMutex;
    QStringList order;
    auto fn = [&](QString name) {
        return [&, name](work::WorkContext &c) {
            if (!c.checkpoint())
                return work::WorkResult::Cancelled;
            std::lock_guard<std::mutex> g(orderMutex);
            order << name;
            return work::WorkResult::Completed;
        };
    };
    q.submit({QStringLiteral("remainder"), 10}, fn(QStringLiteral("remainder")));
    q.submit({QStringLiteral("current"), 100}, fn(QStringLiteral("current")));
    q.submit({QStringLiteral("next"), 90}, fn(QStringLiteral("next")));

    require(q.status(QStringLiteral("current")) == work::Status::Queued,
            "suspended pressure holds the queue");

    q.pause(QStringLiteral("next"));
    require(q.status(QStringLiteral("next")) == work::Status::Paused, "pause visible");
    q.resume(QStringLiteral("next"));
    require(q.status(QStringLiteral("next")) == work::Status::Queued, "resume requeues");

    QEventLoop loop;
    int finished = 0;
    QObject::connect(&q, &work::BackgroundWorkCoordinator::workFinished, &app,
                     [&](const QString &) {
                         if (++finished == 3)
                             loop.quit();
                     });
    q.setPressure(work::Pressure::Normal);
    QTimer::singleShot(10000, &loop, [&] { loop.quit(); }); // watchdog — never hang
    loop.exec();

    require(finished == 3, "all three jobs completed (watchdog fired = scheduling bug)");
    {
        std::lock_guard<std::mutex> g(orderMutex);
        require(order == QStringList({QStringLiteral("current"), QStringLiteral("next"),
                                      QStringLiteral("remainder")}),
                "priority order current > next > remainder");
    }

    // Cancel a queued job before it runs.
    q.setPressure(work::Pressure::Suspended);
    q.submit({QStringLiteral("doomed"), 5}, fn(QStringLiteral("doomed")));
    q.cancel(QStringLiteral("doomed"));
    require(q.status(QStringLiteral("doomed")) == work::Status::Cancelled,
            "cancel visible on queued job");

    // Pressure reaches a running worker through shouldYield().
    std::atomic_bool sawYield{false};
    QEventLoop loop2;
    QObject::connect(&q, &work::BackgroundWorkCoordinator::workFinished, &app,
                     [&](const QString &id) {
                         if (id == QStringLiteral("yieldprobe"))
                             loop2.quit();
                     });
    q.setPressure(work::Pressure::Normal);
    q.submit({QStringLiteral("yieldprobe"), 1}, [&](work::WorkContext &c) {
        q.setPressure(work::Pressure::LatencySensitive);
        sawYield = c.shouldYield();
        q.setPressure(work::Pressure::Normal);
        return work::WorkResult::Completed;
    });
    QTimer::singleShot(10000, &loop2, [&] { loop2.quit(); });
    loop2.exec();
    require(sawYield.load(), "video/decode pressure reaches worker");

    pressureTransitionsWakeBothWaits();
    cancellationAndShutdownWakeSuspendedCheckpoints();

    std::cout << "BACKGROUND_WORK_OK\n";
    return 0;
}
