#pragma once

// Arc 18 M2 — production metainfo resolver: decodes `.torrent` bytes through
// Colosseum's ONE existing torrent engine dependency (libtorrent, via the
// colosseum_libtorrent INTERFACE target). No second engine, no session, no
// payload — torrent_info::files() is pure metadata enumeration.

#include "torrent/IMangaTorrentMetainfoResolver.h"

namespace MangaTankoban {

class MangaTorrentMetainfoResolver : public IMangaTorrentMetainfoResolver {
public:
    bool resolve(const QByteArray& torrentBytes, TorrentMetainfo& out) override;
};

} // namespace MangaTankoban
