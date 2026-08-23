#pragma once

// Arc 18 M2 — the metainfo inspection seam (TARGET-ARCHITECTURE "Metainfo
// acquisition"). Torrent identity must be solved from REAL torrent metadata —
// file index/path/size — without downloading a byte of payload and without
// waiting on DHT. Nyaa's RSS <link> is a .torrent metainfo URL, so the
// preferred indexing path fetches those bytes and decodes them; the paused-
// magnet engine-metadata path stays as the fallback architecture. This seam
// keeps that capability behind one tiny interface so the indexer and its
// harnesses never need a live torrent session (or a libtorrent link).
//
// LAW (IMPLEMENTATION-PLAN M2 adoption rule): the decoded infoHash must match
// the discovered candidate's infoHash before anything may be indexed. A bad or
// mismatched metainfo response fails closed — verifyInfoHash() is the only
// approved bridge between a candidate row and a resolved metainfo.

#include <QByteArray>
#include <QString>
#include <QVector>

namespace MangaTankoban {

struct TorrentMetainfoFile {
    int index = -1;
    QString path;     // torrent-relative path, separators as the metainfo carries
    qint64 size = 0;
};

struct TorrentMetainfo {
    QString infoHash; // lowercase hex, computed from the decoded info dictionary
    QString name;     // root name from the info dictionary
    qint64 totalSize = 0;
    QVector<TorrentMetainfoFile> files;
};

class IMangaTorrentMetainfoResolver {
public:
    virtual ~IMangaTorrentMetainfoResolver() = default;

    // Decode `.torrent` metainfo bytes into the file list. Returns false on any
    // decode failure (out stays untouched). Implementations MUST NOT download
    // payload bytes — metainfo only.
    virtual bool resolve(const QByteArray& torrentBytes, TorrentMetainfo& out) = 0;
};

// The only approved candidate↔metainfo bridge: true when the expected candidate
// infoHash (lowercase hex, as discovery produced it) matches the decoded
// metainfo hash. Mismatch or malformed input → false, fail closed. Header-inline
// on purpose: indexer harnesses prove this rule without a libtorrent link.
inline bool verifyInfoHash(const QString& candidateInfoHash, const TorrentMetainfo& metainfo)
{
    const QString expected = candidateInfoHash.trimmed().toLower();
    if (expected.isEmpty() || metainfo.infoHash.isEmpty())
        return false;
    return expected == metainfo.infoHash;
}

} // namespace MangaTankoban
