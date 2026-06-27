// MangaDownloader.h
//
// The download-fed backbone: reading is NEVER a live stream. A chapter is
// downloaded once — its page images land as loose files on disk — and the
// reader then reads those local files, offline, forever. This recreates
// Tankoban 2 / the Electron app's proven downloader in Colosseum-lean form:
// the irreducible core (fetch page URLs -> download images -> JSON index ->
// localPages flip) is kept; TB2's CBZ packing / followed-library / history-cap
// are deferred to a later pass (justified up only when needed).
//
// Pipeline (mirrors TB2 + mangaDownloads.js):
//   1. WeebCentralScraper::fetchPages(chapterId)  -> [{index, imageUrl}]
//   2. for each page: GET image -> write <dir>/page_NNN.<ext>  (3 retries,
//      2/4/8s backoff; resume skips existing files > 1 KB; bounded concurrency)
//   3. write an index entry {chapterId -> dir, files[], pageCount, bytes}
//   4. reader calls localPages(chapterId) -> file:/// URLs for the saved pages
//
// On-disk layout (under QStandardPaths::AppDataLocation, NOT the purgeable
// CacheLocation the image cache uses):
//   <appdata>/manga/<series>/<chapter>/page_000.jpg ...
//   <appdata>/manga/index.json
//
// Threading: pure QNetworkAccessManager + QObject lambdas on the main thread.

#pragma once

#include "MangaResult.h"

#include <QObject>
#include <QHash>
#include <QList>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class WeebCentralScraper;

class MangaDownloader : public QObject
{
    Q_OBJECT
public:
    // nam is shared with the rest of the app (carries the IPv4-pin / Host fix),
    // so image fetches use the same proven networking the streaming reader did.
    explicit MangaDownloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaDownloader() override;

    // ---- QML entry points ----

    // Queue a single chapter for download. Idempotent: an already-downloaded or
    // already-active chapter is a no-op (re-emits finished for the downloaded one).
    Q_INVOKABLE void downloadChapter(const QString& chapterId,
                                     const QString& seriesId,
                                     const QString& seriesTitle,
                                     const QString& chapterLabel);

    // The local-read FLIP. Returns [{index:int, url:"file:///.../page_NNN.ext"}]
    // for a downloaded chapter, or an empty list if it isn't downloaded — the
    // reader shows "go download it" on empty, it NEVER falls back to streaming.
    Q_INVOKABLE QVariantList localPages(const QString& chapterId) const;

    // True once the chapter is on disk with at least one page.
    Q_INVOKABLE bool isDownloaded(const QString& chapterId) const;

    // Live status for binding a row's affordance without waiting for a signal:
    // { state:"none"|"queued"|"downloading"|"done", done:int, total:int }.
    Q_INVOKABLE QVariantMap statusOf(const QString& chapterId) const;

    // Delete a downloaded chapter (loose files + index entry). Emits removed().
    Q_INVOKABLE void deleteChapter(const QString& chapterId);
    // Cancel a queued or in-flight download (aborts replies, drops partials). Emits failed(reason="cancelled").
    Q_INVOKABLE void cancelDownload(const QString& chapterId);

    // Resolve a chapter's THUMBNAIL = its first page. Downloaded -> local file (instant);
    // otherwise scrape the first page once (capped concurrency, cached). Always answers
    // exactly once via thumbReady(chapterId, url) ("" = no thumb).
    Q_INVOKABLE void fetchThumb(const QString& seriesId, const QString& chapterId);

    // Dev smoke (env COLOSSEUM_DL_SELFTEST=<title>): resolve a title -> its earliest
    // chapter -> download it, logging page count + localPages. Proves the whole
    // pipeline headlessly, without driving the GUI. Mirrors MangaEngine::selfTest.
    void selfTest(const QString& seriesTitle);

signals:
    void progress(const QString& chapterId, int done, int total);
    void finished(const QString& chapterId);
    void failed(const QString& chapterId, const QString& reason);
    void removed(const QString& chapterId);
    void thumbReady(const QString& chapterId, const QString& url);

private:
    struct Job {
        QString chapterId;
        QString seriesId;
        QString seriesTitle;
        QString chapterLabel;
        QString dir;                 // resolved chapter directory
        WeebCentralScraper* scraper = nullptr;
        QList<PageInfo> pages;
        QStringList files;           // index-aligned saved filenames ("" until saved)
        int total = 0;
        int done = 0;
        int nextDispatch = 0;        // next page index to GET
        int inFlight = 0;
        qint64 bytes = 0;
        bool failedFlag = false;
        bool cancelled = false;
        QList<QNetworkReply*> replies;   // in-flight image GETs, for cancel/abort
    };

    struct Entry {
        QString seriesId;
        QString seriesTitle;
        QString chapterLabel;
        QString dir;
        QStringList files;
        qint64 bytes = 0;
        qint64 addedAt = 0;
    };

    // queue pump
    void pumpQueue();
    void beginJob(Job* job);
    void onPagesReady(Job* job, const QList<PageInfo>& pages);
    void pumpImages(Job* job);
    void fetchImage(Job* job, int pageIndex, int attempt);
    void onImageSaved(Job* job, int pageIndex, const QString& fileName, qint64 size);
    void failJob(Job* job, const QString& reason);
    void finishJob(Job* job);
    void cleanupJob(Job* job);

    // disk + index
    QString baseDir() const;                       // <appdata>/manga
    QString chapterDir(const QString& seriesId, const QString& chapterId) const;
    static QString safeSeg(const QString& v);      // path-segment sanitiser
    static QString extForContentType(const QString& ct, const QString& fallbackUrl);
    void loadIndex();
    void saveIndex() const;
    void writeEntry(const Job* job);

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, Entry> m_index;                 // chapterId -> entry
    QHash<QString, Job*>  m_active;                 // chapterId -> in-flight job
    QQueue<Job*>          m_queue;                  // waiting jobs

    static constexpr int MAX_CONCURRENT_CHAPTERS = 2;
    static constexpr int IMAGE_CONCURRENCY       = 3;
    static constexpr int MAX_IMAGE_RETRIES       = 3;
    static constexpr qint64 MIN_VALID_BYTES      = 1024;   // < 1 KB = truncated/placeholder
};
