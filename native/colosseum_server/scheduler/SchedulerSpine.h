#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace colosseum::server::scheduler {

class PieceBuffer final {
public:
    static constexpr std::size_t BlockSize = 16 * 1024;

    explicit PieceBuffer(std::size_t length);

    [[nodiscard]] std::size_t length() const noexcept { return length_; }
    [[nodiscard]] std::size_t parts() const noexcept { return parts_; }
    [[nodiscard]] std::size_t missing() const noexcept { return missing_; }
    [[nodiscard]] std::size_t bufferedParts() const noexcept { return buffered_; }
    [[nodiscard]] std::size_t blockSize(std::size_t index) const;
    [[nodiscard]] std::size_t blockOffset(std::size_t index) const noexcept;

    int reserve();
    void cancel(std::size_t index);
    bool set(std::size_t index, const std::vector<std::byte> &data);
    [[nodiscard]] std::vector<std::byte> flush();

private:
    void ensureInitialized();

    std::size_t length_ = 0;
    std::size_t parts_ = 0;
    std::size_t remainder_ = 0;
    std::size_t missing_ = 0;
    std::size_t buffered_ = 0;
    std::size_t reservations_ = 0;
    bool initialized_ = false;
    bool flushed_ = false;
    std::vector<std::size_t> cancellations_;
    std::vector<std::optional<std::vector<std::byte>>> blocks_;
};

struct Selection final {
    std::size_t from = 0;
    std::size_t to = 0;
    std::size_t offset = 0;
    int priority = 0;
    std::function<void()> notify;
    std::optional<std::size_t> selectTo;
    std::optional<std::size_t> readFrom;
    std::uint64_t serial = 0;
};

using SelectionHandle = std::shared_ptr<Selection>;

struct WireBlock final {
    std::size_t piece = 0;
    std::size_t offset = 0;
    std::size_t length = 0;
};

struct OutstandingRequest final {
    std::uint64_t id = 0;
    std::size_t streamPiece = 0;
    std::size_t blockIndex = 0;
    WireBlock wire;
    bool stolen = false;
    bool completed = false;
};

struct PeerState final {
    std::string id;
    bool peerChoking = true;
    bool peerInterested = false;
    bool amInterested = false;
    bool amChoking = true;
    bool isSeeder = false;
    std::uint64_t downloaded = 0;
    double downloadSpeed = 0.0;
    double uploadSpeed = 0.0;
    double salt = 0.0;
    std::vector<bool> peerPieces;
    std::vector<OutstandingRequest> requests;

    [[nodiscard]] std::size_t activeRequestCount() const noexcept;
};

struct CompletedPiece final {
    std::size_t piece = 0;
    std::vector<std::byte> bytes;
};

struct SwarmCapOptions final {
    std::size_t minPeers = 0;
    std::optional<double> maxSpeed;
    std::optional<double> maxBuffer;
};

class FloodPulse final {
public:
    static constexpr double DisabledPulse = 9007199254740991.0;

    void setFlood(std::uint64_t bytes, std::uint64_t downloaded) noexcept;
    void setPulse(double bytesPerSecond) noexcept;
    void setFloodedPulse(std::uint64_t bytes,
                         double bytesPerSecond,
                         std::uint64_t downloaded) noexcept;
    void flood() noexcept;
    [[nodiscard]] int updateDelayMs(std::uint64_t downloaded,
                                    double bytesPerSecond) const noexcept;

private:
    std::uint64_t floodThreshold_ = 0;
    double pulse_ = DisabledPulse;
};

struct RechokeResult final {
    std::vector<std::string> choked;
    std::vector<std::string> unchoked;
    std::optional<std::string> optimisticPeer;
};

class RechokePolicy final {
public:
    using OptimisticChooser = std::function<std::size_t(std::size_t)>;

    explicit RechokePolicy(std::size_t slots,
                           OptimisticChooser chooser = {});

    RechokeResult tick(std::vector<PeerState> &peers);
    [[nodiscard]] const std::optional<std::string> &optimisticPeer() const noexcept
    {
        return optimisticPeer_;
    }

private:
    std::size_t slots_ = 0;
    OptimisticChooser chooser_;
    std::optional<std::string> optimisticPeer_;
    int optimisticTime_ = 0;
};

class SchedulerSpine final {
public:
    using Observer = std::function<void()>;
    using InterestObserver = std::function<void(bool)>;
    using PieceMapper = std::function<std::size_t(std::size_t)>;
    using WireMapper = std::function<WireBlock(std::size_t,
                                               std::size_t,
                                               std::size_t)>;
    using HotswapObserver = std::function<void(const std::string &,
                                               const std::string &,
                                               std::size_t)>;

    explicit SchedulerSpine(std::vector<std::size_t> pieceLengths,
                            PieceMapper pieceMapper = {},
                            WireMapper wireMapper = {});

    [[nodiscard]] static int requestTarget(std::size_t unchokedPeers) noexcept;
    [[nodiscard]] static double averageBufferProgress(
        const std::vector<SelectionHandle> &selections) noexcept;
    [[nodiscard]] static bool shouldPauseSwarm(
        double downloadSpeed,
        std::size_t unchokedPeers,
        const std::vector<SelectionHandle> &selections,
        const SwarmCapOptions &options) noexcept;
    [[nodiscard]] static bool shouldDestroyChokedPeer(std::size_t queuedPeers,
                                                      std::size_t swarmSize,
                                                      std::size_t wireCount,
                                                      bool amInterested) noexcept;
    [[nodiscard]] static bool isSeeder(const std::vector<bool> &peerPieces,
                                       std::size_t torrentPieces) noexcept;

    SelectionHandle select(std::size_t from,
                           std::size_t to,
                           int priority,
                           std::function<void()> notify = {});
    SelectionHandle select(std::size_t from,
                           std::size_t to,
                           bool priority,
                           std::function<void()> notify = {});
    void deselect(const SelectionHandle &selection);
    void markPieceAvailable(std::size_t piece);
    void resetPiece(std::size_t piece);
    [[nodiscard]] bool isPieceAvailable(std::size_t piece) const;
    [[nodiscard]] bool isSelected(std::size_t piece) const noexcept;
    void refresh();

    void critical(std::size_t piece, std::size_t width = 1);
    [[nodiscard]] bool isCritical(std::size_t piece) const noexcept;

    void upsertPeer(PeerState peer);
    [[nodiscard]] PeerState *peer(const std::string &id) noexcept;
    [[nodiscard]] const PeerState *peer(const std::string &id) const noexcept;
    // A transport may opt into one bootstrap request while a peer is still
    // choking us. This lets the transport send the interested handshake; the
    // default preserves the normal unchoked-only scheduler policy.
    std::vector<OutstandingRequest> updatePeerRequests(
        const std::string &peerId, bool allowChoked = false);
    std::optional<CompletedPiece> completeRequest(
        std::uint64_t requestId,
        const std::vector<std::byte> &data);
    void failRequest(std::uint64_t requestId);

    void lockPiece(std::size_t piece);
    void unlockPiece(std::size_t piece);
    [[nodiscard]] bool isPieceLocked(std::size_t piece) const noexcept;
    [[nodiscard]] const std::vector<std::size_t> &lockedPieces() const noexcept
    {
        return lockedPieces_;
    }

    void setInterestObserver(InterestObserver observer)
    {
        interestObserver_ = std::move(observer);
    }
    void setIdleObserver(Observer observer) { idleObserver_ = std::move(observer); }
    void setHotswapObserver(HotswapObserver observer)
    {
        hotswapObserver_ = std::move(observer);
    }

    [[nodiscard]] const std::vector<SelectionHandle> &selections() const noexcept
    {
        return selections_;
    }
    [[nodiscard]] bool amInterested() const noexcept { return amInterested_; }
    [[nodiscard]] std::size_t pieceCount() const noexcept { return pieceLengths_.size(); }

private:
    static constexpr double SpeedThreshold = 3.0 * PieceBuffer::BlockSize;

    void collectGarbage();
    void updateInterest();
    void shufflePriority(std::size_t selectionIndex);
    bool trySelect(PeerState &peer,
                   bool hotswap,
                   std::vector<OutstandingRequest> &created);
    bool rankAllows(const PeerState &peer,
                    std::size_t piece,
                    std::size_t &tries,
                    std::size_t &peerCursor) const;
    bool requestBlock(PeerState &peer,
                      std::size_t piece,
                      bool allowHotswap,
                      std::vector<OutstandingRequest> &created);
    bool hotswap(PeerState &peer, std::size_t piece);
    [[nodiscard]] std::size_t unchokedPeerCount() const noexcept;
    [[nodiscard]] std::size_t mappedPiece(std::size_t streamPiece) const;
    [[nodiscard]] WireBlock mappedWireBlock(std::size_t streamPiece,
                                            std::size_t offset,
                                            std::size_t length) const;

    std::vector<std::size_t> pieceLengths_;
    std::vector<bool> available_;
    std::vector<bool> critical_;
    std::vector<std::unique_ptr<PieceBuffer>> pieces_;
    std::vector<std::vector<std::optional<std::string>>> blockOwners_;
    std::vector<SelectionHandle> selections_;
    std::vector<PeerState> peers_;
    std::vector<std::size_t> lockedPieces_;
    PieceMapper pieceMapper_;
    WireMapper wireMapper_;
    std::uint64_t nextSelectionSerial_ = 1;
    std::uint64_t nextRequestId_ = 1;
    bool amInterested_ = false;
    InterestObserver interestObserver_;
    Observer idleObserver_;
    HotswapObserver hotswapObserver_;
};

} // namespace colosseum::server::scheduler
