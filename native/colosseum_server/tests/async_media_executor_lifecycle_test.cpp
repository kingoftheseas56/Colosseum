#include "../integration/AsyncMediaExecutor.h"

#include <QTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using colosseum::server::CancellationToken;
using colosseum::server::integration::AsyncMediaExecutor;

class AsyncMediaExecutorLifecycleTest final : public QObject
{
    Q_OBJECT

private slots:
    void cancellationCanBeDrainedBeforeServiceTeardown();
};

void AsyncMediaExecutorLifecycleTest::cancellationCanBeDrainedBeforeServiceTeardown()
{
    const auto cancellation = std::make_shared<CancellationToken>();
    std::atomic_bool started{false};

    AsyncMediaExecutor::runCancellable(
        cancellation,
        [&started](const std::atomic_bool *cancelled) {
            started.store(true, std::memory_order_release);
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(75);
            while (!cancelled->load(std::memory_order_acquire)
                   && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return colosseum::server::app::AppResponse{};
        },
        [](colosseum::server::app::AppResponse) {});

    QTRY_VERIFY_WITH_TIMEOUT(started.load(std::memory_order_acquire), 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    QVERIFY(AsyncMediaExecutor::waitForIdle(3000));
}

QTEST_GUILESS_MAIN(AsyncMediaExecutorLifecycleTest)
#include "async_media_executor_lifecycle_test.moc"
