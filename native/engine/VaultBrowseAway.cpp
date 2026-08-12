#include "VaultBrowseAway.h"
#include "VaultIndex.h"

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
        bool identified = false;
        for (const VaultIndex::FileRow& r : groupRows) {
            if (!r.identityId.isEmpty() && !r.identitySuppressed)
                identified = true;
        }
        m.insert(QStringLiteral("state"),
                 identified ? QStringLiteral("identified") : QStringLiteral("resolving"));
        m.insert(QStringLiteral("away"), true);
        out.append(m);
    }
    return out;
}

} // namespace VaultBrowseAway
