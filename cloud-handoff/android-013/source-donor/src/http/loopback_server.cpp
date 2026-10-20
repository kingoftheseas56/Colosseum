#include "http/loopback_server.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace nuvio::http {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
#endif

constexpr std::size_t maximum_request_head_size = 16 * 1024;

class AtomicCounterGuard {
public:
    explicit AtomicCounterGuard(std::atomic_uint32_t& counter) : counter_(counter) {
        counter_.fetch_add(1);
    }

    ~AtomicCounterGuard() {
        counter_.fetch_sub(1);
    }

    AtomicCounterGuard(const AtomicCounterGuard&) = delete;
    AtomicCounterGuard& operator=(const AtomicCounterGuard&) = delete;

private:
    std::atomic_uint32_t& counter_;
};

int socket_error() {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

void close_socket(const NativeSocket socket) {
    if (socket == invalid_socket) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

void shutdown_socket(const NativeSocket socket) {
    if (socket == invalid_socket) {
        return;
    }
#if defined(_WIN32)
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

void monitor_client_disconnect(
    const NativeSocket socket,
    const std::atomic_bool& finishing,
    std::stop_source& request_stop
) {
#if defined(_WIN32)
    while (!finishing.load()) {
        fd_set readable;
        fd_set exceptional;
        FD_ZERO(&readable);
        FD_ZERO(&exceptional);
        FD_SET(socket, &readable);
        FD_SET(socket, &exceptional);
        const auto ready = select(0, &readable, nullptr, &exceptional, nullptr);
        if (ready == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEINTR) {
                continue;
            }
            if (!finishing.load()) {
                request_stop.request_stop();
            }
            return;
        }
        if (finishing.load()) {
            return;
        }
        if (FD_ISSET(socket, &exceptional)) {
            request_stop.request_stop();
            return;
        }
        char value = 0;
        const auto count = recv(socket, &value, 1, MSG_PEEK);
        if (count == SOCKET_ERROR) {
            const auto error = WSAGetLastError();
            if (error == WSAEINTR || error == WSAEWOULDBLOCK) {
                continue;
            }
        }
        request_stop.request_stop();
        return;
    }
#else
    short events = POLLIN | POLLERR | POLLHUP;
#if defined(POLLRDHUP)
    events = static_cast<short>(events | POLLRDHUP);
#endif
    pollfd descriptor{socket, events, 0};
    while (!finishing.load()) {
        descriptor.revents = 0;
        const auto ready = poll(&descriptor, 1, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!finishing.load()) {
                request_stop.request_stop();
            }
            return;
        }
        if (finishing.load()) {
            return;
        }
        constexpr short terminal_events = POLLERR | POLLHUP | POLLNVAL;
        if ((descriptor.revents & terminal_events) != 0) {
            request_stop.request_stop();
            return;
        }
#if defined(POLLRDHUP)
        if ((descriptor.revents & POLLRDHUP) != 0) {
            request_stop.request_stop();
            return;
        }
#endif
        if ((descriptor.revents & POLLIN) == 0) {
            continue;
        }
        char value = 0;
        const auto count = recv(socket, &value, 1, MSG_PEEK | MSG_DONTWAIT);
        if (count < 0 &&
            (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        request_stop.request_stop();
        return;
    }
#endif
}

class DisconnectMonitor {
public:
    DisconnectMonitor(const NativeSocket socket, std::stop_source& request_stop)
        : socket_(socket),
          request_stop_(request_stop),
          thread_([this] {
              monitor_client_disconnect(socket_, finishing_, request_stop_);
          }) {
    }

    ~DisconnectMonitor() {
        finishing_.store(true);
        shutdown_socket(socket_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    DisconnectMonitor(const DisconnectMonitor&) = delete;
    DisconnectMonitor& operator=(const DisconnectMonitor&) = delete;

private:
    NativeSocket socket_;
    std::stop_source& request_stop_;
    std::atomic_bool finishing_ = false;
    std::thread thread_;
};

class NetworkRuntime {
public:
    NetworkRuntime() {
#if defined(_WIN32)
        WSADATA data{};
        const auto result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            throw std::system_error(result, std::system_category(), "initialize Winsock");
        }
#endif
    }

    ~NetworkRuntime() {
#if defined(_WIN32)
        WSACleanup();
#endif
    }
};

class OwnedSocket {
public:
    OwnedSocket() = default;
    explicit OwnedSocket(const NativeSocket socket) : socket_(socket) {
    }

    ~OwnedSocket() {
        close_socket(socket_);
    }

    OwnedSocket(const OwnedSocket&) = delete;
    OwnedSocket& operator=(const OwnedSocket&) = delete;

    OwnedSocket(OwnedSocket&& other) noexcept
        : socket_(std::exchange(other.socket_, invalid_socket)) {
    }

    OwnedSocket& operator=(OwnedSocket&& other) noexcept {
        if (this != &other) {
            close_socket(socket_);
            socket_ = std::exchange(other.socket_, invalid_socket);
        }
        return *this;
    }

    [[nodiscard]] NativeSocket get() const {
        return socket_;
    }

    [[nodiscard]] NativeSocket release() {
        return std::exchange(socket_, invalid_socket);
    }

private:
    NativeSocket socket_ = invalid_socket;
};

class SharedSocket {
public:
    explicit SharedSocket(const NativeSocket socket) : socket_(socket) {
    }

    ~SharedSocket() {
        close();
    }

    [[nodiscard]] NativeSocket get() const {
        return socket_.load();
    }

    void shutdown() const {
        std::lock_guard lock(lifetime_mutex_);
        shutdown_socket(socket_.load());
    }

    void close() {
        std::lock_guard lock(lifetime_mutex_);
        const auto socket = socket_.exchange(invalid_socket);
        shutdown_socket(socket);
        close_socket(socket);
    }

private:
    mutable std::mutex lifetime_mutex_;
    std::atomic<NativeSocket> socket_;
};

bool configure_client_socket(const NativeSocket socket) {
#if defined(_WIN32)
    constexpr DWORD timeout = 5000;
    setsockopt(
        socket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout),
        sizeof(timeout)
    );
    setsockopt(
        socket,
        SOL_SOCKET,
        SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout),
        sizeof(timeout)
    );
#else
    constexpr timeval timeout{5, 0};
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#if defined(SO_NOSIGPIPE)
    constexpr int enabled = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        return false;
    }
#endif
#endif
    return true;
}

bool send_all(
    const NativeSocket socket,
    const std::span<const char> contents,
    const std::stop_token stop_token
) {
    std::size_t offset = 0;
    while (offset < contents.size() && !stop_token.stop_requested()) {
        const auto remaining = contents.size() - offset;
#if defined(_WIN32)
        const auto chunk = static_cast<int>(std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<int>::max())
        ));
        const auto written = send(socket, contents.data() + offset, chunk, 0);
        if (written == SOCKET_ERROR || written == 0) {
            return false;
        }
#else
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto chunk = std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<int>::max())
        );
        const auto written = send(socket, contents.data() + offset, chunk, flags);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }
#endif
        offset += static_cast<std::size_t>(written);
    }
    return offset == contents.size();
}

std::optional<std::string> receive_request_head(
    const NativeSocket socket,
    const std::stop_token stop_token,
    bool& exceeded_limit
) {
    std::string received;
    received.reserve(4096);
    std::array<char, 4096> buffer{};
    while (!stop_token.stop_requested()) {
#if defined(_WIN32)
        const auto count = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count == SOCKET_ERROR || count == 0) {
            return std::nullopt;
        }
#else
        const auto count = recv(socket, buffer.data(), buffer.size(), 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }
        if (count == 0) {
            return std::nullopt;
        }
#endif
        received.append(buffer.data(), static_cast<std::size_t>(count));
        const auto end = received.find("\r\n\r\n");
        if (end != std::string::npos) {
            const auto head_size = end + 4;
            if (head_size > maximum_request_head_size) {
                exceeded_limit = true;
                return std::nullopt;
            }
            received.resize(head_size);
            return received;
        }
        if (received.size() >= maximum_request_head_size) {
            exceeded_limit = true;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

NativeSocket create_listener(const std::uint16_t requested_port, std::uint16_t& actual_port) {
    const auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == invalid_socket) {
        throw std::system_error(socket_error(), std::system_category(), "create HTTP socket");
    }
    OwnedSocket guard(listener);
#if defined(_WIN32)
    constexpr int enabled = 1;
    if (setsockopt(
            listener,
            SOL_SOCKET,
            SO_EXCLUSIVEADDRUSE,
            reinterpret_cast<const char*>(&enabled),
            sizeof(enabled)
        ) != 0) {
        throw std::system_error(
            socket_error(),
            std::system_category(),
            "make HTTP loopback exclusive"
        );
    }
#else
    constexpr int enabled = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(requested_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw std::system_error(socket_error(), std::system_category(), "bind HTTP loopback");
    }
    if (listen(listener, 64) != 0) {
        throw std::system_error(socket_error(), std::system_category(), "listen on HTTP loopback");
    }
    sockaddr_in bound{};
#if defined(_WIN32)
    int length = sizeof(bound);
#else
    socklen_t length = sizeof(bound);
#endif
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
        throw std::system_error(socket_error(), std::system_category(), "inspect HTTP loopback");
    }
    actual_port = ntohs(bound.sin_port);
    return guard.release();
}

}

struct LoopbackServer::Impl {
    Impl(
        const std::uint16_t requested_port,
        Handler request_handler,
        const std::size_t worker_count,
        const std::size_t connection_capacity
    )
        : handler(std::move(request_handler)),
          capacity(connection_capacity) {
        if (!handler || worker_count == 0 || worker_count > 32 ||
            capacity == 0 || capacity > 1024) {
            throw std::invalid_argument("HTTP listener requires a handler and bounded workers");
        }
        const auto socket = create_listener(requested_port, bound_port);
        listener.store(socket);
        try {
            workers.reserve(worker_count);
            for (std::size_t index = 0; index < worker_count; ++index) {
                workers.emplace_back([this](const std::stop_token token) {
                    try {
                        worker_loop(token);
                    } catch (...) {
                    }
                });
            }
            accepter = std::jthread([this](const std::stop_token token) {
                try {
                    accept_loop(token);
                } catch (...) {
                }
            });
        } catch (...) {
            stop();
            throw;
        }
    }

    ~Impl() {
        stop();
    }

    void stop() {
        if (stopping.exchange(true)) {
            return;
        }
        accepter.request_stop();
        for (auto& worker : workers) {
            worker.request_stop();
        }
        const auto socket = listener.exchange(invalid_socket);
        shutdown_socket(socket);
        close_socket(socket);
        {
            std::lock_guard lock(queue_mutex);
            for (const auto& connection : queue) {
                connection->close();
            }
            for (const auto& connection : active_connections) {
                connection->shutdown();
            }
            queue.clear();
        }
        queue_ready.notify_all();
        if (accepter.joinable()) {
            accepter.join();
        }
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
    }

    void accept_loop(const std::stop_token token) {
        while (!token.stop_requested() && !stopping.load()) {
            const auto listening_socket = listener.load();
            if (listening_socket == invalid_socket) {
                return;
            }
            const auto client = accept(listening_socket, nullptr, nullptr);
            if (client == invalid_socket) {
                if (stopping.load()) {
                    return;
                }
                continue;
            }
            OwnedSocket accepted(client);
            if (!configure_client_socket(client)) {
                continue;
            }
            auto connection = std::make_shared<SharedSocket>(client);
            static_cast<void>(accepted.release());
            std::lock_guard lock(queue_mutex);
            if (stopping.load() || queue.size() >= capacity) {
                continue;
            }
            queue.push_back(std::move(connection));
            queue_ready.notify_one();
        }
    }

    void worker_loop(const std::stop_token token) {
        while (!token.stop_requested()) {
            std::shared_ptr<SharedSocket> connection;
            {
                std::unique_lock lock(queue_mutex);
                queue_ready.wait(lock, [this, &token] {
                    return token.stop_requested() || stopping.load() || !queue.empty();
                });
                if (token.stop_requested() || stopping.load()) {
                    return;
                }
                connection = std::move(queue.front());
                queue.pop_front();
                active_connections.insert(connection);
            }
            try {
                handle_connection(connection, token);
            } catch (...) {
            }
            connection->close();
            {
                std::lock_guard lock(queue_mutex);
                active_connections.erase(connection);
            }
        }
    }

    void handle_connection(
        const std::shared_ptr<SharedSocket>& connection,
        const std::stop_token token
    ) {
        const auto client = connection->get();
        if (client == invalid_socket) {
            return;
        }
        bool exceeded_limit = false;
        const auto head = receive_request_head(client, token, exceeded_limit);
        if (!head.has_value()) {
            if (exceeded_limit) {
                const auto response = build_error_response(
                    ResponseStatus::request_header_fields_too_large
                );
                static_cast<void>(send_all(client, response, token));
            }
            return;
        }
        const auto parsed = parse_http_request_head(*head);
        if (parsed.status != RequestParseStatus::ok || !parsed.request.has_value()) {
            auto status = ResponseStatus::bad_request;
            if (parsed.status == RequestParseStatus::method_not_allowed) {
                status = ResponseStatus::method_not_allowed;
            } else if (parsed.status == RequestParseStatus::header_too_large) {
                status = ResponseStatus::request_header_fields_too_large;
            }
            const auto response = build_error_response(status);
            static_cast<void>(send_all(client, response, token));
            return;
        }

        std::stop_source request_stop;
        std::stop_callback worker_stop(token, [&request_stop] {
            request_stop.request_stop();
        });
        DisconnectMonitor disconnect_monitor(client, request_stop);
        const auto request_token = request_stop.get_token();
        auto wrote_response = std::make_shared<std::atomic_bool>(false);
        const Writer writer = [connection, request_token, wrote_response](
                                  const std::span<const char> data
                              ) {
            if (!data.empty()) {
                wrote_response->store(true);
            }
            const auto socket = connection->get();
            if (socket == invalid_socket) {
                return false;
            }
            const auto sent = send_all(socket, data, request_token);
            return sent;
        };
        const AtomicCounterGuard request_guard(active_requests);
        try {
            handler(*parsed.request, writer, request_token);
            if (!wrote_response->load() && !request_token.stop_requested()) {
                const auto response = build_error_response(ResponseStatus::internal_server_error);
                static_cast<void>(send_all(client, response, request_token));
            }
        } catch (...) {
            if (!wrote_response->load() && !request_token.stop_requested()) {
                const auto response = build_error_response(ResponseStatus::internal_server_error);
                static_cast<void>(send_all(client, response, request_token));
            }
        }
    }

    NetworkRuntime network_runtime;
    Handler handler;
    const std::size_t capacity;
    std::atomic<NativeSocket> listener{invalid_socket};
    std::uint16_t bound_port = 0;
    std::atomic_bool stopping = false;
    std::atomic_uint32_t active_requests = 0;
    std::mutex queue_mutex;
    std::condition_variable queue_ready;
    std::deque<std::shared_ptr<SharedSocket>> queue;
    std::unordered_set<std::shared_ptr<SharedSocket>> active_connections;
    std::vector<std::jthread> workers;
    std::jthread accepter;
};

LoopbackServer::LoopbackServer(
    const std::uint16_t requested_port,
    Handler handler,
    const std::size_t worker_count,
    const std::size_t connection_capacity
)
    : impl_(std::make_unique<Impl>(
          requested_port,
          std::move(handler),
          worker_count,
          connection_capacity
      )) {
}

LoopbackServer::~LoopbackServer() = default;

std::uint16_t LoopbackServer::port() const {
    return impl_->bound_port;
}

std::uint32_t LoopbackServer::active_request_count() const {
    return impl_->active_requests.load();
}

void LoopbackServer::stop() {
    impl_->stop();
}

}
