#include "server1/ports/TorrentTransport.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace lt = libtorrent;

namespace server1::transport {
std::unique_ptr<ports::TorrentTransport> makeLibTorrent2Adapter(
    const std::string &torrentPath, const std::string &savePath);
bool connectPeer(ports::TorrentTransport &, ports::PeerHandle,
                 const std::string &, std::uint16_t);
bool forbiddenCrossThreadNativeAccessIsRejected(ports::TorrentTransport &);
std::uint64_t framedRequestCount(const ports::TorrentTransport &);
std::uint64_t ownedNativeAddCount(const ports::TorrentTransport &);
std::uint64_t autonomousNativeMutationCount(const ports::TorrentTransport &);
std::uint64_t forbiddenNativeAttemptCount(const ports::TorrentTransport &);
std::uint64_t staleDisconnectIgnoredCount(const ports::TorrentTransport &);
bool armReceiveCallbackBarrier(ports::TorrentTransport &, const std::string &, const std::string &);
bool closeHasStarted(const ports::TorrentTransport &);
ports::PeerHandle boundEndpointOwner(const ports::TorrentTransport &, const std::string &, std::uint16_t);
}

namespace {
using namespace server1::ports;

[[noreturn]] void fail(const std::string &message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string &message)
{
    if (!condition) fail(message);
}

void prepare(const fs::path &directory, char fill = 'K')
{
    fs::create_directories(directory);
    const auto payload = directory / "K10-wire.bin";
    std::ofstream output(payload, std::ios::binary | std::ios::trunc);
    output << std::string(32768, fill);
    output.close();
    lt::file_storage storage;
    storage.add_file("K10-wire.bin", 32768);
    lt::create_torrent creator(storage, 16384);
    lt::set_piece_hashes(creator, directory.string());
    std::vector<char> encoded;
    lt::bencode(std::back_inserter(encoded), creator.generate());
    std::ofstream torrent(directory / "K10-wire.torrent", std::ios::binary | std::ios::trunc);
    torrent.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    torrent.close();
    lt::torrent_info info(encoded.data(), static_cast<int>(encoded.size()));
    std::ofstream(directory / "info_hash.txt") << info.info_hashes().v1 << '\n';
    std::ofstream(directory / "dependency_identity.txt")
        << "linked_runtime_version=" << lt::version() << '\n'
        << "archive_sha256=a2d67f24710303750aaf068d1945380d95434e4791ad611b68b0b3b055d89a30\n";
}

std::vector<TorrentObservation> pollUntil(TorrentTransport &transport,
    const std::function<bool(const std::vector<TorrentObservation>&)> &done,
    std::chrono::seconds timeout = std::chrono::seconds(8))
{
    std::vector<TorrentObservation> all;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto next = transport.poll();
        all.insert(all.end(), std::make_move_iterator(next.begin()),
                   std::make_move_iterator(next.end()));
        if (done(all)) return all;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return all;
}

void caseWire(const fs::path &directory, int portA, int portB)
{
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "download").string());
    expect(bool(transport), "K10-01 production adapter factory failed");
    expect(transport->configureAutonomy({}), "K10-01 external-control suppression rejected");
    const RequestAction request{{41, 7, 3}, 77, {0, 0, 0, 16384}};
    expect(transport->submit(request), "K10-01 owned request rejected");
    expect(transport->submit(InterestAction{77, true})
               && transport->submit(InterestAction{76, true}),
           "K10-01 scheduler interest actions rejected");
    expect(server1::transport::connectPeer(*transport, 77, "127.0.0.1",
                                           static_cast<std::uint16_t>(portB)),
           "K10-01 selected peer connection rejected");
    const auto selectedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < selectedDeadline
           && transport->statistics().connectedPeers < 1)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    expect(server1::transport::connectPeer(*transport, 76, "127.0.0.1",
                                           static_cast<std::uint16_t>(portA)),
           "K10-01 readiness peer connection rejected");
    const auto observations = pollUntil(*transport, [](const auto &items) {
        for (const auto &item : items)
            if (std::holds_alternative<BlockObservation>(item)) return true;
        return false;
    });
    int blocks = 0;
    for (const auto &item : observations) {
        if (const auto *block = std::get_if<BlockObservation>(&item)) {
            ++blocks;
            expect(block->ownership.requestId == 41 && block->ownership.generation == 7
                       && block->ownership.selectionId == 3,
                   "K10-01 ownership changed across native wire");
            expect(block->peer == 77 && block->block.piece == 0 && block->block.offset == 0
                       && block->block.length == 16384 && block->payload.size() == 16384,
                   "K10-01 block identity/payload mismatch");
        }
    }
    const auto stats = transport->statistics();
    if (blocks != 1) {
        std::cerr << "K10-01 diagnostics connected=" << stats.connectedPeers
                  << " outstanding=" << stats.ownedRequestsOutstanding
                  << " suppressed=" << stats.pickerRequestsSuppressed
                  << " observations=" << observations.size() << '\n';
        for (const auto &item : observations)
            if (const auto *failure = std::get_if<FailureObservation>(&item))
                std::cerr << "failure=" << failure->error << " peer=" << failure->peer
                          << " piece=" << failure->block.piece << " offset=" << failure->block.offset
                          << " length=" << failure->block.length << '\n';
    }
    expect(blocks == 1, "K10-01 expected one terminal block observation");
    expect(stats.ownedRequestsOutstanding == 0 && stats.downloadedBytes >= 16384
               && stats.pickerRequestsSuppressed == 0,
           "K10-01 native accounting did not settle");
    expect(server1::transport::ownedNativeAddCount(*transport) == 1
               && server1::transport::framedRequestCount(*transport) == 1
               && server1::transport::autonomousNativeMutationCount(*transport) == 0,
           "K10-01 queued/framed/autonomous counters mismatch");
    expect(!transport->submit(request), "K10-01 one-shot ownership replay must be rejected");
    expect(server1::transport::forbiddenCrossThreadNativeAccessIsRejected(*transport),
           "K10-03 caller-thread access guard must reject after native callbacks");
    transport->close();
    const auto closed = transport->poll();
    expect(std::count_if(closed.begin(), closed.end(), [](const auto &item) {
               return std::holds_alternative<ClosedObservation>(item);
           }) == 1, "K10-01 close must emit exactly once");
    std::cout << "K10-01 PASS linked_runtime=" << lt::version()
              << " owned=41/7/3 peer=77 piece=0 offset=0 length=16384"
              << " queued=1 framed=1 autonomous_mutations=0 replay_rejected=1\n";
}

void caseLifecycle(const fs::path &directory, int portA, int portB)
{
    const auto entered = directory / "on-piece.entered";
    const auto release = directory / "on-piece.release";
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "lifecycle-download").string());
    expect(bool(transport) && transport->configureAutonomy({}), "K10-02 lifecycle setup");
    expect(server1::transport::armReceiveCallbackBarrier(*transport, entered.string(), release.string()),
           "K10-02 receive callback barrier rejected");
    const RequestAction request{{52, 4, 9}, 77, {0, 0, 0, 16384}};
    expect(transport->submit(request), "K10-02 lifecycle request rejected");
    expect(transport->submit(InterestAction{77, true})
               && transport->submit(InterestAction{76, true}),
           "K10-02 lifecycle interest rejected");
    // Reverse the K10-01 connection order: the selected peer advertises
    // second, so readiness cannot depend on a specific callback order.
    expect(server1::transport::connectPeer(*transport, 76, "127.0.0.1",
                                           static_cast<std::uint16_t>(portA)),
           "K10-02 lifecycle readiness peer rejected");
    const auto selectedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < selectedDeadline
           && transport->statistics().connectedPeers < 1)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    expect(server1::transport::connectPeer(*transport, 77, "127.0.0.1",
                                           static_cast<std::uint16_t>(portB)),
           "K10-02 lifecycle selected peer rejected");
    const auto enteredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < enteredDeadline && !fs::exists(entered))
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    expect(fs::exists(entered), "K10-02 on_piece callback did not enter barrier");
    expect(server1::transport::ownedNativeAddCount(*transport) == 1
               && server1::transport::framedRequestCount(*transport) == 1,
           "K10-02 lifecycle request was not queued and framed exactly once");

    std::thread closer([&] { transport->close(); });
    const auto closeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < closeDeadline
           && !server1::transport::closeHasStarted(*transport))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(server1::transport::closeHasStarted(*transport),
           "K10-02 close did not overlap the active on_piece callback");
    std::ofstream(release, std::ios::trunc).close();
    closer.join();

    expect(!transport->submit(request), "K10-02 lifecycle replay after close must fail");
    const auto terminal = transport->poll();
    expect(std::count_if(terminal.begin(), terminal.end(), [](const auto &item) {
               return std::holds_alternative<FailureObservation>(item);
           }) == 1, "K10-02 stop during on_piece must terminalize once");
    expect(std::count_if(terminal.begin(), terminal.end(), [](const auto &item) {
               return std::holds_alternative<BlockObservation>(item);
           }) == 0, "K10-02 late on_piece must not complete after stop");
    expect(std::count_if(terminal.begin(), terminal.end(), [](const auto &item) {
               return std::holds_alternative<ClosedObservation>(item);
           }) == 1, "K10-02 stop during on_piece must close once");
    expect(transport->statistics().ownedRequestsOutstanding == 0,
           "K10-02 lifecycle accounting did not settle");
    std::cout << "K10-02 LIFECYCLE PASS stop_during_on_piece=1 late_completion=0"
              << " replay=0 queued=1 framed=1\n";
}

void caseLedger(const fs::path &directory)
{
    const auto firstTorrent = directory / "first";
    const auto secondTorrent = directory / "second";
    prepare(firstTorrent, 'K');
    prepare(secondTorrent, 'Z');
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (firstTorrent / "K10-wire.torrent").string(), (directory / "unit-download").string());
    expect(bool(transport) && transport->configureAutonomy({}), "K10-02 setup");
    const RequestAction first{{9, 1, 4}, 80, {0, 0, 0, 16384}};
    expect(transport->submit(first), "K10-02 first queued command");
    expect(!transport->submit(first), "K10-02 duplicate ownership must fail");
    const RequestAction sameIdNewGeneration{{9, 2, 4}, 81, {1, 0, 0, 16384}};
    expect(transport->submit(sameIdNewGeneration), "K10-02 new generation must queue independently");
    const RequestAction thirdPeer{{10, 1, 5}, 82, {0, 0, 0, 16384}};
    expect(transport->submit(thirdPeer), "K10-02 several-peer queue must accept independent owner");
    expect(!transport->submit(CancelAction{{9, 0, 4}, 80, first.block, true}),
           "K10-02 stale head-of-line attempt must remain retryable");
    expect(transport->submit(CancelAction{first.ownership, first.peer, first.block, true}),
           "K10-02 head-of-line exact retry must reconcile");
    expect(!transport->submit(first), "K10-02 canceled ownership must never replay");
    expect(transport->statistics().ownedRequestsOutstanding == 2,
           "K10-02 cancellation decrements exactly one active request");

    auto otherInfoHash = server1::transport::makeLibTorrent2Adapter(
        (secondTorrent / "K10-wire.torrent").string(), (directory / "second-download").string());
    expect(bool(otherInfoHash) && otherInfoHash->configureAutonomy({}),
           "K10-02 second infohash adapter setup");
    expect(otherInfoHash->submit(first),
           "K10-02 ownership ledger is isolated across infohash adapters");
    expect(otherInfoHash->statistics().ownedRequestsOutstanding == 1
               && transport->statistics().ownedRequestsOutstanding == 2,
           "K10-02 multi-infohash counters remain isolated");

    constexpr std::uint16_t reusedPort = 65530;
    expect(server1::transport::connectPeer(*transport, 90, "127.0.0.1", reusedPort)
               && server1::transport::connectPeer(*transport, 91, "127.0.0.1", reusedPort),
           "K10-02 reused endpoint must accept a replacement peer identity");
    expect(server1::transport::boundEndpointOwner(*transport, "127.0.0.1", reusedPort) == 91,
           "K10-02 reused endpoint retained stale peer ownership");
    otherInfoHash->close();
    const auto otherClosed = otherInfoHash->poll();
    expect(std::count_if(otherClosed.begin(), otherClosed.end(), [](const auto &item) {
               return std::holds_alternative<ClosedObservation>(item);
           }) == 1, "K10-02 second infohash close is terminal once");

    transport->close();
    expect(!transport->submit(sameIdNewGeneration), "K10-02 terminal close rejects replay");
    transport->close();
    const auto terminal = transport->poll();
    expect(std::count_if(terminal.begin(), terminal.end(), [](const auto &item) {
               return std::holds_alternative<FailureObservation>(item);
           }) == 2, "K10-02 close terminalizes each queued peer once");
    expect(std::count_if(terminal.begin(), terminal.end(), [](const auto &item) {
               return std::holds_alternative<ClosedObservation>(item);
           }) == 1, "K10-02 close observation is emitted once");
    std::cout << "K10-02 PASS multi-peer/head-retry/reused-endpoint/multi-infohash/terminal lifecycle\n";
}

void caseThreadGuard(const fs::path &directory)
{
    prepare(directory);
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "guard-download").string());
    expect(bool(transport), "K10-03 setup");
    expect(!transport->configureAutonomy({true, false, false}),
           "K10-03 autonomous picker must remain rejected");
    expect(transport->configureAutonomy({}), "K10-03 controlled configuration");
    expect(server1::transport::forbiddenCrossThreadNativeAccessIsRejected(*transport),
           "K10-03 native-thread guard failed to reject caller thread");
    std::thread secondCaller([&] {
        expect(server1::transport::forbiddenCrossThreadNativeAccessIsRejected(*transport),
               "K10-03 second forbidden caller thread was not rejected");
    });
    secondCaller.join();
    expect(server1::transport::forbiddenNativeAttemptCount(*transport) == 2,
           "K10-03 guard must account for every forbidden attempt");
    transport->close();
    std::cout << "K10-03 PASS forbidden cross-thread native access rejected\n";
}
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--prepare") { prepare(argv[2]); return 0; }
    if (argc == 5 && std::string(argv[1]) == "--wire") {
        caseWire(argv[2], std::stoi(argv[3]), std::stoi(argv[4])); return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "--lifecycle") {
        caseLifecycle(argv[2], std::stoi(argv[3]), std::stoi(argv[4])); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "K10-02") { caseLedger(argv[2]); return 0; }
    if (argc == 3 && std::string(argv[1]) == "K10-03") { caseThreadGuard(argv[2]); return 0; }
    std::cerr << "usage: test_native_transport --prepare DIR | --wire DIR PORT_A PORT_B | --lifecycle DIR PORT_A PORT_B | K10-02 DIR | K10-03 DIR\n";
    return 2;
}
