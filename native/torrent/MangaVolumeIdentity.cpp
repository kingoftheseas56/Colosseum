// native/torrent/MangaVolumeIdentity.cpp — see the header for the contract.
#include "torrent/MangaVolumeIdentity.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace MangaVolumeIdentity {
namespace {

// Numeric capture: optional leading zeros, digits, optional single fractional
// part. Kept as one capturing group so canonicalization stays a pure string
// operation — no toInt()/toDouble() anywhere in identity equality.
const QRegularExpression& rangeRegex()
{
    // The second bound may repeat the v / vol / volume prefix ("v01-v12",
    // "Vol 1 - Vol 12"): optional prefix on the upper bound only.
    static const QRegularExpression re(
        QStringLiteral(R"((?:\bv|\bvol\.?|\bvolumes?)\s*0*([0-9]+(?:\.[0-9]+)?)\s*-\s*(?:v|vol\.?|volume)?\s*0*([0-9]+(?:\.[0-9]+)?))"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression& singleRegex()
{
    // Alternation order matters: \bv alone must not swallow the v of "volume" —
    // the engine backtracks through vol/volume until one alternative lets the
    // digit capture start, exactly like the pre-Arc-18 grammars did.
    static const QRegularExpression re(
        QStringLiteral(R"((?:\bv|\bvol\.?\s*|\bvolume\s*|\bvolumes\s*)0*([0-9]+(?:\.[0-9]+)?))"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Named single: an explicit vol/volume marker immediately followed by a word
// ("Vol Special", "Volume Omega"). Tried ONLY after the numeric forms fail, so
// "Volume 5" never lands here. Alternatives are ordered LONGEST-FIRST: the
// word capture accepts any letter, so a shortest-first "\bvol" would match
// inside "Volume" and capture "ume".
const QRegularExpression& namedRegex()
{
    static const QRegularExpression re(
        QStringLiteral(R"((?:\bvolumes\s*|\bvolume\s*|\bvol\.?\s*)([A-Za-z][A-Za-z0-9'-]*))"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString normalizedIntPart(const QString& digits)
{
    QString d = digits;
    while (d.size() > 1 && d.startsWith(QLatin1Char('0')))
        d.remove(0, 1);
    return d;
}

QString normalizedFracPart(const QString& digits)
{
    QString d = digits;
    while (d.size() > 0 && d.endsWith(QLatin1Char('0')))
        d.chop(1);
    return d;
}

} // namespace

QString canonicalizeNumber(const QString& token)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*0*([0-9]+)(?:\.([0-9]+))?\s*$)"));
    const auto m = re.match(token);
    if (!m.hasMatch())
        return QString();
    const QString ip = normalizedIntPart(m.captured(1));
    const QString fp = normalizedFracPart(m.captured(2));
    return fp.isEmpty() ? ip : ip + QLatin1Char('.') + fp;
}

bool isNumericToken(const QString& token)
{
    return !canonicalizeNumber(token).isEmpty();
}

QString foldNamed(const QString& token)
{
    return token.trimmed().toCaseFolded().simplified();
}

VolumeLabel makeLabel(const QString& token)
{
    VolumeLabel label;
    const QString n = canonicalizeNumber(token);
    if (!n.isEmpty()) {
        label.kind = LabelKind::Numeric;
        label.canonical = n;
        return label;
    }
    const QString named = foldNamed(token);
    if (!named.isEmpty()) {
        label.kind = LabelKind::Named;
        label.canonical = named;
    }
    return label;
}

int numericCompare(const QString& a, const QString& b)
{
    const QStringList ka = a.split(QLatin1Char('.'));
    const QStringList kb = b.split(QLatin1Char('.'));
    if (ka.isEmpty() || kb.isEmpty())
        return 0;
    const QString ia = normalizedIntPart(ka.at(0));
    const QString ib = normalizedIntPart(kb.at(0));
    // Integer parts: shorter (after zero-strip) is smaller; equal length
    // compares lexicographically — pure string ordering, no int conversion.
    if (ia.size() != ib.size())
        return ia.size() < ib.size() ? -1 : 1;
    if (ia != ib)
        return ia < ib ? -1 : 1;
    // Fractional parts compare digit-by-digit from the decimal point with
    // trailing zeros stripped ("5" == "50", "25" < "5").
    const QString fa = ka.size() > 1 ? normalizedFracPart(ka.at(1)) : QString();
    const QString fb = kb.size() > 1 ? normalizedFracPart(kb.at(1)) : QString();
    if (fa == fb)
        return 0;
    return fa < fb ? -1 : 1;
}

bool labelsEqual(const QString& a, const QString& b)
{
    const QString ca = canonicalizeNumber(a);
    const QString cb = canonicalizeNumber(b);
    if (!ca.isEmpty() && !cb.isEmpty())
        return numericCompare(ca, cb) == 0;
    return foldNamed(a) == foldNamed(b);
}

VolumeCoverage detectCoverage(const QString& text, EvidenceSource source)
{
    VolumeCoverage coverage;
    coverage.source = source;

    // Range first so "v01-03" reads as an inclusive span, not a single "01".
    const auto rm = rangeRegex().match(text);
    if (rm.hasMatch()) {
        const VolumeLabel lo = makeLabel(rm.captured(1));
        const VolumeLabel hi = makeLabel(rm.captured(2));
        if (lo.isNumeric() && hi.isNumeric()) {
            // An inverted or fractional-split range is not honest volume
            // coverage; fail closed rather than guess an intent.
            if (numericCompare(lo.canonical, hi.canonical) <= 0) {
                coverage.kind = CoverageKind::Range;
                coverage.lo = lo;
                coverage.hi = hi;
            }
            return coverage;
        }
        return coverage;
    }

    const auto sm = singleRegex().match(text);
    if (sm.hasMatch()) {
        coverage.kind = CoverageKind::Single;
        coverage.lo = makeLabel(sm.captured(1));
        coverage.hi = coverage.lo;
        return coverage;
    }

    // Named only after every numeric form failed — fail-closed textual evidence.
    const auto nm = namedRegex().match(text);
    if (nm.hasMatch()) {
        const VolumeLabel named = makeLabel(nm.captured(1));
        if (named.isNamed()) {
            coverage.kind = CoverageKind::Single;
            coverage.lo = named;
            coverage.hi = named;
        }
    }
    return coverage;
}

VolumeCoverage coverageForPath(const QString& filePath)
{
    QString normalized = filePath;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/')); // libtorrent uses '\' on Windows
    const QFileInfo fi(normalized);

    const VolumeCoverage fromName = detectCoverage(fi.completeBaseName(), EvidenceSource::Filename);
    if (fromName.has())
        return fromName;

    const QStringList segments = fi.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (auto it = segments.crbegin(); it != segments.crend(); ++it) {
        const VolumeCoverage fromDir = detectCoverage(*it, EvidenceSource::Directory);
        if (fromDir.has())
            return fromDir;
    }
    return {};
}

bool coversTarget(const VolumeCoverage& coverage, const QString& target)
{
    if (!coverage.has())
        return false;

    const bool targetNumeric = isNumericToken(target);
    const QString canonicalTarget = targetNumeric ? canonicalizeNumber(target) : foldNamed(target);

    if (coverage.isSingle()) {
        if (coverage.lo.isNumeric())
            return targetNumeric && numericCompare(coverage.lo.canonical, canonicalTarget) == 0;
        if (coverage.lo.isNamed())
            return !targetNumeric && coverage.lo.canonical == canonicalTarget;
        return false;
    }

    if (coverage.isRange()) {
        // Ranges are numeric-only; a named target can never sit inside one.
        if (!targetNumeric || !coverage.lo.isNumeric() || !coverage.hi.isNumeric())
            return false;
        return numericCompare(coverage.lo.canonical, canonicalTarget) <= 0
            && numericCompare(canonicalTarget, coverage.hi.canonical) <= 0;
    }
    return false;
}

} // namespace MangaVolumeIdentity
