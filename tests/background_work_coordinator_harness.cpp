// tests/background_work_coordinator_harness.cpp
#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QStringList>
#include <QTimer>

#include <atomic>
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

    // Repeated idle -> Suspended -> Normal transitions exercise the wake-up
    // boundary, not just work that happens to arrive while a worker is awake.
    // Keep the same watchdog and require every submitted job to complete.
    QEventLoop transitionLoop;
    QTimer transitionWatchdog;
    transitionWatchdog.setSingleShot(true);
    QObject::connect(&transitionWatchdog, &QTimer::timeout, &transitionLoop, &QEventLoop::quit);
    QString expectedId;
    bool transitionFinished = false;
    QObject::connect(&q, &work::BackgroundWorkCoordinator::workFinished, &transitionLoop,
                     [&](const QString &id) {
                         if (id == expectedId) {
                             transitionFinished = true;
                             transitionLoop.quit();
                         }
                     });
    for (int i = 0; i < 512; ++i) {
        q.setPressure(work::Pressure::Suspended);
        expectedId = QStringLiteral("pressure-transition-%1").arg(i);
        transitionFinished = false;
        q.submit({expectedId, 1}, [](work::WorkContext &c) {
            return c.checkpoint() ? work::WorkResult::Completed : work::WorkResult::Cancelled;
        });
        require(q.status(expectedId) == work::Status::Queued,
                "suspended transition keeps work queued");
        transitionWatchdog.start(10000);
        q.setPressure(work::Pressure::Normal);
        transitionLoop.exec();
        transitionWatchdog.stop();
        if (!transitionFinished)
            std::cerr << "transition " << i << ", status " << static_cast<int>(q.status(expectedId)) << '\n';
        require(transitionFinished, "resuming pressure wakes queued work without another notification");
    }

    std::cout << "BACKGROUND_WORK_OK\n";
    return 0;
}
