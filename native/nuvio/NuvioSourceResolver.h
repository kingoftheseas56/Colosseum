#pragma once

#include "NuvioRepositoryStore.h"
#include "NuvioWorkerProtocol.h"

#include <QHash>
#include <QProcess>
#include <QTemporaryDir>

class QJsonArray;

class NuvioSourceResolver final : public QObject
{
    Q_OBJECT

public:
    explicit NuvioSourceResolver(NuvioRepositoryStore *store, QString workerPath,
                                 QObject *parent = nullptr,
                                 int processDeadlineMs = NuvioWorkerProtocol::processDeadlineMs);
    ~NuvioSourceResolver() override;

    Q_INVOKABLE int resolve(const QString &mediaId, const QString &mediaType, int season, int episode);
    Q_INVOKABLE void cancel(int requestId);

signals:
    void streamsReady(int requestId, const QVariantList &streams);
    void finished(int requestId);

private:
    struct Search {
        int id = 0;
        QString imdbId;
        QString tmdbId;
        QString type;
        int season = 1;
        int episode = 1;
        bool lookingUp = false;
        QList<NuvioRepositoryStore::Plugin> queued;
    };

    struct Job {
        int searchId;
        bool lookup;
        NuvioRepositoryStore::Plugin plugin;
        QByteArray output;
        bool rejected = false;
    };

    void pump();
    void startWorker(const QStringList &arguments, bool lookup,
                     const NuvioRepositoryStore::Plugin &plugin = {});
    void drain(QProcess *process);
    void workerFinished(QProcess *process, bool success);
    void finishSearch();
    QVariantList directRows(const QJsonArray &streams, const NuvioRepositoryStore::Plugin &plugin) const;

    NuvioRepositoryStore *m_store;
    QString m_workerPath;
    int m_processDeadlineMs;
    QTemporaryDir m_retryDirectory;
    Search m_search;
    QHash<QProcess *, Job> m_jobs;
    QHash<QString, QString> m_tmdbIds;
    int m_nextSearchId = 0;
    bool m_pumping = false;
};
