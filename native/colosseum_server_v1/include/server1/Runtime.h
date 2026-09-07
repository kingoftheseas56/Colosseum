#pragma once

#include <string>
#include <vector>

namespace server1 {

// P03 owns only process-local lifecycle. It intentionally exposes no routes,
// torrent operations, media services, or readiness-producing capability.
class Runtime final
{
public:
    Runtime() = default;

    bool initialize() noexcept;
    void shutdown() noexcept;

    bool initialized() const noexcept;
    bool streamingReady() const noexcept;
    std::vector<std::string> capabilities() const;

private:
    bool initialized_ = false;
};

} // namespace server1
