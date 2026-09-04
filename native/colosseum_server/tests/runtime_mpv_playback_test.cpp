#include "player/mpvitem.h"
#include "player/streamserver.h"
#include "torrent/engine/TorrentEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#ifdef Q_OS_WIN
#include <tlhelp32.h>
#include <windows.h>
#endif
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr int BlockSize = 16 * 1024;

void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL:%s\n", message);
        std::fflush(stderr);
        std::exit(1);
    }
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

QByteArray makeTorrent(const QByteArray &bytes, const QString &root)
{
    QByteArray seededBytes = bytes;
    const QString seedPath = QDir(root).filePath(QStringLiteral("seed"));
    require(QDir().mkpath(seedPath), "mpv fixture seed directory creation failed");
    const QString mediaPath = QDir(seedPath).filePath(QStringLiteral("movie.mp4"));
    std::ofstream file(mediaPath.toStdString(), std::ios::binary | std::ios::trunc);
    require(file.is_open(), "mpv fixture payload open failed");
    file.write(seededBytes.constData(), static_cast<std::streamsize>(seededBytes.size()));
    require(file.good(), "mpv fixture payload write failed");
    file.close();
    require(file.good(), "mpv fixture payload close failed");
    lt::file_storage files;
    files.add_file("movie.mp4", seededBytes.size());
    lt::create_torrent creator(files, BlockSize, lt::create_torrent::v1_only);
    for (int piece = 0; piece < creator.num_pieces(); ++piece) {
        const int offset = piece * BlockSize;
        const int length = qMin(BlockSize, seededBytes.size() - offset);
        lt::hasher hasher(seededBytes.constData() + offset, length);
        creator.set_hash(lt::piece_index_t{piece}, hasher.final());
    }
    const auto encoded = creator.generate_buf();
    return QByteArray(encoded.data(), static_cast<qsizetype>(encoded.size()));
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

#ifdef Q_OS_WIN
QSet<DWORD> legacyRuntimePids()
{
    QSet<DWORD> pids;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return pids;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"stremio-runtime.exe") == 0)
                pids.insert(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pids;
}
#else
QSet<quint32> legacyRuntimePids()
{
    return {};
}
#endif

} // namespace

int main(int argc, char **argv)
{
    const auto legacyPidsBefore = legacyRuntimePids();
    QFile fixture(QStringLiteral(W40_MEDIA_FIXTURE));
    require(fixture.open(QIODevice::ReadOnly), "real W40 MP4 fixture must open");
    const QByteArray media = fixture.readAll();
    require(!media.isEmpty(), "real W40 MP4 fixture must not be empty");

    QTemporaryDir root;
    require(root.isValid(), "mpv torrent fixture directory must be valid");
    const QByteArray metainfo = makeTorrent(media, root.path());

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication application(argc, argv);

    lt::session seed(fixtureSessionParams());
    lt::error_code error;
    lt::add_torrent_params seedParams;
    seedParams.ti = std::make_shared<lt::torrent_info>(
        metainfo.constData(), metainfo.size(), error);
    require(!error && seedParams.ti, "mpv seed metadata decode failed");
    seedParams.save_path = QDir(root.path()).filePath(QStringLiteral("seed")).toStdString();
    seedParams.flags &= ~lt::torrent_flags::paused;
    const auto seedHandle = seed.add_torrent(std::move(seedParams), error);
    require(!error && seedHandle.is_valid(), "mpv seed torrent add failed");

    {
    TorrentEngine engine(QDir(root.path()).filePath(QStringLiteral("engine")));
    {
    StreamServer stream(&engine);
    stream.warmUp();
    require(stream.ready(), "native StreamServer must warm before mpv playback");

    const QString hash = engine.addTorrentBytes(
        metainfo, QDir(root.path()).filePath(QStringLiteral("client")), true);
    require(!hash.isEmpty(), "native engine must accept the real MP4 torrent");
    const auto clientHandle = engine.torrentHandle(hash);
    require(clientHandle.is_valid(), "native engine must expose the MP4 handle");
    const auto loopback = lt::make_address("127.0.0.1", error);
    require(!error, "mpv loopback address construction failed");
    clientHandle.connect_peer({loopback, seed.listen_port()});

    QSignalSpy streamReady(&stream, &StreamServer::streamReady);
    stream.play(hash, 0);
    require(waitForSignal(streamReady, 5000),
            "native stream registration must emit a playable URL");
    require(streamReady.count() == 1, "native stream registration must emit one URL");
    const QString url = streamReady.at(0).at(0).toString();
    require(!url.isEmpty(), "native stream URL must not be empty");
    auto *window = new QQuickWindow;
    auto *item = new MpvItem(window->contentItem());
    item->setWidth(320);
    item->setHeight(240);
    window->resize(320, 240);
    window->show();
    require(QTest::qWaitForWindowExposed(window), "mpv window must be exposed");

    QSignalSpy loaded(item, &MpvItem::fileLoaded);
    item->loadFile(url);
    require(loaded.wait(15000), "mpv did not load the native torrent URL");
    const auto decodeDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(15);
    while ((item->decodedWidth() <= 0 || item->decodedHeight() <= 0)
           && std::chrono::steady_clock::now() < decodeDeadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(item->decodedWidth() > 0 && item->decodedHeight() > 0,
            "mpv did not decode a frame from the native torrent URL");
    require(item->decodedWidth() == 64 && item->decodedHeight() == 64,
            "mpv decoded dimensions must match the real 64x64 fixture");
    require(legacyRuntimePids() == legacyPidsBefore,
            "native playback must not launch a stremio-runtime child process");

    stream.unwatchStats();
    std::puts("RUNTIME_MPV_PLAYBACK_OK");
    std::fflush(stdout);
    delete item;
    delete window;
    }
    engine.stop();
    }
    seed.pause();
    return 0;
}
