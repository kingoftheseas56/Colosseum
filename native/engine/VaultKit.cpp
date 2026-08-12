#include "VaultKit.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

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
    const CancellationToken* cancel, const QStringList& needles,
    const std::function<void(int, int, const QString&)>& onProgress)
{
    QMap<QString, QStringList> result;

    for (const auto& root : rootFolders) {
        if (cancel && cancel->isCancelled())
            return result;

        // 1. Each immediate subdirectory becomes a group.
        const QStringList subdirs = listImmediateSubdirs(root, needles);
        for (int i = 0; i < subdirs.size(); ++i) {
            if (cancel && cancel->isCancelled())
                return result;
            const QString& subdir = subdirs.at(i);
            // Live pill signal: (done, total, folder) as each first-level subtree begins
            // its walk — the walk is the slow part, so report before it, not after.
            if (onProgress)
                onProgress(i, subdirs.size(), QFileInfo(subdir).fileName());
            QStringList files = walkFiles(subdir, nameFilters, cancel, needles);
            if (!files.isEmpty())
                result[subdir] = files;
        }
        if (onProgress && !subdirs.isEmpty())
            onProgress(subdirs.size(), subdirs.size(), QString());

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

// Audio-channel release tags ("5.1", "7.1", "2.0", "DDP5.1", "TrueHD 7.1") — noise that is only
// recognizable as such from its OWN dot ("5.1"); once the outer cleaner's blanket '.'->' ' pass
// has already run, "5 1" is an unrecognizable two-token fragment (fails "pure numeric", matches
// no noise word) and slips through as stray title text. Tested against the RAW bracket content,
// before any dot/underscore normalization.
static const QRegularExpression kAudioChannelRawRe(
    QStringLiteral("(?i)^(?:ddp|dd\\+|dd|ac-?3|eac3|truehd|dts-?hd(?:\\s*ma)?|dts|atmos)?"
                   "\\s*\\d(?:\\.\\d){1,2}$"));

// Release-site / group domain tags ("YTS.MX", "RARBG.to") — a single dotted word pair with no
// internal spaces, the scene-tag convention for a tracker/site credit. Same raw-text reasoning
// as the audio-channel tag above: the dot is the only signal, and it survives only in the
// pre-normalization form.
static const QRegularExpression kDomainTagRawRe(
    QStringLiteral("^[A-Za-z0-9][A-Za-z0-9-]{1,30}\\.[A-Za-z]{2,6}$"));

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
                // Audio-channel and domain-tag noise is tested against `inner` — the RAW,
                // pre-normalization capture — because both patterns hinge on a literal dot that
                // `normalized` has already replaced with a space.
                if (yearOnly.match(normalized).hasMatch() ||
                    pureNumeric.match(normalized).hasMatch() ||
                    kShowTitleNoiseRe.match(normalized).hasMatch() ||
                    kAudioChannelRawRe.match(inner).hasMatch() ||
                    kDomainTagRawRe.match(inner).hasMatch()) {
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

    // Strip bracket/paren/brace noise chunks FIRST, while dots/underscores are still intact —
    // an audio-channel tag ("[5.1]") or a release-site domain tag ("[YTS.MX]") is recognizable
    // as noise only from its own punctuation; the blanket '.'/'_' -> ' ' pass below would
    // otherwise turn "5.1" into the unrecognizable "5 1" and "YTS.MX" into "YTS MX" BEFORE the
    // noise test ever saw them (the exact bug behind a real folder's stray "5 1"/"YTS MX").
    QString cleaned = stripNoiseBracketChunks(raw);

    cleaned.replace('_', ' ');
    cleaned.replace('.', ' ');

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

    // Re-append a season label ONLY for a SINGLE-season folder, so a lone "Season 1"
    // folder keeps its identity after the noise strip removed the token. A folder that
    // NAMES MULTIPLE seasons is a show root spanning them (e.g. "The Wire S01 S05") —
    // the bare show name is the right title; re-appending every number produced the
    // "The Wire Season 1 Season 5" doubling artifact (Slice 11 Thread C).
    if (seasonNumbers.size() == 1) {
        const QString label = QStringLiteral("Season %1").arg(seasonNumbers.first());
        if (cleaned.isEmpty())
            cleaned = label;
        else if (!cleaned.toLower().contains(label.toLower()))
            cleaned += ' ' + label;
    }

    cleaned.replace(multiSpace, QStringLiteral(" "));
    cleaned.replace(trimChars, QString());

    if (cleaned.length() < 2)
        return raw;

    return cleaned;
}

QString normalizedTitle(const QString& rawTitle)
{
    QString s = cleanMediaFolderTitle(rawTitle).toLower();
    s.replace(QRegularExpression(QStringLiteral("['’]s")), QString());
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    s = s.simplified();
    s.replace(QRegularExpression(QStringLiteral("^(the|a|an) ")), QString());
    return s;
}

// ── Media kinds + classifier ──────────────────────────────────────────
static const QSet<QString> kComicExts  = { "cbz", "cbr" };
// NB: .txt is deliberately NOT a book format. In a media library a loose .txt is release junk
// (YIFY status files, readmes, notes), not an ebook — and counting it as a book let a single junk
// .txt tie with the real media and, via the Comic>Book>Video tie-break, shelve whole movie folders
// under Books (e.g. a Spider-Man .mp4 + YIFYStatus.com.txt filed as a book). Nobody packages books
// as .txt; treat it as ignorable like .srt/.jpg. (2026-08-09)
static const QSet<QString> kBookExts   = { "epub", "pdf", "mobi", "fb2", "azw3", "djvu" };
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

MediaKind kindFromName(const QString& name)
{
    if (name == QStringLiteral("comic")) return MediaKind::Comic;
    if (name == QStringLiteral("book"))  return MediaKind::Book;
    if (name == QStringLiteral("video")) return MediaKind::Video;
    return MediaKind::Unknown;
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

SeasonEpisode parseAbsoluteEpisode(const QString& fileName)
{
    SeasonEpisode r;
    // A spaced "- NNN" token, 2-3 digits: the fansub absolute-numbering convention (e.g.
    // "Gintama - 003 [...]"). Digits are bounded to 2-3 so a 4-digit year ("- 2021") never
    // false-positives, and the hyphen must carry surrounding whitespace so a bare release-tag
    // range like "1-5" or "S01-S05" (no spaces around '-') never matches either.
    static const QRegularExpression re(QStringLiteral("\\s-\\s(\\d{2,3})(?=\\s|$|\\[|\\()"));
    const QString base = QFileInfo(fileName).completeBaseName();
    const auto m = re.match(base);
    if (!m.hasMatch())
        return r;
    r.matched = true;
    r.absolute = true;
    r.episode = m.captured(1).toInt();
    return r;
}

SeasonEpisode parseEpisodeNumber(const QString& fileName)
{
    const SeasonEpisode explicitMatch = parseSeasonEpisode(fileName);
    if (explicitMatch.matched)
        return explicitMatch; // an explicit SxxExx marking always wins
    return parseAbsoluteEpisode(fileName);
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

// ── Browse-collapse planner (Vault browse-face execution plan, Slice 1) ─────────────────────
QString browseNodeTypeName(BrowseNodeType type)
{
    switch (type) {
    case BrowseNodeType::Folder:  return QStringLiteral("folder");
    case BrowseNodeType::Show:    return QStringLiteral("show");
    case BrowseNodeType::Season:  return QStringLiteral("season");
    case BrowseNodeType::Film:    return QStringLiteral("film");
    case BrowseNodeType::Episode: return QStringLiteral("episode");
    case BrowseNodeType::Clip:    return QStringLiteral("clip");
    }
    return QStringLiteral("folder");
}

static bool isExtrasDirName(const QString& name)
{
    const QString n = name.trimmed().toLower();
    return n == QLatin1String("extras") || n == QLatin1String("featurettes");
}

// The ordinal from a bare season-like dir name (isSeasonLikeDirName already true), e.g.
// "Season 4" -> 4, "S04" -> 4, "Disc 2" -> 2. 0 if no digits (shouldn't happen for a name that
// already passed the guard).
static int seasonOrdinalFromDirName(const QString& dirName)
{
    static const QRegularExpression re(QStringLiteral("(\\d+)"));
    const auto m = re.match(dirName);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

// A folder's own name may CLAIM a season span ("Season 1-5"/"S01-S05", the Wire shape) or a
// single season ("Loki ... Season 1 S01 ...", one Loki sibling). hasClaim=false means the name
// says nothing about seasons at all.
struct SeasonClaim { bool hasClaim = false; int from = 0; int to = 0; };

static SeasonClaim seasonClaimFromFolderName(const QString& rawName)
{
    SeasonClaim c;
    // A literal range token beats individual-number extraction, so "Season 1-5" is read as the
    // SPAN 1..5, not the pair {1,5} — the Wire folder's fact hinges on this.
    static const QRegularExpression rangeRe(QStringLiteral(
        "(?i)(?:season\\s*|s)(\\d{1,3})\\s*-\\s*(?:season\\s*|s)?(\\d{1,3})\\b"));
    const auto rm = rangeRe.match(rawName);
    if (rm.hasMatch()) {
        c.hasClaim = true;
        c.from = rm.captured(1).toInt();
        c.to = rm.captured(2).toInt();
        if (c.from > c.to)
            std::swap(c.from, c.to);
        return c;
    }
    const QList<int> singles = extractSeasonNumbers(rawName); // existing title-cleaner helper
    if (singles.size() == 1) {
        c.hasClaim = true;
        c.from = c.to = singles.first();
    }
    return c;
}

// Strip a trailing " Season N" label the way cleanMediaFolderTitle appends it for a
// single-season folder, so sibling folders differing only by season number ("Loki Season 1" /
// "Loki Season 2") fold to the same base title ("Loki") for the sibling-merge pass.
static QString stripTrailingSeasonLabel(const QString& cleanedTitle)
{
    static const QRegularExpression re(QStringLiteral("(?i)\\s+Season\\s+\\d{1,3}$"));
    QString out = cleanedTitle;
    out.remove(re);
    return out.trimmed();
}

static QStringList videoFilesDirectlyIn(const QString& dirPath, const QStringList& needles)
{
    QStringList out;
    QDir dir(dirPath);
    const auto files = dir.entryInfoList(videoFilters(), QDir::Files);
    for (const auto& f : files) {
        if (pathHitsNeedle(f.absoluteFilePath(), needles))
            continue;
        out.append(f.absoluteFilePath());
    }
    return out;
}

// One classified child directory, before the sibling-merge pass decides whether it stands
// alone or folds into a show alongside its siblings.
struct ChildClassification {
    BrowseNode node;
    // True when this folder's OWN name claims exactly one season AND its content is a complete
    // run of episode-shaped video files — "one season of a (possibly multi-season) show", the
    // shape the sibling-fold pass looks for (the Loki shape). A lone one-season folder with no
    // matching sibling simply stays a show of its own (see planBrowseLevel).
    bool siblingCandidate = false;
    int siblingSeasonNumber = 0;
};

static ChildClassification classifyChildDirectory(const QString& childPath,
                                                   const QStringList& needles)
{
    ChildClassification cc;
    const QString rawName = QFileInfo(childPath).fileName();
    cc.node.path = childPath;
    cc.node.key = childPath;

    // This folder's immediate bare-season subdirectories (extras/featurettes folded out here,
    // never counted, never a node — the plan's extras-folding requirement).
    QStringList seasonSubdirs;
    QDir dir(childPath);
    const auto subEntries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& e : subEntries) {
        if (isIgnoredDir(e.fileName()))
            continue;
        if (pathHitsNeedle(e.absoluteFilePath(), needles))
            continue;
        if (isExtrasDirName(e.fileName()))
            continue; // folded: never counted, never a node
        if (isSeasonLikeDirName(e.fileName()))
            seasonSubdirs.append(e.absoluteFilePath());
        // Any other subdirectory (e.g. "Subs") is simply never visited — companion folding is
        // automatic because nothing ever recurses into it.
    }

    QDir mdir(childPath);
    const auto looseMedia = mdir.entryInfoList(allMediaFilters(), QDir::Files);
    QStringList looseMediaPaths;
    for (const auto& f : looseMedia) {
        if (pathHitsNeedle(f.absoluteFilePath(), needles))
            continue;
        looseMediaPaths.append(f.absoluteFilePath());
    }

    if (!seasonSubdirs.isEmpty()) {
        // Nested-season show (the Wire shape): the folder's own name claims a season span, but
        // disk holds only some of the seasons it claims.
        QList<int> held;
        for (const QString& sp : seasonSubdirs)
            held.append(seasonOrdinalFromDirName(QFileInfo(sp).fileName()));
        std::sort(held.begin(), held.end());
        const SeasonClaim claim = seasonClaimFromFolderName(rawName);

        cc.node.nodeType = BrowseNodeType::Show;
        cc.node.displayTitle = cleanMediaFolderTitle(rawName);
        cc.node.heldSeasons = held;
        if (claim.hasClaim) {
            for (int s = claim.from; s <= claim.to; ++s)
                cc.node.claimedSeasons.append(s);
        } else {
            cc.node.claimedSeasons = held;
        }
        cc.node.seasonFolderPaths = seasonSubdirs;
        cc.node.mediaCount = held.size();
        const bool fullyHeld = cc.node.claimedSeasons.size() == held.size();
        if (fullyHeld) {
            cc.node.physicalFact = held.size() == 1
                ? QStringLiteral("1 season") : QStringLiteral("%1 seasons").arg(held.size());
        } else if (held.size() == 1) {
            cc.node.physicalFact = QStringLiteral("Season %1 only").arg(held.first());
        } else {
            QStringList parts;
            for (int s : held)
                parts << QString::number(s);
            cc.node.physicalFact =
                QStringLiteral("Seasons %1 only").arg(parts.join(QStringLiteral(", ")));
        }
        return cc;
    }

    if (looseMediaPaths.size() == 1) {
        // A folder that is one film IS that film (locked design #4) — companions and extras
        // already never contributed to this count.
        cc.node.nodeType = BrowseNodeType::Film;
        cc.node.displayTitle = cleanMediaFolderTitle(rawName);
        cc.node.mediaCount = 1;
        return cc; // quality/copy-count facts are Slice 3/enrichment's business, not this one
    }

    if (looseMediaPaths.size() > 1) {
        int episodeLike = 0;
        int videoTotal = 0;
        for (const QString& p : looseMediaPaths) {
            if (kindForFile(p) != MediaKind::Video)
                continue;
            ++videoTotal;
            if (parseEpisodeNumber(QFileInfo(p).fileName()).matched)
                ++episodeLike;
        }
        if (videoTotal > 1 && episodeLike == videoTotal) {
            // Every video file in this folder parses as an episode (SxxExx or absolute
            // numbering): a show — flat, no season subfolders (the Gintama shape), or one
            // season of a larger show (the Loki shape, resolved by the sibling-merge pass in
            // planBrowseLevel, one level up).
            cc.node.nodeType = BrowseNodeType::Show;
            cc.node.displayTitle = cleanMediaFolderTitle(rawName);
            cc.node.mediaCount = videoTotal;
            cc.node.physicalFact = QStringLiteral("%1 episodes").arg(videoTotal);
            const SeasonClaim claim = seasonClaimFromFolderName(rawName);
            if (claim.hasClaim && claim.from == claim.to) {
                cc.siblingCandidate = true;
                cc.siblingSeasonNumber = claim.from;
            }
            return cc;
        }
        // Multiple media files with no discernible show pattern: an honest plain folder (the
        // Cricket shape) — its clips become tiles only once you drill in, never here.
        cc.node.nodeType = BrowseNodeType::Folder;
        cc.node.displayTitle = cleanMediaFolderTitle(rawName);
        cc.node.mediaCount = looseMediaPaths.size();
        return cc;
    }

    // No loose media directly here and no season subdirectories: nothing collapses it, so it
    // presents honestly as a plain folder (companions-only, or deeper unrelated structure).
    cc.node.nodeType = BrowseNodeType::Folder;
    cc.node.displayTitle = cleanMediaFolderTitle(rawName);
    cc.node.mediaCount = 0;
    return cc;
}

QList<BrowseNode> planBrowseLevel(const QString& levelPath, const QStringList& scanIgnore,
                                  const CancellationToken* cancel)
{
    QList<BrowseNode> out;
    const QStringList needles = sanitizeIgnoreNeedles(scanIgnore);

    // ── A virtual show key spanning separate sibling season folders (the Loki shape): recompute
    // the siblings from the parent and hand back one season row per matching folder. ──
    static const QString kShowSentinel = QStringLiteral("::show::");
    const int sentinelPos = levelPath.indexOf(kShowSentinel);
    if (sentinelPos >= 0) {
        const QString parentPath = levelPath.left(sentinelPos);
        const QString slug = levelPath.mid(sentinelPos + kShowSentinel.size());
        const QStringList siblingDirs = listImmediateSubdirs(parentPath, needles);
        for (const QString& sib : siblingDirs) {
            if (cancel && cancel->isCancelled())
                return out;
            const QString name = QFileInfo(sib).fileName();
            if (isExtrasDirName(name) || isSeasonLikeDirName(name))
                continue;
            const ChildClassification cc = classifyChildDirectory(sib, needles);
            if (!cc.siblingCandidate)
                continue;
            if (normalizedTitle(stripTrailingSeasonLabel(cc.node.displayTitle)) != slug)
                continue;
            BrowseNode season;
            season.nodeType = BrowseNodeType::Season;
            season.key = sib;
            season.path = sib;
            season.seasonNumber = cc.siblingSeasonNumber;
            season.displayTitle = QStringLiteral("Season %1").arg(cc.siblingSeasonNumber);
            season.mediaCount = cc.node.mediaCount;
            season.physicalFact = cc.node.physicalFact;
            out.append(season);
        }
        std::sort(out.begin(), out.end(), [](const BrowseNode& a, const BrowseNode& b) {
            return a.seasonNumber < b.seasonNumber;
        });
        return out;
    }

    QDir dir(levelPath);
    if (!dir.exists())
        return out;

    // Immediate subdirectories, split into bare-season (a show's own seasons), extras (folded,
    // never a node), and everything else worth classifying.
    const QStringList allChildDirs = listImmediateSubdirs(levelPath, needles);
    QStringList bareSeasonChildren;
    QStringList qualifyingChildDirs;
    for (const QString& c : allChildDirs) {
        const QString name = QFileInfo(c).fileName();
        if (isExtrasDirName(name))
            continue;
        if (isSeasonLikeDirName(name)) {
            bareSeasonChildren.append(c);
            continue;
        }
        qualifyingChildDirs.append(c);
    }

    // ── Drilled into a show whose seasons are nested bare-season subfolders (the Wire shape):
    // hand back season rows for exactly those. (Slice 1 scope: a show folder mixing season
    // subfolders with unrelated siblings is not one of the five real shapes this slice targets —
    // recorded here rather than silently mishandled.) ──
    if (!bareSeasonChildren.isEmpty()) {
        for (const QString& sp : bareSeasonChildren) {
            if (cancel && cancel->isCancelled())
                return out;
            BrowseNode season;
            season.nodeType = BrowseNodeType::Season;
            season.key = sp;
            season.path = sp;
            season.seasonNumber = seasonOrdinalFromDirName(QFileInfo(sp).fileName());
            season.displayTitle = QStringLiteral("Season %1").arg(season.seasonNumber);
            const QStringList vids = videoFilesDirectlyIn(sp, needles);
            season.mediaCount = vids.size();
            season.physicalFact = QStringLiteral("%1 episodes").arg(vids.size());
            out.append(season);
        }
        std::sort(out.begin(), out.end(), [](const BrowseNode& a, const BrowseNode& b) {
            return a.seasonNumber < b.seasonNumber;
        });
        return out;
    }

    // ── Plain-folder / root browse: classify each qualifying subdirectory, then fold sibling
    // season-shaped folders sharing a base title into one show (the Loki shape). ──
    if (!qualifyingChildDirs.isEmpty()) {
        QList<ChildClassification> classified;
        classified.reserve(qualifyingChildDirs.size());
        for (const QString& child : qualifyingChildDirs) {
            if (cancel && cancel->isCancelled())
                return out;
            classified.append(classifyChildDirectory(child, needles));
        }

        QList<BrowseNode> folders, shows, films;
        QList<bool> merged;
        merged.reserve(classified.size());
        for (int i = 0; i < classified.size(); ++i)
            merged.append(false);

        for (int i = 0; i < classified.size(); ++i) {
            if (merged.at(i) || !classified.at(i).siblingCandidate)
                continue;
            const QString baseI =
                normalizedTitle(stripTrailingSeasonLabel(classified.at(i).node.displayTitle));
            QList<int> group = {i};
            for (int j = i + 1; j < classified.size(); ++j) {
                if (merged.at(j) || !classified.at(j).siblingCandidate)
                    continue;
                const QString baseJ = normalizedTitle(
                    stripTrailingSeasonLabel(classified.at(j).node.displayTitle));
                if (baseJ == baseI)
                    group.append(j);
            }
            if (group.size() < 2)
                continue; // a lone season-shaped folder stays a show of its own, below

            BrowseNode show;
            show.nodeType = BrowseNodeType::Show;
            show.displayTitle = stripTrailingSeasonLabel(classified.at(i).node.displayTitle);
            QList<int> held;
            QStringList seasonPaths;
            for (int idx : group) {
                merged[idx] = true;
                held.append(classified.at(idx).siblingSeasonNumber);
                seasonPaths.append(classified.at(idx).node.path);
            }
            std::sort(held.begin(), held.end());
            const QString slug = normalizedTitle(show.displayTitle);
            show.key = levelPath + kShowSentinel + slug;
            show.heldSeasons = held;
            show.claimedSeasons = held; // no single folder name claims a total across siblings
            show.seasonFolderPaths = seasonPaths;
            show.mediaCount = held.size();
            show.physicalFact = QStringLiteral("%1 seasons").arg(held.size());
            shows.append(show);
        }
        for (int i = 0; i < classified.size(); ++i) {
            if (merged.at(i))
                continue;
            const BrowseNode& n = classified.at(i).node;
            if (n.nodeType == BrowseNodeType::Show)
                shows.append(n);
            else if (n.nodeType == BrowseNodeType::Film)
                films.append(n);
            else
                folders.append(n);
        }

        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        auto byTitle = [&collator](const BrowseNode& a, const BrowseNode& b) {
            return collator.compare(a.displayTitle, b.displayTitle) < 0;
        };
        std::sort(folders.begin(), folders.end(), byTitle);
        std::sort(shows.begin(), shows.end(), byTitle);
        std::sort(films.begin(), films.end(), byTitle);

        // Locked design §4.2 ordering: subfolders, then series, then films.
        out += folders;
        out += shows;
        out += films;
        return out;
    }

    // ── No subdirectories worth a tile: this level is a leaf holding loose video files
    // directly — episodes if they parse (Gintama's own folder, or any season folder reached a
    // different way), clips otherwise (Cricket's own folder — "loose clips render wide at
    // folder level", locked design). ──
    QDir mdir(levelPath);
    const auto looseVideos = mdir.entryInfoList(videoFilters(), QDir::Files);
    for (const auto& f : looseVideos) {
        if (cancel && cancel->isCancelled())
            return out;
        if (pathHitsNeedle(f.absoluteFilePath(), needles))
            continue;
        BrowseNode n;
        n.key = f.absoluteFilePath();
        n.path = f.absoluteFilePath();
        n.displayTitle = cleanMediaFolderTitle(f.completeBaseName());
        const SeasonEpisode se = parseEpisodeNumber(f.fileName());
        if (se.matched) {
            n.nodeType = BrowseNodeType::Episode;
            n.episodeNumber = se.episode;
            n.seasonNumber = se.season;
            n.physicalFact = se.absolute
                ? QStringLiteral("Episode %1").arg(se.episode)
                : QStringLiteral("S%1E%2")
                      .arg(se.season, 2, 10, QLatin1Char('0'))
                      .arg(se.episode, 2, 10, QLatin1Char('0'));
        } else {
            n.nodeType = BrowseNodeType::Clip;
        }
        out.append(n);
    }
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(out.begin(), out.end(), [&collator](const BrowseNode& a, const BrowseNode& b) {
        return collator.compare(a.path, b.path) < 0;
    });
    return out;
}

} // namespace VaultKit
