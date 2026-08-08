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

    // ── Queries the UI needs ──
    Q_INVOKABLE int itemCount() const;
    Q_INVOKABLE int itemCountForKind(const QString& kind) const;
    Q_INVOKABLE QStringList kinds() const;                        // distinct, sorted
    Q_INVOKABLE QVariantList groupsForKind(const QString& kind) const; // [{groupKey,subtreePath,groupTitle,kind,count}]
    Q_INVOKABLE QVariantList filesInSubtree(const QString& subtreePath) const; // natural order, grouped by subfolder

    // Numeric-aware sort key: lexicographic order of the key reproduces
    // QCollator numeric order ("vol 2" before "vol 10"). Pure + static.
    static QString naturalSortKey(const QString& s);

signals:
    void changed();

private:
    void ensureSchema();
    bool insertRow(const FileRow& row); // prepared INSERT OR REPLACE

    QString m_conn;
    QSqlDatabase m_db;
};
