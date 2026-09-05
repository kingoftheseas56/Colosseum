#pragma once

#include "SchedulerTransportBridge.h"

#include <cstdint>
#include <memory>
#include <string>

#include <libtorrent/socket.hpp>
#include <libtorrent/torrent_handle.hpp>

namespace colosseum::server::integration {

// Peer/block transport for the exact libtorrent torrent represented by
// `torrent`. SchedulerTransportBridge remains the owner of request policy;
// this class only turns its wire decisions into libtorrent peer requests and
// returns copied block bytes through pumpResults().
class LibtorrentBlockTransport final : public IBlockTransport
{
public:
    explicit LibtorrentBlockTransport(lt::torrent_handle torrent);
    ~LibtorrentBlockTransport() override;

    LibtorrentBlockTransport(const LibtorrentBlockTransport &) = delete;
    LibtorrentBlockTransport &operator=(const LibtorrentBlockTransport &) = delete;

    void requestBlock(const std::string &peerHint,
                      const scheduler::WireBlock &block,
                      Completion completion) override;
    void cancelBlock(const std::string &peerHint,
                     const scheduler::WireBlock &block) override;
    void pumpResults() override;

    [[nodiscard]] std::shared_ptr<SchedulerTransportMetrics> metrics() const noexcept;
    [[nodiscard]] SchedulerTransportMetricsSnapshot metricsSnapshot() const noexcept;

    // The scheduler uses this same stable identity when it projects
    // libtorrent peer snapshots into PeerState.
    [[nodiscard]] static std::string peerIdentity(const lt::tcp::endpoint &endpoint);

private:
    struct State;
    struct TorrentPlugin;
    struct PeerPlugin;

    void enqueueCommand();

    lt::torrent_handle torrent_;
    std::shared_ptr<State> state_;
};

} // namespace colosseum::server::integration
