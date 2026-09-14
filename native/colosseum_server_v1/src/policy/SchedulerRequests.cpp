#include "server1/policy/SchedulerActions.h"

#include <algorithm>
#include <cmath>

namespace server1::policy {

int Scheduler::requestBudget(std::size_t unchokedPeers) noexcept
{
    const double unchoked = static_cast<double>(unchokedPeers);
    const double clamped = std::max(0.0, std::min(1.0, (unchoked - 1.0) / 29.0));
    return static_cast<int>(std::round(45.0 * std::pow(1.0 - clamped, 4.0) + 5.0));
}

PulseDisposition Scheduler::pulseDisposition(std::uint64_t downloadedBytes,
                                              std::uint64_t floodAt,
                                              double aggregateBytesPerSecond,
                                              double pulseBytesPerSecond) noexcept
{
    return downloadedBytes >= floodAt && aggregateBytesPerSecond > pulseBytesPerSecond
        ? PulseDisposition::Debounced : PulseDisposition::Immediate;
}

bool SchedulerActionContract::track(const RequestIdentity &request)
{
    if (request.requestId == 0 || requests_.count(request.requestId) != 0)
        return false;
    requests_.emplace(request.requestId, LedgerEntry{request, RequestOutcome::Active, 0});
    nextRequestId_ = std::max(nextRequestId_, request.requestId + 1);
    return true;
}

bool SchedulerActionContract::terminalize(LedgerEntry &entry, RequestOutcome outcome)
{
    if (entry.outcome != RequestOutcome::Active || outcome == RequestOutcome::Active)
        return false;
    entry.outcome = outcome;
    ++entry.terminals;
    return true;
}

std::vector<SchedulerAction> SchedulerActionContract::decide(
    const RequestDecisionContext &context,
    const std::vector<NormalRequestCandidate> &normal,
    const std::vector<HotswapRequestCandidate> &hotswap)
{
    std::vector<SchedulerAction> result;
    const auto budget = static_cast<std::size_t>(Scheduler::requestBudget(context.unchokedPeers));
    std::size_t remaining = context.outstandingRequests < budget
        ? budget - context.outstandingRequests : 0;
    for (const auto &entry : normal) {
        if (remaining == 0)
            break;
        if (!entry.available || entry.reserved || entry.generation != context.generation)
            continue;
        RequestIdentity request{nextRequestId_++, entry.generation, entry.selectionId, context.peer,
                                entry.piece, entry.block, entry.offset, entry.length};
        if (track(request)) {
            result.push_back({SchedulerActionType::Request, request, false});
            --remaining;
        }
    }
    if (!result.empty())
        return result;

    std::vector<HotswapCandidate> candidates;
    for (std::size_t i = 0; i < hotswap.size(); ++i) {
        const auto &entry = hotswap[i];
        const auto found = requests_.find(entry.victim.requestId);
        const bool usable = entry.active && entry.victim.generation == context.generation
            && entry.replacement.generation == context.generation
            && found != requests_.end() && found->second.outcome == RequestOutcome::Active;
        candidates.push_back({i, entry.victimBytesPerSecond, usable});
    }
    const auto victimIndex = Scheduler::hotswapVictim(context.requesterBytesPerSecond, candidates);
    if (!victimIndex)
        return result;
    auto &victim = requests_.at(hotswap[*victimIndex].victim.requestId);
    if (!terminalize(victim, RequestOutcome::Replaced))
        return result;
    result.push_back({SchedulerActionType::Cancel, victim.request, true});
    const auto &replacement = hotswap[*victimIndex].replacement;
    RequestIdentity request{nextRequestId_++, replacement.generation, replacement.selectionId,
                            context.peer, replacement.piece, replacement.block,
                            replacement.offset, replacement.length};
    if (track(request))
        result.push_back({SchedulerActionType::Request, request, false});
    return result;
}

bool SchedulerActionContract::finish(std::uint64_t requestId,
                                     std::uint64_t generation,
                                     RequestOutcome outcome)
{
    const auto found = requests_.find(requestId);
    return found != requests_.end() && found->second.request.generation == generation
        && terminalize(found->second, outcome);
}

std::size_t SchedulerActionContract::replacePiece(std::size_t piece, std::uint64_t generation)
{
    std::size_t count = 0;
    for (auto &[id, entry] : requests_) {
        static_cast<void>(id);
        if (entry.request.piece == piece && entry.request.generation == generation
            && terminalize(entry, RequestOutcome::Replaced))
            ++count;
    }
    return count;
}

std::vector<std::size_t> SchedulerActionContract::invalidateGroup(
    std::size_t start, std::size_t endExclusive, std::uint64_t generation)
{
    std::vector<std::size_t> reset;
    for (std::size_t piece = start; piece < endExclusive; ++piece) {
        reset.push_back(piece);
        for (auto &[id, entry] : requests_) {
            static_cast<void>(id);
            if (entry.request.piece == piece && entry.request.generation == generation)
                terminalize(entry, RequestOutcome::CorruptReset);
        }
    }
    return reset;
}

std::optional<RequestOutcome> SchedulerActionContract::outcome(std::uint64_t requestId) const
{
    const auto found = requests_.find(requestId);
    return found == requests_.end() ? std::nullopt : std::optional<RequestOutcome>(found->second.outcome);
}

std::size_t SchedulerActionContract::terminalCount(std::uint64_t requestId) const
{
    const auto found = requests_.find(requestId);
    return found == requests_.end() ? 0 : found->second.terminals;
}

void SchedulerActionContract::pulse(std::uint64_t downloadedBytes,
                                    std::uint64_t floodAt,
                                    double aggregateBytesPerSecond,
                                    double pulseBytesPerSecond,
                                    std::uint64_t nowMs)
{
    actions_.push_back({SchedulerActionType::Update, {}, false});
    if (Scheduler::pulseDisposition(downloadedBytes, floodAt, aggregateBytesPerSecond,
                                    pulseBytesPerSecond) == PulseDisposition::Debounced) {
        pulseDueAtMs_ = nowMs + 500;
        return;
    }
    pulseDueAtMs_.reset();
    actions_.push_back({SchedulerActionType::Select, {}, false});
}

void SchedulerActionContract::advancePulse(std::uint64_t nowMs,
                                           std::uint64_t downloadedBytes,
                                           std::uint64_t floodAt,
                                           double aggregateBytesPerSecond,
                                           double pulseBytesPerSecond)
{
    if (!pulseDueAtMs_ || nowMs < *pulseDueAtMs_)
        return;
    pulseDueAtMs_.reset();
    pulse(downloadedBytes, floodAt, aggregateBytesPerSecond, pulseBytesPerSecond, nowMs);
}

std::vector<SchedulerAction> SchedulerActionContract::takeActions()
{
    std::vector<SchedulerAction> result;
    result.swap(actions_);
    return result;
}

} // namespace server1::policy
