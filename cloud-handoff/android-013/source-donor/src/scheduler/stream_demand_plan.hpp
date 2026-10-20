#ifndef NUVIO_ENGINE_STREAM_DEMAND_PLAN_HPP
#define NUVIO_ENGINE_STREAM_DEMAND_PLAN_HPP

#include "nuvio_engine/byte_range.hpp"
#include "nuvio_engine/piece_scheduler.hpp"

#include <cstdint>
#include <vector>

namespace nuvio::scheduler {

enum class DemandKind : std::uint8_t {
    reader,
    anchor,
    prefetch,
};

struct StreamDemand {
    std::uint64_t id;
    std::uint64_t file_offset;
    std::uint64_t file_size;
    std::uint32_t piece_size;
    http::ByteRange range;
    DemandKind kind = DemandKind::reader;
    bool cold = false;
};

struct StreamWindow {
    std::uint64_t critical_bytes;
    std::uint32_t critical_piece_limit;
    std::uint64_t playback_bytes;
    std::uint64_t readahead_bytes;
    std::uint64_t tail_bytes;
};

struct StreamDemandPlan {
    std::uint64_t focused_demand_id = 0;
    std::vector<PiecePriority> pieces;
    std::vector<std::uint32_t> deadline_order;
    bool cold = false;
};

[[nodiscard]] StreamDemandPlan build_stream_demand_plan(
    std::vector<StreamDemand> demands,
    const StreamWindow& window
);

}

#endif
