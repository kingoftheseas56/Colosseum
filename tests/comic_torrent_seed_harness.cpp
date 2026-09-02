// Deterministic loopback seeder for the comics DLTEST gate. It creates a legal
// two-page CBZ, torrents it with libtorrent, and prints a real magnet carrying
// an explicit loopback peer (`x.pe`) before seeding for five minutes.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>

#include <cstdio>
#include <memory>
#include <vector>

namespace lt = libtorrent;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) {
        std::fprintf(stderr, "usage: comic_torrent_seed_harness <workDir>\n");
        return 1;
    }
    const QString workDir = QDir::cleanPath(app.arguments().at(1));
    const QString pagesDir = workDir + QStringLiteral("/fixture-pages");
    const QString archive = workDir + QStringLiteral("/Loopback_Comic.cbz");
    QDir().mkpath(pagesDir);
    for (int i = 0; i < 2; ++i) {
        QFile page(pagesDir + QStringLiteral("/page_%1.jpg").arg(i));
        if (!page.open(QIODevice::WriteOnly)) return 1;
        page.write(QByteArray(128 * 1024, char('A' + i)));
    }
    QFile::remove(archive);
#ifdef Q_OS_WIN
    const QString archiveTool = QStringLiteral("C:/Windows/System32/tar.exe");
#else
    const QString archiveTool = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
#endif
    if (archiveTool.isEmpty()
        || QProcess::execute(archiveTool,
            {QStringLiteral("-cf"), archive, QStringLiteral("--format"), QStringLiteral("zip"),
             QStringLiteral("-C"), pagesDir, QStringLiteral(".")}) != 0)
        return 1;

    lt::file_storage storage;
    storage.add_file("Loopback_Comic.cbz", QFileInfo(archive).size());
    lt::create_torrent creator(storage, 16 * 1024);
    lt::set_piece_hashes(creator, workDir.toStdString());
    const lt::entry generated = creator.generate();
    std::vector<char> encoded;
    lt::bencode(std::back_inserter(encoded), generated);
    auto info = std::make_shared<lt::torrent_info>(encoded.data(), int(encoded.size()));

    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:49001");
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    lt::session session(settings);
    lt::error_code error;
    lt::add_torrent_params params;
    params.ti = info;
    params.save_path = workDir.toStdString();
    params.flags |= lt::torrent_flags::seed_mode;
    session.add_torrent(std::move(params), error);
    if (error) {
        std::fprintf(stderr, "add_torrent failed: %s\n", error.message().c_str());
        return 1;
    }

    const std::string base = lt::make_magnet_uri(*info);
    std::printf("READY %s&x.pe=127.0.0.1:49001\n", base.c_str());
    std::fflush(stdout);
    for (int i = 0; i < 3000; ++i) QThread::msleep(100);
    return 0;
}
