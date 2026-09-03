#include "../NetworkAppServices.h"
#include "TestSupport.h"

#include <QCoreApplication>

#include <atomic>

using namespace colosseum::server::app;

class FakeProxyTransport final : public ProxyTransport {
public:
    QList<ProxyFetchRequest> seen;
    QList<ProxyFetchResponse> queued;

    ProxyFetchResponse fetch(const ProxyFetchRequest &request,
                             const std::atomic_bool *cancelled) override
    {
        Q_UNUSED(cancelled);
        seen.push_back(request);
        if (queued.isEmpty())
            return {.error = QStringLiteral("fixture exhausted")};
        return queued.takeFirst();
    }
};

static void testForwardingAndRedirects(TestState &t)
{
    FakeProxyTransport transport;
    transport.queued = {
        {.status = 302, .headers = {{"Location", "/final"}}},
        {.status = 206,
         .headers = {{"Content-Type", "video/mp4"},
                     {"Content-Length", "4"},
                     {"Content-Range", "bytes 2-5/8"},
                     {"X-Discard-Me", "no"}},
         .body = "CDEF"},
    };

    ProxyService service(transport);
    AppRequest req;
    req.method = "POST";
    req.path = "/proxy/d=http%3A%2F%2Forigin.test&h=X-Test%3Ayes&r=X-Reply%3Aok/blob.bin";
    req.query = QUrlQuery("token=abc");
    req.headers = {{"Range", "bytes=2-5"}, {"Authorization", "secret"}, {"User-Agent", "oracle"}};
    req.body = "upstream-body-is-intentionally-not-forwarded";

    const AppResponse response = service.handle(req);
    t.equal(response.status, 206, "proxy preserves final upstream status");
    t.equal(response.body, QByteArray("CDEF"), "proxy preserves response bytes");
    t.equal(headerValue(response.headers, "content-range"), QByteArray("bytes 2-5/8"),
            "proxy preserves allowed response headers");
    t.equal(headerValue(response.headers, "x-reply"), QByteArray("ok"),
            "proxy response override is applied");
    t.require(!hasHeader(response.headers, "x-discard-me"), "proxy drops unlisted response headers");

    t.equal(transport.seen.size(), 2, "proxy follows one redirect");
    t.equal(transport.seen[0].method, QByteArray("POST"), "proxy forwards method");
    t.equal(headerValue(transport.seen[0].headers, "range"), QByteArray("bytes=2-5"),
            "proxy forwards Range");
    t.equal(headerValue(transport.seen[0].headers, "x-test"), QByteArray("yes"),
            "proxy applies destination header override");
    t.require(!hasHeader(transport.seen[0].headers, "authorization"),
              "proxy does not forward non-whitelisted Authorization");
    t.require(transport.seen[0].body.isEmpty(),
              "module 805 quirk: request body is not forwarded");
    t.equal(transport.seen[1].url.path(), QString("/final"), "relative redirect is resolved");
}

static void testRelativeRedirectUsesOriginRoot(TestState &t)
{
    FakeProxyTransport transport;
    transport.queued = {
        {.status = 302, .headers = {{"Location", "next.bin"}}},
        {.status = 200, .body = "OK"},
    };
    ProxyService service(transport);
    AppRequest req;
    req.path = "/proxy/d=http%3A%2F%2Forigin.test/dir/blob.bin";
    const AppResponse response = service.handle(req);
    t.equal(response.status, 200, "relative redirect completes");
    t.equal(transport.seen.size(), 2, "relative redirect performs second fetch");
    t.equal(transport.seen[1].url.path(), QString("/next.bin"),
            "module 805 resolves relative redirects from destination origin root");
}

static void testPlaylistRewrite(TestState &t)
{
    FakeProxyTransport transport;
    transport.queued = {{
        .status = 200,
        .headers = {{"Content-Type", "application/vnd.apple.mpegurl"},
                    {"Content-Length", "999"}},
        .body = "#EXTM3U\nhttps://media.test/a.ts\nhttps://other.test/b.ts\n/root.ts\nrel.ts\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://other.test/key\"\n",
    }};

    ProxyService service(transport);
    AppRequest req;
    req.path = "/proxy/d=https%3A%2F%2Fmedia.test&h=User-Agent%3Astremio/master.m3u8";

    const AppResponse response = service.handle(req);
    const QString text = QString::fromUtf8(response.body);
    t.equal(response.status, 200, "playlist status");
    t.equal(headerValue(response.headers, "accept-ranges"), QByteArray("none"),
            "playlist disables ranges");
    t.equal(headerValue(response.headers, "transfer-encoding"), QByteArray("chunked"),
            "playlist forces chunked transfer");
    t.require(!hasHeader(response.headers, "content-length"), "playlist deletes content length");
    t.require(text.contains("/proxy/d=https%3A%2F%2Fmedia.test&h=User-Agent%3Astremio/a.ts"),
              "same-origin absolute URI is virtualized");
    t.require(text.contains("/proxy/d=https%3A%2F%2Fother.test&h=User-Agent%3Astremio/b.ts"),
              "cross-origin absolute URI gets a new destination");
    t.require(text.contains("\n/proxy/d=https%3A%2F%2Fmedia.test&h=User-Agent%3Astremio/root.ts\n"),
              "root-relative URI is virtualized");
    t.require(text.contains("\nrel.ts\n"), "ordinary relative URI is intentionally left alone");
    t.require(text.contains("URI=\"/proxy/d=https%3A%2F%2Fother.test&h=User-Agent%3Astremio/key\""),
              "URI attribute is rewritten");
}

static void testErrorContract(TestState &t)
{
    FakeProxyTransport transport;
    transport.queued = {{.error = QStringLiteral("connection refused")}};
    ProxyService service(transport);
    AppRequest req;
    req.path = "/proxy/d=http%3A%2F%2F127.0.0.1%3A1/blob.bin";
    req.headers = {{"Range", "bytes=17-80"}};

    const AppResponse response = service.handle(req);
    t.equal(response.status, 500, "transport failure maps to Express-compatible 500");
    t.equal(headerValue(response.headers, "content-type"), QByteArray("text/html; charset=utf-8"),
            "500 content type matches oracle");
    t.equal(response.body.size(), qsizetype(148), "500 body length matches Wave 0 oracle");
    t.require(response.body.contains("<pre>Internal Server Error</pre>"),
              "500 body preserves observable Express error text");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TestState t;
    testForwardingAndRedirects(t);
    testRelativeRedirectUsesOriginRoot(t);
    testPlaylistRewrite(t);
    testErrorContract(t);
    return finishTests(t, "proxy");
}
