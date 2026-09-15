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
inline constexpr std::uint32_t kMaxOutstandingUploadsPerPeer = 4;
inline constexpr std::uint32_t kMaxOutstandingUploadsGlobal = 20;
using PeerHandle = std::uint64_t;
using EngineGeneration = std::uint64_t;
using UploadRequestId = std::uint64_t;
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

// The adapter allocates an identifier only after an inbound upload request is
// admissible. It is nonzero and increases monotonically within one open generation.
// A later retry receives a new request identifier even when its block is unchanged.
struct UploadOwnership final {
    UploadRequestId requestId = 0;
    EngineGeneration generation = 0;
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

// Only the current generation, after metadata readiness and within its exact
// piece count, may advertise a piece. Advertisement is an
// upstream commit assertion only: it asserts an upstream commit; it does not read or verify storage.
// The advertised set is monotonic and idempotent within that generation and is
// replayed to every newly attached peer. Generation replacement clears it.
struct AdvertisePieceAction final {
    EngineGeneration generation = 0;
    std::uint32_t piece = 0;
};

// A response must match exact peer, block, and generation ownership and its
// payload size exactly equals the owned BlockSpan length. K11 must call
// PersistentPieceStore::isCommitted before reading persistent bytes. Circular-cache upload is forbidden.
struct UploadResponseAction final {
    UploadOwnership ownership;
    PeerHandle peer = 0;
    BlockSpan block;
    std::vector<std::uint8_t> payload;
};

// Abort rejects the exact still-live request. For one ownership record,
// exactly one successful response or abort terminalizes the upload.
struct UploadAbortAction final {
    UploadOwnership ownership;
    PeerHandle peer = 0;
    BlockSpan block;
};

using TorrentAction = std::variant<RequestAction,
                                   CancelAction,
                                   InterestAction,
                                   ChokeAction,
                                   ConnectAction,
                                   PauseAction,
                                   AdvertisePieceAction,
                                   UploadResponseAction,
                                   UploadAbortAction>;

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

// The adapter accepts an inbound request only from a private live connection identity
// whose peer is interested and locally unchoked. The requested block must be
// aligned and no larger than kWireBlockLength, remain within the exact final-piece bounds,
// and name a piece advertised as committed. on_request and on_cancel are intercepted and swallowed;
// they never enter libtorrent's default disk request queue. Duplicate requests for one live connection identity and BlockSpan
// are suppressed. At most kMaxOutstandingUploadsPerPeer requests per peer and
// kMaxOutstandingUploadsGlobal requests overall may be live. Rejected requests
// do not allocate an identifier or emit this observation.
struct UploadRequestObservation final {
    UploadOwnership ownership;
    PeerHandle peer = 0;
    BlockSpan block;
};

// Remote cancellation, peer detach, generation replacement, and close terminalize
// each live upload with exactly one UploadCancelObservation. A later response or abort
// for a terminal ownership record is rejected without a peer write.
struct UploadCancelObservation final {
    UploadOwnership ownership;
    PeerHandle peer = 0;
    BlockSpan block;
};

using TorrentObservation = std::variant<BlockObservation,
                                        PeerObservation,
                                        FailureObservation,
                                        ClosedObservation,
                                        MetadataReadyObservation,
                                        SourceFailureObservation,
                                        AvailablePiecesObservation,
                                        UploadRequestObservation,
                                        UploadCancelObservation>;

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
    // Submit is a mailbox boundary: the caller thread only enqueues a mailbox command;
    // network tick performs all peer-connection writes. ownedUploadsOutstanding is a current gauge.
    // uploadRequestsAccepted counts observations enqueued.
    // uploadRequestsRejected counts intercepted requests denied before observation.
    // uploadResponsesFramed counts full piece frames handed to a live connection.
    // uploadRequestsCancelled counts cancel observations enqueued.
    // uploadAbortsFramed counts reject frames handed to a live connection.
    // All five upload counters are cumulative event counters.
    // uploadedBytes and uploadBytesPerSecond remain native libtorrent measurements and
    // must not be synthesized from upload actions or payload sizes.
    std::uint32_t ownedUploadsOutstanding = 0;
    std::uint64_t uploadRequestsAccepted = 0;
    std::uint64_t uploadRequestsRejected = 0;
    std::uint64_t uploadResponsesFramed = 0;
    std::uint64_t uploadRequestsCancelled = 0;
    std::uint64_t uploadAbortsFramed = 0;
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
