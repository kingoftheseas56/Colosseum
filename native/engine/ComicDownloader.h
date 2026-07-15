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

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

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

    // Feed a CBR/CBZ/CB7/CBT produced by another transport into the same
    // extraction/index/reader pipeline. Ownership transfers to this object:
    // the source archive is deleted after extraction or terminal failure.
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
                                          const QString& collects);
    Q_INVOKABLE void searchTorrentSourcesQuery(const QString& issueId, const QString& query);
    Q_INVOKABLE void cancelTorrentSourceSearch(const QString& issueId);
    Q_INVOKABLE void downloadTorrentSource(const QString& issueId, const QString& seriesId,
                                           const QString& seriesTitle, const QString& issueLabel,
                                           const QString& infoHash, const QString& releaseTitle,
                                           const QString& magnetUri);
    // Commit a user-chosen archive from an ambiguous, paused torrent (fileIndex
    // is a manifest index the picker offered via torrentArchiveSelectionRequired).
    Q_INVOKABLE void chooseTorrentArchive(const QString& issueId, int fileIndex);

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
    Q_INVOKABLE void deleteIssue(const QString& issueId);

    // Headless smoke (COLOSSEUM_COMIC_DLTEST=<postUrl>): resolve → download →
    // extract a real post, log OK/FAILED + the page count. Drive-harness food.
    Q_INVOKABLE void selfTest(const QString& postUrl);
    void selfTestTorrent(const QString& infoHash, const QString& seriesTitle,
                         const QString& issueLabel);

signals:
    void progress(const QString& issueId, double done, double total);   // bytes
    void finished(const QString& issueId);
    void failed(const QString& issueId, const QString& reason);
    void removed(const QString& issueId);
    // Source browsing (v2) — search-only, distinct from the acquisition signals.
    void torrentSourcesUpdated(const QString& issueId, const QVariantList& rows, bool complete);
    void torrentSourceSearchFailed(const QString& issueId, const QString& reason);
    // Ambiguous torrent paused for a manual archive choice, then the outcome.
    void torrentArchiveSelectionRequired(const QString& issueId, const QVariantList& files);
    void torrentArchiveSelected(const QString& issueId, const QString& fileName, bool automatic);

private:
    struct Entry {
        QString seriesId;
        QString seriesTitle;
        QString label;
        QString dir;
        QStringList files;
        QList<int> groups;   // parallel to files; empty = no grouping (localPages() reports -1)
        qint64 bytes = 0;
        qint64 addedAt = 0;
    };
    struct InFlight {
        QString id;
        QString postUrl;
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
    void closeAndDeletePart(InFlight& f);
    void startNextQueued();

    void beginExtract(InFlight& f);
    void runExtractor(InFlight& f, int which);   // 0 = bsdtar, 1 = 7z
    void onExtractDone(int exitCode, int which);
    void finalizeExtract(InFlight& f);
    void cleanupExtract(InFlight& f);

    void publishAssembledEdition(InFlight& f);

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, Entry> m_index;
    QHash<QNetworkReply*, InFlight> m_resolving;
    InFlight* m_active = nullptr;
    QList<InFlight> m_queue;
    QProcess* m_proc = nullptr;
    ComicTorrents* m_torrents = nullptr;
};
