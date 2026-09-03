#include "SchedulerSpine.h"
#include "FileStream.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace colosseum::server::scheduler;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class FakePieceStore final : public IFilePieceStore {
public:
    struct Pending final {
        ReadToken token = 0;
        std::size_t piece = 0;
        ReadCallback callback;
    };

    ReadToken readPiece(const std::size_t piece, ReadCallback callback) override
    {
        const auto token = nextToken++;
        pending.push_back({token, piece, std::move(callback)});
        return token;
    }

    void cancelRead(const ReadToken token) override
    {
        pending.erase(std::remove_if(pending.begin(), pending.end(),
            [token](const Pending &request) { return request.token == token; }),
            pending.end());
    }

    [[nodiscard]] std::size_t pendingCount() const noexcept
    {
        return pending.size();
    }

    void complete(const std::size_t piece, std::vector<std::byte> bytes)
    {
        const auto found = std::find_if(pending.begin(), pending.end(),
            [piece](const Pending &request) { return request.piece == piece; });
        require(found != pending.end(), "fake store completion must match a pending read");
        auto callback = std::move(found->callback);
        pending.erase(found);
        callback({}, std::move(bytes));
    }

    std::vector<Pending> pending;
    ReadToken nextToken = 1;
};

std::vector<std::byte> bytes(const std::string &text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const unsigned char ch : text) {
        result.push_back(static_cast<std::byte>(ch));
    }
    return result;
}

std::string text(const std::vector<std::byte> &value)
{
    std::string result;
    result.reserve(value.size());
    for (const auto ch : value) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(ch)));
    }
    return result;
}


void testPieceBufferMatchesModule853()
{
    PieceBuffer piece(32769);
    require(piece.parts() == 3, "32769 bytes must split into three 16 KiB blocks");
    require(piece.blockSize(0) == 16384, "first block must be 16 KiB");
    require(piece.blockSize(2) == 1, "final block must preserve the remainder");
    require(piece.reserve() == 0, "first reservation must be block 0");
    require(piece.reserve() == 1, "second reservation must be block 1");
    piece.cancel(0);
    require(piece.reserve() == 0, "cancelled block must be reserved again first");

    std::vector<std::byte> block0(16384, std::byte{0x11});
    std::vector<std::byte> block1(16384, std::byte{0x22});
    std::vector<std::byte> block2(1, std::byte{0x33});
    require(!piece.set(0, block0), "piece cannot complete after one block");
    require(!piece.set(1, block1), "piece cannot complete after two blocks");
    require(piece.set(2, block2), "piece must complete after every block arrives");
    require(piece.missing() == 0, "completed piece must have zero missing bytes");
    const auto bytes = piece.flush();
    require(bytes.size() == 32769, "flush must preserve exact piece length");
    require(piece.reserve() == -1, "flushed pieces cannot reserve more blocks");
}

void testRequestPressureMatchesModule816()
{
    require(SchedulerSpine::requestTarget(0) == 50, "zero unchoked peers maps to 50 requests");
    require(SchedulerSpine::requestTarget(1) == 50, "one unchoked peer maps to 50 requests");
    require(SchedulerSpine::requestTarget(30) == 5, "thirty unchoked peers maps to five requests");
    require(SchedulerSpine::requestTarget(100) == 5, "request floor remains five above thirty peers");
}

void testSelectionOrderingGcAndInterestMatchModule816()
{
    SchedulerSpine scheduler({16384, 16384, 16384, 16384});
    std::vector<bool> interestTransitions;
    int idleCount = 0;
    int lowNotify = 0;
    scheduler.setInterestObserver([&](bool interested) { interestTransitions.push_back(interested); });
    scheduler.setIdleObserver([&]() { ++idleCount; });

    const auto low = scheduler.select(0, 2, 0, [&]() { ++lowNotify; });
    const auto high = scheduler.select(3, 3, true);
    require(scheduler.selections().front() == high, "priority selections must sort before priority zero");
    require(interestTransitions == std::vector<bool>{true}, "first selection must enter interested state once");
    scheduler.deselect(high);

    scheduler.markPieceAvailable(0);
    require(low->offset == 1 && lowNotify == 1, "GC must advance and notify after piece 0 completes");
    scheduler.markPieceAvailable(1);
    require(low->offset == 2 && lowNotify == 2, "GC must advance and notify after piece 1 completes");
    scheduler.markPieceAvailable(2);
    require(scheduler.selections().empty(), "selection must leave the queue when its final piece completes");
    require(lowNotify == 3, "final completion must notify before selection retirement");
    require(interestTransitions == std::vector<bool>({true, false}), "last selection must leave interested state");
    require(idleCount == 1, "empty selection queue must emit idle once");
}

void testRequestFillAndVirtualMappingMatchModule816()
{
    SchedulerSpine scheduler(
        {32768},
        [](std::size_t) { return std::size_t{7}; },
        [](std::size_t, std::size_t offset, std::size_t length) {
            return WireBlock{3, 100 + offset, length};
        });
    scheduler.select(0, 0, true);

    PeerState peer;
    peer.id = "mapped";
    peer.peerChoking = false;
    peer.downloaded = 1;
    peer.downloadSpeed = 100000.0;
    peer.peerPieces.resize(8, false);
    peer.peerPieces[7] = true;
    scheduler.upsertPeer(peer);

    const auto requests = scheduler.updatePeerRequests("mapped");
    require(requests.size() == 2, "one 32 KiB piece must produce two 16 KiB requests");
    require(requests[0].streamPiece == 0 && requests[0].blockIndex == 0, "first reservation must target block zero");
    require(requests[0].wire.piece == 3 && requests[0].wire.offset == 100, "wire mapper must own virtual-to-verification coordinates");
    require(requests[1].wire.offset == 100 + 16384, "wire mapper must receive each streaming block offset");
}

void testHotswapStealsSlowReservationsMatchModule816()
{
    SchedulerSpine scheduler({32768});
    scheduler.select(0, 0, true);
    int hotswaps = 0;
    scheduler.setHotswapObserver([&](const std::string &slow, const std::string &fast, std::size_t piece) {
        require(slow == "slow" && fast == "fast" && piece == 0, "hotswap observer must identify both peers and piece");
        ++hotswaps;
    });

    PeerState slow;
    slow.id = "slow";
    slow.peerChoking = false;
    slow.downloaded = 1;
    slow.downloadSpeed = 20000.0;
    slow.peerPieces = {true};
    scheduler.upsertPeer(slow);
    const auto slowRequests = scheduler.updatePeerRequests("slow");
    require(slowRequests.size() == 2, "slow peer must reserve both blocks before the fast peer arrives");

    PeerState fast;
    fast.id = "fast";
    fast.peerChoking = false;
    fast.downloaded = 1;
    fast.downloadSpeed = 100000.0;
    fast.peerPieces = {true};
    scheduler.upsertPeer(fast);
    const auto fastRequests = scheduler.updatePeerRequests("fast");
    require(fastRequests.size() == 2, "fast peer must reclaim both blocks from a materially slower peer");
    require(hotswaps == 1, "one hotswap pass must release all reservations owned by the slow peer");
    const auto *slowAfter = scheduler.peer("slow");
    require(slowAfter && slowAfter->activeRequestCount() == 2, "stolen wire requests remain outstanding until their callbacks arrive");
    require(slowAfter->requests[0].stolen && slowAfter->requests[1].stolen, "stolen requests must be traceable without being cancelled at transport level");

    const auto first = scheduler.completeRequest(slowRequests[0].id, std::vector<std::byte>(16384, std::byte{0x11}));
    require(!first.has_value(), "first unique block cannot assemble the piece");
    const auto fastBlockOne = fastRequests[0].blockIndex == 1 ? fastRequests[0] : fastRequests[1];
    const auto assembled = scheduler.completeRequest(fastBlockOne.id, std::vector<std::byte>(16384, std::byte{0x22}));
    require(assembled && assembled->bytes.size() == 32768, "two unique blocks must assemble exact piece bytes");
    require(assembled->bytes.front() == std::byte{0x11} && assembled->bytes.back() == std::byte{0x22}, "duplicate hotswap race must not corrupt block order");
}

void testFileStreamMovingWindowAndOrderingMatchModule848()
{
    SchedulerSpine scheduler({4, 4, 4, 4});
    scheduler.markPieceAvailable(0);
    scheduler.markPieceAvailable(1);
    scheduler.markPieceAvailable(2);
    FakePieceStore store;

    FileStreamOptions options;
    options.start = 1;
    options.end = 8;
    options.bufferBytes = 8;
    FileStream stream(scheduler, store, FileSpan{1, 12}, 4, options);

    std::vector<std::byte> emitted;
    int ended = 0;
    stream.setChunkObserver([&](const std::vector<std::byte> &chunk) {
        emitted.insert(emitted.end(), chunk.begin(), chunk.end());
    });
    stream.setEndObserver([&]() { ++ended; });
    stream.start();

    require(store.pendingCount() == 2, "FileStream must cap store reads at two in flight");
    require(stream.inFlight() == 2, "two available pieces must be locked and reading");
    require(stream.selection()->readFrom == std::optional<std::size_t>{2}
            && stream.selection()->selectTo == std::optional<std::size_t>{2},
            "moving buffer window must advance from the next scheduled read, not the next emitted read");
    require(scheduler.isPieceLocked(0) && scheduler.isPieceLocked(1),
            "in-flight pieces must be protected from circular eviction");

    store.complete(1, bytes("EFGH"));
    require(emitted.empty(), "out-of-order read completion must wait for the earlier piece");
    require(store.pendingCount() == 2 && scheduler.isPieceLocked(2),
            "completing one read must advance the moving window without exceeding two reads");

    store.complete(0, bytes("ABCD"));
    require(text(emitted) == "CDEFGH",
            "first-piece slicing and ordered drain must preserve requested bytes");
    require(!scheduler.isPieceLocked(0) && !scheduler.isPieceLocked(1),
            "completed store reads must release their eviction locks");

    store.complete(2, bytes("IJKL"));
    require(text(emitted) == "CDEFGHIJ", "EOF slice must stop at the inclusive requested end");
    require(ended == 1 && stream.ended(), "exact final byte must end the stream once");
    require(stream.remaining() == 0 && stream.inFlight() == 0,
            "completed FileStream must have no missing bytes or active reads");
    require(!scheduler.isPieceLocked(2), "final read must release its locked piece");
    require(scheduler.selections().empty(), "FileStream completion must deselect its scheduler range");
}

void testFileStreamWaitsForMissingPieceMatchModule848()
{
    SchedulerSpine scheduler({4, 4});
    FakePieceStore store;
    FileStream stream(scheduler, store, FileSpan{0, 4}, 4);
    std::vector<std::byte> emitted;
    stream.setChunkObserver([&](const std::vector<std::byte> &chunk) { emitted = chunk; });
    stream.start();

    require(store.pendingCount() == 0 && stream.waiting(),
            "missing piece must wait instead of producing premature EOF");
    require(scheduler.isCritical(0), "blocking read head must mark its critical window");

    scheduler.markPieceAvailable(0);
    require(store.pendingCount() == 1 && !stream.waiting(),
            "piece notification must resume the blocked FileStream read");
    store.complete(0, bytes("WXYZ"));
    require(text(emitted) == "WXYZ" && stream.ended(),
            "resumed missing-piece read must deliver bytes and cleanly reach EOF");
}

void testFileStreamDestroyReleasesLocks()
{
    SchedulerSpine scheduler({4});
    scheduler.markPieceAvailable(0);
    FakePieceStore store;
    FileStream stream(scheduler, store, FileSpan{0, 4}, 4);
    stream.start();
    require(scheduler.isPieceLocked(0), "active read must lock its piece");
    stream.destroy();
    require(stream.destroyed() && !scheduler.isPieceLocked(0),
            "destroy must release every outstanding FileStream eviction lock");
    require(scheduler.selections().empty(), "destroy must deselect the FileStream range");
}


void testSeederChokeAndRechokeMatchModule816()
{
    require(SchedulerSpine::isSeeder({true, true, true}, 3),
            "complete peer bitfield must classify as a seeder");
    require(!SchedulerSpine::isSeeder({true, false, true}, 3),
            "a missing piece must keep a peer out of seeder state");

    SchedulerSpine scheduler({4, 4});
    PeerState detected;
    detected.id = "detected-seed";
    detected.peerPieces = {true, true};
    scheduler.upsertPeer(detected);
    require(scheduler.peer("detected-seed")->isSeeder,
            "scheduler peer insertion must derive seeder state from the torrent bitfield");

    require(SchedulerSpine::shouldDestroyChokedPeer(7, 5, 2, true),
            "choked interested peers must be culled when the queue exceeds twice open swarm slots");
    require(!SchedulerSpine::shouldDestroyChokedPeer(6, 5, 2, true),
            "choke timeout pressure uses strict greater-than at the queue threshold");
    require(!SchedulerSpine::shouldDestroyChokedPeer(99, 5, 2, false),
            "uninterested peers are not destroyed by the interested choke-timeout rule");

    std::size_t chooserCalls = 0;
    RechokePolicy policy(1, [&](const std::size_t) { return chooserCalls++; });
    std::vector<PeerState> peers;
    PeerState seed;
    seed.id = "seed";
    seed.isSeeder = true;
    seed.amChoking = false;
    peers.push_back(seed);

    for (const auto &[id, speed] :
         std::vector<std::pair<std::string, double>>{{"fast", 100.0},
                                                     {"mid", 50.0},
                                                     {"slow", 10.0}}) {
        PeerState peer;
        peer.id = id;
        peer.peerInterested = true;
        peer.downloadSpeed = speed;
        peer.amChoking = true;
        peers.push_back(peer);
    }

    const auto first = policy.tick(peers);
    require(peers[0].amChoking,
            "rechoke must always choke seeders");
    require(!peers[1].amChoking,
            "fastest interested leecher must receive the regular unchoke slot");
    require(first.optimisticPeer == std::optional<std::string>{"mid"}
            && !peers[2].amChoking,
            "first eligible remainder peer must receive the optimistic slot");

    policy.tick(peers);
    policy.tick(peers);
    const auto rotated = policy.tick(peers);
    require(rotated.optimisticPeer == std::optional<std::string>{"slow"},
            "optimistic unchoke must rotate after the three-tick hold window");
    require(peers[2].amChoking && !peers[3].amChoking,
            "expired optimistic peer must be rechoked when the replacement wins");
    require(!peers[1].amChoking,
            "regular fastest-peer slot must survive optimistic rotation");
}

void testSwarmGovernorMatchesModule172()
{
    SchedulerSpine scheduler({16384, 16384, 16384, 16384, 16384, 16384, 16384});
    auto selection = scheduler.select(0, 6, true);
    selection->readFrom = 2;
    selection->selectTo = 6;
    selection->offset = 5;

    SwarmCapOptions speedCap;
    speedCap.minPeers = 5;
    speedCap.maxSpeed = 100.0;
    require(SchedulerSpine::shouldPauseSwarm(101.0, 6, scheduler.selections(), speedCap), "speed cap must pause above the threshold with more than minPeers");
    require(!SchedulerSpine::shouldPauseSwarm(101.0, 5, scheduler.selections(), speedCap), "minPeers comparison must be strict greater-than");

    SwarmCapOptions both;
    both.minPeers = 5;
    both.maxSpeed = 100.0;
    both.maxBuffer = 0.75;
    require(!SchedulerSpine::shouldPauseSwarm(1000.0, 6, scheduler.selections(), both), "maxBuffer must replace maxSpeed when both are configured");
    selection->offset = 6;
    require(SchedulerSpine::shouldPauseSwarm(1.0, 6, scheduler.selections(), both), "buffer progress above 0.75 must pause regardless of speed");
}

void testFloodPulseMatchesModule816()
{
    FloodPulse governor;
    governor.setFloodedPulse(1000, 2000.0, 500);
    require(governor.updateDelayMs(1499, 5000.0) == 0, "flood threshold is relative to current downloaded bytes");
    require(governor.updateDelayMs(1500, 2000.0) == 0, "pulse comparison must be strict greater-than");
    require(governor.updateDelayMs(1500, 2001.0) == 500, "flooded over-pulse updates must debounce for 500 ms");
    governor.flood();
    require(governor.updateDelayMs(999999, 999999.0) == 0, "flood() must disable pulse throttling");
}

} // namespace
int main()
{
    try {
        testPieceBufferMatchesModule853();
        testRequestPressureMatchesModule816();
        testSelectionOrderingGcAndInterestMatchModule816();
        testRequestFillAndVirtualMappingMatchModule816();
        testHotswapStealsSlowReservationsMatchModule816();
        testFileStreamMovingWindowAndOrderingMatchModule848();
        testFileStreamWaitsForMissingPieceMatchModule848();
        testFileStreamDestroyReleasesLocks();
        testSeederChokeAndRechokeMatchModule816();
        testSwarmGovernorMatchesModule172();
        testFloodPulseMatchesModule816();
        std::cout << "SCHEDULER_SPINE_OK\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "SCHEDULER_SPINE_FAIL: " << error.what() << '\n';
        return 1;
    }
}
