#include "test_support.hpp"

#include "scheduler/stream_demand_plan.hpp"

#include <algorithm>
#include <optional>

using nuvio::http::ByteRange;
using nuvio::scheduler::DemandKind;
using nuvio::scheduler::PiecePriority;
using nuvio::scheduler::PriorityClass;
using nuvio::scheduler::StreamDemand;
using nuvio::scheduler::StreamDemandPlan;
using nuvio::scheduler::StreamWindow;
using nuvio::scheduler::build_stream_demand_plan;

namespace {

constexpr std::uint64_t mebibyte = 1024ULL * 1024ULL;
constexpr std::uint64_t file_size = 64 * mebibyte;
constexpr std::uint32_t piece_size = static_cast<std::uint32_t>(mebibyte);
constexpr StreamWindow window{2 * mebibyte, 16, 10 * mebibyte, 0, 0};

StreamDemand demand(const std::uint64_t id, const std::uint32_t piece) {
    return {
        id,
        0,
        file_size,
        piece_size,
        ByteRange{
            static_cast<std::uint64_t>(piece) * piece_size,
            (static_cast<std::uint64_t>(piece) + 1) * piece_size - 1,
        },
    };
}

std::optional<PriorityClass> priority_for(
    const StreamDemandPlan& plan,
    const std::uint32_t piece
) {
    const auto found = std::ranges::find(plan.pieces, piece, &PiecePriority::piece);
    return found == plan.pieces.end()
        ? std::nullopt
        : std::optional(found->priority);
}

StreamDemandPlan three_range_plan(std::vector<StreamDemand> demands) {
    return build_stream_demand_plan(std::move(demands), window);
}

}

NUVIO_TEST("newest stream demand owns the critical window and readahead") {
    const auto plan = three_range_plan({demand(10, 0), demand(20, 63), demand(30, 32)});

    NUVIO_EXPECT_EQ(plan.focused_demand_id, std::uint64_t(30));
    NUVIO_EXPECT_EQ(
        plan.deadline_order,
        (std::vector<std::uint32_t>{32, 63, 0, 33, 34})
    );
    NUVIO_EXPECT_EQ(priority_for(plan, 0), std::optional(PriorityClass::blocking));
    NUVIO_EXPECT_EQ(priority_for(plan, 32), std::optional(PriorityClass::blocking));
    NUVIO_EXPECT_EQ(priority_for(plan, 63), std::optional(PriorityClass::blocking));
    NUVIO_EXPECT_EQ(priority_for(plan, 33), std::optional(PriorityClass::critical));
    NUVIO_EXPECT_EQ(priority_for(plan, 34), std::optional(PriorityClass::critical));
    for (std::uint32_t piece = 35; piece <= 44; ++piece) {
        NUVIO_EXPECT_EQ(priority_for(plan, piece), std::optional(PriorityClass::playback));
    }
    NUVIO_EXPECT_TRUE(!priority_for(plan, 1).has_value());
    NUVIO_EXPECT_TRUE(!priority_for(plan, 45).has_value());
    NUVIO_EXPECT_TRUE(!priority_for(plan, 62).has_value());
    NUVIO_EXPECT_EQ(plan.pieces.size(), std::size_t(15));
}

NUVIO_TEST("stream demand planning is independent of input iteration order") {
    const auto expected = three_range_plan({demand(10, 0), demand(20, 63), demand(30, 32)});
    const auto reversed = three_range_plan({demand(30, 32), demand(20, 63), demand(10, 0)});
    const auto shuffled = three_range_plan({demand(20, 63), demand(10, 0), demand(30, 32)});

    NUVIO_EXPECT_EQ(reversed.focused_demand_id, expected.focused_demand_id);
    NUVIO_EXPECT_EQ(reversed.deadline_order, expected.deadline_order);
    NUVIO_EXPECT_EQ(reversed.pieces, expected.pieces);
    NUVIO_EXPECT_EQ(shuffled.focused_demand_id, expected.focused_demand_id);
    NUVIO_EXPECT_EQ(shuffled.deadline_order, expected.deadline_order);
    NUVIO_EXPECT_EQ(shuffled.pieces, expected.pieces);
}

NUVIO_TEST("overlapping stream blockers are scheduled once") {
    const auto plan = three_range_plan({demand(10, 32), demand(20, 63), demand(30, 32)});

    NUVIO_EXPECT_EQ(plan.focused_demand_id, std::uint64_t(30));
    NUVIO_EXPECT_EQ(
        plan.deadline_order,
        (std::vector<std::uint32_t>{32, 63, 33, 34})
    );
    NUVIO_EXPECT_EQ(plan.pieces.size(), std::size_t(14));
}

NUVIO_TEST("a blocker inside the focused critical window stays blocking") {
    const auto plan = three_range_plan({demand(10, 33), demand(20, 32)});

    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{32, 33, 34}));
    NUVIO_EXPECT_EQ(priority_for(plan, 32), std::optional(PriorityClass::blocking));
    NUVIO_EXPECT_EQ(priority_for(plan, 33), std::optional(PriorityClass::blocking));
    NUVIO_EXPECT_EQ(priority_for(plan, 34), std::optional(PriorityClass::critical));
    NUVIO_EXPECT_EQ(priority_for(plan, 44), std::optional(PriorityClass::playback));
    NUVIO_EXPECT_TRUE(!priority_for(plan, 45).has_value());
}

NUVIO_TEST("ending the newest stream demand promotes the next live range") {
    const auto plan = three_range_plan({demand(10, 0), demand(20, 63)});

    NUVIO_EXPECT_EQ(plan.focused_demand_id, std::uint64_t(20));
    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{63, 0}));
    NUVIO_EXPECT_EQ(priority_for(plan, 0), std::optional(PriorityClass::blocking));
    NUVIO_EXPECT_EQ(priority_for(plan, 63), std::optional(PriorityClass::blocking));
    NUVIO_EXPECT_TRUE(!priority_for(plan, 1).has_value());
}

NUVIO_TEST("the critical window reaches the next physical piece") {
    constexpr std::uint32_t large_piece = 8 * static_cast<std::uint32_t>(mebibyte);
    const StreamDemand unaligned{
        1,
        0,
        128 * mebibyte,
        large_piece,
        ByteRange{6 * mebibyte, 8 * mebibyte - 1},
    };
    const auto plan = build_stream_demand_plan(
        {unaligned},
        StreamWindow{mebibyte, 16, 10 * mebibyte, 0, 0}
    );

    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{0, 1}));
    NUVIO_EXPECT_EQ(priority_for(plan, 0), std::optional(PriorityClass::blocking));
    NUVIO_EXPECT_EQ(priority_for(plan, 1), std::optional(PriorityClass::critical));
    NUVIO_EXPECT_EQ(priority_for(plan, 2), std::optional(PriorityClass::playback));
    NUVIO_EXPECT_TRUE(!priority_for(plan, 3).has_value());
}

NUVIO_TEST("the focused file tail is fetched without a deadline") {
    const auto plan = build_stream_demand_plan(
        {demand(1, 0)},
        StreamWindow{mebibyte, 16, 4 * mebibyte, 0, 2 * mebibyte}
    );

    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{0, 1}));
    NUVIO_EXPECT_EQ(priority_for(plan, 62), std::optional(PriorityClass::metadata_tail));
    NUVIO_EXPECT_EQ(priority_for(plan, 63), std::optional(PriorityClass::metadata_tail));
    NUVIO_EXPECT_TRUE(!priority_for(plan, 61).has_value());
}

NUVIO_TEST("a demand at the end of the file has no lookahead") {
    const auto plan = build_stream_demand_plan({demand(1, 63)}, window);

    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{63}));
    NUVIO_EXPECT_EQ(plan.pieces.size(), std::size_t(1));
}

NUVIO_TEST("the critical window is capped in pieces for small piece torrents") {
    const auto plan = build_stream_demand_plan(
        {demand(1, 0)},
        StreamWindow{8 * mebibyte, 3, 10 * mebibyte, 0, 0}
    );

    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{0, 1, 2, 3}));
    NUVIO_EXPECT_EQ(priority_for(plan, 4), std::optional(PriorityClass::playback));
}

NUVIO_TEST("an anchor keeps its window without reporting blocking pieces") {
    auto anchor = demand(1, 10);
    anchor.kind = DemandKind::anchor;
    const auto plan = build_stream_demand_plan({anchor}, window);

    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{10, 11, 12}));
    NUVIO_EXPECT_TRUE(std::ranges::none_of(plan.pieces, [](const PiecePriority& piece) {
        return piece.priority == PriorityClass::blocking;
    }));
    NUVIO_EXPECT_EQ(priority_for(plan, 10), std::optional(PriorityClass::critical));
    NUVIO_EXPECT_EQ(priority_for(plan, 13), std::optional(PriorityClass::playback));
}

NUVIO_TEST("a prefetch puts a deadline on the head piece only") {
    auto prefetch = demand(1, 0);
    prefetch.kind = DemandKind::prefetch;
    const auto plan = build_stream_demand_plan(
        {prefetch},
        StreamWindow{2 * mebibyte, 16, 4 * mebibyte, 0, 2 * mebibyte}
    );

    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{0}));
    NUVIO_EXPECT_EQ(priority_for(plan, 0), std::optional(PriorityClass::critical));
    NUVIO_EXPECT_EQ(priority_for(plan, 1), std::optional(PriorityClass::playback));
    NUVIO_EXPECT_EQ(priority_for(plan, 4), std::optional(PriorityClass::playback));
    NUVIO_EXPECT_EQ(priority_for(plan, 63), std::optional(PriorityClass::metadata_tail));
}

NUVIO_TEST("the readahead window follows the playback window") {
    const auto plan = build_stream_demand_plan(
        {demand(1, 0)},
        StreamWindow{mebibyte, 16, 2 * mebibyte, 3 * mebibyte, 0}
    );

    NUVIO_EXPECT_EQ(priority_for(plan, 1), std::optional(PriorityClass::critical));
    NUVIO_EXPECT_EQ(priority_for(plan, 2), std::optional(PriorityClass::playback));
    NUVIO_EXPECT_EQ(priority_for(plan, 3), std::optional(PriorityClass::playback));
    NUVIO_EXPECT_EQ(priority_for(plan, 4), std::optional(PriorityClass::readahead));
    NUVIO_EXPECT_EQ(priority_for(plan, 6), std::optional(PriorityClass::readahead));
    NUVIO_EXPECT_TRUE(!priority_for(plan, 7).has_value());
}

NUVIO_TEST("a cold reader withholds its critical window until it has data") {
    auto cold = demand(1, 20);
    cold.cold = true;
    const auto plan = build_stream_demand_plan({cold}, window);

    NUVIO_EXPECT_TRUE(plan.cold);
    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{20}));
    NUVIO_EXPECT_EQ(priority_for(plan, 20), std::optional(PriorityClass::blocking));
    NUVIO_EXPECT_EQ(priority_for(plan, 21), std::optional(PriorityClass::playback));

    const auto warm = build_stream_demand_plan({demand(1, 20)}, window);
    NUVIO_EXPECT_TRUE(!warm.cold);
    NUVIO_EXPECT_EQ(warm.deadline_order, (std::vector<std::uint32_t>{20, 21, 22}));
}

NUVIO_TEST("reading the index tail keeps the file head downloading") {
    const auto plan = build_stream_demand_plan(
        {demand(1, 62)},
        StreamWindow{mebibyte, 16, 2 * mebibyte, 0, 4 * mebibyte}
    );

    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{62, 63}));
    NUVIO_EXPECT_EQ(priority_for(plan, 0), std::optional(PriorityClass::playback));
    NUVIO_EXPECT_EQ(priority_for(plan, 2), std::optional(PriorityClass::playback));
    NUVIO_EXPECT_TRUE(!priority_for(plan, 3).has_value());

    const auto index_region = build_stream_demand_plan(
        {demand(1, 63)},
        StreamWindow{mebibyte, 16, 2 * mebibyte, 0, mebibyte / 2}
    );
    NUVIO_EXPECT_EQ(priority_for(index_region, 0), std::optional(PriorityClass::playback));

    const auto middle = build_stream_demand_plan(
        {demand(1, 30)},
        StreamWindow{mebibyte, 16, 2 * mebibyte, 0, 4 * mebibyte}
    );
    NUVIO_EXPECT_TRUE(!priority_for(middle, 0).has_value());
}

NUVIO_TEST("only readers keep blocking pieces beside the focused demand") {
    auto anchor = demand(1, 5);
    anchor.kind = DemandKind::anchor;
    const auto plan = build_stream_demand_plan({anchor, demand(2, 40)}, window);

    NUVIO_EXPECT_EQ(plan.focused_demand_id, std::uint64_t(2));
    NUVIO_EXPECT_EQ(plan.deadline_order, (std::vector<std::uint32_t>{40, 41, 42}));
    NUVIO_EXPECT_TRUE(!priority_for(plan, 5).has_value());
}

NUVIO_TEST("no demands produce an empty plan") {
    const auto plan = build_stream_demand_plan({}, window);

    NUVIO_EXPECT_EQ(plan.focused_demand_id, std::uint64_t(0));
    NUVIO_EXPECT_TRUE(plan.pieces.empty());
    NUVIO_EXPECT_TRUE(plan.deadline_order.empty());
}
