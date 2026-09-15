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
bool replayLastDetachedCallbacks(ports::TorrentTransport &, ports::PeerHandle,
                                 const ports::BlockSpan &);
std::uint64_t staleCallbackIgnoredCount(const ports::TorrentTransport &);
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

void prepare(const fs::path &directory, char fill = 'K', std::size_t size = 32768)
{
    fs::create_directories(directory);
    const auto payload = directory / "K10-wire.bin";
    std::ofstream output(payload, std::ios::binary | std::ios::trunc);
    output << std::string(size, fill);
    output.close();
    lt::file_storage storage;
    storage.add_file("K10-wire.bin", static_cast<std::int64_t>(size));
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

void waitEndpointOwner(TorrentTransport &transport, const std::string &address,
                       std::uint16_t port, PeerHandle expected, const std::string &caseName)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        if (server1::transport::boundEndpointOwner(transport, address, port) == expected
            && transport.statistics().connectedPeers >= 1) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    fail(caseName + " endpoint ownership deadline expected=" + std::to_string(expected)
         + " actual=" + std::to_string(
               server1::transport::boundEndpointOwner(transport, address, port))
         + " connected=" + std::to_string(transport.statistics().connectedPeers));
}

void caseWire(const fs::path &directory, int portA, int portB)
{
    auto transport = server1::transport::makeLibTorrent2Adapter(
        (directory / "K10-wire.torrent").string(), (directory / "download").string());
    expect(bool(transport), "K10-01 production adapter factory failed");
    expect(transport->configureAutonomy({}), "K10-01 external-control suppression rejected");
    const RequestAction request{{41, 1, 3}, 77, {0, 0, 0, 16384}};
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
            expect(block->ownership.requestId == 41 && block->ownership.generation == 1
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
              << " owned=41/1/3 peer=77 piece=0 offset=0 length=16384"
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

void caseAvailability(const fs::path &directory, int port)
{
    const auto haveMarker = directory / "send-have.marker";
    const auto disconnectMarker = directory / "disconnect-first.marker";
    const auto disconnected = directory / "first-disconnected.marker";
    const auto hash = readInfoHash(directory);
    const EngineGeneration generation = 404;
    auto transport = server1::ports::openTorrentTransport(TorrentOpenRequest{
        generation, hash, MetainfoSource{readBytes(directory / "K10-wire.torrent")},
        (directory / "availability-download").string()});
    expect(bool(transport) && transport->configureAutonomy({}), "K10-F availability setup");
    expect(transport->submit(InterestAction{90, true}), "K10-F first interest rejected");
    expect(transport->submit(ConnectAction{generation, 90, "127.0.0.1",
                                            static_cast<std::uint16_t>(port)}),
           "K10-F first connection rejected");

    const auto initial = pollUntil(*transport, [=](const auto &items) {
        return std::any_of(items.begin(), items.end(), [=](const auto &item) {
            const auto *available = std::get_if<AvailablePiecesObservation>(&item);
            return available && available->generation == generation && available->peer == 90
                && available->pieces == std::vector<std::uint32_t>({1, 3});
        });
    });
    expect(std::any_of(initial.begin(), initial.end(), [=](const auto &item) {
               const auto *available = std::get_if<AvailablePiecesObservation>(&item);
               return available && available->generation == generation && available->peer == 90
                   && available->pieces == std::vector<std::uint32_t>({1, 3});
           }), "K10-F initial bitfield did not emit sorted, deduplicated full snapshot");

    std::ofstream(haveMarker, std::ios::trunc).close();
    const auto afterHave = pollUntil(*transport, [=](const auto &items) {
        return std::any_of(items.begin(), items.end(), [=](const auto &item) {
            const auto *available = std::get_if<AvailablePiecesObservation>(&item);
            return available && available->generation == generation && available->peer == 90
                && available->pieces == std::vector<std::uint32_t>({0, 1, 3});
        });
    });
    expect(std::any_of(afterHave.begin(), afterHave.end(), [=](const auto &item) {
               const auto *available = std::get_if<AvailablePiecesObservation>(&item);
               return available && available->pieces == std::vector<std::uint32_t>({0, 1, 3});
           }), "K10-F HAVE did not emit the full updated snapshot");

    std::ofstream(disconnectMarker, std::ios::trunc).close();
    const auto detached = pollUntil(*transport, [&](const auto &items) {
        return fs::exists(disconnected)
            && std::any_of(items.begin(), items.end(), [=](const auto &item) {
                const auto *available = std::get_if<AvailablePiecesObservation>(&item);
                return available && available->generation == generation && available->peer == 90
                    && available->pieces.empty();
            });
    });
    expect(std::any_of(detached.begin(), detached.end(), [=](const auto &item) {
               const auto *available = std::get_if<AvailablePiecesObservation>(&item);
               return available && available->generation == generation && available->peer == 90
                   && available->pieces.empty();
           }), "K10-F current detach did not emit an empty clearing snapshot");

    expect(transport->submit(InterestAction{90, true}), "K10-F replacement interest rejected");
    expect(transport->submit(ConnectAction{generation, 90, "127.0.0.1",
                                            static_cast<std::uint16_t>(port)}),
           "K10-F replacement connection rejected");
    const auto replacement = pollUntil(*transport, [&, generation](const auto &items) {
        const bool emptySnapshot = std::any_of(items.begin(), items.end(), [=](const auto &item) {
            const auto *available = std::get_if<AvailablePiecesObservation>(&item);
            return available && available->generation == generation && available->peer == 90
                && available->pieces.empty();
        });
        return emptySnapshot && transport->statistics().connectedPeers == 1
            && transport->statistics().unchokedPeers == 1;
    });
    expect(std::any_of(replacement.begin(), replacement.end(), [=](const auto &item) {
               const auto *available = std::get_if<AvailablePiecesObservation>(&item);
               return available && available->pieces.empty();
           }), "K10-F empty replacement bitfield did not replace stale pieces");
    expect(server1::transport::replayLastDetachedIdentity(*transport, 90),
           "K10-F stale disconnect was not rejected");
    expect(transport->poll().empty(), "K10-F stale disconnect emitted a clearing snapshot");

    const RequestAction pending{{405, generation, 9}, 90, {0, 0, 0, 16384}};
    expect(transport->submit(pending), "K10-F pending replacement request rejected");
    const auto before = transport->statistics();
    const auto staleBefore = server1::transport::staleCallbackIgnoredCount(*transport);
    expect(server1::transport::replayLastDetachedCallbacks(*transport, 90, pending.block),
           "K10-F stale callback replay was not rejected in every lane");
    const auto afterStale = transport->statistics();
    const auto staleEffects = transport->poll();
    expect(server1::transport::staleCallbackIgnoredCount(*transport) == staleBefore + 5,
           "K10-F stale callback rejection count mismatch");
    expect(staleEffects.empty() && afterStale.connectedPeers == before.connectedPeers
               && afterStale.unchokedPeers == before.unchokedPeers
               && afterStale.ownedRequestsOutstanding == 1
               && afterStale.downloadedBytes == before.downloadedBytes
               && afterStale.pickerRequestsSuppressed == before.pickerRequestsSuppressed,
           "K10-F stale HAVE/state/authorize/framed/payload mutated live replacement state");
    expect(transport->submit(CancelAction{pending.ownership, pending.peer, pending.block, false}),
           "K10-F stale payload retired the live pending request");
    transport->close();
    std::cout << "K10-F PASS generation=404 initial=1,3 have=0,1,3 detach=empty"
              << " replacement=empty stale_disconnect=preserved stale_callbacks=5\n";
}

void casePauseAndGenerationOwnership(const fs::path &directory, int activePort, int deferredPort)
{
    const auto generation = EngineGeneration{407};
    const auto hash = readInfoHash(directory);
    const TorrentOpenRequest open{generation, hash,
        MetainfoSource{readBytes(directory / "K10-wire.torrent")},
        (directory / "pause-download").string()};
    auto transport = server1::ports::openTorrentTransport(open);
    expect(bool(transport) && transport->configureAutonomy({}), "K10-G setup");

    expect(!transport->submit(PauseAction{0, true})
               && !transport->submit(PauseAction{generation - 1, true}),
           "K10-G zero or stale pause was accepted");
    expect(!transport->statistics().paused, "K10-G rejected pause changed state");
    expect(transport->submit(InterestAction{201, true})
               && transport->submit(ConnectAction{generation, 201, "127.0.0.1",
                                                   static_cast<std::uint16_t>(activePort)}),
           "K10-G active peer setup rejected");
    waitReady(*transport, "K10-G active peer");

    expect(transport->submit(PauseAction{generation, true})
               && transport->submit(PauseAction{generation, true}),
           "K10-G current pause was not idempotent");
    expect(transport->statistics().paused, "K10-G paused statistic did not publish");

    const auto ownedBefore = transport->statistics().ownedRequestsOutstanding;
    const auto addsBefore = server1::transport::ownedNativeAddCount(*transport);
    const auto framedBefore = server1::transport::framedRequestCount(*transport);
    const RequestAction stale{{601, generation - 1, 1}, 201, {0, 0, 0, 16384}};
    const RequestAction zero{{602, 0, 1}, 201, {0, 0, 0, 16384}};
    expect(!transport->submit(stale) && !transport->submit(zero),
           "K10-G stale or zero request generation was accepted");
    expect(transport->statistics().ownedRequestsOutstanding == ownedBefore
               && server1::transport::ownedNativeAddCount(*transport) == addsBefore
               && server1::transport::framedRequestCount(*transport) == framedBefore,
           "K10-G rejected request changed counters or native wire state");

    const RequestAction active{{603, generation, 1}, 201, {0, 0, 0, 16384}};
    expect(transport->submit(active), "K10-G active request was rejected while paused");
    const auto activeObservations = pollUntil(*transport, [&](const auto &items) {
        return std::any_of(items.begin(), items.end(), [&](const auto &item) {
            const auto *block = std::get_if<BlockObservation>(&item);
            return block && block->ownership.requestId == active.ownership.requestId;
        });
    });
    const auto activeBlock = std::find_if(activeObservations.begin(), activeObservations.end(),
        [&](const auto &item) {
            const auto *block = std::get_if<BlockObservation>(&item);
            return block && block->ownership.requestId == active.ownership.requestId;
        });
    if (activeBlock == activeObservations.end()) {
        const auto diagnostics = transport->statistics();
        std::cerr << "K10-G active diagnostics connected=" << diagnostics.connectedPeers
                  << " unchoked=" << diagnostics.unchokedPeers
                  << " outstanding=" << diagnostics.ownedRequestsOutstanding
                  << " paused=" << diagnostics.paused
                  << " native_adds=" << server1::transport::ownedNativeAddCount(*transport)
                  << " framed=" << server1::transport::framedRequestCount(*transport)
                  << " observations=" << activeObservations.size() << '\n';
        for (const auto &item : activeObservations)
            if (const auto *failure = std::get_if<FailureObservation>(&item))
                std::cerr << "K10-G active failure=" << failure->error
                          << " generation=" << failure->ownership.generation
                          << " request=" << failure->ownership.requestId << '\n';
    }
    expect(activeBlock != activeObservations.end(),
           "K10-G paused active peer did not complete its owned block");
    const auto &payload = std::get<BlockObservation>(*activeBlock).payload;
    expect(payload.size() == 16384
               && std::all_of(payload.begin(), payload.end(), [](std::uint8_t byte) {
                      return byte == static_cast<std::uint8_t>('P');
                  }),
           "K10-G paused active transfer changed payload bytes");

    expect(transport->submit(InterestAction{202, true})
               && transport->submit(ConnectAction{generation, 202, "127.0.0.1",
                                                   static_cast<std::uint16_t>(deferredPort)}),
           "K10-G paused outbound connect was not accepted for deferral");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto pausedDeferredOwner = server1::transport::boundEndpointOwner(
        *transport, "127.0.0.1", static_cast<std::uint16_t>(deferredPort));
    expect(pausedDeferredOwner == 0,
           "K10-G paused outbound connect escaped before resume");

    const RequestAction deferred{{604, generation, 1}, 202, {1, 0, 0, 16384}};
    expect(transport->submit(deferred), "K10-G deferred-peer request setup failed");
    const auto pendingBeforeCancel = transport->statistics().ownedRequestsOutstanding;
    expect(pendingBeforeCancel == 1,
           "K10-G deferred-peer request did not enter the ownership ledger");
    expect(!transport->submit(CancelAction{{604, generation - 1, 1}, 202,
                                           deferred.block, true})
               && !transport->submit(CancelAction{{604, 0, 1}, 202,
                                                   deferred.block, true}),
           "K10-G stale or zero cancel generation was accepted");
    expect(transport->statistics().ownedRequestsOutstanding == pendingBeforeCancel,
           "K10-G rejected cancel changed ownership state");
    expect(transport->submit(CancelAction{deferred.ownership, deferred.peer,
                                         deferred.block, true})
               && transport->statistics().ownedRequestsOutstanding == 0,
           "K10-G current cancel did not settle the exact owned request");

    expect(transport->submit(PauseAction{generation, false})
               && transport->submit(PauseAction{generation, false}),
           "K10-G current resume was not idempotent");
    expect(!transport->statistics().paused, "K10-G resumed statistic did not publish");
    waitEndpointOwner(*transport, "127.0.0.1", static_cast<std::uint16_t>(deferredPort),
                      202, "K10-G deferred resume");
    expect(server1::transport::boundEndpointOwner(
               *transport, "127.0.0.1", static_cast<std::uint16_t>(deferredPort)) == 202,
           "K10-G resume did not drain deferred connect ownership");
    expect(server1::transport::ownedNativeAddCount(*transport) == addsBefore + 1
               && server1::transport::framedRequestCount(*transport) == framedBefore + 1,
           "K10-G paused active transfer was not the only new wire request");
    transport->close();
    std::cout << "K10-G PASS active_bytes=16384 deferred_connect=1 stale_generation=0"
              << " handle_pause_calls=0\n";
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
    const RequestAction request{{52, 1, 9}, 77, {0, 0, 0, 16384}};
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
    expect(!transport->submit(sameIdNewGeneration),
           "K10-02 stale generation must not enter the ownership ledger");
    const RequestAction thirdPeer{{10, 1, 5}, 82, {0, 0, 0, 16384}};
    expect(transport->submit(thirdPeer), "K10-02 several-peer queue must accept independent owner");
    expect(!transport->submit(CancelAction{{9, 0, 4}, 80, first.block, true}),
           "K10-02 stale head-of-line attempt must remain retryable");
    expect(transport->submit(CancelAction{first.ownership, first.peer, first.block, true}),
           "K10-02 head-of-line exact retry must reconcile");
    expect(!transport->submit(first), "K10-02 canceled ownership must never replay");
    expect(transport->statistics().ownedRequestsOutstanding == 1,
           "K10-02 cancellation decrements exactly one active request");

    auto otherInfoHash = server1::transport::makeLibTorrent2Adapter(
        (secondTorrent / "K10-wire.torrent").string(), (directory / "second-download").string());
    expect(bool(otherInfoHash) && otherInfoHash->configureAutonomy({}),
           "K10-02 second infohash adapter setup");
    expect(otherInfoHash->submit(first),
           "K10-02 ownership ledger is isolated across infohash adapters");
    expect(otherInfoHash->statistics().ownedRequestsOutstanding == 1
               && transport->statistics().ownedRequestsOutstanding == 1,
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
           }) == 1, "K10-02 close terminalizes each current-generation request once");
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
    const std::string magnetTracker = "http://magnet-tracker.invalid/announce";
    const std::string magnetUrlSeed = "http://magnet-seed.invalid/K10-wire.bin";
    prepare(directory);
    const auto hash = readInfoHash(directory);
    NativeMetadataSeeder seeder(directory);
    TorrentSource source = InfoHashSource{};
    if (magnet) source = MagnetSource{"magnet:?xt=urn:btih:" + infoHashHex(hash)
        + "&tr=http%3A%2F%2Fmagnet-tracker.invalid%2Fannounce"
        + "&ws=http%3A%2F%2Fmagnet-seed.invalid%2FK10-wire.bin"};
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
    if (magnet) {
        expect(metadata.trackers == std::vector<std::string>{magnetTracker},
               "K10-E magnet ready observation did not carry normalized live trackers");
        expect(metadata.urlSeeds == std::vector<std::string>{magnetUrlSeed},
               "K10-E magnet ready observation did not carry normalized live URL seeds");
    }
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

void caseUploadFeasibility(const fs::path &directory, int port)
{
    constexpr EngineGeneration generation = 707;
    constexpr PeerHandle peer = 301;
    auto transport = server1::ports::openTorrentTransport(TorrentOpenRequest{
        generation, readInfoHash(directory),
        MetainfoSource{readBytes(directory / "K10-wire.torrent")},
        (directory / "upload-download").string()});
    expect(bool(transport) && transport->configureAutonomy({}), "K10-H feasibility setup");
    const auto metadata = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<MetadataReadyObservation>(item);
        });
    });
    expect(std::count_if(metadata.begin(), metadata.end(), [](const auto &item) {
               return std::holds_alternative<MetadataReadyObservation>(item);
           }) == 1, "K10-H metadata readiness missing");
    expect(transport->submit(AdvertisePieceAction{generation, 1}),
           "K10-H committed piece advertisement rejected");
    expect(transport->submit(ChokeAction{peer, false}), "K10-H local unchoke rejected");
    expect(transport->submit(ConnectAction{generation, peer, "127.0.0.1",
                                           static_cast<std::uint16_t>(port)}),
           "K10-H raw upload peer connection rejected");

    const auto observations = pollUntil(*transport, [](const auto &items) {
        const auto requests = std::count_if(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<UploadRequestObservation>(item);
        });
        const auto cancels = std::count_if(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<UploadCancelObservation>(item);
        });
        return requests >= 2 && cancels >= 1;
    });
    std::vector<UploadRequestObservation> requests;
    std::vector<UploadCancelObservation> cancels;
    for (const auto &item : observations) {
        if (const auto *request = std::get_if<UploadRequestObservation>(&item))
            requests.push_back(*request);
        if (const auto *cancel = std::get_if<UploadCancelObservation>(&item))
            cancels.push_back(*cancel);
    }
    expect(requests.size() == 2 && cancels.size() == 1,
           "K10-H on_request/on_cancel interception count mismatch");
    expect(requests[0].ownership.requestId != 0
               && requests[1].ownership.requestId > requests[0].ownership.requestId
               && requests[0].ownership.generation == generation
               && cancels[0].ownership.requestId == requests[0].ownership.requestId,
           "K10-H upload ownership retry monotonicity mismatch");
    expect(requests[1].block.piece == 1 && requests[1].block.offset == 0
               && requests[1].block.length == 73 && requests[1].block.blockOrdinal == 0,
           "K10-H final-tail block identity mismatch");

    UploadResponseAction response{requests[1].ownership, peer, requests[1].block,
                                  std::vector<std::uint8_t>(73, 'R')};
    expect(transport->submit(response), "K10-H exact upload response rejected");
    std::fill(response.payload.begin(), response.payload.end(), static_cast<std::uint8_t>('X'));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline
           && transport->statistics().uploadResponsesFramed != 1)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto stats = transport->statistics();
    expect(stats.uploadRequestsAccepted == 2 && stats.uploadRequestsCancelled == 1
               && stats.uploadResponsesFramed == 1 && stats.uploadPayloadBytesFramed == 73
               && stats.ownedUploadsOutstanding == 0,
           "K10-H upload feasibility accounting mismatch");
    transport->close();
    std::cout << "K10-H FEASIBILITY PASS request=2 cancel=1 retry_id=monotonic"
              << " have=raw piece=raw copied_lifetime=1 network_tick=1\n";
}

void caseUploadCaps(const fs::path &directory, const std::vector<int> &ports)
{
    constexpr EngineGeneration generation = 708;
    expect(ports.size() == 6, "K10-H caps port inventory");
    auto transport = server1::ports::openTorrentTransport(TorrentOpenRequest{
        generation, readInfoHash(directory),
        MetainfoSource{readBytes(directory / "K10-wire.torrent")},
        (directory / "caps-download").string()});
    expect(bool(transport) && transport->configureAutonomy({}), "K10-H caps setup");
    const auto metadata = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<MetadataReadyObservation>(item);
        });
    });
    expect(!metadata.empty(), "K10-H caps metadata readiness missing");
    for (std::uint32_t piece = 0; piece < 5; ++piece)
        expect(transport->submit(AdvertisePieceAction{generation, piece}),
               "K10-H caps advertisement rejected");
    expect(transport->submit(AdvertisePieceAction{generation, 0}),
           "K10-H repeated advertisement was not idempotent");
    expect(!transport->submit(AdvertisePieceAction{generation - 1, 0})
               && !transport->submit(AdvertisePieceAction{generation, 6}),
           "K10-H stale or out-of-range advertisement accepted");

    for (std::size_t index = 0; index < 1; ++index) {
        const auto peer = static_cast<PeerHandle>(401 + index);
        expect(transport->submit(ChokeAction{peer, false}), "K10-H caps unchoke rejected");
        expect(transport->submit(ConnectAction{generation, peer, "127.0.0.1",
                                               static_cast<std::uint16_t>(ports[index])}),
               "K10-H caps peer connect rejected");
    }
    const auto firstPeer = pollUntil(*transport, [](const auto &items) {
        return std::count_if(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<UploadRequestObservation>(item);
        }) >= kMaxOutstandingUploadsPerPeer;
    }, std::chrono::seconds(12));
    std::vector<UploadRequestObservation> uploads;
    for (const auto &item : firstPeer)
        if (const auto *upload = std::get_if<UploadRequestObservation>(&item))
            uploads.push_back(*upload);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    expect(uploads.size() == kMaxOutstandingUploadsPerPeer,
           "K10-H per-peer admission cap mismatch");
    expect(std::count_if(uploads.begin(), uploads.end(), [](const auto &upload) {
               return upload.peer == 401 && upload.block.piece == 0;
           }) == 1, "K10-H live duplicate block was admitted twice");
    expect(transport->statistics().uploadRequestsRejected >= 3,
           "K10-H invalid, unadvertised, duplicate, and per-peer-cap requests were not suppressed");

    for (std::size_t index = 1; index < 5; ++index) {
        const auto peer = static_cast<PeerHandle>(401 + index);
        expect(transport->submit(ChokeAction{peer, false}), "K10-H caps unchoke rejected");
        expect(transport->submit(ConnectAction{generation, peer, "127.0.0.1",
                                               static_cast<std::uint16_t>(ports[index])}),
               "K10-H caps peer connect rejected");
    }
    const auto admitted = pollUntil(*transport, [](const auto &items) {
        return std::count_if(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<UploadRequestObservation>(item);
        }) >= kMaxOutstandingUploadsGlobal - kMaxOutstandingUploadsPerPeer;
    }, std::chrono::seconds(12));
    for (const auto &item : admitted)
        if (const auto *upload = std::get_if<UploadRequestObservation>(&item))
            uploads.push_back(*upload);
    expect(uploads.size() == kMaxOutstandingUploadsGlobal,
           "K10-H global admission did not reach exact cap");
    for (PeerHandle peer = 401; peer <= 405; ++peer)
        expect(std::count_if(uploads.begin(), uploads.end(), [&](const auto &upload) {
                   return upload.peer == peer;
               }) == kMaxOutstandingUploadsPerPeer,
               "K10-H per-peer admission cap mismatch");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto rejectedBeforeGlobal = transport->statistics().uploadRequestsRejected;
    expect(transport->submit(ChokeAction{406, false}), "K10-H global cap unchoke rejected");
    expect(transport->submit(ConnectAction{generation, 406, "127.0.0.1",
                                           static_cast<std::uint16_t>(ports[5])}),
           "K10-H global cap peer connect rejected");
    const auto rejectDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < rejectDeadline) {
        const auto current = transport->statistics();
        if (current.connectedPeers >= 6
            && current.uploadRequestsRejected > rejectedBeforeGlobal) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto capped = transport->statistics();
    expect(capped.connectedPeers >= 6
               && capped.uploadRequestsAccepted == kMaxOutstandingUploadsGlobal
               && capped.uploadRequestsRejected > rejectedBeforeGlobal
               && capped.ownedUploadsOutstanding == kMaxOutstandingUploadsGlobal,
           "K10-H global cap did not reject the twenty-first live ownership");

    for (const auto &upload : uploads)
        expect(transport->submit(UploadAbortAction{upload.ownership, upload.peer, upload.block}),
               "K10-H exact upload abort rejected");
    expect(!transport->submit(UploadResponseAction{uploads.front().ownership,
                                                    uploads.front().peer,
                                                    uploads.front().block,
                                                    std::vector<std::uint8_t>(16384, 'X')}),
           "K10-H second terminal action was accepted");
    const auto abortDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < abortDeadline
           && transport->statistics().uploadRequestsAborted != uploads.size())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto stats = transport->statistics();
    expect(stats.uploadRequestsAborted == uploads.size() && stats.ownedUploadsOutstanding == 0
               && stats.uploadResponsesFramed == 0 && stats.uploadPayloadBytesFramed == 0
               && stats.uploadActionsRejected >= 3,
           "K10-H abort/rejection accounting mismatch");
    transport->close();
    std::cout << "K10-H CAPS PASS per_peer=4 global=20 invalid=2 duplicate=1 replay=5"
              << " abort_local=20 piece_frames=0\n";
}

void caseUploadLifecycle(const fs::path &directory, const std::vector<int> &ports)
{
    constexpr EngineGeneration generation = 709;
    expect(ports.size() == 3, "K10-H lifecycle port inventory");
    auto transport = server1::ports::openTorrentTransport(TorrentOpenRequest{
        generation, readInfoHash(directory),
        MetainfoSource{readBytes(directory / "K10-wire.torrent")},
        (directory / "lifecycle-upload-download").string()});
    expect(bool(transport) && transport->configureAutonomy({}), "K10-H lifecycle setup");
    const auto metadata = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<MetadataReadyObservation>(item);
        });
    });
    expect(!metadata.empty(), "K10-H lifecycle metadata missing");
    expect(transport->submit(AdvertisePieceAction{generation, 0})
               && transport->submit(AdvertisePieceAction{generation, 1}),
           "K10-H lifecycle advertise failed");

    const auto connect = [&](PeerHandle peer, int port) {
        expect(transport->submit(ChokeAction{peer, false}), "K10-H lifecycle unchoke failed");
        expect(transport->submit(ConnectAction{generation, peer, "127.0.0.1",
                                               static_cast<std::uint16_t>(port)}),
               "K10-H lifecycle connect failed");
    };
    const auto waitRequest = [&](PeerHandle peer) {
        const auto items = pollUntil(*transport, [&](const auto &all) {
            return std::any_of(all.begin(), all.end(), [&](const auto &item) {
                const auto *upload = std::get_if<UploadRequestObservation>(&item);
                return upload && upload->peer == peer;
            });
        });
        const auto found = std::find_if(items.begin(), items.end(), [&](const auto &item) {
            const auto *upload = std::get_if<UploadRequestObservation>(&item);
            return upload && upload->peer == peer;
        });
        expect(found != items.end(), "K10-H lifecycle upload request missing");
        return std::get<UploadRequestObservation>(*found);
    };

    connect(501, ports[0]);
    const auto chokeUpload = waitRequest(501);
    expect(transport->submit(UploadResponseAction{chokeUpload.ownership, 501, chokeUpload.block,
                                                  std::vector<std::uint8_t>(16384, 'L')}),
           "K10-H lifecycle queued response rejected");
    expect(transport->submit(ChokeAction{501, true}), "K10-H lifecycle choke rejected");
    const auto chokeTerminal = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            const auto *cancel = std::get_if<UploadCancelObservation>(&item);
            return cancel && cancel->peer == 501;
        });
    });
    expect(std::count_if(chokeTerminal.begin(), chokeTerminal.end(), [](const auto &item) {
               const auto *cancel = std::get_if<UploadCancelObservation>(&item);
               return cancel && cancel->peer == 501;
           }) == 1, "K10-H choke did not terminalize once");
    const auto staleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < staleDeadline
           && transport->statistics().uploadActionsRejected < 1)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    expect(transport->statistics().uploadResponsesFramed == 0
               && transport->statistics().uploadActionsRejected >= 1,
           "K10-H stale mailbox response wrote after choke");

    connect(502, ports[1]);
    const auto detachedUpload = waitRequest(502);
    const auto detachTerminal = pollUntil(*transport, [](const auto &items) {
        return std::any_of(items.begin(), items.end(), [](const auto &item) {
            const auto *cancel = std::get_if<UploadCancelObservation>(&item);
            return cancel && cancel->peer == 502;
        });
    });
    expect(std::count_if(detachTerminal.begin(), detachTerminal.end(), [](const auto &item) {
               const auto *cancel = std::get_if<UploadCancelObservation>(&item);
               return cancel && cancel->peer == 502;
           }) == 1, "K10-H detach did not terminalize once");
    expect(!transport->submit(UploadAbortAction{detachedUpload.ownership, 502,
                                                detachedUpload.block}),
           "K10-H detached ownership accepted a late action");

    connect(503, ports[2]);
    const auto closeUpload = waitRequest(503);
    transport->close();
    const auto closed = transport->poll();
    expect(std::count_if(closed.begin(), closed.end(), [&](const auto &item) {
               const auto *cancel = std::get_if<UploadCancelObservation>(&item);
               return cancel && cancel->ownership.requestId == closeUpload.ownership.requestId;
           }) == 1, "K10-H close did not terminalize upload once");
    expect(std::count_if(closed.begin(), closed.end(), [](const auto &item) {
               return std::holds_alternative<ClosedObservation>(item);
           }) == 1, "K10-H close was not terminal once");
    expect(!transport->submit(UploadAbortAction{closeUpload.ownership, 503, closeUpload.block}),
           "K10-H closed ownership accepted a late action");
    const auto stats = transport->statistics();
    expect(stats.uploadRequestsAccepted == 3 && stats.uploadRequestsCancelled == 3
               && stats.ownedUploadsOutstanding == 0 && stats.uploadResponsesFramed == 0
               && stats.uploadActionsRejected >= 3,
           "K10-H lifecycle counters mismatch");
    std::cout << "K10-H LIFECYCLE PASS choke=1 detach=1 close=1 stale_mailbox=1"
              << " late_action=reject piece_frames=0\n";
}
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--prepare") { prepare(argv[2]); return 0; }
    if (argc == 3 && std::string(argv[1]) == "--prepare-availability") {
        prepare(argv[2], 'K', 65536); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--prepare-upload") {
        prepare(argv[2], 'U', 16457); return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--prepare-upload-caps") {
        prepare(argv[2], 'C', 98304); return 0;
    }
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
    if (argc == 4 && std::string(argv[1]) == "--availability") {
        caseAvailability(argv[2], std::stoi(argv[3])); return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "--pause") {
        casePauseAndGenerationOwnership(argv[2], std::stoi(argv[3]), std::stoi(argv[4])); return 0;
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
    if (argc == 4 && std::string(argv[1]) == "--upload-feasibility") {
        caseUploadFeasibility(argv[2], std::stoi(argv[3])); return 0;
    }
    if (argc == 9 && std::string(argv[1]) == "--upload-caps") {
        std::vector<int> ports;
        for (int index = 3; index < argc; ++index) ports.push_back(std::stoi(argv[index]));
        caseUploadCaps(argv[2], ports); return 0;
    }
    if (argc == 6 && std::string(argv[1]) == "--upload-lifecycle") {
        std::vector<int> ports;
        for (int index = 3; index < argc; ++index) ports.push_back(std::stoi(argv[index]));
        caseUploadLifecycle(argv[2], ports); return 0;
    }
    std::cerr << "usage: test_native_transport --prepare DIR | --wire DIR PORT_A PORT_B | --lifecycle DIR PORT_A PORT_B | --thread-guard DIR PORT | K10-02 DIR\n";
    return 2;
}
