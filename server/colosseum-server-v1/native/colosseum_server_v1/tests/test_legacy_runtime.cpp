#include "server1/http/HttpContract.h"
#include "server1/http/LegacyMediaRoutes.h"
#include "server1/media/LegacyHlsService.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <cerrno>
#include <csignal>
#include <functional>
#include <iostream>

// Oracle provenance: official server.js SHA256
// 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f.
// Module 564 forwards a relative /probe URL and 40-character source identifiers
// using enginefs.baseUrlLocal at request time. Modules 305/564 serialize runtime
// probe failures as JSON null, but an early missing-ffmpeg callback omits result.
// Lifecycle cases below are ownership regressions, not fabricated oracle rows.
using server1::media::LegacyHlsService;
using server1::media::LegacyOperation;
using server1::media::LegacyResponse;
namespace {
int failures = 0;
void check(bool condition, const char* label) {
    std::cout << (condition ? "PASS " : "FAIL ") << label << std::endl;
    if (!condition) ++failures;
}
bool spin(const std::function<bool()>& ready, int limit = 5000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < limit) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}
QJsonArray events(const QString& path) {
    QFile file(path); QJsonArray result;
    if (!file.open(QIODevice::ReadOnly)) return result;
    for (const auto& line : file.readAll().split('\n')) {
        auto document = QJsonDocument::fromJson(line);
        if (document.isObject()) result.append(document.object());
    }
    return result;
}
QJsonArray matching(const QString& path, QString source, QString phase, QString kind = "start") {
    QJsonArray result;
    for (auto value : events(path)) {
        const auto event = value.toObject();
        if (event["source"] == source && event["phase"] == phase && event["event"] == kind)
            result.append(event);
    }
    return result;
}
bool terminated(qint64 pid) {
    errno = 0;
    return pid > 0 && kill(pid_t(pid), 0) == -1 && errno == ESRCH;
}
QJsonObject input(QString source) {
    return {{"params", QJsonObject{{"from", source}}}, {"query", QJsonObject{}}};
}
struct Response { bool finished; int status; QByteArray type; QByteArray body; };
struct HttpHarness {
    LegacyHlsService service;
    void* router = nullptr;
    void* server = nullptr;
    QNetworkAccessManager network;
    explicit HttpHarness(QString executable) : service({executable, {}, {}}) {
        router = server1_http_router_create();
        server1_http_router_add_static(router, 0, 0, "GET", "/fixture", 200, "fixture-payload");
        server1_http_router_add_static(router, 0, 0, "GET", "/0123456789abcdef0123456789abcdef01234567/3", 200, "torrent-fixture");
        // Routes are mounted before listen(0), as in ServerComposition.
        check(server1::http::installLegacyMediaRoutes(router, &service, [this] { return root(); }),
              "install routes before dynamic listener port exists");
        server = server1_http_server_create(router, 65536);
        check(server1_http_server_listen(server, 0), "listen on loopback ephemeral port");
    }
    ~HttpHarness() {
        server1_http_server_destroy(server);
        server1_http_router_destroy(router);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
    QString root() const {
        return "http://127.0.0.1:" + QString::number(server ? server1_http_server_port(server) : 0);
    }
    QNetworkReply* start(QString path) {
        return network.get(QNetworkRequest(QUrl(root() + path)));
    }
    Response fetch(QString path, int limit = 5000) {
        auto reply = start(path);
        const bool finished = spin([&] { return reply->isFinished(); }, limit);
        Response result{finished, reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                        reply->rawHeader("content-type"), reply->readAll()};
        if (!finished) reply->abort();
        reply->deleteLater();
        return result;
    }
};
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { std::cerr << "usage: test fixture.py [--timeout]\n"; return 2; }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    const auto log = temporary.filePath("events.jsonl");
    qputenv("SERVER1_LEGACY_RUNTIME_LOG", log.toUtf8());
    {
        HttpHarness http(fixture);
        auto relative = http.fetch("/probe?url=%2Ffixture");
        const auto relativeValue = QJsonDocument::fromJson(relative.body).object();
        check(relative.finished && relative.status == 200 && relative.type == "application/json"
              && relativeValue["duration"].toInt() == 7000
              && matching(log, http.root() + "/fixture", "probe", "fetch").size() == 1,
              "relative /probe fetches actual ephemeral listener, not a captured port");
        const QString hashSource = http.root() + "/0123456789abcdef0123456789abcdef01234567/3";
        auto hash = http.fetch("/0123456789abcdef0123456789abcdef01234567/3/master.m3u8");
        check(hash.finished && hash.status == 200 && hash.body.contains("stream-0.m3u8")
              && matching(log, hashSource, "probe", "fetch").size() == 1,
              "40-character source maps to actual server port and file index");
        auto file = http.fetch("/file/local-fixture/master.m3u8");
        check(file.finished && file.status == 200 && matching(log, "local-fixture", "probe").size() == 1,
              "file source preserves local input instead of prefixing loopback base");
        auto url = http.fetch("/url/" + QString::fromLatin1(QUrl::toPercentEncoding(http.root() + "/fixture")) + "/thumb.jpg");
        check(url.finished && url.status == 200 && url.type == "image/jpg" && url.body.startsWith(QByteArray::fromHex("ffd8")),
              "url source percent-decodes once and produces thumbnail stream");
        auto invalid = http.fetch("/file/invalid-probe/master.m3u8");
        check(invalid.finished && invalid.status == 415 && invalid.body == "no video stream found",
              "invalid prepared source preserves 415 legacy video error");
        auto empty = http.fetch("/probe?url=fixture%3A%2F%2Finvalid-probe");
        check(empty.finished && empty.status == 200 && empty.body == "{\"streams\":[]}",
              "zero-exit invalid probe preserves source's empty-stream success");
        auto failed = http.fetch("/file/failed-convert/thumb.jpg");
        check(failed.finished && failed.status == 500 && failed.body.isEmpty(),
              "converter failing before output returns 500 without fabricated payload");
        auto reply = http.start("/probe?url=fixture%3A%2F%2Fhang-probe-http");
        const QString source = "fixture://hang-probe-http";
        const bool observed = spin([&] { return !matching(log, source, "probe").isEmpty(); });
        const auto pid = observed ? matching(log, source, "probe").first().toObject()["pid"].toInteger() : -1;
        reply->abort(); reply->deleteLater();
        check(observed && spin([&] { return terminated(pid); }),
              "HTTP abort during deferred probe terminates the observed OS child");
    }
    {
        HttpHarness missing{QString()};
        auto response = missing.fetch("/probe?url=fixture%3A%2F%2Fabsent");
        check(response.finished && response.status == 500 && response.body.isEmpty(),
              "missing ffmpeg early callback remains an empty /probe body");
    }
    {
        HttpHarness broken(temporary.filePath("no-such-ffmpeg"));
        auto response = broken.fetch("/probe?url=fixture%3A%2F%2Fspawn-error");
        check(response.finished && response.status == 500 && response.type == "application/json" && response.body == "null",
              "runtime spawn failure serializes /probe result as JSON null");
        if (response.body != "null") std::cerr << "spawn error status=" << response.status << " body hex=" << response.body.toHex().constData() << '\n';
    }
    {
        LegacyHlsService service({fixture, {}, {}});
        bool callback = false;
        const QString source = "hang-probe-direct";
        auto operation = service.request("thumb", input(source), [&](LegacyResponse) { callback = true; });
        const bool observed = spin([&] { return !matching(log, source, "probe").isEmpty(); });
        const auto pid = observed ? matching(log, source, "probe").first().toObject()["pid"].toInteger() : -1;
        operation->cancel();
        const bool dead = spin([&] { return terminated(pid); });
        spin([] { return false; }, 50);
        check(observed && dead && !callback, "direct cancellation kills pending probe PID without invoking response callback");
        bool teardownCallback = false;
        std::shared_ptr<LegacyOperation> survivor;
        qint64 teardownPid = -1;
        {
            LegacyHlsService owned({fixture, {}, {}});
            survivor = owned.request("thumb", input("hang-probe-teardown"), [&](LegacyResponse) { teardownCallback = true; });
            if (spin([&] { return !matching(log, "hang-probe-teardown", "probe").isEmpty(); }))
                teardownPid = matching(log, "hang-probe-teardown", "probe").first().toObject()["pid"].toInteger();
        }
        char byte = 0;
        check(spin([&] { return terminated(teardownPid); }) && !teardownCallback && survivor->read(&byte, 1) == -1,
              "service teardown cancels probe even while caller retains operation");
        QVector<std::shared_ptr<LegacyOperation>> operations;
        int callbacks = 0;
        for (int i = 0; i < 10; ++i)
            operations.append(service.request("thumb", input("queue-hold-" + QString::number(i)), [&](LegacyResponse) { ++callbacks; }));
        auto conversionCount = [&] { int count = 0; for (auto v : events(log)) { auto e = v.toObject(); if (e["source"].toString().startsWith("queue-hold-") && e["phase"] == "convert" && e["event"] == "start") ++count; } return count; };
        const bool eight = spin([&] { return conversionCount() == 8 && callbacks == 8; });
        // Queue arrival order follows probe completion, not request construction.
        int waiting = -1, running = -1;
        for (int i = 0; i < 10; ++i) {
            if (matching(log, "queue-hold-" + QString::number(i), "convert").isEmpty()) waiting = i;
            else running = i;
        }
        const bool allProbed = spin([&] {
            for (int i = 0; i < 10; ++i) {
                const auto probes = matching(log, "queue-hold-" + QString::number(i), "probe");
                if (probes.size() != 1 || !terminated(probes.first().toObject()["pid"].toInteger()))
                    return false;
            }
            return true;
        }); // Probes have exited and been reaped: the two remaining requests are queued conversions.
        spin([] { return false; }, 25);
        if (waiting >= 0) operations[waiting]->cancel();
        if (running >= 0) operations[running]->cancel();
        const bool resumed = spin([&] { return conversionCount() == 9 && callbacks == 9; });
        check(eight && allProbed && resumed && waiting >= 0 && matching(log, "queue-hold-" + QString::number(waiting), "convert").isEmpty(),
              "eight-converter bound skips canceled queued work and starts the next request");
        for (auto operation : operations) operation->cancel();
        check(spin([&] { for (auto value : events(log)) { const auto e = value.toObject(); if (e["source"].toString().startsWith("queue-hold-") && !terminated(e["pid"].toInteger())) return false; } return true; }),
              "queued/running cleanup terminates every observed fixture PID");
    }
    if (app.arguments().contains("--timeout")) {
        HttpHarness http(fixture);
        auto response = http.fetch("/probe?url=fixture%3A%2F%2Fhang-probe-timeout", 24000);
        const auto entries = matching(log, "fixture://hang-probe-timeout", "probe");
        const auto pid = entries.isEmpty() ? -1 : entries.first().toObject()["pid"].toInteger();
        check(response.finished && response.status == 500 && response.body == "null" && spin([&] { return terminated(pid); }),
              "20-second legacy probe timeout returns 500/null and kills child");
    }
    std::cout << "Runtime failures: " << failures << std::endl;
    return failures ? 1 : 0;
}
