#pragma once
// VaultLibrary — the Vault's single QML façade: the Slice-10 read-model plus the Slice-11
// scan/confirm commands. QML paints from THIS object and fires gestures AT it; C++ owns the
// scan/publish threading and the multi-step confirm sequence, so QML never sequences
// addRoot→scan or setKind→confirm→publish itself (QML paints, C++ decides). It wraps
// VaultIndex (queryable truth), VaultScanner (the cancellable off-thread census + aggregate
// publish), and VaultConfig (user intent: roots + kind overrides). The read half mirrors
// LocalDownloads (revision + series + items) so shelves only render normalized results.
//
// revision bumps ONLY on committed truth (VaultIndex::changed(), emitted after a successful
// publish/upsert). scanning/scanningRoot drive the scan pill; candidate drives the confirmation
// card. Commands: addFolder (add an unconfirmed root + census it), confirmRoot (persist the
// card's chip reassignments, mark confirmed, publish the UNION of all confirmed roots),
// dismissCard, cancelScan.

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class VaultIndex;
class VaultScanner;
class VaultConfig;
class VaultDownloadsRoot;
class VaultIdentity;
class VaultWatcher;
class VaultIdentifier;

class VaultLibrary : public QObject {
    Q_OBJECT

    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY changed)
    // ── scan pill ──
    Q_PROPERTY(QString scanningRoot READ scanningRoot NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanDone READ scanDone NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    // ── confirmation card ──
    Q_PROPERTY(QVariantList candidate READ candidate NOTIFY candidateChanged)
    Q_PROPERTY(QString candidateRoot READ candidateRoot NOTIFY candidateChanged)
    Q_PROPERTY(bool cardVisible READ cardVisible NOTIFY candidateChanged)
    // ── the alive door (Slice 15) ──
    // arrivalTick: a monotone counter bumped on EVERY live-shelf landing (no counts — spec
    // §3); the door binds to it to time-box its "arrival" pulse. immersive: the gate that
    // defers watcher upserts while a reader/player is open (driven from Main.qml).
    Q_PROPERTY(int arrivalTick READ arrivalTick NOTIFY liveArrival)
    Q_PROPERTY(bool immersive READ immersive WRITE setImmersive NOTIFY immersiveChanged)
    Q_PROPERTY(QVariantList identityCeremonies READ identityCeremonies NOTIFY identityCeremoniesChanged)

public:
    explicit VaultLibrary(VaultIndex* index, VaultScanner* scanner, VaultConfig* config,
                          VaultIdentity* identity, QObject* parent = nullptr);

    int revision() const { return m_revision; }
    bool scanning() const { return m_scanning; }
    int itemCount() const;

    // Slice 18 — wire the synthetic downloads root. `path` is the synthetic root's
    // normalized path (stamped on every derived row + the config marker); `root`
    // derives the container-download rows from the backbones. Either may be null
    // (fresh runs / tests pass null → the synthetic root is a no-op).
    void setDownloadsRoot(VaultDownloadsRoot* root, const QString& path);

    QString scanningRoot() const { return m_scanningRoot; }
    int scanDone() const { return m_scanDone; }
    int scanTotal() const { return m_scanTotal; }

    QVariantList candidate() const { return m_candidate; }
    QString candidateRoot() const { return m_candidateRoot; }
    bool cardVisible() const { return !m_candidate.isEmpty(); }

    // Alive-door facts (Slice 15). arrivalTick increments on each watcher landing so QML's
    // door pulse fires even when two landings race (a pure NOTIFY without a value change
    // would not re-evaluate the door's binding).
    int arrivalTick() const { return m_arrivalTick; }
    bool immersive() const;
    void setImmersive(bool on);

    // Confirmed-root count for the marquee "· N folders" (revision-driven: a confirm publishes).
    // Includes the synthetic downloads root when it is present and not hidden.
    Q_INVOKABLE int rootCount() const;
    // The synthetic downloads root's normalized path, or "" when no synthetic root
    // is wired. QML uses this to flag the downloads chip as muted (the trusted root).
    Q_INVOKABLE QString downloadsRootPath() const { return m_downloadsRootPath; }

    // series(kind): normalization of VaultIndex::groupsForKind → { key, title, kind, count,
    // subtreePath }. items(kind, seriesKey): VaultIndex::filesInSubtree, facts preserved.
    Q_INVOKABLE QVariantList series(const QString& kind) const;
    Q_INVOKABLE QVariantList items(const QString& kind, const QString& seriesKey) const;
    Q_INVOKABLE QVariantList hiddenSeries() const;

    // Slice 17 identity actions. The index owns the reversible decoration; this façade owns
    // the QML-facing commands and the persistent hide surface.
    void setIdentifier(VaultIdentifier* identifier);
    Q_INVOKABLE bool identifyGroup(const QString& groupKey);
    Q_INVOKABLE bool identifyGroupWith(const QString& groupKey,
                                       const QVariantMap& chosenIdentity);
    Q_INVOKABLE bool unidentifyGroup(const QString& groupKey);
    Q_INVOKABLE bool reshelveGroup(const QString& groupKey, const QString& kind);
    Q_INVOKABLE bool hideGroup(const QString& groupKey);
    Q_INVOKABLE bool restoreGroup(const QString& groupKey);
    Q_INVOKABLE bool enrichIdentity(const QString& groupKey, const QString& synopsis,
                                    const QString& coverUrl);
    Q_INVOKABLE QVariantList identityCeremonies() const;
    Q_INVOKABLE bool decideIdentityCeremony(const QString& relationship, const QString& choice);

    // Read-only { id -> admissionVerdict } projection for the Vault Continue gate. Re-read from QML
    // through the revision clock (a publish/upsert bumps it); exposes no VaultIndex mutation.
    Q_INVOKABLE QVariantMap admissionById() const;

    // ── commands (C++ owns the multi-step sequences) ──
    // Add a folder as an UNCONFIRMED root and census it off-thread; scanFinished raises the card.
    Q_INVOKABLE void addFolder(const QString& pathOrUrl);
    // Called when the Vault opens: if a root was added but never confirmed (a picked-then-
    // abandoned folder, or a crash mid-ceremony), resume its founding card — but only ONCE
    // per app run, so dismissing it and reopening the Vault the same session does not nag.
    // Slice 18: also ensures the synthetic downloads root is present if downloads exist
    // (pre-confirmed, no card) and publishes it alongside any confirmed user roots.
    Q_INVOKABLE void offerUnconfirmedRoots();
    // Slice 18 — the downloads chip's remove action: hides the synthetic root (config
    // flag) and re-publishes WITHOUT its rows. The files + transfer history on the
    // Downloads page are untouched; setRootHidden(false) restores it.
    Q_INVOKABLE void removeDownloadsRoot();
    // Confirm the candidate root: persist the card's chip reassignments (subtreePath → kind),
    // mark the root confirmed, then re-census + publish the UNION of ALL confirmed roots.
    Q_INVOKABLE void confirmRoot(const QString& root, const QVariantMap& kindOverrides);
    // Not now — drop the candidate card; the root stays added-but-unconfirmed.
    Q_INVOKABLE void dismissCard();
    // Cancel an in-flight census.
    Q_INVOKABLE void cancelScan();
    // Reveal a Vault folder (or file) in the OS file manager. A directory opens; a file is
    // selected in its parent. Windows-only for now; returns false if the path is gone.
    Q_INVOKABLE bool revealInExplorer(const QString& path) const;
    // Watcher-failure fallback (Slice 15): silently rescan any confirmed root whose watcher
    // is degraded, the next time the Vault opens. Publishes the UNION (never one root alone).
    Q_INVOKABLE void rescanDegradedRoots();
    // ── watcher → door/card wiring (Slice 15) ──
    void onWatcherLanded(int count);
    void onWatcherNewKind(const QString& root, const QVariantList& slices);
    void onRootAvailabilityChanged(const QString& root, bool available);

signals:
    void changed();
    void scanningChanged();
    void scanProgressChanged();
    void candidateChanged();
    // A live-shelf landing (Slice 15) — the door's "arrival" pulse trigger. No payload
    // beyond arrivalTick (spec §3: no counts on the door).
    void liveArrival();
    void immersiveChanged();
    void identityCeremoniesChanged();

private:
    void setScanning(bool scanning);
    // Kick off an off-thread census of an already-added root and clear any stale candidate
    // (shared by addFolder and offerUnconfirmedRoots).
    void beginCensus(const QString& path);
    // Slice 18 — publish the UNION of all confirmed roots INCLUDING the synthetic
    // downloads root (when present + not hidden). Shared by confirmRoot,
    // offerUnconfirmedRoots, and removeDownloadsRoot so every publish path folds
    // the synthetic rows in consistently.
    void publishAllConfirmed();
    // Slice 18 — add the synthetic downloads root to config (idempotent) when
    // downloads exist and it isn't already present.
    void ensureDownloadsRoot();
    // A returned root stays away until a successful aggregate publish has replaced its rows with
    // a fresh census. This prevents stale paths becoming clickable during an active/failed scan.
    void maybePublishPendingRevives();
    void scheduleAutoIdentify();
    void runAutoIdentifySlice();

    VaultIndex* m_index = nullptr;
    VaultScanner* m_scanner = nullptr;
    VaultConfig* m_config = nullptr;
    VaultIdentity* m_identity = nullptr; // shared local identity mechanism for launch + Vault
    VaultWatcher* m_watcher = nullptr; // owns the per-root QFileSystemWatcher + debounce
    VaultIdentifier* m_identifier = nullptr; // non-owning; constructed after catalogues in main
    VaultDownloadsRoot* m_downloadsRoot = nullptr;
    QString m_downloadsRootPath;
    int m_revision = 0;
    int m_arrivalTick = 0; // bumped on every live-shelf landing (the door's pulse clock)
    bool m_scanning = false;
    QString m_scanningRoot;
    int m_scanDone = 0;
    int m_scanTotal = 0;
    QVariantList m_candidate;
    QString m_candidateRoot;
    QSet<QString> m_offeredThisRun; // normalized roots whose card we already raised this launch
    QSet<QString> m_pendingReviveRoots;
    bool m_revivalRescanInFlight = false;
    bool m_autoIdentifyScheduled = false;
    bool m_autoIdentifyDirty = false;
    bool m_autoIdentifyKeysReady = false;
    int m_autoIdentifyCursor = 0;
    QStringList m_autoIdentifyKeys;
};
