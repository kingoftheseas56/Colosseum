#include "server1/discovery/PeerSearch.h"
#include <stdexcept>
#include <utility>
namespace server1::discovery {
PeerSearch::PeerSearch(std::vector<std::string> sourceNames,
                       std::optional<std::size_t> min,
                       std::optional<std::size_t> max, std::uint64_t now,
                       AutonomyPolicy autonomy,
                       std::shared_ptr<DiscoveryNetwork> network,
                       std::string infoHash)
    : network_(std::move(network)), minimum_(min), maximum_(max),
      autonomy_(autonomy) {
  if (!autonomy_.externallyControlled())
    throw std::invalid_argument("autonomous picker/discovery is forbidden");
  std::string hash = std::move(infoHash);
  for (const auto &s : sourceNames)
    if (hash.empty() && s.rfind("dht:", 0) == 0) {
      hash = s.substr(4);
      break;
    }
  for (auto &source : sourceNames) {
    if (source.rfind("tracker:", 0) == 0) {
      const auto i = trackers_.size();
      trackers_.emplace_back(source.substr(8), hash);
      bindings_.push_back({true, i});
      sources_.push_back({0, 0, 0, std::move(source), 0});
    } else if (source.rfind("dht:", 0) == 0) {
      const auto i = dhts_.size();
      dhts_.emplace_back(source.substr(4));
      if (network_)
        network_->prepareDht(sources_.size(), dhts_.back().infoHash());
      bindings_.push_back({false, i});
      sources_.push_back({0, 0, 0, std::move(source), 0});
    }
  }
  lastIntervalMs_ = now;
  run(now);
}
PeerSearch::~PeerSearch() { close(); }
void PeerSearch::run(std::uint64_t now) {
  if (closed_)
    return;
  running_ = true;
  lastNowMs_ = now;
  for (std::size_t i = 0; i < sources_.size(); ++i) {
    sources_[i].lastStartedMs = now;
    const auto b = bindings_[i];
    if (b.tracker) {
      trackers_[b.index].run();
      if (network_)
        network_->tracker(i, trackers_[b.index].url(),
                          trackers_[b.index].infoHash());
      sources_[i].numRequests = trackers_[b.index].numRequests();
    } else
      dhts_[b.index].run(now);
  }
}
void PeerSearch::pause(std::uint64_t now) noexcept {
  running_ = false;
  lastNowMs_ = now;
  for (auto &t : trackers_)
    t.pause();
  for (std::size_t i = 0; i < bindings_.size(); ++i) {
    const auto b = bindings_[i];
    if (!b.tracker) {
      if (network_ && dhts_[b.index].lookupActive())
        network_->pauseDht(i, now);
      dhts_[b.index].pause(now);
    }
  }
}
void PeerSearch::onSwarmState(std::size_t queued, bool swarmPaused,
                              std::uint64_t now) {
  if (swarmPaused && running_)
    pause(now);
  else if (minimum_ && queued < *minimum_ && !running_)
    run(now);
  else if (maximum_ && queued > *maximum_ && running_)
    pause(now);
}
void PeerSearch::tick(std::uint64_t now) {
  lastNowMs_ = now;
  for (std::size_t i = 0; i < bindings_.size(); ++i) {
    const auto b = bindings_[i];
    if (!b.tracker) {
      auto &d = dhts_[b.index];
      auto requests = d.numRequests();
      d.advance(now);
      if (network_ && d.numRequests() != requests)
        network_->lookup(i, d.infoHash());
    }
  }
  if (network_) {
    network_->advance(now);
    for (auto &peer : network_->takePeers())
      emitPeer(peer.source, std::move(peer.address));
  }
  for (std::size_t i = 0; i < bindings_.size(); ++i)
    if (!bindings_[i].tracker)
      sources_[i].numRequests = dhts_[bindings_[i].index].numRequests();
  if (intervalActive_ && now >= lastIntervalMs_ && now-lastIntervalMs_ >= 30000) {
    // The oracle's setInterval remains scheduled while paused; only its run
    // body is conditional. Never replay a missed announce on resume.
    lastIntervalMs_ += ((now-lastIntervalMs_)/30000)*30000;
    if (running_) run(now);
  }
}
void PeerSearch::emitPeer(std::size_t i, std::string address) {
  if (closed_ || i >= sources_.size())
    return;
  auto &s = sources_[i];
  const bool unique = uniquePeers_.insert(address).second;
  if (unique) {
    ++s.numFoundUniq;
  }
  // M612 calls swarm.add on every observation; the swarm, not the source
  // lifetime uniqueness table, decides whether the endpoint is already
  // retained.
  peerAdds_.push_back(std::move(address));
  ++s.numFound;
}
void PeerSearch::close() {
  if (closed_)
    return;
  if (network_)
    network_->close();
  pause(lastNowMs_);
  for (auto &t : trackers_)
    t.close();
  for (auto &d : dhts_)
    d.close();
  intervalActive_ = false;
  closed_ = true;
}
bool PeerSearch::isRunning() const noexcept { return running_; }
bool PeerSearch::closed() const noexcept { return closed_; }
bool PeerSearch::intervalActive() const noexcept { return intervalActive_; }
const std::vector<PeerSourceStats> &PeerSearch::stats() const noexcept {
  return sources_;
}
const std::vector<std::string> &PeerSearch::peerAdds() const noexcept {
  return peerAdds_;
}
std::vector<std::string> PeerSearch::takePeerAdds() {
  std::vector<std::string> result;
  result.swap(peerAdds_);
  return result;
}
const AutonomyPolicy &PeerSearch::autonomyPolicy() const noexcept {
  return autonomy_;
}
std::size_t PeerSearch::trackerRequests(std::size_t i) const {
  if (i >= bindings_.size() || !bindings_[i].tracker)
    throw std::out_of_range("tracker source index");
  return trackers_[bindings_[i].index].numRequests();
}
std::size_t PeerSearch::dhtRequests(std::size_t i) const {
  if (i >= bindings_.size() || bindings_[i].tracker)
    throw std::out_of_range("dht source index");
  return dhts_[bindings_[i].index].numRequests();
}
bool PeerSearch::dhtWaiting(std::size_t i) const {
  if (i >= bindings_.size() || bindings_[i].tracker)
    throw std::out_of_range("dht source index");
  return dhts_[bindings_[i].index].waiting();
}
std::vector<std::string>
PeerSearch::selectSources(const std::vector<std::string> &announces,
                          const std::vector<std::string> &configured,
                          const std::string &hash) {
  if (announces.empty())
    return configured;
  std::vector<std::string> r;
  for (const auto &a : announces)
    r.push_back("tracker:" + a);
  r.push_back("dht:" + hash);
  return r;
}
bool PeerSearch::internalDhtEnabled() noexcept { return false; }
bool PeerSearch::internalTrackerEnabled() noexcept { return false; }
} // namespace server1::discovery
