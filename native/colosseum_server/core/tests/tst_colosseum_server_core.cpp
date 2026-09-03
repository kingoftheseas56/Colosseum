#include <QtTest>
#include <QTcpSocket>
#include <QUrl>
#include <QJsonObject>

#include "ColosseumServer.h"
#include "HttpRouter.h"

using namespace colosseum::server;

namespace {
struct RawResponse {
    int status = 0;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

RawResponse parseResponse(const QByteArray &wire)
{
    RawResponse out;
    const qsizetype split = wire.indexOf("\r\n\r\n");
    if (split < 0)
        return out;
    const QList<QByteArray> lines = wire.left(split).split('\n');
    if (!lines.isEmpty()) {
        const QList<QByteArray> parts = lines.first().trimmed().split(' ');
        if (parts.size() >= 2)
            out.status = parts.at(1).toInt();
    }
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            out.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }
    out.body = wire.mid(split + 4);
    return out;
}

QByteArray decodeChunked(QByteArray body)
{
    QByteArray out;
    for (;;) {
        const qsizetype eol = body.indexOf("\r\n");
        if (eol < 0)
            break;
        bool ok = false;
        const qsizetype size = body.left(eol).trimmed().toLongLong(&ok, 16);
        if (!ok)
            break;
        body.remove(0, eol + 2);
        if (size == 0)
            break;
        if (body.size() < size + 2)
            break;
        out += body.left(size);
        body.remove(0, size + 2);
    }
    return out;
}

QByteArray exchange(const QUrl &baseUrl, const QByteArray &request)
{
    QTcpSocket socket;
    socket.connectToHost(baseUrl.host(), static_cast<quint16>(baseUrl.port()));
    if (!socket.waitForConnected(3000))
        return {};
    socket.write(request);
    if (!socket.waitForBytesWritten(3000))
        return {};
    QByteArray wire;
    for (;;) {
        if (socket.bytesAvailable() > 0)
            wire += socket.readAll();
        if (socket.state() == QAbstractSocket::UnconnectedState)
            break;
        if (!socket.waitForReadyRead(3000)) {
            if (socket.state() == QAbstractSocket::UnconnectedState)
                break;
        }
    }
    wire += socket.readAll();
    return wire;
}

QByteArray requestWithBody(const QByteArray &method, const QByteArray &target,
                           const QByteArray &contentType, const QByteArray &body,
                           const QList<QPair<QByteArray, QByteArray>> &headers = {})
{
    QByteArray req = method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (!contentType.isEmpty())
        req += "Content-Type: " + contentType + "\r\n";
    for (const auto &header : headers)
        req += header.first + ": " + header.second + "\r\n";
    req += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    req += body;
    return req;
}
}

class ColosseumServerCoreTest final : public QObject
{
    Q_OBJECT
private slots:
    void lifecycleRepeatedStartStop();
    void routeQueryJsonAndFormContract();
    void corsDlnaStreamingAndErrors();
    void fixedLengthProgressiveBodyIsRaw();
    void cancellationTracksDisconnectedClient();
};

void ColosseumServerCoreTest::lifecycleRepeatedStartStop()
{
    ColosseumServer server;
    server.router().get("/ping", [](HttpRequest &, HttpResponse response) {
        response.setHeader("content-type", "application/json");
        response.end(R"({"success":true})");
        return true;
    });

    for (int i = 0; i < 3; ++i) {
        QVERIFY2(server.start(0), qPrintable(server.lastError()));
        QVERIFY(server.isRunning());
        QCOMPARE(server.boundUrl().host(), QStringLiteral("127.0.0.1"));
        QVERIFY(server.boundUrl().port() > 0);

        const RawResponse response = parseResponse(exchange(
            server.boundUrl(),
            "GET /ping HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"));
        QCOMPARE(response.status, 200);
        QCOMPARE(response.headers.value("content-type"), QByteArray("application/json"));
        QCOMPARE(response.body, QByteArray(R"({"success":true})"));

        server.stop();
        QVERIFY(!server.isRunning());
        QCOMPARE(server.activeConnectionCount(), 0);
    }
}

void ColosseumServerCoreTest::routeQueryJsonAndFormContract()
{
    ColosseumServer server;
    server.router().post("/items/:id/:tail?", [](HttpRequest &request, HttpResponse response) {
        if (request.parameter("id") != QStringLiteral("hello world")) {
            response.writeHead(500);
            response.end("bad id");
            return true;
        }
        if (!request.parameter("tail").isEmpty()) {
            response.writeHead(500);
            response.end("bad optional");
            return true;
        }
        if (request.queryValues("tag") != QStringList{QStringLiteral("one"), QStringLiteral("two words")}) {
            response.writeHead(500);
            response.end("bad query");
            return true;
        }
        if (!request.hasJsonBody || request.jsonBody.object().value("n").toInt() != 7) {
            response.writeHead(500);
            response.end("bad json");
            return true;
        }
        response.setHeader("content-type", "application/json");
        response.end(R"({"ok":true})");
        return true;
    });
    server.router().post("/form", [](HttpRequest &request, HttpResponse response) {
        const QStringList names = request.formValues("name");
        if (names != QStringList{QStringLiteral("Ada Lovelace"), QStringLiteral("Grace Hopper")}) {
            response.writeHead(500);
            response.end("bad form");
            return true;
        }
        response.end("form-ok");
        return true;
    });
    server.router().get("/files/:path(*)?", [](HttpRequest &request, HttpResponse response) {
        response.end(request.parameter("path").toUtf8());
        return true;
    });

    QVERIFY2(server.start(0), qPrintable(server.lastError()));

    const RawResponse json = parseResponse(exchange(
        server.boundUrl(),
        requestWithBody("POST", "/items/hello%20world?tag=one&tag=two+words",
                        "application/json", R"({"n":7})")));
    QCOMPARE(json.status, 200);
    QCOMPARE(json.body, QByteArray(R"({"ok":true})"));

    const RawResponse form = parseResponse(exchange(
        server.boundUrl(),
        requestWithBody("POST", "/form", "application/x-www-form-urlencoded",
                        "name=Ada+Lovelace&name=Grace%20Hopper")));
    QCOMPARE(form.status, 200);
    QCOMPARE(form.body, QByteArray("form-ok"));

    const RawResponse wildcard = parseResponse(exchange(
        server.boundUrl(),
        "GET /files/a/b%20c HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"));
    QCOMPARE(wildcard.status, 200);
    QCOMPARE(wildcard.body, QByteArray("a/b c"));

    server.stop();
}

void ColosseumServerCoreTest::corsDlnaStreamingAndErrors()
{
    ColosseumServer server;
    server.router().use("/", [](HttpRequest &request, HttpResponse response) {
        return applyCorsHeaders(request, response);
    });
    server.router().get("/media", [](HttpRequest &, HttpResponse response) {
        applyDlnaHeaders(response);
        response.end("media");
        return true;
    });
    server.router().get("/stream", [](HttpRequest &, HttpResponse response) {
        response.setHeader("content-type", "text/plain");
        response.write("alpha");
        response.write("beta");
        response.end();
        return true;
    });
    QVERIFY2(server.start(0), qPrintable(server.lastError()));

    const RawResponse cors = parseResponse(exchange(
        server.boundUrl(),
        "OPTIONS /media HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: https://app.strem.io\r\nAccess-Control-Request-Headers: Range, X-Test\r\nConnection: close\r\n\r\n"));
    QCOMPARE(cors.status, 200);
    QCOMPARE(cors.headers.value("access-control-allow-origin"), QByteArray("*"));
    QCOMPARE(cors.headers.value("access-control-allow-methods"), QByteArray("POST, GET, OPTIONS"));
    QCOMPARE(cors.headers.value("access-control-allow-headers"), QByteArray("Range, X-Test"));
    QCOMPARE(cors.headers.value("access-control-max-age"), QByteArray("1728000"));

    const RawResponse media = parseResponse(exchange(
        server.boundUrl(),
        "GET /media HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: https://app.strem.io\r\nConnection: close\r\n\r\n"));
    QCOMPARE(media.status, 200);
    QCOMPARE(media.headers.value("access-control-allow-origin"), QByteArray("*"));
    QCOMPARE(media.headers.value("transfermode.dlna.org"), QByteArray("Streaming"));
    QCOMPARE(media.headers.value("contentfeatures.dlna.org"),
             QByteArray("DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=017000 00000000000000000000000000"));

    const RawResponse stream = parseResponse(exchange(
        server.boundUrl(),
        "GET /stream HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"));
    QCOMPARE(stream.status, 200);
    QCOMPARE(stream.headers.value("transfer-encoding"), QByteArray("chunked"));
    QCOMPARE(decodeChunked(stream.body), QByteArray("alphabeta"));

    const RawResponse missing = parseResponse(exchange(
        server.boundUrl(),
        "GET /missing HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"));
    QCOMPARE(missing.status, 404);

    const RawResponse badJson = parseResponse(exchange(
        server.boundUrl(),
        requestWithBody("POST", "/media", "application/json", "{")));
    QCOMPARE(badJson.status, 400);

    const RawResponse tooLarge = parseResponse(exchange(
        server.boundUrl(),
        "POST /media HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: 3145729\r\nConnection: close\r\n\r\n"));
    QCOMPARE(tooLarge.status, 413);

    server.stop();
}

void ColosseumServerCoreTest::cancellationTracksDisconnectedClient()
{
    ColosseumServer server;
    std::shared_ptr<CancellationToken> observed;
    server.router().get("/hold", [&observed](HttpRequest &request, HttpResponse) {
        observed = request.cancellation;
        return true;
    });
    QVERIFY2(server.start(0), qPrintable(server.lastError()));

    QTcpSocket socket;
    socket.connectToHost(server.boundUrl().host(), static_cast<quint16>(server.boundUrl().port()));
    QVERIFY(socket.waitForConnected(3000));
    socket.write("GET /hold HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    QVERIFY(socket.waitForBytesWritten(3000));
    QTRY_VERIFY_WITH_TIMEOUT(observed != nullptr, 3000);
    QVERIFY(!observed->isCancelled());
    socket.abort();
    QTRY_VERIFY_WITH_TIMEOUT(observed->isCancelled(), 3000);

    server.stop();
}

void ColosseumServerCoreTest::fixedLengthProgressiveBodyIsRaw()
{
    ColosseumServer server;
    server.router().get("/fixed", [](HttpRequest &, HttpResponse response) {
        response.setHeader("content-type", "application/octet-stream");
        response.setHeader("content-length", "9");
        response.write("alpha");
        response.write("beta");
        response.end();
        return true;
    });

    QVERIFY2(server.start(0), qPrintable(server.lastError()));
    const RawResponse fixed = parseResponse(exchange(
        server.boundUrl(),
        "GET /fixed HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"));

    QCOMPARE(fixed.status, 200);
    QCOMPARE(fixed.headers.value("content-length"), QByteArray("9"));
    QVERIFY(!fixed.headers.contains("transfer-encoding"));
    QCOMPARE(fixed.body, QByteArray("alphabeta"));

    server.stop();
}

QTEST_GUILESS_MAIN(ColosseumServerCoreTest)
#include "tst_colosseum_server_core.moc"
