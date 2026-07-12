// AudiobookDownloader.h
//
// The audiobook half of the download-fed backbone. An audiobook is a torrent of
// N audio files (a single .m4b, or a set of .mp3 chapters). This ports
// BookDownloader's proven HTTP-stream-to-disk machinery but sources bytes from
// the Stremio engine's localhost HTTP instead of LibGen, and keys everything by
// `pairKey` (the title+author pairing identity) so a book page can flip to "Listen".
//
// Transport (proven live 2026-07-12 — see docs plan Task 0):
//   1. m_stream->prefetch(infoHash, 0) starts/adopts the Stremio engine AND
//      registers the torrent, then emits fetchReady(url, infoHash, 0). The url
//      (http://127.0.0.1:<port>/<infoHash>/0) gives us the engine base.
//   2. POST <base>/<infoHash>/create → JSON { files:[{path,name,length,offset}] }.
//      fileIdx = the index into that files[] array. Filter to audio extensions,
//      natural-sort, and each file streams from <base>/<infoHash>/<fileIdx>
//      COMPLETE (plain GET, no Range → whole file; proven, no buffer cap).
//   3. Stream each file to <appdata>/audiobooks/<pairKeyHash>/<NN - name>.<ext>
//      via chunked readyRead → .part → atomic rename (BookDownloader lineage).
//
// On-disk: <appdata>/audiobooks/<pairKeyHash>/ + <appdata>/audiobooks/index.json.
//
// Threading: pure QNetworkAccessManager + QObject lambdas on the main thread.

#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class StreamServer;

class AudiobookDownloader : public QObject {
    Q_OBJECT
public:
    // nam is the shared uncached NAM (audiobook bytes must not hit the image cache).
    // stream is the app's StreamServer (torrent→localhost HTTP); not owned.
    AudiobookDownloader(QNetworkAccessManager* nam, StreamServer* stream, QObject* parent = nullptr);
    ~AudiobookDownloader() override;

    // ---- QML entry points (exposed as context property `Audiobooks`) ----

    // Resolve the torrent's audio files via the Stream engine, then download each
    // to disk. Idempotent: an already-downloaded pairKey re-emits finished(); an
    // active pairKey is a no-op.
    Q_INVOKABLE void downloadAudiobook(const QString& pairKey, const QString& infoHash,
                                       const QString& title, const QString& author);

    // The local FLIP: absolute path of the audiobook's directory, or "" if not local.
    Q_INVOKABLE QString localAudiobook(const QString& pairKey) const;
    Q_INVOKABLE bool isDownloaded(const QString& pairKey) const;
    // Ordered local audio file paths for the player's chapter/playlist (or empty).
    Q_INVOKABLE QStringList localFiles(const QString& pairKey) const;
    // { state:"none"|"resolving"|"downloading"|"done", received, total }.
    Q_INVOKABLE QVariantMap statusOf(const QString& pairKey) const;
    Q_INVOKABLE QVariantList downloadedAudiobooks() const;
    Q_INVOKABLE void cancelDownload(const QString& pairKey);
    Q_INVOKABLE void deleteAudiobook(const QString& pairKey);

    // Dev smoke (env COLOSSEUM_ABB_DLTEST="<pairKey>|<infoHash>"): resolve +
    // download headlessly, logging the manifest + final paths. Mirrors BookDownloader::selfTest.
    void selfTest(const QString& pairKey, const QString& infoHash);

signals:
    void resolving(const QString& pairKey);
    void progress(const QString& pairKey, double received, double total);  // aggregate across files
    void finished(const QString& pairKey, const QString& dirPath);
    void failed(const QString& pairKey, const QString& reason);
    void removed(const QString& pairKey);

private:
    struct FileJob {
        int     fileIdx = 0;       // index into the manifest files[] = the streaming URL segment
        QString name;              // sanitized "NN - basename.ext"
        QString ext;
        qint64  bytes = 0;         // expected length from the manifest
        QString finalPath;
        QString partPath;
    };
    struct Job {
        QString pairKey, infoHash, title, author;
        QString baseUrl;                       // http://127.0.0.1:<port>/<infoHash>
        QList<FileJob> files;                  // resolved audio files, natural-sorted
        int     current = 0;                   // index into files being downloaded
        int     createAttempts = 0;            // manifest poll count (metadata may still be loading)
        qint64  doneBytes = 0;                 // bytes of fully-completed files
        qint64  totalBytes = 0;                // sum of all audio file lengths
        // in-flight per-file state
        QPointer<QNetworkReply> reply;
        QFile*  file = nullptr;
        qint64  fileReceived = 0;
        qint64  lastProgressEmit = 0;
        bool    sanityChecked = false;
    };

    // ── engine handshake ──
    void onFetchReady(const QString& url, const QString& infoHash, int fileIdx);
    void requestManifest(Job* job);
    void onManifestReply(QNetworkReply* reply, Job* job);
    QList<FileJob> parseManifest(const QByteArray& json) const;

    // ── per-file streaming (BookDownloader lineage) ──
    void startNextFile(Job* job);
    void onFileReadyRead();
    void onFileFinished();
    void finalizeJob(Job* job);
    void failJob(Job* job, const QString& reason);
    void cleanupInFlight(Job* job);
    void promoteQueue();                       // active done → start next queued

    Job* jobForHash(const QString& infoHash) const;
    bool isActive(const QString& pairKey) const;

    // ── disk + index ──
    QString baseDir() const;                   // <appdata>/audiobooks
    QString dirFor(const QString& pairKey) const;  // <appdata>/audiobooks/<sha1[:16]>
    void loadIndex();
    void saveIndex() const;

    struct Entry {
        QString dir;
        QStringList files;         // ordered absolute paths
        QString title, author;
        qint64  bytes = 0, addedAt = 0;
    };

    QNetworkAccessManager* m_nam = nullptr;
    StreamServer* m_stream = nullptr;
    Job* m_active = nullptr;
    QList<Job*> m_queue;
    QHash<QString, Entry> m_index;             // pairKey → entry
};
