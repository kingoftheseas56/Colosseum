// BookTorrentDownloader.h
//
// Engine-direct transport (Phase 2): pulls the SINGLE best ebook file
// (BookTorrentFilePicker) through the imported libtorrent TorrentEngine —
// addMagnet -> metadataReady -> setFilePriorities -> torrentFinished -> finalize
// from disk. Keys everything by infoHash; concurrent (QHash of jobs, each its own Job).
//
// On-disk: <appdata>/books-torrent/<infoHash>/<torrent-relative path> + .../index.json
// (by infoHash). Multi-file packs keep their torrent-relative subfolders under the hash dir.
#pragma once

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QString>
#include <QVariantMap>

class TorrentEngine;

class BookTorrentDownloader : public QObject {
    Q_OBJECT
public:
    BookTorrentDownloader(TorrentEngine* engine, QObject* parent = nullptr);
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
        int     pickedIdx = -1;
        QString fileName;              // torrent-relative path from metadata (forward-slashed)
        qint64  totalBytes = 0, received = 0;
        qint64  lastProgressEmit = 0;
        bool    picked = false;        // metadata resolved + priorities set
    };

    // engine handlers (TorrentEngine's addMagnet -> metadataReady -> progress/finished/error)
    void onMetadataReady(const QString& infoHash, const QString& name, qint64 totalSize, const QJsonArray& files);
    void onEngineProgress(const QString& infoHash, float progress, int dl, int ul, int peers, int seeds);
    void onEngineFinished(const QString& infoHash);
    void onEngineFailed(const QString& infoHash, const QString& message);
    void applyMetadata(Job* job, const QJsonArray& files);   // pick+priorities+record (signal or synthesize)
    void finalizeJob(Job* job);
    void failJob(Job* job, const QString& reason);
    Job* jobForHash(const QString& infoHash) const;
    bool alive(Job* job) const;          // job is still the active entry for its hash
    // disk + index
    QString baseDir() const;
    QString dirFor(const QString& infoHash) const;
    void loadIndex();
    void saveIndex() const;

    struct Entry { QString path, title, author; qint64 bytes = 0, addedAt = 0; };

    TorrentEngine* m_engine = nullptr;
    QHash<QString, Job*> m_active;       // infoHash(lowercased) -> job
    QHash<QString, Entry> m_index;       // infoHash(lowercased) -> entry
};
