#pragma once

#include "server1/policy/SchedulerActions.h"
#include "server1/discovery/PeerSearch.h"

#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <utility>

namespace server1::ports {

inline constexpr std::uint32_t kWireBlockLength = 16384;
using PeerHandle = std::uint64_t;
using EngineGeneration = std::uint64_t;
using V1InfoHash = std::array<std::uint8_t, 20>;

struct InfoHashSource final {};

struct MagnetSource final {
    std::string uri;
};

struct MetainfoSource final {
    std::vector<std::uint8_t> bytes;
};

using TorrentSource = std::variant<InfoHashSource, MagnetSource, MetainfoSource>;

struct TorrentOpenRequest final {
    TorrentOpenRequest() = delete;
    TorrentOpenRequest(EngineGeneration engineGeneration,
                       V1InfoHash canonicalInfoHash,
                       TorrentSource torrentSource,
                       std::string destination)
        : generation(engineGeneration),
          infoHash(std::move(canonicalInfoHash)),
          source(std::move(torrentSource)),
          savePath(std::move(destination))
    {}

    EngineGeneration generation;
    V1InfoHash infoHash;
    TorrentSource source;
    std::string savePath;
};

struct RequestOwnership final {
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    policy::SelectionId selectionId = 0;
};

struct BlockSpan final {
    std::uint32_t piece = 0;
    std::uint32_t blockOrdinal = 0;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

[[nodiscard]] constexpr bool isValidBlock(const BlockSpan &block) noexcept
{
    return block.length > 0 && block.length <= kWireBlockLength
        && block.offset % kWireBlockLength == 0
        && static_cast<std::uint64_t>(block.blockOrdinal) * kWireBlockLength
            == block.offset;
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

struct ConnectAction final {
    EngineGeneration generation = 0;
    PeerHandle peer = 0;
    std::string address;
    std::uint16_t port = 0;
};

// Generation-bound scheduler pressure signal. Only the current open
// generation may change this state; stale generations are rejected. Repeating
// the current state is successful and has no additional effect.
//
// Pausing gates and defers only new outbound ConnectAction work. Existing
// peers, owned requests, metadata exchange, and incoming connections continue.
// Resuming drains the deferred outbound connects. Implementations must not map
// this scheduler signal to torrent_handle::pause().
struct PauseAction final {
    EngineGeneration generation = 0;
    bool paused = false;
};

using TorrentAction = std::variant<RequestAction,
                                   CancelAction,
                                   InterestAction,
                                   ChokeAction,
                                   ConnectAction,
                                   PauseAction>;

[[nodiscard]] inline std::optional<TorrentAction>
toTorrentAction(const policy::SchedulerAction &action)
{
    const RequestOwnership ownership{action.request.requestId,
                                     action.request.generation,
                                     action.request.selectionId};
    if (action.request.piece > UINT32_MAX || action.request.block > UINT32_MAX
        || action.request.offset > UINT32_MAX
        || action.request.length > UINT32_MAX)
        return std::nullopt;
    const BlockSpan block{static_cast<std::uint32_t>(action.request.piece),
                          static_cast<std::uint32_t>(action.request.block),
                          static_cast<std::uint32_t>(action.request.offset),
                          static_cast<std::uint32_t>(action.request.length)};
    if (!isValidBlock(block))
        return std::nullopt;
    switch (action.type) {
    case policy::SchedulerActionType::Request:
        return TorrentAction{RequestAction{ownership, action.request.peer, block}};
    case policy::SchedulerActionType::Cancel:
        return TorrentAction{CancelAction{ownership, action.request.peer, block,
                                          action.requestWireCancel}};
    case policy::SchedulerActionType::Update:
    case policy::SchedulerActionType::Select:
        return std::nullopt;
    }
    return std::nullopt;
}

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

struct MetadataReadyObservation final {
    EngineGeneration generation = 0;
    V1InfoHash infoHash{};
    std::vector<std::uint8_t> infoSection;
    // Active values at readiness, each lexicographically sorted and deduplicated.
    std::vector<std::string> trackers;
    std::vector<std::string> urlSeeds;
};

struct SourceFailureObservation final {
    EngineGeneration generation = 0;
    V1InfoHash infoHash{};
    std::string error;
    bool retryable = false;
};

// Full, sorted, deduplicated snapshot advertised by one peer. An empty
// snapshot clears all previously advertised pieces for that peer.
struct AvailablePiecesObservation final {
    EngineGeneration generation = 0;
    PeerHandle peer = 0;
    std::vector<std::uint32_t> pieces;
};

using TorrentObservation = std::variant<BlockObservation,
                                        PeerObservation,
                                        FailureObservation,
                                        ClosedObservation,
                                        MetadataReadyObservation,
                                        SourceFailureObservation,
                                        AvailablePiecesObservation>;

struct TransportStatistics final {
    std::uint32_t connectedPeers = 0;
    std::uint32_t unchokedPeers = 0;
    std::uint32_t ownedRequestsOutstanding = 0;
    std::uint64_t downloadedBytes = 0;
    std::uint64_t uploadedBytes = 0;
    double downloadBytesPerSecond = 0.0;
    double uploadBytesPerSecond = 0.0;
    std::uint64_t pickerRequestsSuppressed = 0;
    bool paused = false;
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

[[nodiscard]] std::unique_ptr<TorrentTransport>
openTorrentTransport(const TorrentOpenRequest &request) noexcept;

} // namespace server1::ports
