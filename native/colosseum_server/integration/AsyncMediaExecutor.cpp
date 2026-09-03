#include "AsyncMediaExecutor.h"

#include <QThreadPool>

#include <exception>
#include <utility>

namespace colosseum::server::integration {

void AsyncMediaExecutor::run(
    const std::shared_ptr<server::CancellationToken> &cancellation,
    Work work, Completion completion)
{
    runCancellable(cancellation,
        [work = std::move(work)](const std::atomic_bool *) mutable {
            return work ? work() : app::AppResponse{};
        }, std::move(completion));
}

void AsyncMediaExecutor::runCancellable(
    const std::shared_ptr<server::CancellationToken> &cancellation,
    CancellableWork work, Completion completion)
{
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    if (cancellation) {
        cancellation->addCancelCallback([cancelled] {
            cancelled->store(true, std::memory_order_release);
        });
    }
    QThreadPool::globalInstance()->start(
        [cancellation, cancelled, work = std::move(work),
         completion = std::move(completion)]() mutable {
            if (cancelled->load(std::memory_order_acquire)
                || (cancellation && cancellation->isCancelled()))
                return;
            app::AppResponse result;
            try {
                result = work ? work(cancelled.get()) : app::AppResponse{};
            } catch (const std::exception &error) {
                result.status = 500;
                result.headers = {{QByteArrayLiteral("Content-Type"),
                                   QByteArrayLiteral("text/plain")}};
                result.body = QByteArray::fromStdString(error.what());
            } catch (...) {
                result.status = 500;
                result.headers = {{QByteArrayLiteral("Content-Type"),
                                   QByteArrayLiteral("text/plain")}};
                result.body = QByteArrayLiteral("Internal Server Error");
            }
            if (cancelled->load(std::memory_order_acquire)
                || (cancellation && cancellation->isCancelled()))
                return;
            if (completion)
                completion(std::move(result));
        });
}

} // namespace colosseum::server::integration
