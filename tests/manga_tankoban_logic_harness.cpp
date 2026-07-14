// Tankoban volume model contract: volume numbers are string-safe (fractional and
// special volumes never collapse through float), identity ids/settings keys escape
// the series id, and every volume row is emitted as a canonical record even when no
// chapter maps onto it.
#include "engine/MangaTankobanLogic.h"
#include "torrent/MangaNyaaSource.h"

#include <QByteArray>
#include <QFile>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <cstdlib>
#include <iostream>

using namespace MangaTankoban;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Read a Nyaa RSS fixture as raw bytes. The directory is injected by CMake via
// the TANKOBAN_FIXTURES_DIR compile definition so the harness finds the file no
// matter the build/working directory.
QByteArray fixture(const QString& name)
{
    const QString path = QStringLiteral(TANKOBAN_FIXTURES_DIR) + QLatin1Char('/') + name;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::cerr << "FAIL: cannot open fixture " << path.toStdString() << '\n';
        std::exit(1);
    }
    return f.readAll();
}

// Build a one-item Nyaa RSS document inline. Used for the precision-fix cases so
// the shared 7-item fixture (and its pinned ranked.size()==2) stays untouched.
QByteArray rssItem(const char* title, const char* uploader, const char* infoHash)
{
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                      "<rss version=\"2.0\" xmlns:nyaa=\"https://nyaa.si/xmlns/nyaa\">"
                      "<channel><item><title>")
        + title
        + "</title>"
          "<nyaa:seeders>60</nyaa:seeders><nyaa:leechers>2</nyaa:leechers>"
          "<nyaa:infoHash>"
        + infoHash
        + "</nyaa:infoHash><nyaa:size>40.0 MiB</nyaa:size><nyaa:uploader>"
        + uploader
        + "</nyaa:uploader></item></channel></rss>";
}
} // namespace

int main()
{
    // ── Canonical identity ────────────────────────────────────────────────
    require(normalizeVolumeNumber(QStringLiteral("10.5")) == QStringLiteral("10.5"),
            "fractional volume survives");
    require(volumeId("mangafire:berserk", "10.5") ==
            "tankoban:mangafire%3Aberserk:volume:10.5", "stable escaped id");
    require(settingsKey("mangafire:berserk") ==
            "manga/tankobanMode/mangafire%3Aberserk", "per-series settings key");

    // ── Normalization contract ────────────────────────────────────────────
    require(normalizeVolumeNumber(QStringLiteral("01")) == QStringLiteral("1"),
            "leading zero stripped");
    require(normalizeVolumeNumber(QStringLiteral(" 2 ")) == QStringLiteral("2"),
            "surrounding whitespace trimmed");
    require(normalizeVolumeNumber(QStringLiteral("0")) == QStringLiteral("0"),
            "bare zero preserved");
    require(normalizeVolumeNumber(QStringLiteral("007")) == QStringLiteral("7"),
            "run of leading zeroes collapsed to the significant digit");
    require(normalizeVolumeNumber(QVariant(10.5)) == QStringLiteral("10.5"),
            "double 10.5 renders without float noise");
    require(normalizeVolumeNumber(QVariant(2.0)) == QStringLiteral("2"),
            "integer-valued double drops the fractional tail");
    require(normalizeVolumeNumber(QVariant(0.5)) == QStringLiteral("0.5"),
            "sub-one double keeps its zero integer part");
    require(normalizeVolumeNumber(QVariant()) == QString(),
            "null variant yields empty");
    require(normalizeVolumeNumber(QStringLiteral("")) == QString(),
            "empty string yields empty");
    require(normalizeVolumeNumber(QStringLiteral("   ")) == QString(),
            "whitespace-only yields empty");
    require(normalizeVolumeNumber(QStringLiteral("Extra")) == QStringLiteral("Extra"),
            "special/named volume is never collapsed");

    // ── Volume assembly: explicit chapter 'volume' field ──────────────────
    const QVariantList volumes{{QVariantMap{{"number", "1"}, {"cover", "a.jpg"}}},
                               {QVariantMap{{"number", "2"}, {"cover", "b.jpg"}}}};
    const QVariantList chapters{{QVariantMap{{"id", "c1"}, {"volume", "1"}}},
                                {QVariantMap{{"id", "c2"}, {"volume", "1"}}}};
    const auto snap = prepareSeries({{"seriesId", "s1"}, {"title", "Series"}}, volumes, chapters);
    require(snap.volumes.size() == 2, "source-less volume remains canonical");
    require(snap.volumes[0].chapterIds == QStringList{"c1", "c2"}, "chapter mapping retained");
    require(snap.volumes[1].chapterIds.isEmpty(),
            "unmatched volume still emitted with an empty chapter list");
    require(snap.volumes[0].id == QStringLiteral("tankoban:s1:volume:1"),
            "volume record carries its canonical id");
    require(snap.volumes[0].cover == QStringLiteral("a.jpg"), "volume cover retained");
    require(snap.seriesId == QStringLiteral("s1") && snap.title == QStringLiteral("Series"),
            "series descriptor retained");

    // ── Volume assembly: chapterStart/chapterEnd range fallback ───────────
    const QVariantList rangeVols{
        {QVariantMap{{"number", "1"}, {"chapterStart", "1"}, {"chapterEnd", "3"}}},
        {QVariantMap{{"number", "2"}, {"chapterStart", "4"}, {"chapterEnd", "6"}}}};
    const QVariantList rangeChaps{{QVariantMap{{"id", "c1"}, {"number", "1"}}},
                                  {QVariantMap{{"id", "c2"}, {"number", "2"}}},
                                  {QVariantMap{{"id", "c3"}, {"number", "5"}}}};
    const auto rsnap = prepareSeries({{"seriesId", "r1"}}, rangeVols, rangeChaps);
    require(rsnap.volumes.size() == 2, "range volumes emitted");
    require(rsnap.volumes[0].chapterIds == QStringList({"c1", "c2"}),
            "range 1-3 collects its chapters in input order");
    require(rsnap.volumes[1].chapterIds == QStringList({"c3"}),
            "range 4-6 collects its chapter");

    // ── Mixed: explicit 'volume' wins, range never re-grabs the same chapter ─
    // c1 is explicitly tagged volume 1 AND its number 1 falls inside volume 2's
    // 1-5 range. Phase 1 must own it; the range fallback must not duplicate it.
    const QVariantList mixedVols{
        {QVariantMap{{"number", "1"}}},
        {QVariantMap{{"number", "2"}, {"chapterStart", "1"}, {"chapterEnd", "5"}}}};
    const QVariantList mixedChaps{{QVariantMap{{"id", "c1"}, {"volume", "1"}, {"number", "1"}}}};
    const auto msnap = prepareSeries({{"seriesId", "m1"}}, mixedVols, mixedChaps);
    require(msnap.volumes.size() == 2, "mixed volumes emitted");
    require(msnap.volumes[0].chapterIds == QStringList({"c1"}),
            "explicit volume 1 owns c1");
    require(msnap.volumes[1].chapterIds.isEmpty(),
            "range fallback does not duplicate an already-claimed chapter");

    // ── A lone volume with no chapters at all is still canonical ──────────
    const QVariantList loneVols{{QVariantMap{{"number", "7"}}}};
    const auto lsnap = prepareSeries({{"seriesId", "l1"}}, loneVols, QVariantList{});
    require(lsnap.volumes.size() == 1, "lone volume emitted");
    require(lsnap.volumes[0].chapterIds.isEmpty(), "lone volume has an empty chapter list");
    require(lsnap.volumes[0].id == QStringLiteral("tankoban:l1:volume:7"), "lone volume id");

    // ── Nyaa volume discovery: query family + parse + trust filter + rank ──
    // Contract: for target volume "2" of "Grand Blue Dreaming" (alias "Grand
    // Blue"), only the exact Volume 2 and the inclusive 1-12 pack survive; the
    // chapter pack, the wrong-target Volume 3, the blocked uploader, the raw
    // release and the duplicate-infohash repost are all removed.
    {
        TrustTable trust;
        trust.tier1 = QSet<QString>{QStringLiteral("1r0n")};
        trust.blocked = QSet<QString>{QStringLiteral("baduploader")};

        const auto parsed = MangaNyaaSource::parseRss(fixture("nyaa_volume_results.xml"));
        require(parsed.size() == 7, "all seven RSS items parse before filtering");

        const auto ranked = MangaNyaaSource::filterAndRank(
            {"s1", "Grand Blue Dreaming", "Kenji Inoue", {"Grand Blue"}, {}}, "2", parsed, trust);
        require(ranked.size() == 2, "only exact volume and inclusive pack survive");
        require(ranked[0].coverageLo == "2" && ranked[0].coverageHi == "2",
                "standalone volume ranks before pack");
        require(ranked[1].coverageLo == "1" && ranked[1].coverageHi == "12",
                "inclusive pack retained");
        require(ranked[0].uploader == "1r0n", "uploader preserved");
        require(queryVariants("Grand Blue Dreaming", "2").contains("Grand Blue Dreaming Vol 2"),
                "Tankoban 2 query family retained");

        // Extended contract — lock the tricky rejections/derivations.
        require(ranked[0].standalone && !ranked[1].standalone,
                "standalone flag derives from coverage bounds (single vs range)");
        require(ranked[0].digitalHint, "digital-edition hint carried on the exact volume");
        require(ranked[0].infoHash == "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678",
                "infohash lower-cased and preserved");
        require(ranked[0].magnetUri
                    == "magnet:?xt=urn:btih:a1b2c3d4e5f60718293a4b5c6d7e8f9012345678",
                "magnet uri built from infohash");
        for (const auto& c : ranked)
            require(!c.title.contains(QStringLiteral("Chapter")),
                    "chapter pack never masquerades as a volume (even from a trusted uploader)");
        for (const auto& c : ranked)
            require(!c.title.toLower().contains(QStringLiteral("(japanese)")),
                    "raw japanese release rejected");
        for (const auto& c : ranked)
            require(c.uploader != QStringLiteral("baduploader"), "blocked uploader dropped");
        for (const auto& c : ranked)
            require(!(c.coverageLo == "3" && c.coverageHi == "3"),
                    "wrong-target volume 3 rejected");
        int hashHits = 0;
        for (const auto& c : ranked)
            if (c.infoHash == ranked[0].infoHash)
                ++hashHits;
        require(hashHits == 1, "duplicate infohash deduped to a single survivor");
    }

    // ── Ranking: digital/official edition wins an otherwise-equal tie ─────
    // Same tier, same standalone coverage, same seeders — the digital hint is
    // the deciding key, independent of document order.
    {
        TrustTable trust;
        trust.tier1 = QSet<QString>{QStringLiteral("1r0n")};
        const SeriesSnapshot series{"s1", "Grand Blue Dreaming", "Kenji Inoue", {"Grand Blue"}, {}};

        MangaNyaaCandidate plain;
        plain.title = QStringLiteral("Grand Blue Dreaming Volume 2 (1r0n)");
        plain.uploader = QStringLiteral("1r0n");
        plain.infoHash = QStringLiteral("1111111111111111111111111111111111111111");
        plain.coverageLo = QStringLiteral("2");
        plain.coverageHi = QStringLiteral("2");
        plain.standalone = true;
        plain.digitalHint = false;
        plain.seeders = 10;

        MangaNyaaCandidate digital;
        digital.title = QStringLiteral("Grand Blue Dreaming Volume 2 (Digital) (1r0n)");
        digital.uploader = QStringLiteral("1r0n");
        digital.infoHash = QStringLiteral("2222222222222222222222222222222222222222");
        digital.coverageLo = QStringLiteral("2");
        digital.coverageHi = QStringLiteral("2");
        digital.standalone = true;
        digital.digitalHint = true;
        digital.seeders = 10;

        // Document order deliberately puts the plain scan first.
        const auto order = MangaNyaaSource::filterAndRank(series, "2", {plain, digital}, trust);
        require(order.size() == 2, "both editions survive filtering");
        require(order[0].digitalHint && !order[1].digitalHint,
                "digital/official edition ordered ahead of the plain scan");
    }

    // ── I1: an inclusive pack written as "v01-v12" (both bounds v-marked) ─────
    // TB2's range regex required a bare number after the dash, so "v01-v12" fell
    // through to the single-volume regex and mis-read as vol 1. The widened range
    // regex now captures both bounds. Kept in its own block (own RSS + target) so
    // the original 7-item fixture and its pinned ranked.size()==2 are untouched.
    {
        TrustTable trust;
        trust.tier1 = QSet<QString>{QStringLiteral("1r0n")};
        const auto parsed = MangaNyaaSource::parseRss(
            rssItem("Grand Blue Dreaming v01-v12 (1r0n)", "1r0n",
                    "abcdef0123456789abcdef0123456789abcdef01"));
        require(parsed.size() == 1, "v01-v12 pack parses as one item");
        require(parsed[0].coverageLo == "1" && parsed[0].coverageHi == "12",
                "v01-v12 parses as an inclusive 1..12 range, not a single volume");
        require(!parsed[0].standalone, "multi-volume pack is not standalone");
        const auto ranked = MangaNyaaSource::filterAndRank(
            {"s1", "Grand Blue Dreaming", "Kenji Inoue", {"Grand Blue"}, {}}, "5", parsed, trust);
        require(ranked.size() == 1, "v01-v12 pack survives for a mid-range target 5");
        require(ranked[0].coverageLo == "1" && ranked[0].coverageHi == "12" && !ranked[0].standalone,
                "surviving pack keeps its 1..12 inclusive range");
    }

    // ── I2: an annotated volume ("v02 (Ch. 8-15)") still survives ─────────────
    // It carries a real volume marker; the chapter annotation must NOT trip the
    // chapter-pack reject (which now only applies when NO volume marker is found).
    {
        TrustTable trust;
        trust.tier1 = QSet<QString>{QStringLiteral("1r0n")};
        const auto parsed = MangaNyaaSource::parseRss(
            rssItem("Grand Blue Dreaming v02 (Ch. 8-15) (1r0n)", "1r0n",
                    "1234567890abcdef1234567890abcdef12345678"));
        const auto ranked = MangaNyaaSource::filterAndRank(
            {"s1", "Grand Blue Dreaming", "Kenji Inoue", {"Grand Blue"}, {}}, "2", parsed, trust);
        require(ranked.size() == 1,
                "annotated volume v02 (Ch. 8-15) survives — a chapter note is not a chapter pack");
        require(ranked[0].coverageLo == "2" && ranked[0].coverageHi == "2",
                "annotated volume covers exactly 2");
    }

    // ── M3: a series legitimately named with "Raw" is not a raw scan ──────────
    {
        TrustTable trust;
        trust.tier1 = QSet<QString>{QStringLiteral("1r0n")};
        const auto parsed = MangaNyaaSource::parseRss(
            rssItem("Raw Hero - Volume 2 (1r0n)", "1r0n",
                    "fedcba9876543210fedcba9876543210fedcba98"));
        const auto ranked = MangaNyaaSource::filterAndRank(
            {"s2", "Raw Hero", "", {}, {}}, "2", parsed, trust);
        require(ranked.size() == 1,
                "a series legitimately named 'Raw' is not dropped by the raw-scan filter");
        // The explicit (Japanese) marker still rejects, even for a 'Raw'-named series.
        const auto jp = MangaNyaaSource::parseRss(
            rssItem("Raw Hero - Volume 2 (Japanese)", "someone",
                    "0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f"));
        const auto jpRanked = MangaNyaaSource::filterAndRank(
            {"s2", "Raw Hero", "", {}, {}}, "2", jp, trust);
        require(jpRanked.isEmpty(),
                "explicit (Japanese) marker still rejects even a 'Raw'-named series");
    }

    // ── M5: apostrophe folding — "JoJo's" title matches a "JoJos" release ─────
    {
        TrustTable trust;
        trust.tier1 = QSet<QString>{QStringLiteral("1r0n")};
        const auto parsed = MangaNyaaSource::parseRss(
            rssItem("JoJos Bizarre Adventure Volume 2 (1r0n)", "1r0n",
                    "aa00aa00aa00aa00aa00aa00aa00aa00aa00aa00"));
        const auto ranked = MangaNyaaSource::filterAndRank(
            {"s3", "JoJo's Bizarre Adventure", "", {}, {}}, "2", parsed, trust);
        require(ranked.size() == 1,
                "apostrophe-folded series title (JoJo's) matches a JoJos release");
    }

    std::cout << "MANGA_TANKOBAN_LOGIC_OK\n";
    return 0;
}
