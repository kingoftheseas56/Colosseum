#pragma once

#include "ComicTorrentFilePicker.h"   // ComicArchiveCandidate, ComicArchiveDecision

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class TorrentEngine;

class ComicTorrentDownloader : public QObject
{
    Q_OBJECT
public:
    explicit ComicTorrentDownloader(TorrentEngine* engine, QObject* parent = nullptr);
    ~ComicTorrentDownloader() override;

    void download(const QString& issueId, const QString& infoHash, const QString& title,
                  const QString& magnetUri = QString());
    bool cancel(const QString& issueId);
    // Commit a user-chosen archive from an ambiguous, paused torrent. Returns
    // false for an unknown issue or a non-eligible file index.
    bool chooseFile(const QString& issueId, int fileIndex);
    QVariantMap statusOf(const QString& issueId) const;
    QVariantList activeJobs() const;

signals:
    void resolving(const QString& issueId);
    void progress(const QString& issueId, double received, double total);
    void finished(const QString& issueId, const QString& path);
    void failed(const QString& issueId, const QString& reason);
    // Ambiguous manifest paused for a manual choice among eligible archives.
    void fileSelectionRequired(const QString& issueId, const QVariantList& files);
    // A comic archive was committed — automatic=true for lone/unique-exact picks.
    void fileSelected(const QString& issueId, const QString& fileName, bool automatic);

private:
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
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void applyMetadata(Job* job, const QJsonArray& files);
    void applyPickedFile(Job* job, int pickedIdx, const QString& name, qint64 bytes,
                         int fileCount, bool automatic);
    QVariantList toVariantFiles(const QList<ComicArchiveCandidate>& candidates) const;
    void onEngineProgress(const QString& infoHash, float progress,
                          int downloadRate, int uploadRate, int peers, int seeds);
    void onEngineFinished(const QString& infoHash);
    void onEngineFailed(const QString& infoHash, const QString& message);
    void finalizeJob(Job* job);
    void failJob(Job* job, const QString& reason);
    QString baseDir() const;
    QString dirFor(const QString& infoHash) const;

    TorrentEngine* m_engine = nullptr;
    QHash<QString, Job*> m_byHash;
    QHash<QString, QString> m_hashByIssue;
};
