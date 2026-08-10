#include "VaultWatcher.h"

#include "VaultConfig.h"
#include "VaultIdentity.h"
#include "VaultIndex.h"
#include "VaultKit.h"
#include "VaultScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QVariantMap>

// Mirror VaultConfig::norm so override keys and the degraded/dirty sets match how the config
// and the census look paths up (cleanPath + lowercase on Windows).
QString VaultWatcher::normPath(const QString& p)
{
    QString n = QDir::cleanPath(p);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n;
}

VaultWatcher::VaultWatcher(VaultIndex* index, VaultIdentity* identity, VaultConfig* config,
                           QObject* parent)
    : QObject(parent), m_index(index), m_identity(identity), m_config(config)
{
    m_watcher = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300); // arrival storms coalesce into one pass; "within seconds"

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString& path) { onDirectoryChanged(path); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString& path) { onDirectoryChanged(path); });
    connect(m_debounce, &QTimer::timeout, this, [this] { debounceExpired(); });

    // A confirm (or a root added/removed/re-kind-chip) changes the watch set: reconcile.
    if (m_config) {
        connect(m_config, &VaultConfig::changed, this, [this]() { refresh(); });
    }
}

void VaultWatcher::setImmersive(bool on)
{
    if (m_immersive == on)
        return;
    m_immersive = on;
    emit immersiveChanged();
    if (!on && !m_dirty.isEmpty())
        flushPending(); // the gate closed — flush what accumulated behind it
}

void VaultWatcher::refresh()
{
    m_watched.clear();
    if (!m_config)
        return;
    const QVariantList roots = m_config->roots();
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
        if (!m.value(QStringLiteral("confirmed")).toBool())
            continue;
        const QString path = m.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            continue;
        watchRoot(path);
    }
}

bool VaultWatcher::isRootDegraded(const QString& rootPath) const
{
    return m_degraded.contains(normPath(rootPath));
}

void VaultWatcher::watchRoot(const QString& root)
{
    const QString norm = normPath(root);
    // addPath returns FALSE when the path is ALREADY being watched (and when it does not
    // exist) — so refresh() must be idempotent against QFSW's own list, not just ours.
    if (m_watcher->directories().contains(root) || m_watcher->addPath(root)) {
        m_watched.insert(norm);
        m_degraded.remove(norm);
    } else {
        m_degraded.insert(norm); // QFSW limits, network drives, vanished root — rescan-on-open covers it
    }
}

void VaultWatcher::onDirectoryChanged(const QString& path)
{
    // A dirty root set keeps the debounce accumulating regardless of the immersive gate —
    // the gate only defers the UPSERT, never the observation (Slice 15 "behavior to preserve").
    m_dirty.insert(normPath(path));
    m_debounce->start();
}

void VaultWatcher::debounceExpired()
{
    if (m_immersive)
        return; // the upsert stays deferred until the immersive surface closes
    flushPending();
}

void VaultWatcher::flushPending()
{
    if (m_dirty.isEmpty())
        return;
    if (!m_config)
        return;
    const QStringList needles = m_config->scanIgnore();
    const QVariantMap overrides = m_config->kindOverrides();
    const QSet<QString> dirty = m_dirty;
    m_dirty.clear();
    for (const QString& norm : dirty) {
        // Resolve the normalized dirty key back to a real path: the config is the source of
        // truth (a root may have been removed while we were dirty — then nothing to do).
        QString root;
        const QVariantList roots = m_config->roots();
        for (const QVariant& r : roots) {
            const QVariantMap m = r.toMap();
            if (!m.value(QStringLiteral("confirmed")).toBool())
                continue;
            if (normPath(m.value(QStringLiteral("path")).toString()) == norm) {
                root = m.value(QStringLiteral("path")).toString();
                break;
            }
        }
        if (root.isEmpty())
            continue;
        // Re-arm the watch as we go — a root that dropped its watch (drive away) comes back
        // here, and if it is still unwatchable it stays degraded for the rescan-on-open.
        if (!m_watched.contains(norm))
            watchRoot(root);

        const Landing landing = processRoot(root, needles, overrides);
        if (landing.landedCount > 0)
            emit landed(landing.landedCount);
        if (!landing.newKindSlices.isEmpty())
            emit newKindArrival(root, landing.newKindSlices);
    }
}

QString VaultWatcher::lawForSubtree(const QString& subtree,
                                    const QVariantMap& kindOverrides) const
{
    // 1. The user's chip override IS the law (VaultConfig::kindOverrides, normalized keys).
    const auto ov = kindOverrides.constFind(normPath(subtree));
    if (ov != kindOverrides.constEnd())
        return ov.value().toString();
    // 2. The shelf's current truth: the index's dominant kind for the subtree. The index
    //    stores the path exactly as scanned, so query with the REAL-case subtree.
    if (m_index) {
        const QString dominant = m_index->dominantKindForSubtree(subtree);
        if (!dominant.isEmpty())
            return dominant;
    }
    // 3. A brand-new subtree — the arrival itself is the law; never a new-kind card.
    return QString();
}

VaultWatcher::Landing VaultWatcher::processRoot(const QString& root,
                                                const QStringList& scanIgnore,
                                                const QVariantMap& kindOverrides)
{
    Landing landing;
    if (!m_index || !m_identity)
        return landing;

    const QStringList needles = VaultKit::sanitizeIgnoreNeedles(scanIgnore);
    const auto groups = VaultKit::groupByFirstLevelSubdir(
        {root}, VaultKit::allMediaFilters(), nullptr, needles);

    // The ids already shelved under this root — everything else on disk is an arrival.
    const QSet<QString> existing = m_index->fileIdsInRoot(root);

    QList<VaultIndex::FileRow> toUpsert;
    // normSubtree -> { kind -> { count, sampleTitles } } for the new-kind card slices.
    QMap<QString, QMap<QString, QPair<int, QStringList>>> newKind;
    // normSubtree -> real subtree path (for the card model).
    QMap<QString, QString> subtreeByNorm;
    // normSubtree -> loose flag.
    QMap<QString, bool> looseByNorm;

    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const QString key = it.key();
        const bool loose = key.endsWith(QStringLiteral("::LOOSE"));
        const QString subtree = loose ? root : key;
        const QString normSubtree = normPath(subtree);
        const QString law = lawForSubtree(subtree, kindOverrides);

        QString groupTitle = VaultKit::cleanMediaFolderTitle(QFileInfo(subtree).fileName());
        if (groupTitle.isEmpty())
            groupTitle = QFileInfo(subtree).fileName();

        for (const QString& f : it.value()) {
            const VaultKit::MediaKind fk = VaultKit::kindForFile(f);
            if (fk == VaultKit::MediaKind::Unknown)
                continue; // not media — never an arrival

            const QFileInfo fi(f);
            const QString id = m_identity->idForFile(f, fi.size(),
                                                     fi.lastModified().toMSecsSinceEpoch());
            if (existing.contains(id))
                continue; // unchanged — already shelved (the exact-arrival-set diff)

            // Row construction mirrors VaultScanner::buildScan so a later full rescan
            // reproduces the same rows (the index is a rebuildable product).
            VaultIndex::FileRow row;
            row.id = id;
            row.rootPath = root;
            row.subtreePath = subtree;
            row.groupKey = subtree;
            row.groupTitle = groupTitle;
            const QString kindName = VaultKit::kindName(fk);
            row.kind = kindName;
            row.path = f;
            row.realName = fi.fileName();
            QString disp = VaultKit::cleanMediaFolderTitle(fi.completeBaseName());
            row.displayTitle = disp.isEmpty() ? fi.completeBaseName() : disp;
            row.subfolder = VaultScanner::subfolderOf(subtree, f);
            row.size = fi.size();
            row.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
            row.format = fi.suffix().toLower();
            toUpsert.append(row);

            // New-kind arrival: the law exists (override or index truth) and disagrees.
            if (!law.isEmpty() && law != kindName) {
                QMap<QString, QPair<int, QStringList>>& kinds = newKind[normSubtree];
                QPair<int, QStringList>& acc = kinds[kindName];
                ++acc.first;
                if (acc.second.size() < 3)
                    acc.second.append(row.displayTitle);
                subtreeByNorm[normSubtree] = subtree;
                looseByNorm[normSubtree] = loose;
            }
        }
    }

    if (!toUpsert.isEmpty()) {
        if (!m_index->upsertMany(toUpsert))
            return landing; // a failed upsert is a failed pass; no signals, no card
        landing.landedCount = toUpsert.size();
    }

    // One-slice card model per subtree with a new-kind arrival (S11 law: one card at a time —
    // VaultLibrary guards card-while-card/scanning).
    for (auto it = newKind.constBegin(); it != newKind.constEnd(); ++it) {
        const QString normSubtree = it.key();
        for (auto k = it.value().constBegin(); k != it.value().constEnd(); ++k) {
            QVariantMap sm;
            sm[QStringLiteral("subtreePath")] = subtreeByNorm.value(normSubtree);
            const QString subtree = subtreeByNorm.value(normSubtree);
            QString groupTitle = VaultKit::cleanMediaFolderTitle(QFileInfo(subtree).fileName());
            if (groupTitle.isEmpty())
                groupTitle = QFileInfo(subtree).fileName();
            sm[QStringLiteral("groupTitle")] = groupTitle;
            sm[QStringLiteral("kind")] = k.key();
            sm[QStringLiteral("count")] = k.value().first;
            sm[QStringLiteral("mixed")] = false;
            sm[QStringLiteral("loose")] = looseByNorm.value(normSubtree, false);
            sm[QStringLiteral("leftoverCount")] = 0;
            sm[QStringLiteral("seriesCount")] = 0;
            sm[QStringLiteral("sample")] = k.value().second.join(QStringLiteral("  ·  "));
            sm[QStringLiteral("sizeBytes")] = 0.0;
            landing.newKindSlices.append(sm);
        }
    }
    return landing;
}
