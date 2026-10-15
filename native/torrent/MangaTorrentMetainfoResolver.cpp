// native/torrent/MangaTorrentMetainfoResolver.cpp — see the seam header for the
// contract. libtorrent decode only; the same dependency TorrentEngine already
// uses, never a second engine.
#include "torrent/MangaTorrentMetainfoResolver.h"

#include <libtorrent/error_code.hpp>
#include <libtorrent/torrent_info.hpp>

namespace MangaTankoban {
namespace {

QString toHexLower(const std::string& raw)
{
    QString hex;
    hex.reserve(static_cast<int>(raw.size()) * 2);
    static const char kDigits[] = "0123456789abcdef";
    for (const unsigned char c : raw) {
        hex += QLatin1Char(kDigits[c >> 4]);
        hex += QLatin1Char(kDigits[c & 0x0f]);
    }
    return hex;
}

} // namespace

bool MangaTorrentMetainfoResolver::resolve(const QByteArray& torrentBytes, TorrentMetainfo& out)
{
    // lt::parse_error / torrent_info constructor both fail closed on malformed
    // bencode; error_code because the throwing ctor is disabled in this build.
    lt::error_code ec;
    const lt::torrent_info ti(torrentBytes.constData(), torrentBytes.size(), ec);
    if (ec || !ti.is_valid())
        return false;

    // Nyaa candidates carry the v1 40-hex hash; hybrid/v2 torrents expose both.
    // Report the v1 hash when present (it is what discovery matched), else the
    // v2 hash — the candidate bridge verifyInfoHash() still decides adoption.
    const auto& hashes = ti.info_hashes();
    if (!hashes.v1.is_all_zeros())
        out.infoHash = toHexLower(hashes.v1.to_string());
    else if (!hashes.v2.is_all_zeros())
        out.infoHash = toHexLower(hashes.v2.to_string());
    else
        return false;

    out.name = QString::fromStdString(ti.name());
    out.totalSize = ti.total_size();

    // File enumeration mirrors TorrentEngine's metadataReady JSON exactly
    // (raw libtorrent indices over ALL entries — a multi-file torrent's root
    // directory sits at index 0 with size 0). The runtime picker gates on
    // archive extensions, so the root never becomes a candidate, and any
    // persisted fileIndex expectation matches runtime metadata 1:1.
    const lt::file_storage& files = ti.files();
    out.files.clear();
    out.files.reserve(files.num_files());
    for (int i = 0; i < files.num_files(); ++i) {
        const lt::file_index_t fi{i};
        TorrentMetainfoFile f;
        f.index = i;
        f.path = QString::fromStdString(files.file_path(fi));
        f.size = static_cast<qint64>(files.file_size(fi));
        out.files.append(f);
    }
    return true;
}

} // namespace MangaTankoban
