#include "server1/ports/TorrentTransport.h"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/magnet_uri.hpp>
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
#include <type_traits>
#include <utility>
#include <vector>

namespace lt = libtorrent;

namespace server1::transport {
bool sameOwnership(const ports::RequestOwnership &, const ports::RequestOwnership &) noexcept;
bool sameBlock(const ports::BlockSpan &, const ports::BlockSpan &) noexcept;
using ConnectionIdentity = std::uint64_t;
using IdentifyPeer = std::function<ports::PeerHandle(const lt::tcp::endpoint &)>;
using PeerInbound = std::function<void(ports::PeerHandle,
    std::shared_ptr<lt::peer_connection>, ConnectionIdentity, const lt::bitfield *)>;
using DetachPeer = std::function<void(ports::PeerHandle,
    std::shared_ptr<lt::peer_connection>, ConnectionIdentity, const std::string &)>;
using AuthorizeRequest = std::function<bool(ports::PeerHandle, ConnectionIdentity,
    const lt::peer_request &)>;
using SentRequest = std::function<void(ports::PeerHandle, ConnectionIdentity,
    const lt::peer_request &)>;
using ReceivePiece = std::function<void(ports::PeerHandle, ConnectionIdentity,
    const lt::peer_request &, lt::span<const char>)>;
using PeerState = std::function<void(ports::PeerHandle, ConnectionIdentity, bool, bool,
    std::shared_ptr<lt::peer_connection>)>;
using PeerHave = std::function<void(ports::PeerHandle, ConnectionIdentity, lt::piece_index_t)>;
using UploadRequest = std::function<void(ports::PeerHandle, ConnectionIdentity,
    const lt::peer_request &)>;
using UploadCancel = std::function<void(ports::PeerHandle, ConnectionIdentity,
    const lt::peer_request &)>;
using NetworkTick = std::function<void()>;
std::shared_ptr<lt::torrent_plugin> makeProductionPeerPlugin(
    IdentifyPeer, PeerInbound, DetachPeer, AuthorizeRequest, SentRequest, ReceivePiece, PeerState,
    PeerHave, UploadRequest, UploadCancel, NetworkTick);

namespace {
using namespace ports;

V1InfoHash v1InfoHash(const lt::sha1_hash &hash)
{
    V1InfoHash result{};
    std::copy_n(reinterpret_cast<const std::uint8_t *>(hash.data()), result.size(), result.begin());
    return result;
}

V1InfoHash v1InfoHash(const lt::torrent_info &info)
{
    return v1InfoHash(info.info_hashes().v1);
}

std::string infoHashHex(const V1InfoHash &hash)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(hash.size() * 2);
    for (const auto byte : hash) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

lt::add_torrent_params sourceParams(const TorrentOpenRequest &request)
{
    if (request.generation == 0) throw std::invalid_argument("engine generation must be non-zero");
    if (request.savePath.empty()) throw std::invalid_argument("save path must not be empty");

    lt::error_code error;
    lt::add_torrent_params params;
    if (std::holds_alternative<InfoHashSource>(request.source)) {
        params = lt::parse_magnet_uri("magnet:?xt=urn:btih:" + infoHashHex(request.infoHash), error);
    } else if (const auto *magnet = std::get_if<MagnetSource>(&request.source)) {
        if (magnet->uri.empty()) throw std::invalid_argument("magnet URI must not be empty");
        params = lt::parse_magnet_uri(magnet->uri, error);
    } else {
        const auto &bytes = std::get<MetainfoSource>(request.source).bytes;
        if (bytes.empty()) throw std::invalid_argument("metainfo bytes must not be empty");
        params.ti = std::make_shared<lt::torrent_info>(
            reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()), error);
    }
    if (error) throw std::invalid_argument("invalid torrent source: " + error.message());

    V1InfoHash actual{};
    if (params.ti) actual = v1InfoHash(*params.ti);
    else actual = v1InfoHash(params.info_hashes.v1);
    if (actual != request.infoHash)
        throw std::invalid_argument("torrent source v1 infohash mismatch");

    params.save_path = request.savePath;
    params.flags |= lt::torrent_flags::paused;
    params.flags &= ~lt::torrent_flags::auto_managed;
    return params;
}

class FailedTorrentTransport final : public TorrentTransport {
public:
    FailedTorrentTransport(EngineGeneration generation, V1InfoHash infoHash, std::string error)
    {
        observations_.push_back(SourceFailureObservation{
            generation, std::move(infoHash), std::move(error), false});
    }

    bool submit(const TorrentAction &) override { return false; }
    std::vector<TorrentObservation> poll() override
    {
        std::vector<TorrentObservation> result;
        result.swap(observations_);
        return result;
    }
    TransportStatistics statistics() const override { return {}; }
    void close() override
    {
        if (closed_) return;
        observations_.clear();
        observations_.push_back(ClosedObservation{});
        closed_ = true;
    }

protected:
    bool applyAutonomySuppression() override { return false; }

private:
    std::vector<TorrentObservation> observations_;
    bool closed_ = false;
};

struct OwnedRequest {
    RequestAction action;
    bool queuedToNative = false;
    bool framed = false;
    bool terminal = false;
};

struct OwnedUpload {
    UploadOwnership ownership;
    PeerHandle peer = 0;
    ConnectionIdentity identity = 0;
    BlockSpan block;
    bool mailboxPending = false;
};

using UploadMailboxAction = std::variant<UploadResponseAction, UploadAbortAction>;

bool matches(const BlockSpan &block, const lt::peer_request &request)
{
    return request.piece == lt::piece_index_t(static_cast<int>(block.piece))
        && request.start == static_cast<int>(block.offset)
        && request.length == static_cast<int>(block.length);
}

std::string endpointKey(const lt::tcp::endpoint &endpoint)
{ return endpoint.address().to_string() + ":" + std::to_string(endpoint.port()); }

void appendBigEndian(std::vector<char> &bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<char>((value >> 24) & 0xff));
    bytes.push_back(static_cast<char>((value >> 16) & 0xff));
    bytes.push_back(static_cast<char>((value >> 8) & 0xff));
    bytes.push_back(static_cast<char>(value & 0xff));
}

class LibTorrent2Adapter final : public TorrentTransport {
public:
    explicit LibTorrent2Adapter(const TorrentOpenRequest &request)
        : generation_(request.generation), canonicalInfoHash_(request.infoHash)
    {
        auto params = sourceParams(request);
        if (params.ti) setPieceSizes(*params.ti);
        lt::settings_pack settings;
        settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
        settings.set_bool(lt::settings_pack::enable_dht, false);
        settings.set_bool(lt::settings_pack::enable_lsd, false);
        settings.set_bool(lt::settings_pack::enable_upnp, false);
        settings.set_bool(lt::settings_pack::enable_natpmp, false);
        settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
        settings.set_bool(lt::settings_pack::close_redundant_connections, false);
        settings.set_int(lt::settings_pack::min_reconnect_time, 0);
        settings.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_disabled);
        settings.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_disabled);
        settings.set_int(lt::settings_pack::alert_mask,
                         static_cast<int>(lt::alert_category::error | lt::alert_category::status));
        session_ = std::make_unique<lt::session>(settings);
        session_->add_extension([this](const lt::torrent_handle &, lt::client_data_t) {
            return makeProductionPeerPlugin(
                [this](const auto &ep) { return identify(ep); },
                [this](PeerHandle p, auto n, ConnectionIdentity id, const auto *b) {
                    inbound(p, std::move(n), id, b);
                },
                [this](PeerHandle p, auto n, ConnectionIdentity id, const auto &e) {
                    detach(p, std::move(n), id, e);
                },
                [this](PeerHandle p, ConnectionIdentity id, const auto &r) {
                    return authorize(p, id, r);
                },
                [this](PeerHandle p, ConnectionIdentity id, const auto &r) { framed(p, id, r); },
                [this](PeerHandle p, ConnectionIdentity id, const auto &r, auto bytes) {
                    receive(p, id, r, bytes);
                },
                [this](PeerHandle p, ConnectionIdentity id, bool c, bool i, auto n) {
                    observePeer(p, id, c, i, std::move(n));
                },
                [this](PeerHandle p, ConnectionIdentity id, auto piece) { have(p, id, piece); },
                [this](PeerHandle p, ConnectionIdentity id, const auto &r) {
                    uploadRequest(p, id, r);
                },
                [this](PeerHandle p, ConnectionIdentity id, const auto &r) {
                    uploadCancel(p, id, r);
                },
                [this] { tickDrain(); });
        });

        std::filesystem::create_directories(request.savePath);
        session_->async_add_torrent(std::move(params));
    }

    ~LibTorrent2Adapter() override { close(); }

    bool submit(const TorrentAction &action) override
    {
        collectSourceAlerts();
        if (const auto *pauseAction = std::get_if<PauseAction>(&action))
            return setPaused(*pauseAction);
        if (const auto *connectAction = std::get_if<ConnectAction>(&action))
            return connect(*connectAction);
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || !controlled_) {
            if (std::holds_alternative<AdvertisePieceAction>(action)
                || std::holds_alternative<UploadResponseAction>(action)
                || std::holds_alternative<UploadAbortAction>(action))
                ++stats_.uploadActionsRejected;
            return false;
        }
        if (const auto *advertise = std::get_if<AdvertisePieceAction>(&action)) {
            if (advertise->generation == 0 || advertise->generation != generation_
                || !metadataReadyObserved_ || advertise->piece >= pieceCount_) {
                ++stats_.uploadActionsRejected;
                return false;
            }
            if (localAdvertisedPieces_.insert(advertise->piece).second) requestDrainLocked();
            return true;
        }
        if (const auto *response = std::get_if<UploadResponseAction>(&action)) {
            auto found = findUpload(response->ownership, response->peer, response->block);
            if (response->ownership.requestId == 0 || response->ownership.generation != generation_
                || response->peer == 0 || !isValidBlock(response->block)
                || response->payload.size() != response->block.length
                || found == activeUploads_.end() || found->mailboxPending) {
                ++stats_.uploadActionsRejected;
                return false;
            }
            found->mailboxPending = true;
            pendingUploadActions_.push_back(*response);
            requestDrainLocked();
            return true;
        }
        if (const auto *abort = std::get_if<UploadAbortAction>(&action)) {
            auto found = findUpload(abort->ownership, abort->peer, abort->block);
            if (abort->ownership.requestId == 0 || abort->ownership.generation != generation_
                || abort->peer == 0 || !isValidBlock(abort->block)
                || found == activeUploads_.end() || found->mailboxPending) {
                ++stats_.uploadActionsRejected;
                return false;
            }
            found->mailboxPending = true;
            pendingUploadActions_.push_back(*abort);
            requestDrainLocked();
            return true;
        }
        if (const auto *request = std::get_if<RequestAction>(&action)) {
            if (!isValidBlock(request->block) || request->block.piece >= pieceCount_
                || request->peer == 0 || request->ownership.requestId == 0
                || request->ownership.generation == 0
                || request->ownership.generation != generation_) return false;
            for (const auto &owned : active_)
                if (sameOwnership(owned.action.ownership, request->ownership)) return false;
            active_.push_back({*request});
            pendingRequests_.push_back(*request);
            ++stats_.ownedRequestsOutstanding;
            requestDrainLocked();
            return true;
        }
        if (const auto *cancel = std::get_if<CancelAction>(&action)) {
            if (cancel->ownership.generation == 0
                || cancel->ownership.generation != generation_) return false;
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
            requestDrainLocked();
            return true;
        }
        if (const auto *choke = std::get_if<ChokeAction>(&action)) {
            if (choke->peer == 0) return false;
            if (choke->choked) {
                locallyUnchoked_.erase(choke->peer);
                cancelUploadsLocked(choke->peer, 0);
            } else {
                locallyUnchoked_.insert(choke->peer);
            }
        }
        const auto peer = std::visit([](const auto &value) -> PeerHandle {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, PauseAction>
                || std::is_same_v<Value, AdvertisePieceAction>) return 0;
            else return value.peer;
        }, action);
        if (peer == 0) return false;
        pendingControl_[peer].push_back(action);
        requestDrainLocked();
        return true;
    }

    std::vector<TorrentObservation> poll() override
    {
        collectSourceAlerts();
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TorrentObservation> result;
        while (!observations_.empty()) {
            result.push_back(std::move(observations_.front())); observations_.pop_front();
        }
        return result;
    }

    TransportStatistics statistics() const override
    {
        const_cast<LibTorrent2Adapter *>(this)->collectSourceAlerts();
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    void close() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            closed_ = true;
            drainRequested_ = false;
            for (auto &owned : active_) if (!owned.terminal) {
                owned.terminal = true;
                observations_.push_back(FailureObservation{owned.action.ownership, owned.action.peer,
                    owned.action.block, "transport closed", true});
            }
            cancelUploadsLocked(0, 0);
            stats_.uploadActionsRejected += pendingUploadActions_.size();
            stats_.ownedRequestsOutstanding = 0;
            stats_.ownedUploadsOutstanding = 0;
            pendingRequests_.clear(); pendingControl_.clear(); permits_.clear();
            pendingUploadActions_.clear();
            pendingConnects_.clear(); deferredConnects_.clear();
            observations_.erase(std::remove_if(observations_.begin(), observations_.end(),
                [](const auto &observation) {
                    return std::holds_alternative<MetadataReadyObservation>(observation)
                        || std::holds_alternative<SourceFailureObservation>(observation);
                }), observations_.end());
        }
        std::lock_guard<std::mutex> alertLock(alertMutex_);
        if (session_) {
            if (torrent_.is_valid()) session_->remove_torrent(torrent_);
            session_.reset();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        peers_.clear(); advertised_.clear(); unchoked_.clear(); endpointPeers_.clear();
        locallyUnchoked_.clear(); interestedPeers_.clear(); announcedLocalPieces_.clear();
        localAdvertisedPieces_.clear(); activeUploads_.clear();
        lastDetachedIdentity_.clear(); peerIdentities_.clear();
        stats_.connectedPeers = 0; stats_.unchokedPeers = 0;
        if (!closedObserved_) { observations_.push_back(ClosedObservation{}); closedObserved_ = true; }
    }

    bool connect(const ConnectAction &action)
    {
        lt::error_code error;
        const auto parsed = lt::make_address(action.address, error);
        if (error || action.port == 0) return false;
        const lt::tcp::endpoint endpoint{parsed, action.port};
        lt::torrent_handle handle;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || !controlled_ || action.generation != generation_ || action.peer == 0)
                return false;
            if (stats_.paused) {
                deferredConnects_.push_back(action);
                return true;
            }
            endpointPeers_[endpointKey(endpoint)] = action.peer;
            if (!torrent_.is_valid()) {
                pendingConnects_.push_back(action);
                return true;
            }
            handle = torrent_;
        }
        // A disconnect callback precedes libtorrent's final peer-list cleanup.
        // This exported synchronous handle call is an event-loop barrier; a
        // replacement connect posted afterward cannot race the old connection.
        try {
            (void)handle.status();
            handle.connect_peer(endpoint, lt::peer_info::tracker, lt::pex_flags_t{});
            return true;
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = endpointPeers_.find(endpointKey(endpoint));
            if (found != endpointPeers_.end() && found->second == action.peer) endpointPeers_.erase(found);
            return false;
        }
    }

    bool setPaused(const PauseAction &action)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || !controlled_ || action.generation == 0
            || action.generation != generation_) return false;
        if (stats_.paused == action.paused) return true;
        stats_.paused = action.paused;
        if (!action.paused) requestDrainLocked();
        return true;
    }

    bool crossThreadGuard()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (nativeThread_ == std::thread::id{}) return false;
            if (nativeThread_ != std::this_thread::get_id()) {
                ++forbiddenAttempts_;
                return true;
            }
            ++guardedNativeTouches_;
        }
        drainOnNetworkThread();
        return false;
    }

    std::uint64_t framedCount() const { std::lock_guard<std::mutex> lock(mutex_); return framedCount_; }
    std::uint64_t ownedAddCount() const { std::lock_guard<std::mutex> lock(mutex_); return ownedAddCount_; }
    std::uint64_t autonomousMutationCount() const { std::lock_guard<std::mutex> lock(mutex_); return autonomousNativeMutations_; }
    std::uint64_t forbiddenAttemptCount() const { std::lock_guard<std::mutex> lock(mutex_); return forbiddenAttempts_; }
    std::uint64_t guardedNativeTouchCount() const { std::lock_guard<std::mutex> lock(mutex_); return guardedNativeTouches_; }
    std::uint64_t staleDisconnectCount() const { std::lock_guard<std::mutex> lock(mutex_); return staleDisconnectsIgnored_; }
    std::uint64_t staleCallbackCount() const { std::lock_guard<std::mutex> lock(mutex_); return staleCallbacksIgnored_; }

    bool replayLastDetached(PeerHandle peer)
    {
        ConnectionIdentity old = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = lastDetachedIdentity_.find(peer);
            if (found == lastDetachedIdentity_.end() || found->second == 0) return false;
            old = found->second;
        }
        const auto before = staleDisconnectCount();
        detach(peer, {}, old, "replayed stale disconnect");
        return staleDisconnectCount() == before + 1;
    }

    bool replayLastDetachedCallbacks(PeerHandle peer, const BlockSpan &block)
    {
        ConnectionIdentity old = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = lastDetachedIdentity_.find(peer);
            if (found == lastDetachedIdentity_.end() || found->second == 0) return false;
            old = found->second;
        }
        const lt::peer_request wire{lt::piece_index_t(static_cast<int>(block.piece)),
                                    static_cast<int>(block.offset),
                                    static_cast<int>(block.length)};
        const auto before = staleCallbackCount();
        have(peer, old, wire.piece);
        observePeer(peer, old, false, true, {});
        const bool authorized = authorize(peer, old, wire);
        framed(peer, old, wire);
        std::vector<char> bytes(block.length, 'S');
        receive(peer, old, wire, bytes);
        return !authorized && staleCallbackCount() == before + 5;
    }

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
        lt::torrent_handle handle;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || controlled_) return !closed_;
            controlled_ = true;
            handle = torrent_;
        }
        return !handle.is_valid() || suppressAutonomy(handle);
    }

private:
    bool suppressAutonomy(const lt::torrent_handle &handle)
    {
        try {
            const auto info = handle.torrent_file();
            if (info) {
                std::vector<lt::download_priority_t> priorities(
                    static_cast<std::size_t>(info->num_pieces()), lt::dont_download);
                handle.prioritize_pieces(priorities);
            }
            handle.resume();
            return true;
        } catch (...) { return false; }
    }

    void collectSourceAlerts()
    {
        std::lock_guard<std::mutex> alertLock(alertMutex_);
        if (!session_) return;
        std::vector<lt::alert *> alerts;
        session_->pop_alerts(&alerts);
        for (const auto *alert : alerts) {
            if (const auto *added = lt::alert_cast<lt::add_torrent_alert>(alert)) {
                handleAdded(*added);
            } else if (const auto *metadata = lt::alert_cast<lt::metadata_received_alert>(alert)) {
                emitMetadataReady(metadata->handle, metadata->handle.torrent_file());
            }
        }
    }

    void handleAdded(const lt::add_torrent_alert &alert)
    {
        if (alert.error) {
            emitSourceFailure("libtorrent async_add_torrent: " + alert.error.message(), false);
            return;
        }
        bool controlled = false;
        std::vector<ConnectAction> connects;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            torrent_ = alert.handle;
            controlled = controlled_;
            if (stats_.paused) {
                deferredConnects_.insert(deferredConnects_.end(),
                                         pendingConnects_.begin(), pendingConnects_.end());
                pendingConnects_.clear();
            } else {
                connects.swap(pendingConnects_);
            }
        }
        if (controlled && !suppressAutonomy(alert.handle)) {
            emitSourceFailure("libtorrent autonomy suppression failed", false);
            return;
        }
        const auto info = alert.params.ti ? alert.params.ti : alert.handle.torrent_file();
        if (info) emitMetadataReady(alert.handle, info);
        for (const auto &connectAction : connects) (void)connect(connectAction);
    }

    void emitMetadataReady(const lt::torrent_handle &handle,
                           const std::shared_ptr<const lt::torrent_info> &info)
    {
        if (!info) return;
        const auto actualHash = v1InfoHash(*info);
        if (actualHash != canonicalInfoHash_) {
            emitSourceFailure("libtorrent metadata v1 infohash mismatch", false);
            return;
        }
        const auto section = info->info_section();
        std::vector<std::uint8_t> bytes(section.begin(), section.end());
        std::set<std::string> trackerSet;
        std::set<std::string> urlSeedSet;
        try {
            for (const auto &tracker : handle.trackers()) trackerSet.insert(tracker.url);
            const auto liveUrlSeeds = handle.url_seeds();
            urlSeedSet.insert(liveUrlSeeds.begin(), liveUrlSeeds.end());
        } catch (...) {
            // The alert handle can become invalid during shutdown. Immutable torrent-info
            // values remain a safe fallback for any source metadata it carries.
        }
        for (const auto &tracker : info->trackers()) trackerSet.insert(tracker.url);
        const auto infoUrlSeeds = info->url_seeds();
        urlSeedSet.insert(infoUrlSeeds.begin(), infoUrlSeeds.end());
        std::vector<std::string> trackers(trackerSet.begin(), trackerSet.end());
        std::vector<std::string> urlSeeds(urlSeedSet.begin(), urlSeedSet.end());
        std::vector<std::uint32_t> pieceSizes;
        pieceSizes.reserve(static_cast<std::size_t>(info->num_pieces()));
        for (auto piece = lt::piece_index_t{0}; piece < info->end_piece(); ++piece)
            pieceSizes.push_back(static_cast<std::uint32_t>(info->piece_size(piece)));
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || metadataReadyObserved_ || sourceFailed_) return;
        pieceCount_ = static_cast<std::uint32_t>(info->num_pieces());
        pieceSizes_ = std::move(pieceSizes);
        observations_.push_back(MetadataReadyObservation{
            generation_, canonicalInfoHash_, std::move(bytes),
            std::move(trackers), std::move(urlSeeds)});
        metadataReadyObserved_ = true;
        requestDrainLocked();
    }

    void setPieceSizes(const lt::torrent_info &info)
    {
        pieceCount_ = static_cast<std::uint32_t>(info.num_pieces());
        pieceSizes_.clear();
        pieceSizes_.reserve(pieceCount_);
        for (auto piece = lt::piece_index_t{0}; piece < info.end_piece(); ++piece)
            pieceSizes_.push_back(static_cast<std::uint32_t>(info.piece_size(piece)));
    }

    void emitSourceFailure(std::string error, bool retryable)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || sourceFailed_ || metadataReadyObserved_) return;
        observations_.push_back(SourceFailureObservation{
            generation_, canonicalInfoHash_, std::move(error), retryable});
        sourceFailed_ = true;
    }

    void requestDrainLocked()
    {
        drainRequested_ = true;
    }

    void tickDrain()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || !drainRequested_) return;
            drainRequested_ = false;
        }
        drainOnNetworkThread();
    }

    void drainOnNetworkThread()
    {
        std::vector<std::pair<PeerHandle, std::shared_ptr<lt::peer_connection>>> ready;
        std::vector<ConnectAction> connects;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            nativeThread_ = std::this_thread::get_id();
            for (const auto &[peer, weak] : peers_)
                if (auto native = weak.lock()) ready.emplace_back(peer, std::move(native));
            if (!stats_.paused) connects.swap(deferredConnects_);
        }
        for (auto &[peer, native] : ready) dispatchControl(peer, native);
        dispatchUploadWire(ready);
        for (const auto &connectAction : connects) (void)connect(connectAction);
        issueReady(0);
    }

    void sendHaveLocked(PeerHandle peer, const std::shared_ptr<lt::peer_connection> &native,
                        std::uint32_t piece)
    {
        if (!native || native->is_connecting() || native->is_disconnecting()) return;
        std::vector<char> frame;
        frame.reserve(9);
        appendBigEndian(frame, 5);
        frame.push_back(static_cast<char>(4));
        appendBigEndian(frame, piece);
        native->send_buffer(lt::span<const char>(frame.data(), frame.size()));
        announcedLocalPieces_[peer].insert(piece);
    }

    void dispatchUploadWire(
        const std::vector<std::pair<PeerHandle, std::shared_ptr<lt::peer_connection>>> &ready)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        for (const auto &[peer, native] : ready) {
            if (!isCurrentNativeLocked(peer, native)) continue;
            for (const auto piece : localAdvertisedPieces_)
                if (announcedLocalPieces_[peer].count(piece) == 0)
                    sendHaveLocked(peer, native, piece);
        }

        while (!pendingUploadActions_.empty()) {
            auto action = std::move(pendingUploadActions_.front());
            pendingUploadActions_.pop_front();
            std::visit([&](const auto &value) {
                auto found = findUpload(value.ownership, value.peer, value.block);
                const auto peer = peers_.find(value.peer);
                auto native = peer == peers_.end() ? std::shared_ptr<lt::peer_connection>{}
                                                   : peer->second.lock();
                const bool valid = found != activeUploads_.end()
                    && isCurrentIdentityLocked(value.peer, found->identity)
                    && locallyUnchoked_.count(value.peer) != 0
                    && interestedPeers_.count(value.peer) != 0
                    && native && !native->is_connecting() && !native->is_disconnecting();
                if (!valid) {
                    ++stats_.uploadActionsRejected;
                    if (found != activeUploads_.end()) found->mailboxPending = false;
                    return;
                }
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, UploadResponseAction>) {
                    std::vector<char> frame;
                    frame.reserve(13 + value.payload.size());
                    appendBigEndian(frame, static_cast<std::uint32_t>(9 + value.payload.size()));
                    frame.push_back(static_cast<char>(7));
                    appendBigEndian(frame, value.block.piece);
                    appendBigEndian(frame, value.block.offset);
                    frame.insert(frame.end(), value.payload.begin(), value.payload.end());
                    native->send_buffer(lt::span<const char>(frame.data(), frame.size()));
                    ++stats_.uploadResponsesFramed;
                    stats_.uploadPayloadBytesFramed += value.payload.size();
                } else {
                    ++stats_.uploadRequestsAborted;
                }
                activeUploads_.erase(found);
                if (stats_.ownedUploadsOutstanding > 0) --stats_.ownedUploadsOutstanding;
            }, action);
        }
    }

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
                 ConnectionIdentity identity,
                 const lt::bitfield *pieces)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            if (pieces && !isCurrentIdentityLocked(peer, identity)) {
                ++staleCallbacksIgnored_;
                return;
            }
            if (!pieces) {
                const auto current = peerIdentities_.find(peer);
                if (current != peerIdentities_.end() && current->second != identity) {
                    cancelUploadsLocked(peer, current->second);
                    advertised_.erase(peer);
                    unchoked_.erase(peer);
                    locallyUnchoked_.erase(peer);
                    interestedPeers_.erase(peer);
                    announcedLocalPieces_.erase(peer);
                    emitAvailabilityLocked(peer);
                }
            }
            prepareNative(peer, native);
            peers_[peer] = native;
            peerIdentities_[peer] = identity;
            stats_.connectedPeers = static_cast<std::uint32_t>(peers_.size());
            stats_.unchokedPeers = static_cast<std::uint32_t>(unchoked_.size());
            if (pieces) {
                auto &available = advertised_[peer];
                available.clear();
                for (int index = 0; index < pieces->size(); ++index)
                    if ((*pieces)[index]) available.insert(static_cast<std::uint32_t>(index));
                emitAvailabilityLocked(peer);
            }
        }
        dispatchControl(peer, native);
        drainOnNetworkThread();
        if (pieces) issueReady(peer);
    }

    void issueReady(PeerHandle)
    {
        RequestAction request;
        std::shared_ptr<lt::peer_connection> selected;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || pendingRequests_.empty()) return;
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

    bool isCurrentNativeLocked(PeerHandle peer,
                               const std::shared_ptr<lt::peer_connection> &native) const
    {
        const auto found = peers_.find(peer);
        return found != peers_.end() && found->second.lock() == native;
    }

    bool validUploadBlockLocked(const lt::peer_request &wire, BlockSpan &block) const
    {
        if (wire.piece < lt::piece_index_t{0} || wire.start < 0 || wire.length <= 0
            || wire.length > static_cast<int>(kWireBlockLength)) return false;
        const auto piece = static_cast<std::uint32_t>(static_cast<int>(wire.piece));
        const auto offset = static_cast<std::uint32_t>(wire.start);
        const auto length = static_cast<std::uint32_t>(wire.length);
        if (piece >= pieceCount_ || piece >= pieceSizes_.size()
            || offset % kWireBlockLength != 0
            || static_cast<std::uint64_t>(offset) + length > pieceSizes_[piece]) return false;
        block = {piece, offset / kWireBlockLength, offset, length};
        return isValidBlock(block);
    }

    void uploadRequest(PeerHandle peer, ConnectionIdentity identity,
                       const lt::peer_request &wire)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nativeThread_ = std::this_thread::get_id();
        BlockSpan block;
        const bool validBlock = validUploadBlockLocked(wire, block);
        const auto peerCount = static_cast<std::uint32_t>(std::count_if(
            activeUploads_.begin(), activeUploads_.end(),
            [&](const auto &owned) { return owned.peer == peer; }));
        const bool duplicate = std::any_of(activeUploads_.begin(), activeUploads_.end(),
            [&](const auto &owned) {
                return owned.peer == peer && owned.identity == identity
                    && validBlock && sameBlock(owned.block, block);
            });
        if (closed_ || !isCurrentIdentityLocked(peer, identity) || !metadataReadyObserved_
            || !validBlock
            || localAdvertisedPieces_.count(block.piece) == 0
            || interestedPeers_.count(peer) == 0 || locallyUnchoked_.count(peer) == 0
            || duplicate || peerCount >= kMaxOutstandingUploadsPerPeer
            || activeUploads_.size() >= kMaxOutstandingUploadsGlobal
            || nextUploadRequestId_ == 0) {
            ++stats_.uploadRequestsRejected;
            return;
        }
        const UploadOwnership ownership{nextUploadRequestId_++, generation_};
        activeUploads_.push_back({ownership, peer, identity, block, false});
        stats_.ownedUploadsOutstanding = static_cast<std::uint32_t>(activeUploads_.size());
        ++stats_.uploadRequestsAccepted;
        observations_.push_back(UploadRequestObservation{ownership, peer, block});
    }

    void uploadCancel(PeerHandle peer, ConnectionIdentity identity,
                      const lt::peer_request &wire)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nativeThread_ = std::this_thread::get_id();
        if (closed_ || !isCurrentIdentityLocked(peer, identity)) {
            ++staleCallbacksIgnored_;
            return;
        }
        BlockSpan block;
        if (!validUploadBlockLocked(wire, block)) return;
        const auto found = std::find_if(activeUploads_.begin(), activeUploads_.end(),
            [&](const auto &owned) {
                return owned.peer == peer && owned.identity == identity
                    && sameBlock(owned.block, block);
            });
        if (found == activeUploads_.end()) return;
        observations_.push_back(UploadCancelObservation{found->ownership, peer, found->block});
        ++stats_.uploadRequestsCancelled;
        activeUploads_.erase(found);
        stats_.ownedUploadsOutstanding = static_cast<std::uint32_t>(activeUploads_.size());
    }

    void cancelUploadsLocked(PeerHandle peer, ConnectionIdentity identity)
    {
        for (auto it = activeUploads_.begin(); it != activeUploads_.end();) {
            if ((peer == 0 || it->peer == peer) && (identity == 0 || it->identity == identity)) {
                observations_.push_back(UploadCancelObservation{it->ownership, it->peer, it->block});
                ++stats_.uploadRequestsCancelled;
                it = activeUploads_.erase(it);
            } else {
                ++it;
            }
        }
        stats_.ownedUploadsOutstanding = static_cast<std::uint32_t>(activeUploads_.size());
    }

    bool authorize(PeerHandle peer, ConnectionIdentity identity, const lt::peer_request &wire)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || !isCurrentIdentityLocked(peer, identity)) {
            ++staleCallbacksIgnored_;
            return false;
        }
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

    void framed(PeerHandle peer, ConnectionIdentity identity, const lt::peer_request &wire)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || !isCurrentIdentityLocked(peer, identity)) {
            ++staleCallbacksIgnored_;
            return;
        }
        const auto found = std::find_if(framedPending_.begin(), framedPending_.end(),
            [&](const auto &r) { return r.peer == peer && matches(r.block, wire); });
        if (found == framedPending_.end()) return;
        auto active = findActive(*found);
        if (active != active_.end()) { active->framed = true; ++framedCount_; }
        framedPending_.erase(found);
    }

    void receive(PeerHandle peer, ConnectionIdentity identity,
                 const lt::peer_request &wire, lt::span<const char> payload)
    {
        std::string entered;
        std::string release;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || !isCurrentIdentityLocked(peer, identity)) {
                ++staleCallbacksIgnored_;
                return;
            }
            entered = receiveBarrierEntered_;
            release = receiveBarrierRelease_;
        }
        if (!entered.empty()) {
            std::ofstream(entered, std::ios::trunc).close();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!std::filesystem::exists(release) && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || !isCurrentIdentityLocked(peer, identity)) {
                ++staleCallbacksIgnored_;
                return;
            }
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
                requestDrainLocked();
            } else {
                const auto old = std::find_if(retired_.rbegin(), retired_.rend(), [&](const auto &r) {
                    return r.peer == peer && matches(r.block, wire);
                });
                if (old != retired_.rend()) {
                    std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
                    observations_.push_back(BlockObservation{old->ownership, peer, old->block,
                                                             std::move(bytes), true, true});
                }
            }
        }
        // on_piece runs before locked libtorrent removes the completed entry
        // from its download queue. The libtorrent-owned tick is the first safe
        // callback after normal receive accounting, so it drains this mailbox.
    }

    void have(PeerHandle peer, ConnectionIdentity identity, lt::piece_index_t piece)
    {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || !isCurrentIdentityLocked(peer, identity)) {
                ++staleCallbacksIgnored_;
                return;
            }
            if (static_cast<int>(piece) < 0) return;
            changed = advertised_[peer].insert(
                static_cast<std::uint32_t>(static_cast<int>(piece))).second;
            if (changed) emitAvailabilityLocked(peer);
        }
        if (changed) drainOnNetworkThread();
    }

    void detach(PeerHandle peer, const std::shared_ptr<lt::peer_connection> &native,
                ConnectionIdentity identity,
                const std::string &error)
    {
        const auto endpoint = native ? endpointKey(native->remote()) : std::string{};
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto current = peers_.find(peer);
            if (current == peers_.end()) return;
            if (identity != 0 && isStaleIdentityLocked(peer, identity)) {
                ++staleDisconnectsIgnored_;
                return;
            }
            if (identity != 0) lastDetachedIdentity_.try_emplace(peer, identity);
            cancelUploadsLocked(peer, identity);
            peers_.erase(peer);
            peerIdentities_.erase(peer);
            advertised_.erase(peer);
            emitAvailabilityLocked(peer);
            unchoked_.erase(peer);
            locallyUnchoked_.erase(peer);
            interestedPeers_.erase(peer);
            announcedLocalPieces_.erase(peer);
            pendingControl_.erase(peer);
            if (!endpoint.empty()) {
                const auto owner = endpointPeers_.find(endpoint);
                if (owner != endpointPeers_.end() && owner->second == peer)
                    endpointPeers_.erase(owner);
            }
            stats_.connectedPeers = static_cast<std::uint32_t>(peers_.size());
            stats_.unchokedPeers = static_cast<std::uint32_t>(unchoked_.size());
            for (auto &owned : active_) if (!owned.terminal && owned.action.peer == peer) {
                owned.terminal = true; retired_.push_back(owned.action);
                if (stats_.ownedRequestsOutstanding > 0) --stats_.ownedRequestsOutstanding;
                observations_.push_back(FailureObservation{owned.action.ownership, peer,
                    owned.action.block, "peer disconnected: " + error, true});
            }
            pendingRequests_.erase(std::remove_if(pendingRequests_.begin(), pendingRequests_.end(),
                [&](const auto &request) { return request.peer == peer; }), pendingRequests_.end());
            permits_.erase(std::remove_if(permits_.begin(), permits_.end(),
                [&](const auto &request) { return request.peer == peer; }), permits_.end());
            framedPending_.erase(std::remove_if(framedPending_.begin(), framedPending_.end(),
                [&](const auto &request) { return request.peer == peer; }), framedPending_.end());
            removed = true;
        }
        if (removed) drainOnNetworkThread();
    }

    bool isStaleIdentityLocked(PeerHandle peer, ConnectionIdentity identity) const
    {
        const auto current = peerIdentities_.find(peer);
        return current != peerIdentities_.end() && current->second != identity;
    }

    bool isCurrentIdentityLocked(PeerHandle peer, ConnectionIdentity identity) const
    {
        const auto current = peerIdentities_.find(peer);
        return identity != 0 && current != peerIdentities_.end() && current->second == identity;
    }

    void emitAvailabilityLocked(PeerHandle peer)
    {
        std::vector<std::uint32_t> pieces;
        const auto found = advertised_.find(peer);
        if (found != advertised_.end()) pieces.assign(found->second.begin(), found->second.end());
        observations_.push_back(AvailablePiecesObservation{generation_, peer, std::move(pieces)});
    }

    void observePeer(PeerHandle peer, ConnectionIdentity identity, bool choking, bool interested,
                     std::shared_ptr<lt::peer_connection> native)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || !isCurrentIdentityLocked(peer, identity)) {
                ++staleCallbacksIgnored_;
                return;
            }
        }
        lt::peer_info info;
        if (native) native->get_peer_info(info);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || !isCurrentIdentityLocked(peer, identity)) {
                ++staleCallbacksIgnored_;
                return;
            }
            if (choking) unchoked_.erase(peer); else unchoked_.insert(peer);
            if (interested) interestedPeers_.insert(peer); else interestedPeers_.erase(peer);
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

    std::vector<OwnedUpload>::iterator findUpload(const UploadOwnership &owner,
        PeerHandle peer, const BlockSpan &block)
    {
        return std::find_if(activeUploads_.begin(), activeUploads_.end(), [&](const auto &owned) {
            return owned.peer == peer && owned.ownership.requestId == owner.requestId
                && owned.ownership.generation == owner.generation
                && sameBlock(owned.block, block);
        });
    }

    mutable std::mutex mutex_;
    std::mutex alertMutex_;
    std::unique_ptr<lt::session> session_;
    lt::torrent_handle torrent_;
    std::uint32_t pieceCount_ = 0;
    std::vector<std::uint32_t> pieceSizes_;
    EngineGeneration generation_ = 0;
    V1InfoHash canonicalInfoHash_{};
    bool controlled_ = false, closed_ = false, closedObserved_ = false, drainRequested_ = false;
    bool metadataReadyObserved_ = false, sourceFailed_ = false;
    std::uint64_t forbiddenAttempts_ = 0, guardedNativeTouches_ = 0;
    std::uint64_t autonomousNativeMutations_ = 0, ownedAddCount_ = 0;
    std::uint64_t framedCount_ = 0, staleDisconnectsIgnored_ = 0, staleCallbacksIgnored_ = 0;
    std::thread::id nativeThread_{};
    std::map<std::string, PeerHandle> endpointPeers_;
    std::map<PeerHandle, std::weak_ptr<lt::peer_connection>> peers_;
    std::map<PeerHandle, ConnectionIdentity> peerIdentities_;
    std::map<PeerHandle, ConnectionIdentity> lastDetachedIdentity_;
    std::map<PeerHandle, std::set<std::uint32_t>> advertised_;
    std::set<std::uint32_t> localAdvertisedPieces_;
    std::map<PeerHandle, std::set<std::uint32_t>> announcedLocalPieces_;
    std::set<PeerHandle> unchoked_;
    std::set<PeerHandle> locallyUnchoked_;
    std::set<PeerHandle> interestedPeers_;
    std::deque<RequestAction> pendingRequests_;
    std::vector<ConnectAction> pendingConnects_;
    std::vector<ConnectAction> deferredConnects_;
    std::map<PeerHandle, std::deque<TorrentAction>> pendingControl_;
    std::vector<RequestAction> permits_, framedPending_, retired_;
    std::vector<OwnedRequest> active_;
    std::vector<OwnedUpload> activeUploads_;
    std::deque<UploadMailboxAction> pendingUploadActions_;
    UploadRequestId nextUploadRequestId_ = 1;
    std::deque<TorrentObservation> observations_;
    TransportStatistics stats_{};
    std::string receiveBarrierEntered_, receiveBarrierRelease_;
};
}

std::unique_ptr<ports::TorrentTransport> makeLibTorrent2Adapter(
    const std::string &torrentPath, const std::string &savePath)
{
    try {
        std::ifstream input(torrentPath, std::ios::binary);
        if (!input) return {};
        std::vector<std::uint8_t> bytes{
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        lt::error_code error;
        lt::torrent_info info(reinterpret_cast<const char *>(bytes.data()),
                              static_cast<int>(bytes.size()), error);
        if (error) return {};
        return ports::openTorrentTransport(
            ports::TorrentOpenRequest{1, v1InfoHash(info),
                                      ports::MetainfoSource{std::move(bytes)}, savePath});
    }
    catch (...) { return {}; }
}

bool connectPeer(ports::TorrentTransport &transport, ports::PeerHandle peer,
                 const std::string &address, std::uint16_t port)
{
    return transport.submit(ports::ConnectAction{1, peer, address, port});
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

std::uint64_t guardedNativeTouchCount(const ports::TorrentTransport &transport)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter ? adapter->guardedNativeTouchCount() : 0;
}

std::uint64_t staleDisconnectIgnoredCount(const ports::TorrentTransport &transport)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter ? adapter->staleDisconnectCount() : 0;
}

bool replayLastDetachedIdentity(ports::TorrentTransport &transport, ports::PeerHandle peer)
{
    auto *adapter = dynamic_cast<LibTorrent2Adapter *>(&transport);
    return adapter && adapter->replayLastDetached(peer);
}

bool replayLastDetachedCallbacks(ports::TorrentTransport &transport, ports::PeerHandle peer,
                                 const ports::BlockSpan &block)
{
    auto *adapter = dynamic_cast<LibTorrent2Adapter *>(&transport);
    return adapter && adapter->replayLastDetachedCallbacks(peer, block);
}

std::uint64_t staleCallbackIgnoredCount(const ports::TorrentTransport &transport)
{
    const auto *adapter = dynamic_cast<const LibTorrent2Adapter *>(&transport);
    return adapter ? adapter->staleCallbackCount() : 0;
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

namespace server1::ports {
std::unique_ptr<TorrentTransport>
openTorrentTransport(const TorrentOpenRequest &request) noexcept
{
    try {
        return std::make_unique<transport::LibTorrent2Adapter>(request);
    } catch (const std::exception &error) {
        return std::make_unique<transport::FailedTorrentTransport>(
            request.generation, request.infoHash, error.what());
    } catch (...) {
        return std::make_unique<transport::FailedTorrentTransport>(
            request.generation, request.infoHash, "unknown torrent source failure");
    }
}
} // namespace server1::ports
