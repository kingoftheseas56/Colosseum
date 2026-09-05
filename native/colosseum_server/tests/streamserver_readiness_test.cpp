#include "player/streamserver.h"
#include "torrent/engine/TorrentEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <vector>

namespace {

constexpr int BlockSize = 16 * 1024;

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "FAIL:%s\n", message);
    std::fflush(stderr);
    std::abort();
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

bool waitForSignal(QSignalSpy &spy, int timeoutMs)
{
    return waitFor([&] { return !spy.isEmpty(); }, timeoutMs);
}

lt::session_params fixtureSessionParams()
{
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_bool(lt::settings_pack::close_redundant_connections, false);
    settings.set_int(lt::settings_pack::unchoke_slots_limit, -1);
    return lt::session_params(settings);
}

QByteArray payload()
{
    QByteArray bytes(BlockSize * 2 + 31, Qt::Uninitialized);
    for (qsizetype i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>((i * 37 + 11) & 0xff);
    return bytes;
}

QByteArray makeTorrent(const QByteArray &bytes, const QString &root)
{
    const QString seedPath = QDir(root).filePath(QStringLiteral("seed"));
    require(QDir().mkpath(seedPath), "readiness fixture seed directory creation failed");
    std::ofstream file(QDir(seedPath).filePath(QStringLiteral("movie.mp4")).toStdString(),
                       std::ios::binary | std::ios::trunc);
    require(file.is_open(), "readiness fixture payload open failed");
    file.write(bytes.constData(), static_cast<std::streamsize>(bytes.size()));
    require(file.good(), "readiness fixture payload write failed");
    file.close();
    require(file.good(), "readiness fixture payload close failed");

    lt::file_storage files;
    files.add_file("movie.mp4", bytes.size());
    lt::create_torrent creator(files, BlockSize, lt::create_torrent::v1_only);
    for (int piece = 0; piece < creator.num_pieces(); ++piece) {
        const int offset = piece * BlockSize;
        const int length = std::min(BlockSize, static_cast<int>(bytes.size()) - offset);
        lt::hasher hasher(bytes.constData() + offset, length);
        creator.set_hash(lt::piece_index_t{piece}, hasher.final());
    }
    const auto encoded = creator.generate_buf();
    return QByteArray(encoded.data(), static_cast<qsizetype>(encoded.size()));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    qputenv("COLOSSEUM_TORRENT_LISTEN_INTERFACES", QByteArrayLiteral("127.0.0.1:0"));
    qputenv("COLOSSEUM_TORRENT_DISABLE_DISCOVERY", QByteArrayLiteral("1"));

    QTemporaryDir root;
    require(root.isValid(), "readiness fixture directory must be valid");
    const QByteArray bytes = payload();
    const QByteArray metainfo = makeTorrent(bytes, root.path());

    lt::session seed(fixtureSessionParams());
    lt::error_code error;
    lt::add_torrent_params seedParams;
    seedParams.ti = std::make_shared<lt::torrent_info>(
        metainfo.constData(), metainfo.size(), error);
    require(!error && seedParams.ti, "readiness seed metadata decode failed");
    seedParams.save_path = QDir(root.path()).filePath(QStringLiteral("seed")).toStdString();
    seedParams.flags &= ~lt::torrent_flags::paused;
    const auto seedHandle = seed.add_torrent(std::move(seedParams), error);
    require(!error && seedHandle.is_valid(), "readiness seed add failed");
    require(waitFor([&] { return seedHandle.status().is_seeding; }, 5000),
            "readiness seed must finish checking before the test starts");

    TorrentEngine engine(QDir(root.path()).filePath(QStringLiteral("engine")));
    StreamServer stream(&engine);
    stream.warmUp();
    require(stream.ready(), "native StreamServer must warm before readiness qualification");

    const QString hash = engine.addTorrentBytes(
        metainfo, QDir(root.path()).filePath(QStringLiteral("client")), true);
    require(!hash.isEmpty(), "readiness client must accept fixture torrent bytes");
    const auto clientHandle = engine.torrentHandle(hash);
    require(clientHandle.is_valid(), "readiness client handle must be valid");
    require(!clientHandle.have_piece(lt::piece_index_t{0}),
            "readiness client must begin without the first media piece");
    clientHandle.apply_ip_filter(false);
    clientHandle.set_flags(lt::torrent_flags::disable_dht
                           | lt::torrent_flags::disable_lsd
                           | lt::torrent_flags::disable_pex);

    QSignalSpy ready(&stream, &StreamServer::streamReady);
    QSignalSpy errors(&stream, &StreamServer::streamError);
    stream.play(hash, 0);

    require(!waitForSignal(ready, 1000),
            "streamReady must not fire while the media path cannot deliver a first byte");
    require(errors.isEmpty(),
            "waiting for the first readable byte must not be reported as a playback error");

    const auto loopback = lt::make_address("127.0.0.1", error);
    require(!error, "readiness loopback address construction failed");
    clientHandle.resume();
    clientHandle.connect_peer({loopback, seed.listen_port()});
    seedHandle.connect_peer({loopback, engine.listenPort()});

    QTimer reconnect;
    QObject::connect(&reconnect, &QTimer::timeout, [&] {
        clientHandle.connect_peer({loopback, seed.listen_port()});
        seedHandle.connect_peer({loopback, engine.listenPort()});
    });
    reconnect.start(100);

    require(waitForSignal(ready, 15000),
            "streamReady must fire after the native media path delivers a readable byte");
    reconnect.stop();
    require(ready.count() == 1, "readiness transition must emit exactly one streamReady");
    require(clientHandle.have_piece(lt::piece_index_t{0}),
            "streamReady must imply the first requested media piece is verified");
    require(errors.isEmpty(), "successful readiness must not emit streamError");

    stream.unwatchStats();
    engine.stop();
    seed.pause();
    std::puts("STREAMSERVER_READINESS_OK");
    return 0;
}
