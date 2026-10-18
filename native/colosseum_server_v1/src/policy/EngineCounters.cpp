#include "server1/policy/EngineCounters.h"
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>

namespace server1::policy {
struct EngineCounter::State final {
    explicit State(EngineCounterConfig c) : config(std::move(c)) {}
    EngineCounterConfig config;
    std::thread::id lane = std::this_thread::get_id();
    bool alive = true;
    std::uint64_t serial = 0;
    std::map<std::string, double> counts;
    std::map<std::string, std::uint64_t> timeouts;
    struct Pending { std::shared_ptr<EngineTimer> handle; bool fired = false; bool scheduling = true; };
    std::map<std::uint64_t, Pending> owned;
    void checkLane() const {
        if (lane != std::this_thread::get_id())
            throw std::logic_error("EngineCounter requires its app lane");
    }
    double &value(const std::string &key) {
        return counts.emplace(key, std::numeric_limits<double>::quiet_NaN()).first->second;
    }
    void cancel(const std::string &key) {
        const auto it = timeouts.find(key);
        if (it == timeouts.end()) return;
        const auto token = it->second;
        const auto timer = owned.find(token);
        if (timer != owned.end()) {
            auto handle = timer->second.handle;
            owned.erase(timer);
            if (handle) handle->cancel();
        }
        timeouts.erase(key);
    }
};
EngineCounter::EngineCounter(EngineCounterConfig config) {
    if (!config.key || !config.onPositive || !config.onZero ||
        !config.timeoutMs || !config.after)
        throw std::invalid_argument("EngineCounter requires callbacks and one-shot scheduler");
    state_ = std::make_shared<State>(std::move(config));
}
EngineCounter::~EngineCounter() { close(); }
void EngineCounter::close() noexcept {
    auto state = state_;
    if (!state || !state->alive) return;
    state->alive = false;
    auto pending = std::move(state->owned);
    state->timeouts.clear();
    state->counts.clear();
    for (auto &entry : pending) if (entry.second.handle) entry.second.handle->cancel();
}
void EngineCounter::increment(const CounterEvent &event) {
    auto s = state_;
    s->checkLane();
    if (!s->alive) return;
    const auto key = s->config.key(event);
    if (!s->alive) return;
    if (!s->counts.count(key)) {
        s->counts[key] = 0;
        s->config.onPositive(event);
        if (!s->alive) return;
    }
    ++s->value(key);
    s->cancel(key);
}
void EngineCounter::decrement(const CounterEvent &event) {
    auto s = state_;
    s->checkLane();
    if (!s->alive) return;
    const auto key = s->config.key(event);
    if (!s->alive) return;
    if (--s->value(key) != 0) return;
    s->cancel(key);
    const auto delay = s->config.timeoutMs();
    if (!s->alive) return;
    const auto token = ++s->serial;
    s->owned.emplace(token, State::Pending{});
    std::weak_ptr<State> weak = s;
    std::shared_ptr<EngineTimer> handle;
    try {
        handle = s->config.after(delay, [weak, key, event, token] {
            auto state = weak.lock();
            if (!state || !state->alive) return;
            state->checkLane();
            auto pending = state->owned.find(token);
            if (pending == state->owned.end() || pending->second.fired) return;
            if (pending->second.scheduling)
                throw std::logic_error("EngineCounter scheduler delivered inline");
            pending->second.fired = true;
            try {
                state->config.onZero(event);
            } catch (...) {
                // Keep the fired handle: M662 retains it after an onZero throw.
                throw; // M662 does not execute count/timeout deletion on callback throw.
            }
            if (!state->alive) return;
            state->counts.erase(key);
            state->timeouts.erase(key);
            // A reentrant timer stays owned even if the source-visible map lost it.
            state->owned.erase(token);
        });
        if (!handle) throw std::runtime_error("EngineCounter scheduler rejected timer");
    } catch (...) {
        s->owned.erase(token);
        throw;
    }
    auto it = s->owned.find(token);
    if (!s->alive) { handle->cancel(); return; }
    if (it == s->owned.end() || it->second.fired) {
        handle->cancel();
        throw std::logic_error("EngineCounter scheduler delivered inline");
    }
    it->second.scheduling = false;
    it->second.handle = std::move(handle);
    s->timeouts[key] = token;
}
}
