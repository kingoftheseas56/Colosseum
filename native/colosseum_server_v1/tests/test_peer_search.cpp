#include "server1/discovery/PeerSearch.h"
#include "server1/policy/SwarmCaps.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using server1::discovery::DhtSource;
using server1::discovery::PeerSearch;
using server1::discovery::TrackerSource;
using server1::discovery::AutonomyPolicy;
using server1::policy::BufferSelection;
using server1::policy::SwarmCapOptions;
using server1::policy::SwarmCaps;

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

void caseK0901()
{
    PeerSearch search({"tracker:udp://one", "dht:abc"}, 40, 200, 0);
    expect(search.trackerRequests(0) == 1 && search.dhtWaiting(1),
           "K09-01 coordinator runs composed tracker and delayed DHT sources");
    search.tick(1500);
    expect(search.dhtRequests(1) == 1,
           "K09-01 coordinator advances the composed DHT source");
    search.onSwarmState(199, false, 1);
    expect(search.isRunning(), "K09-01 below max stays running");
    search.onSwarmState(200, false, 2);
    expect(search.isRunning(), "K09-01 equal max stays running");
    search.onSwarmState(201, false, 3);
    expect(!search.isRunning(), "K09-01 above max pauses");
    search.onSwarmState(40, false, 4);
    expect(!search.isRunning(), "K09-01 equal min stays paused");
    search.onSwarmState(39, false, 5);
    expect(search.isRunning(), "K09-01 below min runs");
    search.onSwarmState(39, true, 6);
    expect(!search.isRunning(), "K09-01 paused swarm pauses search");
    search.emitPeer(0, "1.2.3.4:5");
    search.emitPeer(1, "1.2.3.4:5");
    const auto stats = search.stats();
    expect(stats[0].numFoundUniq == 1 && stats[1].numFoundUniq == 0,
           "K09-01 peer uniqueness spans sources");
    const auto peerAdds = search.takePeerAdds();
    expect(peerAdds == std::vector<std::string>{"1.2.3.4:5"},
           "K09-01 duplicate peers across sources produce one actionable add");
    expect(search.takePeerAdds().empty(), "K09-01 actionable peer adds drain exactly once");
}

void caseK0902()
{
    const auto torrentSources = PeerSearch::selectSources(
        {"udp://torrent-one", "https://torrent-two"}, {"dht:configured"}, "abc");
    expect(torrentSources == std::vector<std::string>{"tracker:udp://torrent-one",
                                                       "tracker:https://torrent-two",
                                                       "dht:abc"},
           "K09-02 torrent announces override configured sources and append DHT");
    expect(PeerSearch::selectSources({}, {}, "abc").empty(), "K09-02 no sources stays empty");
    expect(!PeerSearch::internalDhtEnabled() && !PeerSearch::internalTrackerEnabled(),
           "K09-02 no duplicate native discovery underneath PeerSearch");
    for (const auto policy : {AutonomyPolicy{true, false, false},
                              AutonomyPolicy{false, true, false},
                              AutonomyPolicy{false, false, true}}) {
        bool rejected = false;
        try { PeerSearch forbidden({}, {}, {}, 0, policy); }
        catch (const std::invalid_argument &) { rejected = true; }
        expect(rejected, "K09-02 autonomous picker/discovery policy is actively rejected");
    }

    TrackerSource tracker("udp://failed", "abc");
    tracker.failNextRun();
    tracker.run();
    expect(tracker.numRequests() == 1 && tracker.lastRunFailed(),
           "K09-02 failed tracker remains counted and visible");

    DhtSource timeout("abc");
    timeout.run(0);
    timeout.advance(1499);
    expect(timeout.numRequests() == 0, "K09-02 DHT delay boundary");
    timeout.advance(1500);
    expect(timeout.numRequests() == 1, "K09-02 DHT lookup starts at timeout");

    DhtSource tornDown("abc");
    tornDown.run(0);
    tornDown.close();
    tornDown.advance(2000);
    expect(tornDown.numRequests() == 0 && tornDown.closed(),
           "K09-02 teardown wins over deferred discovery completion");
    PeerSearch search({}, 40, 200, 0);
    expect(search.autonomyPolicy().externallyControlled(),
           "K09-02 accepted coordinator exposes explicit external-control policy");
    search.close();
    expect(search.closed() && !search.intervalActive(), "K09-02 coordinator teardown stops interval");
}

void caseK0903()
{
    SwarmCapOptions speed{100.0, std::nullopt, 2};
    expect(!SwarmCaps::shouldPause(2, 101.0, {}, speed),
           "K09-03 exactly minPeers does not pause");
    expect(!SwarmCaps::shouldPause(3, 100.0, {}, speed),
           "K09-03 speed equality does not pause");
    expect(SwarmCaps::shouldPause(3, 101.0, {}, speed),
           "K09-03 just above min and speed pauses");

    SwarmCapOptions buffer{100.0, 0.5, 2};
    const std::vector<BufferSelection> half{{2, 1, 2, 4}};
    expect(!SwarmCaps::shouldPause(3, 1000.0, half, buffer),
           "K09-03 maxBuffer overrides speed condition and equality stays running");
    const std::vector<BufferSelection> full{{2, 2, 2, 4}};
    expect(SwarmCaps::shouldPause(3, 0.0, full, buffer),
           "K09-03 buffer above threshold pauses independent of speed");
    const std::vector<BufferSelection> ignored{{0, 9, 0, 0}};
    expect(!SwarmCaps::shouldPause(3, 1000.0, ignored, buffer),
           "K09-03 zero readFrom/selectTo selections are excluded");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        fail("expected one case id");
    }
    const std::string id = argv[1];
    if (id == "K09-01") {
        caseK0901();
    } else if (id == "K09-02") {
        caseK0902();
    } else if (id == "K09-03") {
        caseK0903();
    } else {
        fail("unknown case id");
    }
    std::cout << id << " PASS\n";
    return 0;
}
