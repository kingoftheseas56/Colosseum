#include "VaultBrowseAway.h"
#include "VaultIndex.h"
#include "VaultLocation.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace {

// Mirrors VaultConfig::norm / VaultWatcher::normPath — the same normalization every root-path
// comparison in this codebase already uses (cleanPath + lowercase on Windows).
QString normPath(const QString& p)
{
    return VaultLocation::normalize(p);
}

} // namespace

namespace VaultBrowseAway {

QString ownerRootPath(const QVariantList& roots, const QString& path)
{
    if (path.isEmpty())
        return QString();
    static const QString kShowSentinel = QStringLiteral("::show::");
    const int sentinelPos = path.indexOf(kShowSentinel);
    const QString searchPath = sentinelPos >= 0 ? path.left(sentinelPos) : path;
    const QString target = normPath(searchPath);

    QString bestRootPath;
    int bestLen = -1;
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
        const bool confirmedOrSynthetic = m.value(QStringLiteral("confirmed")).toBool()
            || m.value(QStringLiteral("synthetic")).toBool();
        if (!confirmedOrSynthetic)
            continue;
        const QString rootPath = m.value(QStringLiteral("path")).toString();
        const QString normRoot = normPath(rootPath);
        if (normRoot.isEmpty())
            continue;
        if (target != normRoot && !target.startsWith(normRoot + QLatin1Char('/')))
            continue;
        if (normRoot.size() > bestLen) {
            bestLen = normRoot.size();
            bestRootPath = rootPath;
        }
    }
    return bestRootPath;
}

bool ownerRootAway(VaultIndex* index, const QVariantList& roots, const QString& path)
{
    if (!index)
        return false;
    const QString rootPath = ownerRootPath(roots, path);
    if (rootPath.isEmpty())
        return false;
    const QList<VaultIndex::FileRow> rows = index->rowsForRoot(rootPath);
    return !rows.isEmpty() && rows.first().away;
}

QVariantList offlineBrowseAt(VaultIndex* index, const QVariantList& roots, const QString& levelPath)
{
    QVariantList out;
    if (!index)
        return out;
    const QString rootPath = ownerRootPath(roots, levelPath);
    if (rootPath.isEmpty())
        return out;
    static const QString kShowSentinel = QStringLiteral("::show::");
    const int sentinelPos = levelPath.indexOf(kShowSentinel);
    const QString realLevelPath = sentinelPos >= 0 ? levelPath.left(sentinelPos) : levelPath;
    const QString normLevel = normPath(realLevelPath);

    const QList<VaultIndex::FileRow> rows = index->rowsForRoot(rootPath);
    QMap<QString, QList<VaultIndex::FileRow>> byChild; // childKey -> its rows
    QStringList childOrder; // insertion order, stable output
    for (const VaultIndex::FileRow& row : rows) {
        const QString normSub = normPath(row.subtreePath);
        QString childKey;
        if (normSub == normLevel) {
            childKey = row.path; // a loose file directly at this level: one tile of its own
        } else if (normSub.startsWith(normLevel + QLatin1Char('/'))) {
            const QString rest = normSub.mid(normLevel.size() + 1);
            const int slash = rest.indexOf(QLatin1Char('/'));
            childKey = slash >= 0 ? normLevel + QLatin1Char('/') + rest.left(slash) : normSub;
        } else {
            continue; // not under this level at all
        }
        if (!byChild.contains(childKey))
            childOrder.append(childKey);
        byChild[childKey].append(row);
    }

    for (const QString& key : childOrder) {
        const QList<VaultIndex::FileRow>& groupRows = byChild.value(key);
        if (groupRows.isEmpty())
            continue;
        const bool singleFile = groupRows.size() == 1;
        QVariantMap m;
        m.insert(QStringLiteral("key"), key);
        // Structural simplification: an offline level cannot re-derive the live walker's
        // show/season collapse (no directory to classify), so a multi-file group folds to a
        // plain folder tile instead of a show/season tile — still one tile per held group,
        // still marked away, still "nothing disappears" (design §4.7); precise offline show
        // fidelity is the parent ownership arc's business, not this fallback's.
        m.insert(QStringLiteral("nodeType"),
                 singleFile ? QStringLiteral("film") : QStringLiteral("folder"));
        QString title = groupRows.first().identityTitle.isEmpty()
            ? groupRows.first().groupTitle : groupRows.first().identityTitle;
        if (title.isEmpty())
            title = QFileInfo(key).fileName();
        m.insert(QStringLiteral("displayTitle"), title);
        m.insert(QStringLiteral("physicalFact"), QString());
        m.insert(QStringLiteral("path"), singleFile ? groupRows.first().path : key);
        QVariantMap counts;
        counts.insert(QStringLiteral("items"), groupRows.size());
        m.insert(QStringLiteral("counts"), counts);
        m.insert(QStringLiteral("coverRef"), QString());
        // The group's stored kind, same contract VaultLibrary::browseAt() carries — an away tile
        // still opens the detail sheet, and the sheet's Identify still has to reach the right
        // catalogue. Most common kind wins; the first row to reach that count breaks a tie.
        QMap<QString, int> kindTally;
        QString dominantKind;
        int dominantCount = 0;
        bool identified = false;
        for (const VaultIndex::FileRow& r : groupRows) {
            if (!r.identityId.isEmpty() && !r.identitySuppressed)
                identified = true;
            if (r.kind.isEmpty())
                continue;
            const int seen = ++kindTally[r.kind];
            if (seen > dominantCount) {
                dominantCount = seen;
                dominantKind = r.kind;
            }
        }
        m.insert(QStringLiteral("kind"), dominantKind);
        m.insert(QStringLiteral("state"),
                 identified ? QStringLiteral("identified") : QStringLiteral("resolving"));
        m.insert(QStringLiteral("away"), true);
        out.append(m);
    }
    return out;
}

} // namespace VaultBrowseAway


namespace {

QString relativeToRoot(const QString& root, const QString& value)
{
    const QString normalizedRoot = normPath(root);
    const QString normalizedValue = normPath(value);
    if (normalizedValue == normalizedRoot)
        return QString();
    const QString prefix = normalizedRoot + QLatin1Char('/');
    return normalizedValue.startsWith(prefix)
        ? normalizedValue.mid(prefix.size()) : QString();
}

QString logicalPath(const QString& root, const QString& relative)
{
    if (relative.isEmpty())
        return root;
    return root + (root.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + relative;
}

QString dominantKind(const QList<VaultIndex::FileRow>& rows)
{
    QMap<QString, int> tally;
    QString best;
    int bestCount = 0;
    for (const VaultIndex::FileRow& row : rows) {
        if (row.kind.isEmpty())
            continue;
        const int count = ++tally[row.kind];
        if (count > bestCount) {
            best = row.kind;
            bestCount = count;
        }
    }
    return best;
}

bool isSeasonLikeName(const QString& name)
{
    static const QRegularExpression rx(
        QStringLiteral("^(?:Season\\s*\\d+|S\\d{1,3}|Disc\\s*\\d+|Volume\\s*\\d+|Vol\\s*\\d+|Part\\s*\\d+|CD\\s*\\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    return rx.match(name.trimmed()).hasMatch();
}

bool looksLikeEpisodeName(const QString& name)
{
    static const QRegularExpression sxe(
        QStringLiteral("(?:^|[ ._\\-])S\\d{1,3}E\\d{1,4}(?:[ ._\\-]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression absolute(
        QStringLiteral("\\s-\\s\\d{2,3}(?:\\s|[._\\-]|$)"));
    return sxe.match(name).hasMatch() || absolute.match(name).hasMatch();
}

bool looksLikeShow(const QList<VaultIndex::FileRow>& rows)
{
    if (rows.isEmpty())
        return false;
    bool sawEpisode = false;
    for (const VaultIndex::FileRow& row : rows) {
        if (row.kind != QLatin1String("video"))
            return false;
        const QString firstSubfolder = row.subfolder.section(QLatin1Char('/'), 0, 0);
        if (!firstSubfolder.isEmpty() && isSeasonLikeName(firstSubfolder)) {
            sawEpisode = true;
            continue;
        }
        if (!looksLikeEpisodeName(row.realName))
            return false;
        sawEpisode = true;
    }
    return sawEpisode;
}

QVariantMap indexedNode(const QString& key, const QString& nodeType,
                        const QString& title, const QString& path,
                        const QList<VaultIndex::FileRow>& rows)
{
    QVariantMap node;
    node.insert(QStringLiteral("key"), key);
    node.insert(QStringLiteral("nodeType"), nodeType);
    node.insert(QStringLiteral("displayTitle"), title);
    node.insert(QStringLiteral("path"), path);
    QVariantMap counts;
    counts.insert(QStringLiteral("items"), rows.size());
    node.insert(QStringLiteral("counts"), counts);
    node.insert(QStringLiteral("kind"), dominantKind(rows));

    bool away = false;
    bool identified = false;
    qint64 newestMs = 0;
    qint64 sizeBytes = 0;
    for (const VaultIndex::FileRow& row : rows) {
        away = away || row.away;
        identified = identified || (!row.identityId.isEmpty() && !row.identitySuppressed);
        newestMs = qMax(newestMs, row.mtimeMs);
        sizeBytes += row.size;
    }
    node.insert(QStringLiteral("_sortNewestMs"), newestMs);
    node.insert(QStringLiteral("_sortSizeBytes"), sizeBytes);
    node.insert(QStringLiteral("away"), away);
    node.insert(QStringLiteral("state"), identified || nodeType != QLatin1String("film")
                    ? QStringLiteral("identified") : QStringLiteral("resolving"));
    node.insert(QStringLiteral("coverRef"), rows.size() == 1 ? rows.first().coverRef : QString());
    node.insert(QStringLiteral("physicalFact"),
                rows.size() > 1 ? QStringLiteral("%1 items").arg(rows.size()) : QString());
    if (rows.size() == 1 && !rows.first().id.isEmpty())
        node.insert(QStringLiteral("id"), rows.first().id);
    return node;
}

QVariantMap indexedLeaf(const VaultIndex::FileRow& row)
{
    const bool episode = looksLikeEpisodeName(row.realName);
    QString nodeType = QStringLiteral("film");
    if (row.kind == QLatin1String("video"))
        nodeType = episode ? QStringLiteral("episode") : QStringLiteral("clip");

    QVariantMap node = indexedNode(row.path, nodeType, row.displayTitle,
                                   row.path, QList<VaultIndex::FileRow>{row});
    node.insert(QStringLiteral("physicalFact"),
                QString());
    if (nodeType == QLatin1String("clip"))
        node.insert(QStringLiteral("state"), QStringLiteral("localOnly"));
    return node;
}

bool passesIndexedFilter(const QVariantMap& node, const QVariantMap& filter)
{
    const QString kind = filter.value(QStringLiteral("kind")).toString();
    if (!kind.isEmpty() && node.value(QStringLiteral("kind")).toString() != kind)
        return false;
    const QString state = filter.value(QStringLiteral("identState")).toString();
    if (!state.isEmpty() && node.value(QStringLiteral("state")).toString() != state)
        return false;
    const QString presence = filter.value(QStringLiteral("presence")).toString();
    const bool away = node.value(QStringLiteral("away")).toBool();
    if (presence == QLatin1String("present") && away)
        return false;
    if (presence == QLatin1String("away") && !away)
        return false;
    return true;
}

void sortIndexedNodes(QVariantList& nodes, const QString& sort)
{
    std::stable_sort(nodes.begin(), nodes.end(), [&sort](const QVariant& a, const QVariant& b) {
        const QVariantMap am = a.toMap();
        const QVariantMap bm = b.toMap();
        if (sort == QLatin1String("newest")) {
            const qint64 av = am.value(QStringLiteral("_sortNewestMs")).toLongLong();
            const qint64 bv = bm.value(QStringLiteral("_sortNewestMs")).toLongLong();
            if (av != bv)
                return av > bv;
        } else if (sort == QLatin1String("size")) {
            const qint64 av = am.value(QStringLiteral("_sortSizeBytes")).toLongLong();
            const qint64 bv = bm.value(QStringLiteral("_sortSizeBytes")).toLongLong();
            if (av != bv)
                return av > bv;
        }
        const QString at = VaultIndex::naturalSortKey(
            am.value(QStringLiteral("displayTitle")).toString());
        const QString bt = VaultIndex::naturalSortKey(
            bm.value(QStringLiteral("displayTitle")).toString());
        if (at != bt)
            return at < bt;
        return am.value(QStringLiteral("key")).toString()
            < bm.value(QStringLiteral("key")).toString();
    });
}

} // namespace

namespace VaultBrowseAway {

QVariantList indexedBrowseAt(VaultIndex* index, const QVariantList& roots,
                             const QString& levelPath, const QString& sort,
                             const QVariantMap& filter)
{
    QVariantList out;
    if (!index)
        return out;
    const QString root = ownerRootPath(roots, levelPath);
    if (root.isEmpty())
        return out;

    const QString levelRelative = relativeToRoot(root, levelPath);
    const QList<VaultIndex::FileRow> rootRows = index->rowsForRoot(root);
    QMap<QString, QList<VaultIndex::FileRow>> folders;
    QList<VaultIndex::FileRow> leaves;
    for (const VaultIndex::FileRow& row : rootRows) {
        const QString groupRelative = relativeToRoot(root, row.subtreePath);
        if (groupRelative.isEmpty())
            continue;
        QString folderRelative = groupRelative;
        if (!row.subfolder.isEmpty())
            folderRelative += QLatin1Char('/') + row.subfolder;

        if (levelRelative.isEmpty()) {
            const QString immediate = groupRelative.section(QLatin1Char('/'), 0, 0);
            folders[immediate].append(row);
            continue;
        }

        if (folderRelative == levelRelative) {
            leaves.append(row);
            continue;
        }
        const QString prefix = levelRelative + QLatin1Char('/');
        if (!folderRelative.startsWith(prefix))
            continue;
        const QString remainder = folderRelative.mid(prefix.size());
        const QString immediate = remainder.section(QLatin1Char('/'), 0, 0);
        if (!immediate.isEmpty())
            folders[immediate].append(row);
    }

    for (auto it = folders.constBegin(); it != folders.constEnd(); ++it) {
        const QString segment = it.key();
        const QList<VaultIndex::FileRow>& rows = it.value();
        const QString childRelative = levelRelative.isEmpty()
            ? segment : levelRelative + QLatin1Char('/') + segment;
        const QString childPath = logicalPath(root, childRelative);

        QString nodeType = QStringLiteral("folder");
        if (levelRelative.isEmpty()) {
            const bool oneFlatFile = rows.size() == 1 && rows.first().subfolder.isEmpty();
            if (oneFlatFile)
                nodeType = QStringLiteral("film");
            else if (looksLikeShow(rows))
                nodeType = QStringLiteral("show");
        } else if (isSeasonLikeName(segment)) {
            nodeType = QStringLiteral("season");
        }

        QString title = segment;
        if (levelRelative.isEmpty() && !rows.first().groupTitle.isEmpty())
            title = rows.first().groupTitle;
        const QString openPath = nodeType == QLatin1String("film")
            ? rows.first().path : childPath;
        out.append(indexedNode(childPath, nodeType, title, openPath, rows));
    }

    for (const VaultIndex::FileRow& row : leaves)
        out.append(indexedLeaf(row));

    sortIndexedNodes(out, sort);
    QVariantList filtered;
    filtered.reserve(out.size());
    for (const QVariant& value : out) {
        QVariantMap node = value.toMap();
        if (!passesIndexedFilter(node, filter))
            continue;
        node.remove(QStringLiteral("_sortNewestMs"));
        node.remove(QStringLiteral("_sortSizeBytes"));
        filtered.append(node);
    }
    return filtered;
}

} // namespace VaultBrowseAway
