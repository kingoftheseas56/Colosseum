#include "server1/discovery/PeerSearch.h"
#include <stdexcept>
#include <utility>
namespace server1::discovery {
PeerSearch::PeerSearch(std::vector<std::string> sourceNames,std::optional<std::size_t> min,std::optional<std::size_t> max,std::uint64_t now,AutonomyPolicy autonomy)
 :minimum_(min),maximum_(max),autonomy_(autonomy)
{
 if(!autonomy_.externallyControlled())throw std::invalid_argument("autonomous picker/discovery is forbidden");
 std::string hash;for(const auto&s:sourceNames)if(s.rfind("dht:",0)==0){hash=s.substr(4);break;}
 for(auto &source:sourceNames){if(source.rfind("tracker:",0)==0){const auto i=trackers_.size();trackers_.emplace_back(source.substr(8),hash);bindings_.push_back({true,i});sources_.push_back({0,0,0,std::move(source),0});}else if(source.rfind("dht:",0)==0){const auto i=dhts_.size();dhts_.emplace_back(source.substr(4));bindings_.push_back({false,i});sources_.push_back({0,0,0,std::move(source),0});}}
 run(now);
}
void PeerSearch::run(std::uint64_t now){if(closed_)return;running_=true;lastNowMs_=now;for(std::size_t i=0;i<sources_.size();++i){sources_[i].lastStartedMs=now;const auto b=bindings_[i];if(b.tracker){trackers_[b.index].run();sources_[i].numRequests=trackers_[b.index].numRequests();}else dhts_[b.index].run(now);}}
void PeerSearch::pause(std::uint64_t now)noexcept{running_=false;lastNowMs_=now;for(auto&t:trackers_)t.pause();for(auto&d:dhts_)d.pause(now);}
void PeerSearch::onSwarmState(std::size_t queued,bool swarmPaused,std::uint64_t now){if(swarmPaused&&running_)pause(now);else if(minimum_&&queued<*minimum_&&!running_)run(now);else if(maximum_&&queued>*maximum_&&running_)pause(now);}
void PeerSearch::tick(std::uint64_t now){lastNowMs_=now;for(auto&d:dhts_)d.advance(now);for(std::size_t i=0;i<bindings_.size();++i)if(!bindings_[i].tracker)sources_[i].numRequests=dhts_[bindings_[i].index].numRequests();if(running_&&intervalActive_&&now>=lastIntervalMs_+30000){lastIntervalMs_=now;run(now);}}
void PeerSearch::emitPeer(std::size_t i,std::string address){if(closed_||i>=sources_.size())return;auto&s=sources_[i];if(uniquePeers_.insert(address).second)++s.numFoundUniq;++s.numFound;peerAdds_.push_back(std::move(address));}
void PeerSearch::close(){pause(lastNowMs_);for(auto&t:trackers_)t.close();for(auto&d:dhts_)d.close();intervalActive_=false;closed_=true;}
bool PeerSearch::isRunning()const noexcept{return running_;}bool PeerSearch::closed()const noexcept{return closed_;}bool PeerSearch::intervalActive()const noexcept{return intervalActive_;}
const std::vector<PeerSourceStats>&PeerSearch::stats()const noexcept{return sources_;}const std::vector<std::string>&PeerSearch::peerAdds()const noexcept{return peerAdds_;}
const AutonomyPolicy&PeerSearch::autonomyPolicy()const noexcept{return autonomy_;}
std::size_t PeerSearch::trackerRequests(std::size_t i)const{if(i>=bindings_.size()||!bindings_[i].tracker)throw std::out_of_range("tracker source index");return trackers_[bindings_[i].index].numRequests();}
std::size_t PeerSearch::dhtRequests(std::size_t i)const{if(i>=bindings_.size()||bindings_[i].tracker)throw std::out_of_range("dht source index");return dhts_[bindings_[i].index].numRequests();}
bool PeerSearch::dhtWaiting(std::size_t i)const{if(i>=bindings_.size()||bindings_[i].tracker)throw std::out_of_range("dht source index");return dhts_[bindings_[i].index].waiting();}
std::vector<std::string>PeerSearch::selectSources(const std::vector<std::string>&announces,const std::vector<std::string>&configured,const std::string&hash){if(announces.empty())return configured;std::vector<std::string>r;for(const auto&a:announces)r.push_back("tracker:"+a);r.push_back("dht:"+hash);return r;}
bool PeerSearch::internalDhtEnabled()noexcept{return false;}bool PeerSearch::internalTrackerEnabled()noexcept{return false;}
}
