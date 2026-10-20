#pragma once
#include "server1/ports/TorrentTransport.h"
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace server1::policy {
// M818 PWS connection ownership. Each add/resume/close/timeout drains at most
// one queue element, matching the source (not a greedy native dial loop).
class SwarmConnections final {
public:
    explicit SwarmConnections(double capacity=100,std::uint64_t connectTimeout=3000,
                              std::uint64_t handshakeTimeout=25000)
        :capacity_(capacity),connectTimeout_(connectTimeout),handshakeTimeout_(handshakeTimeout){}
    bool add(ports::ConnectAction action,std::uint64_t nowMs=0);
    void pause() noexcept {paused_=true;}
    void resume(std::uint64_t nowMs=0);
    void connected(ports::PeerHandle peer);
    void handshake(ports::PeerHandle peer);
    void disconnected(ports::PeerHandle peer,std::uint64_t nowMs);
    void advance(std::uint64_t nowMs);
    void close();
    [[nodiscard]] std::optional<ports::PeerHandle> findRaw(const std::string &raw)const;
    [[nodiscard]] bool contains(ports::PeerHandle peer)const;
    [[nodiscard]] std::optional<ports::PeerHandle> find(const std::string &address,std::uint16_t port)const;
    [[nodiscard]] std::size_t queued()const noexcept{return queue_.size();}
    [[nodiscard]] std::size_t unique()const noexcept{return peers_.size();}
    [[nodiscard]] std::size_t connections()const noexcept{return connections_;}
    [[nodiscard]] std::uint64_t tries()const noexcept{return tries_;}
    std::vector<ports::ConnectAction> takeConnects();
    std::vector<ports::PeerHandle> takeDisconnects();
private:
    struct Peer {ports::ConnectAction action;bool active=false,reconnect=false;unsigned retries=0;
        std::optional<std::uint64_t> retryAt,connectAt,handshakeAt;
        std::uint64_t timerOrder=0,connectOrder=0,handshakeOrder=0;};
    void drain();
    double capacity_;std::uint64_t connectTimeout_,handshakeTimeout_,now_=0;bool paused_=false,closed_=false;std::size_t connections_=0;
    std::uint64_t tries_=0,timerOrder_=0;
    std::map<ports::PeerHandle,Peer> peers_;
    std::deque<ports::PeerHandle> queue_;
    std::vector<ports::ConnectAction> connects_;
    std::vector<ports::PeerHandle> disconnects_;
    std::set<ports::PeerHandle> incoming_;
};
}
