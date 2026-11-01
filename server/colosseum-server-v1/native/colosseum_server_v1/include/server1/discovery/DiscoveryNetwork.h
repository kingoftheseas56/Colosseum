#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace server1::discovery {
struct DiscoveredPeer {
  std::size_t source;
  std::string address;
};
// Instance belongs to one PeerSearch. All methods execute on its event-loop
// thread.
class DiscoveryNetwork {
public:
  virtual ~DiscoveryNetwork() = default;
  virtual void prepareDht(std::size_t source, const std::string &hash) {}
  virtual void tracker(std::size_t source, const std::string &url,
                       const std::string &hash) = 0;
  virtual void lookup(std::size_t source, const std::string &hash) = 0;
  virtual void pauseDht(std::size_t source, std::uint64_t nowMs) = 0;
  virtual void advance(std::uint64_t nowMs) = 0;
  virtual std::vector<DiscoveredPeer> takePeers() = 0;
  virtual void close() noexcept = 0;
};
} // namespace server1::discovery
