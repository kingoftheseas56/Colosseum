// Comic torrent ranking contract: canonical hash dedup, title relevance first,
// then explicit comic-archive evidence, then live seed count. The v2 manual
// picker additionally grades format-scoped coverage and uploader trust, and
// unions evidence across duplicate infohashes before confidence is assigned
// (design: docs/superpowers/specs/2026-07-15-colosseum-tankorent-comic-
// volume-mode-design.md, "Torrent-level ranking").
#include "torrent/ComicEditionIdentity.h"
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

// Builds a canonical match target the same way ComicTorrents does at its
// facade boundary — tests exercise the shipped ComicEditionIdentity module,
// not a hand-rolled struct literal.
ComicEditionIdentity::ComicEditionTarget makeTarget(const QString& seriesTitle,
                                                     const QString& editionTitle,
                                                     const QString& catalogFormat,
                                                     const QString& isbn,
                                                     const QString& collects)
{
    return ComicEditionIdentity::buildTarget(QString(), QString(), seriesTitle, editionTitle,
                                              catalogFormat, isbn, collects);
}
} // namespace

int main()
{
    const QString hashA(40, QChar('a'));
    const QString hashB(40, QChar('b'));
    const QString hashC(40, QChar('c'));
    const QString hashD(40, QChar('d'));
    const QString hashE(40, QChar('e'));

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
    const ComicEditionIdentity::ComicEditionTarget sagaTarget =
        makeTarget(QStringLiteral("Saga"), QStringLiteral("Saga: Book One"), QString(),
                  QStringLiteral("9781632150783"), QStringLiteral("Saga #1-18"));
    require(sagaTarget.format == ComicEditionIdentity::ComicCollectionFormat::Book
                && sagaTarget.ordinal == 1,
            "sanity: worded ordinal + title-derived Book format parse off the shipped identity module");

    const QList<RankedComicTorrent> picker = ComicTorrentRanker::rankForEdition(
        sagaTarget,
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

    // ── Non-comic media (games / ebooks / video) is dropped before it can rank ──
    // The eyes-on false positives for "Saga: Book One": a Switch game and a prose
    // novel both matched on the bare word "Saga" + a "1-18" range and showed as
    // POSSIBLE MATCH. A comic is never an NSZ game or an EPUB/MOBI novel.
    const QList<RankedComicTorrent> filtered = ComicTorrentRanker::rankForEdition(
        sagaTarget,
        {
            row("[Nintendo Switch] Hiveswap Friendsim Vol. 1-18 Complete - MS Paint Reader Saga [NSZ][ENG]", 4, hashA),
            row("Vorkosigan Saga (1-18) - Lois McMaster Bujold [EPUB + MOBI + PDF]", 4, hashB),
            row("Saga Book One 1-18 CBZ", 8, hashC)
        });
    require(filtered.size() == 1 && filtered.first().src.infoHash == hashC,
            "the Switch game and the prose ebook are dropped; only the real comic survives");

    // ── Series gate: a "Book One" coverage match for the WRONG series is never
    //    our edition. Eyes-on false positives for "Saga: Book One": a video game
    //    and a LitRPG novel, both "Book One" + a bare "Saga" (as a suffix / genre
    //    tag), were ranking STRONG. They must demote to weak; the real Saga
    //    edition (its release LEADS with the series) stays strong. ──
    {
        const QList<RankedComicTorrent> gated = ComicTorrentRanker::rankForEdition(
            sagaTarget,
            {
                row("The Fernweh Saga: Book One", 36, hashA),
                row("C.M. Carney - Awakened The Quintessence Crucible, Book One (An Epic Cultivation LitRPG Saga)", 20, hashB),
                row("Saga Book One (2014) (Digital) (Empire) CBZ", 8, hashC),
                row("[DCP] Saga Book One CBZ", 5, hashD)
            });
        const auto conf = [&](const QString& h) -> QString {
            for (const auto& r : gated) if (r.src.infoHash == h) return r.confidence;
            return QStringLiteral("<absent>");
        };
        require(conf(hashA) == QStringLiteral("weak"),
                "'The Fernweh Saga: Book One' (wrong series) is weak, not a strong coverage match");
        require(conf(hashB) == QStringLiteral("weak"),
                "the LitRPG 'Saga' novel (Saga only as a genre tag) is weak");
        require(conf(hashC) == QStringLiteral("strong"),
                "the real Saga edition (release leads with the series) is a strong coverage match");
        require(conf(hashD) == QStringLiteral("strong"),
                "a bracket-tagged real Saga release still leads with the series after tag-strip");
    }

    // ── Serious media filter drops video by container/codec, incl. Knaben's
    //    anime-capture firehose (.ts / .mkv / MPEG2). ──
    {
        const QList<RankedComicTorrent> filteredVideo = ComicTorrentRanker::rankForEdition(
            sagaTarget,
            {
                row("[shincaps] One Piece - 978 (BS-FUJI 1440x1080 MPEG2 AACx2).ts", 340, hashA),
                row("[SubsPlease] Kill Ao - 11 (720p) [2D978D5F].mkv", 50, hashB),
                row("Saga Book One (2014) CBZ", 8, hashC)
            });
        require(filteredVideo.size() == 1 && filteredVideo.first().src.infoHash == hashC,
                "anime .ts/.mkv/MPEG2 video captures are dropped; only the comic survives");
    }

    // Duplicate canonical hash across query slices collapses to one, higher seed wins.
    const QList<RankedComicTorrent> merged = ComicTorrentRanker::rankForEdition(
        sagaTarget,
        {
            row("Saga Book One 1-18 CBZ", 5, hashB),
            row("Saga Book One 1-18 CBZ", 41, hashB)
        });
    require(merged.size() == 1 && merged.first().src.seeders == 41,
            "duplicate canonical hash collapses and keeps the higher seed count");

    // Same infohash listed two ways: a generic high-seed title and an exact-title
    // low-seed one. Dedup must UNION evidence, not bury the exact one under seeds.
    const QList<RankedComicTorrent> dupEvidence = ComicTorrentRanker::rankForEdition(
        sagaTarget,
        {
            row("Comics Weekly Pack 2014", 900, hashB),   // generic title, high seed
            row("Saga Book One 1-18 CBZ", 4, hashB)        // exact title/range, low seed
        });
    require(dupEvidence.size() == 1, "the shared hash collapses to one row");
    require(dupEvidence.first().confidence == QStringLiteral("strong")
                && dupEvidence.first().evidence.contains(QStringLiteral("TITLE")),
            "the exact-title listing's evidence survives dedup, not the generic one");
    require(dupEvidence.first().src.seeders == 900,
            "the highest seed count is retained as volatile metadata");

    // ── Format-scoped coverage + trusted uploader (Task 4) ──────────────────
    // "Invincible Compendiums v01-v03" advertises a Compendium range covering
    // ordinal 1 — a chosen pack whose name advertises a matching-format range
    // must rank STRONG, not the old token-only WEAK, and duplicate infohashes
    // must retain BOTH the coverage evidence and the trusted-uploader tag.
    const ComicEditionIdentity::ComicEditionTarget compendiumTarget =
        makeTarget(QStringLiteral("Invincible"), QStringLiteral("Invincible Compendium One"),
                  QStringLiteral("Compendium"), QString(), QString());
    require(compendiumTarget.format == ComicEditionIdentity::ComicCollectionFormat::Compendium
                && compendiumTarget.ordinal == 1,
            "sanity: catalog format + worded ordinal parse Compendium #1");

    const QList<RankedComicTorrent> coverageDedup = ComicTorrentRanker::rankForEdition(
        compendiumTarget,
        {
            row("Random Comics Bundle 2020", 500, hashD),               // generic, high seed
            row("Invincible Compendiums v01-v03 (- Nem -)", 6, hashD)   // exact coverage + trust, low seed
        });
    require(coverageDedup.size() == 1, "shared infohash collapses to one canonical row");
    require(coverageDedup.first().confidence == QStringLiteral("strong"),
            "a matching-format range pack ranks strong, not weak token-match");
    require(coverageDedup.first().coverageMatch, "coverage flag is set on the canonical row");
    require(coverageDedup.first().evidence.contains(QStringLiteral("COVERAGE")),
            "coverage evidence survives infohash dedup");
    require(coverageDedup.first().evidence.contains(QStringLiteral("UPLOADER"))
                && coverageDedup.first().trustTier == 1
                && coverageDedup.first().uploaderName == QStringLiteral("Nem"),
            "trusted-uploader evidence survives infohash dedup");
    require(coverageDedup.first().src.seeders == 500,
            "the higher seed count is retained after aggregation");

    // A DIFFERENT format ("TPBs") advertising the same ordinal range is NOT
    // coverage for a Compendium target, and the trusted "Nem" tag must never
    // rescue that conflict into a strong ranking.
    const QList<RankedComicTorrent> conflict = ComicTorrentRanker::rankForEdition(
        compendiumTarget,
        {
            row("Invincible TPBs v01-v03 (- Nem -)", 40, hashE)
        });
    require(conflict.size() == 1, "conflicting-format row still surfaces (universal filter)");
    require(!conflict.first().coverageMatch,
            "a different-format range is not coverage for this target");
    require(conflict.first().confidence != QStringLiteral("strong"),
            "trust cannot rescue a format conflict into strong");
    require(conflict.first().trustTier == 1,
            "the trusted uploader tag is still recognized, just not enough alone");

    // Variant-row projection exposes the QML-facing contract fields.
    const QVariantList variants = ComicTorrentRanker::toVariantRows(picker);
    require(variants.size() == 3, "variant projection preserves every ranked row");
    const QVariantMap top = variants.first().toMap();
    require(top.value("infoHash").toString() == hashC
                && top.value("confidence").toString() == QStringLiteral("strong")
                && top.value("evidence").toStringList().contains(QStringLiteral("ISBN")),
            "variant row carries hash, confidence, and evidence for QML");

    const QVariantList coverageVariants = ComicTorrentRanker::toVariantRows(coverageDedup);
    const QVariantMap coverageTop = coverageVariants.first().toMap();
    require(coverageTop.value("coverage").toBool() == true
                && coverageTop.value("uploader").toString() == QStringLiteral("Nem")
                && coverageTop.value("trustTier").toInt() == 1,
            "variant row carries coverage/uploader/trustTier for QML");

    std::cout << "comic_torrent_ranker_harness PASS\n";
    return 0;
}
