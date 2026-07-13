#pragma once

#include <QHash>
#include <QJsonArray>
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
    QVariantMap statusOf(const QString& issueId) const;
    QVariantList activeJobs() const;

signals:
    void resolving(const QString& issueId);
    void progress(const QString& issueId, double received, double total);
    void finished(const QString& issueId, const QString& path);
    void failed(const QString& issueId, const QString& reason);

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
    };

    Job* jobForHash(const QString& infoHash) const;
    Job* jobForIssue(const QString& issueId) const;
    bool alive(Job* job) const;
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void applyMetadata(Job* job, const QJsonArray& files);
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
