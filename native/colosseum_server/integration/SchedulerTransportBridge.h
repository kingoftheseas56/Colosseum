#pragma once

#include "scheduler/SchedulerSpine.h"

#include <cstdint>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace colosseum::server::integration {

struct SchedulerTransportMetrics final
{
    std::atomic<std::uint64_t> schedulerDispatches{0};
    std::atomic<std::uint64_t> transportRequests{0};
    std::atomic<std::uint64_t> transportCompletions{0};
    std::atomic<std::uint64_t> wireRequestsAuthorized{0};
    std::atomic<std::uint64_t> wireRequestsSuppressed{0};
};

struct SchedulerTransportMetricsSnapshot final
{
    std::uint64_t schedulerDispatches = 0;
    std::uint64_t transportRequests = 0;
    std::uint64_t transportCompletions = 0;
    std::uint64_t wireRequestsAuthorized = 0;
    std::uint64_t wireRequestsSuppressed = 0;
};

class IBlockTransport
{
public:
    using Completion = std::function<void(std::error_code, std::vector<std::byte>)>;

    virtual ~IBlockTransport() = default;
    virtual void requestBlock(const std::string &peerHint,
                              const scheduler::WireBlock &block,
                              Completion completion) = 0;
    virtual void cancelBlock(const std::string &peerHint,
                             const scheduler::WireBlock &block) = 0;
    virtual void pumpResults() {}
};

class SchedulerTransportBridge final
{
public:
    using CompletedObserver = std::function<void(scheduler::CompletedPiece)>;

    SchedulerTransportBridge(scheduler::SchedulerSpine &scheduler,
                             IBlockTransport &transport,
                             std::shared_ptr<SchedulerTransportMetrics> metrics = {});
    ~SchedulerTransportBridge();
    void upsertPeer(scheduler::PeerState peer);
    void retirePeer(const std::string &peerId);
    void pump();

    // Manual libtorrent transports need an initial request to provoke the
    // remote unchoke after sending interest. Other transports retain the
    // scheduler's normal unchoked-only behavior by default.
    void setAllowChokedBootstrap(bool enabled) noexcept
    {
        allowChokedBootstrap_ = enabled;
    }

    // Test-only negative-control seam. Production sessions leave dispatch
    // enabled; disabling it proves that libtorrent's ordinary picker cannot
    // satisfy a stream behind W06's back.
    void setDispatchEnabled(bool enabled) noexcept
    {
        dispatchEnabled_ = enabled;
    }

    void setCompletedObserver(CompletedObserver observer)
    {
        completedObserver_ = std::move(observer);
    }

private:
    struct ActiveRequest {
        std::string peerId;
        std::size_t streamPiece = 0;
        scheduler::WireBlock wire;
        bool cancelled = false;
    };

    void dispatch(const std::string &peerId,
                  const scheduler::OutstandingRequest &request);
    void onHotswap(const std::string &fromPeer,
                   const std::string &toPeer,
                   std::size_t streamPiece);

    scheduler::SchedulerSpine &scheduler_;
    IBlockTransport &transport_;
    std::set<std::string> peerIds_;
    std::map<std::uint64_t, ActiveRequest> active_;
    CompletedObserver completedObserver_;
    std::shared_ptr<SchedulerTransportMetrics> metrics_;
    bool allowChokedBootstrap_ = false;
    bool dispatchEnabled_ = true;
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
};

} // namespace colosseum::server::integration
