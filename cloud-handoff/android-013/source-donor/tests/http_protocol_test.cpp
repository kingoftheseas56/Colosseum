#include "test_support.hpp"

#include "nuvio_engine/http_protocol.hpp"

#include <string>

using nuvio::http::RequestParseStatus;
using nuvio::http::ResponseStatus;
using nuvio::http::build_error_response;
using nuvio::http::build_stream_response;
using nuvio::http::parse_http_request_head;

NUVIO_TEST("HTTP GET request produces a complete stream response") {
    const auto parsed = parse_http_request_head(
        "GET /stream/token HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
    );
    NUVIO_EXPECT_EQ(parsed.status, RequestParseStatus::ok);
    NUVIO_EXPECT_TRUE(parsed.request.has_value());
    const auto response = build_stream_response(*parsed.request, 1000, "video/mp4");
    NUVIO_EXPECT_EQ(response.status, ResponseStatus::ok);
    NUVIO_EXPECT_TRUE(response.include_body);
    NUVIO_EXPECT_EQ(response.body_range, std::optional(nuvio::http::ByteRange{0, 999}));
    NUVIO_EXPECT_TRUE(response.headers.find("Content-Length: 1000\r\n") != std::string::npos);
    NUVIO_EXPECT_TRUE(response.headers.find("Accept-Ranges: bytes\r\n") != std::string::npos);
}

NUVIO_TEST("HTTP HEAD range mirrors 206 headers without a body") {
    const auto parsed = parse_http_request_head(
        "HEAD /stream/token HTTP/1.1\r\nRange: bytes=100-199\r\n\r\n"
    );
    NUVIO_EXPECT_EQ(parsed.status, RequestParseStatus::ok);
    const auto response = build_stream_response(*parsed.request, 1000, "video/x-matroska");
    NUVIO_EXPECT_EQ(response.status, ResponseStatus::partial_content);
    NUVIO_EXPECT_TRUE(!response.include_body);
    NUVIO_EXPECT_TRUE(
        response.headers.find("Content-Range: bytes 100-199/1000\r\n") != std::string::npos
    );
    NUVIO_EXPECT_TRUE(response.headers.find("Content-Length: 100\r\n") != std::string::npos);
}

NUVIO_TEST("unsatisfiable stream range produces RFC content range") {
    const auto parsed = parse_http_request_head(
        "GET /stream/token HTTP/1.1\r\nRange: bytes=1000-\r\n\r\n"
    );
    const auto response = build_stream_response(*parsed.request, 1000, "video/mp4");
    NUVIO_EXPECT_EQ(response.status, ResponseStatus::range_not_satisfiable);
    NUVIO_EXPECT_TRUE(!response.include_body);
    NUVIO_EXPECT_TRUE(response.headers.find("Content-Range: bytes */1000\r\n") != std::string::npos);
}

NUVIO_TEST("duplicate range headers are rejected") {
    const auto parsed = parse_http_request_head(
        "GET /stream/token HTTP/1.1\r\nRange: bytes=0-1\r\nrange: bytes=2-3\r\n\r\n"
    );
    NUVIO_EXPECT_EQ(parsed.status, RequestParseStatus::malformed);
}

NUVIO_TEST("request bodies and transfer encodings are rejected") {
    const auto with_body = parse_http_request_head(
        "GET /stream/token HTTP/1.1\r\nContent-Length: 1\r\n\r\n"
    );
    NUVIO_EXPECT_EQ(with_body.status, RequestParseStatus::malformed);
    const auto chunked = parse_http_request_head(
        "GET /stream/token HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
    );
    NUVIO_EXPECT_EQ(chunked.status, RequestParseStatus::malformed);
}

NUVIO_TEST("folded headers and unsafe targets are rejected") {
    const auto folded = parse_http_request_head(
        "GET /stream/token HTTP/1.1\r\nRange: bytes=0-1\r\n more\r\n\r\n"
    );
    NUVIO_EXPECT_EQ(folded.status, RequestParseStatus::malformed);
    const auto unsafe = parse_http_request_head(
        "GET /stream\\token HTTP/1.1\r\n\r\n"
    );
    NUVIO_EXPECT_EQ(unsafe.status, RequestParseStatus::malformed);
}

NUVIO_TEST("unsupported methods produce an Allow response") {
    const auto parsed = parse_http_request_head(
        "POST /stream/token HTTP/1.1\r\nContent-Length: 0\r\n\r\n"
    );
    NUVIO_EXPECT_EQ(parsed.status, RequestParseStatus::method_not_allowed);
    const auto response = build_error_response(ResponseStatus::method_not_allowed);
    NUVIO_EXPECT_TRUE(response.find("HTTP/1.1 405 Method Not Allowed\r\n") == 0);
    NUVIO_EXPECT_TRUE(response.find("Allow: GET, HEAD\r\n") != std::string::npos);
}

NUVIO_TEST("content type injection falls back to binary") {
    const auto parsed = parse_http_request_head("GET /stream/token HTTP/1.0\r\n\r\n");
    const auto response = build_stream_response(
        *parsed.request,
        10,
        "video/mp4\r\nInjected: true"
    );
    NUVIO_EXPECT_TRUE(
        response.headers.find("Content-Type: application/octet-stream\r\n") !=
            std::string::npos
    );
    NUVIO_EXPECT_TRUE(response.headers.find("Injected") == std::string::npos);
}

NUVIO_TEST("HTTP request bounds and empty range are enforced") {
    std::string oversized = "GET /stream/token HTTP/1.1\r\nX-Fill: ";
    oversized.append(17 * 1024, 'x');
    oversized += "\r\n\r\n";
    NUVIO_EXPECT_EQ(
        parse_http_request_head(oversized).status,
        RequestParseStatus::header_too_large
    );
    NUVIO_EXPECT_EQ(
        parse_http_request_head(
            "GET /stream/token HTTP/1.1\r\nRange:\r\n\r\n"
        ).status,
        RequestParseStatus::malformed
    );
}
