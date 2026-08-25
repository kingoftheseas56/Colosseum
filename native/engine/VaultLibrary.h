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
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class VaultIndex;
class VaultScanner;
class VaultConfig;
class VaultDownloadsRoot;
class VaultIdentity;
class VaultWatcher;
class VaultIdentifier;
class VaultThumbnailer;
class VaultPosterFetcher;
class VaultArtworkResolver;

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
    // `cacheDir` (browse-artwork execution plan, Slice 3 part 2) is the SAME VaultStoreIo-managed
    // dir every other Vault store already gets (main.cpp's `vaultDir`, the same one VaultConfig/
    // VaultIdentity/VaultIndex are constructed with) — so every Vault cache (config.json,
    // identity.json, index-v1.sqlite, and now thumbs/ + posters/) lives under one root. VaultLibrary
    // owns a VaultThumbnailer + VaultPosterFetcher + VaultArtworkResolver (parented to `this`) built
    // from it; either producer is harmless to construct against an empty/unwritable dir (its own
    // cache-miss path just never resolves), so `cacheDir` has no separate null/empty contract to
    // document beyond what VaultThumbnailer/VaultPosterFetcher already promise.
    explicit VaultLibrary(VaultIndex* index, VaultScanner* scanner, VaultConfig* config,
                          VaultIdentity* identity, QString cacheDir, QObject* parent = nullptr);

    // One deferred healing publish after all download backbones are wired. The guard is
    // intentionally here, rather than in QML, so an existing stale index is repaired once
    // per process without adding a rescan control to the Vault surface.
    void republishAtBoot();

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

    // ── Browse projection spine (browse-face execution plan, Slice 1) ──
    // browseAt(rootOrPath): the collapsed rows for one folder level — VaultKit::planBrowseLevel
    // for structure (folder-is-one-film, sibling/nested season fold to show, season-presence
    // facts, the episode/clip leaf grammar), decorated here with what today's index already
    // knows: away (VaultIndex row state) and a best-effort identified/resolving/localOnly state
    // for film nodes (a single real file's own group is a reliable lookup; deeper per-episode
    // state and durable uncertainty are Slices 2/6's business, not this one's). coverRef
    // (browse-artwork execution plan, Slice 3 part 2): EVERY node this method returns — Film,
    // Episode/Clip leaves (VaultIndex::rowsForPath — their own path IS the video file, never a
    // group's folder-shaped key the way Film's is), AND Folder/Show/Season containers — is now
    // walked through VaultArtworkResolver::resolve(): local art the row already carries (a video
    // group's adopted "file://" ref, or the comic/book image://comiccover|vaultbookcover/<id>
    // translation) wins outright; failing that, a matched identity's canonical poster
    // (fetched+cached by VaultPosterFetcher) or, video-only, a cached frame-grab (VaultThumbnailer
    // — the locked design's Episode still / Clip real-footage frame). A remote `identityCoverUrl`
    // is consulted ONLY as resolve()'s input — it never lands in coverRef directly. A miss returns
    // "" (typographic fallback) and kicks that producer's async fetch/grab; VaultArtworkResolver::
    // artResolved(rowKey) then fires browseArtResolved(rowKey) (see that signal below) so the
    // caller re-projects the same level. Row shape:
    // {key, nodeType, displayTitle, physicalFact, state, away, counts:{items}, coverRef, path,
    //  kind, id}.
    // `id` (vault ux uplift S6) is the node's durable vault id on Film/Episode/Clip rows — the
    // live Progress join key (VaultApi.joinRow's progressFraction/progressed override and
    // ProgressStore.watchedMark both need it; it is not derivable from `path` in QML). Container
    // rows and away-fallback rows (offlineBrowseAt) carry no id: their tiles render no progress
    // chrome by the cards' own away/precedence rules.
    // `kind` is the STORED comic|book|video the index rows under a node carry (VaultScanner's own
    // per-file classification), never a re-derivation of the structural `nodeType` — QML's identify
    // gesture branches on it to choose IMDb over the comic/manga catalogues, and this projection
    // returning "" for every row is exactly why identifying a film from the browse face used to
    // search comics. Film/Episode/Clip take it from their own group/exact-path rows; a container
    // that IS a group takes its most common row kind; a pure ancestor folder (no rows of its own)
    // and anything not indexed yet honestly stay "".
    // Slice 6: away now flows to EVERY node type, not just Film — a whole level's owning root
    // going away marks every one of its tiles, not just the ones this method already had a
    // per-row lookup for. When the owning root's directory can no longer be walked at all,
    // browseAt() falls back to offlineBrowseAt() so the level's tiles hold position (marked
    // away) instead of the grid reading as empty (design §4.7 "nothing disappears").
    // `sort` (vault ux uplift S12): "natural" (default — planBrowseLevel's locked §4.2 order:
    // folders, then series, then films, each numeric-aware by title) | "title" (ONE merged
    // numeric-aware order across every node — VaultIndex::naturalSortKey, so "Vol 2" < "Vol
    // 10") | "newest" (node's newest row mtimeMs descending) | "size" (node's total row bytes
    // descending). Newest/size keys come from the rows each branch already fetches; a pure
    // ancestor folder or season node (no rows of its own) falls back to VaultIndex::subtreeFacts.
    // Ties break by the natural key ascending (deterministic, never by SQL/walk accident).
    // "Recently played" is deliberately NOT here: its key (lastReadMs) lives in the Progress
    // store, not the index, so QML sorts the joined rows (VaultApi.sortRowsRecentlyPlayed —
    // the VaultFolderView lastread precedent). An unknown sort string reads as "natural".
    Q_INVOKABLE QVariantList browseAt(const QString& rootOrPath,
                                      const QString& sort = QStringLiteral("natural"),
                                      const QVariantMap& filter = {}) const;
    // rootsDetail(): one row per confirmed/synthetic, non-hidden root — {path, name, available,
    // itemCount, fileCount} — the rail's data source once Slice 5 wires it. Vault ux uplift
    // S11 extends each row with the "needs attention" facts: `errorCount` (rows under the root
    // carrying any recorded error state), `errorItems` (capped [{path, reason}] — the plain
    // "path · reason" list the rail's attention panel shows; 8 cap, total stays in errorCount),
    // and `watcherDegraded` (root-watch registration failure OR the recursive-registration
    // 512-dir over-budget class — both state the rescan-on-open consequence).
    Q_INVOKABLE QVariantList rootsDetail() const;
    // recentArrivals(limit): the newest-mtime index groups, most-recent first — the carousel's
    // data source once Slice 5 wires it. Row shape mirrors browseAt's film/show rows.
    Q_INVOKABLE QVariantList recentArrivals(int limit) const;
    // searchLibrary(query, limit = 60) — vault ux uplift S14, the in-vault search: one row per
    // GROUP whose cleaned file title, adopted identity title, or real on-disk filename contains
    // the query (case-insensitive; %/_ in the query are literal — VaultIndex::rowsMatching
    // escapes them). Newest-hit-first; hidden items never surface (the Hidden shelf's own
    // population). Row shape is the FULL browseAt contract (key/nodeType/displayTitle/
    // physicalFact/path/state/away/counts/coverRef/kind/id) so the SAME grid cards render it;
    // physicalFact carries the hit's crumb ("root / subfolder") — the plan's own tagging law —
    // and `id` is the S6 Progress join key so progress/watched chrome survives on search tiles.
    // Multi-select/bulk actions over results are OUT of scope (Phase-4-style ruling pending).
    Q_INVOKABLE QVariantList searchLibrary(const QString& query, int limit = 60) const;
    // seasonFactsForShow(showFolderPath) — vault ux uplift S17: the DERIVED season/episode
    // structure behind one show folder ({seasons:[{season,total,episodes:[{path,id,season,
    // episode,title}]}], total}), natural order within each season, season ordinals via
    // VaultKit::seasonOrdinalFromDirName (planBrowseLevel's own rule). Derivation only: no
    // watched data (the page joins Progress against each episode id), no identity state, no
    // persistence. Empty for a path that holds no rows (an unscanned folder, a sibling-
    // collapsed sentinel key).
    Q_INVOKABLE QVariantMap seasonFactsForShow(const QString& showFolderPath) const;
    // browseDetail(key): the detail sheet's ONE projection (Slice 7) — copies you hold (same
    // canonical identity across roots where identity exists, else the single physical group),
    // companions, extras, evidence, and a best-quality line. `key` is a Film browse-row's own
    // key. See VaultBrowseDetail::detailFor for the returned shape; `{found: false}` on a stale
    // key. Never cast, synopsis, or related titles — the locked design's decision #11.
    Q_INVOKABLE QVariantMap browseDetail(const QString& key) const;
    // Slice 9 — the grid's empty-cause classification (design §4.5's four distinct causes minus
    // the formerly deferred "filtered" — VaultBrowseEmpty.qml always rendered its copy; S13's
    // filter surface is the production trigger: with an ACTIVE filter whose projection is empty
    // while the level HAS rows unfiltered, this returns "filtered". "none" when the level
    // actually has rows. See VaultBrowseEmpty::classify for the pure logic.
    Q_INVOKABLE QString browseEmptyCause(const QString& rootOrPath,
                                         const QVariantMap& filter = {}) const;
    // The owning root's total known file count — the "all away" empty state's one physical fact
    // (design plate 6: "All N items live on a drive that is not connected"). 0 when the path
    // resolves to no confirmed root or that root has no durable rows at all.
    Q_INVOKABLE int browseEmptyAwayCount(const QString& rootOrPath) const;

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
    // ── storage management (vault ux uplift S10) — the rail overflow menu's verbs ──
    // A user-facing rescan for ONE known root (before S10 only confirm/boot/watcher paths
    // ever re-censused). Silent by design — no card rises; the scan pill shows progress.
    // Like every publish path here it re-censuses and publishes the UNION of all confirmed
    // roots (VaultScanner's whole-index replace law), so a rescan of one root can never
    // wipe a sibling root's rows. A path that is not a known, non-hidden, confirmed-or-
    // synthetic root is a no-op (never a scan of an arbitrary folder).
    Q_INVOKABLE void rescanRoot(const QString& path);
    // Remove a USER root entirely: config row gone (VaultConfig::removeRootCompletely(),
    // documented at VaultConfig.h as awaiting exactly this affordance), rows dropped by
    // the union republish that follows. Files on disk are NEVER touched. Forgetting the
    // SYNTHETIC downloads root delegates to removeDownloadsRoot()'s reversible hide — its
    // files belong to the Downloads lane and the config row that remembers that ownership
    // must survive. Other roots' rows, identities, and hidden items are preserved by the
    // union-publish law.
    Q_INVOKABLE void forgetRoot(const QString& path);
    // scanIgnore needles (Groundworks contract) — a case-insensitive substring test every
    // walk already threads through (census, watcher, browse projections, detail). Thin
    // passthroughs so QML never sees VaultConfig itself; setScanIgnore re-publishes so an
    // edit takes effect on the shelves immediately, not on the next unrelated scan.
    Q_INVOKABLE QStringList scanIgnore() const;
    Q_INVOKABLE void setScanIgnore(const QStringList& needles);
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
    // Browse-artwork execution plan, Slice 3 part 2 — the resolver's re-projection hook. Fires
    // when VaultArtworkResolver::artResolved(rowKey) lands a NEWLY-cached poster/frame-grab for a
    // row a prior browseAt()/items()/series() call resolved with only "" (the typographic
    // fallback). Deliberately NARROW: unlike changed() (revision-gated — every property that reads
    // VaultLibrary.revision recomputes, `browseGridRows`/`rootsDetail`/`hiddenSeries`/
    // `admissionById`/`browseDetail`/… alike), this does not bump m_revision or emit changed() — a
    // single frame-grab landing must not force every OTHER revision-gated projection to redo its
    // own SQL/filesystem work too (this subsystem's own §4.5 note above already names that exact
    // doubling hazard for a different call). VaultPage.qml connects this directly to
    // syncGridModel() (the same in-place ListModel diff identify-in-place already reuses), which is
    // the SAME re-projection path browseGridRows' own VaultLibrary.revision dependency drives.
    void browseArtResolved(const QString& rowKey);

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
    // Browse-artwork execution plan, Slice 3 part 2 — owned (parented to `this`), built from the
    // constructor's `cacheDir`. See VaultThumbnailer/VaultPosterFetcher/VaultArtworkResolver for
    // what each does; VaultLibrary's own job is only to build RowFacts per row and hand the ladder
    // resolve()'s answer to browseAt()/items()/series().
    VaultThumbnailer* m_thumbnailer = nullptr;
    VaultPosterFetcher* m_posterFetcher = nullptr;
    VaultArtworkResolver* m_artworkResolver = nullptr;
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
    bool m_bootRepublishDone = false;
    bool m_autoIdentifyScheduled = false;
    bool m_autoIdentifyDirty = false;
    bool m_autoIdentifyKeysReady = false;
    int m_autoIdentifyCursor = 0;
    QStringList m_autoIdentifyKeys;
};
