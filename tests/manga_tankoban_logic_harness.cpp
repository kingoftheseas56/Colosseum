// Tankoban volume model contract: volume numbers are string-safe (fractional and
// special volumes never collapse through float), identity ids/settings keys escape
// the series id, and every volume row is emitted as a canonical record even when no
// chapter maps onto it.
#include "engine/MangaTankobanLogic.h"

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

    std::cout << "MANGA_TANKOBAN_LOGIC_OK\n";
    return 0;
}
