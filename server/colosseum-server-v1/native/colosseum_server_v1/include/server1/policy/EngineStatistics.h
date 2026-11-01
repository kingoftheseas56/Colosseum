#pragma once
#include "server1/policy/TorrentMetadata.h"
#include <optional>
#include <set>
namespace server1::policy {
// Source-facing M172 snapshot. Transport measurements must be supplied by the
// owner; this projector never invents native counters or substitutes rates.
struct EngineStatisticsSnapshot final {
    std::string infoHash;
    std::optional<std::string> name;
    std::optional<std::vector<TorrentFile>> files;
    std::uint64_t peers=0, unchoked=0, queued=0, unique=0, connectionTries=0;
    bool swarmPaused=false;
    Value swarmSize=Value::number(100);
    std::uint64_t swarmConnections=0, downloaded=0, uploaded=0;
    Value selections=Value::array({}), wires=Value::array({});
    double downloadSpeed=0;
    Value sources=Value::missing(), peerSearchRunning=Value::missing();
    Value options=Value::object({});
    std::uint64_t pieceLength=0;
    std::set<std::size_t> availablePieces;
};
[[nodiscard]] Value projectEngineStatistics(const EngineStatisticsSnapshot &snapshot,
                                             const Value &index=Value::missing());
}
