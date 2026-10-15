#pragma once

// TorrentRow + supporting POD structs + TorrentState enum.
//
// Shared row types used by:
//   - core/torrent/TorrentRepository (SQLite-backed durable store)
//   - core/torrent/LegacyImporter (one-shot migration from torrents.json +
//     stream_bulk_groups.json + stream_downloads.json + .fastresume files)
//   - core/torrent/TorrentClient (callers that produce or consume rows)
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md
// (Phase 0 / Task 0.2) and agents/audits/torrent_persistence_collapse_2026-05-19.md
// for the architectural rationale.

#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <optional>

namespace tankoban::torrent {

// Eight-state lifecycle enum used by the torrents.state column.
//
// metadata_probe is RUNTIME-ONLY and never persisted to SQLite — it represents
// transient engine draft handles (resolveMetadata previews / dialog probes /
// pack-verifier work) that must not leak into the durable store.
enum class TorrentState {
    MetadataProbe,      // runtime-only; not persisted
    PendingEngineAdd,   // durable row written; engine confirmation pending
    Active,             // engine confirmed; downloading or queued
    Paused,             // user-paused; engine handle still present
    Completed,          // finished; may be seeding
    Error,              // engine add failed or libtorrent reported error
    RemovePending,      // tombstoned; engine remove issued
    Removed             // terminal; awaiting vacuum
};

inline QString torrentStateToString(TorrentState s) {
    switch (s) {
        case TorrentState::MetadataProbe:    return QStringLiteral("metadata_probe");
        case TorrentState::PendingEngineAdd: return QStringLiteral("pending_engine_add");
        case TorrentState::Active:           return QStringLiteral("active");
        case TorrentState::Paused:           return QStringLiteral("paused");
        case TorrentState::Completed:        return QStringLiteral("completed");
        case TorrentState::Error:            return QStringLiteral("error");
        case TorrentState::RemovePending:    return QStringLiteral("remove_pending");
        case TorrentState::Removed:          return QStringLiteral("removed");
    }
    return QStringLiteral("active");  // unreachable; defensive default
}

inline std::optional<TorrentState> torrentStateFromString(const QString& s) {
    if (s == QStringLiteral("metadata_probe"))    return TorrentState::MetadataProbe;
    if (s == QStringLiteral("pending_engine_add")) return TorrentState::PendingEngineAdd;
    if (s == QStringLiteral("active"))            return TorrentState::Active;
    if (s == QStringLiteral("paused"))            return TorrentState::Paused;
    if (s == QStringLiteral("completed"))         return TorrentState::Completed;
    if (s == QStringLiteral("error"))             return TorrentState::Error;
    if (s == QStringLiteral("remove_pending"))    return TorrentState::RemovePending;
    if (s == QStringLiteral("removed"))           return TorrentState::Removed;
    return std::nullopt;
}

// One row per torrent in the durable store. PK is `hash` (lowercase canonical
// info-hash).
struct TorrentRow {
    QString hash;
    TorrentState state = TorrentState::PendingEngineAdd;
    QString name;
    QDateTime addedAt;
    QString category;
    QString savePath;
    QString contentLayout;
    QString streamGroupId;
    bool sequential = false;
    QString imdbId;
    int season = 0;
    QString magnetUri;
    bool legacyNoMagnet = false;
    QString errorMessage;
    QByteArray resumeData;
    QByteArray torrentFile;
};

// One row per stream cohort (Stream-mode season grouping). PK is `groupId`.
// Holds group-level lifecycle distinct from individual torrent rows.
struct StreamGroupRow {
    QString groupId;
    QString imdbId;
    int season = 0;
    QString label;
    QString state;
    int retryGeneration = 0;
    QString stagingPath;
    QDateTime createdAt;
    bool packMode = false;
};

// Per-episode item within a stream group. Composite PK (groupId, itemId).
// infoHash FKs into TorrentRow when the item is realized as a torrent;
// remains empty for pending/cancelled items.
struct StreamGroupItemRow {
    QString groupId;
    QString itemId;
    int episode = 0;
    QString infoHash;
    QString state;
    QString errorMessage;
    int fileIndex = -1;
};

// Path-keyed playback index. Answers "what file plays this IMDb/season/episode"
// — a different question from "what torrents exist", which is why it lives in
// its own table.
//
// Phase 3.4.0 (2026-05-20) — schema bumped v1 → v2: added sourceGroupId +
// progressPct fields to absorb StreamDownloadIndex::Entry's full shape so the
// Phase 3.4 cutover doesn't lose evictBySourceGroup semantics or post-restart
// progress display state. Default '' / 0 keep existing v1 rows valid post-
// ALTER TABLE migration.
struct StreamDownloadRow {
    QString canonicalPath;
    QString imdbId;
    int season = 0;
    int episode = 0;
    QString state;
    QString infoHash;
    QDateTime addedAt;
    QString sourceGroupId;  // bulk-cohort group id; empty for migration-rescued movies
    int progressPct = 0;    // 0-100; persisted offline display when engine has no live status
};

} // namespace tankoban::torrent
