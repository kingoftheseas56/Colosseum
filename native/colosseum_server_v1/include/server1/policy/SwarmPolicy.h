#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace server1::policy {
struct PeerState final {
    std::string id; bool peerChoking = true; bool amChoking = true; bool amInterested = false;
    bool isSeeder = false; std::uint64_t downloadSpeed = 0; std::uint64_t uploadSpeed = 0;
    std::uint64_t salt = 0; std::optional<std::uint64_t> chokeDeadlineMs;
};
struct SwarmAction final { std::string peerId; bool choke = true; };
class SwarmPolicy final {
public:
    SwarmPolicy(std::size_t capacity, std::size_t uploads);
    void addPeer(PeerState peer);
    [[nodiscard]] const PeerState &peer(const std::string &id) const;
    void onChoke(const std::string &id, std::uint64_t nowMs);
    void onUnchoke(const std::string &id);
    [[nodiscard]] bool shouldDropChoked(const std::string &id, std::size_t queued,
                                        std::size_t connected, std::uint64_t nowMs) const;
    [[nodiscard]] bool rechokeDue(std::uint64_t nowMs) const noexcept;
    std::vector<SwarmAction> rechoke(std::uint64_t nowMs);
private:
    std::size_t capacity_; std::size_t uploads_; std::uint64_t lastRechokeMs_ = 0;
    std::map<std::string, PeerState> peers_;
};

[[nodiscard]] std::string generatePeerIdentity(std::uint64_t seed);
enum class PeerLifecycleState { Queued, Handshaking, Ready };
enum class SwarmTransportActionType { Connect, Disconnect, Request, Cancel };
struct SwarmTransportAction final {
    SwarmTransportActionType type = SwarmTransportActionType::Connect;
    std::string infoHash; std::string peer; std::uint64_t generation = 0;
    std::uint64_t requestId = 0; std::size_t piece = 0; std::size_t offset = 0;
    std::size_t length = 0; std::string reason;
};
class EngineSwarmRegistry final {
public:
    void start(std::string infoHash, std::uint64_t generation, std::uint64_t identitySeed = 0);
    bool stop(const std::string &infoHash);
    [[nodiscard]] bool live(const std::string &infoHash) const;
    [[nodiscard]] std::string peerIdentity(const std::string &infoHash) const;
    void recordDiscovery(const std::string &infoHash, std::string source);
    void scheduleTimer(const std::string &infoHash, std::uint64_t deadline);
    [[nodiscard]] std::vector<std::string> discovery(const std::string &infoHash) const;
    [[nodiscard]] std::uint64_t timer(const std::string &infoHash) const;
    bool queuePeer(const std::string &infoHash, std::string peer, std::uint64_t generation);
    bool connectPeer(const std::string &infoHash, const std::string &peer,
                     std::uint64_t generation, std::uint64_t nowMs);
    bool completeHandshake(const std::string &infoHash, const std::string &peer,
                           std::uint64_t generation, const std::string &remoteInfoHash);
    bool requestBlock(const std::string &infoHash, const std::string &peer,
                      std::uint64_t generation, std::uint64_t requestId,
                      std::size_t piece, std::size_t offset, std::size_t length,
                      std::uint64_t nowMs);
    bool completeRequest(const std::string &infoHash, std::uint64_t generation,
                         std::uint64_t requestId);
    void advance(std::uint64_t nowMs);
    [[nodiscard]] std::optional<PeerLifecycleState> peerState(
        const std::string &infoHash, const std::string &peer) const;
    std::vector<SwarmTransportAction> takeActions();
private:
    struct PeerEntry final { PeerLifecycleState state; std::uint64_t handshakeDeadlineMs = 0; };
    struct RequestEntry final { std::string peer; std::uint64_t generation; std::size_t piece;
        std::size_t offset; std::size_t length; std::uint64_t deadlineMs; };
    struct Entry final { std::uint64_t generation; std::string identity;
        std::vector<std::string> discovery; std::uint64_t timer = 0;
        std::map<std::string, PeerEntry> peers; std::map<std::uint64_t, RequestEntry> requests; };
    std::map<std::string, Entry> entries_;
    std::vector<SwarmTransportAction> actions_;
};
}
