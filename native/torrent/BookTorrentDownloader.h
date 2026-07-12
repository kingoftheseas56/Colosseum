// BookTorrentDownloader.h
//
// Sibling transport to AudiobookDownloader: same proven Stremio engine handshake
// (prefetch → fetchReady/pollEngine watchdog → POST /create manifest), but pulls the
// SINGLE best ebook file (BookTorrentFilePicker) and keys everything by infoHash.
// Concurrent: multiple infoHashes can download at once (QHash of jobs), each its own Job.
//
// On-disk: <appdata>/books-torrent/<infoHash>/<name>.<ext> + .../index.json (by infoHash).
#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class StreamServer;

class BookTorrentDownloader : public QObject {
    Q_OBJECT
public:
    BookTorrentDownloader(QNetworkAccessManager* nam, StreamServer* stream, QObject* parent = nullptr);
    ~BookTorrentDownloader() override;

    Q_INVOKABLE void download(const QString& infoHash, const QString& title, const QString& author);
    Q_INVOKABLE QString localFile(const QString& infoHash) const;   // path or ""
    Q_INVOKABLE bool    isDownloaded(const QString& infoHash) const;
    Q_INVOKABLE QVariantMap statusOf(const QString& infoHash) const; // {state,received,total}
    Q_INVOKABLE void cancelDownload(const QString& infoHash);

    void selfTest(const QString& infoHash, const QString& title); // COLOSSEUM_TORRENT_DLTEST

signals:
    void resolving(const QString& infoHash);
    void progress(const QString& infoHash, double received, double total);
    void finished(const QString& infoHash, const QString& path);
    void failed(const QString& infoHash, const QString& reason);

private:
    struct Job {
        QString infoHash, title, author;
        QString baseUrl;                 // http://127.0.0.1:<port>/<infoHash>
        int     pickedIdx = -1;
        QString fileName, ext;
        qint64  totalBytes = 0, received = 0;
        int     enginePolls = 0, createAttempts = 0;
        qint64  lastProgressEmit = 0;
        QPointer<QNetworkReply> reply;
        QFile*  file = nullptr;
        QString finalPath, partPath;
    };

    // engine handshake (adapted from AudiobookDownloader; liveness keyed to the QHash)
    void onFetchReady(const QString& url, const QString& infoHash, int fileIdx);
    void beginManifest(Job* job, const QString& url);
    void pollEngine(Job* job);
    void requestManifest(Job* job);
    void onManifestReply(QNetworkReply* reply, Job* job);
    // single-file streaming
    void startFile(Job* job);
    void onFileReadyRead();
    void onFileFinished();
    void finalizeJob(Job* job);
    void failJob(Job* job, const QString& reason);
    void cleanupInFlight(Job* job);
    Job* jobForHash(const QString& infoHash) const;
    Job* jobForReply(QNetworkReply* r) const;
    bool alive(Job* job) const;          // job is still the active entry for its hash
    // disk + index
    QString baseDir() const;
    QString dirFor(const QString& infoHash) const;
    void loadIndex();
    void saveIndex() const;

    struct Entry { QString path, title, author; qint64 bytes = 0, addedAt = 0; };

    QNetworkAccessManager* m_nam = nullptr;
    StreamServer* m_stream = nullptr;
    QHash<QString, Job*> m_active;       // infoHash(lowercased) -> job
    QHash<QString, Entry> m_index;       // infoHash(lowercased) -> entry
};
