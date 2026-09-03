#include "SchedulerTransportBridge.h"

#include <utility>

namespace colosseum::server::integration {

SchedulerTransportBridge::SchedulerTransportBridge(
    scheduler::SchedulerSpine &scheduler,
    IBlockTransport &transport)
    : scheduler_(scheduler), transport_(transport)
{
    scheduler_.setHotswapObserver(
        [this](const std::string &fromPeer,
               const std::string &toPeer,
               std::size_t streamPiece) {
            onHotswap(fromPeer, toPeer, streamPiece);
        });
}

SchedulerTransportBridge::~SchedulerTransportBridge()
{
    scheduler_.setHotswapObserver({});
    for (const auto &[id, request] : active_) {
        if (!request.cancelled)
            transport_.cancelBlock(request.peerId, request.wire);
    }
}

void SchedulerTransportBridge::upsertPeer(scheduler::PeerState peer)
{
    peerIds_.insert(peer.id);
    scheduler_.upsertPeer(std::move(peer));
}
void SchedulerTransportBridge::retirePeer(const std::string &peerId)
{
    peerIds_.erase(peerId);
    if (auto *state = scheduler_.peer(peerId)) {
        state->peerChoking = true;
        state->peerPieces.clear();
    }

    for (auto &[id, request] : active_) {
        if (request.peerId == peerId && !request.cancelled) {
            request.cancelled = true;
            transport_.cancelBlock(request.peerId, request.wire);
            scheduler_.failRequest(id);
        }
    }
}

void SchedulerTransportBridge::pump()
{
    transport_.pumpResults();
    for (const auto &peerId : peerIds_) {
        const auto created = scheduler_.updatePeerRequests(peerId);
        for (const auto &request : created)
            dispatch(peerId, request);
    }
    transport_.pumpResults();
}

void SchedulerTransportBridge::dispatch(
    const std::string &peerId,
    const scheduler::OutstandingRequest &request)
{
    active_[request.id] = ActiveRequest{peerId, request.streamPiece,
                                        request.wire, false};
    transport_.requestBlock(peerId, request.wire,
        [this, requestId = request.id](std::error_code error,
                                       std::vector<std::byte> bytes) mutable {
            auto it = active_.find(requestId);
            if (it == active_.end())
                return;
            const bool cancelled = it->second.cancelled;
            active_.erase(it);
            if (cancelled)
                return;
            if (error) {
                scheduler_.failRequest(requestId);
                return;
            }
            auto completed = scheduler_.completeRequest(requestId, bytes);
            if (completed && completedObserver_)
                completedObserver_(std::move(*completed));
        });
}

void SchedulerTransportBridge::onHotswap(
    const std::string &fromPeer,
    const std::string &,
    std::size_t streamPiece)
{
    for (auto &[id, request] : active_) {
        if (request.peerId != fromPeer || request.streamPiece != streamPiece
            || request.cancelled) {
            continue;
        }
        request.cancelled = true;
        transport_.cancelBlock(request.peerId, request.wire);
        scheduler_.failRequest(id);
    }
}

} // namespace colosseum::server::integration
