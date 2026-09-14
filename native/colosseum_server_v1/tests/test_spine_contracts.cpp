#include "server1/discovery/PeerSearch.h"
#include "server1/policy/CircularPieceStore.h"
#include "server1/policy/MetadataExchange.h"
#include "server1/policy/PieceBuffer.h"
#include "server1/policy/PieceStore.h"
#include "server1/policy/Scheduler.h"
#include "server1/policy/SchedulerActions.h"
#include "server1/policy/SwarmCaps.h"
#include "server1/policy/SwarmPolicy.h"
#include "server1/ports/TorrentTransport.h"

#include <filesystem>
#include <type_traits>

namespace {
class TransportConsumer final : public server1::ports::TorrentTransport {
public:
    bool submit(const server1::ports::TorrentAction &) override { return true; }
    std::vector<server1::ports::TorrentObservation> poll() override { return {}; }
    server1::ports::TransportStatistics statistics() const override { return {}; }
    void close() override {}
protected:
    bool applyAutonomySuppression() override { return true; }
};
}

int main()
{
    using namespace server1::policy;
    PieceBuffer piece(1, 1);
    if (piece.reserve() != 0) return 1;
    Scheduler scheduler(1);
    const auto selection = scheduler.select(0, 0, Value::number(1));
    SchedulerActionContract actions;
    if (!actions.track({1, 1, selection, 12, 0, 0, 0, 1})) return 2;
    const auto schedulerActions = actions.decide({12, 1, 1, 0, 0.0},
        {{selection, 1, 0, 0, 0, 1, true, false}}, {});
    if (schedulerActions.size() != 1) return 8;
    const auto torrentAction = server1::ports::toTorrentAction(schedulerActions[0]);
    if (!torrentAction || !std::holds_alternative<server1::ports::RequestAction>(*torrentAction)) return 9;
    const auto &converted = std::get<server1::ports::RequestAction>(*torrentAction);
    if (converted.ownership.generation != 1 || converted.ownership.selectionId != selection
        || converted.peer != 12 || converted.block.piece != 0
        || converted.block.blockOrdinal != 0 || converted.block.length != 1) return 10;
    MetadataExchange metadata("0000000000000000000000000000000000000000");
    if (!metadata.advertise(1)) return 3;
    SwarmPolicy swarm(10, 1);
    swarm.addPeer({"peer"});
    PersistentPieceStore persistent(std::filesystem::temp_directory_path() / "server1-spine-smoke",
                                    1, 1, 1, {{0, 1}}, {""});
    CircularPieceStore circular({}, CircularStoreMode::Memory, 1, 1);
    if (circular.capacity() != 1) return 4;
    server1::discovery::PeerSearch search({}, {}, {}, 0);
    if (!search.autonomyPolicy().externallyControlled()) return 5;
    if (SwarmCaps::bufferFullness({}) != 0.0) return 6;
    TransportConsumer transport;
    if (!transport.configureAutonomy({})) return 7;
    static_assert(std::is_abstract_v<server1::ports::TorrentTransport>);
    return 0;
}
