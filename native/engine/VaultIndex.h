#pragma once
// VaultIndex — the rebuildable scan product (Slice 3). The queryable truth the
// Vault UI paints from: every discovered file with its kind, group (series/show
// folder), real name, and a numeric-aware sort key, plus the counts and folder
// listings the shelves and folder view need. It is a PRODUCT of scanning, never
// user intent — that half is VaultConfig (Slice 2); the config/index separation
// is the Groundworks contract.
//
// SQLite at a caller-given path (<appdata>/vault/index-v1.sqlite in production,
// a QTemporaryDir file in tests). A full publish() replaces the whole index in
// ONE transaction, so a crash or a cancelled scan rolls back and leaves the
// previous contents intact (decision 4) — the atomic-publish discipline of
// BiblioCatalogStore, reduced to a single transactional replace since the Vault
// index keeps no history. upsert() lands a single live-shelf arrival (Slice 15)
// without a full republish.
//
// Deploy note (ledger): a Qt SQL harness finds qsqlite.dll only when it runs
// from build-msvc/ where the app deployed it — the test target lands there.

#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace VaultKit { struct CancellationToken; }

class VaultIndex : public QObject
{
    Q_OBJECT

public:
    // One row of the index — a single file with its group + census facts. The
    // enrichment fields (pages/duration/author/format) are filled later (Slice 5)
    // and stay at their sentinel until then.
    struct FileRow {
        QString id;           // vault:<sha1> (VaultIdentity)
        QString rootPath;
        QString subtreePath;  // the series/show folder (or the root, for loose)
        QString groupKey;     // group identity (usually == subtreePath)
        QString groupTitle;   // cleaned series/show name (census sample title)
        QString kind;         // comic/book/video
        QString path;         // absolute file path
        QString displayTitle; // cleaned file title
        QString realName;     // basename, as it sits on disk
        QString subfolder;    // real subfolder within the subtree ("Season 01"), or ""
        QString sortKey;      // natural-order key; computed from realName if empty
        qint64 size = 0;
        qint64 mtimeMs = 0;
        int pages = -1;
        double durationSec = -1;
        QString author;
        QString format;
        QString synopsis;       // embedded book description, when available
        QString metadataSource; // "EPUB" for embedded EPUB facts; empty otherwise
        QString identityId;     // adopted catalogue/source id, e.g. mal:123 or imdb:tt...
        QString identityTitle;  // user-facing title supplied by a certain identity
        QString identitySource; // MAL / IMDB / COMICS / EPUB
        QString identitySynopsis;
        QString identityCoverUrl;
        QString identityWorld;  // Tankoban / Biblio / Theatre
        int identityYear = 0;
        bool identitySuppressed = false; // explicit Un-identify; blocks auto re-adoption
        // Durable identification-attempt outcome (browse-face execution plan, Slice 2):
        // "" (none) | "ambiguous" | "adopted" | "suppressed". Recorded by VaultIdentifier so a
        // Browse tile can wear "Vault isn't sure" instead of looking merely unscanned.
        // identityCandidateCount is meaningful only when identityState == "ambiguous" (the
        // count of exact catalogue candidates the certainty gate declined to auto-adopt).
        // NOT carried forward by publish()'s identity-carry snapshot (deliberately — Recon
        // Gate 9 / the identity-carry hazard is owned by a different arc); a rescan resets it
        // to "" until VaultIdentifier's auto-identify pass (already scheduled on every index
        // change) re-derives it.
        QString identityState;
        int identityCandidateCount = 0;
        bool progressed = false;
        QString coverRef; // comic: the CBZ entry name for image://comiccover/; else ""
        // A confirmed root can disappear without destroying the user's shelf. `away` keeps the
        // row in place until the root returns; the existing progress key remains untouched.
        bool away = false;
        // Honest per-item failure state. Empty means no extraction/admission error was recorded.
        QString errorState;
        QString errorDetail;
        // Durable media-admission verdict (vault-admission slice). "" = unprobed; a non-empty
        // value is EXACTLY one of Admitted / RejectedNoVideo / RejectedError / RejectedTimeout.
        // Carried across a destructive publish() only when (id,size,mtimeMs) is unchanged.
        QString admissionVerdict;
        QString admissionDetail; // human-readable reason; empty whenever the verdict is empty
    };

    explicit VaultIndex(const QString& dbPath, QObject* parent = nullptr);
    ~VaultIndex() override;

    bool isOpen() const;

    // Full replace in one transaction. Returns false and rolls back (previous
    // contents intact) on any SQL error or if `cancel` fires mid-publish.
    bool publish(const QList<FileRow>& rows,
                 const VaultKit::CancellationToken* cancel = nullptr);

    // Incremental single-file arrival (no full republish).
    bool upsert(const FileRow& row);
    // Batch upsert in ONE transaction, emitting changed() ONCE — enrichment writes many
    // rows back without N read-model repaints. Rolls back on any row error.
    bool upsertMany(const QList<FileRow>& rows);

    // Optimistic async-write guard. Every successful authoritative mutation advances revision().
    // Async enrichment captures the revision it read from and may write back only if no scanner,
    // watcher, away-state or other index mutation superseded that snapshot.
    quint64 revision() const { return m_revision; }
    bool upsertManyIfRevision(const QList<FileRow>& rows, quint64 expectedRevision);

    // Atomically reconcile one healthy, present filesystem root. `currentIds` is the complete
    // on-disk census for that root; `arrivals` contains only rows not already indexed. Obsolete
    // physical rows are deleted in the SAME transaction as arrivals are inserted, while unchanged
    // rows are left byte-for-byte intact so enrichment/progress/identity facts survive.
    bool reconcileRoot(const QString& rootPath, const QSet<QString>& currentIds,
                       const QList<FileRow>& arrivals, int* removedCount = nullptr);

    // Full rows for a kind, in natural order — the enrichment pass reads these on the GUI
    // thread, does its file I/O off-thread, then upsertMany()s the enriched rows back.
    QList<FileRow> rowsForKind(const QString& kind) const;
    QList<FileRow> rowsForRoot(const QString& rootPath) const;
    QList<FileRow> rowsForGroup(const QString& groupKey) const;
    // Exact-path lookup (browse-artwork execution plan, Slice 3 part 2): an Episode/Clip browse
    // node's OWN key/path is the video file itself (VaultKit::planBrowseLevel's loose-video leaf
    // grammar), never a group's folder-shaped groupKey/subtreePath the way a Film node's is — so
    // rowsForGroup()/filesInSubtree() cannot answer "what does the index know about THIS one
    // file" for an episode without first reconstructing groupByFirstLevelSubdir's own grouping
    // rule (immediate-child-of-root, or the "::LOOSE" sentinel) client-side, which risks a subtly
    // wrong re-derivation. `path` is stored exactly as VaultScanner/VaultWatcher wrote it (no
    // normalization at either write or read side, matching every other rowsForX query here) — 0
    // or 1 rows in practice; returns a list only to match the family's own shape.
    QList<FileRow> rowsForPath(const QString& path) const;
    // Every non-suppressed row sharing one adopted canonical identity, across every group/root —
    // the detail sheet's "copies you hold" truth when a film has been identified (browse-face
    // execution plan Slice 7). Empty identityId returns no rows (an unidentified group is never
    // matched to anything by this query).
    QList<FileRow> rowsForIdentity(const QString& identityId) const;
    // Mark all rows under a confirmed root unavailable/available without deleting them.
    // Returns true only when at least one row changed state.
    bool markRootAway(const QString& rootPath, bool away);

    // Live-shelf arrival seams (Slice 15): the ids already shelved under a root, and the
    // dominant kind of a subtree ("" when the subtree has no rows). The watcher uses them to
    // diff arrivals (exact upsert set) and to detect new-kind arrivals against the law.
    QSet<QString> fileIdsInRoot(const QString& rootPath) const;
    QString dominantKindForSubtree(const QString& subtreePath) const;

    // ── Queries the UI needs ──
    Q_INVOKABLE int itemCount() const;
    Q_INVOKABLE int itemCountForKind(const QString& kind) const;
    Q_INVOKABLE QStringList kinds() const;                        // distinct, sorted
    Q_INVOKABLE QVariantList groupsForKind(const QString& kind) const; // [{groupKey,subtreePath,groupTitle,kind,count}]
    Q_INVOKABLE QVariantList filesInSubtree(const QString& subtreePath) const; // natural order, grouped by subfolder
    // Newest-mtime distinct groups across every kind, most-recent first — the Browse face's
    // "recently arrived" truth (mtime is v1's arrival signal; a durable addedAt column is the
    // ownership arc's business). Each row: {groupKey, subtreePath, groupTitle, kind, mtimeMs}.
    Q_INVOKABLE QVariantList recentGroups(int limit) const;

    // (ux uplift S12) Aggregate facts over a whole subtree PREFIX — the exact group itself
    // plus every deeper group beneath it (subtreePath = :p OR subtreePath LIKE :p || '/%').
    // For a node that IS a group, the caller's rowsForGroup() rows already answer newest/
    // size; this is the ancestor-folder/season-node fallback browseAt()'s sort needs (those
    // nodes hold no rows of their own — filesInSubtree is an exact-match query and cannot
    // answer it). Returns false when the prefix has no rows at all (outputs zeroed).
    bool subtreeFacts(const QString& subtreePath,
                      qint64* newestMtimeMs, qint64* totalSizeBytes) const;

    // Narrow read-only projection for QML: { id -> admissionVerdict } over video rows that carry a
    // non-empty durable verdict. Unprobed and non-video rows are omitted. Read-only by construction —
    // QML never sees VaultIndex itself; it re-reads this through the VaultLibrary.revision clock.
    Q_INVOKABLE QVariantMap admissionById() const;

    // Numeric-aware sort key: lexicographic order of the key reproduces
    // QCollator numeric order ("vol 2" before "vol 10"). Pure + static.
    static QString naturalSortKey(const QString& s);

signals:
    void changed();

private:
    // Migrate/verify the Vault-owned schema and stamp PRAGMA user_version. Returns false (and the
    // caller closes the DB) when the file was created by a newer schema owner — never downgrade.
    bool ensureSchema();
    bool insertRow(const FileRow& row); // prepared INSERT OR REPLACE

    QString m_conn;
    QSqlDatabase m_db;
    quint64 m_revision = 0; // process-local mutation clock for stale async-write rejection
};
