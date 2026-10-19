#include "server1/policy/SwarmConnections.h"
#include <algorithm>
#include <limits>
namespace server1::policy {
bool SwarmConnections::add(ports::ConnectAction action,std::uint64_t nowMs)
{
    now_=nowMs;
    if(closed_ || peers_.count(action.peer) || find(action.address,action.port)) return false;
    const auto peer=action.peer;
    peers_.emplace(peer,Peer{std::move(action)});queue_.push_back(peer);drain();return true;
}
std::optional<ports::PeerHandle> SwarmConnections::find(const std::string &address,std::uint16_t port)const
{
    for(const auto &[id,peer]:peers_)if(peer.action.address==address && peer.action.port==port)return id;
    return {};
}
bool SwarmConnections::contains(ports::PeerHandle peer)const{return peers_.count(peer)!=0;}
void SwarmConnections::drain()
{
    if(closed_ || paused_ || static_cast<double>(connections_)>=capacity_ || queue_.empty())return;
    const auto id=queue_.front();queue_.pop_front();const auto found=peers_.find(id);
    if(found==peers_.end())return;
    auto &peer=found->second;peer.active=true;peer.retryAt.reset();++connections_;++tries_;
    const auto deadline=[&](std::uint64_t delay){return now_>std::numeric_limits<std::uint64_t>::max()-delay
        ? std::numeric_limits<std::uint64_t>::max() : now_+delay;};
    peer.connectAt=deadline(connectTimeout_);peer.connectOrder=++timerOrder_;
    peer.handshakeAt=deadline(handshakeTimeout_);peer.handshakeOrder=++timerOrder_;
    connects_.push_back(peer.action);
}
void SwarmConnections::resume(std::uint64_t nowMs){if(closed_)return;now_=nowMs;paused_=false;drain();}
void SwarmConnections::connected(ports::PeerHandle id)
{const auto found=peers_.find(id);if(found!=peers_.end())found->second.connectAt.reset();}
void SwarmConnections::handshake(ports::PeerHandle id)
{
    if(closed_)return;
    const auto found=peers_.find(id);
    if(found==peers_.end()){if(incoming_.insert(id).second)++connections_;return;}
    if(!found->second.active)return;
    found->second.connectAt.reset();found->second.handshakeAt.reset();
    found->second.reconnect=true;found->second.retries=0;
}
void SwarmConnections::disconnected(ports::PeerHandle id,std::uint64_t nowMs)
{
    now_=nowMs;
    if(incoming_.erase(id)){if(connections_)--connections_;drain();return;}
    const auto found=peers_.find(id);if(found==peers_.end() || !found->second.active)return;
    auto &peer=found->second;peer.active=false;peer.connectAt.reset();peer.handshakeAt.reset();
    if(connections_)--connections_;
    if(!peer.reconnect || closed_ || peer.retries>=3)peers_.erase(found);
    else {
        constexpr std::uint64_t waits[]{4000,8000,12000};
        const auto wait=waits[peer.retries++];
        peer.retryAt=nowMs>std::numeric_limits<std::uint64_t>::max()-wait
            ? std::numeric_limits<std::uint64_t>::max() : nowMs+wait;
        peer.timerOrder=++timerOrder_;
    }
    drain();
}
void SwarmConnections::advance(std::uint64_t nowMs)
{
    if(closed_)return;
    now_=nowMs;
    for(;;){
        auto next=peers_.end();int kind=0;std::pair<std::uint64_t,std::uint64_t> earliest;
        for(auto it=peers_.begin();it!=peers_.end();++it) {
            auto consider=[&](const auto &at,std::uint64_t order,int event){
                if(at && *at<=nowMs && (next==peers_.end() || std::make_pair(*at,order)<earliest))
                    {next=it;earliest={*at,order};kind=event;}
            };
            consider(it->second.retryAt,it->second.timerOrder,0);
            consider(it->second.connectAt,it->second.connectOrder,1);
            consider(it->second.handshakeAt,it->second.handshakeOrder,2);
        }
        if(next==peers_.end())break;
        if(kind==0){next->second.retryAt.reset();queue_.push_back(next->first);drain();}
        else {next->second.connectAt.reset();next->second.handshakeAt.reset();disconnects_.push_back(next->first);}
    }
}
std::vector<ports::ConnectAction> SwarmConnections::takeConnects()
{auto result=std::move(connects_);connects_.clear();return result;}
std::vector<ports::PeerHandle> SwarmConnections::takeDisconnects()
{auto result=std::move(disconnects_);disconnects_.clear();return result;}
void SwarmConnections::close()
{closed_=true;peers_.clear();queue_.clear();connects_.clear();disconnects_.clear();incoming_.clear();connections_=0;}
}
