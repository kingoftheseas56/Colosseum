#pragma once

#include "core/HttpRouter.h"
#include "network_app/NetworkAppServices.h"

#include <atomic>
#include <functional>
#include <memory>

namespace colosseum::server::integration {

class AsyncMediaExecutor final
{
public:
    using Work = std::function<app::AppResponse()>;
    using CancellableWork = std::function<app::AppResponse(const std::atomic_bool *)>;
    using Completion = std::function<void(app::AppResponse)>;

    static void run(const std::shared_ptr<server::CancellationToken> &cancellation,
                    Work work, Completion completion);
    static void runCancellable(const std::shared_ptr<server::CancellationToken> &cancellation,
                               CancellableWork work, Completion completion);
    // Retain a service graph after its owner has stopped. The graph is released by
    // the executor when the already-submitted jobs have returned; this is deliberately
    // non-blocking so teardown cannot wait forever on a third-party call.
    static void retainUntilIdle(std::shared_ptr<void> lifetime);
    // Wait until all route work submitted through this executor has returned.
    // Runtime teardown uses this after cancelling active HTTP connections so
    // service objects captured by worker lambdas cannot be destroyed early.
    static bool waitForIdle(int timeoutMs);
};

} // namespace colosseum::server::integration
