#include "server1/ports/TorrentTransport.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/version.hpp>

#include <chrono>
#include <algorithm>
#include <atomic>
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
std::uint64_t guardedNativeTouchCount(const ports::TorrentTransport &);
std::uint64_t staleDisconnectIgnoredCount(const ports::TorrentTransport &);
bool replayLastDetachedIdentity(ports::TorrentTransport &, ports::PeerHandle);
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
    creator.add_tracker("http://tracker.invalid/announce");
    creator.add_url_seed("http://seed.invalid/K10-wire.bin");
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

std::vector<std::uint8_t> readBytes(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

V1InfoHash readInfoHash(const fs::path &directory)
{
    std::ifstream input(directory / "info_hash.txt");
    std::string hex;
    input >> hex;
    expect(hex.size() == 40, "K10-E prepared v1 hash length mismatch");
    V1InfoHash value{};
    const auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + c - 'a');
        fail("K10-E prepared v1 hash was not canonical lowercase hex");
    };
    for (std::size_t index = 0; index < value.size(); ++index)
        value[index] = static_cast<std::uint8_t>((nibble(hex[index * 2]) << 4) | nibble(hex[index * 2 + 1]));
    return value;
}

std::string infoHashHex(const V1InfoHash &hash)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : hash) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

class NativeMetadataSeeder final {
public:
    explicit NativeMetadataSeeder(const fs::path &directory)
        : session_(settings())
    {
        bool listening = false;
        const auto listenDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!listening && std::chrono::steady_clock::now() < listenDeadline) {
            session_.wait_for_alert(std::chrono::milliseconds(100));
            std::vector<lt::alert *> alerts;
            session_.pop_alerts(&alerts);
            listening = std::any_of(alerts.begin(), alerts.end(), [](const lt::alert *alert) {
                return lt::alert_cast<lt::listen_succeeded_alert>(alert) != nullptr;
            });
        }
        expect(listening, "K10-E native metadata seeder listen barrier failed");
        lt::add_torrent_params params;
        params.ti = std::make_shared<lt::torrent_info>((directory / "K10-wire.torrent").string());
        params.save_path = directory.string();
        params.flags &= ~lt::torrent_flags::paused;
        params.flags &= ~lt::torrent_flags::auto_managed;
        params.flags |= lt::torrent_flags::seed_mode;
        lt::error_code error;
        handle_ = session_.add_torrent(std::move(params), error);
        expect(!error && handle_.is_valid(), "K10-E native metadata seeder add failed");
        port_ = session_.listen_port();
        expect(port_ != 0, "K10-E native metadata seeder did not bind");
    }

    [[nodiscard]] std::uint16_t port() const { return port_; }

private:
    static lt::settings_pack settings()
    {
        lt::settings_pack result;
        result.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
        result.set_bool(lt::settings_pack::enable_dht, false);
        result.set_bool(lt::settings_pack::enable_lsd, false);
        result.set_bool(lt::settings_pack::enable_upnp, false);
        result.set_bool(lt::settings_pack::enable_natpmp, false);
        result.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_disabled);
        result.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_disabled);
        result.set_int(lt::settings_pack::alert_mask,
                       static_cast<int>(lt::alert_category::error | lt::alert_category::status));
        return result;
    }

    lt::session session_;
    lt::torrent_handle handle_;
    std::uint16_t port_ = 0;
};

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

void waitReady(TorrentTransport &transport, const std::string &caseName)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto stats = transport.statistics();
        if (stats.connectedPeers == 1 && stats.unchokedPeers == 1) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto stats = transport.statistics();
    fail(caseName + " peer readiness failed connected=" + std::to_string(stats.connectedPeers)
         + " unchoked=" + std::to_string(stats.unchokedPeers));
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

void caseSubmitAfterReady(const fs::path &directory, int port)
{
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "submit-ready-download").string());
    expect(bool(transport) && transport->configureAutonomy({}), "K10-01 submit-ready setup");
    expect(transport->submit(InterestAction{77, true}), "K10-01 submit-ready interest rejected");
    expect(server1::transport::connectPeer(*transport, 77, "127.0.0.1",
                                           static_cast<std::uint16_t>(port)),
           "K10-01 submit-ready peer rejected");
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < readyDeadline) {
        const auto stats = transport->statistics();
        if (stats.connectedPeers == 1 && stats.unchokedPeers == 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(transport->statistics().connectedPeers == 1
               && transport->statistics().unchokedPeers == 1,
           "K10-01 single peer did not become ready before submit");
    const RequestAction request{{61, 1, 1}, 77, {0, 0, 0, 16384}};
    expect(transport->submit(request), "K10-01 submit-after-ready request rejected");
    const auto observations = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<BlockObservation>(item);
        });
    });
    expect(std::count_if(observations.begin(), observations.end(), [](const auto &item) {
               return std::holds_alternative<BlockObservation>(item);
           }) == 1, "K10-01 submit-after-ready did not complete one block");
    expect(server1::transport::ownedNativeAddCount(*transport) == 1
               && server1::transport::framedRequestCount(*transport) == 1,
           "K10-01 submit-after-ready was not queued/framed once");
    transport->close();
    std::cout << "K10-01 SUBMIT-READY PASS queued=1 framed=1 requests=1\n";
}

void caseSequentialBlocks(const fs::path &directory, int port)
{
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "sequential-download").string());
    expect(bool(transport) && transport->configureAutonomy({}), "K10-02 sequential setup");
    expect(transport->submit(InterestAction{77, true}), "K10-02 sequential interest rejected");
    expect(server1::transport::connectPeer(*transport, 77, "127.0.0.1",
                                           static_cast<std::uint16_t>(port)),
           "K10-02 sequential peer rejected");
    waitReady(*transport, "K10-02 sequential");

    const RequestAction first{{71, 1, 1}, 77, {0, 0, 0, 16384}};
    const RequestAction second{{72, 1, 1}, 77, {1, 0, 0, 16384}};
    expect(transport->submit(first) && transport->submit(second),
           "K10-02 sequential queue rejected");
    expect(!transport->submit(CancelAction{{71, 0, 1}, 77, first.block, true}),
           "K10-02 stale head cancel must leave the exact request retryable");

    const auto observations = pollUntil(*transport, [](const auto &items) {
        return std::count_if(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<BlockObservation>(item);
        }) == 2;
    });
    std::vector<std::uint64_t> completed;
    for (const auto &item : observations)
        if (const auto *block = std::get_if<BlockObservation>(&item))
            completed.push_back(block->ownership.requestId);
    expect(completed == std::vector<std::uint64_t>({71, 72}),
           "K10-02 sequential completion order mismatch");
    expect(server1::transport::ownedNativeAddCount(*transport) == 2
               && server1::transport::framedRequestCount(*transport) == 2
               && transport->statistics().ownedRequestsOutstanding == 0,
           "K10-02 sequential native/accounting counters mismatch");
    transport->close();
    std::cout << "K10-02 SEQUENTIAL PASS requests=2 head_retry=1 queued=2 framed=2\n";
}

void caseHaveTransition(const fs::path &directory, int port)
{
    const auto marker = directory / "send-have.marker";
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "have-download").string());
    expect(bool(transport) && transport->configureAutonomy({}), "K10-02 HAVE setup");
    expect(transport->submit(InterestAction{77, true}), "K10-02 HAVE interest rejected");
    expect(server1::transport::connectPeer(*transport, 77, "127.0.0.1",
                                           static_cast<std::uint16_t>(port)),
           "K10-02 HAVE peer rejected");
    waitReady(*transport, "K10-02 HAVE");

    const RequestAction missingFirst{{81, 1, 1}, 77, {0, 0, 0, 16384}};
    const RequestAction initiallyEligible{{82, 1, 1}, 77, {1, 0, 0, 16384}};
    expect(transport->submit(missingFirst) && transport->submit(initiallyEligible),
           "K10-02 HAVE queue rejected");
    const auto first = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            const auto *block = std::get_if<BlockObservation>(&item);
            return block && block->ownership.requestId == 82;
        });
    });
    expect(std::any_of(first.begin(), first.end(), [](const auto &item) {
               const auto *block = std::get_if<BlockObservation>(&item);
               return block && block->ownership.requestId == 82;
           }), "K10-02 initially eligible piece did not complete");
    std::ofstream(marker, std::ios::trunc).close();
    const auto second = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            const auto *block = std::get_if<BlockObservation>(&item);
            return block && block->ownership.requestId == 81;
        });
    });
    expect(std::any_of(second.begin(), second.end(), [](const auto &item) {
               const auto *block = std::get_if<BlockObservation>(&item);
               return block && block->ownership.requestId == 81;
           }), "K10-02 HAVE did not make the missing piece eligible");
    expect(server1::transport::ownedNativeAddCount(*transport) == 2
               && server1::transport::framedRequestCount(*transport) == 2
               && transport->statistics().ownedRequestsOutstanding == 0,
           "K10-02 HAVE native/accounting counters mismatch");
    transport->close();
    std::cout << "K10-02 HAVE PASS order=piece1,piece0 queued=2 framed=2\n";
}

void caseLiveReuse(const fs::path &directory, int port)
{
    const auto disconnected = directory / "first-disconnected.marker";
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "reuse-download").string());
    expect(bool(transport) && transport->configureAutonomy({}), "K10-02 reuse setup");
    expect(transport->submit(InterestAction{90, true}), "K10-02 first reuse interest rejected");
    expect(server1::transport::connectPeer(*transport, 90, "127.0.0.1",
                                           static_cast<std::uint16_t>(port)),
           "K10-02 first reused-endpoint connection rejected");
    const auto disconnectDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < disconnectDeadline) {
        const auto stats = transport->statistics();
        if (fs::exists(disconnected) && stats.connectedPeers == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto detached = transport->statistics();
    expect(detached.connectedPeers == 0 && detached.unchokedPeers == 0,
           "K10-02 detach did not clear connected/unchoked state");

    expect(transport->submit(InterestAction{90, true}), "K10-02 replacement interest rejected");
    expect(server1::transport::connectPeer(*transport, 90, "127.0.0.1",
                                           static_cast<std::uint16_t>(port)),
           "K10-02 live reused-endpoint replacement rejected");
    waitReady(*transport, "K10-02 replacement");
    expect(server1::transport::replayLastDetachedIdentity(*transport, 90),
           "K10-02 stale disconnect identity was not rejected");
    expect(server1::transport::staleDisconnectIgnoredCount(*transport) == 1,
           "K10-02 stale disconnect rejection was not accounted once");
    const auto afterStale = transport->statistics();
    expect(afterStale.connectedPeers == 1 && afterStale.unchokedPeers == 1,
           "K10-02 stale disconnect damaged the live replacement");
    expect(server1::transport::boundEndpointOwner(*transport, "127.0.0.1",
                                                   static_cast<std::uint16_t>(port)) == 90,
           "K10-02 live replacement lost endpoint ownership");
    const RequestAction request{{91, 1, 1}, 90, {0, 0, 0, 16384}};
    expect(transport->submit(request), "K10-02 replacement request rejected");
    const auto observations = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<BlockObservation>(item);
        });
    });
    expect(std::count_if(observations.begin(), observations.end(), [](const auto &item) {
               return std::holds_alternative<BlockObservation>(item);
           }) == 1, "K10-02 live replacement did not carry the owned block");
    transport->close();
    std::cout << "K10-02 REUSE PASS detached=0/0 replacement=live request=1\n";
}

void caseFailureDrain(const fs::path &directory, int failingPort, int survivingPort)
{
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "failure-drain-download").string());
    expect(bool(transport) && transport->configureAutonomy({}), "K10-02 failure-drain setup");
    expect(transport->submit(InterestAction{77, true})
               && transport->submit(InterestAction{78, true}),
           "K10-02 failure-drain interest rejected");
    expect(server1::transport::connectPeer(*transport, 77, "127.0.0.1",
                                           static_cast<std::uint16_t>(failingPort))
               && server1::transport::connectPeer(*transport, 78, "127.0.0.1",
                                                   static_cast<std::uint16_t>(survivingPort)),
           "K10-02 failure-drain peers rejected");
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < readyDeadline) {
        const auto stats = transport->statistics();
        if (stats.connectedPeers == 2 && stats.unchokedPeers == 2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(transport->statistics().connectedPeers == 2
               && transport->statistics().unchokedPeers == 2,
           "K10-02 failure-drain peers did not become ready");

    const RequestAction failing{{111, 1, 1}, 77, {0, 0, 0, 16384}};
    const RequestAction surviving{{112, 1, 1}, 78, {1, 0, 0, 16384}};
    expect(transport->submit(failing) && transport->submit(surviving),
           "K10-02 failure-drain queue rejected");
    const auto observations = pollUntil(*transport, [](const auto &items) {
        const auto failed = std::any_of(items.begin(), items.end(), [](const auto &item) {
            const auto *failure = std::get_if<FailureObservation>(&item);
            return failure && failure->ownership.requestId == 111 && failure->retryable;
        });
        const auto completed = std::any_of(items.begin(), items.end(), [](const auto &item) {
            const auto *block = std::get_if<BlockObservation>(&item);
            return block && block->ownership.requestId == 112;
        });
        return failed && completed;
    });
    expect(std::any_of(observations.begin(), observations.end(), [](const auto &item) {
               const auto *failure = std::get_if<FailureObservation>(&item);
               return failure && failure->ownership.requestId == 111 && failure->retryable;
           }), "K10-02 disconnected request did not fail terminally");
    expect(std::any_of(observations.begin(), observations.end(), [](const auto &item) {
               const auto *block = std::get_if<BlockObservation>(&item);
               return block && block->ownership.requestId == 112;
           }), "K10-02 surviving peer did not drain after failure");
    expect(server1::transport::ownedNativeAddCount(*transport) == 2
               && server1::transport::framedRequestCount(*transport) == 2
               && transport->statistics().ownedRequestsOutstanding == 0,
           "K10-02 failure-drain native/accounting counters mismatch");
    transport->close();
    std::cout << "K10-02 FAILURE-DRAIN PASS failed=111 completed=112 queued=2 framed=2\n";
}

void caseCancelDrain(const fs::path &directory, int port)
{
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "cancel-drain-download").string());
    expect(bool(transport) && transport->configureAutonomy({}), "K10-02 cancel-drain setup");
    expect(transport->submit(InterestAction{77, true}), "K10-02 cancel-drain interest rejected");
    expect(server1::transport::connectPeer(*transport, 77, "127.0.0.1",
                                           static_cast<std::uint16_t>(port)),
           "K10-02 cancel-drain peer rejected");
    waitReady(*transport, "K10-02 cancel-drain");

    const RequestAction canceled{{115, 1, 1}, 77, {0, 0, 0, 16384}};
    const RequestAction surviving{{116, 1, 1}, 77, {1, 0, 0, 16384}};
    expect(transport->submit(canceled) && transport->submit(surviving),
           "K10-02 cancel-drain queue rejected");
    expect(transport->submit(CancelAction{canceled.ownership, canceled.peer,
                                          canceled.block, true}),
           "K10-02 exact cancel was rejected");
    const auto observations = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            const auto *block = std::get_if<BlockObservation>(&item);
            return block && block->ownership.requestId == 116;
        });
    });
    expect(std::none_of(observations.begin(), observations.end(), [](const auto &item) {
               const auto *block = std::get_if<BlockObservation>(&item);
               return block && block->ownership.requestId == 115;
           }), "K10-02 canceled ownership completed on the wire");
    expect(std::any_of(observations.begin(), observations.end(), [](const auto &item) {
               const auto *block = std::get_if<BlockObservation>(&item);
               return block && block->ownership.requestId == 116;
           }), "K10-02 next request did not drain after exact cancel");
    expect(server1::transport::ownedNativeAddCount(*transport) == 1
               && server1::transport::framedRequestCount(*transport) == 1
               && transport->statistics().ownedRequestsOutstanding == 0,
           "K10-02 cancel-drain native/accounting counters mismatch");
    expect(!transport->submit(canceled), "K10-02 canceled ownership replay was accepted");
    transport->close();
    std::cout << "K10-02 CANCEL-DRAIN PASS canceled=115 completed=116 queued=1 framed=1\n";
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

    auto raceTransport = server1::transport::makeLibTorrent2Adapter(
        (firstTorrent / "K10-wire.torrent").string(), (directory / "race-download").string());
    expect(bool(raceTransport) && raceTransport->configureAutonomy({}),
           "K10-02 submit/close race setup");
    const RequestAction racing{{120, 1, 1}, 83, {0, 0, 0, 16384}};
    std::atomic<bool> start{false};
    bool accepted = false;
    std::thread submitter([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        accepted = raceTransport->submit(racing);
    });
    std::thread raceCloser([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        raceTransport->close();
    });
    start.store(true, std::memory_order_release);
    submitter.join(); raceCloser.join();
    const auto raceTerminal = raceTransport->poll();
    expect(std::count_if(raceTerminal.begin(), raceTerminal.end(), [](const auto &item) {
               return std::holds_alternative<ClosedObservation>(item);
           }) == 1, "K10-02 submit/close race must close exactly once");
    expect(std::count_if(raceTerminal.begin(), raceTerminal.end(), [](const auto &item) {
               return std::holds_alternative<FailureObservation>(item);
           }) == (accepted ? 1 : 0), "K10-02 submit/close race terminal count mismatch");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    expect(raceTransport->poll().empty(),
           "K10-02 libtorrent tick escaped the submit/close lifetime boundary");
    std::cout << "K10-02 PASS multi-peer/head-retry/reused-endpoint/multi-infohash/terminal lifecycle\n";
}

void caseThreadGuard(const fs::path &directory, int port)
{
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "guard-download").string());
    expect(bool(transport), "K10-03 setup");
    expect(!transport->configureAutonomy({true, false, false}),
           "K10-03 autonomous picker must remain rejected");
    expect(transport->configureAutonomy({}), "K10-03 controlled configuration");
    expect(transport->submit(InterestAction{77, true}), "K10-03 interest rejected");
    expect(server1::transport::connectPeer(*transport, 77, "127.0.0.1",
                                           static_cast<std::uint16_t>(port)),
           "K10-03 controlled peer connection rejected");
    waitReady(*transport, "K10-03 native thread establishment");

    const auto nativeTouchesBefore = server1::transport::guardedNativeTouchCount(*transport);
    expect(server1::transport::forbiddenCrossThreadNativeAccessIsRejected(*transport),
           "K10-03 established native-thread guard failed to reject caller thread");
    std::thread secondCaller([&] {
        expect(server1::transport::forbiddenCrossThreadNativeAccessIsRejected(*transport),
               "K10-03 second forbidden caller thread was not rejected");
    });
    secondCaller.join();
    expect(server1::transport::forbiddenNativeAttemptCount(*transport) == 2,
           "K10-03 guard must account for every forbidden attempt");
    expect(server1::transport::guardedNativeTouchCount(*transport) == nativeTouchesBefore,
           "K10-03 forbidden callers touched libtorrent after rejection");

    const RequestAction request{{101, 1, 1}, 77, {0, 0, 0, 16384}};
    expect(transport->submit(request), "K10-03 post-guard owned request rejected");
    const auto observations = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<BlockObservation>(item);
        });
    });
    expect(std::count_if(observations.begin(), observations.end(), [](const auto &item) {
               return std::holds_alternative<BlockObservation>(item);
           }) == 1, "K10-03 guard damaged the live production peer");
    transport->close();
    std::cout << "K10-03 PASS established_native_thread=1 forbidden=2 native_touches=0 request=1\n";
}

void caseSourceFailure(const fs::path &directory)
{
    const TorrentOpenRequest invalid{301,
        V1InfoHash{}, MagnetSource{"magnet:?xt=urn:btih:not-hex"},
        (directory / "invalid-download").string()};
    auto transport = server1::ports::openTorrentTransport(invalid);
    expect(bool(transport), "K10-E invalid source factory threw or returned null");
    const auto first = transport->poll();
    expect(std::count_if(first.begin(), first.end(), [](const auto &item) {
               const auto *failure = std::get_if<SourceFailureObservation>(&item);
               return failure && failure->generation == 301 && !failure->retryable;
           }) == 1, "K10-E invalid canonical hash did not emit one source-owned failure");
    expect(transport->poll().empty(), "K10-E invalid source failure repeated");
    transport->close();
    std::cout << "K10-E INVALID PASS source_failures=1 throws=0\n";
}

void caseInvalidGeneration(const fs::path &directory)
{
    const TorrentOpenRequest invalid{0, V1InfoHash{}, InfoHashSource{},
        (directory / "invalid-generation-download").string()};
    auto transport = server1::ports::openTorrentTransport(invalid);
    expect(bool(transport), "K10-E zero-generation factory returned null");
    const auto observations = transport->poll();
    expect(std::count_if(observations.begin(), observations.end(), [](const auto &item) {
               const auto *failure = std::get_if<SourceFailureObservation>(&item);
               return failure && failure->generation == 0
                   && failure->error.find("generation") != std::string::npos;
           }) == 1, "K10-E zero generation did not emit one source-owned failure");
    transport->close();
    std::cout << "K10-E GENERATION PASS source_failures=1 throws=0\n";
}

void caseCachedMetadata(const fs::path &directory)
{
    prepare(directory);
    const auto hash = readInfoHash(directory);
    const TorrentOpenRequest request{302, hash,
        MetainfoSource{readBytes(directory / "K10-wire.torrent")},
        (directory / "cached-download").string()};
    auto transport = server1::ports::openTorrentTransport(request);
    expect(bool(transport), "K10-E cached source factory failed");
    const auto observations = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<MetadataReadyObservation>(item);
        });
    });
    const auto readyCount = std::count_if(observations.begin(), observations.end(), [](const auto &item) {
        return std::holds_alternative<MetadataReadyObservation>(item);
    });
    expect(readyCount == 1, "K10-E cached metadata did not queue exactly once");
    const auto ready = std::find_if(observations.begin(), observations.end(), [](const auto &item) {
        return std::holds_alternative<MetadataReadyObservation>(item);
    });
    const auto &metadata = std::get<MetadataReadyObservation>(*ready);
    expect(metadata.generation == 302 && metadata.infoHash == hash
               && !metadata.infoSection.empty()
               && metadata.trackers == std::vector<std::string>{"http://tracker.invalid/announce"}
               && metadata.urlSeeds == std::vector<std::string>{"http://seed.invalid/K10-wire.bin"},
           "K10-E cached ready observation lost generation, info section, tracker, or URL seed");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    expect(transport->poll().empty(), "K10-E cached metadata ready repeated");
    transport->close();
    std::cout << "K10-E CACHED PASS queued=1 ready=1 duplicate=0\n";
}

void caseHashMismatch(const fs::path &directory)
{
    prepare(directory);
    const TorrentOpenRequest request{303,
        V1InfoHash{},
        MetainfoSource{readBytes(directory / "K10-wire.torrent")},
        (directory / "mismatch-download").string()};
    auto transport = server1::ports::openTorrentTransport(request);
    expect(bool(transport), "K10-E mismatch source factory returned null");
    const auto observations = transport->poll();
    expect(std::count_if(observations.begin(), observations.end(), [](const auto &item) {
               const auto *failure = std::get_if<SourceFailureObservation>(&item);
               return failure && failure->generation == 303
                   && failure->error.find("mismatch") != std::string::npos;
           }) == 1, "K10-E metainfo hash mismatch did not emit one source failure");
    transport->close();
    std::cout << "K10-E MISMATCH PASS source_failures=1 throws=0\n";
}

void casePeerMetadata(const fs::path &directory, bool magnet)
{
    prepare(directory);
    const auto hash = readInfoHash(directory);
    NativeMetadataSeeder seeder(directory);
    TorrentSource source = InfoHashSource{};
    if (magnet) source = MagnetSource{"magnet:?xt=urn:btih:" + infoHashHex(hash)};
    const EngineGeneration generation = magnet ? 305 : 304;
    const TorrentOpenRequest request{generation, hash, std::move(source),
        (directory / (magnet ? "magnet-download" : "hash-download")).string()};
    auto transport = server1::ports::openTorrentTransport(request);
    expect(bool(transport), "K10-E peer metadata factory failed");
    expect(transport->configureAutonomy({}), "K10-E peer metadata control setup failed");
    expect(transport->poll().empty(), "K10-E peer metadata was synthesized during construction");
    expect(!transport->submit(ConnectAction{generation - 1, 177, "127.0.0.1", seeder.port()}),
           "K10-E stale-generation ConnectAction was accepted");
    expect(transport->submit(ConnectAction{generation, 177, "127.0.0.1", seeder.port()}),
           "K10-E public ConnectAction was rejected");
    const auto observations = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<MetadataReadyObservation>(item)
                || std::holds_alternative<SourceFailureObservation>(item);
        });
    });
    for (const auto &item : observations)
        if (const auto *failure = std::get_if<SourceFailureObservation>(&item))
            fail("K10-E peer metadata source failure: " + failure->error);
    expect(std::count_if(observations.begin(), observations.end(), [](const auto &item) {
               return std::holds_alternative<MetadataReadyObservation>(item);
           }) == 1, "K10-E real libtorrent metadata was not delivered exactly once");
    const auto ready = std::find_if(observations.begin(), observations.end(), [](const auto &item) {
        return std::holds_alternative<MetadataReadyObservation>(item);
    });
    const auto &metadata = std::get<MetadataReadyObservation>(*ready);
    expect(metadata.generation == generation && metadata.infoHash == hash
               && !metadata.infoSection.empty(),
           "K10-E real libtorrent metadata lost generation, hash, or info section");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    expect(transport->poll().empty(), "K10-E real libtorrent metadata repeated");
    transport->close();
    std::cout << "K10-E " << (magnet ? "MAGNET" : "INFOHASH")
              << " PASS public_connect=1 native_metadata=1 ready=1 duplicate=0\n";
}

void caseCloseSuppressesSource(const fs::path &directory)
{
    prepare(directory);
    const TorrentOpenRequest request{306, readInfoHash(directory),
        MetainfoSource{readBytes(directory / "K10-wire.torrent")},
        (directory / "close-download").string()};
    auto transport = server1::ports::openTorrentTransport(request);
    expect(bool(transport), "K10-E close suppression factory failed");
    transport->close();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto terminal = transport->poll();
    expect(std::count_if(terminal.begin(), terminal.end(), [](const auto &item) {
               return std::holds_alternative<ClosedObservation>(item);
           }) == 1, "K10-E close observation missing");
    expect(std::none_of(terminal.begin(), terminal.end(), [](const auto &item) {
               return std::holds_alternative<MetadataReadyObservation>(item)
                   || std::holds_alternative<SourceFailureObservation>(item);
           }), "K10-E stale source effect escaped after close");
    expect(!transport->submit(ConnectAction{306, 177, "127.0.0.1", 65530}),
           "K10-E connect escaped after close");
    std::cout << "K10-E CLOSE PASS stale_source_effects=0\n";
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
    if (argc == 4 && std::string(argv[1]) == "--submit-ready") {
        caseSubmitAfterReady(argv[2], std::stoi(argv[3])); return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--sequential") {
        caseSequentialBlocks(argv[2], std::stoi(argv[3])); return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--have") {
        caseHaveTransition(argv[2], std::stoi(argv[3])); return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--reuse") {
        caseLiveReuse(argv[2], std::stoi(argv[3])); return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "--failure-drain") {
        caseFailureDrain(argv[2], std::stoi(argv[3]), std::stoi(argv[4])); return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--cancel-drain") {
        caseCancelDrain(argv[2], std::stoi(argv[3])); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "K10-02") { caseLedger(argv[2]); return 0; }
    if (argc == 4 && std::string(argv[1]) == "--thread-guard") {
        caseThreadGuard(argv[2], std::stoi(argv[3])); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--source-invalid") {
        caseSourceFailure(argv[2]); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--source-generation") {
        caseInvalidGeneration(argv[2]); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--source-cached") {
        caseCachedMetadata(argv[2]); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--source-mismatch") {
        caseHashMismatch(argv[2]); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--source-infohash") {
        casePeerMetadata(argv[2], false); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--source-magnet") {
        casePeerMetadata(argv[2], true); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--source-close") {
        caseCloseSuppressesSource(argv[2]); return 0;
    }
    std::cerr << "usage: test_native_transport --prepare DIR | --wire DIR PORT_A PORT_B | --lifecycle DIR PORT_A PORT_B | --thread-guard DIR PORT | K10-02 DIR\n";
    return 2;
}
