#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace server1::policy {
// M416's shared quarter-second tick is supplied by the existing owner. All
// meters sharing a source clock must receive the same modulo-65536 tick.
// This utility does not create a timer, worker or independent clock.
// The owner serializes calls; tick is a non-reentrant clock read. The typed
// window supports the server default and integer seconds through 16383.
class EngineSpeedometer final {
public:
    using Tick = std::function<std::uint16_t()>;
    explicit EngineSpeedometer(Tick tick, std::uint32_t seconds = 5);
    double sample(double delta = 0);
private:
    Tick tick_;
    std::uint32_t size_;
    std::vector<double> buffer_{0};
    std::uint32_t pointer_ = 1;
    std::uint16_t last_;
};
}
