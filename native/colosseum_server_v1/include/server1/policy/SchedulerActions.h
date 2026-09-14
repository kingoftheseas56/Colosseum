#pragma once

#include "server1/policy/Scheduler.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace server1::policy {

using SchedulerPeerId = std::string;

struct RequestIdentity final {
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    SelectionId selectionId = 0;
    SchedulerPeerId peer;
    std::size_t piece = 0;
    std::size_t block = 0;
    std::size_t offset = 0;
    std::size_t length = 0;
};

enum class RequestOutcome { Active, Completed, Failed, Canceled, Replaced, CorruptReset };
enum class SchedulerActionType { Request, Cancel, Update, Select };

struct SchedulerAction final {
    SchedulerActionType type = SchedulerActionType::Update;
    RequestIdentity request;
    bool requestWireCancel = false;
};

struct NormalRequestCandidate final {
    SelectionId selectionId = 0;
    std::uint64_t generation = 0;
    std::size_t piece = 0;
    std::size_t block = 0;
    std::size_t offset = 0;
    std::size_t length = 0;
    bool available = true;
    bool reserved = false;
};

struct HotswapRequestCandidate final {
    RequestIdentity victim;
    double victimBytesPerSecond = 0.0;
    NormalRequestCandidate replacement;
    bool active = true;
};

struct RequestDecisionContext final {
    SchedulerPeerId peer;
    std::uint64_t generation = 0;
    std::size_t unchokedPeers = 0;
    std::size_t outstandingRequests = 0;
    double requesterBytesPerSecond = 0.0;
};

class SchedulerActionContract final {
public:
    bool track(const RequestIdentity &request);
    std::vector<SchedulerAction> decide(const RequestDecisionContext &context,
                                        const std::vector<NormalRequestCandidate> &normal,
                                        const std::vector<HotswapRequestCandidate> &hotswap);
    bool finish(std::uint64_t requestId, std::uint64_t generation, RequestOutcome outcome);
    std::size_t replacePiece(std::size_t piece, std::uint64_t generation);
    std::vector<std::size_t> invalidateGroup(std::size_t start,
                                             std::size_t endExclusive,
                                             std::uint64_t generation);
    [[nodiscard]] std::optional<RequestOutcome> outcome(std::uint64_t requestId) const;
    [[nodiscard]] std::size_t terminalCount(std::uint64_t requestId) const;

    void pulse(std::uint64_t downloadedBytes,
               std::uint64_t floodAt,
               double aggregateBytesPerSecond,
               double pulseBytesPerSecond,
               std::uint64_t nowMs);
    void advancePulse(std::uint64_t nowMs,
                      std::uint64_t downloadedBytes,
                      std::uint64_t floodAt,
                      double aggregateBytesPerSecond,
                      double pulseBytesPerSecond);
    std::vector<SchedulerAction> takeActions();

private:
    struct LedgerEntry final { RequestIdentity request; RequestOutcome outcome; std::size_t terminals; };
    bool terminalize(LedgerEntry &entry, RequestOutcome outcome);
    std::map<std::uint64_t, LedgerEntry> requests_;
    std::optional<std::uint64_t> pulseDueAtMs_;
    std::vector<SchedulerAction> actions_;
    std::uint64_t nextRequestId_ = 1;
};

} // namespace server1::policy
