#ifndef NUVIO_ENGINE_HTTP_PROTOCOL_HPP
#define NUVIO_ENGINE_HTTP_PROTOCOL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "nuvio_engine/byte_range.hpp"
#include "nuvio_engine/export.h"

namespace nuvio::http {

enum class HttpMethod {
    get,
    head,
};

enum class RequestParseStatus {
    ok,
    malformed,
    header_too_large,
    method_not_allowed,
};

struct HttpRequest {
    HttpMethod method;
    std::string target;
    std::optional<std::string> range;
};

struct RequestParseResult {
    RequestParseStatus status;
    std::optional<HttpRequest> request;
};

enum class ResponseStatus : std::uint16_t {
    ok = 200,
    partial_content = 206,
    bad_request = 400,
    not_found = 404,
    method_not_allowed = 405,
    range_not_satisfiable = 416,
    request_header_fields_too_large = 431,
    internal_server_error = 500,
    service_unavailable = 503,
};

struct StreamResponse {
    ResponseStatus status;
    std::optional<ByteRange> body_range;
    bool include_body;
    std::string headers;
};

[[nodiscard]] NUVIO_ENGINE_CPP_API RequestParseResult parse_http_request_head(
    std::string_view request_head
);

[[nodiscard]] NUVIO_ENGINE_CPP_API StreamResponse build_stream_response(
    const HttpRequest& request,
    std::uint64_t resource_size,
    std::string_view content_type
);

[[nodiscard]] NUVIO_ENGINE_CPP_API std::string build_error_response(
    ResponseStatus status,
    std::uint64_t resource_size = 0
);

}

#endif
