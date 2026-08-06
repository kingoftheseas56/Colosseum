// ComicDownloader.h
//
// The western-comics half of the download-fed backbone: reading is NEVER a live
// stream. A GetComics release post is one archive file (.cbr/.cbz) — the volume
// unit. This is the BookDownloader lineage (single-file HTTP stream, .part →
// rename, stale-HTML failover), NOT MangaDownloader's page→cbz pipeline, plus
// one extra stage the reader needs: extract the archive into a loose page dir
// so MangaReader consumes it exactly like a downloaded manga chapter.
//
// Pipeline (design: docs/superpowers/specs/2026-07-04-colosseum-western-comics-
// getcomics-design.md, ratified — GetComics for both catalog and download):
//   1. resolve: GET the release post → parse the FULL signed "DOWNLOAD NOW"
//      href (getcomics.org/dls/<payload>:<sig>==). The bare /dls/<token>/ link
//      is the ad-gate (TB2's 2026-06-05 scar); the signed one 302s straight to
//      comicfiles.ru, clean HTTP, no browser. Mirror links are kept as failover.
//   2. stream: GET the signed URL with Chrome UA + getcomics.org Referer →
//      write <dir>/<file>.part in chunks (readyRead, NEVER readAll — TPBs run
//      300MB–1GB), text/html first-chunk detection (ad-gate/interstitial ⇒
//      failover to next link), retry 2/4/8s, atomic rename.
//   3. extract: the archive is RAR (cbr) or zip (cbz). v1 extractor = the OS's
//      bundled bsdtar (C:\Windows\System32\tar.exe, libarchive — reads BOTH;
//      proven on the real Kyoshi Warriors #2 RAR5, 25 pages in 1.6s), with an
//      installed 7-Zip as fallback. No vendored libunrar/7z — reduction reflex.
//      Pages land as <dir>/page_NNN.<ext> (MangaDownloader's naming), archive
//      and temp dir are deleted after.
//   4. index: {issueId → seriesId, title, dir, files, bytes}; localPages(id)
//      returns the same [{index, url, group}] shape Downloads.localPages does,
//      so MangaReader reads western issues through the same machinery.
//
// On-disk layout (AppDataLocation, not the purgeable cache):
//   <appdata>/comics/<series>/<issue>-<hash10>/page_000.jpg ...
//   <appdata>/comics/index.json

#pragma once

#include "DownloadFileOps.h"
#include "engine/CbzArchive.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class QProcess;
class TorrentEngine;
class ComicTorrents;

class ComicDownloader : public QObject
{
    Q_OBJECT
public:
    explicit ComicDownloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ComicDownloader(QNetworkAccessManager* nam, QNetworkAccessManager* searchNam,
                    TorrentEngine* torrentEngine, QObject* parent = nullptr);
    ~ComicDownloader() override;

    // ---- QML entry points ----

    // Resolve a GetComics release post to its signed download link, stream the
    // archive, extract it to a page dir. `issueId` is the stable key (the WP
    // post id as a string); `postUrl` the release post's permalink. Idempotent:
    // downloaded → re-emits finished(); active/queued → no-op. `expectedBytes`
    // (from the post's "Size: N MB") gates a disk-space pre-check when > 0.
    Q_INVOKABLE void downloadIssue(const QString& issueId, const QString& postUrl,
                                   const QString& seriesId, const QString& seriesTitle,
                                   const QString& issueLabel, double expectedBytes = 0);

    // Feed a CBR/CBZ/CB7/CBT produced by another transport into the SAME
    // two-path ingest onFinished() uses (Task 6): a natively-readable CBZ
    // moves into the library archive-in-place with no extraction; anything
    // else extracts-then-repacks. Ownership transfers to this object: the
    // source archive is CONSUMED on success (moved into place, or copied then
    // deleted). On FAILURE the source is now PRESERVED, not deleted (Task 4's
    // repair-before-prune, inherited via the shared path) -- a failed ingest
    // must never destroy the caller's only copy; a retry or crash-recovery
    // adoption reclaims it instead. (Was: deleted on failure too -- changed
    // deliberately, because destroying a source on a transient failure is the
    // exact data-loss shape this arc exists to close.)
    Q_INVOKABLE void ingestLocalArchive(const QString& issueId, const QString& seriesId,
                                        const QString& seriesTitle, const QString& issueLabel,
                                        const QString& archivePath);

    // ── C++-only ingest boundary (NOT QML-invokable) ──────────────────────────
    // Publishes a complete Task-6 ComicEditionAssembler staging directory
    // (an "<editionId>.staging" dir of page_NNN.<ext> files, already in
    // reading order) into the SAME comics library dir/index/reader contract
    // GetComics downloads use, under the edition's own catalog chId. Queues
    // behind the existing single extraction/publication lane — this is never
    // a second concurrent publisher. Every supplied file is verified to live
    // inside stagingDir before anything is touched; the complete staging dir
    // is then moved into place with ONE atomic rename — a partial directory
    // is never published. `groups` is an optional per-page group parallel to
    // `orderedFiles` (one int per page, e.g. a source-issue index); empty
    // means "no grouping", matching the group -1 existing GetComics/single-
    // archive rows already return from localPages().
    void ingestAssembledEdition(const QString& editionId, const QString& seriesId,
                                const QString& seriesTitle, const QString& editionLabel,
                                const QString& stagingDir, const QStringList& orderedFiles,
                                const QList<int>& groups);

    // Search the built-in torrent sources, download the best comic archive,
    // then ingest it through this object's existing extraction/index contract.
    Q_INVOKABLE void downloadIssueTorrent(const QString& issueId, const QString& seriesId,
                                          const QString& seriesTitle, const QString& issueLabel,
                                          const QString& query);

    // ── Alternate torrent sources (v2): manual, edition-aware browsing ────────
    // These NEVER auto-pick — the user chooses a torrent in the picker. Search
    // is cancellable and creates no Downloads job. downloadTorrentSource() then
    // rides the existing infoHash → downloadInfoHash → archiveReady → ingest
    // path under the SAME edition chId; releaseTitle is display-only and must
    // never replace the canonical issueLabel used for archive matching.
    Q_INVOKABLE void searchTorrentSources(const QString& issueId, const QString& seriesTitle,
                                          const QString& editionTitle, const QString& isbn,
                                          const QString& collects, const QString& catalogFormat);
    Q_INVOKABLE void searchTorrentSourcesQuery(const QString& issueId, const QString& query);
    Q_INVOKABLE void cancelTorrentSourceSearch(const QString& issueId);
    Q_INVOKABLE void downloadTorrentSource(const QString& issueId, const QString& seriesId,
                                           const QString& seriesTitle, const QString& issueLabel,
                                           const QString& infoHash, const QString& releaseTitle,
                                           const QString& magnetUri);
    // Commit a user-chosen archive from an ambiguous, paused torrent (fileIndex
    // is a manifest index the picker offered via torrentArchiveSelectionRequired).
    Q_INVOKABLE void chooseTorrentArchive(const QString& issueId, int fileIndex);

    // ── Automatic pack-selection path (v2, Task 10) ───────────────────────────
    // The AUTOMATIC counterpart to downloadTorrentSource(): the caller has
    // picked a torrent for a collected edition; this isolates + downloads the
    // edition itself (shared-infohash pack transport), NO manual file pick,
    // unless the manifest is Ambiguous/CombinedOnly/IncompleteIssueSet — those
    // pause and surface the typed signals below instead of guessing.
    Q_INVOKABLE void downloadTorrentEdition(const QString& issueId, const QString& seriesId,
                                            const QString& seriesTitle, const QString& editionTitle,
                                            const QString& isbn, const QString& collects,
                                            const QString& catalogFormat,
                                            const QString& infoHash, const QString& magnetUri);
    // Commit a manual pick among an Ambiguous decision's candidates (one or
    // more manifest indices — a split multi-file pick counts as one edition).
    Q_INVOKABLE void chooseTorrentFiles(const QString& issueId, const QVariantList& indices);
    // Accept a CombinedOnly decision's whole archive as an explicit,
    // user-confirmed release (it likely also contains other editions).
    Q_INVOKABLE void confirmCombinedArchive(const QString& issueId);

    // Local pages of a downloaded issue, MangaDownloader-shaped:
    // [{index, url: "file:///…/page_000.jpg", group: -1}] — or [] if not on disk.
    Q_INVOKABLE QVariantList localPages(const QString& issueId) const;

    Q_INVOKABLE bool isDownloaded(const QString& issueId) const;

    // {state: "none"|"resolving"|"queued"|"downloading"|"extracting"|"done",
    //  done, total} — done/total are BYTES (doubles), unlike manga's page counts.
    Q_INVOKABLE QVariantMap statusOf(const QString& issueId) const;

    // Bulk views for the Downloads page facade.
    Q_INVOKABLE QVariantList downloadedIssues() const;
    Q_INVOKABLE QVariantList activeIssueJobs() const;

    Q_INVOKABLE void cancelDownload(const QString& issueId);
    Q_INVOKABLE QVariantMap deleteIssue(const QString& issueId);

    // Headless smoke (COLOSSEUM_COMIC_DLTEST=<postUrl>): resolve → download →
    // extract a real post, log OK/FAILED + the page count. Drive-harness food.
    Q_INVOKABLE void selfTest(const QString& postUrl);
    void selfTestTorrent(const QString& infoHash, const QString& seriesTitle,
                         const QString& issueLabel);

    // ── Test-only end-to-end self-test (COLOSSEUM_COMIC_PACK_DLTEST, Task 11) ──
    // Honest end-to-end proof for the shared-infohash edition PACK transport
    // (downloadTorrentEdition), wired from main.cpp only when the env var is
    // set (an idle app never calls it, so it touches no network). Spec:
    //   "<scenario>|<magnet>|<fixture-id>[|<fixture-id2>]"
    // scenario in {single, issues, shared, restart}; fixture-id(s) select a
    // canonical edition target from the fixed table this method carries
    // in-source (matching the archives tests/comic_torrent_pack_seed_harness.cpp
    // seeds). Drives the SAME downloadTorrentEdition() entry point QML uses —
    // never a shortcut path. Prints "COMIC_PACK_<SCENARIO>_DONE pages=<n>
    // [groups=<n>]" and exits 0 on success; "[comic-pack-dltest] FAIL <reason>"
    // and exits 2 on any failure or a 240s hard timeout.
    Q_INVOKABLE void runPackSelfTest(const QString& spec);

signals:
    void progress(const QString& issueId, double done, double total);   // bytes
    void finished(const QString& issueId);
    void failed(const QString& issueId, const QString& reason);
    void removed(const QString& issueId);
    // Source browsing (v2) — search-only, distinct from the acquisition signals.
    void torrentSourcesUpdated(const QString& issueId, const QVariantList& rows, bool complete);
    void torrentSourceSearchFailed(const QString& issueId, const QString& reason);
    // Ambiguous torrent paused for a manual archive choice, then the outcome.
    // Reused for the edition pack transport's Ambiguous case too (same
    // "pick one of these files" shape — see ComicTorrentDownloader.h).
    void torrentArchiveSelectionRequired(const QString& issueId, const QVariantList& files);
    void torrentArchiveSelected(const QString& issueId, const QString& fileName, bool automatic);
    // Shared by both torrent subsystems — a torrent was added and is being
    // inspected before any payload downloads.
    void resolving(const QString& issueId);
    // Edition pack transport only (v2, Task 10) — typed non-automatic
    // outcomes; never auto-download, always wait for the matching Q_INVOKABLE.
    void torrentCombinedArchiveConfirmationRequired(const QString& issueId, const QVariantList& files);
    void torrentIncompleteIssueSetDetected(const QString& issueId, const QStringList& missingIssues);

private:
    struct Entry {
        QString seriesId;
        QString seriesTitle;
        QString label;
        // Storage precedence (Task 2, CBZ-in-place plan): `archive` wins whenever
        // both are set. A legacy loose-folder row has `archive` empty. An
        // archive-shaped row may still carry a leftover `dir` mid-migration --
        // Task 7's first-boot pass deliberately leaves `dir` set for one boot
        // before reclaiming the loose files on the boot after. isDownloaded(),
        // deleteIssue() (Task 2), downloadedIssues() (Task 3), and localPages()
        // (Task 4) all check usesArchive() FIRST.
        QString dir;
        QString archive;
        QStringList files;
        QList<int> groups;   // parallel to files; empty = no grouping (localPages() reports -1)
        qint64 bytes = 0;
        qint64 addedAt = 0;
        bool usesArchive() const { return !archive.isEmpty(); }
    };
    struct InFlight {
        QString id;
        QString postUrl;
        // Empty today — no producer sets this yet. A future multi-part GetComics fix (one
        // InFlight per part, sharing one release post) sets it to a value shared by every part
        // of that post, so the Downloads page groups them the way it already groups TV seasons
        // and manga volume batches (2026-08-05 grouping design). Deliberately NOT derived from
        // postUrl: postUrl's callers differ across ComicSeriesPage.qml/ComicSeries.qml/
        // ComicReaderShell.qml (postUrl vs url vs c.url, three different source fields feeding
        // the same downloadIssue() argument) — an inferred key risks silently merging two
        // unrelated issues under one fold with a group-cancel that kills both. An explicit,
        // opt-in field costs one empty string and removes that risk entirely.
        QString partGroupKey;
        QString seriesId;
        QString seriesTitle;
        QString label;
        QStringList urls;          // signed dls hrefs, best-first
        int urlIdx = 0;
        int attempt = 0;
        qint64 expectedBytes = 0;
        QPointer<QNetworkReply> reply;
        QFile* file = nullptr;
        QString archivePath;       // final archive path (pre-extract)
        QString partPath;
        qint64 receivedBytes = 0;
        bool sanityChecked = false;
        bool redirectBlocked = false;   // redirect target was a known-blocked host → skip URL, not retry
        qint64 lastProgressEmit = 0;
        qint64 lastProgressBytes = 0;
        bool extracting = false;
        bool localArchive = false;   // starts at beginExtract(), never at HTTP startAttempt()
        QString extractTmp;

        // Task 4 (CBZ-in-place plan) -- background copy/pack safety:
        // `serial` is stamped from `m_nextJobSerial` when this InFlight becomes
        // `m_active`. A background worker (the fast path's cross-volume copy,
        // or the fallback's repack) captures it BY VALUE; its GUI-thread
        // completion handler only touches m_index/m_active/emits a signal when
        // `m_active && m_active->serial == captured` still holds -- immune to
        // both "the job was cancelled" and the sharper trap "cancel-then-
        // redownload the same issue id", which an id-only comparison would miss
        // (a NEW InFlight with the SAME id would wrongly accept a stale future).
        // `packing` is true for the whole window such a worker may be reading
        // `extractTmp`/`archivePath` on another thread -- cancelDownload() and
        // ~ComicDownloader() must not delete either while it's set; the
        // worker's own completion handler owns that cleanup instead (on the
        // GUI thread, after the serial check tells it whether to publish or
        // discard). See finalizeSafeMove()/finalizeExtract().
        quint64 serial = 0;
        bool packing = false;

        // Assembled-edition ingest (Task 7): set only by ingestAssembledEdition().
        // No archive, no network — publishAssembledEdition() validates+moves
        // assembledStagingDir directly, sharing this same single-lane queue.
        bool assembledIngest = false;
        QString assembledStagingDir;
        QStringList assembledOrderedFiles;
        QList<int> assembledGroups;
    };

    QString baseDir() const;
    QString issueDir(const QString& seriesId, const QString& label, const QString& id) const;
    void loadIndex();
    void saveIndex() const;

    // Boot-time legacy migration (Task 7, CBZ-in-place plan): every western
    // comic downloaded before this arc is a loose page_NNN.<ext> folder (a
    // `dir` row, no `archive`). Convert each to the canonical CBZ-in-place
    // model, repair-before-prune, across TWO boots:
    //   Pass 1 (a legacy dir row this boot) -- pack the loose pages into the
    //     canonical CBZ, round-trip-verify it, set `archive`, and LEAVE `dir`
    //     ALONE. The loose files are NOT reclaimed the same boot -- the
    //     deliberate amendment past manga's own migrateLegacy(), so a bad pack
    //     can never destroy the only copy before a separate boot re-verifies.
    //   Pass 2 (a row that arrived already archive+dir from a PRIOR boot) --
    //     independently re-probe the archive; if openable, remove the loose
    //     dir and clear `dir`; if the archive is present-but-unreadable,
    //     demote it back to the dir (clear `archive`) so the next boot re-packs.
    // A row with any listed page missing migrates not at all (untouched, warn).
    // Runs SYNCHRONOUSLY from loadIndex() -- one-shot per row, rare, before the
    // app is interactive; a materially different cost than freezing a live
    // session (why Task 4/5's live repacks go off-thread but this need not).
    void migrateLegacyComicsInPlace();

    void onResolveFinished(QNetworkReply* reply);
    QStringList parsePostHtml(const QByteArray& html) const;
    void startDownload(InFlight&& f);
    void startAttempt(InFlight& f);
    void onReadyRead();
    void onProgressFromReply(qint64 received, qint64 total);
    void onFinished();
    void retryOrFailover(InFlight& f, const QString& reason);
    void startNextUrlOrFail(InFlight& f);
    void failAndCleanup(InFlight& f, const QString& reason);
    // Same terminal shape as failAndCleanup(), for the two-path ingest's
    // (Task 4) own verification/finalize failures specifically: it clears
    // f.archivePath/partPath/extractTmp on the InFlight BEFORE delegating, so
    // failAndCleanup()'s existing (and otherwise correct) cleanup helpers --
    // which already no-op on an empty path -- destroy nothing. Repair-before-
    // prune requires the ordinary failure path to preserve the source exactly
    // as faithfully as a simulated mid-operation crash does; using plain
    // failAndCleanup() for these specific failures would not (it would delete
    // the very source this task exists to protect).
    void failPreservingSource(InFlight& f, const QString& reason);
    // Terminal failure for an EXTRACTION-path failure (missing extractor,
    // unreadable payload) that respects the ingest source's ownership (Task 6):
    // a local-archive import (torrent-produced or user-picked) has no other
    // copy, so its source is PRESERVED on failure -- but OUR extraction temp
    // dir is still cleaned up (it's ours, not the source). An HTTP download's
    // staging file is re-downloadable, so it keeps the plain failAndCleanup()
    // (deleting a re-downloadable temp buys nothing). Deleting a local import's
    // only copy on an environmental failure is the exact data-loss shape this
    // arc closes.
    void failIngest(InFlight& f, const QString& reason);
    // True if `absPath` is the archive of some live m_index row -- a guard for
    // the ingestLocalArchive() removes: after an index loss a user could
    // re-import the canonical file itself, and blindly deleting the caller's
    // source would then destroy a comic the freshly-written/existing row still
    // points at (Task 6 review).
    bool isLiveLibraryArchive(const QString& absPath) const;
    void cancelAndCleanup(InFlight& f);
    DownloadFileOps::Result cleanupCancelledPayload(InFlight& f);
    void closePart(InFlight& f);
    void closeAndDeletePart(InFlight& f);
    void startNextQueued();

    void beginExtract(InFlight& f);
    void runExtractor(InFlight& f, int which);   // 0 = bsdtar, 1 = 7z
    void onExtractDone(int exitCode, int which);
    void finalizeExtract(InFlight& f);
    void cleanupExtract(InFlight& f);

    // ── Two-path ingest (Task 4, CBZ-in-place plan) ───────────────────────────
    // The shared probe-then-branch decision: a natively-readable CBZ at
    // f.archivePath takes the fast path (finalizeSafeMove, archive-in-place),
    // anything else falls to extract-then-repack (beginExtract). Called from
    // onFinished() (HTTP download) AND ingestLocalArchive() (Task 6:
    // user-imported / torrent-produced single archives) so both share ONE
    // ingest mechanism, not a duplicated branch. Both success paths consume
    // f.archivePath, which is exactly ingestLocalArchive's ownership-transfer
    // contract.
    void ingestArchiveByProbe(InFlight& f);

    // The canonical archive location for an issue -- a FILE sibling to (never
    // colliding with) the legacy loose-folder path issueDir() returns, so
    // Task 7's migration can have both `archive` and `dir` populated for one
    // boot without a path collision.
    QString issueArchivePath(const QString& seriesId, const QString& label, const QString& id) const;

    // Crash-recovery adoption: a canonical archive from an interrupted prior
    // attempt (a safe-move or repack that completed on disk but never reached
    // saveIndex(), e.g. the app was killed, or quit while a background worker
    // was still running past this object's destruction -- see `packing`
    // above) sits at issueArchivePath(...) with no index row. Probes it; if
    // valid, adopts it directly (builds Entry, saveIndex(), returns true --
    // caller emits finished(), no network/repack ever touched). If invalid,
    // removes the stale file and returns false so the caller proceeds with a
    // normal ingest. Never the hard "already exists" dead end a naive check
    // would produce.
    bool adoptExistingCanonicalIfValid(const QString& id, const QString& seriesId,
                                       const QString& seriesTitle, const QString& label);

    // The fast path: a freshly downloaded file that CbzArchive::probe()d
    // `nativelyReadable`. Moves it into the library archive-in-place, no
    // extraction. `probe` is the result that already proved f.archivePath
    // readable (its `entries` become Entry::files directly -- no later zip
    // re-open). Rename onto the canonical location when same-volume (the
    // common case, cheap, GUI thread); a cross-volume rename failure falls
    // back to a backgrounded copy (see runPackOrCopyThenPublish).
    void finalizeSafeMove(InFlight& f, const MangaTankoban::CbzProbeResult& probe);

    // Shared tail of the fast path, reached either directly from
    // finalizeSafeMove() (same-volume rename, synchronous) or from a
    // background copy job's completion handler. `tempCanonical` has already
    // been proven readable via `reprobe`; `wasCopy` gates whether the
    // original source is deleted (a copy leaves it behind on failure, a
    // rename already consumed it).
    void completeSafeMove(InFlight& f, const QString& tempCanonical,
                          const MangaTankoban::CbzProbeResult& reprobe, bool wasCopy);

    // The result of a background copy-then-probe or pack-then-probe job --
    // pure data, safe to pass across the thread boundary by value.
    struct PackOrCopyResult {
        bool ok = false;
        QString error;
        MangaTankoban::CbzProbeResult probe;   // valid entries only when ok
        // Every path this worker (or the input it consumed) should have
        // removed if the job is discarded before it can publish -- covers
        // both what the worker itself produced (a temp/final canonical) and
        // the input side (extractTmp, the original downloaded archive) that
        // cancelAndCleanup() deliberately leaves alone while this job might
        // still be reading them (see InFlight::packing).
        QStringList cleanupPathsOnDiscard;
    };

    // Runs `work` on a background thread (QtConcurrent), then -- on the GUI
    // thread, via QFutureWatcher -- calls `onDone(result)` ONLY IF `m_active`
    // still points at the InFlight this job was dispatched for (checked by
    // `serial`, not by id -- see the InFlight::serial comment). `work` must
    // capture everything it needs BY VALUE, never `this` or a reference into
    // `*m_active`: the InFlight may be deleted and `m_active` reassigned to a
    // completely different job before this job finishes. The watcher's
    // context is `this`, so its completion slot auto-disconnects on this
    // object's destruction -- but the pool thread itself is NOT cancelled and
    // keeps running to completion regardless (it may leave a finished,
    // unindexed canonical on disk, which adoptExistingCanonicalIfValid()
    // reclaims on a later launch). This is QtConcurrent's global QThreadPool,
    // which Qt itself joins at static destruction -- so on a graceful app
    // quit mid-pack, process exit blocks until the worker finishes, same as
    // waiting out any other in-flight background task. It is NOT the
    // freeze-and-get-killed shape this arc exists to fix: that was the GUI
    // thread itself blocked and unresponsive during a synchronous repack;
    // this is app shutdown waiting on a worker thread while the GUI has
    // already torn down, which a user watching a slow-to-exit app could still
    // force-kill -- safe either way, since the repack's own `.incoming`/
    // `.part` temp files are never the canonical path until proven complete.
    void runPackOrCopyThenPublish(quint64 serial, std::function<PackOrCopyResult()> work,
                                  std::function<void(PackOrCopyResult)> onDone);

    void publishAssembledEdition(InFlight& f);

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, Entry> m_index;
    QHash<QNetworkReply*, InFlight> m_resolving;
    InFlight* m_active = nullptr;
    QList<InFlight> m_queue;
    QProcess* m_proc = nullptr;
    ComicTorrents* m_torrents = nullptr;
    quint64 m_nextJobSerial = 0;   // see InFlight::serial (Task 4)
};
