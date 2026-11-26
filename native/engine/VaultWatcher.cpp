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
#include <QPromise>
#include <QTimer>
#include <QVariantMap>
#include <QtConcurrent>

namespace {
constexpr int kMaxWatchedDirectoriesPerRoot = 512;

struct ScanActivity {
    std::atomic<int> discovered{0};
    std::atomic<int> processed{0};
    std::atomic<bool> preparing{false};
};
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
    m_scanActivityTimer = new QTimer(this);
    m_scanActivityTimer->setInterval(110);
    m_probe = new QTimer(this);
    m_probe->setInterval(1000); // cheap exists probe; a replug must revive without reopening Vault

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString& path) { onDirectoryChanged(path); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString& path) { onDirectoryChanged(path); });
    connect(m_debounce, &QTimer::timeout, this, [this] { debounceExpired(); });
    connect(m_probe, &QTimer::timeout, this, [this] { refreshInternal(false); });
    if (m_index) {
        connect(m_index, &VaultIndex::personalPathsPrepared, this, [this](bool success) {
            if (success && !m_dirty.isEmpty())
                m_debounce->start();
        });
    }
    m_probe->start();

    // A confirm (or a root added/removed/re-kind-chip) changes the watch set: reconcile.
    if (m_config) {
        connect(m_config, &VaultConfig::changed, this, [this]() { refresh(); });
    }
}

VaultWatcher::~VaultWatcher()
{
    if (m_arrivalCancel) m_arrivalCancel->cancel();
    if (m_treeRegistrationCancel) m_treeRegistrationCancel->cancel();
    if (m_treeThread) {
        m_treeThread->requestInterruption();
        m_treeThread->quit();
        m_treeThread->wait();
    }
}

void VaultWatcher::setPublishing(bool publishing)
{
    if (m_publishing == publishing)
        return;
    m_publishing = publishing;
    if (m_publishing && m_arrivalCancel)
        m_arrivalCancel->cancel();
    if (!m_publishing && !m_dirty.isEmpty())
        m_debounce->start();
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
    const auto personal = m_config->personalPaths();
    const auto needles = m_config->scanIgnore();
    const bool policyChanged = personal != m_personal || needles != m_scanIgnore;
    if (policyChanged) {
        m_personal = personal;
        m_scanIgnore = needles;
        m_treeInitialized.clear();
        if (m_arrivalCancel)
            m_arrivalCancel->cancel();
    }
    const QVariantList roots = m_config->roots();
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
        if (!m.value(QStringLiteral("confirmed")).toBool() || m.value(QStringLiteral("hidden")).toBool()
            || m.value(QStringLiteral("synthetic")).toBool())
            continue;
        const QString path = m.value(QStringLiteral("path")).toString();
        if (path.isEmpty() || personal.excludes(path, true))
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

    for (const QString& watched : m_watcher->directories()) {
        if (!configuredRoots.contains(normPath(watched)) || VaultKit::pathHitsNeedle(watched, needles))
            m_watcher->removePath(watched);
    }
    if (policyChanged || configuredRoots != m_configuredWatchRoots) {
        m_configuredWatchRoots = configuredRoots;
        if (m_treeRegistrationCancel)
            m_treeRegistrationCancel->cancel();
        // Remove forgotten and excluded directories on the same thread that registers them.
        // Queue this only on configuration changes, keeping the availability probe cheap.
        if (m_treeWorker) {
            QMetaObject::invokeMethod(m_treeWorker, [this, roots = configuredRoots.keys(), personal, needles] {
                for (const QString& path : m_treeWatcher->directories()) {
                    if (QThread::currentThread()->isInterruptionRequested())
                        break;
                    if (VaultKit::ownerRootForPath(roots, path).isEmpty()
                        || personal.excludes(path, true) || VaultKit::pathHitsNeedle(path, needles))
                        m_treeWatcher->removePath(path);
                }
            }, Qt::QueuedConnection);
        }
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

    if (!m_treeScansInFlight.isEmpty()) {
        m_treeQueue.insert(norm);
        return;
    }
    m_treeQueue.remove(norm);
    m_treeScansInFlight.insert(norm);
    const auto personal = m_personal;
    const auto needles = m_scanIgnore;
    auto future = QtConcurrent::run([root, personal, needles]() {
        if (!personal.isEmpty() && !VaultKit::isDirectDirectory(QFileInfo(root)))
            return QStringList{};
        QStringList directories{root};
        // Traverse directory entries lazily and prune before looking inside a directory.
        for (qsizetype i = 0; i < directories.size(); ++i) {
            QDirIterator it(directories.at(i), QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            while (it.hasNext()) {
                const QString path = it.next();
                const QFileInfo entry = it.fileInfo();
                if (entry.isJunction() || VaultKit::isIgnoredDir(entry.fileName())
                    || personal.excludes(path, true) || VaultKit::pathHitsNeedle(path, needles))
                    continue;
                if (directories.size() >= kMaxWatchedDirectoriesPerRoot) {
                    // An empty sentinel carries the cap state without a second result type.
                    directories.append(QString());
                    return directories;
                }
                directories.append(path);
            }
        }
        return directories;
    });
    future.then(this, [this, root, norm, personal, needles](QStringList directories) {
        if (!m_configuredWatchRoots.contains(norm) || m_unavailable.contains(norm)) {
            finishTreeWatch(root);
            return;
        }
        if (personal != m_personal || needles != m_scanIgnore) {
            finishTreeWatch(root);
            scheduleTreeWatch(root);
            return;
        }
        if (directories.isEmpty()) {
            m_watcher->removePath(root);
            m_degraded.insert(norm);
            m_treeDegraded.insert(norm);
            finishTreeWatch(root);
            return;
        }
        registerTree(root, std::move(directories));
    });
}

void VaultWatcher::startTreeWatcher()
{
    if (m_treeWorker)
        return;
    m_treeThread = new QThread(this);
    m_treeThread->setObjectName(QStringLiteral("Vault directory watches"));
    m_treeWorker = new QObject;
    m_treeWorker->moveToThread(m_treeThread);
    connect(m_treeThread, &QThread::finished, m_treeWorker, &QObject::deleteLater);
    // On Windows QFileSystemWatcher installs a native event filter in its constructing thread.
    // Construct and destroy it on the worker so that filter shares its event dispatcher.
    QMetaObject::invokeMethod(m_treeWorker, [this, worker = m_treeWorker] {
        m_treeWatcher = new QFileSystemWatcher(worker);
        connect(m_treeWatcher, &QFileSystemWatcher::directoryChanged,
                this, &VaultWatcher::onDirectoryChanged, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    m_treeThread->start();
}

void VaultWatcher::registerTree(const QString& root, QStringList directories)
{
    startTreeWatcher();
    const QString norm = normPath(root);
    const bool capped = directories.constLast().isEmpty();
    if (capped)
        directories.removeLast();
    const qsizetype count = directories.size();
    // The root is already watched by m_watcher. The recursive watcher owns only child paths.
    directories.removeFirst();
    const auto cancel = std::make_shared<VaultKit::CancellationToken>();
    m_treeRegistrationCancel = cancel;
    auto completion = std::make_shared<QPromise<bool>>();
    completion->start();
    completion->future().then(this, [this, root, norm, capped, count, cancel](bool registered) {
        m_treeRegistrationCancel.reset();
        if (!m_configuredWatchRoots.contains(norm) || m_unavailable.contains(norm)) {
            finishTreeWatch(root);
            return;
        }
        if (cancel->isCancelled()) {
            finishTreeWatch(root);
            scheduleTreeWatch(root);
            return;
        }

        // A capped or failed tree can be retried by an explicit refresh or a directory change.
        const bool complete = !capped && registered && m_watched.contains(norm);
        if (complete) {
            m_treeInitialized.insert(norm);
            m_treeDegraded.remove(norm);
            m_degraded.remove(norm);
        } else {
            m_treeInitialized.remove(norm);
            m_treeDegraded.insert(norm);
            m_degraded.insert(norm);
        }
        emit watchTreeReconciled(root, count, complete);
        const bool replay = m_treeRescanRequested.remove(norm);
        finishTreeWatch(root);
        if (replay)
            scheduleTreeWatch(root);
    });

    QMetaObject::invokeMethod(m_treeWorker,
        [this, directories = std::move(directories), cancel, completion] {
            QSet<QString> watched;
            for (const QString& path : m_treeWatcher->directories())
                watched.insert(normPath(path));
            bool registered = true;
            for (const QString& directory : directories) {
                if (cancel->isCancelled()
                    || (!watched.contains(normPath(directory)) && !m_treeWatcher->addPath(directory))) {
                    registered = false;
                    break;
                }
            }
            completion->addResult(registered);
            completion->finish();
        }, Qt::QueuedConnection);
}

void VaultWatcher::finishTreeWatch(const QString& root)
{
    const QString norm = normPath(root);
    m_treeScansInFlight.remove(norm);
    m_treeRescanRequested.remove(norm);
    QTimer::singleShot(0, this, &VaultWatcher::startNextTreeWatch);
    if (m_dirty.contains(norm) && !m_immersive)
        m_debounce->start();
}

void VaultWatcher::startNextTreeWatch()
{
    while (m_treeScansInFlight.isEmpty() && !m_treeQueue.isEmpty()) {
        const QString root = *m_treeQueue.constBegin();
        m_treeQueue.remove(root);
        if (m_config && m_config->isRootConfirmed(root) && !m_personal.excludes(root, true))
            scheduleTreeWatch(root);
    }
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
        if (owner.isEmpty())
            return; // a queued notification can outlive the configured watch
        root = owner;
        if (m_personal.excludes(path, true) || m_personal.excludes(root, true)
            || VaultKit::pathHitsNeedle(path, m_scanIgnore))
            return;
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
    if (m_dirty.isEmpty() || !m_config || !m_index || !m_identity
        || m_arrivalCancel || m_publishing || m_immersive || m_index->isPreparingPersonalPaths())
        return;
    const QStringList needles = m_config->scanIgnore();
    const QVariantMap kinds = m_config->kindOverrides();
    QMap<QString, QString> overrides;
    for (auto it = kinds.constBegin(); it != kinds.constEnd(); ++it)
        overrides.insert(it.key(), it.value().toString());

    QStringList roots;
    for (const auto& value : m_config->roots()) {
        const auto root = value.toMap();
        if (root.value(QStringLiteral("confirmed")).toBool()
            && !root.value(QStringLiteral("hidden")).toBool()
            && !root.value(QStringLiteral("synthetic")).toBool())
            roots.append(root.value(QStringLiteral("path")).toString());
    }

    const QSet<QString> dirty = m_dirty;
    QString root;
    for (const QString& pending : dirty) {
        if (m_treeScansInFlight.contains(pending) || m_treeQueue.contains(pending))
            continue; // the registration callback re-arms the debounce
        m_dirty.remove(pending);
        if (roots.contains(pending) && !m_personal.excludes(pending, true) && QDir(pending).exists()) {
            root = pending;
            break;
        }
    }
    if (root.isEmpty())
        return;
    if (!m_watched.contains(root))
        watchRoot(root);

    auto policy = m_personal;
    policy.files.unite(m_index->personalPaths().files);
    policy.directories.unite(m_index->personalPaths().directories);
    for (const QString& other : roots)
        if (other != root && VaultLocation::containsPath(root, other))
            policy.directories.insert(other);

    const int configRevision = m_config->revision();
    const quint64 indexRevision = m_index->revision();
    auto cancel = std::make_shared<VaultKit::CancellationToken>();
    m_arrivalCancel = cancel;
    const auto activity = std::make_shared<ScanActivity>();
    connect(m_scanActivityTimer, &QTimer::timeout, this, [this, root, activity] {
        const bool preparing = activity->preparing.load();
        const int discovered = activity->discovered.load();
        emit scanActivity(root, preparing ? activity->processed.load() : discovered,
                          preparing ? discovered : 0,
                          preparing ? QStringLiteral("processing") : QStringLiteral("discovering"));
    });
    m_scanActivityTimer->start();
    emit scanActivity(root, 0, 0, QStringLiteral("discovering"));
    auto future = QtConcurrent::run([root, needles, policy, cancel, overrides, activity] {
        return VaultScanner::buildScan(root, needles, 0, cancel, overrides, {}, policy,
                                      [activity](bool preparing, int files) {
            activity->preparing.store(preparing);
            if (preparing)
                activity->processed.store(files);
            else
                activity->discovered.store(files);
        });
    });
    future.then(this, [this, root, kinds, configRevision, indexRevision, cancel](VaultScanner::RawResult scan) {
        m_scanActivityTimer->stop();
        m_scanActivityTimer->disconnect(this);
        m_arrivalCancel.reset();
        if (m_config->isRootConfirmed(root) && !m_personal.excludes(root, true)) {
            if (scan.cancelled || cancel->isCancelled() || m_publishing || m_immersive
                || m_index->isPreparingPersonalPaths()
                || m_config->revision() != configRevision || m_index->revision() != indexRevision) {
                m_dirty.insert(root);
            } else if (scan.scanned && QDir(root).exists()) {
                emit scanActivity(root, scan.rows.size(), scan.rows.size(), QStringLiteral("publishing"));
                const Landing landing = applyRoot(root, kinds, scan.rows);
                if (landing.landedCount > 0)
                    emit landed(landing.landedCount);
                if (!landing.newKindSlices.isEmpty())
                    emit newKindArrival(root, landing.newKindSlices);
            }
        }
        emit scanEnded();
        if (!m_dirty.isEmpty() && !m_publishing && !m_immersive)
            m_debounce->start();
    });

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
    if (!m_index || !m_identity || !QDir(root).exists())
        return {}; // an away root is availability state, never an empty destructive census

    QMap<QString, QString> overrides;
    for (auto it = kindOverrides.constBegin(); it != kindOverrides.constEnd(); ++it)
        overrides.insert(it.key(), it.value().toString());
    // One census implementation owns grouping, actual-kind admission and overrides.
    const auto personal = m_config ? m_config->personalPaths() : VaultLocation::PersonalPaths{};
    const auto scan = VaultScanner::buildScan(root, scanIgnore, 0, {}, overrides, {}, personal);
    return scan.scanned ? applyRoot(root, kindOverrides, scan.rows) : Landing{};
}

VaultWatcher::Landing VaultWatcher::applyRoot(const QString& root, const QVariantMap& kindOverrides,
                                               const QList<VaultIndex::FileRow>& rows)
{
    Landing landing;
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
    for (VaultIndex::FileRow row : rows) {
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
