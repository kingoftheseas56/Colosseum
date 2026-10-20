#include "nuvio_engine/byte_range.hpp"

#include <charconv>
#include <limits>
#include <string_view>

namespace nuvio::http {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<std::uint64_t> parse_unsigned(const std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

RangeResult parse_suffix(const std::string_view suffix_text, const std::uint64_t resource_size) {
    const auto suffix_length = parse_unsigned(trim(suffix_text));
    if (!suffix_length.has_value()) {
        return {RangeStatus::malformed, std::nullopt};
    }
    if (*suffix_length == 0 || resource_size == 0) {
        return {RangeStatus::unsatisfiable, std::nullopt};
    }
    const auto length = *suffix_length < resource_size ? *suffix_length : resource_size;
    return {RangeStatus::partial, ByteRange{resource_size - length, resource_size - 1}};
}

}

std::uint64_t ByteRange::length() const {
    return end - start + 1;
}

bool RangeResult::is_success() const {
    return status == RangeStatus::full || status == RangeStatus::partial;
}

RangeResult parse_range_header(std::string_view header, const std::uint64_t resource_size) {
    header = trim(header);
    if (header.empty()) {
        if (resource_size == 0) {
            return {RangeStatus::full, std::nullopt};
        }
        return {RangeStatus::full, ByteRange{0, resource_size - 1}};
    }

    constexpr std::string_view prefix = "bytes=";
    if (!header.starts_with(prefix)) {
        return {RangeStatus::malformed, std::nullopt};
    }
    auto specification = trim(header.substr(prefix.size()));
    if (specification.empty()) {
        return {RangeStatus::malformed, std::nullopt};
    }
    if (specification.find(',') != std::string_view::npos) {
        return {RangeStatus::multiple_ranges_unsupported, std::nullopt};
    }

    const auto separator = specification.find('-');
    if (separator == std::string_view::npos || specification.find('-', separator + 1) != std::string_view::npos) {
        return {RangeStatus::malformed, std::nullopt};
    }
    const auto start_text = trim(specification.substr(0, separator));
    const auto end_text = trim(specification.substr(separator + 1));
    if (start_text.empty()) {
        return parse_suffix(end_text, resource_size);
    }

    const auto start = parse_unsigned(start_text);
    if (!start.has_value() || (!end_text.empty() && !parse_unsigned(end_text).has_value())) {
        return {RangeStatus::malformed, std::nullopt};
    }
    if (resource_size == 0 || *start >= resource_size) {
        return {RangeStatus::unsatisfiable, std::nullopt};
    }

    auto end = resource_size - 1;
    if (!end_text.empty()) {
        end = *parse_unsigned(end_text);
        if (*start > end) {
            return {RangeStatus::unsatisfiable, std::nullopt};
        }
        if (end >= resource_size) {
            end = resource_size - 1;
        }
    }
    return {RangeStatus::partial, ByteRange{*start, end}};
}

}
