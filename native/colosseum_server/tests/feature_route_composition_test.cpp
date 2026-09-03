#include "core/ColosseumServer.h"
#include "integration/FeatureRouteComposition.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpSocket>

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace colosseum::server;
using namespace colosseum::server::integration;
namespace Media = ::ColosseumServer::Media;

namespace {

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "FAIL:%s\n", message);
    std::abort();
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

class CertificateTransport final : public app::CertificateTransport
{
public:
    QJsonObject request(const QUrl &, const QJsonObject &, QString *) override { return {}; }
};

class Interfaces final : public app::NetworkInterfaceProvider
{
public:
    QStringList ipv4Interfaces(QString *) override { return {QStringLiteral("192.0.2.10")}; }
};

class Profiler final : public app::HardwareAccelerationProfiler
{
public:
    QJsonValue profile(int) override { return QJsonArray{QStringLiteral("test-profile")}; }
};

class ProxyTransport final : public app::ProxyTransport
{
public:
    app::ProxyFetchResponse fetch(const app::ProxyFetchRequest &request,
                                  const std::atomic_bool *cancelled) override
    {
        require(request.url.toString() == QStringLiteral("http://example.test/video"),
                "proxy must translate destination and path");
        if (cancelled && cancelled->load())
            return {};
        app::ProxyFetchResponse response;
        response.status = 200;
        response.headers = {{QByteArrayLiteral("content-type"),
                             QByteArrayLiteral("text/plain")}};
        response.body = QByteArrayLiteral("proxy-body");
        return response;
    }
};

class YouTubeResolver final : public app::YouTubeResolver
{
public:
    app::YouTubeResolution resolveAudioVideo(const QString &id) override
    {
        return {QJsonObject{{QStringLiteral("url"),
                             QStringLiteral("https://cdn.example.test/%1").arg(id)},
                            {QStringLiteral("format_id"), QStringLiteral("test")}}, {}};
    }
};

QByteArray request(const QUrl &url, const QByteArray &wire)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, url.port());
    require(socket.waitForConnected(3000), "composition test client failed to connect");
    socket.write(wire);
    require(socket.waitForBytesWritten(3000), "composition test request failed to write");
    QByteArray result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (socket.waitForReadyRead(100))
            result += socket.readAll();
        QCoreApplication::processEvents();
        if (socket.state() == QAbstractSocket::UnconnectedState)
            break;
    }
    result += socket.readAll();
    return result;
}

QByteArray body(const QByteArray &wire)
{
    const qsizetype split = wire.indexOf("\r\n\r\n");
    require(split >= 0, "composition response must contain a head");
    return wire.mid(split + 4);
}

QByteArray header(const QByteArray &wire, const QByteArray &name)
{
    const qsizetype end = wire.indexOf("\r\n\r\n");
    require(end >= 0, "composition response must contain a complete head");
    for (const QByteArray &line : wire.left(end).split('\n')) {
        const qsizetype colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return {};
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    CertificateTransport certificateTransport;
    app::HttpsCertificateService certificates(certificateTransport, QString(), {});
    Interfaces interfaces;
    Profiler profiler;
    app::NetworkRouteService network(certificates, interfaces, profiler, 11470, 12470,
                                     QUrl(QStringLiteral("https://app.example.test/")));

    ProxyTransport proxyTransport;
    app::ProxyService proxy(proxyTransport);
    YouTubeResolver youtubeResolver;
    app::YouTubeService youtube(youtubeResolver);

    FeatureRouteDependencies dependencies;
    dependencies.networkApp.network = &network;
    dependencies.networkApp.proxy = &proxy;
    dependencies.networkApp.youtube = &youtube;
    dependencies.networkApp.engineUrl = QStringLiteral("http://127.0.0.1:11470");
    dependencies.media.v2Probe = [](const QString &source, Media::V2ProbeResult *result,
                                    QString *) {
        require(source == QStringLiteral("http://source.example.test/video"),
                "probe must normalize the requested media URL");
        result->format.name = QStringLiteral("matroska");
        result->format.duration = 12.5;
        result->streams = {{0, 0, QStringLiteral("video"), QStringLiteral("h264")}};
        return true;
    };
    dependencies.media.legacyProbe = [](const QString &, Media::LegacyProbeResult *result,
                                        QString *) {
        result->container = QStringLiteral("mp4");
        result->durationMs = 6000;
        result->bitrate = 1000;
        result->streams = {{QStringLiteral("video"), QStringLiteral("h264"), 1280, 720,
                            0, true, 1000, 24.0, {}}};
        return true;
    };
    dependencies.media.subtitlesTracks = [](const QUrl &, QJsonObject *result, QString *) {
        (*result)[QStringLiteral("tracks")] = QJsonArray{
            QJsonObject{{QStringLiteral("number"), 0},
                        {QStringLiteral("startTime"), 1000},
                        {QStringLiteral("endTime"), 2000},
                        {QStringLiteral("text"), QStringLiteral("A & B")}}};
        return true;
    };

    colosseum::server::ColosseumServer server;
    mountFeatureRoutes(server.router(), dependencies);
    server.router().get(QStringLiteral("/sentinel"), [](HttpRequest &, HttpResponse response) {
        response.writeHead(204);
        response.end();
        return true;
    });
    require(server.start(0), "composition server failed to start");

    const QUrl url = server.boundUrl();
    const QByteArray heartbeat = request(url,
        "GET /heartbeat HTTP/1.1\r\nHost: localhost\r\n\r\n");
    require(heartbeat.startsWith("HTTP/1.1 200 "), "heartbeat must be mounted");
    require(body(heartbeat) == "{\"success\":true}", "heartbeat body must match server.js");

    const QByteArray sample = request(url,
        "GET /samples/aac-6chan.wav HTTP/1.1\r\nHost: localhost\r\n\r\n");
    require(sample.startsWith("HTTP/1.1 200 "), "embedded sample route must be mounted");
    require(header(sample, "content-type") == "audio/x-wav",
            "embedded sample MIME type must be preserved");
    require(!body(sample).isEmpty(), "embedded sample must contain bytes");

    const QByteArray probe = request(url,
        "GET /probe?url=http%3A%2F%2Fsource.example.test%2Fvideo HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n");
    require(probe.startsWith("HTTP/1.1 200 "), "probe route must be mounted asynchronously");
    require(body(probe).contains("\"duration\":12.5"), "probe response must serialize media data");

    const QByteArray subtitles = request(url,
        "GET /subtitles.vtt?from=http%3A%2F%2Fsubs.example.test%2Fa.srt&offset=500 HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n");
    if (!subtitles.startsWith("HTTP/1.1 200 ")) {
        std::fprintf(stderr, "subtitle wire: %s\n", subtitles.constData());
        fail("subtitle route must be mounted");
    }
    require(body(subtitles).startsWith("WEBVTT\n\n0\n00:00:01.500 --> 00:00:02.500"),
            "subtitle conversion must apply format and offset");

    const QByteArray legacy = request(url,
        "GET /url/http%3A%2F%2Fsource.example.test%2Fvideo/hls.m3u8 HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n");
    require(legacy.startsWith("HTTP/1.1 200 "), "legacy HLS route must be mounted");
    require(body(legacy).startsWith("#EXTM3U\n#EXT-X-VERSION:4"),
            "legacy HLS must produce an HLS playlist");

    const QByteArray proxied = request(url,
        "GET /proxy/d=http%3A%2F%2Fexample.test/video HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n");
    require(proxied.startsWith("HTTP/1.1 200 "), "proxy route must be mounted");
    require(body(proxied) == "proxy-body", "proxy response must preserve upstream bytes");

    const QByteArray yt = request(url,
        "GET /yt/test-id.json HTTP/1.1\r\nHost: localhost\r\n\r\n");
    require(yt.startsWith("HTTP/1.1 200 "), "YouTube route must be mounted");
    require(body(yt).contains("https://cdn.example.test/test-id"),
            "YouTube JSON route must preserve resolved format");

    const QByteArray fallthrough = request(url,
        "GET /sentinel HTTP/1.1\r\nHost: localhost\r\n\r\n");
    require(fallthrough.startsWith("HTTP/1.1 204 "),
            "feature composition must fall through unknown paths");

    server.stop();
    return 0;
}
