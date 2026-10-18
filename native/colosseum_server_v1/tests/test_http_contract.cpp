#include <QtCore/QCoreApplication>
#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtNetwork/QTcpSocket>

#include "server1/http/HttpContract.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kNeedMore = 0;
constexpr int kComplete = 1;
constexpr int kParseError = 2;
constexpr int kJsonBody = 1;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void pumpUntil(const std::function<bool()> &condition, std::string_view timeoutMessage)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 2000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    require(condition(), timeoutMessage);
}

QByteArray collectUntilDisconnected(QTcpSocket &client)
{
    QByteArray received;
    QElapsedTimer timer;
    timer.start();
    while (client.state() != QAbstractSocket::UnconnectedState && timer.elapsed() < 5000) {
        if (client.bytesAvailable() != 0)
            received += client.readAll();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    received += client.readAll();
    require(client.state() == QAbstractSocket::UnconnectedState,
            "closeAfter connection eventually reaches disconnected state");
    return received;
}

struct RouteReply final {
    const char *body = "";
    const char *contentType = "text/plain";
};

int replyFromContext(const void *, void *response, server1_http_next_fn, void *, void *context)
{
    const auto *reply = static_cast<const RouteReply *>(context);
    require(reply != nullptr, "handler context is available");
    require(server1_http_response_set_status(response, 200) == 1,
            "handler sets response status");
    require(server1_http_response_set_header(response, "Content-Type", reply->contentType) == 1,
            "handler sets response content type");
    require(server1_http_response_set_body(response, reply->body, std::strlen(reply->body)) == 1,
            "handler sets response body");
    return 1;
}

int replyWithParameter(const void *request, void *response, server1_http_next_fn, void *, void *)
{
    const std::string body = std::string("yt:")
        + server1_http_request_param(request, "id");
    require(server1_http_response_set_status(response, 200) == 1,
            "parameter handler sets response status");
    require(server1_http_response_set_header(response, "Content-Type", "text/plain") == 1,
            "parameter handler sets response content type");
    require(server1_http_response_set_body(response, body.data(), body.size()) == 1,
            "parameter handler sets response body");
    return 1;
}

int replyWithNameParameter(const void *request, void *response, server1_http_next_fn, void *, void *)
{
    const std::string body = std::string("name:")
        + server1_http_request_param(request, "name");
    require(server1_http_response_set_status(response, 200) == 1,
            "name parameter handler sets response status");
    require(server1_http_response_set_header(response, "Content-Type", "text/plain") == 1,
            "name parameter handler sets response content type");
    require(server1_http_response_set_body(response, body.data(), body.size()) == 1,
            "name parameter handler sets response body");
    return 1;
}

int replyWithTorrentParameters(const void *request, void *response, server1_http_next_fn, void *,
                               void *)
{
    const std::string body = std::string("root:")
        + server1_http_request_param(request, "infoHash") + ":"
        + server1_http_request_param(request, "idx");
    require(server1_http_response_set_status(response, 200) == 1,
            "torrent handler sets response status");
    require(server1_http_response_set_header(response, "Content-Type", "text/plain") == 1,
            "torrent handler sets response content type");
    require(server1_http_response_set_body(response, body.data(), body.size()) == 1,
            "torrent handler sets response body");
    return 1;
}

int continueThroughProxy(const void *, void *response, server1_http_next_fn next,
                         void *nextContext, void *)
{
    require(server1_http_response_set_header(response, "X-Middleware", "seen") == 1,
            "middleware can mutate the response before continuation");
    require(next != nullptr, "middleware receives a continuation");
    return next(nextContext);
}

struct BinaryStream final {
    QByteArray payload;
    qsizetype offset = 0;
};

std::ptrdiff_t readBinaryStream(void *context, void *buffer, std::size_t capacity)
{
    auto *stream = static_cast<BinaryStream *>(context);
    if (stream == nullptr || buffer == nullptr)
        return -1;
    if (stream->offset >= stream->payload.size())
        return 0;
    const qsizetype available = stream->payload.size() - stream->offset;
    const qsizetype count = std::min<qsizetype>(available, static_cast<qsizetype>(capacity));
    std::memcpy(buffer, stream->payload.constData() + stream->offset,
                static_cast<std::size_t>(count));
    stream->offset += count;
    return static_cast<std::ptrdiff_t>(count);
}

int binaryStreamHandler(const void *, void *response, server1_http_next_fn, void *, void *context)
{
    auto *stream = static_cast<BinaryStream *>(context);
    require(stream != nullptr, "binary stream handler context is available");
    require(server1_http_response_set_status(response, 200) == 1,
            "binary stream handler sets response status");
    require(server1_http_response_set_header(response, "Content-Type", "application/octet-stream")
                == 1,
            "binary stream handler sets binary content type");
    require(server1_http_response_set_stream(
                response, readBinaryStream, nullptr, stream,
                static_cast<std::size_t>(stream->payload.size()))
                == 1,
            "binary stream handler transfers the stream contract");
    return 1;
}

void caseH00_01()
{
    void *parser = server1_http_parser_create(3U * 1024U * 1024U);
    require(parser != nullptr, "H00-01 parser was created");

    const std::string first =
        "POST /submit?tag=one&tag=two&name=hello+world HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "X-Trace: first\r\n"
        "x-trace: second\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 17\r\n\r\n"
        "{\"ok\":true,";
    require(server1_http_parser_feed(parser, first.data(), first.size()) == kNeedMore,
            "fragmented header/body waits for the remainder");

    const std::string second = "\"n\":1}";
    require(server1_http_parser_feed(parser, second.data(), second.size()) == kComplete,
            "fragmented body completes the request");
    require(std::string(server1_http_parser_method(parser)) == "POST", "method is preserved");
    require(std::string(server1_http_parser_target(parser))
                == "/submit?tag=one&tag=two&name=hello+world",
            "raw request target is preserved");
    require(std::string(server1_http_parser_path(parser)) == "/submit", "pathname is parsed");
    require(server1_http_parser_body_kind(parser) == kJsonBody, "JSON content type is recognized");
    require(std::string(server1_http_parser_body(parser)) == "{\"ok\":true,\"n\":1}",
            "fragmented JSON body is preserved");
    require(server1_http_parser_keep_alive(parser), "HTTP/1.1 defaults to keep-alive");
    require(server1_http_parser_header_count(parser, "X-TRACE") == 2,
            "duplicate headers remain observable");
    require(std::string(server1_http_parser_header_value_at(parser, "x-trace", 1)) == "second",
            "duplicate header values retain wire order");
    require(server1_http_parser_query_entry_count(parser) == 3,
            "query entries retain repeated keys");
    require(std::string(server1_http_parser_query_key_at(parser, 0)) == "tag"
                && std::string(server1_http_parser_query_value_at(parser, 0)) == "one"
                && std::string(server1_http_parser_query_value_at(parser, 1)) == "two"
                && std::string(server1_http_parser_query_value_at(parser, 2)) == "hello world",
            "query values are decoded without losing repetition");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(3U * 1024U * 1024U);
    const std::string form =
        "POST /form HTTP/1.1\r\nHost: localhost\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 7\r\n\r\n"
        "a=1&a=2";
    require(server1_http_parser_feed(parser, form.data(), form.size()) == kComplete,
            "urlencoded body parses");
    require(server1_http_parser_body_kind(parser) == 2,
            "urlencoded content type selects extended form parsing");
    require(server1_http_parser_form_entry_count(parser) == 2
                && std::string(server1_http_parser_form_key_at(parser, 0)) == "a"
                && std::string(server1_http_parser_form_value_at(parser, 0)) == "1"
                && std::string(server1_http_parser_form_value_at(parser, 1)) == "2",
            "urlencoded body preserves repeated form keys");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(3U * 1024U * 1024U);
    const std::string head =
        "HEAD /file HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    require(server1_http_parser_feed(parser, head.data(), head.size()) == kComplete,
            "HEAD request parses");
    require(!server1_http_parser_keep_alive(parser), "Connection close is parsed");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(3U * 1024U * 1024U);
    const std::string encoded = "GET /file/a%2Fb HTTP/1.1\r\nHost: localhost\r\n\r\n";
    require(server1_http_parser_feed(parser, encoded.data(), encoded.size()) == kComplete,
            "encoded slash request parses");
    require(std::string(server1_http_parser_path(parser)) == "/file/a%2Fb",
            "encoded slash remains encoded for route matching");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(3U * 1024U * 1024U);
    const std::string malformed = "GET not-a-target HTTP/1.1\r\nHost: localhost\r\n\r\n";
    require(server1_http_parser_feed(parser, malformed.data(), malformed.size()) == kParseError,
            "malformed request target is rejected by HTTP parsing");
    require(server1_http_parser_error_status(parser) == 400,
            "malformed target is an HTTP 400, not a route-validation failure");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(3U * 1024U * 1024U);
    const std::string oversized =
        "POST /settings HTTP/1.1\r\nHost: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 3145729\r\n\r\n";
    require(server1_http_parser_feed(parser, oversized.data(), oversized.size()) == kParseError,
            "JSON bodies above 3 MiB are rejected before allocation");
    require(server1_http_parser_error_status(parser) == 413,
            "body limit reports Payload Too Large");
    server1_http_parser_destroy(parser);

    const auto expectUnsupportedTransferEncoding = [](const std::string &encoding) {
        void *transferParser = server1_http_parser_create(3U * 1024U * 1024U);
        const std::string transferRequest =
            "POST /transfer HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: " + encoding
            + "\r\n\r\n0\r\n\r\n";
        require(server1_http_parser_feed(transferParser, transferRequest.data(),
                                          transferRequest.size())
                    == kParseError,
                "unsupported transfer coding combination is rejected");
        require(server1_http_parser_error_status(transferParser) == 400,
                "unsupported transfer coding combination reports Bad Request");
        server1_http_parser_destroy(transferParser);
    };
    expectUnsupportedTransferEncoding("gzip, chunked");
    expectUnsupportedTransferEncoding("chunked, gzip");
    expectUnsupportedTransferEncoding("chunked, chunked");

    parser = server1_http_parser_create(3U * 1024U * 1024U);
    const std::string duplicateTransferEncoding =
        "POST /transfer HTTP/1.1\r\nHost: localhost\r\n"
        "Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n"
        "0\r\n\r\n";
    require(server1_http_parser_feed(parser, duplicateTransferEncoding.data(),
                                      duplicateTransferEncoding.size())
                == kParseError,
            "repeated transfer-encoding headers are rejected");
    require(server1_http_parser_error_status(parser) == 400,
            "repeated transfer-encoding headers report Bad Request");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(3U * 1024U * 1024U);
    const std::string oversizedHeader =
        "GET /headers HTTP/1.1\r\nHost: localhost\r\nX-Pad: "
        + std::string(64U * 1024U, 'h') + "\r\n\r\n";
    require(server1_http_parser_feed(parser, oversizedHeader.data(), oversizedHeader.size())
                == kParseError,
            "a complete header block above 64 KiB is rejected");
    require(server1_http_parser_error_status(parser) == 431,
            "completed oversized header reports Request Header Fields Too Large");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(4);
    const std::string oversizedChunked =
        "POST /chunked HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n0\r\n\r\n";
    require(server1_http_parser_feed(parser, oversizedChunked.data(), oversizedChunked.size())
                == kParseError,
            "chunked body above the limit is rejected");
    require(server1_http_parser_error_status(parser) == 413,
            "oversized chunked body reports Payload Too Large");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(4);
    const std::string maximalChunked =
        "POST /chunked HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
        "FFFFFFFFFFFFFFFF\r\n";
    require(server1_http_parser_feed(parser, maximalChunked.data(), maximalChunked.size())
                == kParseError,
            "maximal chunk size is rejected without length arithmetic overflow");
    require(server1_http_parser_error_status(parser) == 413,
            "maximal chunk size above the body limit reports Payload Too Large");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(4);
    const std::string overflowingChunked =
        "POST /chunked HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
        "10000000000000000\r\n";
    require(server1_http_parser_feed(parser, overflowingChunked.data(), overflowingChunked.size())
                == kParseError,
            "chunk-size numeric overflow is rejected without wrapping");
    require(server1_http_parser_error_status(parser) == 400,
            "chunk-size numeric overflow reports Bad Request");
    server1_http_parser_destroy(parser);

    parser = server1_http_parser_create(3U * 1024U * 1024U);
    const std::string oversizedTrailer =
        "POST /chunked HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
        "0\r\nX-Trailer: "
        + std::string(64U * 1024U, 't') + "\r\n\r\n";
    require(server1_http_parser_feed(parser, oversizedTrailer.data(), oversizedTrailer.size())
                == kParseError,
            "chunked trailers above the header budget are rejected");
    require(server1_http_parser_error_status(parser) == 431,
            "oversized chunked trailers report Request Header Fields Too Large");
    server1_http_parser_destroy(parser);

    RouteReply rawReply{"ok", "text/plain"};
    void *rawRouter = server1_http_router_create();
    require(rawRouter != nullptr, "raw-wire router was created");
    require(server1_http_router_add_handler(rawRouter, 0, 0, "GET", "/raw", replyFromContext,
                                            &rawReply, nullptr)
                == 1,
            "raw-wire service handler is registered");
    void *rawServer = server1_http_server_create(rawRouter, 64U * 1024U);
    require(rawServer != nullptr && server1_http_server_listen(rawServer, 0) == 1,
            "raw-wire listener started");
    QTcpSocket rawClient;
    rawClient.connectToHost(QStringLiteral("127.0.0.1"), server1_http_server_port(rawServer));
    require(rawClient.waitForConnected(1000), "raw-wire client connected");
    const std::string rawRequest =
        "GET /raw HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    require(rawClient.write(rawRequest.data(), static_cast<qint64>(rawRequest.size()))
                == static_cast<qint64>(rawRequest.size()),
            "raw-wire client wrote request");
    require(rawClient.flush(), "raw-wire client flushed request");
    const QByteArray rawWire = collectUntilDisconnected(rawClient);
    const QByteArray referenceRawWire =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n"
        "ok";
    require(rawWire == referenceRawWire,
            "raw HTTP wire matches the pinned reference transport profile");
    server1_http_server_stop(rawServer);
    server1_http_server_destroy(rawServer);
    server1_http_router_destroy(rawRouter);

    std::cout << "H00-01 PASS\n";
}

void caseH00_02()
{
    void *router = server1_http_router_create();
    require(router != nullptr, "H00-02 production router was created");
    RouteReply hlsReply{"external-hls"};
    RouteReply proxyReply{"external-proxy"};
    RouteReply addonReply{"external-addon"};
    RouteReply headReply{"head"};
    RouteReply explicitOptionsReply{"explicit-options"};
    require(server1_http_router_add_handler(router, 1, 0, "GET", "/hlsv2/status",
                                            replyFromContext, &hlsReply, nullptr)
                == 1,
            "external hls service handler is registered");
    require(server1_http_router_add_handler(router, 1, 0, "GET", "/yt/:id",
                                            replyWithParameter, nullptr, nullptr)
                == 1,
            "external YouTube service handler is registered");
    require(server1_http_router_add_middleware(router, 1, "/proxy", continueThroughProxy,
                                               nullptr, nullptr)
                == 1,
            "external proxy middleware is registered");
    require(server1_http_router_add_handler(router, 1, 0, "GET", "/proxy/:tail",
                                            replyFromContext, &proxyReply, nullptr)
                == 1,
            "external proxy service handler is registered");
    require(server1_http_router_add_handler(router, 1, 0, "GET", "/local-addon/manifest.json",
                                            replyFromContext, &addonReply, nullptr)
                == 1,
            "external addon service handler is registered");
    require(server1_http_router_add_handler(router, 0, 0, "GET", "/:infoHash/:idx",
                                            replyWithTorrentParameters, nullptr, nullptr)
                == 1,
            "root torrent service handler is registered");
    require(server1_http_router_add_handler(router, 0, 0, "GET", "/head", replyFromContext,
                                            &headReply, nullptr)
                == 1,
            "root GET service handler is registered");
    require(server1_http_router_add_handler(router, 0, 0, "OPTIONS", "/head",
                                            replyFromContext, &explicitOptionsReply, nullptr)
                == 1,
            "explicit OPTIONS service handler is registered");
    require(server1_http_router_add_handler(router, 1, 0, "GET", "/file/:name",
                                            replyWithNameParameter, nullptr, nullptr)
                == 1,
            "encoded parameter service handler is registered");
    require(server1_http_router_add_handler(router, 0, 0, "GET", "/:/", replyFromContext,
                                            nullptr, nullptr)
                == 0,
            "route syntax validation is separate from HTTP parsing");

    auto expectBody = [&](const char *method, const char *target, const char *expected) {
        void *response = server1_http_router_dispatch(router, method, target, "");
        require(response != nullptr, "router returned a response");
        require(server1_http_response_status(response) == 200,
                std::string("matched route returned 200 for ") + target + ", got "
                    + std::to_string(server1_http_response_status(response)));
        require(std::string(server1_http_response_body(response)) == expected,
                std::string("matched route returned expected body for ") + target + ", got "
                    + server1_http_response_body(response));
        server1_http_response_destroy(response);
    };

    expectBody("GET", "/hlsv2/status", "external-hls");
    expectBody("GET", "/yt/id", "yt:id");
    expectBody("GET", "/proxy/anything", "external-proxy");
    expectBody("GET", "/local-addon/manifest.json", "external-addon");
    expectBody("GET", "/0123456789abcdef0123456789abcdef01234567/2", "root:0123456789abcdef0123456789abcdef01234567:2");
    expectBody("GET", "/file/a%2Fb", "name:a/b");

    void *proxyResponse = server1_http_router_dispatch(router, "GET", "/proxy/anything", "");
    require(std::string(server1_http_response_header(proxyResponse, "X-Middleware")) == "seen",
            "middleware continuation preserves middleware response mutations");
    server1_http_response_destroy(proxyResponse);

    void *prefixBoundary = server1_http_router_dispatch(router, "GET", "/proxyx/anything/more", "");
    require(server1_http_response_status(prefixBoundary) == 404,
            "connect prefix routes require a slash or dot boundary");
    server1_http_response_destroy(prefixBoundary);

    void *head = server1_http_router_dispatch(router, "HEAD", "/head", "");
    require(server1_http_response_status(head) == 200, "HEAD falls back to GET route");
    server1_http_response_destroy(head);

    void *options = server1_http_router_dispatch(router, "OPTIONS", "/head", "");
    require(server1_http_response_status(options) == 200,
            "explicit OPTIONS handler returns its response");
    require(std::string(server1_http_response_body(options)) == "explicit-options",
            "explicit OPTIONS handler is not bypassed by automatic OPTIONS");
    require(std::string(server1_http_response_header(options, "Allow")).empty(),
            "explicit OPTIONS response does not receive automatic Allow text");
    server1_http_response_destroy(options);

    void *automaticOptions = server1_http_router_dispatch(router, "OPTIONS", "/file/a%2Fb", "");
    require(server1_http_response_status(automaticOptions) == 200,
            "OPTIONS gets automatic response when no explicit handler exists");
    require(std::string(server1_http_response_header(automaticOptions, "Allow")).find("GET")
                != std::string::npos,
            "automatic OPTIONS exposes the route methods");
    server1_http_response_destroy(automaticOptions);

    void *badTarget = server1_http_router_dispatch(router, "GET", "not-a-target", "");
    require(server1_http_response_status(badTarget) == 400,
            "router rejects malformed HTTP target independently of route registration");
    server1_http_response_destroy(badTarget);

    server1_http_router_destroy(router);
    std::cout << "H00-02 PASS\n";
}

void caseH00_03()
{
    auto requireFullResponse = [](const QByteArray &wire, const QByteArray &expectedBody) {
        const qsizetype headerEnd = wire.indexOf("\r\n\r\n");
        require(headerEnd >= 0, "real wire response has complete headers");
        const QByteArray headers = wire.left(headerEnd);
        require(headers.startsWith("HTTP/1.1 200 OK\r\n"), "real wire response has status 200");
        const QByteArray contentLength =
            QByteArray("Content-Length: ") + QByteArray::number(expectedBody.size());
        require(headers.contains(contentLength), "real wire response has expected content length");
        const QByteArray body = wire.mid(headerEnd + 4);
        require(body.size() == expectedBody.size(), "close occurs only after the full body arrives");
        require(body == expectedBody, "real wire body is not truncated or reordered");
    };

    const QByteArray payload(512U * 1024U, 'x');
    const std::string payloadString(payload.constData(), static_cast<std::size_t>(payload.size()));
    void *router = server1_http_router_create();
    require(router != nullptr, "slow-consumer route router was created");
    require(server1_http_router_add_static(router, 0, 0, "GET", "/large", 200,
                                           payloadString.c_str())
                == 1,
            "slow-consumer route is registered");

    constexpr std::size_t maxQueuedBytes = 1024U * 1024U;
    void *server = server1_http_server_create(router, maxQueuedBytes);
    require(server != nullptr, "HTTP listener was created");
    require(server1_http_server_listen(server, 0) == 1, "HTTP listener started asynchronously");
    require(server1_http_server_port(server) != 0, "ephemeral listener port is available");
    server1_http_server_set_drain_paused(server, 1);

    QTcpSocket client;
    client.setReadBufferSize(1);
    client.connectToHost(QStringLiteral("127.0.0.1"), server1_http_server_port(server));
    require(client.waitForConnected(1000), "test client connected to real listener");
    const std::string request =
        "GET /large HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    require(client.write(request.data(), static_cast<qint64>(request.size()))
                == static_cast<qint64>(request.size()),
            "test client wrote request");
    require(client.flush(), "test client flushed request");

    pumpUntil([&] { return server1_http_server_queued_bytes(server) != 0; },
              "response bytes were queued behind the paused consumer");
    require(client.bytesAvailable() == 0, "paused consumer has not received response bytes");
    require(server1_http_server_queued_bytes(server) <= maxQueuedBytes,
            "paused response stays within the total pending-byte bound");

    server1_http_server_set_drain_paused(server, 0);
    std::size_t peakAfterResume = server1_http_server_queued_bytes(server);
    QElapsedTimer slowConsumerTimer;
    slowConsumerTimer.start();
    while (slowConsumerTimer.elapsed() < 500) {
        peakAfterResume = std::max(peakAfterResume, server1_http_server_queued_bytes(server));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    require(peakAfterResume <= maxQueuedBytes,
            "socket-pending plus application-pending response stays within the cap after resume");
    client.setReadBufferSize(0);
    const QByteArray received = collectUntilDisconnected(client);
    requireFullResponse(received, payload);
    pumpUntil([&] {
        return server1_http_server_active_connections(server) == 0
            && server1_http_server_queued_bytes(server) == 0;
    }, "full response drain releases the connection after socket bytes drain");
    server1_http_server_stop(server);
    server1_http_server_destroy(server);
    server1_http_router_destroy(router);

    constexpr std::size_t capProbeBytes = 512U * 1024U;
    const QByteArray capPayload(300U * 1024U, 'y');
    const std::string capPayloadString(capPayload.constData(),
                                       static_cast<std::size_t>(capPayload.size()));
    void *capRouter = server1_http_router_create();
    require(capRouter != nullptr, "cap-probe router was created");
    require(server1_http_router_add_static(capRouter, 0, 0, "GET", "/large", 200,
                                           capPayloadString.c_str())
                == 1,
            "cap-probe route is registered");
    void *capServer = server1_http_server_create(capRouter, capProbeBytes);
    require(capServer != nullptr, "cap-probe listener was created");
    require(server1_http_server_listen(capServer, 0) == 1, "cap-probe listener started");
    server1_http_server_set_drain_paused(capServer, 1);

    QTcpSocket firstPending;
    QTcpSocket secondPending;
    firstPending.connectToHost(QStringLiteral("127.0.0.1"), server1_http_server_port(capServer));
    secondPending.connectToHost(QStringLiteral("127.0.0.1"), server1_http_server_port(capServer));
    require(firstPending.waitForConnected(1000) && secondPending.waitForConnected(1000),
            "two real clients connected for the total-cap probe");
    const std::string pendingRequest =
        "GET /large HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    require(firstPending.write(pendingRequest.data(), static_cast<qint64>(pendingRequest.size()))
                == static_cast<qint64>(pendingRequest.size()),
            "first pending client wrote request");
    require(secondPending.write(pendingRequest.data(), static_cast<qint64>(pendingRequest.size()))
                == static_cast<qint64>(pendingRequest.size()),
            "second pending client wrote request");
    require(firstPending.flush() && secondPending.flush(), "pending clients flushed requests");

    std::size_t peakPending = 0;
    pumpUntil([&] {
        peakPending = std::max(peakPending, server1_http_server_queued_bytes(capServer));
        return peakPending != 0;
    }, "at least one large response entered the pending queue");
    QElapsedTimer capTimer;
    capTimer.start();
    while (capTimer.elapsed() < 500) {
        peakPending = std::max(peakPending, server1_http_server_queued_bytes(capServer));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    require(peakPending <= capProbeBytes,
            "multiple large responses never exceed the configured total pending-byte cap");

    firstPending.abort();
    secondPending.abort();
    pumpUntil([&] {
        return server1_http_server_active_connections(capServer) == 0
            && server1_http_server_queued_bytes(capServer) == 0;
    }, "disconnect during pending output releases ownership and queued bytes");
    for (int index = 0; index < 4; ++index)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    require(server1_http_server_active_connections(capServer) == 0
                && server1_http_server_queued_bytes(capServer) == 0,
            "no deferred write callback targets a released connection");
    server1_http_server_stop(capServer);
    server1_http_server_destroy(capServer);
    server1_http_router_destroy(capRouter);

    const std::size_t streamCapBytes = 32U * 1024U;
    BinaryStream stream;
    stream.payload.resize(512U * 1024U);
    for (qsizetype index = 0; index < stream.payload.size(); ++index)
        stream.payload[index] = static_cast<char>((index * 37) & 0xff);
    stream.payload[0] = '\0';
    stream.payload[1] = '\x01';
    stream.payload[2] = '\xff';
    void *streamRouter = server1_http_router_create();
    require(streamRouter != nullptr, "binary stream router was created");
    require(server1_http_router_add_handler(streamRouter, 0, 0, "GET", "/binary",
                                            binaryStreamHandler, &stream, nullptr)
                == 1,
            "binary stream handler is registered");
    void *streamServer = server1_http_server_create(streamRouter, streamCapBytes);
    require(streamServer != nullptr, "binary stream listener was created");
    require(server1_http_server_listen(streamServer, 0) == 1,
            "binary stream listener started");
    server1_http_server_set_drain_paused(streamServer, 1);

    QTcpSocket streamClient;
    streamClient.setReadBufferSize(1);
    streamClient.connectToHost(QStringLiteral("127.0.0.1"),
                               server1_http_server_port(streamServer));
    require(streamClient.waitForConnected(1000), "binary stream client connected");
    const std::string streamRequest =
        "GET /binary HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    require(streamClient.write(streamRequest.data(), static_cast<qint64>(streamRequest.size()))
                == static_cast<qint64>(streamRequest.size()),
            "binary stream client wrote request");
    require(streamClient.flush(), "binary stream client flushed request");
    pumpUntil([&] { return server1_http_server_queued_bytes(streamServer) != 0; },
              "binary stream headers were queued behind the paused consumer");
    require(server1_http_server_queued_bytes(streamServer) <= streamCapBytes,
            "binary stream stays within the total pending-byte bound while paused");
    require(stream.offset == 0, "paused binary stream has not been read into a full response");

    server1_http_server_set_drain_paused(streamServer, 0);
    std::size_t peakStreamPending = server1_http_server_queued_bytes(streamServer);
    QElapsedTimer streamTimer;
    streamTimer.start();
    while (streamTimer.elapsed() < 500) {
        peakStreamPending = std::max(peakStreamPending,
                                     server1_http_server_queued_bytes(streamServer));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    require(peakStreamPending <= streamCapBytes,
            "binary streaming never exceeds the configured pending-byte cap");
    streamClient.setReadBufferSize(0);
    const QByteArray streamWire = collectUntilDisconnected(streamClient);
    const qsizetype streamHeaderEnd = streamWire.indexOf("\r\n\r\n");
    require(streamHeaderEnd >= 0, "binary stream response has complete headers");
    const QByteArray streamHeaders = streamWire.left(streamHeaderEnd);
    require(streamHeaders.startsWith("HTTP/1.1 200 OK\r\n"),
            "binary stream response has status 200");
    require(streamHeaders.contains("Content-Type: application/octet-stream\r\n"),
            "binary stream response preserves the content type");
    require(streamHeaders.contains(QByteArray("Content-Length: ")
                                   + QByteArray::number(stream.payload.size())),
            "binary stream response exposes the known content length");
    require(streamWire.mid(streamHeaderEnd + 4) == stream.payload,
            "binary stream response preserves every byte beyond the queue cap");
    require(stream.offset == stream.payload.size(),
            "binary stream callback is consumed exactly once through EOF");
    pumpUntil([&] {
        return server1_http_server_active_connections(streamServer) == 0
            && server1_http_server_queued_bytes(streamServer) == 0;
    }, "binary stream drain releases the connection and queued bytes");
    server1_http_server_stop(streamServer);
    server1_http_server_destroy(streamServer);
    server1_http_router_destroy(streamRouter);

    std::cout << "H00-03 PASS\n";
}

void caseH00_04()
{
    for (const QByteArray &payload : {QByteArray(), QByteArray("A\0B\xff",4), QByteArray("\0Z\0",3)}) {
        void *router = server1_http_router_create();
        auto handler = +[](const void *, void *response, server1_http_next_fn, void *, void *context) {
            const auto &bytes = *static_cast<const QByteArray *>(context);
            server1_http_response_set_status(response,200);
            return server1_http_response_set_body(response,bytes.constData(),bytes.size());
        };
        require(server1_http_router_add_handler(router,0,0,"GET","/bytes",handler,
            const_cast<QByteArray *>(&payload),nullptr)==1,"binary route registered");
        void *server = server1_http_server_create(router,65536);
        require(server1_http_server_listen(server,0)==1,"binary listener started");
        for (const QByteArray &method : {QByteArray("GET"),QByteArray("HEAD")}) {
            QTcpSocket client;
            client.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(server));
            require(client.waitForConnected(1000),"binary client connected");
            client.write(method+" /bytes HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            client.flush();
            const auto wire = collectUntilDisconnected(client);
            const auto split = wire.indexOf("\r\n\r\n");
            require(split>=0,"binary response has headers");
            require(wire.left(split).contains("Content-Length: "+QByteArray::number(payload.size())),
                    "non-stream binary response lost its explicit length");
            require(wire.mid(split+4)==(method=="HEAD" ? QByteArray() : payload),
                    "non-stream binary response changed bytes or HEAD body");
        }
        server1_http_server_destroy(server);
        server1_http_router_destroy(router);
    }
    std::cout << "H00-04 binary wire PASS\n";
}

void caseH00_05()
{
    constexpr std::size_t inputCap = 3U*1024U*1024U + 64U*1024U;
    void *router = server1_http_router_create();
    require(server1_http_router_add_static(router,0,0,"GET","/wait",200,"ok")==1,
            "backpressure route registered");
    void *server = server1_http_server_create(router,65536);
    require(server1_http_server_listen(server,0)==1,"backpressure listener started");
    server1_http_server_set_drain_paused(server,1);
    QTcpSocket client;
    client.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(server));
    require(client.waitForConnected(1000),"backpressure client connected");
    client.write("GET /wait HTTP/1.1\r\nHost: localhost\r\n\r\n"); client.flush();
    pumpUntil([&] {return server1_http_server_queued_bytes(server)>0;},"first response queued");
    client.write(QByteArray(static_cast<qsizetype>(inputCap*4),'x')); client.flush();
    QElapsedTimer timer; timer.start();
    while (timer.elapsed()<250) QCoreApplication::processEvents(QEventLoop::AllEvents,10);
    const auto buffered = server1_http_server_input_backlog_bytes(server);
    std::cout << "H00-05 paused input bytes=" << buffered << " cap=" << inputCap << '\n';
    require(buffered>0 && buffered<=inputCap,"deferred pipeline input exceeded one bounded socket buffer");
    client.abort();
    // Resume the test-only output pause so Qt can observe closure after its
    // full input buffer applies backpressure. Preserve the cleanup assertion.
    server1_http_server_set_drain_paused(server,0);
    pumpUntil([&] {return server1_http_server_active_connections(server)==0;},"paused input disconnect released owner");
    require(server1_http_server_input_backlog_bytes(server)==0,"disconnect retained pipeline bytes");
    server1_http_server_set_drain_paused(server,0);
    QTcpSocket pipeline;
    pipeline.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(server));
    require(pipeline.waitForConnected(1000),"pipeline client connected");
    pipeline.write("GET /wait HTTP/1.1\r\nHost: localhost\r\n\r\n"
                   "GET /wait HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    pipeline.flush();
    const auto wire=collectUntilDisconnected(pipeline);
    require(wire.count("HTTP/1.1 200 OK")==2 && wire.count("\r\n\r\nok")==2,
            "resumed pipelined requests lost responses");
    require(server1_http_router_add_static(router,0,0,"GET","/later",200,"later")==1,
            "later response registered");
    server1_http_server_set_drain_paused(server,1);
    QTcpSocket bufferedClient;
    bufferedClient.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(server));
    require(bufferedClient.waitForConnected(1000),"buffered client connected");
    bufferedClient.write("GET /wait HTTP/1.1\r\nHost: localhost\r\n\r\n"); bufferedClient.flush();
    pumpUntil([&] {return server1_http_server_queued_bytes(server)>0;},"buffered first response queued");
    const QByteArray later="GET /later HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    bufferedClient.write(later); bufferedClient.flush();
    pumpUntil([&] {return server1_http_server_input_backlog_bytes(server)>=static_cast<std::size_t>(later.size());},
              "second request entered bounded socket buffer while response paused");
    server1_http_server_set_drain_paused(server,0);
    const auto resumed=collectUntilDisconnected(bufferedClient);
    require(resumed.count("HTTP/1.1 200 OK")==2 && resumed.endsWith("later")
        && resumed.indexOf("\r\n\r\nok")<resumed.indexOf("\r\n\r\nlater"),
        "buffered second request was lost or reordered after resume");
    server1_http_server_destroy(server);
    server1_http_router_destroy(router);
    std::cout << "H00-05 input backpressure PASS\n";
}

void caseH00_06()
{
    for (const QByteArray &method : {QByteArray("GET"),QByteArray("HEAD")}) {
        int retired=0;
        auto handler=+[](const void *,void *response,server1_http_next_fn,void *,void *context) {
            require(server1_http_response_set_lifetime(response,+[](void *p){++*static_cast<int *>(p);},context)==1,
                    "response lifetime installed");
            require(server1_http_response_set_lifetime(response,+[](void *){},nullptr)==0,
                    "duplicate lifetime retains caller ownership");
            server1_http_response_set_status(response,200);
            return server1_http_response_set_body(response,"ok",2);
        };
        void *router=server1_http_router_create();
        require(server1_http_router_add_handler(router,0,0,"GET","/lease",handler,&retired,nullptr)==1,"lifetime route");
        void *server=server1_http_server_create(router,65536);
        require(server1_http_server_listen(server,0)==1,"lifetime listener");
        server1_http_server_set_drain_paused(server,1);
        QTcpSocket client;client.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(server));
        require(client.waitForConnected(1000),"lifetime connected");
        client.write(method+" /lease HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");client.flush();
        pumpUntil([&]{return server1_http_server_queued_bytes(server)>0;},"lifetime queued");
        require(retired==0,"buffered/HEAD response lifetime ended before drain");
        server1_http_server_set_drain_paused(server,0);
        const auto wire=collectUntilDisconnected(client);
        require(wire.contains("200 OK"),"lifetime response status");
        pumpUntil([&]{return retired==1;},"lifetime retired after drain");
        server1_http_server_destroy(server);server1_http_router_destroy(router);
        require(retired==1,"finish then disconnect double-retired response");
    }
    std::cout<<"H00-06 response lifetime GET/HEAD/drain PASS\n";
}

void caseH00_07()
{
    struct Context {int mode;int retired=0;int readerClosed=0;int reads=0;void *server=nullptr;};
    auto reader=+[](void *opaque,void *buffer,std::size_t capacity)->std::ptrdiff_t {
        auto &c=*static_cast<Context *>(opaque);++c.reads;
        if (c.mode==3) return -1;
        if (c.reads>1) return 0;
        if (capacity<3) return -1;
        std::memcpy(buffer,"abc",3);return 3;
    };
    auto readerClosed=+[](void *opaque){++static_cast<Context *>(opaque)->readerClosed;};
    auto retired=+[](void *opaque){auto &c=*static_cast<Context *>(opaque);++c.retired;
        if(c.mode==5)server1_http_server_stop(c.server);};
    struct Handler {Context *context;server1_http_stream_read_fn read;server1_http_stream_destroy_fn close;
        server1_http_context_destroy_fn retire;};
    auto handler=+[](const void *,void *response,server1_http_next_fn,void *,void *opaque){
        auto &h=*static_cast<Handler *>(opaque);auto &c=*h.context;
        require(server1_http_response_set_lifetime(response,h.retire,&c)==1,"adverse lifetime installed");
        server1_http_response_set_status(response,200);
        if(c.mode==0 || c.mode==7) return server1_http_response_set_body(response,"abc",3);
        return server1_http_response_set_stream(response,h.read,h.close,&c,c.mode==4 ? 2 : 3);
    };
    for(int mode=0;mode<8;++mode) {
        Context c{mode};Handler h{&c,reader,readerClosed,retired};
        void *router=server1_http_router_create();
        require(server1_http_router_add_handler(router,0,0,"GET","/owned",handler,&h,nullptr)==1,"adverse route");
        c.server=server1_http_server_create(router,mode>=6 ? 8 : 65536);
        require(server1_http_server_listen(c.server,0)==1,"adverse listener");
        if(mode<3)server1_http_server_set_drain_paused(c.server,1);
        QTcpSocket client;client.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(c.server));
        require(client.waitForConnected(1000),"adverse connected");
        client.write("GET /owned HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");client.flush();
        if(mode<3){
            pumpUntil([&]{return server1_http_server_queued_bytes(c.server)>0;},"adverse response queued");
            require(c.retired==0,"adverse response prematurely retired");
            if(mode==2)server1_http_server_stop(c.server);else client.abort();
        }else{static_cast<void>(collectUntilDisconnected(client));}
        pumpUntil([&]{return c.retired==1;},"adverse lifetime retired");
        require(c.readerClosed==(mode==0 || mode==7 ? 0 : 1),"reader ownership retired incorrectly");
        server1_http_server_destroy(c.server);server1_http_router_destroy(router);
        require(c.retired==1,"adverse close double retired lifetime");
    }
    // Replacing a body/stream or falling through to a static route must retain
    // the same logical response obligation, independently of its reader.
    Context c{9};Handler h{&c,reader,readerClosed,retired};
    auto middleware=+[](const void *,void *response,server1_http_next_fn next,void *nextContext,void *opaque){
        auto &h=*static_cast<Handler *>(opaque);
        require(server1_http_response_set_lifetime(response,h.retire,h.context)==1,"middleware lifetime installed");
        server1_http_response_set_stream(response,h.read,h.close,h.context,3);
        server1_http_response_set_body(response,"replacement",11);
        require(h.context->readerClosed==1 && h.context->retired==0,"body replacement retired response instead of reader");
        return next(nextContext);
    };
    void *router=server1_http_router_create();
    require(server1_http_router_add_middleware(router,0,"/",middleware,&h,nullptr)==1,"lifetime middleware");
    require(server1_http_router_add_static(router,0,0,"GET","/next",200,"next")==1,"lifetime static destination");
    void *response=server1_http_router_dispatch(router,"GET","/next","");
    require(c.retired==0 && std::string(server1_http_response_body(response))=="next","route replacement lost lifetime");
    server1_http_response_destroy(response);server1_http_router_destroy(router);
    require(c.retired==1 && c.readerClosed==1,"replaced response lifetime leaked or double closed");
    std::cout<<"H00-07 abort/stop/error/cap/reentrant retirement/body replacement PASS\n";
}

void caseH00_08()
{
    struct Context {void *server=nullptr;int retired=0;};
    for(int mode=0;mode<4;++mode) {
        Context c;
        auto handler=+[](const void *,void *response,server1_http_next_fn,void *,void *opaque){
            server1_http_response_set_lifetime(response,+[](void *opaque){
                auto &c=*static_cast<Context *>(opaque);++c.retired;
                void *server=c.server;c.server=nullptr;server1_http_server_destroy(server);
            },opaque);
            server1_http_response_set_status(response,200);
            return server1_http_response_set_body(response,"owned",5);
        };
        void *router=server1_http_router_create();
        require(server1_http_router_add_handler(router,0,0,"GET","/destroy",handler,&c,nullptr)==1,"destroy route");
        c.server=server1_http_server_create(router,mode==0 ? 8 : 65536);
        require(server1_http_server_listen(c.server,0)==1,"destroy listener");
        if(mode==1 || mode==3)server1_http_server_set_drain_paused(c.server,1);
        QTcpSocket client;client.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(c.server));
        require(client.waitForConnected(1000),"destroy connected");
        client.write("GET /destroy HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");client.flush();
        if(mode==1 || mode==3) {
            pumpUntil([&]{return server1_http_server_queued_bytes(c.server)>0;},"destroy response queued");
            if(mode==1)client.abort();else server1_http_server_stop(c.server);
        }
        pumpUntil([&]{return c.retired==1;},"destroy callback occurred");
        require(!c.server,"destroy callback retained server");
        server1_http_router_destroy(router);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    std::cout<<"H00-08 finalizer server destruction cap/abort/drain/stop PASS\n";
}

void caseH00_09()
{
    auto handler=+[](const void *request,void *response,server1_http_next_fn,void *,void *context){
        ++*static_cast<int *>(context);
        std::string result="range="+std::to_string(server1_http_request_header_count(request,"RaNgE"));
        result+=";first="+std::string(server1_http_request_header_value_at(request,"range",0));
        result+=";second="+std::string(server1_http_request_header_value_at(request,"RANGE",1));
        result+=";prio="+std::string(server1_http_request_header_value_at(request,"enginefs-prio",0));
        result+=";body="+std::to_string(server1_http_request_body_size(request))+":";
        result.append(server1_http_request_body(request),server1_http_request_body_size(request));
        server1_http_response_set_status(response,200);
        return server1_http_response_set_body(response,result.data(),result.size());
    };
    int calls=0;
    void *router=server1_http_router_create();
    require(server1_http_router_add_handler(router,0,0,"POST","/headers",handler,&calls,nullptr)==1,"header route");
    void *server=server1_http_server_create(router,65536);
    require(server1_http_server_listen(server,0)==1,"header listener");
    QTcpSocket client;client.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(server));
    require(client.waitForConnected(1000),"header client");
    QByteArray request="POST /headers HTTP/1.1\r\nHost: localhost\r\nRange: bytes=4-\r\nrange: bytes=8-10\r\nEngineFS-Prio: 7\r\nContent-Length: 3\r\nConnection: close\r\n\r\n";
    request.append("a\0b",3);client.write(request);client.flush();
    const auto wire=collectUntilDisconnected(client);
    QByteArray expected="range=2;first=bytes=4-;second=bytes=8-10;prio=7;body=3:";expected.append("a\0b",3);
    const auto split=wire.indexOf("\r\n\r\n");
    require(split>=0 && wire.mid(split+4)==expected,"native dispatch lost duplicate headers, raw range or binary request bytes");
    for(const unsigned char control : {0,1,31,127}) {
        QTcpSocket bad;bad.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(server));
        require(bad.waitForConnected(1000),"malformed header client");
        QByteArray input="POST /headers HTTP/1.1\r\nHost: localhost\r\nRange: bytes=4-";
        input.append(static_cast<char>(control));input.append("junk\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        bad.write(input);bad.flush();const auto rejected=collectUntilDisconnected(bad);
        require(rejected.startsWith("HTTP/1.1 400 ") && calls==1,"prohibited header control reached handler or was silently truncated");
    }
    QTcpSocket valid;valid.connectToHost(QStringLiteral("127.0.0.1"),server1_http_server_port(server));
    require(valid.waitForConnected(1000),"valid header control client");
    QByteArray positive="POST /headers HTTP/1.1\r\nHost: localhost\r\nRange: \tbytes=2-\t\r\nEngineFS-Prio: a\tb";
    positive.append(static_cast<char>(0x80));positive.append("\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    valid.write(positive);valid.flush();const auto accepted=collectUntilDisconnected(valid);
    QByteArray expectedValid="range=1;first=bytes=2-;second=;prio=a\tb";expectedValid.append(static_cast<char>(0x80));expectedValid.append(";body=0:");
    require(accepted.startsWith("HTTP/1.1 200 ") && accepted.endsWith(expectedValid) && calls==2,"valid tab/obs-text header bytes changed");
    server1_http_server_destroy(server);server1_http_router_destroy(router);
    std::cout<<"H00-09 native ordered headers and binary request body PASS\n";
}

void emitTrace()
{
    void *parser = server1_http_parser_create(3U * 1024U * 1024U);
    const std::string wire =
        "POST /submit?tag=one&tag=two&name=hello+world HTTP/1.1\r\n"
        "Host: localhost\r\nX-Trace: first\r\nx-trace: second\r\n"
        "Content-Type: application/json\r\nContent-Length: 17\r\n\r\n"
        "{\"ok\":true,\"n\":1}";
    const int parseResult = server1_http_parser_feed(parser, wire.data(), wire.size());
    std::cout << "candidate H00-01 result=" << parseResult
              << " method=" << server1_http_parser_method(parser)
              << " path=" << server1_http_parser_path(parser)
              << " query=" << server1_http_parser_query_key_at(parser, 0) << ":"
              << server1_http_parser_query_value_at(parser, 0) << "|"
              << server1_http_parser_query_key_at(parser, 1) << ":"
              << server1_http_parser_query_value_at(parser, 1) << "|"
              << server1_http_parser_query_key_at(parser, 2) << ":"
              << server1_http_parser_query_value_at(parser, 2)
              << " body-kind=" << server1_http_parser_body_kind(parser)
              << " duplicate-x-trace=" << server1_http_parser_header_count(parser, "x-trace")
              << " keep-alive=" << (server1_http_parser_keep_alive(parser) ? "true" : "false")
              << '\n';
    server1_http_parser_destroy(parser);

    void *router = server1_http_router_create();
    server1_http_router_add_static(router, 1, 0, "GET", "/hlsv2/status", 200, "external-hls");
    server1_http_router_add_static(router, 1, 0, "GET", "/yt/:id", 200, "yt:{id}");
    server1_http_router_add_static(router, 1, 1, "USE", "/proxy", 200, "external-proxy");
    server1_http_router_add_static(router, 1, 0, "GET", "/local-addon/manifest.json", 200,
                                   "external-addon");
    server1_http_router_add_static(router, 0, 0, "GET", "/:infoHash/:idx", 200,
                                   "root:{infoHash}:{idx}");
    for (const auto &vector : {std::pair<const char *, const char *>{"/hlsv2/status", "external-hls"},
                               {"/yt/id", "yt:id"}, {"/proxy/anything", "external-proxy"},
                               {"/local-addon/manifest.json", "external-addon"}}) {
        void *response = server1_http_router_dispatch(router, "GET", vector.first, "");
        std::cout << "candidate H00-02 target=" << vector.first
                  << " status=" << server1_http_response_status(response)
                  << " body=" << server1_http_response_body(response) << '\n';
        server1_http_response_destroy(response);
    }
    server1_http_router_destroy(router);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        const std::string requested = argc > 1 ? argv[1] : "all";
        if (requested == "--trace") {
            emitTrace();
            return 0;
        }
        if (requested == "all" || requested == "H00-01")
            caseH00_01();
        if (requested == "all" || requested == "H00-02")
            caseH00_02();
        if (requested == "all" || requested == "H00-03")
            caseH00_03();
        if (requested == "all" || requested == "H00-04")
            caseH00_04();
        if (requested == "all" || requested == "H00-09") caseH00_09();
        if (requested == "all" || requested == "H00-08") caseH00_08();
        if (requested == "all" || requested == "H00-07") caseH00_07();
        if (requested == "all" || requested == "H00-06") caseH00_06();
        if (requested == "all" || requested == "H00-05") caseH00_05();
        if (requested != "all" && requested != "H00-01" && requested != "H00-02"
            && requested != "H00-03" && requested != "H00-04" && requested != "H00-05" && requested != "H00-06" && requested != "H00-07" && requested != "H00-08" && requested != "H00-09")
            throw std::runtime_error("unknown H00 case");
    } catch (const std::exception &error) {
        std::cerr << "H00 FAIL: " << error.what() << '\n';
        return 1;
    }
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    return 0;
}
