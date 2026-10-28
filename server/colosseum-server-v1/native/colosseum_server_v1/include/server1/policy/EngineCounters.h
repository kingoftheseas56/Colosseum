#pragma once
#include "server1/policy/EngineTiming.h"
#include <string>
#include <optional>

namespace server1::policy {
// M662 arguments remain distinct from the derived counter key.
struct CounterEvent final {
    std::string hash;
    std::optional<std::int64_t> index;
    // Diagnostic attribution only; counter identity remains source hash/index.
    std::uint64_t generation = 0;
};
struct EngineCounterConfig final {
    std::function<std::string(const CounterEvent &)> key;
    std::function<void(const CounterEvent &)> onPositive;
    std::function<void(const CounterEvent &)> onZero;
    std::function<std::uint64_t()> timeoutMs;
    EngineAfter after;
};
// Source-compatible M662 counter utility. Registry/HTTP wiring is a separate K12 gate.
// Mutations/callbacks belong to one app lane. close/destruction is synchronized;
// injected timer handles must permit thread-safe cancellation/destruction.
class EngineCounter final {
public:
    explicit EngineCounter(EngineCounterConfig config);
    ~EngineCounter();
    EngineCounter(const EngineCounter &) = delete;
    EngineCounter &operator=(const EngineCounter &) = delete;
    void increment(const CounterEvent &event);
    void decrement(const CounterEvent &event);
    void close() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
