#pragma once

// Comic torrent transport: TWO coexisting subsystems sharing one engine seam.
//
//   1. LEGACY single-archive path (download/chooseFile/cancel/statusOf/
//      activeJobs) — UNCHANGED behavior, the shipped GetComics alternate-
//      source flow. One torrent == one issueId; the whole manifest trickles
//      (addMagnet paused=false); ComicTorrentFilePicker::decide() auto-picks
//      or pauses for a manual choice; finished(issueId, archivePath) hands a
//      RAW (unextracted) archive to ComicTorrents -> ComicDownloader::
//      ingestLocalArchive, exactly as before this task.
//
//   2. NEW shared-infohash EDITION pack transport (design: docs/superpowers/
//      specs/2026-07-15-colosseum-tankorent-comic-volume-mode-design.md,
//      "Durable shared-infohash transport"). Mirrors the proven
//      MangaVolumeTorrentDownloader discipline (native/torrent/
//      MangaVolumeTorrentDownloader.h): one job per canonical infoHash with N
//      edition INTENTS. A candidate is added PAUSED; metadata is inspected
//      BEFORE any payload downloads; ComicEditionFileSelector resolves each
//      live intent against the manifest; the union of every live intent's
//      selected indices becomes the file-priority vector; a second edition on
//      the same hash JOINS the job instead of re-adding the magnet. On engine
//      completion each intent independently runs ComicEditionAssembler::
//      assemble() (synchronous) then hands the staging directory to
//      ComicDownloader::ingestAssembledEdition(). One intent's assembly
//      failure never fails its siblings. Every intent is journaled to a
//      ComicRequestLedger so a restart replays exactly the in-flight rows.
//
// Both subsystems are reached through the SAME IComicTorrentEngine seam (the
// comics mirror of MangaVolumeTorrentDownloader's IMangaTorrentEngine), so
// the whole class is testable without libtorrent — see
// tests/comic_torrent_pack_transport_harness.cpp.

#include "ComicEditionFileSelector.h"   // ComicPayloadDecision, ManifestFile, ComicSelectedFile
#include "ComicEditionIdentity.h"       // ComicEditionTarget
#include "ComicTorrentFilePicker.h"     // ComicArchiveCandidate, ComicArchiveDecision

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class ComicDownloader;          // native/engine — edition publish target (ingestAssembledEdition)
class ComicEditionAssembler;    // native/engine
class ComicRequestLedger;

// ── The testable engine seam ────────────────────────────────────────────────
// A minimal abstraction over the concrete, non-virtual TorrentEngine. The real
// adapter (ComicTorrentEngineAdapter, native/torrent/ComicTorrents.cpp,
// HAS_LIBTORRENT-gated) forwards these calls 1:1 and re-emits the signals; a
// harness fake records calls and emits signals on demand. Superset of
// MangaVolumeTorrentDownloader's IMangaTorrentEngine: also carries the
// pause/resume/isRunning/start + torrentAddFailed the LEGACY single-archive
// path already relied on before this task.
class IComicTorrentEngine : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool isRunning() const = 0;
    virtual void start() = 0;
    virtual QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused) = 0;
    virtual void setFilePriorities(const QString& infoHash, const QVector<int>& priorities) = 0;
    virtual void startTorrent(const QString& infoHash, const QString& savePath) = 0;
    virtual void pauseTorrent(const QString& infoHash) = 0;
    virtual void resumeTorrent(const QString& infoHash) = 0;
    virtual void removeTorrent(const QString& infoHash, bool deleteFiles) = 0;
    virtual QJsonArray torrentFiles(const QString& infoHash) const = 0;
signals:
    void metadataReady(const QString& infoHash, const QString& name,
                       qint64 totalSize, const QJsonArray& files);
    void torrentProgress(const QString& infoHash, float progress,
                         int dlSpeed, int ulSpeed, int peers, int seeds);
    void torrentFinished(const QString& infoHash);
    void torrentError(const QString& infoHash, const QString& message);
    void torrentAddFailed(const QString& infoHash, const QString& errorMessage);
};

class ComicTorrentDownloader : public QObject
{
    Q_OBJECT
public:
    // Legacy-only wiring: the single-archive Job map is live; the edition pack
    // transport stays dormant (m_ledger == nullptr) so downloadEdition() etc.
    // fail gracefully rather than silently no-op. Matches the shipped
    // single-arg construction `ComicTorrentDownloader(engine)` old tests and
    // the current QML path still use.
    explicit ComicTorrentDownloader(IComicTorrentEngine* engine, QObject* parent = nullptr);
    // Full wiring: legacy Job map AND the edition pack transport share this
    // SAME engine. `ledgerPath` is the edition-intent journal file;
    // `saveRoot` is the parent directory under which each pack job gets a
    // per-infoHash subfolder; `stagingRoot` is where ComicEditionAssembler
    // stages pages before publication. `ingestTarget` may be null (the
    // capability still builds/tests; publication is simply unavailable until
    // a later task wires a real ComicDownloader in). On construction the
    // pack transport REPLAYS every active ledger row (re-adds each torrent
    // paused, forgets prior file choices, re-resolves once metadata returns).
    ComicTorrentDownloader(IComicTorrentEngine* engine, ComicDownloader* ingestTarget,
                           const QString& ledgerPath, const QString& saveRoot,
                           const QString& stagingRoot, QObject* parent = nullptr);
    ~ComicTorrentDownloader() override;

    // ── Legacy single-archive API (COMPATIBILITY WRAPPER — unchanged behavior) ──
    void download(const QString& issueId, const QString& infoHash, const QString& title,
                  const QString& magnetUri = QString());
    bool cancel(const QString& issueId);
    // Commit a user-chosen archive from an ambiguous, paused torrent. Returns
    // false for an unknown issue or a non-eligible file index.
    bool chooseFile(const QString& issueId, int fileIndex);
    QVariantMap statusOf(const QString& issueId) const;
    QVariantList activeJobs() const;

    // ── Edition pack transport (Task 9) ─────────────────────────────────────
    // Request `target.editionId` from the torrent at `infoHash`. If that
    // torrent is already in flight (shared infoHash) this joins the existing
    // job and grows its priority union instead of re-adding the magnet.
    void downloadEdition(const ComicEditionIdentity::ComicEditionTarget& target,
                         const QString& infoHash, const QString& magnetUri);
    // Drop one edition's intent. If it was the last live intent on its
    // torrent, the torrent is removed (files deleted only if no intent ever
    // succeeded); otherwise the torrent keeps serving the remaining editions.
    bool cancelEdition(const QString& editionId);
    // Commit a manual pick among an Ambiguous decision's manualCandidates.
    bool chooseFiles(const QString& editionId, const QList<int>& indices);
    // Accept a CombinedOnly decision's whole archive as an explicit, single
    // user-confirmed release.
    bool confirmCombined(const QString& editionId);
    QVariantMap statusOfEdition(const QString& editionId) const;

    // ── Pure decision helpers (public for direct unit testing) ────────────────
    // The on-disk state of a payload's selected files at assembly time.
    // libtorrent posts torrentFinished the instant the last wanted piece passes
    // hash-check, but the OS write of that piece can still be in flight —
    // extracting a not-yet-flushed .cbz makes bsdtar/7z fail ("not a cbr/cbz?").
    //   Ready    — every selected file exists AND is at its full manifest size.
    //   Flushing — a selected file exists but is still SHORT (write in flight);
    //              wait and re-check, this is the race to absorb.
    //   Missing  — a selected file is absent entirely. After torrentFinished a
    //              WANTED file always exists, so absence is a genuine error, not
    //              the flush race: assemble anyway so it fails promptly (and a
    //              synthetic missing-payload sibling still fails synchronously).
    enum class DiskReadiness { Ready, Flushing, Missing };
    static DiskReadiness diskReadiness(
        const QString& saveDir,
        const QList<ComicEditionFileSelector::ComicSelectedFile>& files);
    // The label an assembled payload publishes under. A CombinedWholeArchive is
    // several editions in one file, so it publishes as its own release title,
    // never falsely as the single requested edition (design: "publishes it as
    // the release title, not falsely as the target edition"). The edition chId
    // is preserved separately by the caller — only the display label changes.
    static QString publishLabel(ComicEditionFileSelector::ComicPayloadKind kind,
                                const QString& editionTitle,
                                const QList<ComicEditionFileSelector::ComicSelectedFile>& files);

signals:
    // Shared by both subsystems, keyed by issueId (legacy) or editionId (pack).
    void resolving(const QString& id);
    void progress(const QString& id, double received, double total);
    void failed(const QString& id, const QString& reason);
    // Legacy-only: a raw (unextracted) archive is ready for ComicTorrents ->
    // ComicDownloader::ingestLocalArchive. Editions never emit this — their
    // terminal signal is ComicDownloader::finished(editionId), emitted once
    // ingestAssembledEdition() actually publishes (constraint: every
    // terminal signal stays on the public Comics object under the original
    // catalog chId).
    void finished(const QString& issueId, const QString& path);
    // Ambiguous manifest paused for a manual choice among eligible archives.
    // Reused for the pack Ambiguous case too (same "pick one of these files"
    // shape); the pack candidates carry {index, path, bytes} instead of the
    // legacy {index, name, extension, sizeBytes, sizeText, exactTitle,
    // tokenCoverage} — QML wiring (Task 10) renders each shape.
    void fileSelectionRequired(const QString& id, const QVariantList& files);
    // A comic archive was committed — automatic=true for lone/unique-exact picks.
    void fileSelected(const QString& issueId, const QString& fileName, bool automatic);
    // Pack-only typed selection variants (design: "Ambiguous/CombinedOnly/
    // IncompleteIssueSet decisions do NOT auto-download").
    void combinedArchiveConfirmationRequired(const QString& editionId, const QVariantList& files);
    void incompleteIssueSetDetected(const QString& editionId, const QStringList& missingIssues);

private:
    // ── Legacy structures (unchanged shape/logic) ───────────────────────────
    struct Job {
        QString issueId;
        QString infoHash;
        QString saveDir;
        QString title;
        int pickedIdx = -1;
        QString fileName;
        qint64 totalBytes = 0;
        qint64 received = 0;
        qint64 lastProgressEmit = 0;
        bool picked = false;
        bool choosing = false;                       // paused, awaiting a manual pick
        QList<ComicArchiveCandidate> candidates;     // eligible archives while choosing
        int manifestSize = 0;                        // file count, for setFilePriorities
    };

    Job* jobForHash(const QString& infoHash) const;
    Job* jobForIssue(const QString& issueId) const;
    bool alive(Job* job) const;
    void applyMetadata(Job* job, const QJsonArray& files);
    void applyPickedFile(Job* job, int pickedIdx, const QString& name, qint64 bytes,
                         int fileCount, bool automatic);
    QVariantList toVariantFiles(const QList<ComicArchiveCandidate>& candidates) const;
    void finalizeJob(Job* job);
    void failJob(Job* job, const QString& reason);
    QString baseDir() const;
    QString dirFor(const QString& infoHash) const;

    // ── Edition pack structures (Task 9) ────────────────────────────────────
    struct EditionIntent {
        QString editionId;
        ComicEditionIdentity::ComicEditionTarget target;
        ComicEditionFileSelector::ComicPayloadDecision decision;
        bool resolved = false;        // decision computed (success or typed failure)
        bool awaitingChoice = false;  // Ambiguous / CombinedOnly / IncompleteIssueSet
        bool terminal = false;        // done consuming shared files (assembled/failed/cancelled)
        qint64 fileSize = 0;          // sum of decision.files[].bytes, for progress scaling
        qint64 received = 0;
        qint64 lastProgressEmit = 0;
    };
    struct PackJob {
        QString infoHash;
        QString magnetUri;
        QString saveDir;
        bool metadataKnown = false;
        bool started = false;
        bool payloadFinished = false;   // engine torrentFinished already fired once
        bool anySuccess = false;        // >=1 intent assembled successfully
        QJsonArray files;
        QList<EditionIntent> intents;
    };

    void replayPackActive();
    EditionIntent* packIntentFor(PackJob* job, const QString& editionId) const;
    void addPackIntent(PackJob* job, const ComicEditionIdentity::ComicEditionTarget& target,
                       const QString& state);
    void writePackLedgerRow(PackJob* job, const ComicEditionIdentity::ComicEditionTarget& target,
                            const QString& state);
    void resolvePackJob(PackJob* job, bool payloadAlreadyFinished);
    void emitTypedSelection(const EditionIntent& intent);
    void reapplyPackPriorities(PackJob* job);
    void assembleAndPublish(PackJob* job, EditionIntent& intent);
    // Assemble as soon as the selected files are fully on disk; else re-check on
    // a short timer (see filesReadyOnDisk below).
    void tryAssembleWhenReady(const QString& infoHash, const QString& editionId, int attempt);
    bool hasLivePackIntent(const PackJob* job) const;
    // Tears the job down when it has no live intent left AND nothing on it
    // ever succeeded (pure cancel/fail-out). A job with >=1 successful intent
    // stays resident (files preserved on disk, torrent left registered).
    //
    // RATIFIED PRODUCT DECISION (Hemanth, 2026-07-16): a completed pack KEEPS
    // SEEDING for the rest of the session. This is deliberate, not a leak —
    // it (1) lets a LATER edition sharing the same infoHash join and assemble
    // immediately without re-downloading, and (2) seeds the pack back, which is
    // the seed toward Tankorent becoming a Torrentio-style source. This
    // intentionally supersedes the design's DoD #7 "reference-safe cleanup"
    // wording, which called for teardown once every intent reached terminal.
    // (Codex flagged the old behavior as an unbounded leak; the residency is
    // now the chosen behavior. Cross-restart re-seeding is a separate, future
    // feature — completed rows are terminal, so replay does not re-add them.)
    void maybeTearDownPackJob(PackJob* job);
    void tearDownPackJob(PackJob* job, bool deleteFiles);
    QString packSaveDirFor(const QString& infoHash) const;
    static QString reasonForFailure(ComicEditionFileSelector::ComicSelectionFailure failure);
    static QList<ComicEditionFileSelector::ManifestFile> toManifest(const QJsonArray& files);
    static QList<int> indicesOf(const ComicEditionFileSelector::ComicPayloadDecision& decision);

    // ── Unified engine dispatch (both subsystems share one engine signal set) ──
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void onEngineProgress(const QString& infoHash, float progress,
                          int downloadRate, int uploadRate, int peers, int seeds);
    void onEngineFinished(const QString& infoHash);
    void onEngineFailed(const QString& infoHash, const QString& message);

    IComicTorrentEngine* m_engine = nullptr;
    QHash<QString, Job*> m_byHash;
    QHash<QString, QString> m_hashByIssue;

    ComicDownloader* m_ingestTarget = nullptr;
    ComicEditionAssembler* m_assembler = nullptr;
    ComicRequestLedger* m_ledger = nullptr;
    QString m_packSaveRoot;
    QString m_stagingRoot;
    QHash<QString, PackJob*> m_packJobs;         // by infoHash
    QHash<QString, QString> m_hashByEdition;     // editionId -> infoHash
};
