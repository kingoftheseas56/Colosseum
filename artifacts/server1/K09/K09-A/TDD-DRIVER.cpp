#include "../../../../native/colosseum_server_v1/src/discovery/TrackerSource.cpp"
#include "../../../../native/colosseum_server_v1/src/discovery/DhtSource.cpp"
#include "../../../../native/colosseum_server_v1/src/discovery/PeerSearch.cpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using server1::discovery::DhtSource;
using server1::discovery::PeerSearch;
using server1::discovery::TrackerSource;

[[noreturn]] void fail(const std::string &message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string &message)
{
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main()
{
    PeerSearch search({"tracker:udp://one", "dht:abc"}, 40, 200, 0);
    expect(search.isRunning(), "K09-A starts discovery");
    search.onSwarmState(200, false, 1);
    expect(search.isRunning(), "K09-01 queued == max remains running");
    search.onSwarmState(201, false, 2);
    expect(!search.isRunning(), "K09-01 queued > max pauses");
    search.onSwarmState(40, false, 3);
    expect(!search.isRunning(), "K09-01 queued == min remains paused");
    search.onSwarmState(39, false, 4);
    expect(search.isRunning(), "K09-01 queued < min resumes");
    search.onSwarmState(39, true, 5);
    expect(!search.isRunning(), "K09-01 paused swarm pauses running search");

    search.emitPeer(0, "1.2.3.4:5");
    search.emitPeer(0, "1.2.3.4:5");
    search.emitPeer(1, "1.2.3.4:5");
    const auto stats = search.stats();
    expect(stats[0].numFound == 2 && stats[0].numFoundUniq == 1,
           "K09-01 first source total and unique counts");
    expect(stats[1].numFound == 1 && stats[1].numFoundUniq == 0,
           "K09-01 uniqueness is coordinator-wide across sources");
    expect(search.peerAdds().size() == 3, "K09-01 each source event reaches explicit swarm add port");

    TrackerSource tracker("udp://one", "abc");
    tracker.run();
    tracker.run();
    expect(tracker.numRequests() == 2, "K09-A tracker counts each run");
    tracker.pause();
    expect(!tracker.closed(), "K09-A tracker pause is source-compatible no-op");

    DhtSource dht("abc");
    dht.run(0);
    expect(dht.waiting() && dht.numRequests() == 0, "K09-A DHT defers lookup 1500ms");
    dht.advance(1499);
    expect(dht.numRequests() == 0, "K09-A DHT does not start early");
    dht.pause(1499);
    expect(!dht.waiting(), "K09-A pause cancels deferred lookup");
    dht.run(2000);
    dht.advance(3500);
    expect(dht.numRequests() == 1 && dht.lookupActive(), "K09-A DHT starts at timeout");
    dht.pause(3500);
    expect(!dht.lookupActive() && dht.abortPending(), "K09-A pause schedules active lookup abort");
    dht.advance(5000);
    expect(!dht.abortPending(), "K09-A scheduled abort completes");
    dht.close();
    expect(dht.closed(), "K09-A DHT close destroys source");

    search.close();
    expect(search.closed() && !search.intervalActive(), "K09-A close stops sources and interval");
    std::cout << "K09-A PASS\n";
    return 0;
}
