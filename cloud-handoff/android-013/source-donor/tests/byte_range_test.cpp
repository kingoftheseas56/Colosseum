#include "test_support.hpp"

#include "nuvio_engine/byte_range.hpp"

using nuvio::http::ByteRange;
using nuvio::http::RangeStatus;
using nuvio::http::parse_range_header;

NUVIO_TEST("missing range selects the full resource") {
    const auto result = parse_range_header("", 1000);
    NUVIO_EXPECT_EQ(result.status, RangeStatus::full);
    NUVIO_EXPECT_EQ(result.range, std::optional(ByteRange{0, 999}));
}

NUVIO_TEST("closed range is preserved") {
    const auto result = parse_range_header("bytes=100-199", 1000);
    NUVIO_EXPECT_EQ(result.status, RangeStatus::partial);
    NUVIO_EXPECT_EQ(result.range, std::optional(ByteRange{100, 199}));
}

NUVIO_TEST("range end is clamped to the resource") {
    const auto result = parse_range_header("bytes=900-2000", 1000);
    NUVIO_EXPECT_EQ(result.range, std::optional(ByteRange{900, 999}));
}

NUVIO_TEST("open-ended range reaches the resource end") {
    const auto result = parse_range_header("bytes=700-", 1000);
    NUVIO_EXPECT_EQ(result.range, std::optional(ByteRange{700, 999}));
}

NUVIO_TEST("suffix range selects bytes from the end") {
    const auto result = parse_range_header("bytes=-250", 1000);
    NUVIO_EXPECT_EQ(result.range, std::optional(ByteRange{750, 999}));
}

NUVIO_TEST("large suffix range selects the full resource") {
    const auto result = parse_range_header("bytes=-2000", 1000);
    NUVIO_EXPECT_EQ(result.range, std::optional(ByteRange{0, 999}));
}

NUVIO_TEST("out-of-bounds range is unsatisfiable") {
    const auto result = parse_range_header("bytes=1000-", 1000);
    NUVIO_EXPECT_EQ(result.status, RangeStatus::unsatisfiable);
    NUVIO_EXPECT_TRUE(!result.range.has_value());
}

NUVIO_TEST("multiple ranges are rejected explicitly") {
    const auto result = parse_range_header("bytes=0-10,20-30", 1000);
    NUVIO_EXPECT_EQ(result.status, RangeStatus::multiple_ranges_unsupported);
}

NUVIO_TEST("invalid range unit is malformed") {
    const auto result = parse_range_header("items=0-10", 1000);
    NUVIO_EXPECT_EQ(result.status, RangeStatus::malformed);
}

NUVIO_TEST("empty resource has no full range") {
    const auto result = parse_range_header("", 0);
    NUVIO_EXPECT_EQ(result.status, RangeStatus::full);
    NUVIO_EXPECT_TRUE(!result.range.has_value());
}
