#include "server1/policy/SwarmPolicy.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace server1::policy {
SwarmPolicy::SwarmPolicy(std::size_t capacity, std::size_t uploads)
    : capacity_(capacity), uploads_(uploads) {}
void SwarmPolicy::addPeer(PeerState peer) { peers_[peer.id] = std::move(peer); }
const PeerState &SwarmPolicy::peer(const std::string &id) const
{
    const auto found = peers_.find(id); if (found == peers_.end()) throw std::out_of_range("peer id");
    return found->second;
}
void SwarmPolicy::onChoke(const std::string &id, std::uint64_t nowMs)
{ auto &p = peers_.at(id); p.peerChoking = true; p.chokeDeadlineMs = nowMs + 5000; }
void SwarmPolicy::onUnchoke(const std::string &id)
{ auto &p = peers_.at(id); p.peerChoking = false; p.chokeDeadlineMs.reset(); }
bool SwarmPolicy::shouldDropChoked(const std::string &id, std::size_t queued,
                                  std::size_t connected, std::uint64_t nowMs) const
{
    const auto &p = peer(id); const auto open = capacity_ > connected ? capacity_ - connected : 0;
    return p.chokeDeadlineMs && nowMs >= *p.chokeDeadlineMs && p.amInterested && queued > 2 * open;
}
bool SwarmPolicy::rechokeDue(std::uint64_t nowMs) const noexcept { return nowMs >= lastRechokeMs_ + 10000; }
std::vector<SwarmAction> SwarmPolicy::rechoke(std::uint64_t nowMs)
{
    lastRechokeMs_ = nowMs; std::vector<PeerState *> ranked; std::vector<SwarmAction> actions;
    for (auto &[id, state] : peers_) {
        if (state.isSeeder) { if (!state.amChoking) { state.amChoking = true; actions.push_back({id,true}); } }
        else ranked.push_back(&state);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto *a, const auto *b) {
        if (a->downloadSpeed != b->downloadSpeed) return a->downloadSpeed > b->downloadSpeed;
        if (a->uploadSpeed != b->uploadSpeed) return a->uploadSpeed > b->uploadSpeed;
        if (a->amChoking != b->amChoking) return !a->amChoking;
        return a->salt < b->salt;
    });
    std::size_t index = 0, interested = 0;
    for (; index < ranked.size() && interested < uploads_; ++index) {
        auto &p = *ranked[index]; if (p.amChoking) { p.amChoking=false; actions.push_back({p.id,false}); }
        if (p.amInterested) ++interested;
    }
    if (uploads_ && index < ranked.size()) {
        auto optimistic = std::min_element(ranked.begin()+static_cast<std::ptrdiff_t>(index), ranked.end(),
            [](const auto *a, const auto *b) { if (a->amInterested != b->amInterested) return a->amInterested > b->amInterested; return a->salt < b->salt; });
        if (optimistic != ranked.end() && (*optimistic)->amInterested && (*optimistic)->amChoking) {
            (*optimistic)->amChoking=false; actions.push_back({(*optimistic)->id,false});
        }
    }
    for (auto *p : ranked) {
        const bool selected = std::any_of(actions.begin(), actions.end(), [&](const auto &a){ return a.peerId==p->id && !a.choke; });
        if (!selected && !p->amChoking) { p->amChoking=true; actions.push_back({p->id,true}); }
    }
    return actions;
}

std::string generatePeerIdentity(std::uint64_t seed)
{
    static constexpr std::array<const char *,4> bases{"qB4600","DE2110","AZ5770","TR4040"};
    std::mt19937_64 random(seed); std::string version = bases[random()%bases.size()];
    for (char &c : version) if (c >= '1' && c <= '9') {
        const int n = c-'0'; std::uniform_int_distribution<int> digit(n/2,n); c=static_cast<char>('0'+digit(random));
    }
    std::ostringstream tail; tail << std::hex << std::setfill('0') << std::setw(12) << (random() & 0xffffffffffffULL);
    return "-" + version + "-" + tail.str();
}

void EngineSwarmRegistry::start(std::string infoHash, std::uint64_t generation, std::uint64_t seed)
{ purgeActions(infoHash); const auto identity = generatePeerIdentity(seed); entries_[std::move(infoHash)] = {generation, identity, {}, 0, {}, {}}; }
bool EngineSwarmRegistry::stop(const std::string &infoHash) { purgeActions(infoHash); return entries_.erase(infoHash) != 0; }
bool EngineSwarmRegistry::live(const std::string &infoHash) const { return entries_.count(infoHash) != 0; }
std::string EngineSwarmRegistry::peerIdentity(const std::string &infoHash) const { return entries_.at(infoHash).identity; }
void EngineSwarmRegistry::recordDiscovery(const std::string &h, std::string s) { entries_.at(h).discovery.push_back(std::move(s)); }
void EngineSwarmRegistry::scheduleTimer(const std::string &h, std::uint64_t d) { entries_.at(h).timer=d; }
std::vector<std::string> EngineSwarmRegistry::discovery(const std::string &h) const
{ const auto i=entries_.find(h); return i==entries_.end()?std::vector<std::string>{}:i->second.discovery; }
std::uint64_t EngineSwarmRegistry::timer(const std::string &h) const
{ const auto i=entries_.find(h); return i==entries_.end()?0:i->second.timer; }
bool EngineSwarmRegistry::queuePeer(const std::string &h, std::string peer, std::uint64_t g)
{ auto i=entries_.find(h); if(i==entries_.end()||i->second.generation!=g||i->second.peers.count(peer))return false; i->second.peers.emplace(std::move(peer),PeerEntry{PeerLifecycleState::Queued,0}); return true; }
bool EngineSwarmRegistry::connectPeer(const std::string &h,const std::string &p,std::uint64_t g,std::uint64_t now)
{ auto i=entries_.find(h); if(i==entries_.end()||i->second.generation!=g)return false; auto q=i->second.peers.find(p); if(q==i->second.peers.end()||q->second.state!=PeerLifecycleState::Queued)return false; q->second={PeerLifecycleState::Handshaking,now+10000}; actions_.push_back({SwarmTransportActionType::Connect,h,p,g}); return true; }
bool EngineSwarmRegistry::completeHandshake(const std::string &h,const std::string &p,std::uint64_t g,const std::string &remote)
{ auto i=entries_.find(h); if(i==entries_.end()||i->second.generation!=g)return false; auto q=i->second.peers.find(p); if(q==i->second.peers.end()||q->second.state!=PeerLifecycleState::Handshaking)return false; if(remote!=h){actions_.push_back({SwarmTransportActionType::Disconnect,h,p,g,0,0,0,0,"infohash mismatch"}); i->second.peers.erase(q); return false;} q->second={PeerLifecycleState::Ready,0}; return true; }
bool EngineSwarmRegistry::requestBlock(const std::string &h,const std::string &p,std::uint64_t g,std::uint64_t id,std::size_t piece,std::size_t offset,std::size_t length,std::uint64_t now)
{ auto i=entries_.find(h); if(i==entries_.end()||i->second.generation!=g||i->second.requests.count(id))return false; auto q=i->second.peers.find(p); if(q==i->second.peers.end()||q->second.state!=PeerLifecycleState::Ready)return false; i->second.requests.emplace(id,RequestEntry{p,g,piece,offset,length,now+30000}); actions_.push_back({SwarmTransportActionType::Request,h,p,g,id,piece,offset,length,{}}); return true; }
bool EngineSwarmRegistry::completeRequest(const std::string &h,std::uint64_t g,std::uint64_t id)
{ auto i=entries_.find(h); if(i==entries_.end()||i->second.generation!=g)return false; auto r=i->second.requests.find(id); if(r==i->second.requests.end()||r->second.generation!=g)return false; i->second.requests.erase(r); return true; }
void EngineSwarmRegistry::advance(std::uint64_t now)
{ for(auto &[h,e]:entries_){ for(auto p=e.peers.begin();p!=e.peers.end();){if(p->second.state==PeerLifecycleState::Handshaking&&now>=p->second.handshakeDeadlineMs){actions_.push_back({SwarmTransportActionType::Disconnect,h,p->first,e.generation,0,0,0,0,"handshake timeout"});p=e.peers.erase(p);}else ++p;} for(auto r=e.requests.begin();r!=e.requests.end();){if(now>=r->second.deadlineMs){const auto x=r->second;actions_.push_back({SwarmTransportActionType::Cancel,h,x.peer,e.generation,r->first,x.piece,x.offset,x.length,"request timeout"});r=e.requests.erase(r);}else ++r;}} }
std::optional<PeerLifecycleState> EngineSwarmRegistry::peerState(const std::string &h,const std::string &p) const
{ auto i=entries_.find(h); if(i==entries_.end())return std::nullopt; auto q=i->second.peers.find(p); return q==i->second.peers.end()?std::nullopt:std::optional<PeerLifecycleState>(q->second.state); }
std::optional<PeerLifecycleCounts> EngineSwarmRegistry::peerCounts(
    const std::string &h, std::uint64_t generation) const noexcept
{
    const auto entry = entries_.find(h);
    if (entry == entries_.end() || entry->second.generation != generation)
        return std::nullopt;
    PeerLifecycleCounts counts;
    for (const auto &[peer, state] : entry->second.peers) {
        (void)peer;
        switch (state.state) {
        case PeerLifecycleState::Queued: ++counts.queued; break;
        case PeerLifecycleState::Handshaking: ++counts.handshaking; break;
        case PeerLifecycleState::Ready: ++counts.ready; break;
        }
    }
    return counts;
}
std::vector<SwarmTransportAction> EngineSwarmRegistry::takeActions(){std::vector<SwarmTransportAction> r; r.swap(actions_); return r;}
void EngineSwarmRegistry::purgeActions(const std::string &infoHash)
{ actions_.erase(std::remove_if(actions_.begin(),actions_.end(),[&](const auto &action){return action.infoHash==infoHash;}),actions_.end()); }
}
