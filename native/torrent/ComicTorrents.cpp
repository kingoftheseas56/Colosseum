#include "ComicTorrents.h"

#include "ComicTorrentDownloader.h"
#include "ComicTorrentRanker.h"
#include "TankorentSearchService.h"

ComicTorrents::ComicTorrents(QNetworkAccessManager* searchNam, TorrentEngine* engine,
                             QObject* parent)
    : QObject(parent),
      m_search(new TankorentSearchService(searchNam, this)),
      m_downloader(new ComicTorrentDownloader(engine, this))
{
    connect(m_search, &TankorentSearchService::resultsReady,
            this, &ComicTorrents::onIndexerResults);
    connect(m_search, &TankorentSearchService::searchFinished,
            this, &ComicTorrents::onSearchFinished);
    connect(m_downloader, &ComicTorrentDownloader::progress,
            this, &ComicTorrents::progress);
    connect(m_downloader, &ComicTorrentDownloader::finished,
            this, &ComicTorrents::onDownloadFinished);
    connect(m_downloader, &ComicTorrentDownloader::failed,
            this, &ComicTorrents::onDownloadFailed);
}

bool ComicTorrents::contains(const QString& issueId) const
{
    const QString id = issueId.trimmed();
    return m_handleByIssue.contains(id) || m_downloadsByIssue.contains(id);
}

void ComicTorrents::downloadIssue(const QString& issueIdIn, const QString& seriesId,
                                  const QString& seriesTitle, const QString& issueLabel,
                                  const QString& query)
{
    const QString issueId = issueIdIn.trimmed();
    if (issueId.isEmpty() || query.trimmed().isEmpty()) {
        emit failed(issueId, QStringLiteral("empty comic torrent search"));
        return;
    }
    if (contains(issueId)) return;

    Request request{issueId, seriesId, seriesTitle, issueLabel, query.trimmed(), {}, {}};
    const QString handle = m_search->startSearch(QStringLiteral("comics"), QStringLiteral("all"),
                                                  request.query, 30);
    if (handle.isEmpty()) {
        emit failed(issueId, QStringLiteral("no comic torrent indexers available"));
        return;
    }
    request.handle = handle;
    m_searchesByHandle.insert(handle, request);
    m_handleByIssue.insert(issueId, handle);
}

void ComicTorrents::downloadInfoHash(const QString& issueIdIn, const QString& seriesId,
                                     const QString& seriesTitle, const QString& issueLabel,
                                     const QString& infoHash, const QString& pickerTitle,
                                     const QString& magnetUri)
{
    const QString issueId = issueIdIn.trimmed();
    if (issueId.isEmpty() || contains(issueId)) return;
    Request request{issueId, seriesId, seriesTitle, issueLabel, pickerTitle, {}, {}};
    beginDownload(request, infoHash, pickerTitle, magnetUri);
}

void ComicTorrents::onIndexerResults(const QString& handle,
                                     const QList<TorrentResult>& results)
{
    auto it = m_searchesByHandle.find(handle);
    if (it != m_searchesByHandle.end()) it.value().results += results;
}

void ComicTorrents::onSearchFinished(const QString& handle)
{
    auto it = m_searchesByHandle.find(handle);
    if (it == m_searchesByHandle.end()) return;
    Request request = it.value();
    m_searchesByHandle.erase(it);
    m_handleByIssue.remove(request.issueId);
    const TorrentResult selected = ComicTorrentRanker::best(request.query, request.results);
    if (selected.infoHash.isEmpty()) {
        emit failed(request.issueId, QStringLiteral("no matching seeded comic torrent found"));
        return;
    }
    beginDownload(request, selected.infoHash, request.query, selected.magnetUri);
}

void ComicTorrents::beginDownload(Request request, const QString& infoHash,
                                  const QString& pickerTitle, const QString& magnetUri)
{
    m_downloadsByIssue.insert(request.issueId, request);
    m_downloader->download(request.issueId, infoHash, pickerTitle, magnetUri);
}

void ComicTorrents::onDownloadFinished(const QString& issueId, const QString& path)
{
    auto it = m_downloadsByIssue.find(issueId);
    if (it == m_downloadsByIssue.end()) return;
    const Request request = it.value();
    m_downloadsByIssue.erase(it);
    emit archiveReady(issueId, request.seriesId, request.seriesTitle,
                      request.issueLabel, path);
}

void ComicTorrents::onDownloadFailed(const QString& issueId, const QString& reason)
{
    m_downloadsByIssue.remove(issueId);
    emit failed(issueId, reason);
}

bool ComicTorrents::cancel(const QString& issueIdIn)
{
    const QString issueId = issueIdIn.trimmed();
    auto hit = m_handleByIssue.find(issueId);
    if (hit != m_handleByIssue.end()) {
        const QString handle = hit.value();
        m_search->cancelSearch(handle);
        m_handleByIssue.erase(hit);
        m_searchesByHandle.remove(handle);
        emit failed(issueId, QStringLiteral("cancelled by user"));
        return true;
    }
    return m_downloader->cancel(issueId);
}

QVariantMap ComicTorrents::statusOf(const QString& issueIdIn) const
{
    const QString issueId = issueIdIn.trimmed();
    if (m_handleByIssue.contains(issueId)) {
        return QVariantMap{{QStringLiteral("state"), QStringLiteral("resolving")},
                           {QStringLiteral("done"), 0.0},
                           {QStringLiteral("total"), 0.0}};
    }
    return m_downloader->statusOf(issueId);
}

QVariantList ComicTorrents::activeJobs() const
{
    QVariantList rows;
    for (const Request& request : m_searchesByHandle) {
        rows.append(QVariantMap{
            {QStringLiteral("id"), request.issueId},
            {QStringLiteral("seriesId"), request.seriesId},
            {QStringLiteral("seriesTitle"), request.seriesTitle},
            {QStringLiteral("label"), request.issueLabel},
            {QStringLiteral("state"), QStringLiteral("resolving")},
            {QStringLiteral("done"), 0.0},
            {QStringLiteral("total"), 0.0}
        });
    }
    const QVariantList downloads = m_downloader->activeJobs();
    for (const QVariant& value : downloads) {
        QVariantMap row = value.toMap();
        const Request request = m_downloadsByIssue.value(row.value(QStringLiteral("id")).toString());
        row[QStringLiteral("seriesId")] = request.seriesId;
        row[QStringLiteral("seriesTitle")] = request.seriesTitle;
        row[QStringLiteral("label")] = request.issueLabel;
        rows.append(row);
    }
    return rows;
}
