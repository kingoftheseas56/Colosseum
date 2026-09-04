#include "AsyncMediaExecutor.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QThreadPool>
#include <QWaitCondition>

#include <exception>
#include <utility>

namespace colosseum::server::integration {
namespace {

QMutex g_jobsMutex;
QWaitCondition g_jobsIdle;
int g_activeJobs = 0;
std::vector<std::shared_ptr<void>> g_retiredLifetimes;

void jobStarted()
{
    QMutexLocker lock(&g_jobsMutex);
    ++g_activeJobs;
}

void jobFinished()
{
    std::vector<std::shared_ptr<void>> released;
    {
        QMutexLocker lock(&g_jobsMutex);
        Q_ASSERT(g_activeJobs > 0);
        --g_activeJobs;
        if (g_activeJobs == 0) {
            g_jobsIdle.wakeAll();
            released.swap(g_retiredLifetimes);
        }
    }
}

class JobCompletion final
{
public:
    JobCompletion() = default;
    ~JobCompletion() { jobFinished(); }
    JobCompletion(const JobCompletion &) = delete;
    JobCompletion &operator=(const JobCompletion &) = delete;
};

} // namespace

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
    jobStarted();
    QThreadPool::globalInstance()->start(
        [cancellation, cancelled, work = std::move(work),
         completion = std::move(completion)]() mutable {
            const JobCompletion lifetime{};
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

void AsyncMediaExecutor::retainUntilIdle(std::shared_ptr<void> lifetime)
{
    if (!lifetime)
        return;
    QMutexLocker lock(&g_jobsMutex);
    if (g_activeJobs != 0)
        g_retiredLifetimes.push_back(std::move(lifetime));
}

bool AsyncMediaExecutor::waitForIdle(int timeoutMs)
{
    if (timeoutMs < 0)
        return false;
    QElapsedTimer elapsed;
    elapsed.start();
    QMutexLocker lock(&g_jobsMutex);
    while (g_activeJobs != 0) {
        const qint64 remaining = qint64(timeoutMs) - elapsed.elapsed();
        if (remaining <= 0 || !g_jobsIdle.wait(&g_jobsMutex,
                                                static_cast<unsigned long>(remaining)))
            return g_activeJobs == 0;
    }
    return true;
}

} // namespace colosseum::server::integration
