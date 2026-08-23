#pragma once

// Arc 18 M3 — the durable identity half of Torrentio-style volume sync.
//
// Plainly: a small SQLite cache that remembers, per canonical volumeId, WHICH
// EXACT FILE inside WHICH EXACT TORRENT was proven to be that volume — plus
// mutable swarm health and search freshness. Identity is immutable once
// verified (contract §5): seeders, provider visibility and outages may go
// stale, but a swarm dying never makes the file identity false, and NOTHING in
// this store deletes a verified mapping except an explicit runtime
// revalidation verdict (M6 marks status needs_revalidation).
//
// Schema follows guides/VOLUME-IDENTITY-AND-INDEX-CONTRACT.md §4:
//   torrents        — provider row per infoHash (availability fields mutable)
//   files           — full metainfo file list per infoHash, raw engine indices
//   volume_mappings — canonical volumeId -> infoHash + fileIndex + evidence
//                     + parser_version + status (the product truth)
//   search_state    — per search-key freshness: a successful EMPTY result may
//                     carry a short negative TTL, a provider ERROR may not
//                     (contract: the two classes must stay distinguishable).
//
// Conventions mirror TorrentRepository: one unique QtSql connection per store
// instance (pointer-stamped name), bound SQL values everywhere, WAL +
// synchronous=NORMAL, IF NOT EXISTS schema.

#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

namespace MangaTankoban {

// Where a mapping's truth came from (contract §3 evidence ladder). Numeric
// order IS the ladder — do not reorder.
enum class MappingEvidence {
    ReleaseOnly = 0,          // Nyaa title claims coverage — display candidate only
    MetainfoExactFilename,    // one archive's FILENAME is the exact volume
    MetainfoExactDirectory,   // exact volume via unambiguous parent directory
    RuntimeRevalidated,       // live TorrentEngine metadata re-confirmed it
};

// Lifecycle of a mapping row. Only MetainfoExact*/RuntimeRevalidated rows are
// ever `verified`; rejected_* are diagnostics and can never be promoted
// without new evidence (they carry no file truth at all).
enum class MappingStatus {
    Candidate = 0,
    Verified,
    RejectedAmbiguous,
    RejectedCombined,
    RejectedMissing,
    NeedsRevalidation,        // M6: runtime metadata contradicted the row
};

// Why the last search attempt ended — search_state.error_class. `Empty` is a
// SUCCESSFUL provider answer of "nothing here" (negative-TTL eligible);
// `ProviderError` is a network/provider failure (never negative-cached).
enum class SearchErrorClass { None = 0, Empty, ProviderError };

struct IndexedTorrent {
    QString infoHash;      // 40-char lowercase hex (PK)
    QString provider = QStringLiteral("nyaa");
    QString providerId;    // provider-local id (e.g. Nyaa view id) when known
    QString releaseTitle;
    QString uploader;
    qint64  totalSize = 0;
    int     seeders = 0;   // mutable availability — refreshes never touch identity
    int     leechers = 0;
    QString trackersJson = QStringLiteral("[]");
    qint64  discoveredAt = 0; // epoch ms
    qint64  lastSeenAt = 0;   // epoch ms
};

struct IndexedFile {
    int     fileIndex = 0;   // RAW engine index — mirrors TorrentEngine metadata
    QString path;
    qint64  size = 0;
    QString archiveType;     // "cbz"/"cbr"/… or empty for non-archives
    // Coverage as parsed by MangaVolumeIdentity at index time, kept for
    // diagnostics and batch reasoning — never a substitute for the mapping row.
    QString coverageKind = QStringLiteral("none");
    QString coverageLo;
    QString coverageHi;
    QString coverageSource = QStringLiteral("none");
};

struct VolumeMapping {
    QString volumeId;        // MangaTankoban::volumeId(seriesId, canonicalVolume)
    QString infoHash;
    int     fileIndex = 0;
    MappingEvidence evidence = MappingEvidence::ReleaseOnly;
    qint64  verifiedAt = 0;  // epoch ms of the evidence that earned the status
    QString parserVersion;   // grammar version that produced the identity
    MappingStatus status = MappingStatus::Candidate;
};

struct IndexedSearchState {
    qint64 lastSuccessAt = 0;
    qint64 lastAttemptAt = 0;
    qint64 expiresAt = 0;    // 0 = no negative TTL (also the case on error)
    int    resultCount = 0;
    SearchErrorClass errorClass = SearchErrorClass::None;
};

class MangaTorrentIndex : public QObject
{
    Q_OBJECT
public:
    // Bumped when the identity grammar changes materially (contract §8): old
    // mappings stay readable but become revalidation candidates.
    static const char* const kParserVersion;

    // A successful empty answer is cached this long before the same key is
    // searched again. Provider errors get NO such TTL.
    static constexpr qint64 kNegativeTtlMs = 15 * 60 * 1000;

    explicit MangaTorrentIndex(QObject* parent = nullptr);
    ~MangaTorrentIndex() override;

    bool open(const QString& dbFilePath);
    void close();
    bool isOpen() const;

    // ── torrents (availability refresh; identity columns never rewritten) ──
    // INSERT or refresh mutable fields. Requires only infoHash; empty fields
    // do not overwrite non-empty stored ones on refresh.
    bool upsertTorrent(const IndexedTorrent& torrent);
    // The stored provider row for a hash (empty infoHash when absent) — source
    // cards render its availability snapshot.
    IndexedTorrent torrentRow(const QString& infoHash) const;

    // ── files (full raw file list per inspection pass) ──
    // Replaces the stored list for this infoHash in one transaction. Call
    // after upsertTorrent (files carry an FK to torrents).
    bool replaceFiles(const QString& infoHash, const QList<IndexedFile>& files);
    QList<IndexedFile> filesForTorrent(const QString& infoHash) const;

    // ── mappings (the canonical product truth) ──
    bool insertMapping(const VolumeMapping& mapping);
    bool updateMappingStatus(const QString& volumeId, const QString& infoHash,
                             int fileIndex, MappingStatus status, qint64 atMs);
    // All mappings for a volume, verified-first then best-evidence-first.
    QList<VolumeMapping> mappingsForVolume(const QString& volumeId) const;
    // All mappings under one infoHash — M7 batch set-membership truth.
    QList<VolumeMapping> mappingsForTorrent(const QString& infoHash) const;

    // ── search_state ──
    // A successful provider answer (count may be 0 → negative-TTL'd 'empty').
    bool recordSearchSuccess(const QString& searchKey, int resultCount, qint64 atMs);
    // A network/provider failure — records the attempt, sets NO TTL, and
    // touches NO mapping rows (outage must never delete identity).
    bool recordSearchError(const QString& searchKey, qint64 atMs);
    IndexedSearchState searchState(const QString& searchKey) const;
    // True while a recorded successful-empty answer is still within its TTL.
    bool searchNegativeCached(const QString& searchKey, qint64 nowMs) const;

private:
    bool initSchema();

    QString m_connectionName;
    QSqlDatabase m_db;
    bool m_open = false;
};

} // namespace MangaTankoban
