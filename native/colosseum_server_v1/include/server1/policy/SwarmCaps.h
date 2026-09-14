#pragma once
#include <cstddef>
#include <optional>
#include <vector>
namespace server1::policy {
struct BufferSelection final {std::size_t from=0,offset=0,readFrom=0,selectTo=0;};
struct SwarmCapOptions final {std::optional<double>maxSpeed,maxBuffer;std::size_t minPeers=0;};
class SwarmCaps final {public:static double bufferFullness(const std::vector<BufferSelection>&);
static bool shouldPause(std::size_t,double,const std::vector<BufferSelection>&,const SwarmCapOptions&);};
}
