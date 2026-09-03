#pragma once

#include "core/HttpRouter.h"
#include "network_app/NetworkAppServices.h"

#include <functional>

namespace colosseum::server::integration {

class AsyncMediaExecutor final
{
public:
    using Work = std::function<app::AppResponse()>;
    using Completion = std::function<void(app::AppResponse)>;

    static void run(const std::shared_ptr<server::CancellationToken> &cancellation,
                    Work work, Completion completion);
};

} // namespace colosseum::server::integration
