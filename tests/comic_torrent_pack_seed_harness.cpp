// Deterministic loopback seeder for the Tankorent Comic pack-selection DLTEST
// gate (Task 11). Builds ONE torrent covering every fixture the real
// select->download->assemble->publish->restart chain is proven against:
//   Compendiums/Invincible Compendium v01.cbz   (the "single" gate's exact
//   Compendiums/Invincible Compendium v02.cbz    -title target + its sibling
//                                                 volume, shared for "shared")
//   Issues/Guardians #1.cbz                      (the "issues" gate's
//   Issues/Guardians #2.cbz                       collected-issue-set —
//   Issues/Guardians #3.cbz                       DELIBERATELY a different
//                                                  series name than the
//                                                  Compendiums so the
//                                                  issue-set tier's series-
//                                                  agreement check can never
//                                                  accidentally match one)
//   TPBs/Invincible v01.cbz                      (decoy — a real edition-
//                                                  shaped archive no scenario
//                                                  ever selects)
//   Extras/notes.txt                             (decoy — wrong extension,
//                                                  every tier ignores it)
// Every CBZ is a REAL zip (bsdtar --format zip, not tar's ".cbz"-blind -a
// extension guess) of real, magic-byte-valid tiny PNGs (real signature +
// random filler, so on-disk/wire size is not zip-compressed away). No
// external network, no piracy — mirrors comic_torrent_seed_harness.cpp's
// technique (libtorrent create_torrent + seed_mode + an explicit loopback
// peer baked into the printed magnet) extended to a multi-file payload.
//
// The seeder's own upload is deliberately rate-limited: the restart gate
// needs a real, observable window where a real transfer sits in a real
// "downloading" ledger state before it completes, so the runner can kill the
// downloading process by PID and prove the ledger replay on relaunch.
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QThread>
#include <QVector>

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

namespace {

// A real, magic-byte-valid PNG: the 8-byte signature + random filler bytes
// so the archive's on-disk (and wire) size stays close to `size` regardless
// of the zip writer's own compression — real entropy, not a repeated pattern
// zip could shrink to nothing.
QByteArray fixturePngBytes(int size)
{
    static const unsigned char kPngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    QByteArray out(reinterpret_cast<const char*>(kPngSig), 8);
    const int fillerLen = qMax(0, size - out.size());
    QByteArray filler(fillerLen, Qt::Uninitialized);
    if (fillerLen >= int(sizeof(quint32))) {
        QRandomGenerator::global()->fillRange(
            reinterpret_cast<quint32*>(filler.data()), filler.size() / int(sizeof(quint32)));
    }
    out += filler;
    return out;
}

bool writePage(const QString& dir, const QString& name, int size)
{
    QDir().mkpath(dir);
    QFile f(dir + QChar('/') + name);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(fixturePngBytes(size)) >= 0;
}

// Zips `pagesDir`'s contents into `archivePath` as a REAL zip archive
// (--format zip; tar's -a extension-sniffing has no ".cbz" entry so it would
// silently fall back to plain tar) using the OS's bundled bsdtar — the SAME
// extractor ComicDownloader/ComicEditionAssembler read comic archives with,
// so this fixture round-trips through the exact production extraction path.
bool zipDir(const QString& pagesDir, const QString& archivePath)
{
    QFile::remove(archivePath);
    return QProcess::execute(QStringLiteral("C:/Windows/System32/tar.exe"),
        {QStringLiteral("-cf"), archivePath, QStringLiteral("--format"), QStringLiteral("zip"),
         QStringLiteral("-C"), pagesDir, QStringLiteral(".")}) == 0;
}

// Builds one CBZ at `archivePath` from `pageCount` fixture pages of
// `pageSize` bytes each, staged under a throwaway sibling directory removed
// once zipped.
bool buildCbz(const QString& workDir, const QString& archivePath, int pageCount, int pageSize)
{
    const QString stage = workDir + QStringLiteral("/.stage-")
        + QFileInfo(archivePath).completeBaseName().replace(QChar(' '), QChar('_'));
    QDir(stage).removeRecursively();
    for (int i = 0; i < pageCount; ++i) {
        if (!writePage(stage, QStringLiteral("page_%1.png").arg(i, 3, 10, QChar('0')), pageSize)) {
            QDir(stage).removeRecursively();
            return false;
        }
    }
    QDir().mkpath(QFileInfo(archivePath).absolutePath());
    const bool ok = zipDir(stage, archivePath);
    QDir(stage).removeRecursively();
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) {
        std::fprintf(stderr, "usage: comic_torrent_pack_seed_harness <workDir>\n");
        return 1;
    }
    const QString workDir = QDir::cleanPath(app.arguments().at(1));
    QDir().mkpath(workDir);

    // Every fixture file lives under ONE shared root inside the torrent
    // ("PackFixture/...") — standard single-root BitTorrent multi-file
    // layout (BEP0003). A payload split across SEVERAL unrelated top-level
    // directories (no shared root) drives libtorrent's file_storage into its
    // no_root_dir path, which round-tripped unreliably through generate() ->
    // bencode() -> torrent_info() in this environment (torrent_info()
    // silently hung/crashed with zero diagnostics). ComicEditionFileSelector
    // only ever looks at a file's basename stem and its DEEPEST directory
    // segment (native/torrent/ComicEditionFileSelector.cpp: stemOf/
    // lastSegment), so this extra shared prefix is invisible to every
    // selection tier — the fixture's Compendiums/Issues/TPBs/Extras
    // sub-layout is unchanged, just nested one level deeper.
    const QString torrentRoot = QStringLiteral("PackFixture");
    const QString payloadDir = workDir + QChar('/') + torrentRoot;

    // ── Fixture payload ──────────────────────────────────────────────────
    struct Entry { QString rel; int pages; int pageSize; };
    const QVector<Entry> entries = {
        { QStringLiteral("Compendiums/Invincible Compendium v01.cbz"), 2, 350 * 1024 },
        { QStringLiteral("Compendiums/Invincible Compendium v02.cbz"), 2, 350 * 1024 },
        { QStringLiteral("Issues/Guardians #1.cbz"), 2, 40 * 1024 },
        { QStringLiteral("Issues/Guardians #2.cbz"), 2, 40 * 1024 },
        { QStringLiteral("Issues/Guardians #3.cbz"), 2, 40 * 1024 },
        { QStringLiteral("TPBs/Invincible v01.cbz"), 2, 350 * 1024 },
    };
    for (const Entry& e : entries) {
        const QString abs = payloadDir + QChar('/') + e.rel;
        if (!buildCbz(payloadDir, abs, e.pages, e.pageSize)) {
            std::fprintf(stderr, "failed building fixture archive: %s\n", qUtf8Printable(e.rel));
            return 1;
        }
    }
    const QString notesPath = payloadDir + QStringLiteral("/Extras/notes.txt");
    QDir().mkpath(QFileInfo(notesPath).absolutePath());
    {
        QFile notes(notesPath);
        if (!notes.open(QIODevice::WriteOnly)) return 1;
        notes.write("not a comic archive - decoy fixture file for the pack-selection DLTEST gate\n");
    }

    // ── Torrent creation (mirrors comic_torrent_seed_harness.cpp) ──────────
    QVector<QString> allRel;
    for (const Entry& e : entries) allRel.append(torrentRoot + QChar('/') + e.rel);
    allRel.append(torrentRoot + QStringLiteral("/Extras/notes.txt"));

    lt::file_storage storage;
    for (const QString& rel : allRel) {
        const QString abs = workDir + QChar('/') + rel;
        storage.add_file(rel.toStdString(), QFileInfo(abs).size());
    }
    lt::create_torrent creator(storage, 16 * 1024);
    lt::set_piece_hashes(creator, workDir.toStdString());
    const lt::entry generated = creator.generate();
    std::vector<char> encoded;
    lt::bencode(std::back_inserter(encoded), generated);
    auto info = std::make_shared<lt::torrent_info>(encoded.data(), int(encoded.size()));

    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:49011");
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    // Throttle the seeder so a real (small, loopback) transfer still takes
    // several real seconds — the restart gate needs a window to observe a
    // real "downloading" ledger row and kill the process before it completes.
    settings.set_int(lt::settings_pack::upload_rate_limit, 48 * 1024);
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
    std::printf("READY %s&x.pe=127.0.0.1:49011\n", base.c_str());
    std::fflush(stdout);
    // Seed for ~15 minutes: the full runner (single + issues + shared +
    // restart's two launches, the second of which alone allows a 240 s
    // internal backstop) comfortably needs more than the ~5 minutes a single
    // scenario would — measured empirically running the whole suite back to
    // back against a single seeder process.
    for (int i = 0; i < 9000; ++i) QThread::msleep(100);
    return 0;
}
