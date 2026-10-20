#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

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

namespace nuvio::test {

std::vector<Test>& registry() {
    static std::vector<Test> tests;
    return tests;
}

Registrar::Registrar(std::string name, std::function<void()> test) {
    registry().emplace_back(std::move(name), std::move(test));
}

std::string send_http_request(const std::uint16_t port, const std::string& request) {
#if defined(_WIN32)
    using NativeSocket = SOCKET;
    constexpr auto invalid_socket = INVALID_SOCKET;
#else
    using NativeSocket = int;
    constexpr auto invalid_socket = -1;
#endif
    const NativeSocket client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == invalid_socket) {
        throw std::runtime_error("failed to create test HTTP socket");
    }
    const auto close_client = [&] {
#if defined(_WIN32)
        closesocket(client);
#else
        close(client);
#endif
    };
#if defined(_WIN32)
    constexpr DWORD timeout = 10000;
    setsockopt(
        client,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout),
        sizeof(timeout)
    );
#else
    constexpr timeval timeout{10, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#if defined(SO_NOSIGPIPE)
    constexpr int enabled = 1;
    setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close_client();
        throw std::runtime_error("failed to connect test HTTP socket");
    }
    std::size_t offset = 0;
    while (offset < request.size()) {
        const auto remaining = request.size() - offset;
#if defined(_WIN32)
        const auto chunk = static_cast<int>(std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<int>::max())
        ));
        const auto count = send(client, request.data() + offset, chunk, 0);
        if (count == SOCKET_ERROR || count == 0) {
#else
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto count = send(client, request.data() + offset, remaining, flags);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
#endif
            close_client();
            throw std::runtime_error("failed to send test HTTP request");
        }
        offset += static_cast<std::size_t>(count);
    }
    std::string response;
    std::array<char, 4096> buffer{};
    while (true) {
#if defined(_WIN32)
        const auto count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count == SOCKET_ERROR) {
#else
        const auto count = recv(client, buffer.data(), buffer.size(), 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
#endif
            close_client();
            throw std::runtime_error("failed to receive test HTTP response");
        }
        if (count == 0) {
            break;
        }
        response.append(buffer.data(), static_cast<std::size_t>(count));
    }
    close_client();
    return response;
}

}

int main() {
    std::size_t failures = 0;
    for (const auto& [name, test] : nuvio::test::registry()) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << nuvio::test::registry().size() << " tests, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
