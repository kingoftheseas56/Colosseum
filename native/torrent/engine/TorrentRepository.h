#pragma once

// TorrentRepository — SQLite-backed durable store for the torrent persistence
// substrate.
//
// Owns one SQLite DB file (typically <dataDir>/torrents.db) with four
// coordinated tables (torrents / stream_groups / stream_group_items /
// stream_downloads_index) plus a schema_meta key-value table. Becomes the
// durable authority for "what torrents exist", group lifecycle, and path-keyed
// playback index, replacing torrents.json + stream_bulk_groups.json +
// stream_downloads.json + per-hash .fastresume files.
//
// Runtime torrent_handle maps in TorrentEngine remain as caches projected from
// the repository state.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md
// (Phase 0) and agents/audits/torrent_persistence_collapse_2026-05-19.md for
// the architectural rationale + audit Part B state-machine derivation.

#include "TorrentRow.h"
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <optional>
#include <vector>

namespace tankoban::torrent {

class TorrentRepository : public QObject {
    Q_OBJECT
public:
    // Schema version bumped 2026-05-20 (Phase 3.4.0): stream_downloads_index
    // gained source_group_id + progress_pct columns to absorb the full
    // StreamDownloadIndex::Entry shape ahead of the Phase 3.4 subsystem rebake.
    // initSchema runs ALTER TABLE migrations from each older version to current.
    static constexpr int kSchemaVersion = 2;

    explicit TorrentRepository(QObject* parent = nullptr);
    ~TorrentRepository() override;

    // Lifecycle.
    bool open(const QString& dbFilePath);
    bool initSchema();             // idempotent
    void close();
    bool isOpen() const;

    // Durability / self-heal (TORRENT_DB_DURABILITY 2026-06-02).
    // Probe a SQLite file with PRAGMA quick_check on a throwaway connection.
    // Returns true iff the file opens and reports "ok" (healthy). A malformed
    // file ("database disk image is malformed") returns false. Static + reusable
    // so the manga/book SQLite stores can adopt the same pre-open guard.
    static bool databaseFileIsHealthy(const QString& dbFilePath);

    // Schema meta — schema_version, migration_completed_at, legacy_first_clean_boot_at, etc.
    int schemaVersion();
    void setSchemaVersion(int version);
    QString metaValue(const QString& key);
    void setMetaValue(const QString& key, const QString& value);

    // Diagnostic helper — primarily for tests.
    bool hasTable(const QString& tableName);

    // Transactions.
    bool beginTransaction();
    bool commit();
    bool rollback();

    // torrents CRUD — implemented in P0.5.
    bool upsertTorrent(const TorrentRow& row);
    bool updateTorrentState(const QString& hash, TorrentState state, const QString& errorMessage = QString());
    bool updateTorrentResumeData(const QString& hash, const QByteArray& blob);
    bool updateTorrentName(const QString& hash, const QString& name);
    bool updateTorrentSavePath(const QString& hash, const QString& savePath);
    bool removeTorrent(const QString& hash);
    std::optional<TorrentRow> getTorrent(const QString& hash);
    // Cheaper than getTorrent(...).has_value() — issues SELECT 1 instead
    // of loading + decoding the full TorrentRow. Phase 5 of the
    // persistence-collapse arc swaps all m_records.contains(hash) callsites
    // in TorrentClient.cpp to this predicate.
    bool hasTorrent(const QString& hash);
    std::vector<TorrentRow> listTorrents();
    std::vector<TorrentRow> listTorrentsByState(TorrentState state);
    std::vector<TorrentRow> listTorrentsByImdb(const QString& imdbId, int season);
    std::vector<TorrentRow> listTorrentsByStreamGroup(const QString& streamGroupId);

    // stream_groups CRUD — implemented in P0.6.
    bool upsertStreamGroup(const StreamGroupRow& row);
    bool removeStreamGroup(const QString& groupId);
    std::optional<StreamGroupRow> getStreamGroup(const QString& groupId);
    std::vector<StreamGroupRow> listStreamGroups();
    std::vector<StreamGroupRow> listStreamGroupsByImdbSeason(const QString& imdbId, int season);

    // stream_group_items CRUD — implemented in P0.6.
    bool upsertStreamGroupItem(const StreamGroupItemRow& row);
    bool removeStreamGroupItem(const QString& groupId, const QString& itemId);
    std::vector<StreamGroupItemRow> listStreamGroupItems(const QString& groupId);

    // stream_downloads_index CRUD — implemented in P0.7.
    bool upsertStreamDownload(const StreamDownloadRow& row);
    bool removeStreamDownload(const QString& canonicalPath);
    std::optional<StreamDownloadRow> getStreamDownload(const QString& canonicalPath);
    std::vector<StreamDownloadRow> listStreamDownloads();
    std::vector<StreamDownloadRow> listStreamDownloadsByImdb(const QString& imdbId, int season);

signals:
    // Emitted by open() when a malformed DB was detected, backed up, and
    // replaced with a fresh empty schema. Consumers (the download index)
    // should rebuild-from-disk since persisted state was reset.
    void databaseRecovered();

private:
    // Self-heal a malformed DB at open time: backs the file (+ -wal/-shm
    // sidecars) up to <db>.corrupt-<timestamp>, then leaves the path clear for
    // a fresh open(). Returns true if the backup succeeded. (TORRENT_DB_DURABILITY)
    bool backupCorruptDatabase(const QString& dbFilePath);

    QString m_connectionName;
    QSqlDatabase m_db;
    bool m_open = false;
};

} // namespace tankoban::torrent
