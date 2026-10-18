#pragma once
#include <cstdint>
#include <functional>
#include <memory>
namespace server1::policy {
using EngineContinuation = std::function<void()>;
using EnginePost = std::function<bool(EngineContinuation)>;
using EngineClock = std::function<std::uint64_t()>;
class EngineTimer {
public:
    virtual ~EngineTimer() = default;
    virtual void cancel() noexcept = 0;
    [[nodiscard]] virtual bool active() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t id() const noexcept = 0;
};
using EngineRepeat = std::function<std::shared_ptr<EngineTimer>(std::uint64_t, EngineContinuation)>;
// One-shot, never inline, app-lane delivery. Handles allow safe teardown.
using EngineAfter = std::function<std::shared_ptr<EngineTimer>(std::uint64_t, EngineContinuation)>;
}
