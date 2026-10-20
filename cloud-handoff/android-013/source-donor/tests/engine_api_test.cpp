#include "test_support.hpp"

#include "nuvio_engine/nuvio_engine.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <string_view>
#include <thread>

#if defined(NUVIO_ENGINE_EXPECT_LIBTORRENT)
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#endif

namespace {

class EngineTestDirectories {
public:
    EngineTestDirectories() {
        static std::uint64_t next_directory = 1;
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("nuvio-engine-api-" + std::to_string(nonce) + "-" +
             std::to_string(next_directory++));
        data_directory_ = (root_ / "data").string();
        cache_directory_ = (root_ / "cache").string();
        std::filesystem::create_directories(data_directory_);
        std::filesystem::create_directories(cache_directory_);
    }

    ~EngineTestDirectories() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    void apply(nuvio_engine_config& config) const {
        config.data_directory = data_directory_.c_str();
        config.cache_directory = cache_directory_.c_str();
    }

    [[nodiscard]] const std::filesystem::path& root() const {
        return root_;
    }

private:
    std::filesystem::path root_;
    std::string data_directory_;
    std::string cache_directory_;
};

#if defined(NUVIO_ENGINE_EXPECT_LIBTORRENT)
struct LocalStreamUrl {
    std::uint16_t port;
    std::string target;
};

LocalStreamUrl parse_local_stream_url(const std::string& url) {
    constexpr std::string_view prefix = "http://127.0.0.1:";
    if (!url.starts_with(prefix)) {
        throw std::runtime_error("stream URL is not IPv4 loopback");
    }
    const auto target_start = url.find('/', prefix.size());
    if (target_start == std::string::npos) {
        throw std::runtime_error("stream URL has no target");
    }
    const auto port_text = std::string_view(url).substr(
        prefix.size(),
        target_start - prefix.size()
    );
    std::uint16_t port = 0;
    const auto parsed = std::from_chars(
        port_text.data(),
        port_text.data() + port_text.size(),
        port
    );
    if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() + port_text.size() ||
        port == 0) {
        throw std::runtime_error("stream URL has an invalid port");
    }
    return {port, url.substr(target_start)};
}

#if defined(_WIN32)
using TestSocket = SOCKET;
constexpr TestSocket invalid_test_socket = INVALID_SOCKET;
#else
using TestSocket = int;
constexpr TestSocket invalid_test_socket = -1;
#endif

class RawHttpClient {
public:
    RawHttpClient(const std::uint16_t port, const std::string& request) {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == invalid_test_socket) {
            throw std::runtime_error("failed to create raw HTTP test socket");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            close();
            throw std::runtime_error("failed to connect raw HTTP test socket");
        }
        std::size_t offset = 0;
        while (offset < request.size()) {
            const auto remaining = request.size() - offset;
#if defined(_WIN32)
            const auto chunk = static_cast<int>(std::min(
                remaining,
                static_cast<std::size_t>(std::numeric_limits<int>::max())
            ));
            const auto count = send(socket_, request.data() + offset, chunk, 0);
            if (count == SOCKET_ERROR || count == 0) {
#else
#if defined(MSG_NOSIGNAL)
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto count = send(socket_, request.data() + offset, remaining, flags);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
#endif
                close();
                throw std::runtime_error("failed to send raw HTTP test request");
            }
            offset += static_cast<std::size_t>(count);
        }
    }

    ~RawHttpClient() {
        close();
    }

    RawHttpClient(const RawHttpClient&) = delete;
    RawHttpClient& operator=(const RawHttpClient&) = delete;

    void close() {
        if (socket_ == invalid_test_socket) {
            return;
        }
#if defined(_WIN32)
        closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = invalid_test_socket;
    }

private:
    TestSocket socket_ = invalid_test_socket;
};
#endif

}

NUVIO_TEST("C API reports its version") {
    NUVIO_EXPECT_EQ(nuvio_engine_api_version(), NUVIO_ENGINE_API_VERSION);
    NUVIO_EXPECT_EQ(std::string(nuvio_engine_version_string()), std::string("0.1.5"));
}

NUVIO_TEST("C API reports protocol backend availability") {
#if defined(NUVIO_ENGINE_EXPECT_LIBTORRENT)
    NUVIO_EXPECT_EQ(
        std::string(nuvio_engine_protocol_backend_version()),
        std::string("2.0.12.0")
    );
#else
    NUVIO_EXPECT_EQ(
        std::string(nuvio_engine_protocol_backend_version()),
        std::string("unavailable")
    );
#endif
}

NUVIO_TEST("C API exposes stable status messages") {
    NUVIO_EXPECT_EQ(
        std::string(nuvio_engine_status_message(NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED)),
        std::string("initialization failed")
    );
}

NUVIO_TEST("default configuration creates and destroys an engine handle") {
    EngineTestDirectories directories;
    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    NUVIO_EXPECT_EQ(
        config.stream_inactivity_timeout_milliseconds,
        std::uint32_t(30'000)
    );
    NUVIO_EXPECT_EQ(
        config.warm_torrent_timeout_milliseconds,
        std::uint32_t(60'000)
    );
    NUVIO_EXPECT_TRUE(config.tls_ca_bundle_path == nullptr);
    NUVIO_EXPECT_EQ(
        config.torrent_profile,
        nuvio_engine_torrent_profile(NUVIO_ENGINE_TORRENT_PROFILE_BALANCED)
    );
    directories.apply(config);

    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);
    NUVIO_EXPECT_TRUE(engine != nullptr);
    nuvio_engine_stats stats;
    nuvio_engine_stats_init(&stats);
    NUVIO_EXPECT_EQ(stats.struct_size, sizeof(nuvio_engine_stats));
    NUVIO_EXPECT_EQ(stats.known_peers, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.connect_candidates, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.interested_peers, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.unchoked_peers, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.downloading_peers, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.snubbed_peers, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.pending_block_requests, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.target_block_requests, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.timed_out_block_requests, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.connecting_peers, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.handshaking_peers, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.target_piece_peers, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.peer_connect_events, std::uint64_t(0));
    NUVIO_EXPECT_EQ(stats.peer_disconnect_events, std::uint64_t(0));
    NUVIO_EXPECT_EQ(stats.tracker_reply_events, std::uint32_t(0));
    NUVIO_EXPECT_EQ(nuvio_engine_get_stats(engine, &stats), NUVIO_ENGINE_STATUS_OK);
    NUVIO_EXPECT_EQ(stats.active_torrents, std::uint32_t(0));
    stats.struct_size = 1;
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_stats(engine, &stats),
        NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI
    );
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("invalid torrent profile is rejected") {
    EngineTestDirectories directories;
    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    config.torrent_profile = 99;

    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(
        nuvio_engine_create(&config, &engine),
        NUVIO_ENGINE_STATUS_INVALID_ARGUMENT
    );
    NUVIO_EXPECT_TRUE(engine == nullptr);
}

NUVIO_TEST("empty explicit TLS CA bundle path is rejected") {
    EngineTestDirectories directories;
    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    config.tls_ca_bundle_path = "";

    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(
        nuvio_engine_create(&config, &engine),
        NUVIO_ENGINE_STATUS_INVALID_ARGUMENT
    );
    NUVIO_EXPECT_TRUE(engine == nullptr);
}

NUVIO_TEST("oversized explicit TLS CA bundle path is rejected") {
    EngineTestDirectories directories;
    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    const std::string oversized_path(16 * 1024 + 1, 'a');
    config.tls_ca_bundle_path = oversized_path.c_str();

    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(
        nuvio_engine_create(&config, &engine),
        NUVIO_ENGINE_STATUS_INVALID_ARGUMENT
    );
    NUVIO_EXPECT_TRUE(engine == nullptr);
}

NUVIO_TEST("backend rejects an unreadable explicit TLS CA bundle") {
    EngineTestDirectories directories;
    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    const auto missing_bundle = (directories.root() / "missing-ca.pem").string();
    config.tls_ca_bundle_path = missing_bundle.c_str();

    nuvio_engine* engine = nullptr;
    const auto status = nuvio_engine_create(&config, &engine);
#if defined(NUVIO_ENGINE_EXPECT_LIBTORRENT)
    NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED);
    NUVIO_EXPECT_TRUE(engine == nullptr);
#else
    NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
    NUVIO_EXPECT_TRUE(engine != nullptr);
#endif
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("limited upload mode requires a positive limit") {
    EngineTestDirectories directories;
    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    config.upload_mode = NUVIO_ENGINE_UPLOAD_LIMITED;

    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(
        nuvio_engine_create(&config, &engine),
        NUVIO_ENGINE_STATUS_INVALID_ARGUMENT
    );
    NUVIO_EXPECT_TRUE(engine == nullptr);
}

NUVIO_TEST("older configuration layouts are rejected") {
    EngineTestDirectories directories;
    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    config.struct_size = 1;

    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(
        nuvio_engine_create(&config, &engine),
        NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI
    );
}

NUVIO_TEST("torrent submission reports backend availability asynchronously") {
    EngineTestDirectories directories;
    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

    nuvio_engine_torrent_request request;
    nuvio_engine_torrent_request_init(&request);
    request.magnet_uri = "magnet:?xt=urn:btih:not-a-valid-hash";
    std::uint64_t request_id = 0;
    const auto submit_status = nuvio_engine_add_torrent(engine, &request, &request_id);
#if defined(NUVIO_ENGINE_EXPECT_LIBTORRENT)
    NUVIO_EXPECT_EQ(submit_status, NUVIO_ENGINE_STATUS_OK);
    NUVIO_EXPECT_TRUE(request_id > 0);
    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    nuvio_engine_status poll_status = NUVIO_ENGINE_STATUS_NO_EVENT;
    for (int attempt = 0; attempt < 100 && poll_status == NUVIO_ENGINE_STATUS_NO_EVENT; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        poll_status = nuvio_engine_poll_event(engine, &event);
    }
    NUVIO_EXPECT_EQ(poll_status, NUVIO_ENGINE_STATUS_OK);
    NUVIO_EXPECT_EQ(event.type, NUVIO_ENGINE_EVENT_TORRENT_ERROR);
    NUVIO_EXPECT_EQ(event.request_id, request_id);
    NUVIO_EXPECT_TRUE(event.sequence > 0);
#else
    NUVIO_EXPECT_EQ(submit_status, NUVIO_ENGINE_STATUS_BACKEND_UNAVAILABLE);
    NUVIO_EXPECT_EQ(request_id, std::uint64_t(0));
#endif
    nuvio_engine_destroy(engine);
}

#if defined(NUVIO_ENGINE_EXPECT_LIBTORRENT)
NUVIO_TEST("valid magnet produces a canonical correlated torrent event") {
    EngineTestDirectories directories;
    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

    nuvio_engine_torrent_request request;
    nuvio_engine_torrent_request_init(&request);
    request.magnet_uri = "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567";
    std::uint64_t request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_add_torrent(engine, &request, &request_id),
        NUVIO_ENGINE_STATUS_OK
    );

    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    nuvio_engine_status poll_status = NUVIO_ENGINE_STATUS_NO_EVENT;
    for (int attempt = 0; attempt < 100 && poll_status == NUVIO_ENGINE_STATUS_NO_EVENT; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        poll_status = nuvio_engine_poll_event(engine, &event);
    }
    NUVIO_EXPECT_EQ(poll_status, NUVIO_ENGINE_STATUS_OK);
    NUVIO_EXPECT_EQ(event.type, NUVIO_ENGINE_EVENT_TORRENT_ADDED);
    NUVIO_EXPECT_EQ(event.request_id, request_id);
    NUVIO_EXPECT_EQ(
        std::string(event.torrent_id),
        std::string("0123456789abcdef0123456789abcdef01234567")
    );
    NUVIO_EXPECT_EQ(event.dropped_events, std::uint64_t(0));
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("torrent bytes expose a canonical zero-based metadata snapshot") {
    EngineTestDirectories directories;
    std::string torrent =
        "d4:infod6:lengthi4e4:name8:test.bin12:piece lengthi16384e6:pieces20:";
    torrent.append(20, '\0');
    torrent += "ee";

    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

    nuvio_engine_torrent_request request;
    nuvio_engine_torrent_request_init(&request);
    request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
    request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
    request.torrent_data_size = torrent.size();
    std::uint64_t request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_add_torrent(engine, &request, &request_id),
        NUVIO_ENGINE_STATUS_OK
    );

    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    bool received_metadata = false;
    for (int attempt = 0; attempt < 100 && !received_metadata; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_metadata = event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY;
    }
    NUVIO_EXPECT_TRUE(received_metadata);
    NUVIO_EXPECT_EQ(event.request_id, request_id);
    NUVIO_EXPECT_TRUE(
        std::string(event.message).find("metadata_source=provided") != std::string::npos
    );
    NUVIO_EXPECT_TRUE(
        std::string(event.message).find("metadata_elapsed_ms=") != std::string::npos
    );
    const std::string torrent_id = event.torrent_id;

    std::uint64_t live_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_add_torrent(engine, &request, &live_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    bool received_live_metadata = false;
    for (int attempt = 0; attempt < 100 && !received_live_metadata; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_live_metadata = event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY &&
            event.request_id == live_request_id;
    }
    NUVIO_EXPECT_TRUE(received_live_metadata);
    NUVIO_EXPECT_TRUE(
        std::string(event.message).find("metadata_source=live") != std::string::npos
    );

    std::size_t file_count = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_file_count(engine, torrent_id.c_str(), &file_count),
        NUVIO_ENGINE_STATUS_OK
    );
    NUVIO_EXPECT_EQ(file_count, std::size_t(1));

    nuvio_engine_file file;
    nuvio_engine_file_init(&file);
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_file(engine, torrent_id.c_str(), 0, &file),
        NUVIO_ENGINE_STATUS_OK
    );
    NUVIO_EXPECT_EQ(file.index, std::uint32_t(0));
    NUVIO_EXPECT_EQ(file.offset, std::uint64_t(0));
    NUVIO_EXPECT_EQ(file.size, std::uint64_t(4));
    NUVIO_EXPECT_EQ(std::string(file.path), std::string("test.bin"));
    NUVIO_EXPECT_EQ(file.path_truncated, std::uint8_t(0));
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_file(engine, torrent_id.c_str(), 1, &file),
        NUVIO_ENGINE_STATUS_OUT_OF_RANGE
    );

    nuvio_engine_stream_request stream_request;
    nuvio_engine_stream_request_init(&stream_request);
    stream_request.torrent_id = torrent_id.c_str();
    stream_request.filename_hint = "test.bin";
    std::uint64_t prepare_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_prepare_stream(engine, &stream_request, &prepare_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    NUVIO_EXPECT_TRUE(prepare_request_id > request_id);

    bool received_prepared = false;
    for (int attempt = 0; attempt < 100 && !received_prepared; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_prepared = event.type == NUVIO_ENGINE_EVENT_STREAM_PREPARED &&
            event.request_id == prepare_request_id;
    }
    NUVIO_EXPECT_TRUE(received_prepared);
    NUVIO_EXPECT_EQ(event.file_index, std::uint32_t(0));
    NUVIO_EXPECT_EQ(event.file_size, std::uint64_t(4));
    NUVIO_EXPECT_EQ(std::string(event.torrent_id), torrent_id);
    NUVIO_EXPECT_EQ(std::string(event.stream_id).size(), std::size_t(64));
    NUVIO_EXPECT_TRUE(std::string(event.stream_url).starts_with("http://127.0.0.1:"));
    NUVIO_EXPECT_TRUE(
        std::string(event.stream_url).ends_with(
            "/stream/" + std::string(event.stream_id)
        )
    );

    const std::string pending_stream_id = event.stream_id;
    const auto pending_stream = parse_local_stream_url(event.stream_url);
    auto pending_read = std::async(std::launch::async, [&] {
        return nuvio::test::send_http_request(
            pending_stream.port,
            "GET " + pending_stream.target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
        );
    });
    nuvio_engine_stats active_stats;
    nuvio_engine_stats_init(&active_stats);
    for (int attempt = 0; attempt < 100; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &active_stats),
            NUVIO_ENGINE_STATUS_OK
        );
        if (active_stats.active_http_requests > 0 &&
            active_stats.pending_piece_reads > 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NUVIO_EXPECT_EQ(active_stats.active_torrents, std::uint32_t(1));
    NUVIO_EXPECT_EQ(active_stats.active_streams, std::uint32_t(1));
    NUVIO_EXPECT_EQ(active_stats.active_http_requests, std::uint32_t(1));
    NUVIO_EXPECT_TRUE(active_stats.pending_piece_reads >= 1);
    NUVIO_EXPECT_TRUE(active_stats.memory_cache_misses >= 1);

    std::uint64_t stop_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_stop_stream(engine, pending_stream_id.c_str(), &stop_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    bool received_stopped = false;
    for (int attempt = 0; attempt < 100 && !received_stopped; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_stopped = event.type == NUVIO_ENGINE_EVENT_STREAM_STOPPED &&
            event.request_id == stop_request_id;
    }
    NUVIO_EXPECT_TRUE(received_stopped);
    NUVIO_EXPECT_EQ(std::string(event.stream_id), pending_stream_id);
    NUVIO_EXPECT_EQ(std::string(event.torrent_id), torrent_id);
    NUVIO_EXPECT_EQ(
        pending_read.wait_for(std::chrono::seconds(2)),
        std::future_status::ready
    );
    NUVIO_EXPECT_TRUE(!pending_read.get().empty());
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_file_count(engine, torrent_id.c_str(), &file_count),
        NUVIO_ENGINE_STATUS_OK
    );
    NUVIO_EXPECT_EQ(file_count, std::size_t(1));

    std::uint64_t repeated_stop_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_stop_stream(
            engine,
            pending_stream_id.c_str(),
            &repeated_stop_request_id
        ),
        NUVIO_ENGINE_STATUS_OK
    );
    bool received_repeated_stop = false;
    for (int attempt = 0; attempt < 100 && !received_repeated_stop; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_repeated_stop = event.type == NUVIO_ENGINE_EVENT_STREAM_STOPPED &&
            event.request_id == repeated_stop_request_id;
    }
    NUVIO_EXPECT_TRUE(received_repeated_stop);
    NUVIO_EXPECT_EQ(std::string(event.stream_id), pending_stream_id);
    NUVIO_EXPECT_TRUE(std::string(event.torrent_id).empty());

    std::uint64_t remove_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_remove_torrent(engine, torrent_id.c_str(), &remove_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    NUVIO_EXPECT_TRUE(remove_request_id > repeated_stop_request_id);

    bool received_removed = false;
    for (int attempt = 0; attempt < 100 && !received_removed; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_removed = event.type == NUVIO_ENGINE_EVENT_TORRENT_REMOVED &&
            event.request_id == remove_request_id;
    }
    NUVIO_EXPECT_TRUE(received_removed);
    NUVIO_EXPECT_EQ(std::string(event.torrent_id), torrent_id);
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_file_count(engine, torrent_id.c_str(), &file_count),
        NUVIO_ENGINE_STATUS_METADATA_NOT_READY
    );
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("torrent resume stays inert across restart and loads on demand") {
    EngineTestDirectories directories;
    std::string torrent =
        "d4:infod6:lengthi4e4:name8:test.bin12:piece lengthi16384e6:pieces20:";
    torrent.append(20, '\0');
    torrent += "ee";

    std::string torrent_id;
    {
        nuvio_engine_config config;
        nuvio_engine_config_init(&config);
        directories.apply(config);
        nuvio_engine* engine = nullptr;
        NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

        nuvio_engine_torrent_request request;
        nuvio_engine_torrent_request_init(&request);
        request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
        request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
        request.torrent_data_size = torrent.size();
        std::uint64_t request_id = 0;
        NUVIO_EXPECT_EQ(
            nuvio_engine_add_torrent(engine, &request, &request_id),
            NUVIO_ENGINE_STATUS_OK
        );

        nuvio_engine_event event;
        nuvio_engine_event_init(&event);
        bool received_metadata = false;
        for (int attempt = 0; attempt < 100 && !received_metadata; ++attempt) {
            const auto status = nuvio_engine_poll_event(engine, &event);
            if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
            received_metadata = event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY;
        }
        NUVIO_EXPECT_TRUE(received_metadata);
        torrent_id = event.torrent_id;
        nuvio_engine_destroy(engine);
    }

    const auto state_root = directories.root() / "data" / "nuvio-engine-state";
    const auto resume_path = state_root / "resume" / (torrent_id + ".resume");
    NUVIO_EXPECT_TRUE(std::filesystem::exists(state_root / "session.dht"));
    NUVIO_EXPECT_TRUE(std::filesystem::exists(resume_path));

    {
        nuvio_engine_config config;
        nuvio_engine_config_init(&config);
        directories.apply(config);
        nuvio_engine* engine = nullptr;
        NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

        nuvio_engine_stats stats;
        nuvio_engine_stats_init(&stats);
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        NUVIO_EXPECT_EQ(stats.active_torrents, std::uint32_t(0));

        std::size_t file_count = 0;
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_file_count(engine, torrent_id.c_str(), &file_count),
            NUVIO_ENGINE_STATUS_METADATA_NOT_READY
        );

        nuvio_engine_torrent_request request;
        nuvio_engine_torrent_request_init(&request);
        request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
        request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
        request.torrent_data_size = torrent.size();
        std::uint64_t restore_request_id = 0;
        NUVIO_EXPECT_EQ(
            nuvio_engine_add_torrent(engine, &request, &restore_request_id),
            NUVIO_ENGINE_STATUS_OK
        );

        nuvio_engine_event event;
        nuvio_engine_event_init(&event);
        bool restored_metadata = false;
        for (int attempt = 0; attempt < 100 && !restored_metadata; ++attempt) {
            const auto status = nuvio_engine_poll_event(engine, &event);
            if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
            restored_metadata = event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY &&
                event.request_id == restore_request_id &&
                std::string(event.torrent_id) == torrent_id;
        }
        NUVIO_EXPECT_TRUE(restored_metadata);
        NUVIO_EXPECT_TRUE(
            std::string(event.message).find("metadata_source=restored") != std::string::npos
        );
        NUVIO_EXPECT_TRUE(
            std::string(event.message).find("metadata_elapsed_ms=") != std::string::npos
        );

        NUVIO_EXPECT_EQ(
            nuvio_engine_get_file_count(engine, torrent_id.c_str(), &file_count),
            NUVIO_ENGINE_STATUS_OK
        );
        NUVIO_EXPECT_EQ(file_count, std::size_t(1));

        nuvio_engine_destroy(engine);
    }

    {
        NUVIO_EXPECT_EQ(torrent_id.size(), std::size_t(40));
        const auto hex_value = [](const char character) -> unsigned char {
            if (character >= '0' && character <= '9') {
                return static_cast<unsigned char>(character - '0');
            }
            return static_cast<unsigned char>(character - 'a' + 10);
        };
        std::string binary_hash;
        binary_hash.reserve(20);
        for (std::size_t index = 0; index < torrent_id.size(); index += 2) {
            binary_hash.push_back(static_cast<char>(
                (hex_value(torrent_id[index]) << 4) |
                hex_value(torrent_id[index + 1])
            ));
        }
        std::string metadata_free_resume =
            "d11:file-format22:libtorrent resume file12:file-versioni1e9:info-hash20:";
        metadata_free_resume += binary_hash;
        metadata_free_resume += 'e';
        std::ofstream output(resume_path, std::ios::binary | std::ios::trunc);
        output.write(
            metadata_free_resume.data(),
            static_cast<std::streamsize>(metadata_free_resume.size())
        );
    }

    {
        nuvio_engine_config config;
        nuvio_engine_config_init(&config);
        directories.apply(config);
        nuvio_engine* engine = nullptr;
        NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

        nuvio_engine_torrent_request request;
        nuvio_engine_torrent_request_init(&request);
        request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
        request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
        request.torrent_data_size = torrent.size();
        std::uint64_t provided_request_id = 0;
        NUVIO_EXPECT_EQ(
            nuvio_engine_add_torrent(engine, &request, &provided_request_id),
            NUVIO_ENGINE_STATUS_OK
        );

        nuvio_engine_event event;
        nuvio_engine_event_init(&event);
        bool provided_metadata = false;
        for (int attempt = 0; attempt < 100 && !provided_metadata; ++attempt) {
            const auto status = nuvio_engine_poll_event(engine, &event);
            if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
            provided_metadata = event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY &&
                event.request_id == provided_request_id &&
                std::string(event.torrent_id) == torrent_id;
        }
        NUVIO_EXPECT_TRUE(provided_metadata);
        NUVIO_EXPECT_TRUE(
            std::string(event.message).find("metadata_source=provided") != std::string::npos
        );

        nuvio_engine_destroy(engine);
    }

    {
        nuvio_engine_config config;
        nuvio_engine_config_init(&config);
        directories.apply(config);
        nuvio_engine* engine = nullptr;
        NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

        const auto magnet_uri = "magnet:?xt=urn:btih:" + torrent_id;
        nuvio_engine_torrent_request request;
        nuvio_engine_torrent_request_init(&request);
        request.magnet_uri = magnet_uri.c_str();
        std::uint64_t restore_request_id = 0;
        NUVIO_EXPECT_EQ(
            nuvio_engine_add_torrent(engine, &request, &restore_request_id),
            NUVIO_ENGINE_STATUS_OK
        );

        nuvio_engine_event event;
        nuvio_engine_event_init(&event);
        bool restored_metadata = false;
        for (int attempt = 0; attempt < 100 && !restored_metadata; ++attempt) {
            const auto status = nuvio_engine_poll_event(engine, &event);
            if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
            restored_metadata = event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY &&
                event.request_id == restore_request_id &&
                std::string(event.torrent_id) == torrent_id;
        }
        NUVIO_EXPECT_TRUE(restored_metadata);
        NUVIO_EXPECT_TRUE(
            std::string(event.message).find("metadata_source=restored") != std::string::npos
        );

        std::uint64_t remove_request_id = 0;
        NUVIO_EXPECT_EQ(
            nuvio_engine_remove_torrent(engine, torrent_id.c_str(), &remove_request_id),
            NUVIO_ENGINE_STATUS_OK
        );
        bool received_removed = false;
        for (int attempt = 0; attempt < 100 && !received_removed; ++attempt) {
            const auto status = nuvio_engine_poll_event(engine, &event);
            if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
            received_removed = event.type == NUVIO_ENGINE_EVENT_TORRENT_REMOVED &&
                event.request_id == remove_request_id;
        }
        NUVIO_EXPECT_TRUE(received_removed);
        nuvio_engine_destroy(engine);
    }

    NUVIO_EXPECT_TRUE(!std::filesystem::exists(resume_path));

    {
        nuvio_engine_config config;
        nuvio_engine_config_init(&config);
        directories.apply(config);
        nuvio_engine* engine = nullptr;
        NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);
        std::size_t file_count = 0;
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_file_count(engine, torrent_id.c_str(), &file_count),
            NUVIO_ENGINE_STATUS_METADATA_NOT_READY
        );
        nuvio_engine_destroy(engine);
    }
}

NUVIO_TEST("corrupt persisted DHT state is bounded and reported without blocking startup") {
    EngineTestDirectories directories;
    const auto state_root = directories.root() / "data" / "nuvio-engine-state";
    std::filesystem::create_directories(state_root);
    {
        std::ofstream output(state_root / "session.dht", std::ios::binary);
        output << "not-bencoded-session-state";
    }

    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    bool received_diagnostic = false;
    for (int attempt = 0; attempt < 100 && !received_diagnostic; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_diagnostic = event.type == NUVIO_ENGINE_EVENT_TORRENT_ERROR &&
            std::string(event.message).find("ignored persisted DHT state") !=
                std::string::npos;
    }
    NUVIO_EXPECT_TRUE(received_diagnostic);
    NUVIO_EXPECT_EQ(event.request_id, std::uint64_t(0));
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("stream inactivity begins after the final HTTP request ends") {
    EngineTestDirectories directories;
    constexpr auto inactivity_timeout = std::chrono::milliseconds(2'000);
    std::string torrent =
        "d4:infod6:lengthi4e4:name8:test.bin12:piece lengthi16384e6:pieces20:";
    torrent.append(20, '\0');
    torrent += "ee";

    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    config.stream_inactivity_timeout_milliseconds = static_cast<std::uint32_t>(
        inactivity_timeout.count()
    );
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

    nuvio_engine_torrent_request add_request;
    nuvio_engine_torrent_request_init(&add_request);
    add_request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
    add_request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
    add_request.torrent_data_size = torrent.size();
    std::uint64_t add_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_add_torrent(engine, &add_request, &add_request_id),
        NUVIO_ENGINE_STATUS_OK
    );

    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    std::string torrent_id;
    for (int attempt = 0; attempt < 200 && torrent_id.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY) {
            torrent_id = event.torrent_id;
        }
    }
    NUVIO_EXPECT_TRUE(!torrent_id.empty());

    nuvio_engine_stream_request stream_request;
    nuvio_engine_stream_request_init(&stream_request);
    stream_request.torrent_id = torrent_id.c_str();
    stream_request.file_index = 0;
    std::uint64_t prepare_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_prepare_stream(engine, &stream_request, &prepare_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    std::string stream_id;
    std::string stream_url;
    for (int attempt = 0; attempt < 200 && stream_url.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_STREAM_PREPARED &&
            event.request_id == prepare_request_id) {
            stream_id = event.stream_id;
            stream_url = event.stream_url;
        }
    }
    NUVIO_EXPECT_TRUE(!stream_url.empty());

    const auto local = parse_local_stream_url(stream_url);
    RawHttpClient first_reader(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-1\r\n\r\n"
    );
    RawHttpClient second_reader(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=2-3\r\n\r\n"
    );

    nuvio_engine_stats stats;
    nuvio_engine_stats_init(&stats);
    nuvio_engine_stream_stats stream_stats;
    nuvio_engine_stream_stats_init(&stream_stats);
    bool readers_active = false;
    for (int attempt = 0; attempt < 200 && !readers_active; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stream_stats(engine, stream_id.c_str(), &stream_stats),
            NUVIO_ENGINE_STATUS_OK
        );
        readers_active = stats.active_http_requests >= 2 &&
            stream_stats.active_demands >= 2;
        if (!readers_active) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NUVIO_EXPECT_TRUE(readers_active);

    bool expired_while_reading = false;
    const auto active_deadline = std::chrono::steady_clock::now() +
        inactivity_timeout * 2;
    while (std::chrono::steady_clock::now() < active_deadline) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_OK) {
            expired_while_reading = event.type == NUVIO_ENGINE_EVENT_STREAM_STOPPED &&
                event.request_id == 0 && std::string(event.stream_id) == stream_id;
        } else {
            NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_NO_EVENT);
        }
        NUVIO_EXPECT_TRUE(!expired_while_reading);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    first_reader.close();
    bool one_reader_remains = false;
    for (int attempt = 0; attempt < 200 && !one_reader_remains; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        one_reader_remains = stats.active_http_requests == 1;
        if (!one_reader_remains) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NUVIO_EXPECT_TRUE(one_reader_remains);
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_stream_stats(engine, stream_id.c_str(), &stream_stats),
        NUVIO_ENGINE_STATUS_OK
    );

    second_reader.close();
    bool readers_finished = false;
    for (int attempt = 0; attempt < 200 && !readers_finished; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        const auto stream_status = nuvio_engine_get_stream_stats(
            engine,
            stream_id.c_str(),
            &stream_stats
        );
        readers_finished = stats.active_http_requests == 0 &&
            stream_status == NUVIO_ENGINE_STATUS_OK &&
            stream_stats.active_demands == 0;
        if (!readers_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NUVIO_EXPECT_TRUE(readers_finished);

    bool expired_during_post_request_grace = false;
    const auto grace_deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(1'100);
    while (std::chrono::steady_clock::now() < grace_deadline) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stream_stats(engine, stream_id.c_str(), &stream_stats),
            NUVIO_ENGINE_STATUS_OK
        );
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_OK) {
            expired_during_post_request_grace =
                event.type == NUVIO_ENGINE_EVENT_STREAM_STOPPED &&
                event.request_id == 0 && std::string(event.stream_id) == stream_id;
        } else {
            NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_NO_EVENT);
        }
        NUVIO_EXPECT_TRUE(!expired_during_post_request_grace);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto reopened = nuvio::test::send_http_request(
        local.port,
        "HEAD " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-0\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(reopened.find("HTTP/1.1 206 Partial Content\r\n") == 0);

    bool expired = false;
    for (int attempt = 0; attempt < 400 && !expired; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        expired = event.type == NUVIO_ENGINE_EVENT_STREAM_STOPPED &&
            event.request_id == 0 && std::string(event.stream_id) == stream_id;
    }
    NUVIO_EXPECT_TRUE(expired);
    NUVIO_EXPECT_EQ(std::string(event.torrent_id), torrent_id);
    NUVIO_EXPECT_TRUE(std::string(event.message).find("inactivity") != std::string::npos);
    const auto revoked = nuvio::test::send_http_request(
        local.port,
        "GET " + local.target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(revoked.find("HTTP/1.1 404 Not Found\r\n") == 0);
    std::size_t file_count = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_file_count(engine, torrent_id.c_str(), &file_count),
        NUVIO_ENGINE_STATUS_OK
    );
    NUVIO_EXPECT_EQ(file_count, std::size_t(1));

    std::uint64_t reprepare_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_prepare_stream(engine, &stream_request, &reprepare_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    std::string reprepared_stream_id;
    for (int attempt = 0; attempt < 200 && reprepared_stream_id.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_STREAM_PREPARED &&
            event.request_id == reprepare_request_id) {
            reprepared_stream_id = event.stream_id;
        }
    }
    NUVIO_EXPECT_TRUE(!reprepared_stream_id.empty());
    nuvio_engine_stream_stats reprepared_stats;
    nuvio_engine_stream_stats_init(&reprepared_stats);
    bool received_reprepared_stats = false;
    for (int attempt = 0; attempt < 100 && !received_reprepared_stats; ++attempt) {
        const auto status = nuvio_engine_get_stream_stats(
            engine,
            reprepared_stream_id.c_str(),
            &reprepared_stats
        );
        received_reprepared_stats = status == NUVIO_ENGINE_STATUS_OK;
        if (!received_reprepared_stats) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    NUVIO_EXPECT_TRUE(received_reprepared_stats);
    NUVIO_EXPECT_EQ(reprepared_stats.active_demands, std::uint32_t(0));
    NUVIO_EXPECT_EQ(reprepared_stats.scheduled_pieces, std::uint32_t(1));
    NUVIO_EXPECT_EQ(reprepared_stats.blocking_pieces, std::uint32_t(0));
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("hash scoped payload path rejects a directory symlink") {
    EngineTestDirectories directories;
    constexpr std::string_view torrent_id =
        "13ade5f13f4e7ce4021a3cc82f72e504b9ef35ac";
    const auto payload_root = directories.root() / "cache" / "payload";
    const auto outside = directories.root() / "outside";
    std::filesystem::create_directories(payload_root);
    std::filesystem::create_directories(outside);
    std::error_code symlink_error;
    std::filesystem::create_directory_symlink(
        outside,
        payload_root / torrent_id,
        symlink_error
    );
    if (symlink_error) {
        return;
    }

    std::string torrent =
        "d4:infod6:lengthi4e4:name8:test.bin12:piece lengthi16384e6:pieces20:";
    constexpr std::array<unsigned char, 20> piece_hash{
        0xa9, 0x4a, 0x8f, 0xe5, 0xcc, 0xb1, 0x9b, 0xa6, 0x1c, 0x4c,
        0x08, 0x73, 0xd3, 0x91, 0xe9, 0x87, 0x98, 0x2f, 0xbb, 0xd3,
    };
    torrent.append(
        reinterpret_cast<const char*>(piece_hash.data()),
        piece_hash.size()
    );
    torrent += "ee";

    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);
    nuvio_engine_torrent_request request;
    nuvio_engine_torrent_request_init(&request);
    request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
    request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
    request.torrent_data_size = torrent.size();
    std::uint64_t request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_add_torrent(engine, &request, &request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    bool rejected = false;
    for (int attempt = 0; attempt < 100 && !rejected; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        rejected = event.type == NUVIO_ENGINE_EVENT_TORRENT_ERROR &&
            event.request_id == request_id;
    }
    NUVIO_EXPECT_TRUE(rejected);
    NUVIO_EXPECT_TRUE(std::string(event.message).find("payload path") != std::string::npos);
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("engine evicts inactive payload trees to its configured disk budget") {
    EngineTestDirectories directories;
    constexpr std::string_view inactive_id =
        "0000000000000000000000000000000000000001";
    const auto inactive_directory =
        directories.root() / "cache" / "payload" / inactive_id;
    std::filesystem::create_directories(inactive_directory);
    {
        std::ofstream payload(inactive_directory / "old.bin", std::ios::binary);
        payload << "123456";
    }

    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    config.disk_cache_capacity_bytes = 4;
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

    nuvio_engine_stats stats;
    nuvio_engine_stats_init(&stats);
    for (int attempt = 0; attempt < 100; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        if (stats.disk_cache_evictions > 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NUVIO_EXPECT_TRUE(!std::filesystem::exists(inactive_directory));
    NUVIO_EXPECT_EQ(stats.disk_cache_capacity_bytes, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stats.disk_cache_used_bytes, std::uint64_t(0));
    NUVIO_EXPECT_EQ(stats.disk_cache_protected_bytes, std::uint64_t(0));
    NUVIO_EXPECT_EQ(stats.disk_cache_evictions, std::uint64_t(1));
    NUVIO_EXPECT_EQ(stats.disk_cache_reclaimed_bytes, std::uint64_t(6));
    NUVIO_EXPECT_EQ(stats.disk_cache_over_budget, std::uint8_t(0));
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("explicit disk reclaim purges inactive payload below the normal budget") {
    EngineTestDirectories directories;
    constexpr std::string_view inactive_id =
        "0000000000000000000000000000000000000002";
    const auto inactive_directory =
        directories.root() / "cache" / "payload" / inactive_id;
    std::filesystem::create_directories(inactive_directory);
    {
        std::ofstream payload(inactive_directory / "old.bin", std::ios::binary);
        payload << "123456";
    }

    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    config.disk_cache_capacity_bytes = 100;
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);
    nuvio_engine_stats stats;
    nuvio_engine_stats_init(&stats);
    for (int attempt = 0; attempt < 100; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        if (stats.disk_cache_used_bytes == 6) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NUVIO_EXPECT_TRUE(std::filesystem::exists(inactive_directory));
    NUVIO_EXPECT_EQ(stats.disk_cache_used_bytes, std::uint64_t(6));

    std::uint64_t reclaim_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_reclaim_disk_cache(engine, 0, &reclaim_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    bool reclaimed = false;
    for (int attempt = 0; attempt < 100 && !reclaimed; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        reclaimed = event.type == NUVIO_ENGINE_EVENT_DISK_CACHE_RECLAIMED &&
            event.request_id == reclaim_request_id;
    }
    NUVIO_EXPECT_TRUE(reclaimed);
    NUVIO_EXPECT_TRUE(
        std::string(event.message).find("target reached") != std::string::npos
    );
    NUVIO_EXPECT_TRUE(!std::filesystem::exists(inactive_directory));
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_stats(engine, &stats),
        NUVIO_ENGINE_STATUS_OK
    );
    NUVIO_EXPECT_EQ(stats.disk_cache_used_bytes, std::uint64_t(0));
    NUVIO_EXPECT_EQ(stats.disk_cache_reclaimed_bytes, std::uint64_t(6));
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("disk cache usage refreshes while a stream request stays open") {
    EngineTestDirectories directories;
    std::string torrent =
        "d4:infod6:lengthi4e4:name8:test.bin12:piece lengthi16384e6:pieces20:";
    torrent.append(20, '\0');
    torrent += "ee";

    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

    nuvio_engine_torrent_request add_request;
    nuvio_engine_torrent_request_init(&add_request);
    add_request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
    add_request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
    add_request.torrent_data_size = torrent.size();
    std::uint64_t add_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_add_torrent(engine, &add_request, &add_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    std::string torrent_id;
    for (int attempt = 0; attempt < 200 && torrent_id.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY &&
            event.request_id == add_request_id) {
            torrent_id = event.torrent_id;
        }
    }
    NUVIO_EXPECT_TRUE(!torrent_id.empty());

    nuvio_engine_stream_request stream_request;
    nuvio_engine_stream_request_init(&stream_request);
    stream_request.torrent_id = torrent_id.c_str();
    stream_request.file_index = 0;
    std::uint64_t prepare_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_prepare_stream(engine, &stream_request, &prepare_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    std::string stream_url;
    for (int attempt = 0; attempt < 200 && stream_url.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_STREAM_PREPARED &&
            event.request_id == prepare_request_id) {
            stream_url = event.stream_url;
        }
    }
    NUVIO_EXPECT_TRUE(!stream_url.empty());

    const auto local = parse_local_stream_url(stream_url);
    RawHttpClient reader(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-3\r\n\r\n"
    );
    nuvio_engine_stats stats;
    nuvio_engine_stats_init(&stats);
    bool reader_active = false;
    for (int attempt = 0; attempt < 200 && !reader_active; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        reader_active = stats.active_http_requests >= 1;
        if (!reader_active) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NUVIO_EXPECT_TRUE(reader_active);

    const auto payload_directory =
        directories.root() / "cache" / "payload" / torrent_id;
    std::filesystem::create_directories(payload_directory);
    {
        std::ofstream payload(payload_directory / "growth.bin", std::ios::binary);
        payload << "123456";
    }
    bool measured = false;
    for (int attempt = 0; attempt < 1'000 && !measured; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        measured = stats.disk_cache_used_bytes == 6;
        if (!measured) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    NUVIO_EXPECT_TRUE(measured);
    NUVIO_EXPECT_EQ(stats.disk_cache_protected_bytes, std::uint64_t(6));
    NUVIO_EXPECT_TRUE(stats.active_http_requests >= 1);
    reader.close();
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("prepared stream URL serves verified full and partial torrent bytes") {
    EngineTestDirectories directories;
    constexpr std::string_view seeded_torrent_id =
        "13ade5f13f4e7ce4021a3cc82f72e504b9ef35ac";
    {
        const auto payload_directory =
            directories.root() / "cache" / "payload" / seeded_torrent_id;
        std::filesystem::create_directories(payload_directory);
        std::ofstream payload(payload_directory / "test.bin", std::ios::binary);
        payload << "test";
    }
    std::string torrent =
        "d4:infod6:lengthi4e4:name8:test.bin12:piece lengthi16384e6:pieces20:";
    constexpr std::array<unsigned char, 20> piece_hash{
        0xa9, 0x4a, 0x8f, 0xe5, 0xcc, 0xb1, 0x9b, 0xa6, 0x1c, 0x4c,
        0x08, 0x73, 0xd3, 0x91, 0xe9, 0x87, 0x98, 0x2f, 0xbb, 0xd3,
    };
    torrent.append(
        reinterpret_cast<const char*>(piece_hash.data()),
        piece_hash.size()
    );
    torrent += "ee";

    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    config.warm_torrent_timeout_milliseconds = 50;
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

    nuvio_engine_torrent_request add_request;
    nuvio_engine_torrent_request_init(&add_request);
    add_request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
    add_request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
    add_request.torrent_data_size = torrent.size();
    std::uint64_t add_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_add_torrent(engine, &add_request, &add_request_id),
        NUVIO_ENGINE_STATUS_OK
    );

    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    std::string torrent_id;
    for (int attempt = 0; attempt < 200 && torrent_id.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY) {
            torrent_id = event.torrent_id;
        }
    }
    NUVIO_EXPECT_TRUE(!torrent_id.empty());
    NUVIO_EXPECT_EQ(torrent_id, std::string(seeded_torrent_id));

    nuvio_engine_stream_request stream_request;
    nuvio_engine_stream_request_init(&stream_request);
    stream_request.torrent_id = torrent_id.c_str();
    stream_request.file_index = 0;
    std::uint64_t prepare_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_prepare_stream(engine, &stream_request, &prepare_request_id),
        NUVIO_ENGINE_STATUS_OK
    );

    std::string stream_url;
    std::string stream_id;
    for (int attempt = 0; attempt < 200 && stream_url.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_STREAM_PREPARED &&
            event.request_id == prepare_request_id) {
            stream_url = event.stream_url;
            stream_id = event.stream_id;
        }
    }
    NUVIO_EXPECT_TRUE(!stream_url.empty());
    const auto local = parse_local_stream_url(stream_url);

    const auto head = nuvio::test::send_http_request(
        local.port,
        "HEAD " + local.target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(head.find("HTTP/1.1 200 OK\r\n") == 0);
    NUVIO_EXPECT_TRUE(head.find("Content-Length: 4\r\n") != std::string::npos);
    NUVIO_EXPECT_TRUE(head.ends_with("\r\n\r\n"));

    const auto partial = nuvio::test::send_http_request(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=1-2\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(partial.find("HTTP/1.1 206 Partial Content\r\n") == 0);
    NUVIO_EXPECT_TRUE(partial.find("Content-Range: bytes 1-2/4\r\n") != std::string::npos);
    NUVIO_EXPECT_TRUE(partial.ends_with("es"));

    const auto full = nuvio::test::send_http_request(
        local.port,
        "GET " + local.target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(full.find("HTTP/1.1 200 OK\r\n") == 0);
    NUVIO_EXPECT_TRUE(full.ends_with("test"));

    nuvio_engine_stats stats;
    nuvio_engine_stats_init(&stats);
    for (int attempt = 0; attempt < 100; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        if (stats.memory_cache_hits > 0 && stats.active_http_requests == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NUVIO_EXPECT_EQ(stats.active_torrents, std::uint32_t(1));
    NUVIO_EXPECT_EQ(stats.active_streams, std::uint32_t(1));
    NUVIO_EXPECT_EQ(stats.active_http_requests, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.pending_piece_reads, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.memory_cache_capacity_bytes, config.memory_cache_capacity_bytes);
    NUVIO_EXPECT_EQ(stats.memory_cache_used_bytes, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stats.memory_cache_entries, std::uint64_t(1));
    NUVIO_EXPECT_TRUE(stats.memory_cache_hits >= 1);
    NUVIO_EXPECT_TRUE(stats.memory_cache_misses >= 1);

    nuvio_engine_stream_stats stream_stats;
    nuvio_engine_stream_stats_init(&stream_stats);
    for (int attempt = 0; attempt < 100; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stream_stats(engine, stream_id.c_str(), &stream_stats),
            NUVIO_ENGINE_STATUS_OK
        );
        if (stream_stats.contiguous_ready_bytes == 4 &&
            stream_stats.delivered_bytes >= 6) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NUVIO_EXPECT_EQ(stream_stats.file_index, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stream_stats.file_size, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stream_stats.contiguous_ready_bytes, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stream_stats.verified_file_bytes, std::uint64_t(4));
    NUVIO_EXPECT_TRUE(stream_stats.delivered_bytes >= 6);

    const auto denied = nuvio::test::send_http_request(
        local.port,
        "GET /stream/0000000000000000000000000000000000000000000000000000000000000000 "
        "HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(denied.find("HTTP/1.1 404 Not Found\r\n") == 0);

    std::uint64_t stop_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_stop_stream(engine, stream_id.c_str(), &stop_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    bool received_stopped = false;
    for (int attempt = 0; attempt < 200 && !received_stopped; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_stopped = event.type == NUVIO_ENGINE_EVENT_STREAM_STOPPED &&
            event.request_id == stop_request_id;
    }
    NUVIO_EXPECT_TRUE(received_stopped);
    NUVIO_EXPECT_EQ(std::string(event.stream_id), stream_id);
    NUVIO_EXPECT_EQ(std::string(event.torrent_id), torrent_id);
    const auto stopped = nuvio::test::send_http_request(
        local.port,
        "GET " + local.target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(stopped.find("HTTP/1.1 404 Not Found\r\n") == 0);
    std::size_t warm_file_count = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_file_count(engine, torrent_id.c_str(), &warm_file_count),
        NUVIO_ENGINE_STATUS_OK
    );
    NUVIO_EXPECT_EQ(warm_file_count, std::size_t(1));
    for (int attempt = 0; attempt < 100; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        if (stats.active_streams == 0 && stats.active_http_requests == 0 &&
            stats.quiesced_torrents == 1) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NUVIO_EXPECT_EQ(stats.active_torrents, std::uint32_t(1));
    NUVIO_EXPECT_EQ(stats.active_streams, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.warm_torrents, std::uint32_t(1));
    NUVIO_EXPECT_EQ(stats.quiesced_torrents, std::uint32_t(1));
    NUVIO_EXPECT_EQ(stats.memory_cache_used_bytes, std::uint64_t(4));

    std::uint64_t warm_prepare_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_prepare_stream(engine, &stream_request, &warm_prepare_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    std::string warm_stream_url;
    std::string warm_stream_id;
    for (int attempt = 0; attempt < 200 && warm_stream_url.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_STREAM_PREPARED &&
            event.request_id == warm_prepare_request_id) {
            warm_stream_url = event.stream_url;
            warm_stream_id = event.stream_id;
        }
    }
    NUVIO_EXPECT_TRUE(!warm_stream_url.empty());
    NUVIO_EXPECT_TRUE(warm_stream_id != stream_id);
    const auto warm_local = parse_local_stream_url(warm_stream_url);
    const auto warm = nuvio::test::send_http_request(
        warm_local.port,
        "GET " + warm_local.target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(warm.find("HTTP/1.1 200 OK\r\n") == 0);
    NUVIO_EXPECT_TRUE(warm.ends_with("test"));
    for (int attempt = 0; attempt < 100; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &stats),
            NUVIO_ENGINE_STATUS_OK
        );
        if (stats.warm_torrents == 0 && stats.quiesced_torrents == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NUVIO_EXPECT_EQ(stats.warm_torrents, std::uint32_t(0));
    NUVIO_EXPECT_EQ(stats.quiesced_torrents, std::uint32_t(0));

    std::uint64_t protected_reclaim_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_reclaim_disk_cache(
            engine,
            0,
            &protected_reclaim_request_id
        ),
        NUVIO_ENGINE_STATUS_OK
    );
    bool protected_reclaim_completed = false;
    for (int attempt = 0; attempt < 200 && !protected_reclaim_completed; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        protected_reclaim_completed =
            event.type == NUVIO_ENGINE_EVENT_DISK_CACHE_RECLAIMED &&
            event.request_id == protected_reclaim_request_id;
    }
    NUVIO_EXPECT_TRUE(protected_reclaim_completed);
    NUVIO_EXPECT_TRUE(
        std::string(event.message).find("protected") != std::string::npos
    );
    NUVIO_EXPECT_TRUE(
        std::filesystem::exists(
            directories.root() / "cache" / "payload" / seeded_torrent_id / "test.bin"
        )
    );
    NUVIO_EXPECT_EQ(
        nuvio_engine_get_stats(engine, &stats),
        NUVIO_ENGINE_STATUS_OK
    );
    NUVIO_EXPECT_EQ(stats.disk_cache_used_bytes, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stats.disk_cache_protected_bytes, std::uint64_t(4));

    std::uint64_t remove_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_remove_torrent(engine, torrent_id.c_str(), &remove_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    bool received_removed = false;
    for (int attempt = 0; attempt < 200 && !received_removed; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_removed = event.type == NUVIO_ENGINE_EVENT_TORRENT_REMOVED &&
            event.request_id == remove_request_id;
    }
    NUVIO_EXPECT_TRUE(received_removed);
    const auto revoked = nuvio::test::send_http_request(
        warm_local.port,
        "GET " + warm_local.target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(revoked.find("HTTP/1.1 404 Not Found\r\n") == 0);
    nuvio_engine_destroy(engine);
}

NUVIO_TEST("concurrent distant HTTP ranges remain blocking until disconnect") {
    EngineTestDirectories directories;
    constexpr std::uint64_t file_size = 64ULL * 1024ULL * 1024ULL;
    constexpr std::size_t piece_size = 16 * 1024;
    constexpr std::size_t piece_count = static_cast<std::size_t>(file_size) / piece_size;
    std::string torrent =
        "d4:infod6:lengthi67108864e4:name9:large.mkv12:piece lengthi16384e6:pieces81920:";
    torrent.append(piece_count * 20, '\0');
    torrent += "ee";

    nuvio_engine_config config;
    nuvio_engine_config_init(&config);
    directories.apply(config);
    nuvio_engine* engine = nullptr;
    NUVIO_EXPECT_EQ(nuvio_engine_create(&config, &engine), NUVIO_ENGINE_STATUS_OK);

    nuvio_engine_torrent_request add_request;
    nuvio_engine_torrent_request_init(&add_request);
    add_request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
    add_request.torrent_data = reinterpret_cast<const std::uint8_t*>(torrent.data());
    add_request.torrent_data_size = torrent.size();
    std::uint64_t add_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_add_torrent(engine, &add_request, &add_request_id),
        NUVIO_ENGINE_STATUS_OK
    );

    nuvio_engine_event event;
    nuvio_engine_event_init(&event);
    std::string torrent_id;
    for (int attempt = 0; attempt < 200 && torrent_id.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY) {
            torrent_id = event.torrent_id;
        }
    }
    NUVIO_EXPECT_TRUE(!torrent_id.empty());

    nuvio_engine_stream_request stream_request;
    nuvio_engine_stream_request_init(&stream_request);
    stream_request.torrent_id = torrent_id.c_str();
    stream_request.file_index = 0;
    std::uint64_t prepare_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_prepare_stream(engine, &stream_request, &prepare_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    std::string stream_url;
    std::string stream_id;
    for (int attempt = 0; attempt < 200 && stream_url.empty(); ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        if (event.type == NUVIO_ENGINE_EVENT_STREAM_PREPARED &&
            event.request_id == prepare_request_id) {
            stream_url = event.stream_url;
            stream_id = event.stream_id;
        }
    }
    NUVIO_EXPECT_TRUE(!stream_url.empty());
    NUVIO_EXPECT_TRUE(!stream_id.empty());
    const auto local = parse_local_stream_url(stream_url);

    RawHttpClient disconnected(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-\r\n\r\n"
    );
    nuvio_engine_stream_stats disconnect_stats;
    nuvio_engine_stream_stats_init(&disconnect_stats);
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto status = nuvio_engine_get_stream_stats(
            engine,
            stream_id.c_str(),
            &disconnect_stats
        );
        if (status == NUVIO_ENGINE_STATUS_NOT_FOUND) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(
            status,
            NUVIO_ENGINE_STATUS_OK
        );
        if (disconnect_stats.active_demands == 1) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NUVIO_EXPECT_EQ(disconnect_stats.active_demands, std::uint32_t(1));
    disconnected.close();

    nuvio_engine_stats disconnected_engine_stats;
    nuvio_engine_stats_init(&disconnected_engine_stats);
    const auto disconnected_at = std::chrono::steady_clock::now();
    for (int attempt = 0; attempt < 100; ++attempt) {
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stream_stats(engine, stream_id.c_str(), &disconnect_stats),
            NUVIO_ENGINE_STATUS_OK
        );
        NUVIO_EXPECT_EQ(
            nuvio_engine_get_stats(engine, &disconnected_engine_stats),
            NUVIO_ENGINE_STATUS_OK
        );
        if (disconnect_stats.active_demands == 0 &&
            disconnected_engine_stats.active_http_requests == 0 &&
            disconnected_engine_stats.pending_piece_reads == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NUVIO_EXPECT_EQ(disconnect_stats.active_demands, std::uint32_t(0));
    NUVIO_EXPECT_EQ(disconnect_stats.blocking_pieces, std::uint32_t(0));
    NUVIO_EXPECT_EQ(disconnected_engine_stats.active_http_requests, std::uint32_t(0));
    NUVIO_EXPECT_EQ(disconnected_engine_stats.pending_piece_reads, std::uint32_t(0));
    NUVIO_EXPECT_TRUE(
        std::chrono::steady_clock::now() - disconnected_at < std::chrono::seconds(1)
    );

    nuvio_engine_stream_stats range_stats;
    nuvio_engine_stream_stats_init(&range_stats);
    nuvio_engine_stats range_engine_stats;
    nuvio_engine_stats_init(&range_engine_stats);
    const auto wait_for_counts = [&](const std::uint32_t expected) {
        for (int attempt = 0; attempt < 300; ++attempt) {
            NUVIO_EXPECT_EQ(
                nuvio_engine_get_stream_stats(engine, stream_id.c_str(), &range_stats),
                NUVIO_ENGINE_STATUS_OK
            );
            NUVIO_EXPECT_EQ(
                nuvio_engine_get_stats(engine, &range_engine_stats),
                NUVIO_ENGINE_STATUS_OK
            );
            if (range_stats.active_demands == expected &&
                range_engine_stats.active_http_requests == expected &&
                range_engine_stats.pending_piece_reads == expected) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    const auto require_counts = [&](const std::uint32_t expected,
                                    const std::string_view stage) {
        if (wait_for_counts(expected)) {
            return;
        }
        throw std::runtime_error(
            std::string(stage) + " timed out: demands=" +
            std::to_string(range_stats.active_demands) + ", http=" +
            std::to_string(range_engine_stats.active_http_requests) + ", reads=" +
            std::to_string(range_engine_stats.pending_piece_reads)
        );
    };

    RawHttpClient head(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-16383\r\n\r\n"
    );
    require_counts(1, "head open");
    NUVIO_EXPECT_EQ(range_stats.active_demands, std::uint32_t(1));

    RawHttpClient tail(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=67092480-67108863\r\n\r\n"
    );
    require_counts(2, "tail open");
    NUVIO_EXPECT_EQ(range_stats.active_demands, std::uint32_t(2));

    RawHttpClient resume(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=33554432-33570815\r\n\r\n"
    );
    require_counts(3, "resume open");
    NUVIO_EXPECT_EQ(range_stats.active_demands, std::uint32_t(3));
    NUVIO_EXPECT_EQ(range_engine_stats.active_http_requests, std::uint32_t(3));
    NUVIO_EXPECT_EQ(range_engine_stats.pending_piece_reads, std::uint32_t(3));
    NUVIO_EXPECT_EQ(range_stats.primary_demand_start, std::uint64_t(33'554'432));
    NUVIO_EXPECT_EQ(range_stats.secondary_demand_start, std::uint64_t(67'092'480));
    NUVIO_EXPECT_EQ(range_stats.blocking_pieces, std::uint32_t(3));
    NUVIO_EXPECT_EQ(range_stats.primary_blocking_piece, std::uint32_t(2'048));
    NUVIO_EXPECT_EQ(range_stats.secondary_blocking_piece, std::uint32_t(4'095));
    NUVIO_EXPECT_EQ(range_stats.scheduled_pieces, std::uint32_t(2'049));
    NUVIO_EXPECT_TRUE(range_stats.schedule_revision >= std::uint64_t(1));

    RawHttpClient fourth(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=16777216-16793599\r\n\r\n"
    );
    require_counts(4, "fourth range open");
    RawHttpClient fifth(
        local.port,
        "GET " + local.target +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=50331648-50348031\r\n\r\n"
    );
    require_counts(5, "fifth range open");
    NUVIO_EXPECT_EQ(range_stats.blocking_pieces, std::uint32_t(5));
    NUVIO_EXPECT_EQ(range_stats.primary_blocking_piece, std::uint32_t(3'072));
    NUVIO_EXPECT_EQ(range_stats.secondary_blocking_piece, std::uint32_t(1'024));

    fifth.close();
    require_counts(4, "fifth range closed");
    fourth.close();
    require_counts(3, "fourth range closed");
    NUVIO_EXPECT_EQ(range_stats.primary_blocking_piece, std::uint32_t(2'048));
    NUVIO_EXPECT_EQ(range_stats.secondary_blocking_piece, std::uint32_t(4'095));

    head.close();
    require_counts(2, "head range closed");
    NUVIO_EXPECT_EQ(range_stats.active_demands, std::uint32_t(2));
    NUVIO_EXPECT_EQ(range_stats.primary_demand_start, std::uint64_t(33'554'432));
    NUVIO_EXPECT_EQ(range_stats.secondary_demand_start, std::uint64_t(67'092'480));
    NUVIO_EXPECT_EQ(range_stats.blocking_pieces, std::uint32_t(2));
    NUVIO_EXPECT_EQ(range_stats.primary_blocking_piece, std::uint32_t(2'048));
    NUVIO_EXPECT_EQ(range_stats.secondary_blocking_piece, std::uint32_t(4'095));

    resume.close();
    require_counts(1, "resume range closed");
    NUVIO_EXPECT_EQ(range_stats.active_demands, std::uint32_t(1));
    NUVIO_EXPECT_EQ(range_stats.primary_demand_start, std::uint64_t(67'092'480));
    NUVIO_EXPECT_EQ(range_stats.blocking_pieces, std::uint32_t(1));
    NUVIO_EXPECT_EQ(range_stats.primary_blocking_piece, std::uint32_t(4'095));

    tail.close();
    require_counts(0, "tail range closed");
    NUVIO_EXPECT_EQ(range_stats.active_demands, std::uint32_t(0));
    NUVIO_EXPECT_EQ(range_stats.blocking_pieces, std::uint32_t(0));
    NUVIO_EXPECT_EQ(range_engine_stats.active_http_requests, std::uint32_t(0));
    NUVIO_EXPECT_EQ(range_engine_stats.pending_piece_reads, std::uint32_t(0));

    std::uint64_t remove_request_id = 0;
    NUVIO_EXPECT_EQ(
        nuvio_engine_remove_torrent(engine, torrent_id.c_str(), &remove_request_id),
        NUVIO_ENGINE_STATUS_OK
    );
    bool received_removed = false;
    for (int attempt = 0; attempt < 200 && !received_removed; ++attempt) {
        const auto status = nuvio_engine_poll_event(engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        NUVIO_EXPECT_EQ(status, NUVIO_ENGINE_STATUS_OK);
        received_removed = event.type == NUVIO_ENGINE_EVENT_TORRENT_REMOVED &&
            event.request_id == remove_request_id;
    }
    NUVIO_EXPECT_TRUE(received_removed);
    nuvio_engine_destroy(engine);
}
#endif
