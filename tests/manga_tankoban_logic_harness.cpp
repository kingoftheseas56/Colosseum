// Tankoban volume model contract: volume numbers are string-safe (fractional and
// special volumes never collapse through float), identity ids/settings keys escape
// the series id, and every volume row is emitted as a canonical record even when no
// chapter maps onto it.
#include "engine/MangaSynopsisEnricher.h"
#include "engine/MangaTankobanLogic.h"
#include "torrent/BookTorrentMagnet.h"
#include "torrent/MangaNyaaSource.h"

#include <QByteArray>
#include <QDir>
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

    // ── A WHOLE SHELF with no chapters at all is still canonical ──────────
    // Volumes come from our own volume DB; chapters come from WeebCentral, which
    // rate-limits (429) and can fail for an entire session. The series page
    // therefore seeds the shelf the moment volumes land, WITHOUT waiting for
    // chapters — which is only safe because a full volume list survives an empty
    // chapter list intact. Pinned here because that is the assumption the page
    // relies on: Hemanth hit a 429 on Vagabond and got a blank page while all 38
    // volumes sat in hand (2026-07-30). Every volume must still be a row, in order.
    {
        QVariantList shelf;
        for (int n = 1; n <= 38; ++n)
            shelf.append(QVariantMap{{"number", QString::number(n)},
                                     {"chapterStart", QString::number(n * 10 - 9)},
                                     {"chapterEnd", QString::number(n * 10)}});
        const auto ssnap = prepareSeries({{"seriesId", "vag"}}, shelf, QVariantList{});
        require(ssnap.volumes.size() == 38,
                "a 38-volume shelf survives an EMPTY chapter list");
        require(ssnap.volumes.first().number == QStringLiteral("1")
                    && ssnap.volumes.last().number == QStringLiteral("38"),
                "the chapterless shelf keeps volume order 1..38");
        for (const VolumeRecord& v : ssnap.volumes)
            require(v.chapterIds.isEmpty() && !v.id.isEmpty(),
                    "every chapterless volume is still a real, addressable record");
    }

    // ── Fix 1: chapterMapComplete gates the WeebCentral fallback ──────────
    // A range-only volume whose whole [1,3] range exists and maps is COMPLETE:
    // chapters 1,2,3 all fall in range and all land here.
    {
        const QVariantList cVols{
            {QVariantMap{{"number", "1"}, {"chapterStart", "1"}, {"chapterEnd", "3"}}}};
        const QVariantList cChaps{{QVariantMap{{"id", "c1"}, {"number", "1"}}},
                                  {QVariantMap{{"id", "c2"}, {"number", "2"}}},
                                  {QVariantMap{{"id", "c3"}, {"number", "3"}}}};
        const auto csnap = prepareSeries({{"seriesId", "cc"}}, cVols, cChaps);
        require(csnap.volumes.size() == 1, "complete-range volume emitted");
        require(csnap.volumes[0].chapterIds == QStringList({"c1", "c2", "c3"}),
                "complete-range volume collects its whole [1,3] range");
        require(csnap.volumes[0].chapterMapComplete,
                "a fully-mapped [1,3] range volume is chapterMapComplete");
    }
    // Volume 1 has range [1,3] but chapters 2 & 3 are explicitly tagged to volume 2,
    // so only chapter 1 maps to volume 1 → its map is INCOMPLETE.
    {
        const QVariantList iVols{
            {QVariantMap{{"number", "1"}, {"chapterStart", "1"}, {"chapterEnd", "3"}}},
            {QVariantMap{{"number", "2"}}}};
        const QVariantList iChaps{
            {QVariantMap{{"id", "c1"}, {"number", "1"}}},
            {QVariantMap{{"id", "c2"}, {"number", "2"}, {"volume", "2"}}},
            {QVariantMap{{"id", "c3"}, {"number", "3"}, {"volume", "2"}}}};
        const auto isnap = prepareSeries({{"seriesId", "ii"}}, iVols, iChaps);
        require(isnap.volumes.size() == 2, "incomplete-range volumes emitted");
        require(isnap.volumes[0].chapterIds == QStringList({"c1"}),
                "volume 1 maps only its one un-claimed in-range chapter");
        require(!isnap.volumes[0].chapterMapComplete,
                "a [1,3] range volume missing chapters 2 & 3 (claimed elsewhere) is NOT complete");
        require(isnap.volumes[1].chapterMapComplete,
                "an explicitly-tagged (range-less) volume with chapters is complete");
    }

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
                    == BookTorrentMagnet::buildMagnet("a1b2c3d4e5f60718293a4b5c6d7e8f9012345678"),
                "magnet uri built tracker-bearing from infohash (bare-DHT metadata "
                "stalls on this network — 2026-08-16 resolving hang)");
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

    // ── Catalogue-independence Slice 4: series-mode filterAndRank ─────────────
    // Reuses the same 7-item fixture as the volume-mode contract above. Series
    // mode has no volume to target — item 4 (Volume 03, otherwise rejected as
    // "wrong target" in volume mode) must now survive, while every OTHER
    // rejection (chapter-pack item 3, blocked-uploader item 5, raw item 6,
    // duplicate-infohash item 7) still applies unchanged.
    {
        TrustTable trust;
        trust.tier1 = QSet<QString>{QStringLiteral("1r0n")};
        trust.blocked = QSet<QString>{QStringLiteral("baduploader")};

        const auto parsed = MangaNyaaSource::parseRss(fixture("nyaa_volume_results.xml"));
        require(parsed.size() == 7, "series-mode reuses the same 7-item RSS parse");

        const auto ranked = MangaNyaaSource::filterAndRank(
            {"s1", "Grand Blue Dreaming", "Kenji Inoue", {"Grand Blue"}, {}},
            QString(), parsed, trust, /*seriesMode=*/true);
        require(ranked.size() == 3,
                "series mode keeps volume 2 (item 1), the 1-12 pack (item 2), AND the "
                "otherwise-wrong-target volume 3 (item 4) — three, not volume mode's two");

        bool sawVol2 = false, sawPack = false, sawVol3 = false;
        for (const auto& c : ranked) {
            if (c.coverageLo == "2" && c.coverageHi == "2") sawVol2 = true;
            if (c.coverageLo == "1" && c.coverageHi == "12") sawPack = true;
            if (c.coverageLo == "3" && c.coverageHi == "3") sawVol3 = true;
        }
        require(sawVol2 && sawPack, "series mode still carries the two volume-mode survivors");
        require(sawVol3,
                "series mode does NOT reject volume 3 for missing a target — the volume-target "
                "match is the one thing series mode skips");

        for (const auto& c : ranked)
            require(!c.title.contains(QStringLiteral("Chapter")),
                    "series mode still rejects a chapter pack (item 3) even with no volume target");
        for (const auto& c : ranked)
            require(c.uploader != QStringLiteral("baduploader"),
                    "series mode still drops a blocked uploader (item 5) — trust tiers unchanged");
        for (const auto& c : ranked)
            require(!c.title.toLower().contains(QStringLiteral("(japanese)")),
                    "series mode still rejects a raw/untranslated release (item 6)");
        int hashHits = 0;
        for (const auto& c : ranked)
            if (c.infoHash == "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678")
                ++hashHits;
        require(hashHits == 1,
                "series mode still dedups by infohash (item 7's repost of item 1)");

        // Trust tiers themselves are unchanged in series mode: the exact volume 2
        // release from the trusted uploader still ranks ahead of the untrusted
        // 1-12 pack (tier compare is the sort's first key, independent of target).
        require(ranked[0].tier == 1 && ranked[0].coverageLo == "2",
                "series mode still ranks the tier-1 uploader's release first");
    }

    // ── Task 3: honest lazy synopsis enrichment (Open Library + Apple Books) ──
    // matchOpenLibrary / matchApple accept a synopsis ONLY on genuine target-
    // volume evidence; equal-strong Apple candidates with no author distinction
    // are left empty; the series synopsis is never repeated as a volume synopsis.
    {
        SeriesSnapshot series;
        series.seriesId = QStringLiteral("mangadex:grand-blue");
        series.title = QStringLiteral("Grand Blue Dreaming");
        series.author = QStringLiteral("Kenji Inoue");
        series.aliases = QStringList{QStringLiteral("Grand Blue")};

        VolumeRecord vol1;
        vol1.seriesId = series.seriesId;
        vol1.number = QStringLiteral("1");
        vol1.id = volumeId(series.seriesId, vol1.number);
        vol1.title = QStringLiteral("Grand Blue Dreaming, Vol. 1");

        VolumeRecord vol4;
        vol4.seriesId = series.seriesId;
        vol4.number = QStringLiteral("4");
        vol4.id = volumeId(series.seriesId, vol4.number);
        vol4.title = QStringLiteral("Grand Blue Dreaming, Vol. 4");
        series.volumes = QList<VolumeRecord>{vol1, vol4};

        const QString seriesSynopsis = QStringLiteral(
            "Grand Blue Dreaming follows college freshman Iori Kitahara as he joins a "
            "raucous seaside diving club and stumbles through the drunken, sun-soaked "
            "chaos of student life on the Izu peninsula.");

        const QByteArray olFixture = fixture("openlibrary_volume.json");
        const QByteArray appleFixture = fixture("apple_books_volume.json");
        const QByteArray ambiguousFixture = fixture("apple_books_ambiguous.json");

        // ── Pinned contract ────────────────────────────────────────────────
        const auto ol = MangaSynopsisEnricher::matchOpenLibrary(series, vol1, olFixture);
        require(ol.accepted && ol.source == "openlibrary", "strong OL result accepted");
        const auto apple = MangaSynopsisEnricher::matchApple(series, vol4, appleFixture);
        require(apple.accepted && apple.sourceUrl.contains("books.apple"),
                "exact Apple volume accepted");
        const auto ambiguous = MangaSynopsisEnricher::matchApple(series, vol4, ambiguousFixture);
        require(!ambiguous.accepted, "equal strong candidates remain empty");
        require(!MangaSynopsisEnricher::acceptDistinctVolumeText(seriesSynopsis, seriesSynopsis),
                "series synopsis is never repeated as volume synopsis");

        // ── Provenance on the accepted results ─────────────────────────────
        require(ol.volumeId == vol1.id && ol.confidence == "exact-isbn"
                    && ol.sourceUrl.contains("openlibrary.org"),
                "accepted OL record carries volumeId, exact-isbn confidence and an OL url");
        require(apple.source == "apple" && apple.volumeId == vol4.id
                    && apple.confidence == "exact-title-volume",
                "accepted Apple record carries volumeId and exact-title-volume confidence");

        // ── OL honesty: wrong volume, bare series, edition qualifier rejected ─
        const QByteArray olWrongVol = QByteArray(
            "{\"docs\":[{\"key\":\"/works/OLW\",\"title\":\"Grand Blue Dreaming, Vol. 2\","
            "\"isbn\":[\"9781632365705\"],\"description\":\"Second-volume synopsis, plenty long to be real.\"}]}");
        require(!MangaSynopsisEnricher::matchOpenLibrary(series, vol1, olWrongVol).accepted,
                "an OL doc whose volume does not match the target is rejected");
        const QByteArray olBareSeries = QByteArray(
            "{\"docs\":[{\"key\":\"/works/OLB\",\"title\":\"Grand Blue Dreaming\","
            "\"isbn\":[\"9781632365606\"],\"description\":\"A whole-series blurb with no volume marker at all.\"}]}");
        require(!MangaSynopsisEnricher::matchOpenLibrary(series, vol1, olBareSeries).accepted,
                "an OL doc with no explicit volume (bare series) is rejected");
        const QByteArray olFrench = QByteArray(
            "{\"docs\":[{\"key\":\"/works/OLF\",\"title\":\"Grand Blue Dreaming, Vol. 1 (French Edition)\","
            "\"isbn\":[\"9781632365606\"],\"description\":\"Une longue description qui ne correspond pas au disque canonique.\"}]}");
        require(!MangaSynopsisEnricher::matchOpenLibrary(series, vol1, olFrench).accepted,
                "an OL edition qualifier that is not the canonical record (French Edition) is rejected");

        // ── OL confidence gradation: title+volume without an English ISBN ───
        const QByteArray olNoIsbn = QByteArray(
            "{\"docs\":[{\"key\":\"/works/OLZ\",\"title\":\"Grand Blue Dreaming, Vol. 1\","
            "\"description\":\"A distinct first-volume synopsis with enough words to count.\"}]}");
        const auto olTv = MangaSynopsisEnricher::matchOpenLibrary(series, vol1, olNoIsbn);
        require(olTv.accepted && olTv.confidence == "exact-title-volume",
                "an OL title+volume match without an English ISBN is exact-title-volume");
        const QByteArray olJpIsbn = QByteArray(
            "{\"docs\":[{\"key\":\"/works/OLJ\",\"title\":\"Grand Blue Dreaming, Vol. 1\","
            "\"isbn\":[\"9784063842401\"],\"description\":\"Distinct first-volume text, long enough to be a synopsis.\"}]}");
        const auto olJp = MangaSynopsisEnricher::matchOpenLibrary(series, vol1, olJpIsbn);
        require(olJp.accepted && olJp.confidence == "exact-title-volume",
                "a non-English (978-4) ISBN does not earn exact-isbn confidence");

        // ── Apple honesty: wrong volume, author tie-break, no-author-distinction ─
        const QByteArray appleWrong = QByteArray(
            "{\"results\":[{\"trackName\":\"Grand Blue Dreaming, Vol. 3\",\"artistName\":\"Kenji Inoue\","
            "\"description\":\"Third volume, not the requested target.\","
            "\"trackViewUrl\":\"https://books.apple.com/us/book/x/id3\"}]}");
        require(!MangaSynopsisEnricher::matchApple(series, vol4, appleWrong).accepted,
                "an Apple result for the wrong volume is rejected");
        const QByteArray appleAuthorTie = QByteArray(
            "{\"results\":["
            "{\"trackName\":\"Grand Blue Dreaming, Vol. 4\",\"artistName\":\"Kenji Inoue\","
            "\"description\":\"The canonical author fourth-volume blurb, distinct and lengthy.\","
            "\"trackViewUrl\":\"https://books.apple.com/us/book/a/id4a\"},"
            "{\"trackName\":\"Grand Blue Dreaming, Vol. 4\",\"artistName\":\"Bootleg Press\","
            "\"description\":\"A different fourth-volume blurb from an unrelated label.\","
            "\"trackViewUrl\":\"https://books.apple.com/us/book/b/id4b\"}]}");
        const auto appleTie = MangaSynopsisEnricher::matchApple(series, vol4, appleAuthorTie);
        require(appleTie.accepted && appleTie.text.contains("canonical author"),
                "author agreement breaks a near-tie and selects the canonical-author edition");
        const QByteArray appleBothAuthor = QByteArray(
            "{\"results\":["
            "{\"trackName\":\"Grand Blue Dreaming, Vol. 4\",\"artistName\":\"Kenji Inoue\","
            "\"description\":\"One fourth-volume blurb, plenty long to be a real synopsis.\","
            "\"trackViewUrl\":\"https://books.apple.com/us/book/c/id4c\"},"
            "{\"trackName\":\"Grand Blue Dreaming, Vol. 4\",\"artistName\":\"Kenji Inoue\","
            "\"description\":\"A second, different fourth-volume blurb also from the author.\","
            "\"trackViewUrl\":\"https://books.apple.com/us/book/d/id4d\"}]}");
        require(!MangaSynopsisEnricher::matchApple(series, vol4, appleBothAuthor).accepted,
                "equal-strong candidates with no distinguishing author signal stay empty");

        // ── acceptDistinctVolumeText: accept different, reject echoes/siblings ─
        require(MangaSynopsisEnricher::acceptDistinctVolumeText(
                    QStringLiteral("A genuinely different volume-specific synopsis."), seriesSynopsis),
                "genuinely different volume text is accepted");
        const QString echoed = QStringLiteral(
            "  GRAND blue dreaming FOLLOWS college freshman iori kitahara as he JOINS a raucous "
            "seaside diving club and stumbles through the drunken, sun-soaked chaos of student "
            "life on the izu peninsula!!!  ");
        require(!MangaSynopsisEnricher::acceptDistinctVolumeText(echoed, seriesSynopsis),
                "text equal to the series synopsis (case/space/punct-insensitive) is rejected");
        const QString sharedVolText = QStringLiteral("Identical text pasted across two different volumes.");
        require(!MangaSynopsisEnricher::acceptDistinctVolumeText(sharedVolText, sharedVolText),
                "text repeated verbatim from another volume is withheld");

        // ── Cache round-trip: accepted provenance survives, rejected never promotes ─
        const QString cachePath =
            QDir::tempPath() + QStringLiteral("/tankoban_synopsis_cache_test.json");
        QFile::remove(cachePath);
        const QString acceptedId = vol1.id;
        const QString rejectedId = vol4.id;
        {
            MangaSynopsisEnricher writer(nullptr, cachePath);
            SynopsisRecord acc;
            acc.volumeId = acceptedId;
            acc.text = QStringLiteral("An accepted, provenance-bearing volume synopsis.");
            acc.source = QStringLiteral("openlibrary");
            acc.sourceUrl = QStringLiteral("https://openlibrary.org/works/OL20948209W");
            acc.confidence = QStringLiteral("exact-isbn");
            acc.fetchedAt = QStringLiteral("2026-07-14T12:00:00Z");
            acc.accepted = true;
            writer.cacheRecord(acc);

            SynopsisRecord rej; // an ambiguous/miss record — kept, but never accepted
            rej.volumeId = rejectedId;
            rej.confidence = QStringLiteral("none");
            rej.fetchedAt = QStringLiteral("2026-07-14T12:00:00Z");
            rej.accepted = false;
            writer.cacheRecord(rej);
        }
        {
            MangaSynopsisEnricher reader(nullptr, cachePath);
            const SynopsisRecord got = reader.cached(acceptedId);
            require(got.accepted && got.source == "openlibrary"
                        && got.sourceUrl.contains("openlibrary.org")
                        && got.confidence == "exact-isbn" && got.text.contains("provenance"),
                    "accepted provenance survives a cache reload");
            const SynopsisRecord miss = reader.cached(rejectedId);
            require(!miss.accepted,
                    "an ambiguous (rejected) record never reloads as accepted");
        }
        QFile::remove(cachePath);
    }

    std::cout << "MANGA_TANKOBAN_LOGIC_OK\n";
    return 0;
}
