// Arc 18 M2 — metainfo resolver harness (TEST-MATRIX M2). Proves OFFLINE, with
// real libtorrent-built .torrent bytes: (1) file index/path/size enumeration
// happens from metainfo alone — zero payload transfer; (2) the decoded
// infoHash is reported and verifyInfoHash() is the only candidate bridge, true
// on match, false on mismatch; (3) malformed bytes fail closed. The torrent is
// multi-file so the raw-index convention (root dir at index 0, mirroring
// TorrentEngine's metadataReady JSON) is pinned too.
#include "torrent/MangaTorrentMetainfoResolver.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace MangaTankoban;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void writeFile(const QString& dir, const QString& relPath, int size)
{
    const QString abs = dir + QLatin1Char('/') + relPath;
    QFileInfo fi(abs);
    QDir().mkpath(fi.absolutePath());
    QFile f(abs);
    require(f.open(QIODevice::WriteOnly), "fixture file can be written");
    QByteArray bytes(size, '\x5a');
    f.write(bytes);
    f.close();
}
} // namespace

int main()
{
    // ── Build a real multi-file .torrent offline ─────────────────────────────
    QTemporaryDir tmp;
    require(tmp.isValid(), "temp dir for fixture torrent");
    const QString work = tmp.path();
    writeFile(work, QStringLiteral("SeriesPack/Series Vol 01/Series v01.cbz"), 48 * 1024);
    writeFile(work, QStringLiteral("SeriesPack/Series v02.cbz"), 48 * 1024);
    writeFile(work, QStringLiteral("SeriesPack/Series Vol 10.5.cbz"), 32 * 1024);
    writeFile(work, QStringLiteral("SeriesPack/notes.txt"), 64);

    lt::file_storage storage;
    storage.add_file("SeriesPack/Series Vol 01/Series v01.cbz", 48 * 1024);
    storage.add_file("SeriesPack/Series v02.cbz", 48 * 1024);
    storage.add_file("SeriesPack/Series Vol 10.5.cbz", 32 * 1024);
    storage.add_file("SeriesPack/notes.txt", 64);
    lt::create_torrent creator(storage, 16 * 1024);
    lt::set_piece_hashes(creator, work.toStdString());
    const lt::entry generated = creator.generate();
    std::vector<char> encoded;
    lt::bencode(std::back_inserter(encoded), generated);
    const QByteArray torrentBytes(encoded.data(), static_cast<int>(encoded.size()));

    // Reference hash from a direct torrent_info decode of the same bytes — the
    // resolver must agree with the engine's own metadata source of truth.
    QString expectedHash;
    {
        lt::error_code ec;
        const lt::torrent_info ref(encoded.data(), static_cast<int>(encoded.size()), ec);
        require(!ec, "reference torrent_info decodes");
        const std::string raw = ref.info_hashes().v1.to_string();
        static const char kDigits[] = "0123456789abcdef";
        for (const unsigned char c : raw) {
            expectedHash += QLatin1Char(kDigits[c >> 4]);
            expectedHash += QLatin1Char(kDigits[c & 0x0f]);
        }
    }

    // ── Enumeration without payload ──────────────────────────────────────────
    MangaTorrentMetainfoResolver resolver;
    TorrentMetainfo meta;
    require(resolver.resolve(torrentBytes, meta), "valid .torrent bytes decode");
    require(meta.name == QStringLiteral("SeriesPack"), "root name from info dict");
    require(meta.totalSize > 0, "total size carried");
    // Raw-index contract: enumerate EVERY metainfo entry in engine order with
    // no filtering or re-numbering (libtorrent sorts entries and may add .pad
    // filler for v2 alignment — both are real metadata the runtime engine also
    // shows; the picker's archive-extension gate keeps them out of candidacy).
    require(meta.files.size() == storage.num_files(),
            "every metainfo file entry enumerated, none dropped");
    for (int i = 0; i < meta.files.size(); ++i)
        require(meta.files.at(i).index == i, "raw engine file order preserved");

    const auto findBySuffix = [&meta](const QString& suffix) -> const TorrentMetainfoFile* {
        for (const TorrentMetainfoFile& f : meta.files) {
            QString p = f.path;
            if (p.replace(QLatin1Char('\\'), QLatin1Char('/')).endsWith(suffix))
                return &f;
        }
        return nullptr;
    };
    const TorrentMetainfoFile* v01 = findBySuffix(QStringLiteral("Series Vol 01/Series v01.cbz"));
    require(v01 != nullptr, "subdirectory archive enumerated");
    require(v01->size == 48 * 1024, "file size enumerated");
    const TorrentMetainfoFile* v02 = findBySuffix(QStringLiteral("Series v02.cbz"));
    require(v02 != nullptr && v02->size == 48 * 1024,
            "file path/size truth enumerated for flat archive");
    const TorrentMetainfoFile* frac = findBySuffix(QStringLiteral("Series Vol 10.5.cbz"));
    require(frac != nullptr && frac->size == 32 * 1024,
            "fractional-named archive enumerated");

    // ── The candidate bridge: verifyInfoHash ─────────────────────────────────
    require(meta.infoHash == expectedHash, "decoded infoHash equals libtorrent reference");
    require(meta.infoHash.length() == 40 && meta.infoHash == meta.infoHash.toLower(),
            "infoHash reported as 40-char lowercase hex");
    require(verifyInfoHash(expectedHash, meta), "matching candidate infoHash verifies");
    QString tampered = expectedHash;
    tampered[0] = tampered[0] == QLatin1Char('0') ? QLatin1Char('1') : QLatin1Char('0');
    require(!verifyInfoHash(tampered, meta), "mismatched candidate infoHash fails closed");
    require(!verifyInfoHash(QString(), meta), "empty candidate infoHash fails closed");
    require(verifyInfoHash(expectedHash.toUpper(), meta),
            "uppercase candidate tolerated through normalization");

    // ── Malformed input fails closed ─────────────────────────────────────────
    TorrentMetainfo garbage;
    require(!resolver.resolve(QByteArray("this is not bencode"), garbage),
            "garbage bytes fail closed");
    require(!resolver.resolve(QByteArray(), garbage), "empty bytes fail closed");

    std::cout << "MANGA_TORRENT_METAINFO_RESOLVER_OK\n";
    return 0;
}
