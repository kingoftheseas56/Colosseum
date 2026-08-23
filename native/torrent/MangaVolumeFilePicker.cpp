#include "MangaVolumeFilePicker.h"

#include "torrent/MangaVolumeIdentity.h" // the ONE shared volume-identity grammar (Arc 18 M1)

#include <QFileInfo>
#include <QJsonObject>
#include <QSet>

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

// Runtime candidate: coverage parsed by the shared grammar, with the evidence
// source the tiers below rank on (Filename outranks Directory).
struct Candidate {
    int index = -1;
    QString path;
    qint64 size = 0;
    MangaVolumeIdentity::VolumeCoverage cover;
};

// Resolve a file's volume coverage through the shared path grammar: the base
// FILE NAME wins; only when it holds no explicit marker do we fall back to the
// parent directory segments (deepest first). That ordering lets
// "Series Vol 3/Series v02.cbz" resolve to v02.
Candidate resolveCandidate(int index, const QString& name, qint64 size)
{
    Candidate c;
    c.index = index;
    c.path = name;
    c.size = size;
    c.cover = MangaVolumeIdentity::coverageForPath(name);
    return c;
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

    // Tier 1: exact standalone volume declared in the FILE NAME. The shared
    // grammar compares decimal strings, so "10.5" and named volumes isolate
    // here exactly like integers — no int conversion of the target anywhere.
    QVector<int> nameExact;
    for (int i = 0; i < archives.size(); ++i)
        if (archives[i].cover.source == MangaVolumeIdentity::EvidenceSource::Filename
            && archives[i].cover.isSingle()
            && MangaVolumeIdentity::coversTarget(archives[i].cover, target))
            nameExact.append(i);
    if (nameExact.size() == 1)
        return take(archives[nameExact.front()]);
    if (nameExact.size() >= 2)
        return fail(PickFailure::Ambiguous); // equal candidates — needs another source

    // Tier 2: exact standalone volume named only by the parent DIRECTORY.
    QVector<int> dirExact;
    for (int i = 0; i < archives.size(); ++i)
        if (archives[i].cover.source == MangaVolumeIdentity::EvidenceSource::Directory
            && archives[i].cover.isSingle()
            && MangaVolumeIdentity::coversTarget(archives[i].cover, target))
            dirExact.append(i);
    if (dirExact.size() == 1)
        return take(archives[dirExact.front()]);
    if (dirExact.size() >= 2)
        return fail(PickFailure::Ambiguous);

    // No isolable single volume. If an inclusive multi-volume archive covers the
    // target, it is a combined blob we cannot split; say so distinctly.
    for (const Candidate& c : archives)
        if (c.cover.isRange() && MangaVolumeIdentity::coversTarget(c.cover, target))
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
