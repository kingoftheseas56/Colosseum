#include "nuvio_engine/piece_scheduler.hpp"

#include "scheduler/demand_window.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>

namespace nuvio::scheduler {
namespace {

std::uint64_t saturating_add(const std::uint64_t left, const std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

http::ByteRange extend_from(
    const std::uint64_t start,
    const std::uint64_t additional_bytes,
    const std::uint64_t file_size
) {
    const auto exclusive_end = std::min(saturating_add(start, additional_bytes), file_size);
    return {start, exclusive_end > start ? exclusive_end - 1 : start};
}

void add_range(
    std::map<std::uint32_t, PriorityClass>& priorities,
    const ScheduleRequest& request,
    const http::ByteRange range,
    const PriorityClass priority
) {
    if (request.file_size == 0 || range.start >= request.file_size) {
        return;
    }
    const auto clamped_end = std::min(range.end, request.file_size - 1);
    const auto absolute_start = saturating_add(request.file_offset, range.start);
    const auto absolute_end = saturating_add(request.file_offset, clamped_end);
    const auto first_piece = absolute_start / request.piece_size;
    const auto last_piece = absolute_end / request.piece_size;
    if (last_piece > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("piece index exceeds 32-bit libtorrent range");
    }
    for (auto piece = first_piece; piece <= last_piece; ++piece) {
        const auto index = static_cast<std::uint32_t>(piece);
        const auto existing = priorities.find(index);
        if (existing == priorities.end() || priority < existing->second) {
            priorities[index] = priority;
        }
    }
}

}

http::ByteRange blocking_demand_range(
    const http::ByteRange response_range,
    const std::uint64_t position,
    const std::uint64_t file_offset,
    const std::uint32_t piece_size
) {
    if (piece_size == 0) {
        throw std::invalid_argument("piece size must be greater than zero");
    }
    if (response_range.start > response_range.end ||
        position < response_range.start || position > response_range.end) {
        throw std::invalid_argument("position must intersect the response range");
    }

    const auto piece_size_64 = static_cast<std::uint64_t>(piece_size);
    const auto offset_in_piece = (
        (file_offset % piece_size_64) + (position % piece_size_64)
    ) % piece_size_64;
    const auto remaining_in_piece = piece_size_64 - offset_in_piece;
    return {
        position,
        std::min(
            saturating_add(position, remaining_in_piece - 1),
            response_range.end
        ),
    };
}

std::vector<PiecePriority> build_piece_schedule(const ScheduleRequest& request) {
    if (request.piece_size == 0) {
        throw std::invalid_argument("piece size must be greater than zero");
    }
    if (request.file_size == 0) {
        return {};
    }
    if (request.demand.start > request.demand.end || request.demand.start >= request.file_size) {
        throw std::invalid_argument("demand must intersect the selected file");
    }

    std::map<std::uint32_t, PriorityClass> priorities;
    add_range(priorities, request, request.demand, PriorityClass::blocking);

    auto next_start = std::min(saturating_add(request.demand.end, 1), request.file_size);
    if (next_start < request.file_size && request.critical_bytes > 0) {
        const auto range = extend_from(next_start, request.critical_bytes, request.file_size);
        add_range(priorities, request, range, PriorityClass::critical);
        next_start = std::min(saturating_add(range.end, 1), request.file_size);
    }
    if (next_start < request.file_size && request.playback_bytes > 0) {
        const auto range = extend_from(next_start, request.playback_bytes, request.file_size);
        add_range(priorities, request, range, PriorityClass::playback);
        next_start = std::min(saturating_add(range.end, 1), request.file_size);
    }
    if (next_start < request.file_size && request.readahead_bytes > 0) {
        const auto range = extend_from(next_start, request.readahead_bytes, request.file_size);
        add_range(priorities, request, range, PriorityClass::readahead);
    }
    if (request.metadata_tail_bytes > 0) {
        const auto tail_size = std::min(request.metadata_tail_bytes, request.file_size);
        add_range(
            priorities,
            request,
            {request.file_size - tail_size, request.file_size - 1},
            PriorityClass::metadata_tail
        );
    }

    std::vector<PiecePriority> schedule;
    schedule.reserve(priorities.size());
    for (const auto& [piece, priority] : priorities) {
        schedule.push_back({piece, priority});
    }
    std::ranges::stable_sort(schedule, {}, &PiecePriority::priority);
    return schedule;
}

}
