#include "../integration/AsyncMediaExecutor.h"
#include "../core/ColosseumServer.h"

#include <QCoreApplication>
#include <QTcpSocket>
#include <QTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using colosseum::server::integration::AsyncMediaExecutor;

class AsyncMediaExecutorLifecycleTest final : public QObject
{
    Q_OBJECT

private slots:
    void cancellationCanBeDrainedBeforeServiceTeardown();
    void lifetimeCanBeRetiredWithoutAnUnboundedWait();
};

void AsyncMediaExecutorLifecycleTest::cancellationCanBeDrainedBeforeServiceTeardown()
{
    colosseum::server::ColosseumServer server;
    std::atomic_bool observedCancellation{false};
    std::atomic_bool started{false};

    server.router().get(QStringLiteral("/hold"),
        [&started, &observedCancellation](colosseum::server::HttpRequest &request,
                                          colosseum::server::HttpResponse response) {
            AsyncMediaExecutor::runCancellable(
                request.cancellation,
                [&started, &observedCancellation](const std::atomic_bool *cancelled) {
                    started.store(true, std::memory_order_release);
                    while (!cancelled->load(std::memory_order_acquire))
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    observedCancellation.store(true, std::memory_order_release);
                    return colosseum::server::app::AppResponse{};
                },
                [response](colosseum::server::app::AppResponse) mutable {
                    response.end();
                });
            return true;
        });
    QVERIFY2(server.start(0), qPrintable(server.lastError()));

    QTcpSocket client;
    client.connectToHost(server.boundUrl().host(),
                         static_cast<quint16>(server.boundUrl().port()));
    QVERIFY(client.waitForConnected(3000));
    const QByteArray request = QByteArrayLiteral(
        "GET /hold HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    QVERIFY(client.write(request) == request.size());
    QVERIFY(client.waitForBytesWritten(3000));
    QTRY_VERIFY_WITH_TIMEOUT(started.load(std::memory_order_acquire), 3000);
    client.abort();
    QTRY_VERIFY_WITH_TIMEOUT(observedCancellation.load(std::memory_order_acquire), 3000);
    QVERIFY(AsyncMediaExecutor::waitForIdle(3000));
    QVERIFY(observedCancellation.load(std::memory_order_acquire));
    server.stop();
}

void AsyncMediaExecutorLifecycleTest::lifetimeCanBeRetiredWithoutAnUnboundedWait()
{
    struct Lifetime final
    {
        std::atomic_bool *destroyed = nullptr;
        ~Lifetime()
        {
            if (destroyed)
                destroyed->store(true, std::memory_order_release);
        }
    };

    const auto release = std::make_shared<std::atomic_bool>(false);
    const auto started = std::make_shared<std::atomic_bool>(false);
    std::atomic_bool destroyed{false};
    std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>();
    lifetime->destroyed = &destroyed;
    const std::weak_ptr<Lifetime> weakLifetime = lifetime;

    AsyncMediaExecutor::runCancellable(
        {},
        [release, started](const std::atomic_bool *) {
            started->store(true, std::memory_order_release);
            while (!release->load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return colosseum::server::app::AppResponse{};
        },
        [](colosseum::server::app::AppResponse) {});

    QTRY_VERIFY_WITH_TIMEOUT(started->load(std::memory_order_acquire), 3000);
    AsyncMediaExecutor::retainUntilIdle(std::static_pointer_cast<void>(lifetime));
    lifetime.reset();
    QVERIFY(!weakLifetime.expired());
    QVERIFY(!AsyncMediaExecutor::waitForIdle(25));

    release->store(true, std::memory_order_release);
    QVERIFY(AsyncMediaExecutor::waitForIdle(3000));
    QTRY_VERIFY_WITH_TIMEOUT(weakLifetime.expired(), 3000);
    QVERIFY(destroyed.load(std::memory_order_acquire));
}

QTEST_GUILESS_MAIN(AsyncMediaExecutorLifecycleTest)
#include "async_media_executor_lifecycle_test.moc"
