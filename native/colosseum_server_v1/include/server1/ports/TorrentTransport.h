#pragma once

#include "server1/policy/Scheduler.h"
#include "server1/discovery/PeerSearch.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace server1::ports {

inline constexpr std::uint32_t kWireBlockLength = 16384;
using PeerHandle = std::uint64_t;

struct RequestOwnership final {
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    policy::SelectionId selectionId = 0;
};

struct BlockSpan final {
    std::uint32_t piece = 0;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

[[nodiscard]] constexpr bool isValidBlock(const BlockSpan &block) noexcept
{
    return block.length > 0 && block.length <= kWireBlockLength
        && block.offset % kWireBlockLength == 0;
}

struct RequestAction final {
    RequestOwnership ownership;
    PeerHandle peer = 0;
    BlockSpan block;
};

struct CancelAction final {
    RequestOwnership ownership;
    PeerHandle peer = 0;
    BlockSpan block;
    bool requestWireCancel = false;
};

struct InterestAction final {
    PeerHandle peer = 0;
    bool interested = false;
};

struct ChokeAction final {
    PeerHandle peer = 0;
    bool choked = true;
};

using TorrentAction = std::variant<RequestAction, CancelAction, InterestAction, ChokeAction>;

struct BlockObservation final {
    RequestOwnership ownership;
    PeerHandle peer = 0;
    BlockSpan block;
    std::vector<std::uint8_t> payload;
    bool late = false;
    bool duplicate = false;
};

struct PeerObservation final {
    PeerHandle peer = 0;
    bool choking = true;
    bool interested = false;
    double downloadBytesPerSecond = 0.0;
    double uploadBytesPerSecond = 0.0;
    std::uint32_t outstandingRequests = 0;
    std::uint64_t downloadedBytes = 0;
};

struct FailureObservation final {
    RequestOwnership ownership;
    PeerHandle peer = 0;
    BlockSpan block;
    std::string error;
    bool retryable = false;
};

struct ClosedObservation final {};

using TorrentObservation = std::variant<BlockObservation,
                                        PeerObservation,
                                        FailureObservation,
                                        ClosedObservation>;

struct TransportStatistics final {
    std::uint32_t connectedPeers = 0;
    std::uint32_t unchokedPeers = 0;
    std::uint32_t ownedRequestsOutstanding = 0;
    std::uint64_t downloadedBytes = 0;
    std::uint64_t uploadedBytes = 0;
    double downloadBytesPerSecond = 0.0;
    double uploadBytesPerSecond = 0.0;
    std::uint64_t pickerRequestsSuppressed = 0;
};

class TorrentTransport {
public:
    virtual ~TorrentTransport() = default;

    [[nodiscard]] virtual bool submit(const TorrentAction &action) = 0;
    [[nodiscard]] virtual std::vector<TorrentObservation> poll() = 0;
    [[nodiscard]] virtual TransportStatistics statistics() const = 0;
    [[nodiscard]] bool configureAutonomy(const discovery::AutonomyPolicy &policy)
    {
        return policy.externallyControlled() && applyAutonomySuppression();
    }
    virtual void close() = 0;
protected:
    [[nodiscard]] virtual bool applyAutonomySuppression() = 0;
};

} // namespace server1::ports
