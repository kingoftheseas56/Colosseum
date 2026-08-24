// Arc 18 M1 — the shared volume-identity grammar harness (contract:
// VOLUME-IDENTITY-AND-INDEX-CONTRACT §1-2, TEST-MATRIX M1). Pins the string-safe
// decimal identity that replaces the two private int/double grammars: canonical
// normalization, explicit-marker coverage (fractional included), named fail-closed
// labels, pure decimal-string ordering, and path evidence resolution.
#include "torrent/MangaVolumeIdentity.h"

#include <QString>
#include <QStringList>

#include <cstdlib>
#include <iostream>

using namespace MangaVolumeIdentity;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

VolumeCoverage coverageOf(const QString& text)
{
    return detectCoverage(text, EvidenceSource::ReleaseTitle);
}
} // namespace

int main()
{
    // ── Canonical numeric normalization (no int/double anywhere) ─────────────
    require(canonicalizeNumber(QStringLiteral("01")) == QStringLiteral("1"), "01 -> 1");
    require(canonicalizeNumber(QStringLiteral("001.5")) == QStringLiteral("1.5"), "001.5 -> 1.5");
    require(canonicalizeNumber(QStringLiteral("10.50")) == QStringLiteral("10.5"), "10.50 -> 10.5");
    require(canonicalizeNumber(QStringLiteral("0.5")) == QStringLiteral("0.5"), "0.5 preserved");
    require(canonicalizeNumber(QStringLiteral("007")) == QStringLiteral("7"), "007 -> 7");
    require(canonicalizeNumber(QStringLiteral("abc")).isEmpty(), "non-numeric -> empty");
    require(canonicalizeNumber(QStringLiteral("1.2.3")).isEmpty(), "ambiguous decimal -> empty");
    require(isNumericToken(QStringLiteral("10.5")), "10.5 is numeric");
    require(!isNumericToken(QStringLiteral("Special")), "Special is not numeric");

    // ── Explicit-marker coverage: singles ────────────────────────────────────
    require(coverageOf(QStringLiteral("Series v12")).isSingle()
                && coverageOf(QStringLiteral("Series v12")).lo.canonical == QStringLiteral("12"),
            "v12 -> single 12");
    require(coverageOf(QStringLiteral("Series Vol. 3")).lo.canonical == QStringLiteral("3"),
            "Vol. 3 -> single 3");
    require(coverageOf(QStringLiteral("Series Volume 07")).lo.canonical == QStringLiteral("7"),
            "Volume 07 -> single 7 (zero stripped)");
    require(coverageOf(QStringLiteral("Series Vol 1.5")).lo.canonical == QStringLiteral("1.5"),
            "Vol 1.5 -> fractional single");
    require(coverageOf(QStringLiteral("Series volumes 5")).lo.canonical == QStringLiteral("5"),
            "volumes 5 -> single 5");

    // ── Explicit-marker coverage: inclusive ranges ───────────────────────────
    {
        const VolumeCoverage c = coverageOf(QStringLiteral("Series v01-v12"));
        require(c.isRange() && c.lo.canonical == QStringLiteral("1")
                    && c.hi.canonical == QStringLiteral("12"),
                "v01-v12 -> range 1..12");
    }
    {
        const VolumeCoverage c = coverageOf(QStringLiteral("Series Vol 1 - Vol 12"));
        require(c.isRange() && c.lo.canonical == QStringLiteral("1")
                    && c.hi.canonical == QStringLiteral("12"),
                "repeated-prefix range -> 1..12");
    }
    {
        const VolumeCoverage c = coverageOf(QStringLiteral("Series Volumes 1-3"));
        require(c.isRange() && c.lo.canonical == QStringLiteral("1")
                    && c.hi.canonical == QStringLiteral("3"),
                "Volumes 1-3 -> range 1..3");
    }
    {
        const VolumeCoverage c = coverageOf(QStringLiteral("Series v1.5-2.5"));
        require(c.isRange() && c.lo.canonical == QStringLiteral("1.5")
                    && c.hi.canonical == QStringLiteral("2.5"),
                "fractional range bounds");
    }

    // ── A bare number is NEVER volume evidence ───────────────────────────────
    require(!coverageOf(QStringLiteral("Series Chapter 2")).has(), "Chapter 2 is not coverage");
    require(!coverageOf(QStringLiteral("Series 001")).has(), "bare 001 is not coverage");
    require(!coverageOf(QStringLiteral("av2 boom")).has(), "marker needs a word boundary");
    require(!coverageOf(QStringLiteral("Series v5-2")).has(), "inverted range fails closed");

    // ── Named/special labels: textual, fail-closed ───────────────────────────
    {
        const VolumeCoverage c = coverageOf(QStringLiteral("Series Volume Special"));
        require(c.isSingle() && c.lo.isNamed()
                    && c.lo.canonical == QStringLiteral("special"),
                "Volume Special -> named single");
    }
    {
        const VolumeCoverage c = coverageOf(QStringLiteral("Series Vol. Omega"));
        require(c.isSingle() && c.lo.isNamed()
                    && c.lo.canonical == QStringLiteral("omega"),
                "Vol. Omega -> named single");
    }
    require(coversTarget(coverageOf(QStringLiteral("Series Volume Special")),
                         QStringLiteral("SPECIAL")),
            "named target matches by folded text");
    require(!coversTarget(coverageOf(QStringLiteral("Series Volume Special")),
                          QStringLiteral("2")),
            "numeric target never matches a named coverage");
    require(!coversTarget(coverageOf(QStringLiteral("Series v2")),
                          QStringLiteral("special")),
            "named target never matches a numeric coverage");

    // ── Decimal-string ordering and equality ─────────────────────────────────
    require(numericCompare(QStringLiteral("9"), QStringLiteral("10")) < 0, "9 < 10");
    require(numericCompare(QStringLiteral("1.25"), QStringLiteral("1.5")) < 0, "1.25 < 1.5");
    require(numericCompare(QStringLiteral("1.5"), QStringLiteral("1.50")) == 0, "1.5 == 1.50");
    require(numericCompare(QStringLiteral("2"), QStringLiteral("2")) == 0, "2 == 2");
    require(numericCompare(QStringLiteral("10.5"), QStringLiteral("2")) > 0, "10.5 > 2");
    require(labelsEqual(QStringLiteral("1.50"), QStringLiteral("1.5")), "labelsEqual numeric");
    require(!labelsEqual(QStringLiteral("vol 2"), QStringLiteral("2")),
            "labelsEqual never coerces named to numeric");

    // ── coversTarget across kinds ────────────────────────────────────────────
    require(coversTarget(coverageOf(QStringLiteral("Series v2")), QStringLiteral("02")),
            "zero-padded target matches by value");
    require(coversTarget(coverageOf(QStringLiteral("Series v10.5")), QStringLiteral("10.50")),
            "fractional target matches by value");
    require(coversTarget(coverageOf(QStringLiteral("Series v01-v12")), QStringLiteral("10.5")),
            "range covers interior fractional volume");
    require(coversTarget(coverageOf(QStringLiteral("Series v01-v12")), QStringLiteral("1")),
            "range covers its lower bound");
    require(!coversTarget(coverageOf(QStringLiteral("Series v01-v12")), QStringLiteral("13")),
            "range rejects exterior volume");
    require(!coversTarget(coverageOf(QStringLiteral("Series v2")), QStringLiteral("3")),
            "single rejects other volume");
    require(!coversTarget(VolumeCoverage{}, QStringLiteral("2")), "no coverage fails closed");

    // ── Path evidence: filename wins, then deepest directory ─────────────────
    {
        const VolumeCoverage c =
            coverageForPath(QStringLiteral("Series Vol 3/Series v02.cbz"));
        require(c.source == EvidenceSource::Filename && c.lo.canonical == QStringLiteral("2"),
                "filename evidence outranks directory evidence");
    }
    {
        const VolumeCoverage c = coverageForPath(QStringLiteral("Series Vol 2/001.cbz"));
        require(c.source == EvidenceSource::Directory && c.lo.canonical == QStringLiteral("2"),
                "directory evidence when filename has no marker");
    }
    {
        const VolumeCoverage c = coverageForPath(QStringLiteral("Series\\Vol 2\\001.cbz"));
        require(c.source == EvidenceSource::Directory && c.lo.canonical == QStringLiteral("2"),
                "backslash separators understood (libtorrent Windows paths)");
    }
    {
        const VolumeCoverage c =
            coverageForPath(QStringLiteral("Series Vol 2/Volume 10.5.cbz"));
        require(c.source == EvidenceSource::Filename
                    && c.lo.canonical == QStringLiteral("10.5"),
                "fractional filename evidence");
    }
    require(!coverageForPath(QStringLiteral("random/name.cbz")).has(),
            "no marker anywhere -> no coverage");

    std::cout << "MANGA_VOLUME_IDENTITY_OK\n";
    return 0;
}
