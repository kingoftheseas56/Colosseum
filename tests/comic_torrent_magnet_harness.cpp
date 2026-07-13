#include "torrent/ComicTorrentMagnet.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main()
{
    const QString hash(40, QChar('a'));
    const QString supplied = QStringLiteral("magnet:?xt=urn:btih:%1&x.pe=127.0.0.1:49001").arg(hash);
    require(ComicTorrentMagnet::infoHash(supplied) == hash,
            "extracts canonical hash from a full magnet");
    require(ComicTorrentMagnet::build(hash, supplied) == supplied,
            "preserves the indexer or test magnet with its discovery hints");
    require(ComicTorrentMagnet::build(hash, QString()).contains(QStringLiteral("tracker.opentrackr")),
            "falls back to the proven BookTorrentMagnet tracker set");
    require(ComicTorrentMagnet::build(hash, QStringLiteral("magnet:?xt=urn:btih:bad"))
                != QStringLiteral("magnet:?xt=urn:btih:bad"),
            "rejects a supplied magnet that does not carry the selected hash");
    std::cout << "comic_torrent_magnet_harness PASS\n";
    return 0;
}
