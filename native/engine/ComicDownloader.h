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

class ComicDownloader : public QObject
{
    Q_OBJECT
public:
    explicit ComicDownloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
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

signals:
    void progress(const QString& issueId, double done, double total);   // bytes
    void finished(const QString& issueId);
    void failed(const QString& issueId, const QString& reason);
    void removed(const QString& issueId);

private:
    struct Entry {
        QString seriesId;
        QString seriesTitle;
        QString label;
        QString dir;
        QStringList files;
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
        qint64 lastProgressEmit = 0;
        qint64 lastProgressBytes = 0;
        bool extracting = false;
        QString extractTmp;
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

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, Entry> m_index;
    QHash<QNetworkReply*, InFlight> m_resolving;
    InFlight* m_active = nullptr;
    QList<InFlight> m_queue;
    QProcess* m_proc = nullptr;
};
