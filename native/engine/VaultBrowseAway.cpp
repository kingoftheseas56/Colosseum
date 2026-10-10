#include "VaultBrowseAway.h"
#include "VaultIndex.h"
#include "VaultKit.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QStringList>

namespace {

// Mirrors VaultConfig::norm / VaultWatcher::normPath — the same normalization every root-path
// comparison in this codebase already uses (cleanPath + lowercase on Windows).
QString normPath(const QString& p)
{
    QString n = QDir::cleanPath(p);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n;
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
    QMap<QString, QString> childPaths; // normalized key -> original path spelling
    QStringList childOrder; // insertion order, stable output
    for (const VaultIndex::FileRow& row : rows) {
        // subtreePath is the scanner's show/group identity, not necessarily the
        // file's parent (nested seasons intentionally share one show subtree).
        const QString parentPath = QDir::cleanPath(QFileInfo(row.path).path());
        const QString normParent = normPath(parentPath);
        QString childKey;
        if (normParent == normLevel) {
            childKey = row.path; // a loose file directly at this level: one tile of its own
        } else if (normParent.startsWith(normLevel + QLatin1Char('/'))) {
            const QString rest = parentPath.mid(normLevel.size() + 1);
            const int slash = rest.indexOf(QLatin1Char('/'));
            childKey = slash >= 0 ? parentPath.left(normLevel.size() + 1 + slash) : parentPath;
        } else {
            continue; // not under this level at all
        }
        const QString normalizedKey = normPath(childKey);
        if (!byChild.contains(normalizedKey)) {
            childOrder.append(normalizedKey);
            childPaths.insert(normalizedKey, childKey);
        }
        byChild[normalizedKey].append(row);
    }

    for (const QString& normalizedKey : childOrder) {
        const QString key = childPaths.value(normalizedKey);
        const QList<VaultIndex::FileRow>& groupRows = byChild.value(normalizedKey);
        if (groupRows.isEmpty())
            continue;
        const bool singleFile = groupRows.size() == 1;
        const auto& first = groupRows.first();
        const bool directFile = normalizedKey == normPath(first.path);
        const auto episode = VaultKit::parseEpisodeNumber(QFileInfo(first.path).fileName());
        const bool seasonFolder = !directFile
            && VaultKit::isSeasonLikeDirName(QFileInfo(key).fileName());
        // Keep single-film collapse, but never collapse a season (even one
        // episode) or a deeper ancestor into its only file.
        const bool filmFolder = !directFile && !seasonFolder && singleFile
            && !episode.matched && normPath(QFileInfo(first.path).path()) == normalizedKey;
        const bool leaf = directFile || filmFolder;
        QVariantMap m;
        m.insert(QStringLiteral("key"), key);
        const QString nodeType = directFile && first.kind == QLatin1String("video")
            ? (episode.matched ? QStringLiteral("episode") : QStringLiteral("clip"))
            : leaf ? QStringLiteral("film")
            : seasonFolder ? QStringLiteral("season") : QStringLiteral("folder");
        m.insert(QStringLiteral("nodeType"), nodeType);
        QString title = directFile ? first.displayTitle
            : seasonFolder ? QFileInfo(key).fileName()
            : first.identityTitle.isEmpty() ? first.groupTitle : first.identityTitle;
        if (title.isEmpty())
            title = QFileInfo(key).fileName();
        m.insert(QStringLiteral("displayTitle"), title);
        m.insert(QStringLiteral("physicalFact"), QString());
        m.insert(QStringLiteral("path"), leaf ? first.path : key);
        if (leaf) m.insert(QStringLiteral("id"), first.id);
        if (directFile && episode.matched) {
            m.insert(QStringLiteral("episodeNumber"), episode.episode);
            m.insert(QStringLiteral("seasonNumber"), episode.season);
        } else if (seasonFolder) {
            m.insert(QStringLiteral("seasonNumber"), VaultKit::seasonOrdinalFromDirName(QFileInfo(key).fileName()));
        }
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
