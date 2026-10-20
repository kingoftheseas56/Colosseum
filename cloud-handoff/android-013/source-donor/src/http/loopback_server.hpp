#ifndef NUVIO_ENGINE_LOOPBACK_SERVER_HPP
#define NUVIO_ENGINE_LOOPBACK_SERVER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>

#include "nuvio_engine/http_protocol.hpp"

namespace nuvio::http {

class LoopbackServer {
public:
    using Writer = std::function<bool(std::span<const char>)>;
    using Handler = std::function<void(
        const HttpRequest&,
        const Writer&,
        std::stop_token
    )>;

    explicit LoopbackServer(
        std::uint16_t requested_port,
        Handler handler,
        std::size_t worker_count = 4,
        std::size_t connection_capacity = 32
    );
    ~LoopbackServer();

    LoopbackServer(const LoopbackServer&) = delete;
    LoopbackServer& operator=(const LoopbackServer&) = delete;

    [[nodiscard]] std::uint16_t port() const;
    [[nodiscard]] std::uint32_t active_request_count() const;
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

#endif
