#include "AsyncMediaExecutor.h"

#include <QThreadPool>

#include <exception>
#include <utility>

namespace colosseum::server::integration {

void AsyncMediaExecutor::run(
    const std::shared_ptr<server::CancellationToken> &cancellation,
    Work work, Completion completion)
{
    QThreadPool::globalInstance()->start(
        [cancellation, work = std::move(work), completion = std::move(completion)]() mutable {
            if (cancellation && cancellation->isCancelled())
                return;
            app::AppResponse result;
            try {
                result = work ? work() : app::AppResponse{};
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
            if (cancellation && cancellation->isCancelled())
                return;
            if (completion)
                completion(std::move(result));
        });
}

} // namespace colosseum::server::integration
