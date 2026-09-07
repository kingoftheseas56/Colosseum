#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtNetwork/QTcpSocket>

#include <cstddef>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

extern "C" {

void *server1_http_parser_create(std::size_t maxBodyBytes);
void server1_http_parser_destroy(void *parser);
int server1_http_parser_feed(void *parser, const char *data, std::size_t size);
int server1_http_parser_error_status(void *parser);
const char *server1_http_parser_error(void *parser);
std::size_t server1_http_parser_consumed(void *parser);
const char *server1_http_parser_method(void *parser);
const char *server1_http_parser_target(void *parser);
const char *server1_http_parser_path(void *parser);
const char *server1_http_parser_body(void *parser);
int server1_http_parser_body_kind(void *parser);
bool server1_http_parser_keep_alive(void *parser);
std::size_t server1_http_parser_header_count(void *parser, const char *name);
const char *server1_http_parser_header_value_at(void *parser, const char *name, std::size_t index);
std::size_t server1_http_parser_query_entry_count(void *parser);
const char *server1_http_parser_query_key_at(void *parser, std::size_t index);
const char *server1_http_parser_query_value_at(void *parser, std::size_t index);
std::size_t server1_http_parser_form_entry_count(void *parser);
const char *server1_http_parser_form_key_at(void *parser, std::size_t index);
const char *server1_http_parser_form_value_at(void *parser, std::size_t index);

void *server1_http_router_create();
void server1_http_router_destroy(void *router);
int server1_http_router_add_static(void *router, int external, int prefix, const char *method,
                                   const char *pattern, int status, const char *body);
const char *server1_http_router_error(void *router);
void *server1_http_router_dispatch(void *router, const char *method, const char *target,
                                   const char *body);
void server1_http_response_destroy(void *response);
int server1_http_response_status(void *response);
const char *server1_http_response_body(void *response);
const char *server1_http_response_header(void *response, const char *name);

void *server1_http_server_create(void *router, std::size_t maxQueuedBytes);
void server1_http_server_destroy(void *server);
int server1_http_server_listen(void *server, unsigned short port);
void server1_http_server_stop(void *server);
unsigned short server1_http_server_port(void *server);
const char *server1_http_server_error(void *server);
void server1_http_server_set_drain_paused(void *server, int paused);
std::size_t server1_http_server_queued_bytes(void *server);
std::size_t server1_http_server_max_queued_bytes(void *server);
std::size_t server1_http_server_active_connections(void *server);

}

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

    std::cout << "H00-01 PASS\n";
}

void caseH00_02()
{
    void *router = server1_http_router_create();
    require(router != nullptr, "H00-02 production router was created");
    require(server1_http_router_add_static(router, 1, 0, "GET", "/hlsv2/status", 200,
                                           "external-hls")
                == 1,
            "external hls route is registered");
    require(server1_http_router_add_static(router, 1, 0, "GET", "/yt/:id", 200, "yt:{id}") == 1,
            "external YouTube route is registered");
    require(server1_http_router_add_static(router, 1, 1, "USE", "/proxy", 200, "external-proxy")
                == 1,
            "external proxy prefix route is registered");
    require(server1_http_router_add_static(router, 1, 0, "GET", "/local-addon/manifest.json", 200,
                                           "external-addon")
                == 1,
            "external addon route is registered");
    require(server1_http_router_add_static(router, 0, 0, "GET", "/:infoHash/:idx", 200,
                                           "root:{infoHash}:{idx}")
                == 1,
            "root torrent route is registered");
    require(server1_http_router_add_static(router, 0, 0, "GET", "/head", 200, "head") == 1,
            "root GET route is registered");
    require(server1_http_router_add_static(router, 1, 0, "GET", "/file/:name", 200, "name:{name}")
                == 1,
            "encoded parameter route is registered");
    require(server1_http_router_add_static(router, 0, 0, "GET", "/:/", 200, "bad") == 0,
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

    void *prefixBoundary = server1_http_router_dispatch(router, "GET", "/proxyx/anything/more", "");
    require(server1_http_response_status(prefixBoundary) == 404,
            "connect prefix routes require a slash or dot boundary");
    server1_http_response_destroy(prefixBoundary);

    void *head = server1_http_router_dispatch(router, "HEAD", "/head", "");
    require(server1_http_response_status(head) == 200, "HEAD falls back to GET route");
    server1_http_response_destroy(head);

    void *options = server1_http_router_dispatch(router, "OPTIONS", "/head", "");
    require(server1_http_response_status(options) == 200, "OPTIONS gets automatic route response");
    require(std::string(server1_http_response_header(options, "Allow")).find("GET") != std::string::npos,
            "OPTIONS exposes the route methods");
    server1_http_response_destroy(options);

    void *badTarget = server1_http_router_dispatch(router, "GET", "not-a-target", "");
    require(server1_http_response_status(badTarget) == 400,
            "router rejects malformed HTTP target independently of route registration");
    server1_http_response_destroy(badTarget);

    server1_http_router_destroy(router);
    std::cout << "H00-02 PASS\n";
}

void caseH00_03()
{
    void *router = server1_http_router_create();
    const std::string payload(512U * 1024U, 'x');
    require(server1_http_router_add_static(router, 0, 0, "GET", "/large", 200, payload.c_str()) == 1,
            "slow-consumer route is registered");

    constexpr std::size_t maxQueuedBytes = 1024U * 1024U;
    void *server = server1_http_server_create(router, maxQueuedBytes);
    require(server != nullptr, "HTTP listener was created");
    require(server1_http_server_listen(server, 0) == 1, "HTTP listener started asynchronously");
    require(server1_http_server_port(server) != 0, "ephemeral listener port is available");
    server1_http_server_set_drain_paused(server, 1);

    QTcpSocket client;
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
    require(server1_http_server_queued_bytes(server) <= maxQueuedBytes,
            "response queue stays within the explicit backpressure bound");

    client.abort();
    pumpUntil([&] {
        return server1_http_server_active_connections(server) == 0
            && server1_http_server_queued_bytes(server) == 0;
    }, "disconnect releases queued response bytes and connection ownership");
    server1_http_server_stop(server);
    server1_http_server_destroy(server);
    server1_http_router_destroy(router);

    std::cout << "H00-03 PASS\n";
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
        if (requested != "all" && requested != "H00-01" && requested != "H00-02"
            && requested != "H00-03")
            throw std::runtime_error("unknown H00 case");
    } catch (const std::exception &error) {
        std::cerr << "H00 FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
