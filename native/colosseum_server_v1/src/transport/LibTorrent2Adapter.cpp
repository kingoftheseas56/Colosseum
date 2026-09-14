#include "server1/ports/TorrentTransport.h"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/piece_block.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lt = libtorrent;

namespace server1::transport {
bool sameOwnership(const ports::RequestOwnership &, const ports::RequestOwnership &) noexcept;
bool sameBlock(const ports::BlockSpan &, const ports::BlockSpan &) noexcept;
using IdentifyPeer = std::function<ports::PeerHandle(const lt::tcp::endpoint &)>;
using PeerInbound = std::function<void(ports::PeerHandle,
    std::shared_ptr<lt::peer_connection>, const lt::bitfield *)>;
using DetachPeer = std::function<void(ports::PeerHandle,
    std::shared_ptr<lt::peer_connection>, const std::string &)>;
using AuthorizeRequest = std::function<bool(ports::PeerHandle, const lt::peer_request &)>;
using SentRequest = std::function<void(ports::PeerHandle, const lt::peer_request &)>;
using ReceivePiece = std::function<void(ports::PeerHandle, const lt::peer_request &, lt::span<const char>)>;
using PeerState = std::function<void(ports::PeerHandle, bool, bool,
    std::shared_ptr<lt::peer_connection>)>;
std::shared_ptr<lt::torrent_plugin> makeProductionPeerPlugin(
    IdentifyPeer, PeerInbound, DetachPeer, AuthorizeRequest, SentRequest, ReceivePiece, PeerState);

namespace {
using namespace ports;

struct OwnedRequest {
    RequestAction action;
    bool queuedToNative = false;
    bool framed = false;
    bool terminal = false;
};

bool matches(const BlockSpan &block, const lt::peer_request &request)
{
    return request.piece == lt::piece_index_t(static_cast<int>(block.piece))
        && request.start == static_cast<int>(block.offset)
        && request.length == static_cast<int>(block.length);
}

std::string endpointKey(const lt::tcp::endpoint &endpoint)
{ return endpoint.address().to_string() + ":" + std::to_string(endpoint.port()); }

class LibTorrent2Adapter final : public TorrentTransport {
public:
    LibTorrent2Adapter(const std::string &torrentPath, const std::string &savePath)
    {
        lt::settings_pack settings;
        settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
        settings.set_bool(lt::settings_pack::enable_dht, false);
        settings.set_bool(lt::settings_pack::enable_lsd, false);
        settings.set_bool(lt::settings_pack::enable_upnp, false);
        settings.set_bool(lt::settings_pack::enable_natpmp, false);
        settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
        settings.set_bool(lt::settings_pack::close_redundant_connections, false);
        settings.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_disabled);
        settings.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_disabled);
        session_ = std::make_unique<lt::session>(settings);
        session_->add_extension([this](const lt::torrent_handle &, lt::client_data_t) {
            return makeProductionPeerPlugin(
                [this](const auto &ep) { return identify(ep); },
                [this](PeerHandle p, auto n, const auto *b) { inbound(p, std::move(n), b); },
                [this](PeerHandle p, auto n, const auto &e) { detach(p, std::move(n), e); },
                [this](PeerHandle p, const auto &r) { return authorize(p, r); },
                [this](PeerHandle p, const auto &r) { framed(p, r); },
                [this](PeerHandle p, const auto &r, auto bytes) { receive(p, r, bytes); },
                [this](PeerHandle p, bool c, bool i, auto n) { observePeer(p, c, i, std::move(n)); });
        });

        lt::add_torrent_params params;
        params.ti = std::make_shared<lt::torrent_info>(torrentPath);
        pieceCount_ = static_cast<std::uint32_t>(params.ti->num_pieces());
        std::filesystem::create_directories(savePath);
        params.save_path = savePath;
        params.flags |= lt::torrent_flags::paused;
        params.flags &= ~lt::torrent_flags::auto_managed;
        lt::error_code error;
        torrent_ = session_->add_torrent(std::move(params), error);
        if (error) throw std::runtime_error("libtorrent add_torrent: " + error.message());
    }

    ~LibTorrent2Adapter() override { close(); }

    bool submit(const TorrentAction &action) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || !controlled_) return false;
        if (const auto *request = std::get_if<RequestAction>(&action)) {
            if (!isValidBlock(request->block) || request->block.piece >= pieceCount_
                || request->peer == 0 || request->ownership.requestId == 0) return false;
            for (const auto &owned : active_)
                if (sameOwnership(owned.action.ownership, request->ownership)) return false;
            active_.push_back({*request});
            pendingRequests_.push_back(*request);
            ++stats_.ownedRequestsOutstanding;
            return true;
        }
        if (const auto *cancel = std::get_if<CancelAction>(&action)) {
            auto found = findActive(cancel->ownership, cancel->peer, cancel->block);
            if (found == active_.end()) return false;
            found->terminal = true;
            retired_.push_back(found->action);
            pendingRequests_.erase(std::remove_if(pendingRequests_.begin(), pendingRequests_.end(),
                [&](const auto &r) { return sameOwnership(r.ownership, cancel->ownership); }), pendingRequests_.end());
            permits_.erase(std::remove_if(permits_.begin(), permits_.end(),
                [&](const auto &r) { return sameOwnership(r.ownership, cancel->ownership); }), permits_.end());
            pendingControl_[cancel->peer].push_back(action);
            if (stats_.ownedRequestsOutstanding > 0) --stats_.ownedRequestsOutstanding;
            return true;
        }
        const auto peer = std::visit([](const auto &value) { return value.peer; }, action);
        if (peer == 0) return false;
        pendingControl_[peer].push_back(action);
        return true;
    }

    std::vector<TorrentObservation> poll() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TorrentObservation> result;
        while (!observations_.empty()) {
            result.push_back(std::move(observations_.front())); observations_.pop_front();
        }
        return result;
    }

    TransportStatistics statistics() const override
    { std::lock_guard<std::mutex> lock(mutex_); return stats_; }

    void close() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            closed_ = true;
            for (auto &owned : active_) if (!owned.terminal) {
                owned.terminal = true;
                observations_.push_back(FailureObservation{owned.action.ownership, owned.action.peer,
                    owned.action.block, "transport closed", true});
            }
            stats_.ownedRequestsOutstanding = 0;
            pendingRequests_.clear(); pendingControl_.clear(); permits_.clear();
        }
        if (session_) {
            if (torrent_.is_valid()) session_->remove_torrent(torrent_);
            session_.reset();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        peers_.clear(); advertised_.clear();
        stats_.connectedPeers = 0; stats_.unchokedPeers = 0;
        if (!closedObserved_) { observations_.push_back(ClosedObservation{}); closedObserved_ = true; }
    }

    bool connect(PeerHandle peer, const std::string &address, std::uint16_t port)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || !controlled_ || peer == 0 || !torrent_.is_valid()) return false;
        lt::error_code error;
        const auto parsed = lt::make_address(address, error);
        if (error) return false;
        endpointPeers_[endpointKey({parsed, port})] = peer;
        torrent_.connect_peer({parsed, port});
        return true;
    }

    bool crossThreadGuard() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++forbiddenAttempts_;
        return nativeThread_ == std::thread::id{} || nativeThread_ != std::this_thread::get_id();
    }

    std::uint64_t framedCount() const { std::lock_guard<std::mutex> lock(mutex_); return framedCount_; }
    std::uint64_t ownedAddCount() const { std::lock_guard<std::mutex> lock(mutex_); return ownedAddCount_; }
    std::uint64_t autonomousMutationCount() const { std::lock_guard<std::mutex> lock(mutex_); return autonomousNativeMutations_; }
    std::uint64_t forbiddenAttemptCount() const { std::lock_guard<std::mutex> lock(mutex_); return forbiddenAttempts_; }
    std::uint64_t staleDisconnectCount() const { std::lock_guard<std::mutex> lock(mutex_); return staleDisconnectsIgnored_; }

    bool armReceiveBarrier(std::string entered, std::string release)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || entered.empty() || release.empty()) return false;
        receiveBarrierEntered_ = std::move(entered);
        receiveBarrierRelease_ = std::move(release);
        return true;
    }

    bool closeStarted() const
    { std::lock_guard<std::mutex> lock(mutex_); return closed_; }

    PeerHandle endpointOwner(const std::string &address, std::uint16_t port) const
    {
        lt::error_code error;
        const auto parsed = lt::make_address(address, error);
        if (error) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = endpointPeers_.find(endpointKey({parsed, port}));
        return found == endpointPeers_.end() ? 0 : found->second;
    }

protected:
    bool applyAutonomySuppression() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || controlled_) return !closed_;
        controlled_ = true;
        std::vector<lt::download_priority_t> priorities(pieceCount_, lt::dont_download);
        torrent_.prioritize_pieces(priorities);
        torrent_.resume();
        return true;
    }

private:
    PeerHandle identify(const lt::tcp::endpoint &endpoint)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = endpointPeers_.find(endpointKey(endpoint));
        return found == endpointPeers_.end() ? 0 : found->second;
    }

    void prepareNative(PeerHandle peer, const std::shared_ptr<lt::peer_connection> &native)
    {
        if (!native) return;
        nativeThread_ = std::this_thread::get_id();
        auto torrent = native->associated_torrent().lock();
        if (torrent) {
            for (auto piece = lt::piece_index_t(0); piece < torrent->torrent_file().end_piece(); ++piece)
                torrent->set_piece_priority(piece, lt::dont_download);
        }
        // Every connection object is scrubbed independently. A peer handle may
        // be rebound to a replacement connection for the same endpoint.
        native->clear_request_queue();
        native->clear_download_queue();
    }

    void inbound(PeerHandle peer, std::shared_ptr<lt::peer_connection> native,
                 const lt::bitfield *pieces)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            prepareNative(peer, native);
            peers_[peer] = native;
            stats_.connectedPeers = static_cast<std::uint32_t>(peers_.size());
            if (pieces) {
                auto &available = advertised_[peer];
                for (int index = 0; index < pieces->size(); ++index)
                    if ((*pieces)[index]) available.insert(static_cast<std::uint32_t>(index));
            }
        }
        if (pieces) {
            dispatchControl(peer, native);
            issueReady(peer);
        }
    }

    void issueReady(PeerHandle)
    {
        RequestAction request;
        std::shared_ptr<lt::peer_connection> selected;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || advertised_.size() < 2 || pendingRequests_.empty()) return;
            const auto found = std::find_if(pendingRequests_.begin(), pendingRequests_.end(), [&](const auto &r) {
                const auto available = advertised_.find(r.peer);
                return available != advertised_.end()
                    && available->second.count(r.block.piece) != 0 && findActive(r) != active_.end();
            });
            if (found == pendingRequests_.end()) return;
            const auto peer = peers_.find(found->peer);
            if (peer == peers_.end()) return;
            selected = peer->second.lock();
            if (!selected) return;
            if (selected->is_connecting() || selected->is_disconnecting() || selected->has_peer_choked()) return;
            request = *found;
            if (!selected->request_queue().empty() || !selected->download_queue().empty()) {
                ++autonomousNativeMutations_;
                observations_.push_back(FailureObservation{{}, request.peer, request.block,
                    "autonomous native queue mutation before owned request", false});
                return;
            }
            permits_.push_back(request); // ownership exists before native queue mutation
        }

        bool queued = false;
        {
            lt::cork receiveCork(*selected);
            queued = selected->add_request({lt::piece_index_t(static_cast<int>(request.block.piece)),
                                            static_cast<int>(request.block.blockOrdinal)});
            std::cerr << "K10 OWNED_ADD peer=" << request.peer << " request="
                      << request.ownership.requestId << '/' << request.ownership.generation
                      << '/' << request.ownership.selectionId << " piece=" << request.block.piece
                      << " offset=" << request.block.offset << " length=" << request.block.length
                      << " queued=" << (queued ? 1 : 0) << '\n';
            if (queued) selected->send_block_requests_impl();
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto active = findActive(request);
        if (queued) ++ownedAddCount_;
        if (!queued || active == active_.end() || !active->framed) {
            if (!queued && active != active_.end()) {
                active->terminal = true;
                if (stats_.ownedRequestsOutstanding > 0) --stats_.ownedRequestsOutstanding;
                observations_.push_back(FailureObservation{request.ownership, request.peer,
                    request.block, "locked libtorrent add_request returned false", true});
            }
            permits_.erase(std::remove_if(permits_.begin(), permits_.end(), [&](const auto &r) {
                return sameOwnership(r.ownership, request.ownership); }), permits_.end());
            return;
        }
        active->queuedToNative = true;
        pendingRequests_.erase(std::remove_if(pendingRequests_.begin(), pendingRequests_.end(),
            [&](const auto &r) { return sameOwnership(r.ownership, request.ownership); }), pendingRequests_.end());
    }

    void dispatchControl(PeerHandle peer, std::shared_ptr<lt::peer_connection> native)
    {
        std::deque<TorrentAction> actions;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            actions.swap(pendingControl_[peer]);
        }
        for (const auto &action : actions) {
            if (const auto *cancel = std::get_if<CancelAction>(&action)) {
                if (cancel->requestWireCancel)
                    native->cancel_request({lt::piece_index_t(static_cast<int>(cancel->block.piece)),
                                            static_cast<int>(cancel->block.blockOrdinal)}, true);
            } else if (const auto *interest = std::get_if<InterestAction>(&action)) {
                if (interest->interested) native->send_interested(); else native->send_not_interested();
            } else if (const auto *choke = std::get_if<ChokeAction>(&action)) {
                if (choke->choked) native->send_choke(); else native->send_unchoke();
            }
        }
    }

    bool authorize(PeerHandle peer, const lt::peer_request &wire)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nativeThread_ = std::this_thread::get_id();
        const auto found = std::find_if(permits_.begin(), permits_.end(), [&](const auto &r) {
            return r.peer == peer && matches(r.block, wire) && findActive(r) != active_.end();
        });
        if (found == permits_.end()) {
            ++stats_.pickerRequestsSuppressed;
            observations_.push_back(FailureObservation{{}, peer,
                {static_cast<std::uint32_t>(static_cast<int>(wire.piece)),
                 static_cast<std::uint32_t>(wire.start / static_cast<int>(kWireBlockLength)),
                 static_cast<std::uint32_t>(wire.start), static_cast<std::uint32_t>(wire.length)},
                "unowned request reached write firewall", false});
            return false;
        }
        framedPending_.push_back(*found);
        permits_.erase(found); // exact one-shot permit
        return true;
    }

    void framed(PeerHandle peer, const lt::peer_request &wire)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = std::find_if(framedPending_.begin(), framedPending_.end(),
            [&](const auto &r) { return r.peer == peer && matches(r.block, wire); });
        if (found == framedPending_.end()) return;
        auto active = findActive(*found);
        if (active != active_.end()) { active->framed = true; ++framedCount_; }
        framedPending_.erase(found);
    }

    void receive(PeerHandle peer, const lt::peer_request &wire, lt::span<const char> payload)
    {
        std::string entered;
        std::string release;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entered = receiveBarrierEntered_;
            release = receiveBarrierRelease_;
        }
        if (!entered.empty()) {
            std::ofstream(entered, std::ios::trunc).close();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!std::filesystem::exists(release) && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        const auto active = std::find_if(active_.begin(), active_.end(), [&](const auto &owned) {
            return !owned.terminal && owned.action.peer == peer && matches(owned.action.block, wire);
        });
        if (active != active_.end()) {
            active->terminal = true;
            retired_.push_back(active->action);
            if (stats_.ownedRequestsOutstanding > 0) --stats_.ownedRequestsOutstanding;
            stats_.downloadedBytes += static_cast<std::uint64_t>(payload.size());
            std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
            observations_.push_back(BlockObservation{active->action.ownership, peer, active->action.block,
                                                     std::move(bytes), false, false});
            return;
        }
        const auto old = std::find_if(retired_.rbegin(), retired_.rend(), [&](const auto &r) {
            return r.peer == peer && matches(r.block, wire);
        });
        if (old != retired_.rend()) {
            std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
            observations_.push_back(BlockObservation{old->ownership, peer, old->block,
                                                     std::move(bytes), true, true});
        }
    }

    void detach(PeerHandle peer, const std::shared_ptr<lt::peer_connection> &native,
                const std::string &error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto current = peers_.find(peer);
        if (current == peers_.end()) return;
        const auto attached = current->second.lock();
        if (native && attached && native.get() != attached.get()) {
            ++staleDisconnectsIgnored_;
            return;
        }
        peers_.erase(peer); advertised_.erase(peer);
        stats_.connectedPeers = static_cast<std::uint32_t>(peers_.size());
        for (auto &owned : active_) if (!owned.terminal && owned.action.peer == peer) {
            owned.terminal = true; retired_.push_back(owned.action);
            if (stats_.ownedRequestsOutstanding > 0) --stats_.ownedRequestsOutstanding;
            observations_.push_back(FailureObservation{owned.action.ownership, peer,
                owned.action.block, "peer disconnected: " + error, true});
        }
    }

    void observePeer(PeerHandle peer, bool choking, bool interested,
                     std::shared_ptr<lt::peer_connection> native)
    {
        lt::peer_info info;
        if (native) native->get_peer_info(info);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            if (choking) unchoked_.erase(peer); else unchoked_.insert(peer);
            stats_.unchokedPeers = static_cast<std::uint32_t>(unchoked_.size());
            std::uint32_t outstanding = 0;
            for (const auto &owned : active_) if (!owned.terminal && owned.action.peer == peer) ++outstanding;
            observations_.push_back(PeerObservation{peer, choking, interested,
                static_cast<double>(info.payload_down_speed), static_cast<double>(info.payload_up_speed),
                outstanding, static_cast<std::uint64_t>(std::max<std::int64_t>(0, info.total_download))});
            stats_.downloadBytesPerSecond = static_cast<double>(info.payload_down_speed);
            stats_.uploadBytesPerSecond = static_cast<double>(info.payload_up_speed);
            stats_.uploadedBytes = static_cast<std::uint64_t>(std::max<std::int64_t>(0, info.total_upload));
        }
        if (!choking) issueReady(peer);
    }

    std::vector<OwnedRequest>::iterator findActive(const RequestAction &request)
    { return findActive(request.ownership, request.peer, request.block); }
    std::vector<OwnedRequest>::iterator findActive(const RequestOwnership &owner,
        PeerHandle peer, const BlockSpan &block)
    {
        return std::find_if(active_.begin(), active_.end(), [&](const auto &owned) {
            return !owned.terminal && owned.action.peer == peer
                && sameOwnership(owned.action.ownership, owner) && sameBlock(owned.action.block, block);
        });
    }

    mutable std::mutex mutex_;
    std::unique_ptr<lt::session> session_;
    lt::torrent_handle torrent_;
    std::uint32_t pieceCount_ = 0;
    bool controlled_ = false, closed_ = false, closedObserved_ = false;
    mutable std::uint64_t forbiddenAttempts_ = 0;
    std::uint64_t autonomousNativeMutations_ = 0, ownedAddCount_ = 0;
    std::uint64_t framedCount_ = 0, staleDisconnectsIgnored_ = 0;
    std::thread::id nativeThread_{};
    std::map<std::string, PeerHandle> endpointPeers_;
    std::map<PeerHandle, std::weak_ptr<lt::peer_connection>> peers_;
    std::map<PeerHandle, std::set<std::uint32_t>> advertised_;
    std::set<PeerHandle> unchoked_;
    std::deque<RequestAction> pendingRequests_;
    std::map<PeerHandle, std::deque<TorrentAction>> pendingControl_;
    std::vector<RequestAction> permits_, framedPending_, retired_;
    std::vector<OwnedRequest> active_;
    std::deque<TorrentObservation> observations_;
    TransportStatistics stats_{};
    std::string receiveBarrierEntered_, receiveBarrierRelease_;
};
}

std::unique_ptr<ports::TorrentTransport> makeLibTorrent2Adapter(
    const std::string &torrentPath, const std::string &savePath)
{
    try { return std::make_unique<LibTorrent2Adapter>(torrentPath, savePath); }
    catch (...) { return {}; }
}

bool connectPeer(ports::TorrentTransport &transport, ports::PeerHandle peer,
                 const std::string &address, std::uint16_t port)
{
    auto *adapter = dynamic_cast<LibTorrent2Adapter *>(&transport);
    return adapter && adapter->connect(peer, address, port);
}

bool forbiddenCrossThreadNativeAccessIsRejected(ports::TorrentTransport &transport)
{
    auto *adapter = dynamic_cast<LibTorrent2Adapter *>(&transport);
    return adapter && adapter->crossThreadGuard();
}

std::uint64_t framedRequestCount(const ports::TorrentTransport &transport)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter ? adapter->framedCount() : 0;
}

std::uint64_t ownedNativeAddCount(const ports::TorrentTransport &transport)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter ? adapter->ownedAddCount() : 0;
}

std::uint64_t autonomousNativeMutationCount(const ports::TorrentTransport &transport)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter ? adapter->autonomousMutationCount() : 0;
}

std::uint64_t forbiddenNativeAttemptCount(const ports::TorrentTransport &transport)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter ? adapter->forbiddenAttemptCount() : 0;
}

std::uint64_t staleDisconnectIgnoredCount(const ports::TorrentTransport &transport)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter ? adapter->staleDisconnectCount() : 0;
}

bool armReceiveCallbackBarrier(ports::TorrentTransport &transport,
                               const std::string &entered,
                               const std::string &release)
{
    auto *adapter = dynamic_cast<LibTorrent2Adapter *>(&transport);
    return adapter && adapter->armReceiveBarrier(entered, release);
}

bool closeHasStarted(const ports::TorrentTransport &transport)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter && adapter->closeStarted();
}

ports::PeerHandle boundEndpointOwner(const ports::TorrentTransport &transport,
                                     const std::string &address, std::uint16_t port)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter ? adapter->endpointOwner(address, port) : 0;
}
} // namespace server1::transport
