#include "server1/ports/TorrentTransport.h"

#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_connection_handle.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace lt = libtorrent;

namespace server1::transport {
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

namespace {
class ProductionPeerPlugin final : public lt::peer_plugin {
public:
    ProductionPeerPlugin(lt::peer_connection_handle peer, ports::PeerHandle handle,
                         PeerInbound inbound, DetachPeer detach, AuthorizeRequest authorize,
                         SentRequest sent, ReceivePiece receive, PeerState state)
        : peer_(std::move(peer)), handle_(handle), inbound_(std::move(inbound)),
          detach_(std::move(detach)), authorize_(std::move(authorize)), sent_(std::move(sent)),
          receive_(std::move(receive)), state_(std::move(state)) {}

    bool on_handshake(lt::span<const char>) override
    { inbound_(handle_, peer_.native_handle(), nullptr); return true; }
    bool on_bitfield(const lt::bitfield &pieces) override
    { inbound_(handle_, peer_.native_handle(), &pieces); return false; }
    bool on_unchoke() override
    { state_(handle_, false, false, peer_.native_handle()); return false; }
    bool on_choke() override
    { state_(handle_, true, false, peer_.native_handle()); return false; }
    bool on_interested() override
    { state_(handle_, false, true, peer_.native_handle()); return false; }
    bool on_not_interested() override
    { state_(handle_, false, false, peer_.native_handle()); return false; }
    void on_disconnect(const lt::error_code &error) override
    { detach_(handle_, peer_.native_handle(), error.message()); }
    bool write_request(const lt::peer_request &request) override
    { return !authorize_(handle_, request); }
    void sent_request(const lt::peer_request &request) override { sent_(handle_, request); }
    bool on_piece(const lt::peer_request &request, lt::span<const char> payload) override
    { receive_(handle_, request, payload); return false; }
private:
    lt::peer_connection_handle peer_;
    ports::PeerHandle handle_;
    PeerInbound inbound_; DetachPeer detach_; AuthorizeRequest authorize_;
    SentRequest sent_; ReceivePiece receive_; PeerState state_;
};

class ProductionTorrentPlugin final : public lt::torrent_plugin {
public:
    ProductionTorrentPlugin(IdentifyPeer identify, PeerInbound inbound, DetachPeer detach,
                            AuthorizeRequest authorize, SentRequest sent,
                            ReceivePiece receive, PeerState state)
        : identify_(std::move(identify)), inbound_(std::move(inbound)), detach_(std::move(detach)),
          authorize_(std::move(authorize)), sent_(std::move(sent)), receive_(std::move(receive)),
          state_(std::move(state)) {}
    std::shared_ptr<lt::peer_plugin> new_connection(const lt::peer_connection_handle &peer) override
    {
        const auto handle = identify_(peer.remote());
        if (handle == 0) return {};
        return std::make_shared<ProductionPeerPlugin>(peer, handle, inbound_, detach_, authorize_,
                                                       sent_, receive_, state_);
    }
private:
    IdentifyPeer identify_; PeerInbound inbound_; DetachPeer detach_; AuthorizeRequest authorize_;
    SentRequest sent_; ReceivePiece receive_; PeerState state_;
};
}

std::shared_ptr<lt::torrent_plugin> makeProductionPeerPlugin(
    IdentifyPeer identify, PeerInbound inbound, DetachPeer detach, AuthorizeRequest authorize,
    SentRequest sent, ReceivePiece receive, PeerState state)
{
    return std::make_shared<ProductionTorrentPlugin>(std::move(identify), std::move(inbound),
        std::move(detach), std::move(authorize), std::move(sent), std::move(receive),
        std::move(state));
}
} // namespace server1::transport
