#include "player/streamserver.h"
#include "torrent/engine/TorrentEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpSocket>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t BlockSize = 16 * 1024;

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

std::vector<char> payload()
{
    std::vector<char> bytes(BlockSize * 3 + 5);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>((i * 29 + 7) & 0xff);
    return bytes;
}

lt::session_params fixtureSessionParams()
{
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_int(lt::settings_pack::unchoke_slots_limit, -1);
    return lt::session_params(settings);
}

QByteArray makeTorrent(const std::vector<char> &bytes, const QString &root)
{
    const QString seedPath = QDir(root).filePath(QStringLiteral("seed"));
    require(QDir().mkpath(seedPath), "torrent fixture seed directory creation failed");
    std::ofstream file(QDir(seedPath).filePath(QStringLiteral("movie.mp4")).toStdString(),
                       std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(file.good(), "torrent fixture payload write failed");
    file.close();
    require(file.good(), "torrent fixture payload close failed");

    lt::file_storage files;
    files.add_file("movie.mp4", static_cast<std::int64_t>(bytes.size()));
    lt::create_torrent creator(files, static_cast<int>(BlockSize),
                               lt::create_torrent::v1_only);
    lt::set_piece_hashes(creator, seedPath.toStdString());
    const auto encoded = creator.generate_buf();
    return QByteArray(encoded.data(), static_cast<qsizetype>(encoded.size()));
}

QByteArray readResponse(QTcpSocket &socket)
{
    QByteArray wire;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        if (socket.waitForReadyRead(100))
            wire += socket.readAll();
        QCoreApplication::processEvents();
        if (socket.state() == QAbstractSocket::UnconnectedState)
            break;
    }
    wire += socket.readAll();
    return wire;
}

QByteArray header(const QByteArray &wire, const QByteArray &name)
{
    const qsizetype end = wire.indexOf("\r\n\r\n");
    require(end >= 0, "torrent HTTP response must contain a complete head");
    for (const QByteArray &line : wire.left(end).split('\n')) {
        const qsizetype colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return {};
}

QByteArray body(const QByteArray &wire)
{
    const qsizetype end = wire.indexOf("\r\n\r\n");
    require(end >= 0, "torrent HTTP response must contain a body separator");
    return wire.mid(end + 4);
}

bool waitForSignal(QSignalSpy &spy, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);
    while (spy.isEmpty() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !spy.isEmpty();
}

bool waitForPiece(TorrentEngine &engine, const QString &hash, int piece, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (engine.torrentHandle(hash).have_piece(lt::piece_index_t{piece}))
            return true;
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return engine.torrentHandle(hash).have_piece(lt::piece_index_t{piece});
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir root;
    require(root.isValid(), "torrent HTTP fixture directory must be valid");

    const auto bytes = payload();
    const QByteArray metainfo = makeTorrent(bytes, root.path());
    lt::session seed(fixtureSessionParams());
    lt::error_code error;
    lt::add_torrent_params seedParams;
    seedParams.ti = std::make_shared<lt::torrent_info>(metainfo.constData(), metainfo.size(), error);
    require(!error && seedParams.ti, "torrent HTTP seed metadata decode failed");
    seedParams.save_path = QDir(root.path()).filePath(QStringLiteral("seed")).toStdString();
    seedParams.flags &= ~lt::torrent_flags::paused;
    const auto seedHandle = seed.add_torrent(std::move(seedParams), error);
    require(!error && seedHandle.is_valid(), "torrent HTTP seed add failed");

    TorrentEngine engine(QDir(root.path()).filePath(QStringLiteral("engine")));
    StreamServer stream(&engine);
    stream.warmUp();
    require(stream.ready(), "native StreamServer must be ready before torrent registration");

    const QString hash = engine.addTorrentBytes(
        metainfo, QDir(root.path()).filePath(QStringLiteral("client")), true);
    require(!hash.isEmpty(), "native engine must accept fixture torrent bytes");
    const auto clientHandle = engine.torrentHandle(hash);
    require(clientHandle.is_valid(), "native engine must expose the fixture torrent handle");
    const auto loopback = lt::make_address("127.0.0.1", error);
    require(!error, "torrent HTTP loopback address construction failed");
    clientHandle.connect_peer({loopback, seed.listen_port()});

    QSignalSpy streamReady(&stream, &StreamServer::streamReady);
    stream.play(hash, 0);
    require(waitForSignal(streamReady, 5000),
            "native stream registration must emit streamReady");
    require(streamReady.count() == 1, "native stream registration must emit one streamReady");

    const QUrl nativeUrl(streamReady.at(0).at(0).toString());
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(nativeUrl.port()));
    require(socket.waitForConnected(3000), "torrent HTTP client failed to connect");
    const QByteArray request = QByteArrayLiteral("GET /")
        + hash.toUtf8() + QByteArrayLiteral("/0/movie.mp4 HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-")
        + QByteArray::number(bytes.size() - 1)
        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
    require(socket.write(request) == request.size(), "torrent HTTP request write was partial");
    require(socket.waitForBytesWritten(3000), "torrent HTTP request was not written");
    const QByteArray wire = readResponse(socket);

    require(wire.startsWith("HTTP/1.1 206 "), "real torrent range response must be 206");
    require(header(wire, "content-length") == QByteArray::number(bytes.size()),
            "real torrent range length must be exact");
    require(header(wire, "content-range")
                == QByteArray("bytes 0-") + QByteArray::number(bytes.size() - 1)
                    + "/" + QByteArray::number(bytes.size()),
            "real torrent content range must be exact");
    require(header(wire, "transfer-encoding").isEmpty(),
            "real torrent fixed-length response must not be chunked");
    const QByteArray expected(bytes.data(), static_cast<qsizetype>(bytes.size()));
    require(body(wire) == expected, "real torrent HTTP body bytes must match the seeded payload");
    require(waitForPiece(engine, hash, 0, 10000),
            "real torrent HTTP request must verify the first piece through libtorrent");

    stream.unwatchStats();
    std::puts("RUNTIME_TORRENT_HTTP_E2E_OK");
    return 0;
}
