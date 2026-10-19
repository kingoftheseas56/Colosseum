#include "server1/policy/SwarmConnections.h"
#include <iostream>
#include <stdexcept>
using namespace server1::policy;
using namespace server1::ports;
int main(){
    SwarmConnections s(2);bool paused=false;
    auto out=[&]{std::cout<<"{\"queued\":"<<s.queued()<<",\"unique\":"<<s.unique()<<",\"tries\":"<<s.tries()
        <<",\"connections\":"<<s.connections()<<",\"paused\":"<<(paused?"true":"false")<<"}\n";};
    auto add=[&](PeerHandle id){s.add({1,id,"127.0.0.1",static_cast<std::uint16_t>(1000+id)});out();};
    out();add(1);add(2);add(3);add(1);
    s.pause();paused=true;out();s.disconnected(1,0);out();
    s.resume();paused=false;out();s.handshake(2);out();s.disconnected(2,0);out();add(4);
    s.advance(4000);out();s.disconnected(3,4000);out();s.disconnected(2,4000);out();
    s.disconnected(4,4000);out();s.advance(11999);out();s.advance(12000);out();
    s.disconnected(2,12000);out();s.advance(24000);out();s.disconnected(2,24000);out();
    add(2);s.handshake(2);out();s.disconnected(2,24000);out();s.close();out();s.advance(28000);out();
    if(s.connections()||s.unique()||s.queued())throw std::runtime_error("close leaked ownership");
    SwarmConnections timed(1);
    timed.add({1,11,"127.0.0.1",1011},100);
    timed.advance(3099);if(!timed.takeDisconnects().empty())throw std::runtime_error("early connect timeout");
    timed.advance(3100);if(timed.takeDisconnects()!=std::vector<PeerHandle>{11})throw std::runtime_error("missing 3s connect timeout");
    timed.disconnected(11,3100);
    if(timed.unique()!=0 || timed.connections()!=0)throw std::runtime_error("failed pre-handshake peer retained");
    timed.add({1,12,"127.0.0.1",1011},3100);timed.connected(12);
    timed.advance(6100);if(!timed.takeDisconnects().empty())throw std::runtime_error("connected socket retained connect timeout");
    timed.advance(28100);if(timed.takeDisconnects()!=std::vector<PeerHandle>{12})throw std::runtime_error("missing 25s handshake timeout");
    timed.disconnected(12,28100);timed.handshake(99);
    if(timed.unique()!=0 || timed.connections()!=1)throw std::runtime_error("incoming connection must not create retained outgoing peer");
    timed.disconnected(99,28100);timed.pause();timed.add({1,13,"127.0.0.1",1013},30000);
    timed.advance(60000);if(!timed.takeDisconnects().empty())throw std::runtime_error("queued peer had dial timeout");
    timed.resume(60000);timed.advance(63000);
    if(timed.takeDisconnects()!=std::vector<PeerHandle>{13})throw std::runtime_error("resume must start connect timer at actual dial");

}
