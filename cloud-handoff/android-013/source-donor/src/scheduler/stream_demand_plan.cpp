#include "scheduler/stream_demand_plan.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace nuvio::scheduler {
namespace {

void merge_schedule(
    std::map<std::uint32_t, PriorityClass>& combined,
    const std::vector<PiecePriority>& schedule
) {
    for (const auto& priority : schedule) {
        const auto existing = combined.find(priority.piece);
        if (existing == combined.end() || priority.priority < existing->second) {
            combined[priority.piece] = priority.priority;
        }
    }
}

}

StreamDemandPlan build_stream_demand_plan(
    std::vector<StreamDemand> demands,
    const StreamWindow& window
) {
    StreamDemandPlan result;
    if (demands.empty()) {
        return result;
    }

    std::ranges::sort(demands, [](const StreamDemand& left, const StreamDemand& right) {
        return left.id < right.id;
    });

    const auto& focused = demands.back();
    result.focused_demand_id = focused.id;
    result.cold = focused.kind == DemandKind::prefetch || focused.cold;
    const auto critical_bytes = result.cold
        ? std::uint64_t{0}
        : std::min(
              window.critical_bytes,
              static_cast<std::uint64_t>(window.critical_piece_limit) * focused.piece_size
          );
    auto focused_schedule = build_piece_schedule({
        focused.file_offset,
        focused.file_size,
        focused.piece_size,
        focused.range,
        critical_bytes,
        window.playback_bytes,
        window.readahead_bytes,
        window.tail_bytes,
    });
    if (focused.kind != DemandKind::reader) {
        for (auto& priority : focused_schedule) {
            if (priority.priority == PriorityClass::blocking) {
                priority.priority = PriorityClass::critical;
            }
        }
    }

    std::map<std::uint32_t, PriorityClass> combined;
    merge_schedule(combined, focused_schedule);
    const auto index_bytes = std::min(
        std::max(window.tail_bytes, focused.file_size / 64),
        focused.file_size
    );
    if (window.tail_bytes > 0 && focused.range.start >= focused.file_size - index_bytes) {
        const auto head_bytes = std::min(
            critical_bytes + window.playback_bytes,
            focused.file_size
        );
        if (head_bytes > 0) {
            auto head = build_piece_schedule({
                focused.file_offset,
                focused.file_size,
                focused.piece_size,
                http::ByteRange{0, head_bytes - 1},
                0,
                0,
                0,
                0,
            });
            for (auto& priority : head) {
                priority.priority = PriorityClass::playback;
            }
            merge_schedule(combined, head);
        }
    }

    std::set<std::uint32_t> ordered;
    for (const auto& priority : focused_schedule) {
        if (priority.priority == PriorityClass::blocking &&
            ordered.insert(priority.piece).second) {
            result.deadline_order.push_back(priority.piece);
        }
    }
    for (auto demand = std::next(demands.rbegin()); demand != demands.rend(); ++demand) {
        if (demand->kind != DemandKind::reader) {
            continue;
        }
        const auto schedule = build_piece_schedule({
            demand->file_offset,
            demand->file_size,
            demand->piece_size,
            demand->range,
            0,
            0,
            0,
            0,
        });
        merge_schedule(combined, schedule);
        for (const auto& priority : schedule) {
            if (priority.priority == PriorityClass::blocking &&
                ordered.insert(priority.piece).second) {
                result.deadline_order.push_back(priority.piece);
            }
        }
    }
    for (const auto& [piece, priority] : combined) {
        if (priority == PriorityClass::critical && ordered.insert(piece).second) {
            result.deadline_order.push_back(piece);
        }
    }

    result.pieces.reserve(combined.size());
    for (const auto& [piece, priority] : combined) {
        result.pieces.push_back({piece, priority});
    }
    std::ranges::stable_sort(result.pieces, {}, &PiecePriority::priority);
    return result;
}

}
