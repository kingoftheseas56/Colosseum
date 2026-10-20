#ifndef NUVIO_ENGINE_PIECE_SCHEDULER_HPP
#define NUVIO_ENGINE_PIECE_SCHEDULER_HPP

#include <cstdint>
#include <vector>

#include "nuvio_engine/byte_range.hpp"
#include "nuvio_engine/export.h"

namespace nuvio::scheduler {

enum class PriorityClass : std::uint8_t {
    blocking = 0,
    critical = 1,
    playback = 2,
    metadata_tail = 3,
    readahead = 4,
};

struct PiecePriority {
    std::uint32_t piece;
    PriorityClass priority;

    [[nodiscard]] bool operator==(const PiecePriority&) const = default;
};

struct ScheduleRequest {
    std::uint64_t file_offset;
    std::uint64_t file_size;
    std::uint32_t piece_size;
    http::ByteRange demand;
    std::uint64_t critical_bytes;
    std::uint64_t playback_bytes;
    std::uint64_t readahead_bytes;
    std::uint64_t metadata_tail_bytes;
};

[[nodiscard]] NUVIO_ENGINE_CPP_API std::vector<PiecePriority> build_piece_schedule(
    const ScheduleRequest& request
);

}

#endif
