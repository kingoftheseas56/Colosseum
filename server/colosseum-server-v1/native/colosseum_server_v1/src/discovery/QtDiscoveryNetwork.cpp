#include "server1/discovery/QtDiscoveryNetwork.h"
#include "server1/discovery/DhtLookup.h"
#include "server1/discovery/TrackerNetwork.h"
#include <QPointer>
#include <map>
#include <utility>
namespace server1::discovery {
struct QtDiscoveryNetwork::State {
  bool closed = false;
  std::vector<DiscoveredPeer> peers;
  std::map<std::size_t, QPointer<DhtLookup>> services;
  std::map<std::size_t, DhtLookup::LookupId> lookups;
  struct Retired {
    std::uint64_t deadline;
    QPointer<DhtLookup> service;
    DhtLookup::LookupId lookup;
  };
  std::vector<Retired> retired;
};
QtDiscoveryNetwork::QtDiscoveryNetwork(QObject *p)
    : QObject(p), state_(new State) {}
QtDiscoveryNetwork::~QtDiscoveryNetwork() { close(); }
void QtDiscoveryNetwork::tracker(std::size_t source, const std::string &url,
                                 const std::string &hash) {
  if (state_->closed)
    return;
  auto *client = new TrackerNetwork(QString::fromStdString(url),
                                    QString::fromStdString(hash), this);
  client->onPeer = [this, source](std::string peer) {
    if (!state_->closed)
      state_->peers.push_back({source, std::move(peer)});
  };
  client->start();
}
void QtDiscoveryNetwork::prepareDht(std::size_t source,
                                    const std::string &hash) {
  if (state_->closed || state_->services.count(source))
    return;
  auto *client = new DhtLookup(QString::fromStdString(hash), this);
  client->onPeer = [this, source](std::string peer) {
    if (!state_->closed)
      state_->peers.push_back({source, std::move(peer)});
  };
  state_->services[source] = client;
}
void QtDiscoveryNetwork::lookup(std::size_t source, const std::string &hash) {
  if (state_->closed)
    return;
  prepareDht(source, hash);
  if (auto *service = state_->services[source].data())
    state_->lookups[source] = service->startLookup();
}
void QtDiscoveryNetwork::pauseDht(std::size_t source, std::uint64_t now) {
  auto i = state_->lookups.find(source);
  if (i == state_->lookups.end())
    return;
  state_->retired.push_back({now + 1500, state_->services[source], i->second});
  state_->lookups.erase(i);
}
void QtDiscoveryNetwork::advance(std::uint64_t now) {
  auto &i = state_->retired;
  for (auto it = i.begin(); it != i.end();) {
    if (now >= it->deadline) {
      if (it->service)
        it->service->abortLookup(it->lookup);
      it = i.erase(it);
    } else
      ++it;
  }
}
std::vector<DiscoveredPeer> QtDiscoveryNetwork::takePeers() {
  std::vector<DiscoveredPeer> r;
  r.swap(state_->peers);
  return r;
}
void QtDiscoveryNetwork::close() noexcept {
  if (state_->closed)
    return;
  state_->closed = true;
  state_->peers.clear();
  state_->services.clear();
  state_->lookups.clear();
  state_->retired.clear();
  for (auto *c : children())
    delete c;
}
} // namespace server1::discovery
