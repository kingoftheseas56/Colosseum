#pragma once

#include "core/HttpRouter.h"
#include "network_app/NetworkAppServices.h"

#include <atomic>
#include <functional>

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
    // Wait until all route work submitted through this executor has returned.
    // Runtime teardown uses this after cancelling active HTTP connections so
    // service objects captured by worker lambdas cannot be destroyed early.
    static bool waitForIdle(int timeoutMs);
};

} // namespace colosseum::server::integration
