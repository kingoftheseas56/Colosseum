// Comic torrent ranking contract: canonical hash dedup, title relevance first,
// then explicit comic-archive evidence, then live seed count.
#include "torrent/ComicTorrentRanker.h"

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

TorrentResult row(const QString& title, int seeders, const QString& hash)
{
    TorrentResult result;
    result.title = title;
    result.seeders = seeders;
    result.infoHash = hash;
    return result;
}
} // namespace

int main()
{
    const QString hashA(40, QChar('a'));
    const QString hashB(40, QChar('b'));
    const QString hashC(40, QChar('c'));

    const auto deduped = ComicTorrentRanker::rank("Batman I Am Gotham", {
        row("Batman I Am Gotham CBZ", 3, hashA),
        row("Batman I Am Gotham CBZ", 19, hashA)
    });
    require(deduped.size() == 1 && deduped.first().src.seeders == 19,
            "dedup keeps the highest-seeded copy");

    const TorrentResult relevant = ComicTorrentRanker::best("Batman I Am Gotham", {
        row("Unrelated Comics Megapack CBR", 900, hashB),
        row("Batman I Am Gotham CBZ", 4, hashA)
    });
    require(relevant.infoHash == hashA, "title match beats an unrelated high-seed torrent");

    const TorrentResult archiveHint = ComicTorrentRanker::best("Batman I Am Gotham", {
        row("Batman I Am Gotham digital", 50, hashB),
        row("Batman I Am Gotham CBR", 7, hashA)
    });
    require(archiveHint.infoHash == hashA, "explicit CBR/CBZ evidence breaks a title-match tie");

    const TorrentResult seeded = ComicTorrentRanker::best("Batman I Am Gotham", {
        row("Batman I Am Gotham CBZ", 2, hashA),
        row("Batman I Am Gotham CBR", 31, hashC)
    });
    require(seeded.infoHash == hashC, "seeders decide after title and archive evidence tie");

    require(ComicTorrentRanker::best("Batman I Am Gotham", {
                row("Completely Unrelated CBR", 100, hashA)
            }).infoHash.isEmpty(),
            "an unrelated-only result set fails instead of downloading the wrong comic");

    std::cout << "comic_torrent_ranker_harness PASS\n";
    return 0;
}
