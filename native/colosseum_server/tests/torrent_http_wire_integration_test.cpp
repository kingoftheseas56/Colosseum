#include "core/ColosseumServer.h"
#include "integration/TorrentHttpRouteAdapter.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpSocket>
#include <QUrl>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>

using namespace colosseum::server;
using namespace colosseum::server::integration;
using namespace colosseum::server::torrent_http;

namespace {

constexpr auto Hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

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

class Backend final : public TorrentHttpBackend
{
public:
    void ensureEngine(const QString &, const QJsonObject &, ReadyCallback ready) override
    {
        if (ready)
            ready({});
    }

    void createFromTorrent(const QByteArray &, TorrentReadyCallback ready) override
    {
        if (ready)
            ready(QString::fromLatin1(Hash), {});
    }

    QJsonObject defaultEngineOptions(const QString &) const override { return {}; }
    QJsonValue globalStats() const override { return QJsonObject{}; }
    QJsonObject systemStats() const override { return {}; }
    QJsonValue stats(const QString &, std::optional<int>) const override
    {
        return QJsonObject{{QStringLiteral("infoHash"), QString::fromLatin1(Hash)}};
    }
    QVector<TorrentFileView> files(const QString &) const override
    {
        return {{0, QStringLiteral("movie.mp4"), QStringLiteral("movie.mp4"), 10, 0}};
    }
    std::optional<int> guessFileIndex(const QString &, const QJsonObject &) const override
    {
        return 0;
    }
    void remove(const QString &, std::function<void()> complete) override
    {
        if (complete)
            complete();
    }
    void removeAll() override {}
    void prewarm(const QString &, int) override { ++prewarmCount; }
    void streamOpened(const QString &, int) override { ++openCount; }
    void streamClosed(const QString &, int) override { ++closeCount; }

    int prewarmCount = 0;
    int openCount = 0;
    int closeCount = 0;
};

class Session final : public TorrentStreamSession
{
public:
    Session(TorrentReadPlan plan, TorrentStreamCallbacks callbacks, bool hold)
        : plan_(std::move(plan)), callbacks_(std::move(callbacks)), hold_(hold)
    {
    }

    void start() override
    {
        if (hold_ || destroyed_)
            return;
        const QByteArray data = QByteArrayLiteral("0123456789")
            .mid(static_cast<int>(plan_.start),
                 static_cast<int>(plan_.end - plan_.start + 1));
        const int first = data.size() / 2;
        callbacks_.onChunk(data.left(first));
        callbacks_.onChunk(data.mid(first));
        callbacks_.onEnd();
    }

    void destroy() override
    {
        destroyed_ = true;
        ++destroyCount;
    }

    bool destroyed_ = false;
    int destroyCount = 0;

private:
    TorrentReadPlan plan_;
    TorrentStreamCallbacks callbacks_;
    bool hold_ = false;
};

class Factory final : public TorrentStreamFactory
{
public:
    std::shared_ptr<TorrentStreamSession> open(
        const TorrentReadPlan &plan,
        const std::shared_ptr<CancellationToken> &,
        TorrentStreamCallbacks callbacks) override
    {
        ++openCount;
        lastPlan = plan;
        auto session = std::make_shared<Session>(plan, std::move(callbacks), holdNext);
        holdNext = false;
        lastSession = session;
        return session;
    }

    int openCount = 0;
    bool holdNext = false;
    std::optional<TorrentReadPlan> lastPlan;
    std::shared_ptr<Session> lastSession;
};

QByteArray request(QTcpSocket &socket, const QByteArray &value)
{
    socket.write(value);
    require(socket.waitForBytesWritten(3000), "request write failed");
    QByteArray result;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (socket.waitForReadyRead(100))
            result += socket.readAll();
        if (socket.state() == QAbstractSocket::UnconnectedState)
            break;
        QCoreApplication::processEvents();
    }
    result += socket.readAll();
    return result;
}

QByteArray header(const QByteArray &wire, const QByteArray &name)
{
    const auto headEnd = wire.indexOf("\r\n\r\n");
    require(headEnd >= 0, "HTTP response must have a complete head");
    for (const QByteArray &line : wire.left(headEnd).split('\n')) {
        const auto colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return {};
}

QByteArray body(const QByteArray &wire)
{
    const auto split = wire.indexOf("\r\n\r\n");
    require(split >= 0, "HTTP response must have a head/body separator");
    return wire.mid(split + 4);
}

void connectSocket(QTcpSocket &socket, const QUrl &url)
{
    socket.connectToHost(QHostAddress::LocalHost, url.port());
    require(socket.waitForConnected(3000), "client failed to connect to server");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Backend backend;
    class EmptyCreateSource final : public TorrentCreateSource {
    public:
        void load(const QString &, std::function<void(QByteArray, QString)> complete) override
        {
            if (complete)
                complete({}, QStringLiteral("not used"));
        }
    } createSource;
    TorrentHttpSurface surface(backend, createSource);
    Factory factory;
    ColosseumServer server;
    mountTorrentRoutes(server.router(), surface, factory);
    require(server.start(0), "server failed to start");

    QTcpSocket socket;
    connectSocket(socket, server.boundUrl());
    const QByteArray bounded = request(socket,
        "GET /aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/0/movie.mp4 HTTP/1.1\r\n"
        "Host: localhost\r\nRange: bytes=2-7\r\n\r\n");
    require(bounded.startsWith("HTTP/1.1 206 "), "bounded media request must be 206");
    require(header(bounded, "content-length") == "6", "bounded length must be exact");
    require(header(bounded, "content-range") == "bytes 2-7/10",
            "bounded content range must be exact");
    require(header(bounded, "transfer-encoding").isEmpty(),
            "fixed-length media must not be chunked");
    require(body(bounded) == "234567", "progressive fixed-length body must be raw bytes");
    require(factory.lastPlan && factory.lastPlan->start == 2
                && factory.lastPlan->end == 7,
            "stream factory must receive the W07 range plan");
    require(backend.openCount == 1 && backend.closeCount == 1,
            "stream lease must open and close exactly once");

    QTcpSocket headSocket;
    connectSocket(headSocket, server.boundUrl());
    const QByteArray head = request(headSocket,
        "HEAD /aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/0/movie.mp4 HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n");
    require(head.startsWith("HTTP/1.1 200 "), "HEAD must return 200");
    require(header(head, "content-length") == "10", "HEAD must report full length");
    require(body(head).isEmpty(), "HEAD must not deliver a body");
    require(factory.openCount == 1, "HEAD must not open a FileStream session");

    QTcpSocket invalidSocket;
    connectSocket(invalidSocket, server.boundUrl());
    const QByteArray invalid = request(invalidSocket,
        "GET /aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/0/movie.mp4 HTTP/1.1\r\n"
        "Host: localhost\r\nRange: bytes=99-100\r\n\r\n");
    require(invalid.startsWith("HTTP/1.1 200 "),
            "invalid range must preserve module 172 full-file fallback");
    require(header(invalid, "content-length") == "10",
            "invalid range fallback length must be full file");
    require(body(invalid) == "0123456789", "invalid range fallback body must be full file");

    const int prewarmBefore = backend.prewarmCount;
    QTcpSocket openSocket;
    connectSocket(openSocket, server.boundUrl());
    const QByteArray open = request(openSocket,
        "GET /aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/0/movie.mp4 HTTP/1.1\r\n"
        "Host: localhost\r\nRange: bytes=4-\r\n\r\n");
    require(body(open) == "456789", "open-ended range must reach EOF");
    require(backend.prewarmCount == prewarmBefore + 1,
            "open-ended range must prewarm the selected file");

    factory.holdNext = true;
    QTcpSocket cancelledSocket;
    connectSocket(cancelledSocket, server.boundUrl());
    cancelledSocket.write(
        "GET /aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/0/movie.mp4 HTTP/1.1\r\n"
        "Host: localhost\r\nRange: bytes=0-9\r\n\r\n");
    require(cancelledSocket.waitForReadyRead(3000), "held stream headers were not sent");
    const QByteArray partial = cancelledSocket.readAll();
    require(partial.contains("content-length: 10"),
            "held stream must publish fixed-length headers before data");
    cancelledSocket.abort();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!factory.lastSession->destroyed_
           && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(factory.lastSession->destroyed_,
            "client disconnect must destroy the stream session");

    server.stop();
    std::puts("TORRENT_HTTP_WIRE_OK");
    return 0;
}
