#include "server1/discovery/PeerSearch.h"
#include <utility>
namespace server1::discovery {
TrackerSource::TrackerSource(std::string u,std::string h):url_(std::move(u)),infoHash_(std::move(h)){}
void TrackerSource::run(){if(!closed_){++numRequests_;lastRunFailed_=failNextRun_;failNextRun_=false;}}
void TrackerSource::pause()noexcept{} void TrackerSource::close()noexcept{closed_=true;}
void TrackerSource::failNextRun()noexcept{failNextRun_=true;}const std::string&TrackerSource::url()const noexcept{return url_;}
const std::string&TrackerSource::infoHash()const noexcept{return infoHash_;}std::size_t TrackerSource::numRequests()const noexcept{return numRequests_;}
bool TrackerSource::lastRunFailed()const noexcept{return lastRunFailed_;}bool TrackerSource::closed()const noexcept{return closed_;}
}
