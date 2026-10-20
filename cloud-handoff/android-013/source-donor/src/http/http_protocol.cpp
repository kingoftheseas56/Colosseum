#include "nuvio_engine/http_protocol.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <string>
#include <utility>

namespace nuvio::http {
namespace {

constexpr std::size_t maximum_request_head_size = 16 * 1024;
constexpr std::size_t maximum_request_line_size = 4 * 1024;
constexpr std::size_t maximum_header_count = 64;
constexpr std::size_t maximum_target_size = 2 * 1024;

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

bool case_insensitive_equal(const std::string_view left, const std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    return std::ranges::equal(left, right, [](const unsigned char a, const unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    });
}

bool valid_header_name(const std::string_view name) {
    if (name.empty()) {
        return false;
    }
    constexpr std::string_view separators = "()<>@,;:\\\"/[]?={} \t";
    return std::ranges::all_of(name, [&](const unsigned char character) {
        return character > 32 && character < 127 &&
            separators.find(static_cast<char>(character)) == std::string_view::npos;
    });
}

bool valid_header_value(const std::string_view value) {
    return std::ranges::all_of(value, [](const unsigned char character) {
        return character == '\t' || (character >= 32 && character != 127);
    });
}

bool valid_target(const std::string_view target) {
    if (target.empty() || target.size() > maximum_target_size || target.front() != '/') {
        return false;
    }
    return std::ranges::all_of(target, [](const unsigned char character) {
        return character >= 33 && character < 127 && character != '\\' &&
            character != '#';
    });
}

std::optional<std::uint64_t> parse_content_length(const std::string_view value) {
    const auto normalized = trim(value);
    if (normalized.empty()) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(
        normalized.data(),
        normalized.data() + normalized.size(),
        parsed
    );
    if (result.ec != std::errc{} || result.ptr != normalized.data() + normalized.size()) {
        return std::nullopt;
    }
    return parsed;
}

std::string_view reason_phrase(const ResponseStatus status) {
    switch (status) {
    case ResponseStatus::ok:
        return "OK";
    case ResponseStatus::partial_content:
        return "Partial Content";
    case ResponseStatus::bad_request:
        return "Bad Request";
    case ResponseStatus::not_found:
        return "Not Found";
    case ResponseStatus::method_not_allowed:
        return "Method Not Allowed";
    case ResponseStatus::range_not_satisfiable:
        return "Range Not Satisfiable";
    case ResponseStatus::request_header_fields_too_large:
        return "Request Header Fields Too Large";
    case ResponseStatus::internal_server_error:
        return "Internal Server Error";
    case ResponseStatus::service_unavailable:
        return "Service Unavailable";
    }
    return "Unknown";
}

bool valid_content_type(const std::string_view value) {
    if (value.empty() || value.size() > 127 || value.find('/') == std::string_view::npos) {
        return false;
    }
    return std::ranges::all_of(value, [](const unsigned char character) {
        return character >= 33 && character < 127;
    });
}

void append_status_line(std::string& output, const ResponseStatus status) {
    output += "HTTP/1.1 ";
    output += std::to_string(static_cast<std::uint16_t>(status));
    output += ' ';
    output += reason_phrase(status);
    output += "\r\n";
}

void append_common_headers(std::string& output) {
    output += "Accept-Ranges: bytes\r\n";
    output += "Access-Control-Allow-Origin: *\r\n";
    output += "Access-Control-Allow-Headers: Range\r\n";
    output += "Access-Control-Expose-Headers: Content-Length, Content-Range\r\n";
    output += "transferMode.dlna.org: Streaming\r\n";
    output += "Connection: close\r\n";
}

}

RequestParseResult parse_http_request_head(const std::string_view request_head) {
    if (request_head.size() > maximum_request_head_size) {
        return {RequestParseStatus::header_too_large, std::nullopt};
    }
    if (!request_head.ends_with("\r\n\r\n")) {
        return {RequestParseStatus::malformed, std::nullopt};
    }
    const auto request_line_end = request_head.find("\r\n");
    if (request_line_end == std::string_view::npos ||
        request_line_end > maximum_request_line_size) {
        return {RequestParseStatus::malformed, std::nullopt};
    }
    const auto request_line = request_head.substr(0, request_line_end);
    const auto first_space = request_line.find(' ');
    const auto second_space = first_space == std::string_view::npos
        ? std::string_view::npos
        : request_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos ||
        request_line.find(' ', second_space + 1) != std::string_view::npos) {
        return {RequestParseStatus::malformed, std::nullopt};
    }
    const auto method_text = request_line.substr(0, first_space);
    const auto target = request_line.substr(first_space + 1, second_space - first_space - 1);
    const auto version = request_line.substr(second_space + 1);
    if (!valid_header_name(method_text) || !valid_target(target) ||
        (version != "HTTP/1.1" && version != "HTTP/1.0")) {
        return {RequestParseStatus::malformed, std::nullopt};
    }

    HttpMethod method;
    if (method_text == "GET") {
        method = HttpMethod::get;
    } else if (method_text == "HEAD") {
        method = HttpMethod::head;
    } else {
        return {RequestParseStatus::method_not_allowed, std::nullopt};
    }

    std::optional<std::string> range;
    std::optional<std::uint64_t> content_length;
    std::size_t header_count = 0;
    auto position = request_line_end + 2;
    while (position < request_head.size() - 2) {
        const auto line_end = request_head.find("\r\n", position);
        if (line_end == std::string_view::npos) {
            return {RequestParseStatus::malformed, std::nullopt};
        }
        if (line_end == position) {
            break;
        }
        if (++header_count > maximum_header_count) {
            return {RequestParseStatus::header_too_large, std::nullopt};
        }
        const auto line = request_head.substr(position, line_end - position);
        if (line.front() == ' ' || line.front() == '\t') {
            return {RequestParseStatus::malformed, std::nullopt};
        }
        const auto separator = line.find(':');
        if (separator == std::string_view::npos) {
            return {RequestParseStatus::malformed, std::nullopt};
        }
        const auto name = line.substr(0, separator);
        const auto value = trim(line.substr(separator + 1));
        if (!valid_header_name(name) || !valid_header_value(value)) {
            return {RequestParseStatus::malformed, std::nullopt};
        }
        if (case_insensitive_equal(name, "Range")) {
            if (range.has_value() || value.empty()) {
                return {RequestParseStatus::malformed, std::nullopt};
            }
            range = std::string(value);
        } else if (case_insensitive_equal(name, "Transfer-Encoding")) {
            return {RequestParseStatus::malformed, std::nullopt};
        } else if (case_insensitive_equal(name, "Content-Length")) {
            const auto parsed = parse_content_length(value);
            if (!parsed.has_value() || content_length.has_value()) {
                return {RequestParseStatus::malformed, std::nullopt};
            }
            content_length = parsed;
        }
        position = line_end + 2;
    }
    if (content_length.value_or(0) != 0) {
        return {RequestParseStatus::malformed, std::nullopt};
    }
    return {
        RequestParseStatus::ok,
        HttpRequest{method, std::string(target), std::move(range)},
    };
}

StreamResponse build_stream_response(
    const HttpRequest& request,
    const std::uint64_t resource_size,
    const std::string_view content_type
) {
    const auto range = parse_range_header(request.range.value_or(""), resource_size);
    ResponseStatus status = ResponseStatus::ok;
    if (range.status == RangeStatus::partial) {
        status = ResponseStatus::partial_content;
    } else if (range.status == RangeStatus::malformed) {
        return {
            ResponseStatus::bad_request,
            std::nullopt,
            false,
            build_error_response(ResponseStatus::bad_request),
        };
    } else if (range.status == RangeStatus::unsatisfiable ||
               range.status == RangeStatus::multiple_ranges_unsupported) {
        return {
            ResponseStatus::range_not_satisfiable,
            std::nullopt,
            false,
            build_error_response(ResponseStatus::range_not_satisfiable, resource_size),
        };
    }

    std::string headers;
    headers.reserve(384);
    append_status_line(headers, status);
    append_common_headers(headers);
    headers += "Content-Type: ";
    headers += valid_content_type(content_type) ? content_type : "application/octet-stream";
    headers += "\r\n";
    const auto content_length = range.range.has_value() ? range.range->length() : 0;
    headers += "Content-Length: ";
    headers += std::to_string(content_length);
    headers += "\r\n";
    if (status == ResponseStatus::partial_content && range.range.has_value()) {
        headers += "Content-Range: bytes ";
        headers += std::to_string(range.range->start);
        headers += '-';
        headers += std::to_string(range.range->end);
        headers += '/';
        headers += std::to_string(resource_size);
        headers += "\r\n";
    }
    headers += "\r\n";
    return {
        status,
        range.range,
        request.method == HttpMethod::get && content_length > 0,
        std::move(headers),
    };
}

std::string build_error_response(
    const ResponseStatus status,
    const std::uint64_t resource_size
) {
    std::string response;
    response.reserve(256);
    append_status_line(response, status);
    append_common_headers(response);
    if (status == ResponseStatus::method_not_allowed) {
        response += "Allow: GET, HEAD\r\n";
    }
    if (status == ResponseStatus::range_not_satisfiable) {
        response += "Content-Range: bytes */";
        response += std::to_string(resource_size);
        response += "\r\n";
    }
    response += "Content-Length: 0\r\n\r\n";
    return response;
}

}
