#include "server1/policy/Scheduler.h"

#include <limits>
#include <optional>
#include <vector>

namespace server1::policy {

std::optional<std::size_t>
Scheduler::hotswapVictim(double requesterBytesPerSecond,
                         const std::vector<HotswapCandidate> &candidates) noexcept
{
    constexpr double kBlockSize = 16384.0;
    constexpr double kProtectedSpeed = 3.0 * kBlockSize;
    if (requesterBytesPerSecond < kBlockSize) {
        return std::nullopt;
    }

    std::optional<std::size_t> victim;
    double minimumSpeed = std::numeric_limits<double>::infinity();
    for (const auto &candidate : candidates) {
        if (!candidate.active || candidate.bytesPerSecond >= kProtectedSpeed
            || 2.0 * candidate.bytesPerSecond > requesterBytesPerSecond
            || candidate.bytesPerSecond > minimumSpeed) {
            continue;
        }
        victim = candidate.reservation;
        minimumSpeed = candidate.bytesPerSecond;
    }
    return victim;
}

} // namespace server1::policy
