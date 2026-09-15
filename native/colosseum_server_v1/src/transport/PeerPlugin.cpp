#include "server1/ports/TorrentTransport.h"

#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_connection_handle.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace lt = libtorrent;

namespace server1::transport {
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
using NetworkTick = std::function<void()>;

namespace {
class ProductionPeerPlugin final : public lt::peer_plugin {
public:
    ProductionPeerPlugin(lt::peer_connection_handle peer, ports::PeerHandle handle,
                         ConnectionIdentity identity,
                         PeerInbound inbound, DetachPeer detach, AuthorizeRequest authorize,
                         SentRequest sent, ReceivePiece receive, PeerState state, PeerHave have)
        : peer_(std::move(peer)), handle_(handle), identity_(identity), inbound_(std::move(inbound)),
          detach_(std::move(detach)), authorize_(std::move(authorize)), sent_(std::move(sent)),
          receive_(std::move(receive)), state_(std::move(state)), have_(std::move(have)) {}

    bool on_handshake(lt::span<const char>) override
    { inbound_(handle_, peer_.native_handle(), identity_, nullptr); return true; }
    bool on_bitfield(const lt::bitfield &pieces) override
    { inbound_(handle_, peer_.native_handle(), identity_, &pieces); return false; }
    bool on_unchoke() override
    { state_(handle_, identity_, false, false, peer_.native_handle()); return false; }
    bool on_choke() override
    { state_(handle_, identity_, true, false, peer_.native_handle()); return false; }
    bool on_interested() override
    { state_(handle_, identity_, false, true, peer_.native_handle()); return false; }
    bool on_not_interested() override
    { state_(handle_, identity_, false, false, peer_.native_handle()); return false; }
    bool on_have(lt::piece_index_t piece) override
    { have_(handle_, identity_, piece); return false; }
    void on_disconnect(const lt::error_code &error) override
    { detach_(handle_, peer_.native_handle(), identity_, error.message()); }
    bool write_request(const lt::peer_request &request) override
    { return !authorize_(handle_, identity_, request); }
    void sent_request(const lt::peer_request &request) override
    { sent_(handle_, identity_, request); }
    bool on_piece(const lt::peer_request &request, lt::span<const char> payload) override
    {
        receive_(handle_, identity_, request, payload);
        return false;
    }
private:
    lt::peer_connection_handle peer_;
    ports::PeerHandle handle_;
    ConnectionIdentity identity_;
    PeerInbound inbound_; DetachPeer detach_; AuthorizeRequest authorize_;
    SentRequest sent_; ReceivePiece receive_; PeerState state_; PeerHave have_;
};

class ProductionTorrentPlugin final : public lt::torrent_plugin {
public:
    ProductionTorrentPlugin(IdentifyPeer identify, PeerInbound inbound, DetachPeer detach,
                            AuthorizeRequest authorize, SentRequest sent,
                            ReceivePiece receive, PeerState state, PeerHave have, NetworkTick tick)
        : identify_(std::move(identify)), inbound_(std::move(inbound)), detach_(std::move(detach)),
          authorize_(std::move(authorize)), sent_(std::move(sent)), receive_(std::move(receive)),
          state_(std::move(state)), have_(std::move(have)), tick_(std::move(tick)) {}
    std::shared_ptr<lt::peer_plugin> new_connection(const lt::peer_connection_handle &peer) override
    {
        const auto handle = identify_(peer.remote());
        if (handle == 0) return {};
        const auto identity = nextIdentity_++;
        return std::make_shared<ProductionPeerPlugin>(peer, handle, identity, inbound_, detach_, authorize_,
                                                       sent_, receive_, state_, have_);
    }
    void tick() override { tick_(); }
private:
    IdentifyPeer identify_; PeerInbound inbound_; DetachPeer detach_; AuthorizeRequest authorize_;
    SentRequest sent_; ReceivePiece receive_; PeerState state_; PeerHave have_; NetworkTick tick_;
    ConnectionIdentity nextIdentity_ = 1;
};
}

std::shared_ptr<lt::torrent_plugin> makeProductionPeerPlugin(
    IdentifyPeer identify, PeerInbound inbound, DetachPeer detach, AuthorizeRequest authorize,
    SentRequest sent, ReceivePiece receive, PeerState state, PeerHave have, NetworkTick tick)
{
    return std::make_shared<ProductionTorrentPlugin>(std::move(identify), std::move(inbound),
        std::move(detach), std::move(authorize), std::move(sent), std::move(receive),
        std::move(state), std::move(have), std::move(tick));
}
} // namespace server1::transport
