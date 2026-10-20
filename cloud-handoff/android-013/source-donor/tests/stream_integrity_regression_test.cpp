// End-to-end regression for the HTTP streaming read path: the engine's real
// production serve path only delivers whole-piece hash-verified bytes.
//
// Fixture strategy (mirrors engine_api_test's payload-cache seeding): the
// fixture payload is pre-placed in the engine's payload cache directory, so
// libtorrent's check-in pass verifies pieces 0..1 while the corrupt piece 2
// fails its whole-piece hash. The healthy torrent must stream the full
// payload, honor Range (seek) requests, and survive a client disconnect
// mid-stream; the corrupt torrent must stall the HTTP body exactly at the
// verified boundary instead of streaming unverified blocks.
//
// No peers, no trackers, no discovery: discovery is force-disabled
// (NUVIO_ENGINE_DISABLE_DISCOVERY=1) so this fixture can never bootstrap
// public DHT/LSD/UPnP/NAT-PMP traffic.
#include "test_support.hpp"

#include "nuvio_engine/nuvio_engine.h"
#include "torrent/libtorrent_stream_bridge.hpp"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/info_hash.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
const bool discovery_guard = [] {
    return _putenv("NUVIO_ENGINE_DISABLE_DISCOVERY=1") == 0;
}();
#else
const bool discovery_guard = [] {
    return setenv("NUVIO_ENGINE_DISABLE_DISCOVERY", "1", 1) == 0;
}();
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t piece_bytes = 32 * 1024;
constexpr std::uint64_t file_bytes = 5 * piece_bytes;
constexpr std::uint64_t corrupt_piece = 2;
constexpr std::uint64_t corrupt_offset = corrupt_piece * piece_bytes + 4096;
constexpr std::uint64_t corrupt_length = 64;

class WinsockGuard {
public:
#if defined(_WIN32)
    WinsockGuard() {
        WSADATA data{};
        initialized_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockGuard() {
        if (initialized_) {
            WSACleanup();
        }
    }

private:
    bool initialized_ = false;
#endif
};

const WinsockGuard winsock_guard{};

class FixtureDirectories {
public:
    FixtureDirectories() {
        const auto nonce = Clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("nuvio-stream-integrity-" + std::to_string(nonce));
        data_ = root_ / "data";
        cache_ = root_ / "cache";
        std::filesystem::create_directories(data_);
        std::filesystem::create_directories(cache_);
    }

    ~FixtureDirectories() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] const std::filesystem::path& data() const { return data_; }
    [[nodiscard]] const std::filesystem::path& cache() const { return cache_; }

private:
    std::filesystem::path root_;
    std::filesystem::path data_;
    std::filesystem::path cache_;
};

std::string make_payload() {
    std::string payload;
    payload.resize(file_bytes);
    for (std::uint64_t index = 0; index < file_bytes; ++index) {
        payload[static_cast<std::size_t>(index)] =
            static_cast<char>((index * 31 + 7) & 0xFF);
    }
    return payload;
}

struct FixtureTorrent {
    std::string torrent_bytes;
    std::string torrent_id;
};

// Builds a v1 torrent whose piece hashes describe the HEALTHY payload.
// CacheSeeded* shapes place bytes into the engine's payload cache (the
// check-in path); Peer* shapes leave the engine's cache empty and put the
// bytes in an in-process loopback seeder instead, so blocks reach the engine
// through the real per-block download path (block_finished alerts fire).
enum class SeederShape {
    CacheSeededHealthy,
    CacheSeededCorrupt,
    PeerHealthy,
    PeerCorruptPiece,
    PeerCorruptFirstPiece,
    PeerCorruptFirstPieceWithUnverifiedDisk,
};

FixtureTorrent make_fixture_torrent(
    const FixtureDirectories& directories,
    const std::string& payload,
    const SeederShape shape,
    const std::optional<std::uint16_t> &tracker_port = {}
) {
    const auto staging = directories.root() / "staging";
    std::filesystem::create_directories(staging);
    {
        std::ofstream output(staging / "fixture.bin", std::ios::binary);
        output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    lt::file_storage files;
    files.add_file("fixture.bin", static_cast<std::int64_t>(payload.size()));
    lt::create_torrent creator(
        files,
        static_cast<int>(piece_bytes),
        lt::create_torrent::v1_only
    );
    if (tracker_port.has_value()) {
        creator.add_tracker(
            "http://127.0.0.1:" + std::to_string(*tracker_port) + "/announce"
        );
    }
    lt::set_piece_hashes(creator, staging.string());
    const lt::entry encoded = creator.generate();
    std::string torrent;
    lt::bencode(std::back_inserter(torrent), encoded);

    const lt::torrent_info info(
        reinterpret_cast<const char*>(torrent.data()), static_cast<int>(torrent.size())
    );
    const auto torrent_id = lt::aux::to_hex(info.info_hashes().v1);

    auto served = payload;
    if (shape == SeederShape::CacheSeededCorrupt ||
        shape == SeederShape::PeerCorruptPiece) {
        // The torrent hashes describe the healthy payload; flipping bytes in
        // the served copy makes piece 2 fail whole-piece hash verification.
        for (std::uint64_t index = 0; index < corrupt_length; ++index) {
            served[static_cast<std::size_t>(corrupt_offset + index)] =
                static_cast<char>(0x5A);
        }
    } else if (shape == SeederShape::PeerCorruptFirstPiece ||
               shape == SeederShape::PeerCorruptFirstPieceWithUnverifiedDisk) {
        // A full-size file with a subtle corruption inside piece 0's second
        // block: the peer happily serves every block (block_finished alerts
        // fire for the whole piece) while the piece hash can NEVER pass —
        // precisely the speculative-delivery window the hardening closed.
        for (std::uint64_t index = 0; index < corrupt_length; ++index) {
            served[static_cast<std::size_t>(piece_bytes - 256 + index)] =
                static_cast<char>(0x5A);
        }
    }
    if (shape == SeederShape::PeerHealthy ||
        shape == SeederShape::PeerCorruptPiece ||
        shape == SeederShape::PeerCorruptFirstPiece ||
        shape == SeederShape::PeerCorruptFirstPieceWithUnverifiedDisk) {
        // Peer-driven fixture: the seeder holds the bytes and the engine's
        // own cache stays empty so every block arrives through the real
        // download path.
        const auto seeder_dir = directories.root() / "seeder";
        std::filesystem::create_directories(seeder_dir);
        std::ofstream output(seeder_dir / "fixture.bin", std::ios::binary);
        output.write(served.data(), static_cast<std::streamsize>(served.size()));
        return {torrent, torrent_id};
    }
    const auto engine_payload_directory =
        directories.cache() / "payload" / torrent_id;
    std::filesystem::create_directories(engine_payload_directory);
    {
        std::ofstream output(engine_payload_directory / "fixture.bin", std::ios::binary);
        output.write(served.data(), static_cast<std::streamsize>(served.size()));
    }
    return {torrent, torrent_id};
}

class EngineSession {
public:
    explicit EngineSession(const FixtureDirectories& directories) {
        const auto data = directories.data().string();
        const auto cache = directories.cache().string();
        nuvio_engine_config config;
        nuvio_engine_config_init(&config);
        config.data_directory = data.c_str();
        config.cache_directory = cache.c_str();
        config.memory_cache_capacity_bytes = 4 * 1024 * 1024;
        config.disk_cache_capacity_bytes = 8 * 1024 * 1024;
        config.listen_port = 0;
        config.upload_mode = NUVIO_ENGINE_UPLOAD_DISABLED;
        config.torrent_profile = NUVIO_ENGINE_TORRENT_PROFILE_SOFT;
        const auto status = nuvio_engine_create(&config, &engine_);
        if (status != NUVIO_ENGINE_STATUS_OK) {
            throw std::runtime_error(
                "engine create failed: " +
                std::string(nuvio_engine_status_message(status))
            );
        }
    }

    ~EngineSession() {
        if (engine_ != nullptr) {
            nuvio_engine_destroy(engine_);
        }
    }

    EngineSession(const EngineSession&) = delete;
    EngineSession& operator=(const EngineSession&) = delete;

    void add_from_bytes(const std::string& torrent) {
        nuvio_engine_torrent_request request;
        nuvio_engine_torrent_request_init(&request);
        request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
        request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
        request.torrent_data_size = torrent.size();
        std::uint64_t request_id = 0;
        const auto status = nuvio_engine_add_torrent(engine_, &request, &request_id);
        if (status != NUVIO_ENGINE_STATUS_OK) {
            throw std::runtime_error(
                "add_torrent failed: " +
                std::string(nuvio_engine_status_message(status))
            );
        }
    }



    std::string wait_for_stream_url() {
        const auto deadline = Clock::now() + std::chrono::seconds(30);
        std::string torrent_id;
        bool prepared_requested = false;
        std::string last_error;
        while (Clock::now() < deadline) {
            nuvio_engine_event event;
            nuvio_engine_event_init(&event);
            const auto status = nuvio_engine_poll_event(engine_, &event);
            if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (status != NUVIO_ENGINE_STATUS_OK) {
                throw std::runtime_error("poll_event failed");
            }
            if (event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY &&
                !prepared_requested) {
                torrent_id = event.torrent_id;
                size_t file_count = 0;
                if (nuvio_engine_get_file_count(engine_, event.torrent_id, &file_count) !=
                        NUVIO_ENGINE_STATUS_OK ||
                    file_count == 0) {
                    throw std::runtime_error("no files in fixture torrent");
                }
                nuvio_engine_file file;
                nuvio_engine_file_init(&file);
                if (nuvio_engine_get_file(engine_, event.torrent_id, 0, &file) !=
                    NUVIO_ENGINE_STATUS_OK) {
                    throw std::runtime_error("get_file failed");
                }
                nuvio_engine_stream_request request;
                nuvio_engine_stream_request_init(&request);
                request.torrent_id = torrent_id.c_str();
                request.file_index = 0;
                request.filename_hint = file.path;
                std::uint64_t request_id = 0;
                const auto prepared_status =
                    nuvio_engine_prepare_stream(engine_, &request, &request_id);
                if (prepared_status != NUVIO_ENGINE_STATUS_OK) {
                    throw std::runtime_error(
                        std::string("prepare_stream failed: ") +
                        nuvio_engine_status_message(prepared_status)
                    );
                }
                prepared_requested = true;
                continue;
            }
            if (event.type == NUVIO_ENGINE_EVENT_STREAM_PREPARED &&
                prepared_requested) {
                return event.stream_url;
            }
            if (event.type == NUVIO_ENGINE_EVENT_TORRENT_ERROR) {
                last_error = event.message;
            }
        }
        throw std::runtime_error(
            std::string("no STREAM_PREPARED event (torrent_id=") + torrent_id +
            (last_error.empty() ? std::string() : (", error: " + last_error)) +
            ")"
        );
    }

private:
    nuvio_engine* engine_ = nullptr;
};

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#define CLOSE_SOCKET closesocket
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#define CLOSE_SOCKET close
#endif

struct Fetched {
    std::string headers;
    std::string body;
    bool closed_by_server = false;
};

Fetched fetch(
    const std::string& url,
    const std::optional<std::string>& range_header,
    const std::chrono::milliseconds deadline,
    const std::optional<std::uint64_t> stop_after_body_bytes
) {
    if (url.rfind("http://127.0.0.1:", 0) != 0) {
        throw std::runtime_error("unexpected stream url: " + url);
    }
    const auto target_position = url.find('/', 17);
    if (target_position == std::string::npos) {
        throw std::runtime_error("stream url without target: " + url);
    }
    const auto port = static_cast<std::uint16_t>(
        std::stoi(url.substr(17, target_position - 17))
    );
    const auto target = url.substr(target_position);

    const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == kInvalidSocket) {
        throw std::runtime_error("fetch: socket failed");
    }
#if defined(_WIN32)
    constexpr DWORD receive_timeout = 200;
#else
    constexpr timeval receive_timeout{0, 200 * 1000};
#endif
    setsockopt(
        client,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&receive_timeout),
        sizeof(receive_timeout)
    );
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        CLOSE_SOCKET(client);
        throw std::runtime_error("fetch: connect failed");
    }

    std::string request = "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (range_header.has_value()) {
        request += "Range: " + *range_header + "\r\n";
    }
    request += "Connection: close\r\n\r\n";
    std::size_t offset = 0;
    while (offset < request.size()) {
        const auto sent = send(
            client,
            request.data() + offset,
            static_cast<int>(request.size() - offset),
            0
        );
        if (sent <= 0) {
            CLOSE_SOCKET(client);
            throw std::runtime_error("fetch: send failed");
        }
        offset += static_cast<std::size_t>(sent);
    }

    Fetched fetched;
    std::string raw;
    const auto stop_at = Clock::now() + deadline;
    char chunk[16 * 1024];
    while (Clock::now() < stop_at) {
        const auto received = recv(client, chunk, sizeof(chunk), 0);
        if (received > 0) {
            raw.append(chunk, static_cast<std::size_t>(received));
            const auto separator = raw.find("\r\n\r\n");
            if (separator != std::string::npos) {
                fetched.headers = raw.substr(0, separator + 4);
                fetched.body = raw.substr(separator + 4);
            }
            if (stop_after_body_bytes.has_value() &&
                fetched.body.size() >= stop_after_body_bytes.value()) {
                CLOSE_SOCKET(client);
                return fetched;
            }
            continue;
        }
        if (received == 0) {
            fetched.closed_by_server = true;
            CLOSE_SOCKET(client);
            return fetched;
        }
    }
    CLOSE_SOCKET(client);
    return fetched;
}

void run_stream_integrity_regression(const bool corrupt) {
    const std::string payload = make_payload();
    FixtureDirectories directories;
    const auto fixture = make_fixture_torrent(
        directories,
        payload,
        corrupt ? SeederShape::CacheSeededCorrupt : SeederShape::CacheSeededHealthy
    );

    EngineSession engine(directories);
    engine.add_from_bytes(fixture.torrent_bytes);
    const auto url = engine.wait_for_stream_url();

    const auto corrupt_piece_start = corrupt_piece * piece_bytes;
    if (!corrupt) {
        // Full verified delivery: stop as soon as the whole payload arrived;
        // the engine keeps the route anchored after the last byte, so a
        // server-side close is not part of the contract under test here.
        const auto complete =
            fetch(url, {}, std::chrono::seconds(30), payload.size());
        std::cout << "healthy headers=" << complete.headers.size()
                  << " body=" << complete.body.size() << std::endl;
        nuvio::test::expect_equal(
            complete.body.size(),
            payload.size(),
            "streamed body size",
            "payload size"
        );
        nuvio::test::expect_true(
            complete.body == payload,
            "streamed body should equal the seeded payload"
        );

        // Seek: a mid-file Range request must return the matching verified slice.
        const auto sought = fetch(
            url,
            std::string("bytes=") + std::to_string(piece_bytes) + "-" +
                std::to_string(piece_bytes + 100),
            std::chrono::seconds(30),
            101
        );
        nuvio::test::expect_true(
            sought.body == payload.substr(
                static_cast<std::size_t>(piece_bytes),
                101
            ),
            "range request should return the requested verified slice"
        );

        // Cancellation: dropping the connection mid-stream must not wedge the
        // engine; destruction below exercises demand teardown.
        const auto cancelled = fetch(url, {}, std::chrono::seconds(30), piece_bytes);
        nuvio::test::expect_true(
            cancelled.body.size() >= piece_bytes,
            "cancelled stream should have delivered at least one verified piece"
        );
        return;
    }

    const auto first_pass =
        fetch(url, {}, std::chrono::seconds(8), {});
    std::cout << "corrupt regression first pass body bytes: "
              << first_pass.body.size() << std::endl;
    if (first_pass.body.empty() ||
        first_pass.body.size() > corrupt_piece_start) {
        throw std::runtime_error(
            "body must stall at the verified boundary before the corrupt piece (got " +
            std::to_string(first_pass.body.size()) + " bytes)"
        );
    }
    const auto second_pass =
        fetch(url, {}, std::chrono::seconds(8), {});
    if (second_pass.body.size() != first_pass.body.size() ||
        second_pass.body.size() > corrupt_piece_start) {
        throw std::runtime_error(
            "body must make no unverified progress past the corrupt piece (got " +
            std::to_string(second_pass.body.size()) + " bytes after " +
            std::to_string(first_pass.body.size()) + " bytes)"
        );
    }
    nuvio::test::expect_true(
        first_pass.body == payload.substr(0, first_pass.body.size()),
        "delivered prefix must match the verified payload bytes"
    );
}

// --- Peer-driven probes: real block_finished delivery through a loopback peer -
//
// The check-in fixtures above verify pieces from disk before any demand; they
// can never exercise the speculative block_finished window that the hardening
// removed. These scenarios route every byte through an in-process loopback
// tracker + seeder so the engine's production serve path sees partial and
// corrupt blocks exactly as a real swarm delivers them.

class LoopbackTracker {
public:
    LoopbackTracker() {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = 0;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_ == kInvalidSocket) {
            throw std::runtime_error("tracker: socket failed");
        }
        if (bind(listen_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listen_, 16) != 0) {
            CLOSE_SOCKET(listen_);
            throw std::runtime_error("tracker: bind/listen failed");
        }
        sockaddr_in bound{};
        int bound_size = sizeof(bound);
        if (getsockname(
                listen_, reinterpret_cast<sockaddr*>(&bound), &bound_size
            ) != 0) {
            CLOSE_SOCKET(listen_);
            throw std::runtime_error("tracker: getsockname failed");
        }
        port_ = ntohs(bound.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~LoopbackTracker() {
        stop_ = true;
        CLOSE_SOCKET(listen_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void register_peer(const std::uint16_t peer_port) {
        std::lock_guard lock(mutex_);
        peer_port_ = peer_port;
    }

    [[nodiscard]] std::uint16_t port() const { return port_; }

private:
    void serve() {
        while (!stop_.load()) {
            const auto client = accept(listen_, nullptr, nullptr);
            if (client == kInvalidSocket) {
                return;
            }
            handle(client);
            CLOSE_SOCKET(client);
        }
    }

    void handle(const NativeSocket client) {
        std::string request;
        char chunk[2048];
        while (request.find("\r\n\r\n") == std::string::npos &&
               request.size() < 8192) {
#if defined(_WIN32)
            const int received = recv(client, chunk, sizeof(chunk), 0);
#else
            const ssize_t received = recv(client, chunk, sizeof(chunk), 0);
#endif
            if (received <= 0) {
                return;
            }
            request.append(chunk, static_cast<std::size_t>(received));
        }
        if (request.rfind("GET /announce", 0) != 0) {
            return;
        }
        std::uint16_t peer = 0;
        {
            std::lock_guard lock(mutex_);
            peer = peer_port_;
        }
        if (peer == 0) {
            return;
        }
        // Never hand a peer its own address: with libtorrent's default one
        // connection per IP, the seeder's self-connection would permanently
        // occupy its 127.0.0.1 slot and every engine connection would be
        // rejected at the handshake.
        const auto port_mark = request.find("&port=");
        const auto announcer_port_end = port_mark == std::string::npos
            ? std::string::npos
            : request.find('&', port_mark + 6);
        std::string body;
        const bool announcer_is_seeder =
            port_mark != std::string::npos &&
            request.substr(port_mark + 6,
                (announcer_port_end == std::string::npos
                     ? request.find(' ', port_mark + 6)
                     : announcer_port_end) - (port_mark + 6)) ==
                std::to_string(peer);
        if (announcer_is_seeder) {
            body = "d8:completei1e10:incompletei0e8:intervali5e5:peers0:e";
        } else {
            const std::string peer_bytes{
                static_cast<char>(0x7F), static_cast<char>(0x00),
                static_cast<char>(0x00), static_cast<char>(0x01),
                static_cast<char>((peer >> 8) & 0xFF),
                static_cast<char>(peer & 0xFF)};
            body =
                "d8:completei1e10:incompletei1e8:intervali5e5:peers6:" +
                peer_bytes + "e";
        }
        const std::string response =
            "HTTP/1.1 200 OK\r\nContent-Length: " +
            std::to_string(body.size()) +
            "\r\nConnection: close\r\n\r\n" + body;
        std::size_t offset = 0;
        while (offset < response.size()) {
            const auto sent = send(
                client,
                response.data() + offset,
                static_cast<int>(response.size() - offset),
                0
            );
            if (sent <= 0) {
                return;
            }
            offset += static_cast<std::size_t>(sent);
        }
    }

    NativeSocket listen_ = kInvalidSocket;
    std::uint16_t port_ = 0;
    std::uint16_t peer_port_ = 0;
    std::mutex mutex_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// Seeds the fixture bytes on loopback. seed_mode keeps libtorrent from
// checking the seeder's files, so a corrupt file is served verbatim —
// exactly the hostile peer the hardening must survive.
class SeederSession {
public:
    SeederSession(
        const std::string &torrent_bytes,
        const std::filesystem::path &save_path
    ) {
        lt::settings_pack settings;
        settings.set_str(
            lt::settings_pack::listen_interfaces,
            "127.0.0.1:0"
        );
        settings.set_bool(lt::settings_pack::enable_dht, false);
        settings.set_bool(lt::settings_pack::enable_lsd, false);
        settings.set_bool(lt::settings_pack::enable_upnp, false);
        settings.set_bool(lt::settings_pack::enable_natpmp, false);
        settings.set_int(
            lt::settings_pack::alert_mask,
            static_cast<std::int64_t>(lt::alert_category::status |
                                      lt::alert_category::error |
                                      lt::alert_category::connect)
        );
        session_ = std::make_unique<lt::session>(settings);
        const std::vector<char> buffer(torrent_bytes.begin(), torrent_bytes.end());
        auto params = lt::load_torrent_buffer(buffer);
        params.save_path = save_path.string();
        params.flags |= lt::torrent_flags::seed_mode;
        session_->add_torrent(params);
        port_ = static_cast<std::uint16_t>(session_->listen_port());
    }

    ~SeederSession() = default;

    [[nodiscard]] std::uint16_t port() const { return port_; }



private:
    std::unique_ptr<lt::session> session_;
    std::uint16_t port_ = 0;
};

void run_peer_driven_regression(const SeederShape shape) {
    const std::string payload = make_payload();
    FixtureDirectories directories;
    LoopbackTracker tracker;
    const auto fixture = make_fixture_torrent(
        directories, payload, shape, tracker.port()
    );
    SeederSession seeder(
        fixture.torrent_bytes, directories.root() / "seeder"
    );
    tracker.register_peer(seeder.port());

    EngineSession engine(directories);
    engine.add_from_bytes(fixture.torrent_bytes);
    const auto url = engine.wait_for_stream_url();

    if (shape == SeederShape::PeerCorruptFirstPieceWithUnverifiedDisk) {
        // Adversarial disk race: a partial/unverified payload file is already
        // readable while the loopback peer supplies corrupt blocks. The
        // speculative PayloadReader previously regarded finished blocks as
        // sufficient to publish these bytes. This is NOT a verified cache
        // seed; neither source can produce a valid piece hash.
        auto partial = payload;
        for (std::uint64_t index = 0; index < corrupt_length; ++index) {
            partial[static_cast<std::size_t>(piece_bytes - 256 + index)] =
                static_cast<char>(0x5A);
        }
        const auto disk_path = directories.cache() / "payload" /
            fixture.torrent_id / "fixture.bin";
        std::filesystem::create_directories(disk_path.parent_path());
        std::ofstream output(disk_path, std::ios::binary);
        output.write(partial.data(), static_cast<std::streamsize>(partial.size()));
        output.close();
        if (!output) throw std::runtime_error("unverified-disk fixture write failed");
    }

    if (shape == SeederShape::PeerHealthy) {
        // Verified bytes AFTER the piece actually verifies, over the real
        // download path: full payload, Range seek, and mid-stream cancel.
        const auto complete =
            fetch(url, {}, std::chrono::seconds(30), payload.size());
        nuvio::test::expect_equal(
            complete.body.size(),
            payload.size(),
            "peer-streamed body size",
            "payload size"
        );
        nuvio::test::expect_true(
            complete.body == payload,
            "peer-streamed body should equal the healthy payload"
        );
        const auto sought = fetch(
            url,
            std::string("bytes=") + std::to_string(piece_bytes) + "-" +
                std::to_string(piece_bytes + 100),
            std::chrono::seconds(30),
            101
        );
        nuvio::test::expect_true(
            sought.body == payload.substr(
                static_cast<std::size_t>(piece_bytes),
                101
            ),
            "peer range request should return the requested verified slice"
        );
        const auto cancelled =
            fetch(url, {}, std::chrono::seconds(30), piece_bytes);
        nuvio::test::expect_true(
            cancelled.body.size() >= piece_bytes,
            "cancelled peer stream should have delivered a verified piece"
        );
        return;
    }

    if (shape == SeederShape::PeerCorruptFirstPiece ||
        shape == SeederShape::PeerCorruptFirstPieceWithUnverifiedDisk) {
        // The peer serves every block of piece 0 (each block_finished fires)
        // but the piece hash can never pass: NOT ONE byte may reach the HTTP
        // client. This is the exact window the speculative block_finished
        // path used to serve from disk before verification.
        const auto stalled = fetch(url, {}, std::chrono::seconds(6), {});
        if (!stalled.body.empty()) {
            throw std::runtime_error(
                "partial piece was served before whole-piece verification (got " +
                std::to_string(stalled.body.size()) + " bytes)"
            );
        }
        return;
    }

    // PeerCorruptPiece: healthy pieces 0..1 verify through the real download
    // path; piece 2 blocks arrive but never verify, so the body must stall
    // exactly at the verified boundary and only verified bytes may appear.
    const auto corrupt_piece_start = corrupt_piece * piece_bytes;
    const auto first_pass = fetch(url, {}, std::chrono::seconds(10), {});
    if (first_pass.body.empty() ||
        first_pass.body.size() > corrupt_piece_start) {
        throw std::runtime_error(
            "peer-corrupt body must stall at the verified boundary (got " +
            std::to_string(first_pass.body.size()) + " bytes)"
        );
    }
    const auto second_pass = fetch(url, {}, std::chrono::seconds(10), {});
    if (second_pass.body.size() > corrupt_piece_start) {
        throw std::runtime_error(
            "peer-corrupt body made unverified progress past the corrupt piece (got " +
            std::to_string(second_pass.body.size()) + " bytes)"
        );
    }
    nuvio::test::expect_true(
        first_pass.body == payload.substr(0, first_pass.body.size()),
        "peer-corrupt delivered prefix must match the verified payload bytes"
    );
}

} // namespace

NUVIO_TEST("verified-only stream delivery with seek and cancellation") {
    run_stream_integrity_regression(false);
}

NUVIO_TEST("corrupt piece never streams before hash verification") {
    run_stream_integrity_regression(true);
}

NUVIO_TEST("peer blocks of an unverifiable piece are never served") {
    run_peer_driven_regression(SeederShape::PeerCorruptFirstPiece);
}

NUVIO_TEST("peer corrupt blocks with readable unverified disk never stream") {
    run_peer_driven_regression(SeederShape::PeerCorruptFirstPieceWithUnverifiedDisk);
}

NUVIO_TEST("peer corrupt blocks never stream past the verified boundary") {
    run_peer_driven_regression(SeederShape::PeerCorruptPiece);
}

NUVIO_TEST("peer healthy torrent streams verified payload with seek and cancel") {
    run_peer_driven_regression(SeederShape::PeerHealthy);
}

#if defined(NUVIO_ENGINE_INTEGRITY_TEST_SEAM)
NUVIO_TEST("production waiter holds readable finished blocks until piece hash verification") {
    FixtureDirectories directories;
    const auto path = directories.root() / "readable-unverified-payload.bin";
    std::vector<char> verified(32 * 1024);
    for (std::size_t i = 0; i < verified.size(); ++i) {
        verified[i] = static_cast<char>('A' + (i % 26));
    }
    auto unverified = verified;
    unverified[4096] = static_cast<char>(verified[4096] ^ 0x2a);
    {
        std::ofstream output(path, std::ios::binary);
        nuvio::test::expect_true(bool(output), "fixture file should be writable");
        output.write(unverified.data(), static_cast<std::streamsize>(unverified.size()));
        nuvio::test::expect_true(bool(output), "fixture file should be readable by the old path");
    }
    nuvio::torrent::LibtorrentStreamBridge bridge(
        0, 2 * 1024 * 1024, 2 * 1024 * 1024, std::chrono::minutes(5)
    );
    const auto check = [&](const std::uint64_t begin, const std::uint64_t end,
                           const bool cancel) {
        const auto result = bridge.test_piece_delivery(
            path, verified, begin, end, cancel
        );
        std::cout << "PRODUCTION_WAITER_OBSERVATION begin=" << begin
                  << " end=" << end << " cancel=" << cancel
                  << " before_verified=" << result.before_verification.size()
                  << " after_completion=" << result.after_completion.size()
                  << " delivered_counter=" << result.recorded_delivered_bytes
                  << " served=" << result.serve_succeeded << '\n';
        nuvio::test::expect_true(
            result.before_verification.empty(),
            "UNSAFE: production waiter delivered finished-block bytes before hash verification"
        );
        if (cancel) {
            nuvio::test::expect_true(
                !result.serve_succeeded && result.after_completion.empty(),
                "cancelled waiter must never deliver or claim completion"
            );
        } else {
            const std::vector<char> expected(
                verified.begin() + static_cast<std::ptrdiff_t>(begin),
                verified.begin() + static_cast<std::ptrdiff_t>(end)
            );
            nuvio::test::expect_true(
                result.serve_succeeded && result.after_completion == expected &&
                result.recorded_delivered_bytes == expected.size(),
                "after whole-piece completion exact verified bytes must reach the writer"
            );
        }
    };
    check(0, 8192, false);
    check(4096, 12288, false); // seek within an unverified finished block
    check(0, 8192, true);       // cancellation before whole-piece verification
}
#endif
