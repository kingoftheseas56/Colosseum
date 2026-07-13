#pragma once

#include <QObject>
#include <QThread>
#include <QJsonArray>
#include <QMutex>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QVector>
#include <QDateTime>
#include <QStringList>

#ifdef HAS_LIBTORRENT
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#endif

// ── Per-torrent record (thread-safe via m_mutex) ────────────────────────────
struct TorrentRecord {
    QString   infoHash;
    QString   name;
    QString   savePath;
    bool      metadataReady = false;
#ifdef HAS_LIBTORRENT
    lt::torrent_handle handle;
#endif
};

// ── Status snapshot for UI consumption ──────────────────────────────────────
struct TorrentStatus {
    QString infoHash;
    QString name;
    QString savePath;
    QString stateString;  // "downloading", "seeding", "paused", "checking", "metadata"
    float   progress     = 0.f;
    int     downloadRate = 0;
    int     uploadRate   = 0;
    int     numPeers     = 0;
    int     numSeeds     = 0;
    qint64  totalDone    = 0;
    qint64  totalWanted  = 0;
    bool    forceStarted  = false;
    int     queuePosition = -1;
    int     dlLimit       = 0;   // 0 = unlimited, else bytes/s
    int     ulLimit       = 0;
};

// ── Per-tracker snapshot (Phase 6.3) ────────────────────────────────────────
struct TrackerInfo {
    QString   url;
    int       tier = 0;
    QString   status;       // "Working" / "Updating" / "Error" / "Not contacted"
    QDateTime nextAnnounce; // invalid if not contacted
    QDateTime minAnnounce;
    int       peers      = 0;
    int       seeds      = 0;
    int       leeches    = 0;
    int       downloaded = 0;
    QString   message;
};

// ── Per-peer snapshot (Phase 6.4) ───────────────────────────────────────────
struct PeerInfo {
    QString address;
    quint16 port       = 0;
    QString client;
    QString country;   // 2-letter code; "--" when GeoIP unresolved
    QString flags;     // compact "I"/"E"/"uTP" flag string
    QString connection;// "TCP" / "uTP"
    float   progress   = 0.f;
    qint64  downSpeed  = 0;
    qint64  upSpeed    = 0;
    qint64  downloaded = 0;
    qint64  uploaded   = 0;
    float   relevance  = 0.f;
};

// ── Combined metadata + live status for General tab (Phase 6.5) ─────────────
struct TorrentDetails {
    QString   name;
    qint64    totalSize  = 0;
    int       pieceCount = 0;
    qint64    pieceSize  = 0;
    QDateTime created;
    QString   createdBy;
    QString   comment;
    QString   infoHash;
    QString   savePath;
    QString   currentTracker;
    float     availability = 0.f;
    float     shareRatio   = 0.f;
    QDateTime nextReannounce;
};

// ── TorrentEngine ───────────────────────────────────────────────────────────
class TorrentEngine : public QObject
{
    Q_OBJECT

public:
    explicit TorrentEngine(const QString& cacheDir, QObject* parent = nullptr);
    ~TorrentEngine();

    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // Torrent operations (all thread-safe)
    QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused = true);
    QString addFromResume(const QString& resumePath, const QString& savePath, bool paused);
    // EXTERNAL_DELETE_RECONCILE (2026-06-12) — pre-add disk probe for the
    // startup restore loop. Parses a .fastresume WITHOUT touching the session
    // and reports whether the torrent had prior progress and whether any of
    // its files still exist under savePath. TorrentClient uses this to detect
    // "files deleted outside the app" and purge instead of re-download.
    struct ResumeDiskState {
        bool parsed = false;          // resume file existed + read_resume_data ok
        bool hasFileList = false;     // metadata known (file list available)
        bool hadProgress = false;     // any piece downloaded per resume data
        bool anyFilePresent = false;  // >=1 of the torrent's files exists on disk
    };
    ResumeDiskState resumeDataDiskState(const QString& resumePath,
                                        const QString& savePath) const;
    void    setFilePriorities(const QString& infoHash, const QVector<int>& priorities);
    void    renameFile(const QString& infoHash, int fileIndex, const QString& newName);
    void    setSequentialDownload(const QString& infoHash, bool sequential);
    void    flattenFiles(const QString& infoHash);
    void    startTorrent(const QString& infoHash, const QString& newSavePath);
    // Relocate an active/completed torrent's save path. Mirrors qBittorrent's
    // "Set Location" — wraps libtorrent::torrent_handle::move_storage(), which
    // copies+verifies files asynchronously and emits storage_moved_alert on
    // success or storage_moved_failed_alert on failure. The engine's in-memory
    // record's savePath is updated optimistically; consumers should listen on
    // storageMoveFailed and revert their persisted state if the move fails.
    void    moveStorage(const QString& infoHash, const QString& newSavePath);
    void    resumeTorrent(const QString& infoHash);
    void    pauseTorrent(const QString& infoHash);
    void    removeTorrent(const QString& infoHash, bool deleteFiles = false);
    void    forceStart(const QString& infoHash);
    void    forceRecheck(const QString& infoHash);
    void    forceReannounce(const QString& infoHash);
    void    queuePositionUp(const QString& infoHash);
    void    queuePositionDown(const QString& infoHash);
    void    setQueueLimits(int maxDownloads, int maxUploads, int maxActive);
    void    setSpeedLimits(const QString& infoHash, int dlLimitBps, int ulLimitBps);
    void    setGlobalSpeedLimits(int dlLimitBps, int ulLimitBps);
    void    setSeedingRules(const QString& infoHash, float ratioLimit, int seedTimeLimitSecs);
    void    setGlobalSeedingRules(float ratioLimit, int seedTimeLimitSecs);

    // Tracker management (Phase 6.3)
    QList<TrackerInfo> trackersFor(const QString& infoHash) const;
    void    addTracker(const QString& infoHash, const QString& url, int tier);
    void    removeTracker(const QString& infoHash, const QString& url);
    void    editTrackerUrl(const QString& infoHash, const QString& oldUrl, const QString& newUrl);

    // Peer management (Phase 6.4)
    QList<PeerInfo> peersFor(const QString& infoHash) const;
    void    banPeer(const QString& ipAddr);
    void    addPeer(const QString& infoHash, const QString& ipPort);
    QStringList bannedPeers() const;

    // STREAM_ENGINE_REBUILD P3 — per-piece peer availability. Counts peers
    // whose bitfield has `pieceIdx` set. Iterates lt::peer_info::pieces
    // (typed_bitfield<piece_index_t>) from get_peer_info() under m_mutex;
    // O(num_peers), ≤1ms in practice. Fresh-handshake peers (empty bitfield
    // — BITFIELD/HAVE not yet received) are counted as "unknown" and
    // skipped, not counted as "no" — correct semantic for R3 falsification
    // (scheduler-starvation vs. swarm-availability-starvation). Returns -1
    // on unknown infoHash / invalid pieceIdx / metadata-not-ready / no-
    // libtorrent build. Consumed by StreamPrioritizer + SeekClassifier in
    // P3 to distinguish "priority-7 storm will fix it" from "no peers have
    // this piece — any priority is futile" during seek-hang triage.
    // Congress 5 Amendment 1 lifted this from optional-telemetry to hard
    // P3 exit gate — plan's fallback via peer_info.progress conflates
    // aggregate swarm completeness with per-piece availability and cannot
    // disambiguate those two states.
    int peersWithPiece(const QString& infoHash, int pieceIdx) const;

    // STREAM_ENGINE_REBUILD 2026-04-19 — diagnostic projection of libtorrent's
    // internal state for a single piece. Used by StreamEngine::onStallTick
    // to emit a `piece_diag` telemetry event alongside stall_detected so the
    // next iteration has concrete evidence of WHY libtorrent isn't converging
    // on a stuck piece (block-level state + peer-level state). Pure read;
    // additive-only per Congress 5 Amendment 2 API-freeze discipline.
    // `inDownloadQueue=false` means libtorrent hasn't even started requesting
    // blocks of this piece — that would be a scheduler issue further upstream
    // than the 2s-cap / time-critical-queue path explored in the 2026-04-19
    // failed revert.
    struct PieceDiag {
        int blocksInPiece      = 0;
        int finished           = 0;   // blocks fully received + hash-checked
        int writing            = 0;   // blocks on disk-write queue
        int requested          = 0;   // blocks in-flight from a peer
        bool inDownloadQueue   = false;
        int peersWithPiece     = 0;   // same semantic as peersWithPiece()
        int peersDownloadingPiece = 0;// peers where p.downloading_piece_index == pieceIdx
        int avgPeerQueueMs     = 0;   // mean of peer_info.download_queue_time
        int peerCount          = 0;
    };
    PieceDiag pieceDiagnostic(const QString& infoHash, int pieceIdx) const;

    // STREAM metadata investigation Wake 1 (2026-04-21) — pre-metadata
    // diagnostic projection. Observed: Torrentio EZTV magnets take 93-245 s
    // from addMagnet() to metadata_received_alert; post-metadata cold-open
    // is only ~114 ms (Wake 3 deadline shape fix). The 93-245 s is pure
    // libtorrent-layer metadata-fetch latency with zero observability
    // today. This accessor surfaces the pre-metadata phase state so
    // StreamEngine::onMetadataFetchDiagTick can emit 1 Hz telemetry to
    // disambiguate between:
    //   dhtRunning=false, trackersOk=0, peersConnected=0 → bootstrap stuck
    //   dhtRunning=true,  trackersOk=0, peersConnected=0 → DHT only, trackers failing
    //   trackersOk>0, peersConnected=0                   → trackers responded but handshake pending
    //   peersConnected>0 over many ticks (metadata not arriving)
    //                                                     → peers present but not exchanging metadata
    // Pure read; additive per 12-method API freeze. Safe to call at 1 Hz.
    struct MetadataFetchDiag {
        int  peersConnected       = 0;   // handle.status().num_peers — connected peer sockets
        int  swarmSeeds           = 0;   // handle.status().num_complete — tracker-reported
        int  swarmLeechers        = 0;   // handle.status().num_incomplete — tracker-reported
        int  trackersOk           = 0;   // trackers with recent successful scrape/announce
        int  trackersTotal        = 0;   // total tracker count (from handle.trackers())
        bool dhtRunning           = false;   // session::is_dht_running()
        bool announcingToTrackers = false;   // handle.status().announcing_to_trackers
    };
    MetadataFetchDiag metadataFetchDiagnostic(const QString& infoHash) const;

    // General tab convenience wrapper (Phase 6.5)
    TorrentDetails torrentDetails(const QString& infoHash) const;

    // Query (thread-safe snapshot)
    QList<TorrentStatus> allStatuses() const;
    // F9 fix 2026-05-19: returns whether the engine has a record for this hash
    // at all (handle valid OR invalid). Used by TorrentClient::startDownload to
    // detect "magnet was never added" cases before writing a zombie record.
    // Distinct from hasMetadata which gates on metadata_received_alert.
    bool hasTorrent(const QString& infoHash) const;
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — returns true when libtorrent
    // has already received the metadata_received_alert for this infoHash (i.e.
    // TorrentRecord::metadataReady is set). Used by TorrentClient::startDownload
    // to synthesize an onMetadataReady call on duplicate_torrent re-adds, since
    // metadata_received_alert is single-shot per handle and won't re-fire.
    bool hasMetadata(const QString& infoHash) const;
    QJsonArray torrentFiles(const QString& infoHash) const;
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — per-file byte counts for
    // progress tracking. Returns a QJsonArray of qint64 values (one per file
    // in libtorrent's file_storage order) representing bytes downloaded so far.
    // Uses piece_granularity flag so the count updates on every piece_finished
    // alert rather than waiting for file completion. Empty array on unknown hash
    // or no torrent_info yet (metadata still pending).
    QJsonArray torrentFileProgress(const QString& infoHash) const;

    // Check if contiguous bytes [fileOffset .. fileOffset+length) are fully downloaded.
    // Used by StreamEngine to verify the file header is available before handing to player.
    bool haveContiguousBytes(const QString& infoHash, int fileIndex,
                             qint64 fileOffset, qint64 length) const;

    // STREAM_ENGINE_FIX Phase 2.6.1 — per-piece have state. Diagnostic-tier
    // exposure of libtorrent's have_piece() so StreamEngine's seek_target
    // telemetry can report per-piece availability of the seek window. Returns
    // false on unknown infoHash, invalid pieceIdx, or library not built. No
    // behavior change. Agent 4B's pre-offered HELP for Slice A Axis 1
    // (contiguousBytesFromOffset semantics, pieceRangeForFileOffset boundary
    // cases) covers this addition — same const-read shape, same semantic class.
    bool havePiece(const QString& infoHash, int pieceIdx) const;

    // STREAM_ENGINE_FIX Phase 2.6.3 — per-piece priority boost. Exposes
    // libtorrent's set_piece_priority() so prepareSeekTarget can boost seek
    // pieces to priority 7 (max) IN ADDITION to setting a tight deadline.
    // Phase 2.6.1 telemetry proved deadlines alone aren't enough to override
    // libtorrent's general piece selection when many peers are serving
    // varied pieces in parallel; the deadline is one factor, not a hard
    // override. Combining priority + deadline gives seek pieces
    // unambiguous scheduler win. No-op on unknown infoHash, invalid
    // pieceIdx, or library not built. Same Axis 1 territory as havePiece +
    // existing setFilePriorities — Agent 4B pre-offered HELP covers it.
    void setPiecePriority(const QString& infoHash, int pieceIdx, int priority);

    // Flush libtorrent's disk write cache for a specific torrent so all
    // downloaded pieces are immediately readable from disk via QFile.
    void flushCache(const QString& infoHash);

    // ── STREAM_PLAYBACK_FIX Phase 2 — streaming piece primitives ────────────
    //
    // These four helpers translate between file-byte space and piece-index
    // space and let StreamEngine wrap libtorrent's streaming APIs. No
    // streaming behavior change from adding them — callers wired in later
    // batches (2.2 head deadline, 2.3 sliding window, 2.4 seek pre-gate,
    // 3.2 buffering %).

    // Wrap libtorrent's time-critical piece API. `deadlines` is a list of
    // (pieceIndex, msFromNow) pairs — shorter ms = more urgent. Pieces with
    // deadlines are requested from peers expected to deliver soonest, which
    // is the libtorrent-recommended way to stream (sequential_download
    // alone is documented as "sub-optimal for streaming"). Invalid piece
    // indices are skipped silently. No-op on invalid infoHash.
    void setPieceDeadlines(const QString& infoHash,
                           const QList<QPair<int, int>>& deadlines);

    // Clear all time-critical deadlines for this torrent. Used on seek /
    // resume (rebuild a fresh set around the new playback window) and on
    // playback stop (stop pre-fetching ahead of a non-existent frontier).
    void clearPieceDeadlines(const QString& infoHash);

    // Translate a file-byte range to a [firstPiece, lastPiece] inclusive
    // piece-index range using libtorrent's ti->map_file(). Returns
    // {-1,-1} on invalid input. Callers use this to compute head / seek /
    // sliding-window piece ranges from file offsets.
    QPair<int, int> pieceRangeForFileOffset(const QString& infoHash,
                                            int fileIndex,
                                            qint64 fileOffset,
                                            qint64 length) const;

    // Count contiguous have_piece()-true bytes starting at `fileOffset`
    // within `fileIndex`. Walks pieces forward until the first missing
    // piece is hit, then returns the byte count up to that boundary.
    // Used by StreamEngine for gate-progress buffering % (Batch 3.2) and
    // by the seek pre-gate (Batch 2.4).
    qint64 contiguousBytesFromOffset(const QString& infoHash, int fileIndex,
                                     qint64 fileOffset) const;

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.1 — per-file contiguous-have
    // byte-range projection. Walks the file's piece range, collects every
    // have_piece()-true piece, translates each to its file-local byte range
    // (clamped to file boundaries and piece_size() for the final piece),
    // then merges adjacent ranges into a single span. Returns sorted non-
    // overlapping {startByte, endByte} pairs in file-local coordinates
    // ([0, fileSize) range, endByte is exclusive). Pure read — locks
    // m_mutex, no behavior change. Stub returns empty list on no-libtorrent
    // build. Agent 4B's pre-offered Axis 1 HELP (chat.md:555-561) covers
    // this addition — same const-read shape as havePiece +
    // contiguousBytesFromOffset; flagged here for 4B visibility.
    QList<QPair<qint64, qint64>> fileByteRangesOfHavePieces(
        const QString& infoHash, int fileIndex) const;

    // STREAM_ENGINE_FIX Phase 3.1 — default tracker pool for magnet
    // augmentation below the <5 add-on-tracker threshold (Axis 7). Compile-
    // time-constant list of 25 publicly-known reliable UDP trackers; no
    // network fetch, no runtime mutation, zero surface for external
    // pollution. Agent 4B (Sources) curates the roster; Agent 4 Phase 3.2
    // consumes from StreamEngine magnet construction. Library-path-
    // independent — no libtorrent call, same list in stub build path.
    // Note: pre-existing kFallbackTrackers in StreamAggregator.cpp:32 (12-
    // tracker zero-trackers-only fallback) is a subset of this pool;
    // Agent 4 Phase 3.2 can migrate that consumer to this canonical pool
    // during threshold-change work if desired.
    static const QStringList& defaultTrackerPool();

signals:
    void metadataReady(const QString& infoHash, const QString& name,
                       qint64 totalSize, const QJsonArray& files);
    void torrentProgress(const QString& infoHash, float progress,
                         int dlSpeed, int ulSpeed, int peers, int seeds);
    void torrentFinished(const QString& infoHash);
    void torrentError(const QString& infoHash, const QString& message);
    void torrentAddedConfirmed(const QString& infoHash);
    void torrentAddFailed(const QString& infoHash, const QString& errorMessage);
    void resumeDataAvailable(const QString& infoHash, const QByteArray& blob);

    // Emitted when libtorrent finishes relocating a torrent's storage. Fired
    // from the AlertWorker thread on storage_moved_alert / storage_moved_failed_alert
    // — receivers must connect with QueuedConnection (Qt::AutoConnection from a
    // main-thread receiver resolves to that). Pure observation; no state change
    // happens inside the emit.
    void storageMoved(const QString& infoHash, const QString& newPath);
    void storageMoveFailed(const QString& infoHash, const QString& message);
    void fileRenamed(const QString& infoHash, int fileIndex, const QString& newPath);
    void fileRenameFailed(const QString& infoHash, int fileIndex, const QString& message);

    // STREAM_ENGINE_REBUILD P2 — emitted once per libtorrent
    // piece_finished_alert from the AlertWorker thread. Consumers must
    // connect with Qt::QueuedConnection (AutoConnection default from
    // main-thread QObject receivers resolves to this) so StreamPieceWaiter
    // map mutation stays on the consumer thread, not the alert worker.
    // Replaces the 15 s poll floor in StreamHttpServer — consumer binds a
    // QWaitCondition to this signal for sub-second wake on piece arrival.
    // No existing code paths produce or bind this signal; purely additive.
    void pieceFinished(const QString& infoHash, int pieceIndex);

private:
#ifdef HAS_LIBTORRENT
    // Alert worker runs on a dedicated QThread
    class AlertWorker;
    friend class AlertWorker;

    lt::session        m_session;
    QThread            m_alertThread;
    AlertWorker*       m_alertWorker = nullptr;

    void applySettings();
    void loadDhtState();
    void saveDhtState();
    void saveAllResumeData();
    void ensureDirs();

    static QString hashToHex(const lt::torrent_handle& h);
    static QString stateToString(lt::torrent_status::state_t s, bool paused);

    struct SeedingRule { float ratioLimit = 0.f; int seedTimeSecs = 0; };
    QHash<QString, SeedingRule> m_seedingRules;
    SeedingRule                 m_globalSeedRule;
    void checkSeedingRules();
#endif

    mutable QMutex                    m_mutex;
    QHash<QString, TorrentRecord>     m_records;
    QString                           m_cacheDir;
    bool                              m_running = false;
};
