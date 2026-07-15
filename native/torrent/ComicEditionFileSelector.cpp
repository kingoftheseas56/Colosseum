#include "ComicEditionFileSelector.h"

#include "ComicCoverage.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace ComicEditionFileSelector {
namespace {

using ComicEditionIdentity::ComicCollectionFormat;
using ComicEditionIdentity::ComicEditionTarget;
using ComicEditionIdentity::ComicIssueRef;

QString normalized(QString value)
{
    value = value.toLower();
    value.replace(QRegularExpression(QStringLiteral("[._\\-]+")), QStringLiteral(" "));
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9 ]")), QString());
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return value.trimmed();
}

bool hasTraversal(const QString& path)
{
    const QStringList segments = path.split(QLatin1Char('/'));
    for (const QString& seg : segments)
        if (seg == QStringLiteral("..")) return true;
    return false;
}

QString baseName(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? path : path.mid(slash + 1);
}

QString parentDir(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

// The last path segment of a directory path ("A/B/C" -> "C"); empty stays empty.
QString lastSegment(const QString& dirPath)
{
    if (dirPath.isEmpty()) return QString();
    const int slash = dirPath.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? dirPath : dirPath.mid(slash + 1);
}

// Basename with its final extension stripped; punctuation is preserved so
// explicit issue markers ("#14") and coverage grammar survive intact.
QString stemOf(const QString& path)
{
    const QString base = baseName(path);
    const int dot = base.lastIndexOf(QLatin1Char('.'));
    return dot <= 0 ? base : base.left(dot);
}

QString extOf(const QString& path)
{
    const QString base = baseName(path);
    const int dot = base.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QString() : base.mid(dot + 1).toLower();
}

bool isComicArchiveExt(const QString& ext)
{
    return ext == QLatin1String("cbz") || ext == QLatin1String("cbr")
        || ext == QLatin1String("cb7") || ext == QLatin1String("cbt");
}

bool isImageExt(const QString& ext)
{
    return ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")
        || ext == QLatin1String("png") || ext == QLatin1String("gif")
        || ext == QLatin1String("webp") || ext == QLatin1String("avif");
}

ComicSelectedFile toSelected(const ManifestFile& f, int order)
{
    ComicSelectedFile out;
    out.index = f.index;
    out.path = f.path;
    out.bytes = f.bytes;
    out.order = order;
    return out;
}

QVariantList toCandidates(const QList<const ManifestFile*>& items)
{
    QVariantList out;
    for (const ManifestFile* f : items) {
        QVariantMap m;
        m.insert(QStringLiteral("index"), f->index);
        m.insert(QStringLiteral("path"), f->path);
        m.insert(QStringLiteral("bytes"), f->bytes);
        out.append(m);
    }
    return out;
}

bool hasExactSpanMatch(const QList<ComicCoverage::ComicCoverageSpan>& spans,
                        ComicCollectionFormat format, int ordinal)
{
    for (const auto& s : spans)
        if (s.format == format && s.lo == s.hi && s.lo == ordinal) return true;
    return false;
}

bool hasRangeSpanMatch(const QList<ComicCoverage::ComicCoverageSpan>& spans,
                        ComicCollectionFormat format, int ordinal)
{
    for (const auto& s : spans)
        if (s.format == format && s.lo < s.hi && s.lo <= ordinal && ordinal <= s.hi) return true;
    return false;
}

// Eligible (non-traversal, supported) comic archives from the manifest.
QList<const ManifestFile*> eligibleArchives(const QList<ManifestFile>& files)
{
    QList<const ManifestFile*> out;
    for (const ManifestFile& f : files) {
        if (hasTraversal(f.path)) continue;
        if (isComicArchiveExt(extOf(f.path))) out.append(&f);
    }
    return out;
}

// Tier 1: a unique archive whose full stem normalizes exactly to the
// edition's title.
bool tierExactTitle(const ComicEditionTarget& target, const QList<ManifestFile>& files,
                     ComicPayloadDecision& out)
{
    const QString wanted = normalized(target.editionTitle);
    if (wanted.isEmpty()) return false;

    QList<const ManifestFile*> matches;
    for (const ManifestFile* f : eligibleArchives(files))
        if (normalized(stemOf(f->path)) == wanted) matches.append(f);

    if (matches.size() == 1) {
        out = ComicPayloadDecision{};
        out.kind = ComicPayloadKind::SingleArchive;
        out.failure = ComicSelectionFailure::None;
        out.files.append(toSelected(*matches.first(), 0));
        return true;
    }
    if (matches.size() >= 2) {
        out = ComicPayloadDecision{};
        out.kind = ComicPayloadKind::None;
        out.failure = ComicSelectionFailure::Ambiguous;
        out.manualCandidates = toCandidates(matches);
        return true;
    }
    return false;
}

// Tier 2: a unique archive whose OWN filename advertises coverage of the
// target's format + ordinal. An exact single-ordinal span isolates the
// edition; a wider inclusive range covering the ordinal means the file is a
// combined multi-edition archive, never an isolated match.
bool tierFilenameCoverage(const ComicEditionTarget& target, const QList<ManifestFile>& files,
                           ComicPayloadDecision& out)
{
    if (target.format == ComicCollectionFormat::Unknown || target.ordinal < 0) return false;

    QList<const ManifestFile*> exactMatches;
    QList<const ManifestFile*> rangeMatches;
    for (const ManifestFile* f : eligibleArchives(files)) {
        const auto spans = ComicCoverage::detectComicCoverage(stemOf(f->path));
        if (hasExactSpanMatch(spans, target.format, target.ordinal)) exactMatches.append(f);
        else if (hasRangeSpanMatch(spans, target.format, target.ordinal)) rangeMatches.append(f);
    }

    if (exactMatches.size() == 1) {
        out = ComicPayloadDecision{};
        out.kind = ComicPayloadKind::SingleArchive;
        out.failure = ComicSelectionFailure::None;
        out.files.append(toSelected(*exactMatches.first(), 0));
        return true;
    }
    if (exactMatches.size() >= 2) {
        out = ComicPayloadDecision{};
        out.kind = ComicPayloadKind::None;
        out.failure = ComicSelectionFailure::Ambiguous;
        out.manualCandidates = toCandidates(exactMatches);
        return true;
    }
    if (rangeMatches.size() == 1) {
        out = ComicPayloadDecision{};
        out.kind = ComicPayloadKind::CombinedWholeArchive;
        out.failure = ComicSelectionFailure::CombinedOnly;
        out.files.append(toSelected(*rangeMatches.first(), 0));
        return true;
    }
    if (rangeMatches.size() >= 2) {
        out = ComicPayloadDecision{};
        out.kind = ComicPayloadKind::None;
        out.failure = ComicSelectionFailure::Ambiguous;
        out.manualCandidates = toCandidates(rangeMatches);
        return true;
    }
    return false;
}

// Tier 3: a unique deepest parent directory whose own name advertises exact
// coverage of the target. Selects every supported file in that subtree.
bool tierDirectoryCoverage(const ComicEditionTarget& target, const QList<ManifestFile>& files,
                            ComicPayloadDecision& out)
{
    if (target.format == ComicCollectionFormat::Unknown || target.ordinal < 0) return false;

    QList<const ManifestFile*> payload;
    for (const ManifestFile& f : files) {
        if (hasTraversal(f.path)) continue;
        const QString ext = extOf(f.path);
        if (isComicArchiveExt(ext) || isImageExt(ext)) payload.append(&f);
    }
    if (payload.isEmpty()) return false;

    QString matchedDir;
    bool matchedDirFound = false;
    bool ambiguousDirs = false;
    for (const ManifestFile* f : payload) {
        QString dir = parentDir(f->path);
        while (!dir.isEmpty()) {
            const auto spans = ComicCoverage::detectComicCoverage(lastSegment(dir));
            if (hasExactSpanMatch(spans, target.format, target.ordinal)) {
                if (!matchedDirFound) { matchedDir = dir; matchedDirFound = true; }
                else if (matchedDir != dir) { ambiguousDirs = true; }
                break;
            }
            dir = parentDir(dir);
        }
    }
    if (!matchedDirFound || ambiguousDirs) return false;

    // Sibling-ordinal guard: a directory sitting next to the matched one that
    // itself advertises a DIFFERENT ordinal for the same format makes the
    // subtree unsafe to auto-select (e.g. per-edition sibling folders).
    const QString parent = parentDir(matchedDir);
    QSet<QString> siblingDirs;
    for (const ManifestFile* f : payload) {
        const QString dir = parentDir(f->path);
        if (dir.isEmpty() || dir == matchedDir) continue;
        if (parentDir(dir) == parent) siblingDirs.insert(dir);
    }
    for (const QString& dir : siblingDirs) {
        const auto spans = ComicCoverage::detectComicCoverage(lastSegment(dir));
        for (const auto& s : spans)
            if (s.format == target.format && s.lo == s.hi && s.lo != target.ordinal)
                return false;
    }

    const QString prefix = matchedDir + QLatin1Char('/');
    QList<const ManifestFile*> archives;
    QList<const ManifestFile*> images;
    for (const ManifestFile* f : payload) {
        if (!f->path.startsWith(prefix)) continue;
        const QString ext = extOf(f->path);
        if (isComicArchiveExt(ext)) archives.append(f);
        else if (isImageExt(ext)) images.append(f);
    }

    const auto byPath = [](const ManifestFile* a, const ManifestFile* b) { return a->path < b->path; };

    if (!archives.isEmpty()) {
        std::sort(archives.begin(), archives.end(), byPath);
        out = ComicPayloadDecision{};
        if (archives.size() == 1) {
            out.kind = ComicPayloadKind::SingleArchive;
            out.failure = ComicSelectionFailure::None;
        } else {
            out.kind = ComicPayloadKind::CombinedWholeArchive;
            out.failure = ComicSelectionFailure::CombinedOnly;
        }
        for (int i = 0; i < archives.size(); ++i)
            out.files.append(toSelected(*archives[i], i));
        return true;
    }

    if (!images.isEmpty()) {
        std::sort(images.begin(), images.end(), byPath);
        out = ComicPayloadDecision{};
        out.kind = ComicPayloadKind::LooseImageSubtree;
        out.failure = ComicSelectionFailure::None;
        for (int i = 0; i < images.size(); ++i)
            out.files.append(toSelected(*images[i], i));
        return true;
    }

    return false;
}

// A loose issue archive needs strong series agreement (its stem contains the
// canonical series name, OR it lives directly inside a directory whose own
// name is EXACTLY the canonical series) AND an explicit issue marker: "#14",
// "Issue 14", or a bare zero-padded numeral like "0014".
bool fileMatchesIssue(const ManifestFile& f, const ComicIssueRef& ref)
{
    const QString rawStem = stemOf(f.path);
    const QString normStem = normalized(rawStem);
    const QString normSeries = normalized(ref.series);
    const QString normDir = normalized(lastSegment(parentDir(f.path)));

    const bool seriesAgrees = !normSeries.isEmpty()
        && (normStem.contains(normSeries) || normDir == normSeries);
    if (!seriesAgrees) return false;

    static const QRegularExpression markerRe(
        QStringLiteral("(?:#\\s*|\\bissue[\\s._-]*)0*(\\d{1,4})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (const auto m = markerRe.match(rawStem); m.hasMatch())
        return m.captured(1).toInt() == ref.number;

    static const QRegularExpression zeroPadRe(QStringLiteral("(?<![0-9])0(\\d{1,3})(?![0-9])"));
    if (const auto m = zeroPadRe.match(rawStem); m.hasMatch())
        return (QStringLiteral("0") + m.captured(1)).toInt() == ref.number;

    return false;
}

// Tier 4: assemble the edition's exact collected-issue set from loose issue
// archives. Requires collectedIssuesComplete AND every required issue
// present; a single missing issue selects nothing automatically.
bool tierIssueSet(const ComicEditionTarget& target, const QList<ManifestFile>& files,
                   ComicPayloadDecision& out)
{
    if (!target.collectedIssuesComplete || target.collectedIssues.isEmpty()) return false;

    const QList<const ManifestFile*> archives = eligibleArchives(files);

    QList<ComicSelectedFile> selected;
    QStringList missing;
    QSet<int> used;

    for (int i = 0; i < target.collectedIssues.size(); ++i) {
        const ComicIssueRef& ref = target.collectedIssues[i];
        const ManifestFile* found = nullptr;
        for (const ManifestFile* f : archives) {
            if (used.contains(f->index)) continue;
            if (fileMatchesIssue(*f, ref)) { found = f; break; }
        }
        if (found) {
            selected.append(toSelected(*found, i));
            used.insert(found->index);
        } else {
            missing << (ref.series + QStringLiteral(" #") + QString::number(ref.number));
        }
    }

    out = ComicPayloadDecision{};
    if (!missing.isEmpty()) {
        out.kind = ComicPayloadKind::None;
        out.failure = ComicSelectionFailure::IncompleteIssueSet;
        out.missingIssues = missing;
        return true;
    }

    out.kind = ComicPayloadKind::IssueArchiveSet;
    out.failure = ComicSelectionFailure::None;
    out.files = selected;
    return true;
}

} // namespace

ComicPayloadDecision select(const ComicEditionTarget& target, const QList<ManifestFile>& files)
{
    ComicPayloadDecision out;

    if (tierExactTitle(target, files, out)) return out;

    if (!target.formatAmbiguous) {
        if (tierFilenameCoverage(target, files, out)) return out;
        if (tierDirectoryCoverage(target, files, out)) return out;
    }

    if (tierIssueSet(target, files, out)) return out;

    out = ComicPayloadDecision{};
    out.kind = ComicPayloadKind::None;
    out.failure = ComicSelectionFailure::TargetMissing;
    return out;
}

QVector<int> unionPriorities(const QList<ComicPayloadDecision>& decisions, int fileCount)
{
    QVector<int> priorities(fileCount, 0);
    for (const ComicPayloadDecision& d : decisions)
        for (const ComicSelectedFile& f : d.files)
            if (f.index >= 0 && f.index < fileCount) priorities[f.index] = 7;
    return priorities;
}

} // namespace ComicEditionFileSelector
