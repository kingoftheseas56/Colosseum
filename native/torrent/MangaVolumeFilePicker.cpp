#include "MangaVolumeFilePicker.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace MangaVolumeFilePicker {
namespace {

// ── Comic-archive gate ───────────────────────────────────────────────────────
// Only true comic archives are candidates; loose page images, .nfo, covers etc.
// are ignored. Case-insensitive, matching ComicTorrentFilePicker's accepted set.
bool isComicArchive(const QString& name)
{
    static const QSet<QString> exts{
        QStringLiteral("cbz"), QStringLiteral("cbr"),
        QStringLiteral("cb7"), QStringLiteral("cbt")
    };
    return exts.contains(QFileInfo(name).suffix().toLower());
}

// ── Volume coverage grammar ──────────────────────────────────────────────────
// Mirrors MangaNyaaSource::detectCoverage — explicit v / vol / volume marker +
// number, and inclusive ranges (v01-03, Vol 1 - 12, Volumes 1-3). A bare number
// is NEVER coverage (that keeps "Chapter 2" and page-number "001" out of the
// volume model). Reimplemented locally so the picker owns its parsing.
enum class CoverKind { None, Single, Range };

struct Cover {
    CoverKind kind = CoverKind::None;
    int lo = 0;
    int hi = 0;
    bool has() const { return kind != CoverKind::None; }
};

Cover detectCoverage(const QString& text)
{
    // Range first so "v01-03" reads as an inclusive span, not a single "01".
    static const QRegularExpression range(
        QStringLiteral(R"((?:\bv|\bvol\.?|\bvolumes?)\s*0*([0-9]+)\s*-\s*(?:v|vol\.?|volume)?\s*0*([0-9]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto rm = range.match(text);
    if (rm.hasMatch())
        return {CoverKind::Range, rm.captured(1).toInt(), rm.captured(2).toInt()};

    static const QRegularExpression single(
        QStringLiteral(R"((?:\bv|\bvol\.?\s*|\bvolume\s*)0*([0-9]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto sm = single.match(text);
    if (sm.hasMatch()) {
        const int n = sm.captured(1).toInt();
        return {CoverKind::Single, n, n};
    }
    return {};
}

enum class CoverSource { None, Filename, Directory };

struct Candidate {
    int index = -1;
    QString path;
    qint64 size = 0;
    Cover cover;
    CoverSource source = CoverSource::None;
};

// Resolve a file's volume coverage: the base FILE NAME wins; only when it holds
// no explicit marker do we fall back to the parent directory segments (deepest
// first). That ordering lets "Series Vol 3/Series v02.cbz" resolve to v02.
Candidate resolveCandidate(int index, const QString& name, qint64 size)
{
    Candidate c;
    c.index = index;
    c.path = name;
    c.size = size;

    QString normalized = name;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/')); // libtorrent uses '\' on Windows
    const QFileInfo fi(normalized);

    const Cover fromName = detectCoverage(fi.completeBaseName());
    if (fromName.has()) {
        c.cover = fromName;
        c.source = CoverSource::Filename;
        return c;
    }

    const QStringList segments = fi.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (auto it = segments.crbegin(); it != segments.crend(); ++it) {
        const Cover fromDir = detectCoverage(*it);
        if (fromDir.has()) {
            c.cover = fromDir;
            c.source = CoverSource::Directory;
            return c;
        }
    }
    return c; // no coverage anywhere
}

bool coversAsRange(const Cover& cover, int target)
{
    return cover.kind == CoverKind::Range && cover.lo <= target && target <= cover.hi;
}

bool isStandalone(const Cover& cover, int target)
{
    return cover.kind == CoverKind::Single && cover.lo == target;
}

MangaVolumePick fail(PickFailure why)
{
    MangaVolumePick p;
    p.failure = why;
    return p;
}

MangaVolumePick take(const Candidate& c)
{
    MangaVolumePick p;
    p.index = c.index;
    p.path = c.path;
    p.size = c.size;
    p.failure = PickFailure::None;
    return p;
}

} // namespace

MangaVolumePick pick(const QString& target, const QJsonArray& files)
{
    // Gather comic-archive candidates only, in engine order.
    QVector<Candidate> archives;
    archives.reserve(files.size());
    for (const QJsonValue& v : files) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        if (!isComicArchive(name))
            continue;
        archives.append(resolveCandidate(
            o.value(QStringLiteral("index")).toInt(),
            name,
            static_cast<qint64>(o.value(QStringLiteral("size")).toDouble())));
    }

    if (archives.isEmpty())
        return fail(PickFailure::NoArchive);

    bool okTarget = false;
    const int wanted = target.trimmed().toInt(&okTarget);
    if (!okTarget)
        return fail(PickFailure::TargetMissing); // non-numeric volume can't be isolated here

    // Tier 1: exact standalone volume declared in the FILE NAME.
    QVector<int> nameExact;
    for (int i = 0; i < archives.size(); ++i)
        if (archives[i].source == CoverSource::Filename && isStandalone(archives[i].cover, wanted))
            nameExact.append(i);
    if (nameExact.size() == 1)
        return take(archives[nameExact.front()]);
    if (nameExact.size() >= 2)
        return fail(PickFailure::Ambiguous); // equal candidates — needs another source

    // Tier 2: exact standalone volume named only by the parent DIRECTORY.
    QVector<int> dirExact;
    for (int i = 0; i < archives.size(); ++i)
        if (archives[i].source == CoverSource::Directory && isStandalone(archives[i].cover, wanted))
            dirExact.append(i);
    if (dirExact.size() == 1)
        return take(archives[dirExact.front()]);
    if (dirExact.size() >= 2)
        return fail(PickFailure::Ambiguous);

    // No isolable single volume. If an inclusive multi-volume archive covers the
    // target, it is a combined blob we cannot split; say so distinctly.
    for (const Candidate& c : archives)
        if (coversAsRange(c.cover, wanted))
            return fail(PickFailure::CombinedArchive);

    // Archives exist, but none of them is (or contains isolably) the target.
    return fail(PickFailure::TargetMissing);
}

QVector<int> unionPriorities(const QVector<int>& picks, int fileCount)
{
    QVector<int> priorities(fileCount, 0); // 0 = do-not-download
    for (const int idx : picks)
        if (idx >= 0 && idx < fileCount)
            priorities[idx] = 7; // 7 = max priority (libtorrent setFilePriorities)
    return priorities;
}

} // namespace MangaVolumeFilePicker
