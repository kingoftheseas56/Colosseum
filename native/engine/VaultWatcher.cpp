#include "VaultWatcher.h"

#include "VaultConfig.h"
#include "VaultIdentity.h"
#include "VaultIndex.h"
#include "VaultKit.h"
#include "VaultScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QVariantMap>
#include <QtConcurrent>

namespace {
constexpr int kMaxWatchedDirectoriesPerRoot = 512;
}

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
    m_probe = new QTimer(this);
    m_probe->setInterval(1000); // cheap exists probe; a replug must revive without reopening Vault

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString& path) { onDirectoryChanged(path); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString& path) { onDirectoryChanged(path); });
    connect(m_debounce, &QTimer::timeout, this, [this] { debounceExpired(); });
    connect(m_probe, &QTimer::timeout, this, [this] { refreshInternal(false); });
    m_probe->start();

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
    refreshInternal(true);
}

void VaultWatcher::refreshInternal(bool retryDegradedTree)
{
    const QSet<QString> previouslyUnavailable = m_unavailable;
    QMap<QString, QString> configuredRoots; // normalized -> real/config path
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
        const QString norm = normPath(path);
        configuredRoots.insert(norm, path);
        if (QDir(path).exists()) {
            m_unavailable.remove(norm);
            watchRoot(path);
            // The one-second probe is availability-only after initial registration. Recurse only
            // for a newly confirmed/revived root or when a directory event explicitly requests it.
            if (!m_treeInitialized.contains(norm)
                && (retryDegradedTree || !m_treeDegraded.contains(norm)))
                scheduleTreeWatch(path);
        } else {
            m_unavailable.insert(norm);
            m_watched.remove(norm);
            m_treeInitialized.remove(norm); // revival must rebuild recursive watches once
            m_treeDegraded.remove(norm);   // revival earns one fresh recursive-watch attempt
            m_degraded.insert(norm); // rescan-on-open still needs to revisit the missing root
        }
    }

    // A removed root must not leave its old recursive children consuming the global
    // QFileSystemWatcher budget. Keep only directories belonging to confirmed roots.
    const QStringList watchedDirectories = m_watcher->directories();
    for (const QString& watched : watchedDirectories) {
        const QString watchedNorm = normPath(watched);
        bool keep = false;
        for (auto it = configuredRoots.constBegin(); it != configuredRoots.constEnd(); ++it) {
            if (watchedNorm == it.key()
                || watchedNorm.startsWith(it.key() + QLatin1Char('/'))) {
                keep = true;
                break;
            }
        }
        if (!keep)
            m_watcher->removePath(watched);
    }
    for (auto it = m_treeInitialized.begin(); it != m_treeInitialized.end(); ) {
        if (!configuredRoots.contains(*it))
            it = m_treeInitialized.erase(it);
        else
            ++it;
    }

    // QFileSystemWatcher degradation (limits/network watch failure) is not filesystem absence.
    // Only the explicit root-exists probe can gray a shelf or announce its revival.
    QSet<QString> transitions = previouslyUnavailable;
    transitions.unite(m_unavailable);
    for (const QString& norm : transitions) {
        if (!configuredRoots.contains(norm))
            continue;
        const bool wasAvailable = !previouslyUnavailable.contains(norm);
        const bool isAvailable = !m_unavailable.contains(norm);
        if (wasAvailable != isAvailable)
            emit rootAvailabilityChanged(configuredRoots.value(norm), isAvailable);
    }
}

bool VaultWatcher::isRootDegraded(const QString& rootPath) const
{
    return m_degraded.contains(normPath(rootPath));
}

bool VaultWatcher::isTreeWatchDegraded(const QString& rootPath) const
{
    return m_treeDegraded.contains(normPath(rootPath));
}

void VaultWatcher::watchRoot(const QString& root)
{
    const QString norm = normPath(root);
    // addPath returns FALSE when the path is ALREADY being watched (and when it does not
    // exist) — so refresh() must be idempotent against QFSW's own list, not just ours.
    if (addDirectoryWatch(root)) {
        m_watched.insert(norm);
        // A successful root watch does not prove that the recursive tree registration
        // succeeded. Preserve a prior cap/child-registration degradation until the tree
        // walk reports a complete result.
        if (!m_treeDegraded.contains(norm))
            m_degraded.remove(norm);
    } else {
        m_degraded.insert(norm); // QFSW limits, network drives, vanished root — rescan-on-open covers it
    }
}

bool VaultWatcher::addDirectoryWatch(const QString& path)
{
    const QString target = normPath(path);
    for (const QString& watched : m_watcher->directories()) {
        if (normPath(watched) == target)
            return true;
    }
    return m_watcher->addPath(path);
}

void VaultWatcher::scheduleTreeWatch(const QString& root, bool replayIfInFlight)
{
    const QString norm = normPath(root);
    if (!QDir(root).exists())
        return;
    if (m_treeScansInFlight.contains(norm)) {
        // A directory event during the walk may describe a folder created after the
        // iterator passed its parent. Replay one fresh walk when this one completes.
        if (replayIfInFlight)
            m_treeRescanRequested.insert(norm);
        return;
    }

    m_treeScansInFlight.insert(norm);
    auto future = QtConcurrent::run([root]() {
        QStringList directories;
        directories.append(root);
        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (directories.size() >= kMaxWatchedDirectoriesPerRoot) {
                // An empty sentinel carries the cap state without a second result type.
                directories.append(QString());
                break;
            }
            directories.append(it.next());
        }
        return directories;
    });
    future.then(this, [this, root, norm](QStringList directories) {
                m_treeScansInFlight.remove(norm);

                if (!m_config || !m_config->isRootConfirmed(root) || !QDir(root).exists())
                    return;

                const bool capped = !directories.isEmpty() && directories.constLast().isEmpty();
                if (capped)
                    directories.removeLast();

                bool registrationFailed = false;
                for (const QString& directory : directories) {
                    if (!addDirectoryWatch(directory)) {
                        registrationFailed = true;
                        break;
                    }
                }
                const bool complete = !capped && !registrationFailed;
                // A healthy completed walk is sticky — one probe-time recursive attempt is enough,
                // and a missing/revived root clears this bit in refresh(). A capped or
                // registration-failed walk must NOT set this bit: refresh()'s gate
                // (`!m_treeInitialized.contains(norm)`) is the only thing that re-triggers
                // scheduleTreeWatch, so a degraded root that stays marked initialized would get
                // exactly one walk attempt per session — self-healing (cap relief, watch budget
                // freed elsewhere) would never be revisited.
                if (complete) {
                    m_treeInitialized.insert(norm);
                    m_treeDegraded.remove(norm);
                    m_degraded.remove(norm);
                } else {
                    m_treeDegraded.insert(norm);
                    m_degraded.insert(norm);
                }
                emit watchTreeReconciled(root, directories.size(), complete);

                const bool replay = m_treeRescanRequested.remove(norm);
                if (replay)
                    scheduleTreeWatch(root);
                if (m_dirty.contains(norm) && !m_immersive)
                    m_debounce->start();
            });
}

void VaultWatcher::onDirectoryChanged(const QString& path)
{
    QString root = path;
    if (m_config) {
        QStringList roots;
        for (const QVariant& value : m_config->roots()) {
            const auto configured = value.toMap();
            if (configured.value(QStringLiteral("confirmed")).toBool()
                && !configured.value(QStringLiteral("hidden")).toBool()
                && !configured.value(QStringLiteral("synthetic")).toBool())
                roots.append(configured.value(QStringLiteral("path")).toString());
        }
        const QString owner = VaultKit::ownerRootForPath(roots, path);
        if (!owner.isEmpty()) root = owner;
    }

    if (!QDir(root).exists()) {
        const QString norm = normPath(root);
        const bool wasUnavailable = m_unavailable.contains(norm);
        m_unavailable.insert(norm);
        m_degraded.insert(norm);
        m_watched.remove(norm);
        if (!wasUnavailable)
            emit rootAvailabilityChanged(root, false);
        return;
    }

    // A dirty root set keeps the debounce accumulating regardless of the immersive gate —
    // the gate only defers the UPSERT, never the observation (Slice 15 "behavior to preserve").
    // Re-walk now so a newly-created subdirectory is registered before a later file arrives.
    scheduleTreeWatch(root, true);
    m_dirty.insert(normPath(root));
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
        if (m_treeScansInFlight.contains(norm)) {
            // Do not consume the dirty edge before the new directories are registered.
            // The completion callback restarts this debounce, so a file created during
            // the walk is included by the subsequent processRoot census.
            m_dirty.insert(norm);
            continue;
        }
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
    if (!m_index || !m_identity || !QDir(root).exists())
        return landing; // an away root is availability state, never an empty destructive census

    QMap<QString, QString> overrides;
    for (auto it = kindOverrides.constBegin(); it != kindOverrides.constEnd(); ++it)
        overrides.insert(it.key(), it.value().toString());
    // One census implementation owns grouping, actual-kind admission and overrides.
    const auto scan = VaultScanner::buildScan(root, scanIgnore, 0, {}, overrides);
    QStringList userRoots;
    QSet<QString> syntheticIds;
    QSet<QString> syntheticRoots;
    if (m_config) {
        for (const auto& value : m_config->roots()) {
            const auto configured = value.toMap();
            if (configured.value(QStringLiteral("hidden")).toBool()
                || !configured.value(QStringLiteral("confirmed")).toBool()) continue;
            const QString configuredPath = configured.value(QStringLiteral("path")).toString();
            if (configured.value(QStringLiteral("synthetic")).toBool())
                syntheticRoots.insert(normPath(configuredPath));
            else userRoots.append(configuredPath);
        }
    }
    QHash<QString, VaultIndex::FileRow> previous;
    for (const QString& kind : m_index->kinds()) {
        for (const auto& row : m_index->rowsForKind(kind)) {
            previous.insert(row.id, row);
            if (syntheticRoots.contains(normPath(row.rootPath))) syntheticIds.insert(row.id);
            if (!m_config && !userRoots.contains(row.rootPath)) userRoots.append(row.rootPath);
        }
    }
    if (!userRoots.contains(root)) userRoots.append(root);
    QSet<QString> currentIds;
    QList<VaultIndex::FileRow> toUpsert;
    QMap<QString, QMap<QString, QPair<int, QStringList>>> newKind;
    QMap<QString, QString> subtreeByNorm;
    QMap<QString, bool> looseByNorm;
    for (VaultIndex::FileRow row : scan.rows) {
        if (normPath(VaultKit::ownerRootForPath(userRoots, row.path)) != normPath(root)) continue;
        row.id = m_identity->idForFile(row.path, row.size, row.mtimeMs);
        currentIds.insert(row.id);
        if (syntheticIds.contains(row.id)) continue;
        const auto old = previous.constFind(row.id);
        if (old != previous.constEnd() && normPath(old->rootPath) == normPath(root)
            && old->groupKey == row.groupKey && old->kind == row.kind
            && old->groupTitle == row.groupTitle && !old->away)
            continue; // leave all unchanged durable facts byte-for-byte intact
        toUpsert.append(row);
        const QString relative = QDir(root).relativeFilePath(row.path);
        const int slash = relative.indexOf(QLatin1Char('/'));
        const QString subtree = slash < 0 ? root : QDir(root).filePath(relative.left(slash));
        const QString law = lawForSubtree(subtree, kindOverrides);
        const QString normSubtree = normPath(subtree);
        const QString actualKind = VaultKit::kindName(VaultKit::kindForFile(row.path));
        if (old == previous.constEnd() && !law.isEmpty() && law != actualKind) {
            auto& acc = newKind[normSubtree][actualKind];
            ++acc.first;
            if (acc.second.size() < 3) acc.second.append(row.displayTitle);
            subtreeByNorm[normSubtree] = subtree;
            looseByNorm[normSubtree] = slash < 0;
        }
    }

    int removed = 0;
    if (!m_index->reconcileRoot(root, currentIds, toUpsert, &removed))
        return landing; // atomic failure: previous root truth remains intact
    landing.landedCount = toUpsert.size();
    landing.removedCount = removed;

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
