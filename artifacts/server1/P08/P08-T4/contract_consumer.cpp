#if defined(P08_T4_NEGATE_PAUSE_ACTION)
#define PauseAction PauseAction_removed
#elif defined(P08_T4_NEGATE_PAUSE_GENERATION)
#define generation generation_removed
#elif defined(P08_T4_NEGATE_PAUSED_STATISTIC)
#define paused paused_removed
#elif defined(P08_T4_NEGATE_ACTION_VARIANT)
#define TorrentAction TorrentAction_removed
#endif

#include "server1/ports/TorrentTransport.h"

#if defined(P08_T4_NEGATE_PAUSE_ACTION)
#undef PauseAction
#elif defined(P08_T4_NEGATE_PAUSE_GENERATION)
#undef generation
#elif defined(P08_T4_NEGATE_PAUSED_STATISTIC)
#undef paused
#elif defined(P08_T4_NEGATE_ACTION_VARIANT)
#undef TorrentAction
#endif

#include <cstdint>
#include <type_traits>
#include <variant>

using namespace server1::ports;

static_assert(std::is_same_v<decltype(PauseAction::generation), EngineGeneration>);
static_assert(std::is_same_v<decltype(PauseAction::paused), bool>);
static_assert(std::variant_size_v<TorrentAction> == 6);
static_assert(std::is_same_v<std::variant_alternative_t<0, TorrentAction>, RequestAction>);
static_assert(std::is_same_v<std::variant_alternative_t<1, TorrentAction>, CancelAction>);
static_assert(std::is_same_v<std::variant_alternative_t<2, TorrentAction>, InterestAction>);
static_assert(std::is_same_v<std::variant_alternative_t<3, TorrentAction>, ChokeAction>);
static_assert(std::is_same_v<std::variant_alternative_t<4, TorrentAction>, ConnectAction>);
static_assert(std::is_same_v<std::variant_alternative_t<5, TorrentAction>, PauseAction>);
static_assert(std::is_same_v<decltype(TransportStatistics::paused), bool>);

int main()
{
    const TorrentAction pause = PauseAction{41, true};
    TransportStatistics statistics{};
    statistics.paused = std::get<PauseAction>(pause).paused;
    return statistics.paused && std::get<PauseAction>(pause).generation == 41 ? 0 : 1;
}
