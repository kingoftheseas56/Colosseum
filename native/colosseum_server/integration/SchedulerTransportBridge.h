#pragma once

#include "scheduler/SchedulerSpine.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace colosseum::server::integration {

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
                             IBlockTransport &transport);
    ~SchedulerTransportBridge();
    void upsertPeer(scheduler::PeerState peer);
    void retirePeer(const std::string &peerId);
    void pump();

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
};

} // namespace colosseum::server::integration
