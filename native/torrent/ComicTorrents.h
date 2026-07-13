#pragma once

#include "TorrentResult.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class TorrentEngine;
class TankorentSearchService;
class ComicTorrentDownloader;

// Private comics transport facade. ComicDownloader owns this object and is the
// only QML-visible surface, preserving the issue-id reader contract.
class ComicTorrents : public QObject
{
    Q_OBJECT
public:
    ComicTorrents(QNetworkAccessManager* searchNam, TorrentEngine* engine,
                  QObject* parent = nullptr);

    void downloadIssue(const QString& issueId, const QString& seriesId,
                       const QString& seriesTitle, const QString& issueLabel,
                       const QString& query);
    void downloadInfoHash(const QString& issueId, const QString& seriesId,
                          const QString& seriesTitle, const QString& issueLabel,
                          const QString& infoHash, const QString& pickerTitle,
                          const QString& magnetUri = QString());
    bool cancel(const QString& issueId);
    QVariantMap statusOf(const QString& issueId) const;
    QVariantList activeJobs() const;
    bool contains(const QString& issueId) const;

signals:
    void progress(const QString& issueId, double received, double total);
    void archiveReady(const QString& issueId, const QString& seriesId,
                      const QString& seriesTitle, const QString& issueLabel,
                      const QString& archivePath);
    void failed(const QString& issueId, const QString& reason);

private slots:
    void onIndexerResults(const QString& handle, const QList<TorrentResult>& results);
    void onSearchFinished(const QString& handle);
    void onDownloadFinished(const QString& issueId, const QString& path);
    void onDownloadFailed(const QString& issueId, const QString& reason);

private:
    struct Request {
        QString issueId;
        QString seriesId;
        QString seriesTitle;
        QString issueLabel;
        QString query;
        QString handle;
        QList<TorrentResult> results;
    };

    void beginDownload(Request request, const QString& infoHash, const QString& pickerTitle,
                       const QString& magnetUri = QString());
    void failRequest(const QString& issueId, const QString& reason);

    TankorentSearchService* m_search = nullptr;
    ComicTorrentDownloader* m_downloader = nullptr;
    QHash<QString, Request> m_searchesByHandle;
    QHash<QString, QString> m_handleByIssue;
    QHash<QString, Request> m_downloadsByIssue;
};
