#include "server1/policy/EngineSpeedometerClock.h"
#include <atomic>
#include <stdexcept>

namespace server1::policy {
EngineSpeedometer::Tick engineSpeedometerClock(const EngineRepeat &repeat)
{
    if (!repeat) throw std::invalid_argument("speedometer clock requires an app-lane repeat timer");
    struct State {
        std::atomic<std::uint16_t> tick{1};
        std::shared_ptr<EngineTimer> timer;
        ~State() { if (timer) timer->cancel(); }
    };
    auto state = std::make_shared<State>();
    const std::weak_ptr<State> weak = state;
    state->timer = repeat(250, [weak] {
        if (auto live = weak.lock()) live->tick.fetch_add(1, std::memory_order_relaxed);
    });
    if (!state->timer) throw std::runtime_error("speedometer clock timer was not accepted");
    return [state] { return state->tick.load(std::memory_order_relaxed); };
}
}
