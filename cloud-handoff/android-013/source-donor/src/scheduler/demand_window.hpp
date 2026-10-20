#ifndef NUVIO_ENGINE_DEMAND_WINDOW_HPP
#define NUVIO_ENGINE_DEMAND_WINDOW_HPP

#include "nuvio_engine/byte_range.hpp"

#include <cstdint>

namespace nuvio::scheduler {

[[nodiscard]] http::ByteRange blocking_demand_range(
    http::ByteRange response_range,
    std::uint64_t position,
    std::uint64_t file_offset,
    std::uint32_t piece_size
);

}

#endif
