// BookDownloader.h
//
// The book half of the download-fed backbone: reading is NEVER a live stream.
// A book is downloaded once — its .epub/.pdf lands as a loose file on disk —
// and the reader opens that local file, offline, forever. This ports Tankoban 2's
// proven BookDownloader (HTTP / LibGen path) into Colosseum-lean form: the
// irreducible core is kept; TB2's magnet/libtorrent transport, MD5-of-bytes
// verification, and cross-mirror failover beyond LibGen are dropped (Colosseum
// has no TorrentClient — its books come from LibGen over HTTP).
//
// Pipeline (mirrors TB2 BookDownloader + LibGenScraper::resolveDownload):
//   1. resolve: GET libgen.li/ads.php?md5=<md5> → parse <a href="get.php?...key=Y">
//      → the ephemeral direct-file URL(s). The key rotates ~60s, so resolve is
//      done immediately before streaming (fresh key = the safe pattern).
//   2. stream: GET the direct URL → write <dir>/<name>.part in chunks (readyRead,
//      NEVER readAll — books can be 100s of MB), stale-key detection on the first
//      chunk (text/html ⇒ key rotated ⇒ failover to next URL), retry 2/4/8s,
//      then atomic .part → final rename.
//   3. index: persist {md5 → path, title, bytes, addedAt} to index.json.
//   4. reader calls localBook(md5) → the on-disk file path, or "" (UI then shows
//      "go download it" — it NEVER falls back to streaming).
//
// On-disk layout (under QStandardPaths::AppDataLocation, NOT the purgeable
// CacheLocation the image cache uses):
//   <appdata>/books/<name>.epub ...
//   <appdata>/books/index.json
//
// Threading: pure QNetworkAccessManager + QObject lambdas on the main thread.

#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

class BookDownloader : public QObject
{
    Q_OBJECT
public:
    // nam is a plain (uncached) NAM owned by the app — book bytes must never be
    // served from the image disk-cache. Mirrors MangaDownloader's dlNam.
    explicit BookDownloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~BookDownloader() override;

    // ---- QML entry points ----

    // Resolve a LibGen md5 to its fresh direct URL, then stream it to disk.
    // Idempotent: an already-downloaded md5 re-emits finished() with its path;
    // an already-active/queued md5 is a no-op. `suggestedName` is the filename
    // to save as (e.g. "Dune.epub"); `title` is stored in the index for display.
    Q_INVOKABLE void downloadBook(const QString& md5,
                                  const QString& suggestedName,
                                  const QString& title = QString(),
                                  double expectedBytes = 0);

    // The local-read FLIP. Returns the absolute on-disk path of a downloaded
    // book, or "" if it isn't downloaded — the reader shows "go download it" on
    // empty, it NEVER falls back to streaming.
    Q_INVOKABLE QString localBook(const QString& md5) const;

    // True once the book file is on disk.
    Q_INVOKABLE bool isDownloaded(const QString& md5) const;

    // Live status for binding a row's affordance without waiting for a signal:
    // { state:"none"|"resolving"|"downloading"|"queued"|"done", received, total }.
    Q_INVOKABLE QVariantMap statusOf(const QString& md5) const;

    // Cancel a resolving / in-flight / queued download (aborts, drops partials).
    Q_INVOKABLE void cancelDownload(const QString& md5);

    // Delete a downloaded book (file + index entry). Emits removed().
    Q_INVOKABLE void deleteBook(const QString& md5);

    // Dev smoke (env COLOSSEUM_BOOK_DLTEST=<md5>): resolve + download a book,
    // logging the resolved URL(s) + final path. Proves the whole pipeline
    // headlessly, without driving the GUI. Mirrors MangaDownloader::selfTest.
    void selfTest(const QString& md5);

signals:
    void resolving(const QString& md5);
    void progress(const QString& md5, double received, double total);
    void finished(const QString& md5, const QString& filePath);
    void failed(const QString& md5, const QString& reason);
    void removed(const QString& md5);

private:
    // ── resolve (ads.php → get.php urls) ──
    struct ResolveCtx {
        QString md5;
        QString suggestedName;
        QString title;
        qint64  expectedBytes = 0;
    };
    void onResolveFinished(QNetworkReply* reply);
    QStringList parseResolveHtml(const QByteArray& html) const;

    // ── HTTP streaming download (ported from TB2 BookDownloader, HTTP path) ──
    struct InFlight {
        QString     md5;
        QString     title;
        QStringList urls;          // remaining URLs to try (front = current)
        int         urlIdx = 0;
        int         attempt = 0;   // retry attempt for the current URL (0-based)
        QString     suggestedName;
        qint64      expectedBytes = 0;

        QPointer<QNetworkReply> reply;
        QFile*      file = nullptr;
        QString     partPath;
        QString     finalPath;

        bool        sanityChecked = false;
        qint64      lastProgressEmit = 0;
        qint64      lastProgressBytes = 0;
        qint64      receivedBytes = 0;
    };

    void startDownload(const QString& md5, const QString& title,
                       const QStringList& urls, const QString& suggestedName,
                       qint64 expectedBytes);
    void startAttempt(InFlight& f);
    void onReadyRead();
    void onFinished();
    void onProgressFromReply(qint64 received, qint64 total);
    void retryOrFailover(InFlight& f, const QString& reason);
    void startNextUrlOrFail(InFlight& f);
    void failAndCleanup(InFlight& f, const QString& reason);
    void finalizeSuccess(InFlight& f);
    void closeAndDeletePart(InFlight& f);
    bool detectStaleHtml(const QByteArray& firstChunk, const QString& contentType) const;
    bool pickTargetFilename(InFlight& f);

    bool isActive(const QString& md5) const;

    // ── disk + index ──
    QString baseDir() const;                 // <appdata>/books
    void loadIndex();
    void saveIndex() const;
    void writeEntry(const InFlight& f);

    struct Entry {
        QString path;
        QString title;
        qint64  bytes = 0;
        qint64  addedAt = 0;
    };

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QNetworkReply*, ResolveCtx> m_resolving;   // ads.php fetches in flight
    InFlight*       m_active = nullptr;
    QList<InFlight> m_queue;
    QHash<QString, Entry> m_index;                   // md5 → downloaded entry
};
