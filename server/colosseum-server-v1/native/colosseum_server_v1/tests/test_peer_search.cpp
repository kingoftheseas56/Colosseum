#include "server1/discovery/DiscoveryNetwork.h"
#include "server1/discovery/PeerSearch.h"
#include "server1/policy/SwarmCaps.h"
#include <memory>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using server1::discovery::AutonomyPolicy;
using server1::discovery::DhtSource;
using server1::discovery::PeerSearch;
using server1::discovery::TrackerSource;
using server1::policy::BufferSelection;
using server1::policy::SwarmCapOptions;
using server1::policy::SwarmCaps;

[[noreturn]] void fail(const std::string &message) {
  std::cerr << message << '\n';
  std::exit(1);
}

void expect(bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

class NetworkFixture final : public server1::discovery::DiscoveryNetwork {
public:
  std::size_t trackers = 0, lookups = 0, pauses = 0, prepared = 0;
  std::string hash;
  bool stopped = false;
  void prepareDht(std::size_t, const std::string &) override { ++prepared; }
  void tracker(std::size_t, const std::string &,
               const std::string &h) override {
    ++trackers;
    hash = h;
  }
  void lookup(std::size_t, const std::string &) override { ++lookups; }
  void pauseDht(std::size_t, std::uint64_t) override { ++pauses; }
  void advance(std::uint64_t) override {}
  std::vector<server1::discovery::DiscoveredPeer> takePeers() override {
    auto p = peers;
    peers.clear();
    return p;
  }
  void close() noexcept override { stopped = true; }
  std::vector<server1::discovery::DiscoveredPeer> peers;
};
void caseNetworkBinding() {
  auto network = std::make_shared<NetworkFixture>();
  PeerSearch search({"tracker:http://test/announce", "dht:abc"}, {}, {}, 0, {},
                    network, "explicit-hash");
  expect(network->prepared == 1,
         "DHT service constructed before delayed lookup");
  expect(network->trackers == 1 && network->hash == "explicit-hash",
         "live tracker starts with swarm hash");
  search.tick(1499);
  expect(network->lookups == 0, "DHT respects delayed source start");
  search.tick(1500);
  expect(network->lookups == 1, "DHT lookup runs once at source deadline");
  network->peers.push_back({0, "127.0.0.1:6881"});
  search.tick(1501);
  expect(search.takePeerAdds() == std::vector<std::string>{"127.0.0.1:6881"},
         "network peers enter coordinator statistics and peer queue");
  search.pause(1600);
  expect(network->pauses == 1, "DHT pause reaches executor");
  search.close();
  expect(network->stopped, "network executor closes with search");
}

void caseK0901() {
  PeerSearch pausedClock({"tracker:http://fixture"}, {}, {}, 0);
  pausedClock.pause(1);pausedClock.tick(30000);pausedClock.run(31000);pausedClock.tick(31001);
  expect(pausedClock.trackerRequests(0)==2,"paused source retains interval phase without catch-up announce");
  PeerSearch lateStart({"tracker:http://fixture"}, {}, {}, 100000);
  lateStart.tick(100001);
  expect(lateStart.trackerRequests(0)==1, "30-second interval is relative to source construction");
  lateStart.tick(129999);
  expect(lateStart.trackerRequests(0)==1, "no early interval rerun");
  lateStart.tick(130000);
  expect(lateStart.trackerRequests(0)==2, "interval reruns at construction plus 30 seconds");

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
  expect(peerAdds == std::vector<std::string>{"1.2.3.4:5", "1.2.3.4:5"},
         "K09-01 every source observation reaches swarm admission in order");
  expect(search.takePeerAdds().empty(),
         "K09-01 actionable peer adds drain exactly once");
}

void caseK0902() {
  const auto torrentSources = PeerSearch::selectSources(
      {"udp://torrent-one", "https://torrent-two"}, {"dht:configured"}, "abc");
  expect(torrentSources ==
             std::vector<std::string>{"tracker:udp://torrent-one",
                                      "tracker:https://torrent-two", "dht:abc"},
         "K09-02 torrent announces override configured sources and append DHT");
  expect(PeerSearch::selectSources({}, {}, "abc").empty(),
         "K09-02 no sources stays empty");
  expect(!PeerSearch::internalDhtEnabled() &&
             !PeerSearch::internalTrackerEnabled(),
         "K09-02 no duplicate native discovery underneath PeerSearch");
  for (const auto policy :
       {AutonomyPolicy{true, false, false}, AutonomyPolicy{false, true, false},
        AutonomyPolicy{false, false, true}}) {
    bool rejected = false;
    try {
      PeerSearch forbidden({}, {}, {}, 0, policy);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    expect(rejected,
           "K09-02 autonomous picker/discovery policy is actively rejected");
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
  expect(
      search.autonomyPolicy().externallyControlled(),
      "K09-02 accepted coordinator exposes explicit external-control policy");
  search.close();
  expect(search.closed() && !search.intervalActive(),
         "K09-02 coordinator teardown stops interval");
}

void caseK0903() {
  SwarmCapOptions speed{100.0, std::nullopt, 2};
  expect(!SwarmCaps::shouldPause(2, 101.0, {}, speed),
         "K09-03 exactly minPeers does not pause");
  expect(!SwarmCaps::shouldPause(3, 100.0, {}, speed),
         "K09-03 speed equality does not pause");
  expect(SwarmCaps::shouldPause(3, 101.0, {}, speed),
         "K09-03 just above min and speed pauses");

  SwarmCapOptions buffer{100.0, 0.5, 2};
  const std::vector<BufferSelection> half{{2, 1, 2, 4}};
  expect(
      !SwarmCaps::shouldPause(3, 1000.0, half, buffer),
      "K09-03 maxBuffer overrides speed condition and equality stays running");
  const std::vector<BufferSelection> full{{2, 2, 2, 4}};
  expect(SwarmCaps::shouldPause(3, 0.0, full, buffer),
         "K09-03 buffer above threshold pauses independent of speed");
  const std::vector<BufferSelection> ignored{{0, 9, 0, 0}};
  expect(!SwarmCaps::shouldPause(3, 1000.0, ignored, buffer),
         "K09-03 zero readFrom/selectTo selections are excluded");
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    fail("expected one case id");
  }
  const std::string id = argv[1];
  if (id == "K09-duplicate-trace") {
    PeerSearch search({"tracker:http://fixture", "dht:abc"}, {}, {}, 0);
    search.emitPeer(0, "1.2.3.4:5");
    search.emitPeer(1, "1.2.3.4:5");
    search.emitPeer(1, "2.3.4.5:6");
    search.emitPeer(0, "1.2.3.4:5");
    const auto adds = search.takePeerAdds();
    std::cout << "{\"adds\":[";
    for (std::size_t i = 0; i < adds.size(); ++i)
      std::cout << (i ? "," : "") << "\"" << adds[i] << "\"";
    std::cout << "],\"stats\":[";
    for (std::size_t i = 0; i < search.stats().size(); ++i) {
      const auto &v = search.stats()[i];
      std::cout << (i ? "," : "") << "[" << v.numFound << "," << v.numFoundUniq
                << "]";
    }
    std::cout << "]}\n";
    return 0;
  }

  if (id == "K09-01") {
    caseNetworkBinding();
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
