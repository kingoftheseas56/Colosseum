#include "test_support.hpp"

#include "http/loopback_server.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
#endif

void close_client(const NativeSocket socket) {
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

std::string send_request(const std::uint16_t port, const std::string& request) {
    const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == invalid_socket) {
        throw std::runtime_error("failed to create test client socket");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close_client(client);
        throw std::runtime_error("failed to connect test client socket");
    }

    std::size_t offset = 0;
    while (offset < request.size()) {
        const auto remaining = request.size() - offset;
        const auto chunk = static_cast<int>(std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<int>::max())
        ));
#if defined(_WIN32)
        const auto written = send(client, request.data() + offset, chunk, 0);
        if (written == SOCKET_ERROR || written == 0) {
#else
        const auto written = send(
            client,
            request.data() + offset,
            static_cast<std::size_t>(chunk),
            0
        );
        if (written <= 0) {
#endif
            close_client(client);
            throw std::runtime_error("failed to write test HTTP request");
        }
        offset += static_cast<std::size_t>(written);
    }

    std::string response;
    std::array<char, 4096> buffer{};
    while (true) {
#if defined(_WIN32)
        const auto count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count == SOCKET_ERROR) {
            close_client(client);
            throw std::runtime_error("failed to read test HTTP response");
        }
#else
        const auto count = recv(client, buffer.data(), buffer.size(), 0);
        if (count < 0) {
            close_client(client);
            throw std::runtime_error("failed to read test HTTP response");
        }
#endif
        if (count == 0) {
            break;
        }
        response.append(buffer.data(), static_cast<std::size_t>(count));
    }
    close_client(client);
    return response;
}

}

NUVIO_TEST("loopback listener serves a bounded partial response") {
    const std::string content = "abcdefghij";
    nuvio::http::LoopbackServer server(
        0,
        [&](const nuvio::http::HttpRequest& request,
            const nuvio::http::LoopbackServer::Writer& writer,
            const std::stop_token) {
            const auto response = nuvio::http::build_stream_response(
                request,
                content.size(),
                "video/mp4"
            );
            NUVIO_EXPECT_TRUE(writer(std::span<const char>(response.headers)));
            if (response.include_body) {
                NUVIO_EXPECT_TRUE(response.body_range.has_value());
                const auto start = static_cast<std::size_t>(response.body_range->start);
                const auto length = static_cast<std::size_t>(response.body_range->length());
                NUVIO_EXPECT_TRUE(writer(std::span<const char>(content).subspan(start, length)));
            }
        }
    );
    NUVIO_EXPECT_TRUE(server.port() > 0);
    const auto response = send_request(
        server.port(),
        "GET /stream/token HTTP/1.1\r\nRange: bytes=2-5\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(response.find("HTTP/1.1 206 Partial Content\r\n") == 0);
    NUVIO_EXPECT_TRUE(response.find("Content-Range: bytes 2-5/10\r\n") != std::string::npos);
    NUVIO_EXPECT_TRUE(response.ends_with("cdef"));
}

NUVIO_TEST("loopback listener cancels a handler when its client disconnects") {
    std::atomic_bool handler_started = false;
    std::atomic_bool handler_cancelled = false;
    nuvio::http::LoopbackServer server(
        0,
        [&](const nuvio::http::HttpRequest&,
            const nuvio::http::LoopbackServer::Writer& writer,
            const std::stop_token token) {
            constexpr std::string_view headers =
                "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nConnection: close\r\n\r\n";
            if (!writer(std::span<const char>(headers))) {
                return;
            }
            handler_started.store(true);
            while (!token.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            handler_cancelled.store(true);
        },
        1,
        2
    );

    const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    NUVIO_EXPECT_TRUE(client != invalid_socket);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(server.port());
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    NUVIO_EXPECT_EQ(
        connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)),
        0
    );
    constexpr std::string_view request =
        "GET /stream/token HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
#if defined(_WIN32)
    NUVIO_EXPECT_EQ(
        send(client, request.data(), static_cast<int>(request.size()), 0),
        static_cast<int>(request.size())
    );
#else
    NUVIO_EXPECT_EQ(
        send(client, request.data(), request.size(), 0),
        static_cast<ssize_t>(request.size())
    );
#endif
    for (int attempt = 0; attempt < 100 && !handler_started.load(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    NUVIO_EXPECT_TRUE(handler_started.load());

    const auto disconnected_at = std::chrono::steady_clock::now();
    close_client(client);
    for (int attempt = 0; attempt < 100 && !handler_cancelled.load(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    NUVIO_EXPECT_TRUE(handler_cancelled.load());
    NUVIO_EXPECT_TRUE(
        std::chrono::steady_clock::now() - disconnected_at < std::chrono::seconds(1)
    );
    for (int attempt = 0; attempt < 100 && server.active_request_count() != 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    NUVIO_EXPECT_EQ(server.active_request_count(), std::uint32_t(0));
}

NUVIO_TEST("loopback listener rejects malformed requests before dispatch") {
    std::atomic_int handler_calls = 0;
    nuvio::http::LoopbackServer server(
        0,
        [&](const nuvio::http::HttpRequest&,
            const nuvio::http::LoopbackServer::Writer&,
            const std::stop_token) {
            ++handler_calls;
        },
        2,
        4
    );
    const auto response = send_request(
        server.port(),
        "GET /stream/token HTTP/2\r\n\r\n"
    );
    NUVIO_EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
    NUVIO_EXPECT_EQ(handler_calls.load(), 0);
}

NUVIO_TEST("loopback listener cancels an incomplete active request on stop") {
    nuvio::http::LoopbackServer server(
        0,
        [](const nuvio::http::HttpRequest&,
           const nuvio::http::LoopbackServer::Writer&,
           const std::stop_token) {
        },
        1,
        2
    );
    const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    NUVIO_EXPECT_TRUE(client != invalid_socket);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(server.port());
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    NUVIO_EXPECT_EQ(
        connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)),
        0
    );
    constexpr std::string_view partial_request = "GET ";
#if defined(_WIN32)
    NUVIO_EXPECT_EQ(
        send(client, partial_request.data(), static_cast<int>(partial_request.size()), 0),
        static_cast<int>(partial_request.size())
    );
#else
    NUVIO_EXPECT_EQ(
        send(client, partial_request.data(), partial_request.size(), 0),
        static_cast<ssize_t>(partial_request.size())
    );
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto started = std::chrono::steady_clock::now();
    server.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    close_client(client);
    NUVIO_EXPECT_TRUE(elapsed < std::chrono::seconds(1));
}
