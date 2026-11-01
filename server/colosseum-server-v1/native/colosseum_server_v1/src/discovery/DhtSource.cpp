#include "server1/discovery/PeerSearch.h"
#include <utility>
namespace server1::discovery {
DhtSource::DhtSource(std::string h):infoHash_(std::move(h)){}void DhtSource::run(std::uint64_t now){if(!closed_&&!lookupActive_&&!waiting_){waiting_=true;lookupAtMs_=now+1500;}}
void DhtSource::pause(std::uint64_t now){if(waiting_)waiting_=false;if(lookupActive_){lookupActive_=false;abortPending_=true;abortAtMs_=now+1500;}}
void DhtSource::advance(std::uint64_t now){if(closed_)return;if(waiting_&&now>=lookupAtMs_){waiting_=false;lookupActive_=true;++numRequests_;}if(abortPending_&&now>=abortAtMs_)abortPending_=false;}
void DhtSource::close()noexcept{waiting_=lookupActive_=abortPending_=false;closed_=true;}const std::string&DhtSource::infoHash()const noexcept{return infoHash_;}
std::size_t DhtSource::numRequests()const noexcept{return numRequests_;}bool DhtSource::waiting()const noexcept{return waiting_;}bool DhtSource::lookupActive()const noexcept{return lookupActive_;}
bool DhtSource::abortPending()const noexcept{return abortPending_;}bool DhtSource::closed()const noexcept{return closed_;}
}
