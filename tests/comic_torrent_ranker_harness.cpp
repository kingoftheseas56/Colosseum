// Comic torrent ranking contract: canonical hash dedup, title relevance first,
// then explicit comic-archive evidence, then live seed count.
#include "torrent/ComicTorrentRanker.h"

#include <QVariant>

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

    // ── v2 manual picker: rankForEdition retains weak results and grades evidence ──
    // Identity, not seed count, decides order: ISBN > canonical title+range > unrelated.
    const QList<RankedComicTorrent> picker = ComicTorrentRanker::rankForEdition(
        "Saga", "Saga: Book One", "9781632150783", "Saga #1-18",
        {
            row("Annihilation Saga Issue 1 CBR", 900, hashA),
            row("Saga Book One 1-18 CBZ", 8, hashB),
            row("Saga 9781632150783 Digital", 2, hashC)
        });
    require(picker.size() == 3, "manual picker retains weak universal results");
    require(picker[0].src.infoHash == hashC && picker[0].confidence == QStringLiteral("strong"),
            "exact ISBN is strongest evidence");
    require(picker[1].src.infoHash == hashB && picker[1].evidence.contains(QStringLiteral("ISSUES")),
            "canonical title/range beats unrelated seed count");
    require(picker[2].src.infoHash == hashA && picker[2].confidence == QStringLiteral("weak"),
            "unrelated result remains visible but weak");

    // Duplicate canonical hash across query slices collapses to one, higher seed wins.
    const QList<RankedComicTorrent> merged = ComicTorrentRanker::rankForEdition(
        "Saga", "Saga: Book One", "9781632150783", "Saga #1-18",
        {
            row("Saga Book One 1-18 CBZ", 5, hashB),
            row("Saga Book One 1-18 CBZ", 41, hashB)
        });
    require(merged.size() == 1 && merged.first().src.seeders == 41,
            "duplicate canonical hash collapses and keeps the higher seed count");

    // Variant-row projection exposes the QML-facing contract fields.
    const QVariantList variants = ComicTorrentRanker::toVariantRows(picker);
    require(variants.size() == 3, "variant projection preserves every ranked row");
    const QVariantMap top = variants.first().toMap();
    require(top.value("infoHash").toString() == hashC
                && top.value("confidence").toString() == QStringLiteral("strong")
                && top.value("evidence").toStringList().contains(QStringLiteral("ISBN")),
            "variant row carries hash, confidence, and evidence for QML");

    std::cout << "comic_torrent_ranker_harness PASS\n";
    return 0;
}
