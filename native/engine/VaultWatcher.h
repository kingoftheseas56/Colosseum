#pragma once
// VaultWatcher — the live-shelf arrival engine (execution plan Slice 15). Watches every
// CONFIRMED Vault root with a QFileSystemWatcher (the only other in-repo use is the dev QML
// reloader in main.cpp — the reference pattern), debounces arrival storms into one pass, and
// turns what changed into incremental VaultIndex upserts: a file dropped into a watched root
// lands on the right shelf within seconds — no card, no action.
//
// One debounced pass over a dirty root = processRoot(): enumerate the root under the SAME
// VaultKit laws the census uses (groupByFirstLevelSubdir + kindForFile + scanIgnore needles),
// build rows byte-identical to VaultScanner::buildScan's, diff against the ids already in the
// index, then reconcile arrivals + removals atomically while leaving unchanged rows intact, and
// law-check every arrival: a file whose kind disagrees with the subtree's law (a config chip
// override first, else the index's dominant kind) is a NEW-KIND arrival → a one-slice
// confirmation card (S11 law) so the user can chip-reshelve it. Watcher failure (QFSW limits,
// network drives, vanished roots) sets a per-root degraded flag; the Vault rescan-on-open
// path (VaultLibrary::rescanDegradedRoots) covers those roots silently.
//
// Behavior to preserve: while an immersive surface (player/reader) is open, the debounce may
// keep accumulating but NO upsert/repaint happens — processRoot defers until setImmersive(false)
// flushes. Row construction must stay in lockstep with buildScan so a later full rescan
// reproduces the same rows (the index is a rebuildable product).
//
// Testability: processRoot() is the synchronous seam — a Qt Test drives it directly with a
// QTemporaryDir root (touch files → exact upsert set; new-kind → card slices; a nonexistent
// root → degraded flag via refresh()).

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QMap>

class QFileSystemWatcher;
class QTimer;
class VaultConfig;
class VaultIdentity;
class VaultIndex;

class VaultWatcher : public QObject
{
    Q_OBJECT

    // Immersive gate (Slice 15 "behavior to preserve"): while true, arrivals keep the debounce
    // dirty set but never reach the index until setImmersive(false) flushes. Driven from QML's
    // `immersiveSurfaceOpen` through VaultLibrary.
    Q_PROPERTY(bool immersive READ immersive WRITE setImmersive NOTIFY immersiveChanged)

public:
    explicit VaultWatcher(VaultIndex* index, VaultIdentity* identity, VaultConfig* config,
                          QObject* parent = nullptr);

    bool immersive() const { return m_immersive; }
    void setImmersive(bool on);

    // Reconcile the watch set with the config's CONFIRMED roots; re-arms vanished roots.
    // A root that cannot be watched (does not exist, QFSW limits, network drive) is marked
    // degraded — it stays until a later refresh() succeeds or the rescan-on-open covers it.
    Q_INVOKABLE void refresh();

    // Per-root degraded flag (the plan's watcher-failure fallback trigger).
    Q_INVOKABLE bool isRootDegraded(const QString& rootPath) const;

    // ── The synchronous arrival seam (the testable core) ──
    struct Landing {
        int landedCount = 0;          // rows actually upserted (the exact arrival set)
        int removedCount = 0;         // indexed physical rows no longer present in this healthy root
        QVariantList newKindSlices;   // one-slice card model per subtree with a new-kind arrival
    };
    // Enumerate `root`, diff against the index, upsert the arrivals, and law-check them.
    // Pure-ish: synchronous, no timers, no watcher state — the Qt Test drives it directly.
    // kindOverrides keys are NORMALIZED subtree paths (VaultConfig::kindOverrides snapshot).
    Landing processRoot(const QString& root, const QStringList& scanIgnore,
                        const QVariantMap& kindOverrides);

signals:
    void landed(int count);                                  // a non-empty upsert batch landed
    void newKindArrival(const QString& root, const QVariantList& slices); // card model, S11 law
    // Root availability is a state transition, not a destructive scan result. The library keeps
    // rows in place while false and clears their away flag when true.
    void rootAvailabilityChanged(const QString& root, bool available);
    // Internal reconciliation signal used by deterministic tests and diagnostics. The walk is
    // off-thread; registration and this signal return to the watcher owner thread.
    void watchTreeReconciled(const QString& root, int directoryCount, bool complete);
    void immersiveChanged();

private slots:
    void onDirectoryChanged(const QString& path);
    void debounceExpired();
    void flushPending();

private:
    void watchRoot(const QString& root);
    void scheduleTreeWatch(const QString& root, bool replayIfInFlight = false);
    bool addDirectoryWatch(const QString& path);
    static QString normPath(const QString& p);
    // The subtree's law: config chip override → index dominant kind → "" (a brand-new subtree's
    // first file IS its own law, so it can never be a new-kind arrival).
    QString lawForSubtree(const QString& subtree,
                          const QVariantMap& kindOverrides) const;

    VaultIndex* m_index = nullptr;
    VaultIdentity* m_identity = nullptr;
    VaultConfig* m_config = nullptr;
    QFileSystemWatcher* m_watcher = nullptr; // heap: QFileSystemWatcher needs no Q_OBJECT here
    QTimer* m_debounce = nullptr;
    QTimer* m_probe = nullptr;       // cheap root-exists probe so a replug revives in-place
    QSet<QString> m_dirty;     // roots with unprocessed changes (normalized)
    QSet<QString> m_degraded;  // normalized roots whose watch/tree registration failed
    QSet<QString> m_treeDegraded; // normalized roots that exceeded the recursive watch budget
    QSet<QString> m_unavailable; // normalized roots whose filesystem root is absent
    QSet<QString> m_watched;   // normalized roots with a live root watch
    QSet<QString> m_treeScansInFlight; // normalized roots with an off-thread directory walk
    QSet<QString> m_treeRescanRequested; // changes observed while a tree walk was in flight
    QSet<QString> m_treeInitialized; // roots whose recursive registration has already been attempted
    bool m_immersive = false;
};
