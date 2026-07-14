#pragma once

#include "TorrentResult.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
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
    // Dependency-injection seam: takes ownership of an already-built search
    // service and downloader (tests inject a mock TankorentSearchService).
    ComicTorrents(TankorentSearchService* search, ComicTorrentDownloader* downloader,
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

    // ── Manual source browsing (v2) ──────────────────────────────────────────
    // A cancellable, edition-aware search whose sessions are held SEPARATELY
    // from download jobs and are invisible to statusOf()/activeJobs(): merely
    // browsing sources must never register as a Downloads-page acquisition.
    void searchSources(const QString& issueId, const QString& seriesTitle,
                       const QString& editionTitle, const QString& isbn,
                       const QString& collects);
    void searchSourcesQuery(const QString& issueId, const QString& query);
    void cancelSourceSearch(const QString& issueId);
    // Commit a user-chosen archive from an ambiguous, paused torrent.
    void chooseArchive(const QString& issueId, int fileIndex);

signals:
    void progress(const QString& issueId, double received, double total);
    void archiveReady(const QString& issueId, const QString& seriesId,
                      const QString& seriesTitle, const QString& issueLabel,
                      const QString& archivePath);
    void failed(const QString& issueId, const QString& reason);
    // Source browsing: cumulative partial rows then one complete=true update.
    void sourcesUpdated(const QString& issueId, const QVariantList& rows, bool complete);
    // Search-only failure — never the terminal acquisition failed() signal.
    void sourceSearchFailed(const QString& issueId, const QString& reason);
    // Ambiguous manifest paused for a manual archive choice, then the outcome.
    void archiveSelectionRequired(const QString& issueId, const QVariantList& files);
    void archiveSelected(const QString& issueId, const QString& fileName, bool automatic);

private slots:
    void onIndexerResults(const QString& handle, const QList<TorrentResult>& results);
    void onIndexerError(const QString& handle, const QString& indexerId, const QString& error);
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

    // A live browse session, keyed by edition issueId. Holds edition identity
    // so a later manual query re-ranks against the same edition, and the set of
    // in-flight query handles so completion fires once all settle.
    struct SourceSession {
        QString issueId;
        QString seriesTitle;
        QString editionTitle;
        QString isbn;
        QString collects;
        QSet<QString> pendingHandles;
        QList<TorrentResult> results;
    };

    void wireSignals();
    void beginDownload(Request request, const QString& infoHash, const QString& pickerTitle,
                       const QString& magnetUri = QString());
    void failRequest(const QString& issueId, const QString& reason);

    void startSourceSession(const QString& issueId, const QString& seriesTitle,
                            const QString& editionTitle, const QString& isbn,
                            const QString& collects, const QStringList& queries);
    void handleSourceResults(const QString& handle, const QList<TorrentResult>& results);
    void handleSourceFinished(const QString& handle);
    QVariantList sourceRows(const SourceSession& session) const;

    TankorentSearchService* m_search = nullptr;
    ComicTorrentDownloader* m_downloader = nullptr;
    QHash<QString, Request> m_searchesByHandle;
    QHash<QString, QString> m_handleByIssue;
    QHash<QString, Request> m_downloadsByIssue;
    // Browse sessions live here ONLY — deliberately absent from statusOf()/activeJobs().
    QHash<QString, SourceSession> m_sourceSessionsByIssue;
    QHash<QString, QString> m_sourceIssueByHandle;
};
