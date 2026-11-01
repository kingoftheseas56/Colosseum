#pragma once

namespace server1 {

class Runtime final {
public:
    void initialize() noexcept { initialized_ = true; }
    void shutdown() noexcept { initialized_ = false; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
};

} // namespace server1
