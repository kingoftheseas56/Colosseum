#if defined(K05_A2_NEGATE_COUNTS_TYPE)
#define PeerLifecycleCounts PeerLifecycleCounts_removed
#elif defined(K05_A2_NEGATE_QUEUED)
#define queued queued_removed
#elif defined(K05_A2_NEGATE_HANDSHAKING)
#define handshaking handshaking_removed
#elif defined(K05_A2_NEGATE_READY)
#define ready ready_removed
#elif defined(K05_A2_NEGATE_QUERY)
#define peerCounts peerCounts_removed
#endif

#include "server1/policy/SwarmPolicy.h"

#if defined(K05_A2_NEGATE_COUNTS_TYPE)
#undef PeerLifecycleCounts
#elif defined(K05_A2_NEGATE_QUEUED)
#undef queued
#elif defined(K05_A2_NEGATE_HANDSHAKING)
#undef handshaking
#elif defined(K05_A2_NEGATE_READY)
#undef ready
#elif defined(K05_A2_NEGATE_QUERY)
#undef peerCounts
#endif

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

using namespace server1::policy;

static_assert(std::is_same_v<decltype(PeerLifecycleCounts::queued), std::size_t>);
static_assert(std::is_same_v<decltype(PeerLifecycleCounts::handshaking), std::size_t>);
static_assert(std::is_same_v<decltype(PeerLifecycleCounts::ready), std::size_t>);
static_assert(std::is_same_v<decltype(&EngineSwarmRegistry::peerCounts),
    std::optional<PeerLifecycleCounts> (EngineSwarmRegistry::*)(
        const std::string &, std::uint64_t) const noexcept>);

int main()
{
    const PeerLifecycleCounts empty{};
    return empty.queued == 0 && empty.handshaking == 0 && empty.ready == 0 ? 0 : 1;
}
