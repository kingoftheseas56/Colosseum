#include "VaultKit.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace VaultKit {

// ── Ignore layer 1: hard-coded system/junk dirs (ScannerUtils port) ───
static const QStringList kIgnoreDirs = {
    "__macosx", "node_modules", ".git", ".svn", ".hg",
    "@eadir", "$recycle.bin", "system volume information"
};

bool isIgnoredDir(const QString& dirName)
{
    if (dirName.startsWith('.'))
        return true;
    return kIgnoreDirs.contains(dirName.toLower());
}

// ── Ignore layer 2: user scanIgnore needles (Groundworks contract) ────
QStringList sanitizeIgnoreNeedles(const QStringList& needles)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString& raw : needles) {
        const QString s = raw.trimmed();
        if (s.isEmpty())
            continue;
        const QString key = s.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.append(s);
        if (out.size() >= 200) // Groundworks cap
            break;
    }
    return out;
}

bool pathHitsNeedle(const QString& path, const QStringList& needles)
{
    if (needles.isEmpty())
        return false;
    const QString hay = path.toLower();
    for (const QString& n : needles) {
        if (hay.contains(n.toLower()))
            return true;
    }
    return false;
}

// ── Subdirectory listing ──────────────────────────────────────────────
QStringList listImmediateSubdirs(const QString& rootPath, const QStringList& needles)
{
    QStringList result;
    QDir root(rootPath);
    if (!root.exists())
        return result;

    const auto entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        if (isIgnoredDir(entry.fileName()))
            continue;
        if (pathHitsNeedle(entry.absoluteFilePath(), needles))
            continue;
        result.append(entry.absoluteFilePath());
    }
    return result;
}

// ── Recursive file walk (depth-bounded + symlink-loop-safe) ───────────
static void walkFilesRecursive(const QString& dirPath,
                               const QStringList& nameFilters,
                               QStringList& out,
                               const CancellationToken* cancel,
                               const QStringList& needles,
                               int depth,
                               QSet<QString>& seen)
{
    if (cancel && cancel->isCancelled())
        return;
    if (depth > kMaxWalkDepth)
        return;
    if (pathHitsNeedle(dirPath, needles))
        return;

    QFileInfo info(dirPath);
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || seen.contains(canonical))
        return;
    seen.insert(canonical);

    QDir dir(dirPath);

    const auto files = dir.entryInfoList(nameFilters, QDir::Files);
    for (const auto& f : files) {
        if (cancel && cancel->isCancelled())
            return;
        if (pathHitsNeedle(f.absoluteFilePath(), needles))
            continue;
        out.append(f.absoluteFilePath());
    }

    const auto subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& sub : subdirs) {
        if (cancel && cancel->isCancelled())
            return;
        if (isIgnoredDir(sub.fileName()))
            continue;
        if (pathHitsNeedle(sub.absoluteFilePath(), needles))
            continue;
        walkFilesRecursive(sub.absoluteFilePath(), nameFilters, out, cancel,
                           needles, depth + 1, seen);
    }
}

QStringList walkFiles(const QString& dirPath, const QStringList& nameFilters,
                      const CancellationToken* cancel, const QStringList& needles)
{
    QStringList result;
    QSet<QString> seen;
    walkFilesRecursive(dirPath, nameFilters, result, cancel, needles, 0, seen);
    return result;
}

// ── Group by first-level subdirectory + loose capture ─────────────────
QMap<QString, QStringList> groupByFirstLevelSubdir(
    const QStringList& rootFolders, const QStringList& nameFilters,
    const CancellationToken* cancel, const QStringList& needles)
{
    QMap<QString, QStringList> result;

    for (const auto& root : rootFolders) {
        if (cancel && cancel->isCancelled())
            return result;

        // 1. Each immediate subdirectory becomes a group.
        const QStringList subdirs = listImmediateSubdirs(root, needles);
        for (const auto& subdir : subdirs) {
            if (cancel && cancel->isCancelled())
                return result;
            QStringList files = walkFiles(subdir, nameFilters, cancel, needles);
            if (!files.isEmpty())
                result[subdir] = files;
        }

        // 2. Loose files directly in the root → "<root>::LOOSE" (spec §5).
        QDir rootDir(root);
        const auto looseFiles = rootDir.entryInfoList(nameFilters, QDir::Files);
        if (!looseFiles.isEmpty()) {
            const QString looseKey = root + QStringLiteral("::LOOSE");
            for (const auto& f : looseFiles) {
                if (pathHitsNeedle(f.absoluteFilePath(), needles))
                    continue;
                result[looseKey].append(f.absoluteFilePath());
            }
            if (result.contains(looseKey) && result[looseKey].isEmpty())
                result.remove(looseKey);
        }
    }

    return result;
}

// ── Title cleaner (port of TB2 ScannerUtils::cleanMediaFolderTitle) ────
static const QRegularExpression kShowTitleNoiseRe(
    QStringLiteral(
        "(?i)\\b("
        "2160p|1080p|720p|480p|x264|x265|h\\.?264|h\\.?265|hevc|10bit|8bit|hdr|dv|"
        "webrip|web[\\s.\\-]?dl|bluray|bdrip|dvdrip|hdtv|remux|aac|dts|ddp\\d?|"
        "proper|repack|extended|unrated|multi|dual[\\s.\\-]?audio|dubbed|subbed|"
        "subs?|nf|amzn|hulu|atvp|max|uhd|complete|batch|season[\\s._\\-]*\\d{1,2}|s\\d{1,2}"
        ")\\b"));

static const QRegularExpression kSeasonTokenRe(
    QStringLiteral("(?i)\\b(?:season[\\s._\\-]*|s)(\\d{1,2})\\b"));

static const QRegularExpression kBracketChunkRe(
    QStringLiteral("\\[([^\\]]*)\\]|\\(([^\\)]*)\\)|\\{([^\\}]*)\\}"));

static QList<int> extractSeasonNumbers(const QString& raw)
{
    QList<int> out;
    QSet<int> seen;
    auto it = kSeasonTokenRe.globalMatch(raw);
    while (it.hasNext()) {
        auto match = it.next();
        bool ok = false;
        int number = match.captured(1).toInt(&ok);
        if (!ok || number <= 0 || number > 99 || seen.contains(number))
            continue;
        seen.insert(number);
        out.append(number);
    }
    return out;
}

static QString stripNoiseBracketChunks(const QString& text)
{
    QString result;
    int lastEnd = 0;
    auto it = kBracketChunkRe.globalMatch(text);

    while (it.hasNext()) {
        auto match = it.next();
        result += text.mid(lastEnd, match.capturedStart() - lastEnd);

        QString inner;
        for (int g = 1; g <= 3; ++g) {
            if (!match.captured(g).isNull()) {
                inner = match.captured(g).trimmed();
                break;
            }
        }

        if (inner.isEmpty()) {
            result += ' ';
        } else {
            QString normalized = inner;
            normalized.replace('_', ' ');
            normalized.replace('.', ' ');
            normalized = normalized.trimmed();

            if (normalized.isEmpty()) {
                result += ' ';
            } else {
                static const QRegularExpression yearOnly(QStringLiteral("^(?:19|20)\\d{2}$"));
                static const QRegularExpression pureNumeric(QStringLiteral("^\\d+$"));
                if (yearOnly.match(normalized).hasMatch() ||
                    pureNumeric.match(normalized).hasMatch() ||
                    kShowTitleNoiseRe.match(normalized).hasMatch()) {
                    result += ' ';
                } else {
                    result += ' ' + normalized + ' ';
                }
            }
        }

        lastEnd = match.capturedEnd();
    }

    result += text.mid(lastEnd);
    return result;
}

QString cleanMediaFolderTitle(const QString& rawName)
{
    QString raw = rawName.trimmed();
    if (raw.isEmpty())
        return raw;

    const QList<int> seasonNumbers = extractSeasonNumbers(raw);

    QString cleaned = raw;
    cleaned.replace('_', ' ');
    cleaned.replace('.', ' ');

    cleaned = stripNoiseBracketChunks(cleaned);
    cleaned.replace(kShowTitleNoiseRe, QStringLiteral(" "));

    // Orphan "-N" / "+N" tokens left by season/noise removal.
    static const QRegularExpression strayNumToken(
        QStringLiteral("(?:^|\\s)[\\-+]\\s*\\d{1,2}(?=\\s|$)"));
    cleaned.replace(strayNumToken, QStringLiteral(" "));

    // Orphan "+" separators (not "-", which divides real title parts).
    static const QRegularExpression strayPlus(
        QStringLiteral("(?:^|\\s)\\+(?=\\s|$)"));
    cleaned.replace(strayPlus, QStringLiteral(" "));

    static const QRegularExpression trailingGroup(
        QStringLiteral("(?:\\s*-\\s*[A-Za-z0-9]{2,16})+$"));
    cleaned.replace(trailingGroup, QStringLiteral(" "));

    static const QRegularExpression trailingYear(
        QStringLiteral("\\b(?:19|20)\\d{2}\\b$"));
    cleaned.replace(trailingYear, QStringLiteral(" "));

    static const QRegularExpression multiSpace(QStringLiteral("\\s+"));
    cleaned.replace(multiSpace, QStringLiteral(" "));
    static const QRegularExpression trimChars(QStringLiteral("^[\\s\\-._]+|[\\s\\-._]+$"));
    cleaned.replace(trimChars, QString());

    if (!seasonNumbers.isEmpty()) {
        const QString lower = cleaned.toLower();
        QStringList missing;
        for (int n : seasonNumbers) {
            const QString label = QStringLiteral("Season %1").arg(n);
            if (!lower.contains(label.toLower()))
                missing.append(label);
        }
        if (!cleaned.isEmpty() && !missing.isEmpty())
            cleaned += ' ' + missing.join(' ');
        else if (cleaned.isEmpty())
            cleaned = missing.join(' ');
    }

    cleaned.replace(multiSpace, QStringLiteral(" "));
    cleaned.replace(trimChars, QString());

    if (cleaned.length() < 2)
        return raw;

    return cleaned;
}

// ── Media kinds + classifier ──────────────────────────────────────────
static const QSet<QString> kComicExts  = { "cbz", "cbr" };
static const QSet<QString> kBookExts   = { "epub", "pdf", "mobi", "fb2", "azw3", "djvu", "txt" };
static const QSet<QString> kVideoExts  = { "mp4", "mkv", "avi", "webm", "mov", "wmv",
                                           "flv", "m4v", "ts", "mpg", "mpeg", "ogv" };

static QStringList filtersFor(const QSet<QString>& exts)
{
    QStringList out;
    for (const QString& e : exts)
        out.append(QStringLiteral("*.") + e);
    out.sort(); // stable order for callers/tests
    return out;
}

const QStringList& comicFilters() { static const QStringList f = filtersFor(kComicExts); return f; }
const QStringList& bookFilters()  { static const QStringList f = filtersFor(kBookExts);  return f; }
const QStringList& videoFilters() { static const QStringList f = filtersFor(kVideoExts); return f; }

QStringList allMediaFilters()
{
    QStringList out = comicFilters();
    out += bookFilters();
    out += videoFilters();
    return out;
}

QString kindName(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Comic: return QStringLiteral("comic");
    case MediaKind::Book:  return QStringLiteral("book");
    case MediaKind::Video: return QStringLiteral("video");
    case MediaKind::Unknown: break;
    }
    return QStringLiteral("unknown");
}

MediaKind kindForFile(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (kComicExts.contains(ext)) return MediaKind::Comic;
    if (kBookExts.contains(ext))  return MediaKind::Book;
    if (kVideoExts.contains(ext)) return MediaKind::Video;
    return MediaKind::Unknown;
}

LeafClassification classifyLeaf(const QStringList& files)
{
    LeafClassification r;
    // Tie order is stable and deliberate: Comic before Book before Video.
    static const MediaKind order[] = { MediaKind::Comic, MediaKind::Book, MediaKind::Video };

    for (const QString& f : files) {
        const MediaKind k = kindForFile(f);
        if (k == MediaKind::Unknown)
            continue;
        r.counts[k] += 1;
    }

    int best = 0;
    for (MediaKind k : order) {
        const int c = r.counts.value(k, 0);
        if (c > best) {
            best = c;
            r.dominant = k;
        }
    }

    int kindsPresent = 0;
    for (MediaKind k : order)
        if (r.counts.value(k, 0) > 0)
            ++kindsPresent;
    r.mixed = kindsPresent > 1;

    if (r.dominant != MediaKind::Unknown) {
        for (const QString& f : files) {
            const MediaKind k = kindForFile(f);
            if (k != MediaKind::Unknown && k != r.dominant)
                r.leftovers.append(f);
        }
    }
    return r;
}

static QStringList sampleTitlesFor(const QStringList& files, bool loose,
                                   const QString& subtreePath)
{
    // Non-loose: the group IS one series/show folder — its cleaned name is the
    // sample. Loose: up to 3 cleaned file basenames.
    if (!loose) {
        const QString raw = QFileInfo(subtreePath).fileName();
        const QString t = cleanMediaFolderTitle(raw);
        return { t.isEmpty() ? raw : t };
    }
    QStringList out;
    QSet<QString> seen;
    for (const QString& f : files) {
        const QString raw = QFileInfo(f).completeBaseName();
        QString t = cleanMediaFolderTitle(raw);
        if (t.isEmpty())
            t = raw;
        if (seen.contains(t.toLower()))
            continue;
        seen.insert(t.toLower());
        out.append(t);
        if (out.size() >= 3)
            break;
    }
    return out;
}

QList<CensusSlice> census(const QString& root, const QStringList& scanIgnore,
                          const CancellationToken* cancel)
{
    const QStringList needles = sanitizeIgnoreNeedles(scanIgnore);
    const QStringList filters = allMediaFilters();

    QList<CensusSlice> slices;
    const auto groups = groupByFirstLevelSubdir({root}, filters, cancel, needles);
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (cancel && cancel->isCancelled())
            return slices;

        const QString key = it.key();
        const bool loose = key.endsWith(QStringLiteral("::LOOSE"));
        const QString subtree = loose ? root : key;

        const LeafClassification c = classifyLeaf(it.value());
        if (c.dominant == MediaKind::Unknown)
            continue; // no readable media (shouldn't happen: filters are media-only)

        CensusSlice s;
        s.subtreePath = subtree;
        s.loose = loose;
        s.kind = c.dominant;
        s.count = c.counts.value(c.dominant, 0);
        s.mixed = c.mixed;
        s.leftovers = c.leftovers;
        s.sampleTitles = sampleTitlesFor(it.value(), loose, subtree);
        slices.append(s);
    }
    return slices;
}

// ── Season / episode grammar ──────────────────────────────────────────
SeasonEpisode parseSeasonEpisode(const QString& fileName)
{
    SeasonEpisode r;
    static const QRegularExpression re(
        QStringLiteral("[._\\s]?[Ss](\\d{1,2})[._\\s]?[Ee](\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QString base = QFileInfo(fileName).completeBaseName();
    const auto m = re.match(base);
    if (!m.hasMatch())
        return r;
    r.matched = true;
    r.season = m.captured(1).toInt();
    r.episode = m.captured(2).toInt();
    return r;
}

bool isSeasonLikeDirName(const QString& dirName)
{
    static const QRegularExpression re(
        QStringLiteral("^(season\\s*\\d+|s\\d{1,3}|disc\\s*\\d+|volume\\s*\\d+"
                       "|vol\\s*\\d+|part\\s*\\d+|cd\\s*\\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(dirName.trimmed()).hasMatch();
}

QString showRootForEpisodePath(const QString& filePath)
{
    const QString p = QDir::fromNativeSeparators(filePath);
    QDir parent = QFileInfo(p).absoluteDir();
    while (isSeasonLikeDirName(parent.dirName())) {
        if (!parent.cdUp())
            break;
    }
    return parent.absolutePath();
}

} // namespace VaultKit
