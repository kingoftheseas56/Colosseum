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
        bool progressed = false;
        QString coverRef; // comic: the CBZ entry name for image://comiccover/; else ""
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

    // Full rows for a kind, in natural order — the enrichment pass reads these on the GUI
    // thread, does its file I/O off-thread, then upsertMany()s the enriched rows back.
    QList<FileRow> rowsForKind(const QString& kind) const;

    // ── Queries the UI needs ──
    Q_INVOKABLE int itemCount() const;
    Q_INVOKABLE int itemCountForKind(const QString& kind) const;
    Q_INVOKABLE QStringList kinds() const;                        // distinct, sorted
    Q_INVOKABLE QVariantList groupsForKind(const QString& kind) const; // [{groupKey,subtreePath,groupTitle,kind,count}]
    Q_INVOKABLE QVariantList filesInSubtree(const QString& subtreePath) const; // natural order, grouped by subfolder

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
};
