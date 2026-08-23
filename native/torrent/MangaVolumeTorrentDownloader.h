#pragma once

// Restart-safe torrent transport for Tankoban volume mode.
//
// Mirrors ComicTorrentDownloader's proven engine wiring (metadataReady ->
// pick + priorities, progress throttle, finished-verify-then-emit) but with the
// two behaviours volume mode needs:
//
//   1. Metadata is inspected BEFORE any payload downloads. A candidate is added
//      PAUSED; only after its metadata arrives, the exact volume file is
//      resolved, priorities are set to that file, and the torrent is STARTED.
//      (ComicTorrent adds paused=false and lets everything trickle.)
//
//   2. One torrent can serve SEVERAL requested volumes. Jobs are keyed by
//      infoHash; each carries a set of requested volume intents. Adding a second
//      volume to a live torrent GROWS the file-priority union — it never re-adds
//      the magnet — and each volume finishes/fails/cancels independently.
//
// Every intent is journaled to a MangaVolumeRequestLedger so a restart can
// replay exactly the downloads still in flight.
//
// The engine is reached through the abstract IMangaTorrentEngine seam so this
// unit is harness-testable without libtorrent. The concrete adapter over the
// real TorrentEngine is wired in a later task.

#include "MangaVolumeRequestLedger.h"
#include "MangaNyaaSource.h"          // MangaTankoban::MangaNyaaCandidate
#include "engine/MangaTankobanTypes.h" // MangaTankoban::VolumeRecord

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

// ── The testable engine seam ────────────────────────────────────────────────
// A minimal abstraction over the concrete, non-virtual TorrentEngine. The real
// adapter forwards these calls/signals 1:1; the harness supplies a fake that
// records calls and emits the signals on demand.
class IMangaTorrentEngine : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused) = 0;
    virtual void setFilePriorities(const QString& infoHash, const QVector<int>& priorities) = 0;
    virtual void startTorrent(const QString& infoHash, const QString& savePath) = 0;
    virtual void removeTorrent(const QString& infoHash, bool deleteFiles) = 0;
    virtual QJsonArray torrentFiles(const QString& infoHash) const = 0;
signals:
    void metadataReady(const QString& infoHash, const QString& name, qint64 totalSize, const QJsonArray& files);
    void torrentProgress(const QString& infoHash, float fraction, int dl, int ul, int peers, int seeds);
    void torrentFinished(const QString& infoHash);
    void torrentError(const QString& infoHash, const QString& message);
};

// ── Arc 18 M6: indexed-identity expectation ──────────────────────────────────
// When a request's source is a VERIFIED index mapping, the transport carries
// the persisted identity (exact fileIndex + path) into the paused-metadata
// flow. Live engine metadata must re-confirm THAT EXACT FILE before payload
// starts; anything else fails closed (expectationViolated) — the transport
// never silently falls back to a different archive of the same torrent.
struct MangaVolumeExpectation {
    int     fileIndex = -1;  // -1 = no expectation (ordinary discovery request)
    QString filePath;        // torrent-relative, '/'-normalized
};

class MangaVolumeTorrentDownloader : public QObject {
    Q_OBJECT
public:
    // `ledgerPath` is the journal file; `saveRoot` is the parent directory under
    // which each torrent gets a per-infoHash subfolder. On construction the
    // downloader REPLAYS every active ledger row (re-adds each torrent paused).
    MangaVolumeTorrentDownloader(IMangaTorrentEngine* engine,
                                 const QString& ledgerPath,
                                 const QString& saveRoot,
                                 QObject* parent = nullptr);
    ~MangaVolumeTorrentDownloader() override;

    // Request `volume` from `candidate`. If the candidate's torrent is already
    // in flight this joins the existing job and grows its priority union.
    // `expectation` (Arc 18 M6): when set, live metadata must resolve to exactly
    // this file or the intent fails closed. Persisted to the ledger so a resumed
    // download performs the same check after restart.
    void download(const MangaTankoban::VolumeRecord& volume,
                  const MangaTankoban::MangaNyaaCandidate& candidate,
                  const MangaVolumeExpectation& expectation = MangaVolumeExpectation{});
    // Drop one volume's intent. If it was the last live intent on its torrent,
    // the torrent is removed (files deleted); otherwise the torrent keeps serving
    // the remaining volumes.
    bool cancel(const QString& volumeId);
    // {state, done, total} for one volume ("none" when unknown).
    QVariantMap statusOf(const QString& volumeId) const;
    // Additive const accessor (Task 8 restart-resume): the PERSISTED ledger row
    // for `volumeId`, so a façade can recover provenance (infoHash / seriesId /
    // volumeNumber / savePath / pickedFileIndex) from disk after a restart replay
    // when its in-memory model is empty. The row's volumeId is empty when unknown.
    MangaTankoban::VolumeRequestRow ledgerRow(const QString& volumeId) const;

signals:
    void resolving(const QString& volumeId);
    void progress(const QString& volumeId, double received, double total);
    void finished(const QString& volumeId, const QString& archivePath);
    void failed(const QString& volumeId, const QString& reason);
    // Arc 18 M6: live metadata contradicted a persisted index identity for this
    // volume. Emitted BEFORE `failed`. The store owner (the façade) demotes the
    // mapping to NeedsRevalidation; the transport itself never touches the index.
    void expectationViolated(const QString& volumeId, const QString& infoHash,
                             int expectedFileIndex);

private:
    struct Intent {
        QString volumeId;
        QString volumeNumber;   // the target passed to MangaVolumeFilePicker
        QString seriesId;
        int     pickedIndex = -1;
        QString pickedName;     // relative archive path, '/'-normalized
        qint64  fileSize = 0;
        qint64  received = 0;
        qint64  lastProgressEmit = 0;
        bool    terminal = false; // completed / failed / cancelled
        // Arc 18 M6 indexed-identity expectation (-1 = none).
        int     expectIndex = -1;
        QString expectPath;
    };
    struct Job {
        QString infoHash;
        QString magnetUri;
        QString saveDir;
        bool    metadataKnown = false;
        bool    started = false;
        QJsonArray files;
        QList<Intent> intents;  // one per requested volume of this torrent
    };

    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void onProgress(const QString& infoHash, float fraction,
                    int dl, int ul, int peers, int seeds);
    void onFinished(const QString& infoHash);
    void onError(const QString& infoHash, const QString& message);

    void replayActive();
    void addIntent(Job* job, const MangaTankoban::VolumeRecord& volume, const QString& state,
                   const MangaVolumeExpectation& expectation = MangaVolumeExpectation{});
    void writeLedgerRow(Job* job, const MangaTankoban::VolumeRecord& volume, const QString& state,
                        const MangaVolumeExpectation& expectation = MangaVolumeExpectation{});
    Intent* intentFor(Job* job, const QString& volumeId) const;
    void resolveJob(Job* job);
    void failIntent(Intent& intent, const QString& reason);
    bool hasLiveIntent(const Job* job) const;
    QVector<int> livePickedIndices(const Job* job) const;
    // Detach a job from the maps BEFORE asking the engine to remove it, so a
    // synchronous re-entrant engine signal (Task 8's real adapter) can't find or
    // double-free it. Idempotent: a second call for an already-detached job no-ops.
    void tearDown(Job* job, bool deleteFiles);
    QString saveDirFor(const QString& infoHash) const;
    static QString reasonFor(int pickFailure);

    IMangaTorrentEngine* m_engine = nullptr;
    MangaTankoban::MangaVolumeRequestLedger m_ledger;
    QString m_saveRoot;
    QHash<QString, Job*> m_jobs;             // by infoHash
    QHash<QString, QString> m_hashByVolume;  // volumeId -> infoHash
};
