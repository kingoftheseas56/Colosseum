// AudiobookDownloader.h
//
// Audiobook storage and downloads share one recording index. Torrent sources
// obtain local files from the torrent engine.
// Storage and progress use a recording key; the title+author pair key identifies
// the work for read-along.
//
// Transport (proven live 2026-07-12 — see docs plan Task 0):
//   1. m_stream->prefetch(infoHash, 0) starts/adopts the Stremio engine AND
//      registers the torrent, then emits fetchReady(url, infoHash, 0). The url
//      (http://127.0.0.1:<port>/<infoHash>/0) gives us the engine base.
//   2. POST <base>/<infoHash>/create → JSON { files:[{path,name,length,offset}] }.
//      fileIdx = the index into that files[] array. Filter to audio extensions,
//      natural-sort, and each file streams from <base>/<infoHash>/<fileIdx>
//      COMPLETE (plain GET, no Range → whole file; proven, no buffer cap).
//   3. Stream each file to <appdata>/audiobooks/<recordingKeyHash>/<NN - name>.<ext>
//      via chunked readyRead → .part → atomic rename (BookDownloader lineage).
//
// On-disk: <appdata>/audiobooks/<recordingKeyHash>/ + <appdata>/audiobooks/index.json.
//
// Threading: pure QNetworkAccessManager + QObject lambdas on the main thread.

#pragma once

#include "DownloadLocation.h"

#include <QObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <QByteArray>
#include <utility>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class StreamServer;
class AudioPairingStore;
class AudiobookTorrentTransport;

class AudiobookDownloader : public QObject {
    Q_OBJECT
    // Live job count (active + queued). The shell folds this into LocalDownloads'
    // count so an audiobook download pops the SAME taskbar badge as any other
    // download (Hemanth 2026-07-20: "behave the same way as any other download").
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
public:
    // nam is the shared uncached NAM (audiobook bytes must not hit the image cache).
    // stream is the app's StreamServer (torrent→localhost HTTP); not owned.
    AudiobookDownloader(QNetworkAccessManager* nam, StreamServer* stream, QObject* parent = nullptr);
    ~AudiobookDownloader() override;
    void setSourceEnabledCheck(std::function<bool()> check) { m_sourceEnabledCheck = std::move(check); }
    void setTankorentEnabledCheck(std::function<bool()> check) { m_tankorentEnabledCheck = std::move(check); }
    // C++ wiring only; Android injects the existing download-to-keep engine.
    void setTorrentTransport(AudiobookTorrentTransport* transport);

    // The read-along pairing store. When set, a completed download auto-attaches
    // the audiobook to its book — the pairing is written under `bookId` (see
    // downloadAudiobook's bookId arg) with NO pairing UI: you downloaded the
    // audiobook FROM the book's page, so the system already knows the book.
    void setPairing(AudioPairingStore* s) { m_pairing = s; }

    // ---- QML entry points (exposed as context property `Audiobooks`) ----

    // Resolve the torrent's audio files via the Stream engine, then download each
    // to disk. Legacy calls retain work-key signals; the downloaded content is
    // selected by source recording identity, so different narrations never collide.
    //
    // bookId (optional) is the READER'S book identity — BookStores::keyFor(<the
    // ebook's file path>), the same key ReaderShell.bookId / Task 13's Audio tab
    // looks the pairing up by. On completion, if a pairing store is set and bookId
    // is non-empty, the audiobook auto-attaches to that book. Pass "" (the QML side
    // does when no ebook is on disk yet) to skip the attach — never a mismatched key.
    //
    // bookPath (optional, LAST so existing callers keep compiling) is the ebook's local
    // path. When supplied it is stamped into the auto-attach pairing so the read-along
    // service can find the EPUB without re-deriving it. Empty leaves the key absent.
    Q_INVOKABLE void downloadAudiobook(const QString& pairKey, const QString& infoHash,
                                       const QString& title, const QString& author,
                                       const QString& bookId = QString(),
                                       const QString& bookPath = QString());
    // Optional workId identifies the AudioSilo catalogue work. bookId still identifies
    // the local ebook for read-along pairing. Bundles carry neither work identity.
    Q_INVOKABLE void downloadRecording(const QVariantMap& request);
    Q_INVOKABLE QString resolveRecordingKey(const QString& workOrRecordingKey) const;
    Q_INVOKABLE QVariantList recordingsForWork(const QString& pairKey) const;
    Q_INVOKABLE bool setDefaultRecording(const QString& pairKey, const QString& recordingKey);
    Q_INVOKABLE bool markRecordingPlayed(const QString& recordingKey);

    // The local FLIP: explicit recording key, or unambiguous work key; empty if not local.
    Q_INVOKABLE QString localAudiobook(const QString& pairKey) const;
    Q_INVOKABLE bool isDownloaded(const QString& pairKey) const;
    // Ordered local audio file paths for the player's chapter/playlist (or empty).
    Q_INVOKABLE QStringList localFiles(const QString& pairKey) const;
    // { state:"none"|"resolving"|"downloading"|"done", received, total }.
    Q_INVOKABLE QVariantMap statusOf(const QString& pairKey) const;
    Q_INVOKABLE QVariantList downloadedAudiobooks() const;
    QList<DownloadLocation::Item> managedDownloadItems() const;
    void refreshDownloadPaths();
    void downloadLocationAvailable() { promoteQueue(); }
    Q_SIGNAL void downloadPathsChanged();
    // In-flight jobs (active + queued): [{id,title,author,state,received,total}] — the
    // Downloads page SEEDS its live map from this on open. Signals alone can't backfill
    // a job that started before the page existed (the 2026-07-20 invisible-download gap:
    // resolving() fired once at click time, then a cold engine moved no bytes for 60s+,
    // so a freshly opened Downloads page showed nothing while the download ran).
    Q_INVOKABLE QVariantList activeDownloads() const;
    int activeCount() const { return (m_active ? 1 : 0) + m_queue.size(); }
    Q_INVOKABLE void cancelDownload(const QString& pairKey);
    Q_INVOKABLE QVariantMap deleteAudiobook(const QString& pairKey);
    Q_INVOKABLE QVariantMap removeIncompleteDownloads();
    Q_INVOKABLE void dismissFailure(const QString& pairKey);

    // Dev smoke (env COLOSSEUM_ABB_DLTEST="<pairKey>|<infoHash>"): resolve +
    // download headlessly, logging the manifest + final paths. Mirrors BookDownloader::selfTest.
    void selfTest(const QString& pairKey, const QString& infoHash);

signals:
    void activeCountChanged();
    void failuresChanged();
    void resolving(const QString& pairKey);
    void progress(const QString& pairKey, double received, double total);  // aggregate across files
    void finished(const QString& pairKey, const QString& dirPath);
    void failed(const QString& pairKey, const QString& reason);
    void removed(const QString& pairKey);

private:
    std::function<bool()> m_sourceEnabledCheck;
    std::function<bool()> m_tankorentEnabledCheck;
    struct FileJob {
        int     fileIdx = 0;       // index into the manifest files[] = the streaming URL segment
        QString name;              // sanitized "NN - basename.ext"
        QString ext;
        qint64  bytes = 0;         // expected length from the manifest
        QString finalPath;
        QString partPath;
    };
    struct Job {
        QString pairKey, recordingKey, signalKey, infoHash, title, author;
        QString workId, audiosiloRecordingId, format, abridgement, language, sourceKind, sourceRef;
        QStringList narrators;
        bool bundle = false;
        quint64 generation = 0;
        bool nativeTorrent = false;
        QString nativePhase = QStringLiteral("resolving");
        QString completedDir;
        QString downloadDir;
        QString manifestReason;
        QString baseUrl;                       // http://127.0.0.1:<port>/<infoHash>
        QList<FileJob> files;                  // resolved audio files, natural-sorted
        int     current = 0;                   // index into files being downloaded
        int     enginePolls = 0;               // watchdog ticks waiting for the engine port
        int     createAttempts = 0;            // manifest poll count (metadata may still be loading)
        qint64  doneBytes = 0;                 // bytes of fully-completed files
        qint64  totalBytes = 0;                // sum of all audio file lengths
        // in-flight per-file state
        QPointer<QNetworkReply> reply;
        QFile*  file = nullptr;
        qint64  fileReceived = 0;
        qint64  lastProgressEmit = 0;
    };

    // ── engine handshake ──
    void onFetchReady(const QString& url, const QString& infoHash, int fileIdx);
    void beginManifest(Job* job, const QString& url);   // derive base from a stream URL, start manifest
    void pollEngine(Job* job);                          // watchdog: poll streamUrl (fetchReady can be lost)
    void requestManifest(Job* job);
    void onManifestReply(QNetworkReply* reply, Job* job);
    QList<FileJob> parseManifest(const QByteArray& json) const;

    // ── per-file streaming (BookDownloader lineage) ──
    void startNextFile(Job* job);
    void finishDownloadedFile(Job* job);
    void reportFileProgress(Job* job);
    void onFileReadyRead();
    void onFileFinished();
    void finalizeJob(Job* job);
    void failJob(Job* job, const QString& reason);
    void rememberFailure(const QString& recordingKey, const QString& title,
                         const QString& author, const QString& reason, const QString& workId = {},
                         qint64 received = 0, qint64 total = 0);
    void cancelJob(Job* job);
    void retireCancelledJob(Job* job);
    void cleanupInFlight(Job* job);
    void promoteQueue();                       // active done → start next queued

    Job* jobForHash(const QString& infoHash) const;
    bool isActive(const QString& pairKey) const;

    // Read-along auto-attach: called at every finished() emit site. Writes the
    // pairing (audiobook pairKey + dirPath) under the remembered bookId, keyed the
    // SAME way Task 13's Audio tab reads it. No-op when no store or no bookId.
    void attachToBook(const QString& recordingKey, const QString& dirPath);
    void beginDownload(const QVariantMap& request, bool legacyCaller);

    // ── disk + index ──
    QString baseDir() const;                   // <appdata>/audiobooks
    QString dirFor(const QString& recordingKey) const; // <appdata>/audiobooks/<sha1[:16]>
    void loadIndex();
    bool saveIndex() const;

    struct Entry {
        QString dir;
        QStringList files;         // ordered absolute paths
        QString title, author, pairKey, recordingKey;
        QString workId, audiosiloRecordingId, format, abridgement, language, sourceKind, sourceRef;
        QStringList narrators;
        bool bundle = false, preferred = false;
        QString bookId, bookPath;
        qint64 bytes = 0, addedAt = 0, lastPlayedAt = 0;
    };

    QPointer<AudiobookTorrentTransport> m_torrent;
    quint64 m_nextGeneration = 0;
    QNetworkAccessManager* m_nam = nullptr;
    StreamServer* m_stream = nullptr;
    Job* m_active = nullptr;
    QList<Job*> m_queue;
    QHash<QString, Entry> m_index;             // recordingKey → entry
    QHash<QString, QVariantMap> m_failures;
    AudioPairingStore* m_pairing = nullptr;    // read-along attach target (not owned)
    QHash<QString, QString> m_bookIdFor;       // recordingKey → reader bookId (for auto-attach)
    QHash<QString, QString> m_bookPathFor;     // recordingKey → ebook local path (stamped into pairing)
};
