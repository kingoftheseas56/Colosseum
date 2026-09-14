#include "server1/policy/MetadataExchange.h"
#include "server1/policy/SwarmPolicy.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using server1::policy::ByteVector;
using server1::policy::EngineSwarmRegistry;
using server1::policy::MetadataExchange;
using server1::policy::PeerState;
using server1::policy::SwarmPolicy;
using server1::policy::PeerLifecycleState;
using server1::policy::SwarmTransportActionType;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void caseK05_01()
{
    MetadataExchange exchange("89c6c711b638a14f58d11e99b6ed1c131d7f8ab4");
    require(exchange.advertise(32769), "valid metadata size is accepted");
    require(exchange.pendingRequests() == std::vector<std::size_t>({0, 1, 2}),
            "metadata is requested in 16 KiB chunks");

    require(!exchange.receive(2, ByteVector(1, 'a')), "tail alone is incomplete");
    require(!exchange.receive(0, ByteVector(16384, 'a')), "out-of-order prefix remains incomplete");
    require(!exchange.receive(0, ByteVector(16384, 'a')), "a repeated chunk is harmless");
    require(exchange.receive(1, ByteVector(16384, 'a')), "all valid chunks complete metadata");
    require(exchange.metadata().has_value() && exchange.metadata()->size() == 32769,
            "validated metadata is retained exactly");

    MetadataExchange mismatch("0000000000000000000000000000000000000000");
    require(mismatch.advertise(16384), "hash mismatch fixture advertises");
    require(!mismatch.receive(0, ByteVector(16384, 'x')), "hash mismatch is rejected");
    require(mismatch.rejections() == 1 && mismatch.pendingRequests().size() == 1,
            "hash mismatch clears the chunk set for retry");
    require(!mismatch.receive(-1, ByteVector(1, 'x')), "negative metadata piece is ignored");
    require(!mismatch.advertise(MetadataExchange::kMaxMetadataSize + 1),
            "metadata above 4 MiB is refused");
    std::cout << "K05-01 PASS\n";
}

void caseK05_02()
{
    SwarmPolicy swarm(100, 5);
    swarm.addPeer(PeerState{"seed", true, true, true, true, 9000, 0, 1});
    swarm.addPeer(PeerState{"fast", true, true, true, false, 8000, 100, 2});
    swarm.addPeer(PeerState{"slow", true, true, true, false, 1000, 500, 3});

    swarm.onChoke("slow", 1000);
    require(!swarm.shouldDropChoked("slow", 191, 3, 5999),
            "choke timeout does not fire before 5 seconds");
    require(swarm.shouldDropChoked("slow", 195, 3, 6000),
            "5 second choke check uses strict queued pressure");
    swarm.onUnchoke("slow");
    require(!swarm.shouldDropChoked("slow", 195, 3, 7000),
            "unchoke cancels the choke check");

    require(!swarm.rechokeDue(9999) && swarm.rechokeDue(10000),
            "rechoke interval is exactly 10 seconds");
    SwarmPolicy zero(100, 0);
    zero.addPeer(PeerState{"only", true, true, true, false, 1, 1, 1});
    const auto zeroUpload = zero.rechoke(10000);
    require(zeroUpload.empty(), "configured zero upload slots never unchoke a peer");
    require(swarm.peer("seed").amChoking, "seeded peers remain choked");
    const auto actions = swarm.rechoke(20000);
    require(actions.size() == 2 && actions[0].peerId == "fast" && !actions[0].choke
                && actions[1].peerId == "slow" && !actions[1].choke,
            "source ordering unchokes the ranked peer then one optimistic interested peer");
    std::cout << "K05-02 PASS\n";
}

void caseK05_03()
{
    EngineSwarmRegistry registry;
    registry.start("aaaaaaaa", 10, 1);
    registry.start("bbbbbbbb", 20, 2);
    require(registry.peerIdentity("aaaaaaaa").size() == 20
                && registry.peerIdentity("aaaaaaaa")[0] == '-'
                && registry.peerIdentity("aaaaaaaa") != registry.peerIdentity("bbbbbbbb"),
            "source-shaped peer identities are generated per engine");
    registry.recordDiscovery("aaaaaaaa", "tracker:a");
    registry.recordDiscovery("bbbbbbbb", "dht:b");
    registry.scheduleTimer("aaaaaaaa", 5000);
    registry.scheduleTimer("bbbbbbbb", 10000);

    require(registry.stop("aaaaaaaa"), "first torrent stops");
    require(!registry.live("aaaaaaaa") && registry.live("bbbbbbbb"),
            "stopping one torrent leaves the other live");
    require(registry.discovery("bbbbbbbb") == std::vector<std::string>({"dht:b"})
                && registry.timer("bbbbbbbb") == 10000,
            "discovery and timer state remain isolated by infohash");
    require(registry.discovery("aaaaaaaa").empty() && registry.timer("aaaaaaaa") == 0,
            "stopped torrent state is removed");

    require(registry.queuePeer("bbbbbbbb", "peer-1", 20)
                && !registry.queuePeer("bbbbbbbb", "peer-1", 20),
            "peer lifecycle queues once in the owning generation");
    require(!registry.connectPeer("bbbbbbbb", "peer-1", 19, 0)
                && registry.connectPeer("bbbbbbbb", "peer-1", 20, 0),
            "transport seam rejects stale generation and emits current connect");
    require(registry.peerState("bbbbbbbb", "peer-1") == PeerLifecycleState::Handshaking,
            "queued peer becomes handshaking");
    require(registry.completeHandshake("bbbbbbbb", "peer-1", 20, "bbbbbbbb"),
            "matching handshake makes peer ready");
    require(registry.requestBlock("bbbbbbbb", "peer-1", 20, 7, 3, 0, 16384, 100)
                && !registry.completeRequest("bbbbbbbb", 19, 7),
            "block request is generation-bound");
    registry.advance(30100);
    auto actions = registry.takeActions();
    require(actions.size() == 3 && actions[0].type == SwarmTransportActionType::Connect
                && actions[1].type == SwarmTransportActionType::Request
                && actions[2].type == SwarmTransportActionType::Cancel
                && actions[2].reason == "request timeout",
            "connect/request/cancel actions cross the transport seam in causal order");
    require(registry.queuePeer("bbbbbbbb", "peer-timeout", 20)
                && registry.connectPeer("bbbbbbbb", "peer-timeout", 20, 40000),
            "second peer enters handshake timeout path");
    registry.advance(50000);
    actions = registry.takeActions();
    require(actions.size() == 2 && actions.back().type == SwarmTransportActionType::Disconnect
                && actions.back().reason == "handshake timeout",
            "handshake timeout is an explicit transport disconnect");
    std::cout << "K05-03 PASS\n";
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const std::string requested = argc > 1 ? argv[1] : "all";
        if (requested == "all" || requested == "K05-01")
            caseK05_01();
        if (requested == "all" || requested == "K05-02")
            caseK05_02();
        if (requested == "all" || requested == "K05-03")
            caseK05_03();
        if (requested != "all" && requested != "K05-01" && requested != "K05-02"
            && requested != "K05-03")
            throw std::runtime_error("unknown K05 case");
    } catch (const std::exception &error) {
        std::cerr << "K05 FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
