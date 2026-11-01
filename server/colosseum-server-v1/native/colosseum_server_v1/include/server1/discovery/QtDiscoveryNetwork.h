#pragma once
#include "server1/discovery/DiscoveryNetwork.h"
#include <QObject>
#include <memory>
namespace server1::discovery {
// Qt event-loop executor. Does not enable libtorrent's autonomous discovery.
class QtDiscoveryNetwork final : public QObject, public DiscoveryNetwork {
public:
  explicit QtDiscoveryNetwork(QObject *parent = nullptr);
  ~QtDiscoveryNetwork() override;
  void tracker(std::size_t, const std::string &, const std::string &) override;
  void prepareDht(std::size_t, const std::string &) override;
  void lookup(std::size_t, const std::string &) override;
  void pauseDht(std::size_t, std::uint64_t) override;
  void advance(std::uint64_t) override;
  std::vector<DiscoveredPeer> takePeers() override;
  void close() noexcept override;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace server1::discovery
