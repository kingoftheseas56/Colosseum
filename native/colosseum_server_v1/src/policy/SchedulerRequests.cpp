#include "server1/policy/Scheduler.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

namespace server1::policy {

enum class RequestOutcome {
    Active,
    Completed,
    Failed,
    Canceled,
    Replaced,
    CorruptReset,
};

class RequestLedger final {
public:
    bool begin(std::uint64_t requestId, std::size_t piece, std::size_t block)
    {
        if (requests_.count(requestId) != 0) {
            return false;
        }
        requests_[requestId] = {piece, block, RequestOutcome::Active, 0};
        return true;
    }

    bool finish(std::uint64_t requestId, RequestOutcome outcome)
    {
        const auto it = requests_.find(requestId);
        if (it == requests_.end() || it->second.outcome != RequestOutcome::Active
            || outcome == RequestOutcome::Active) {
            return false;
        }
        it->second.outcome = outcome;
        ++it->second.terminalCount;
        return true;
    }

    std::size_t replacePiece(std::size_t piece)
    {
        std::size_t count = 0;
        for (auto &[id, request] : requests_) {
            static_cast<void>(id);
            if (request.piece == piece && request.outcome == RequestOutcome::Active) {
                request.outcome = RequestOutcome::Replaced;
                ++request.terminalCount;
                ++count;
            }
        }
        return count;
    }

    std::vector<std::size_t> invalidateGroup(std::size_t start, std::size_t endExclusive)
    {
        std::vector<std::size_t> reset;
        for (std::size_t piece = start; piece < endExclusive; ++piece) {
            reset.push_back(piece);
            for (auto &[id, request] : requests_) {
                static_cast<void>(id);
                if (request.piece == piece && request.outcome == RequestOutcome::Active) {
                    request.outcome = RequestOutcome::CorruptReset;
                    ++request.terminalCount;
                }
            }
        }
        return reset;
    }

    [[nodiscard]] std::optional<RequestOutcome> outcome(std::uint64_t requestId) const
    {
        const auto it = requests_.find(requestId);
        return it == requests_.end() ? std::nullopt : std::optional<RequestOutcome>(it->second.outcome);
    }

    [[nodiscard]] std::size_t terminalCount(std::uint64_t requestId) const
    {
        const auto it = requests_.find(requestId);
        return it == requests_.end() ? 0 : it->second.terminalCount;
    }

private:
    struct Request final {
        std::size_t piece = 0;
        std::size_t block = 0;
        RequestOutcome outcome = RequestOutcome::Active;
        std::size_t terminalCount = 0;
    };
    std::map<std::uint64_t, Request> requests_;
};

enum class PulseActionType {
    Update,
    Select,
};

class PulseGate final {
public:
    void update(std::uint64_t downloadedBytes,
                std::uint64_t floodAt,
                double aggregateBytesPerSecond,
                double pulseBytesPerSecond,
                std::uint64_t nowMs)
    {
        actions_.push_back(PulseActionType::Update);
        if (Scheduler::pulseDisposition(downloadedBytes,
                                        floodAt,
                                        aggregateBytesPerSecond,
                                        pulseBytesPerSecond)
            == PulseDisposition::Debounced) {
            dueAtMs_ = nowMs + 500;
            return;
        }
        dueAtMs_.reset();
        actions_.push_back(PulseActionType::Select);
    }

    void advance(std::uint64_t nowMs,
                 std::uint64_t downloadedBytes,
                 std::uint64_t floodAt,
                 double aggregateBytesPerSecond,
                 double pulseBytesPerSecond)
    {
        if (!dueAtMs_ || nowMs < *dueAtMs_) {
            return;
        }
        dueAtMs_.reset();
        update(downloadedBytes, floodAt, aggregateBytesPerSecond, pulseBytesPerSecond, nowMs);
    }

    [[nodiscard]] std::vector<PulseActionType> takeActions()
    {
        std::vector<PulseActionType> result;
        result.swap(actions_);
        return result;
    }

private:
    std::optional<std::uint64_t> dueAtMs_;
    std::vector<PulseActionType> actions_;
};

int Scheduler::requestBudget(std::size_t unchokedPeers) noexcept
{
    const double unchoked = static_cast<double>(unchokedPeers);
    const double clamped = std::max(0.0, std::min(1.0, (unchoked - 1.0) / 29.0));
    const double normalRange = 1.0 - clamped;
    return static_cast<int>(std::round(45.0 * std::pow(normalRange, 4.0) + 5.0));
}

PulseDisposition Scheduler::pulseDisposition(std::uint64_t downloadedBytes,
                                              std::uint64_t floodAt,
                                              double aggregateBytesPerSecond,
                                              double pulseBytesPerSecond) noexcept
{
    return downloadedBytes >= floodAt && aggregateBytesPerSecond > pulseBytesPerSecond
        ? PulseDisposition::Debounced
        : PulseDisposition::Immediate;
}

} // namespace server1::policy
