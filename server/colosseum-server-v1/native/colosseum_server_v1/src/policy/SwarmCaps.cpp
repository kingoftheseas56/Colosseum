#include "server1/policy/SwarmCaps.h"
namespace server1::policy {
double SwarmCaps::bufferFullness(const std::vector<BufferSelection>&s){double sum=0;std::size_t count=0;for(const auto&x:s)if(x.readFrom>0&&x.selectTo>0){sum+=(static_cast<double>(x.from)+x.offset-x.readFrom)/(static_cast<double>(x.selectTo)-x.readFrom);++count;}return count?sum/count:0;}
bool SwarmCaps::shouldPause(std::size_t peers,double speed,const std::vector<BufferSelection>&s,const SwarmCapOptions&o){bool primary=true;if(o.maxSpeed&&*o.maxSpeed!=0)primary=speed>*o.maxSpeed;if(o.maxBuffer&&*o.maxBuffer!=0)primary=bufferFullness(s)>*o.maxBuffer;return primary&&peers>o.minPeers;}
}
