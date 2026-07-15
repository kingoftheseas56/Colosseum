#include "ComicTorrents.h"

#include "ComicTorrentDownloader.h"
#include "ComicTorrentQueryPlanner.h"
#include "ComicTorrentRanker.h"
#include "TankorentSearchService.h"

#include <QDebug>

ComicTorrents::ComicTorrents(QNetworkAccessManager* searchNam, TorrentEngine* engine,
                             QObject* parent)
    : QObject(parent),
      m_search(new TankorentSearchService(searchNam, this)),
      m_downloader(new ComicTorrentDownloader(engine, this))
{
    wireSignals();
}

ComicTorrents::ComicTorrents(TankorentSearchService* search,
                             ComicTorrentDownloader* downloader, QObject* parent)
    : QObject(parent), m_search(search), m_downloader(downloader)
{
    if (m_search) m_search->setParent(this);
    if (m_downloader) m_downloader->setParent(this);
    wireSignals();
}

void ComicTorrents::wireSignals()
{
    connect(m_search, &TankorentSearchService::resultsReady,
            this, &ComicTorrents::onIndexerResults);
    connect(m_search, &TankorentSearchService::indexerError,
            this, &ComicTorrents::onIndexerError);
    connect(m_search, &TankorentSearchService::searchFinished,
            this, &ComicTorrents::onSearchFinished);
    connect(m_downloader, &ComicTorrentDownloader::progress,
            this, &ComicTorrents::progress);
    connect(m_downloader, &ComicTorrentDownloader::finished,
            this, &ComicTorrents::onDownloadFinished);
    connect(m_downloader, &ComicTorrentDownloader::failed,
            this, &ComicTorrents::onDownloadFailed);
    connect(m_downloader, &ComicTorrentDownloader::fileSelectionRequired,
            this, &ComicTorrents::archiveSelectionRequired);
    connect(m_downloader, &ComicTorrentDownloader::fileSelected,
            this, &ComicTorrents::archiveSelected);
}

void ComicTorrents::chooseArchive(const QString& issueId, int fileIndex)
{
    m_downloader->chooseFile(issueId.trimmed(), fileIndex);
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
    if (it != m_searchesByHandle.end()) {
        it.value().results += results;
        return;
    }
    if (m_sourceIssueByHandle.contains(handle))
        handleSourceResults(handle, results);
}

void ComicTorrents::onIndexerError(const QString& handle, const QString& indexerId,
                                   const QString& error)
{
    // Source-honest: a browse session survives partial indexer failure (the
    // successful indexers' results still reach the picker), but the failing
    // source is no longer silently dropped — it is logged, not hidden.
    if (m_sourceIssueByHandle.contains(handle))
        qInfo().noquote() << "[comic-sources] indexer failed:" << indexerId << "-" << error
                          << "(issue" << m_sourceIssueByHandle.value(handle) << ")";
}

void ComicTorrents::onSearchFinished(const QString& handle)
{
    auto it = m_searchesByHandle.find(handle);
    if (it != m_searchesByHandle.end()) {
        Request request = it.value();
        m_searchesByHandle.erase(it);
        m_handleByIssue.remove(request.issueId);
        const TorrentResult selected = ComicTorrentRanker::best(request.query, request.results);
        if (selected.infoHash.isEmpty()) {
            emit failed(request.issueId, QStringLiteral("no matching seeded comic torrent found"));
            return;
        }
        beginDownload(request, selected.infoHash, request.query, selected.magnetUri);
        return;
    }
    if (m_sourceIssueByHandle.contains(handle))
        handleSourceFinished(handle);
}

// ── Manual source browsing (v2) ─────────────────────────────────────────────
// These sessions never touch m_downloadsByIssue and are never reported through
// statusOf()/activeJobs(): browsing sources must not look like an acquisition.

void ComicTorrents::searchSources(const QString& issueIdIn, const QString& seriesTitle,
                                  const QString& editionTitle, const QString& isbn,
                                  const QString& collects)
{
    const QString issueId = issueIdIn.trimmed();
    if (issueId.isEmpty()) return;
    cancelSourceSearch(issueId);
    const QStringList queries =
        ComicTorrentQueryPlanner::automaticQueries(seriesTitle, editionTitle, isbn, collects);
    startSourceSession(issueId, seriesTitle, editionTitle, isbn, collects, queries);
}

void ComicTorrents::searchSourcesQuery(const QString& issueIdIn, const QString& query)
{
    const QString issueId = issueIdIn.trimmed();
    if (issueId.isEmpty()) return;
    const QStringList queries = ComicTorrentQueryPlanner::manualQuery(query);
    if (queries.isEmpty()) return;  // blank manual query — no-op (page guards this)
    // Preserve the edition identity so a manual query re-ranks against the same
    // edition; capture it before cancelSourceSearch tears the session down.
    const SourceSession prev = m_sourceSessionsByIssue.value(issueId);
    cancelSourceSearch(issueId);
    startSourceSession(issueId, prev.seriesTitle, prev.editionTitle, prev.isbn,
                       prev.collects, queries);
}

void ComicTorrents::cancelSourceSearch(const QString& issueIdIn)
{
    const QString issueId = issueIdIn.trimmed();
    auto it = m_sourceSessionsByIssue.find(issueId);
    if (it == m_sourceSessionsByIssue.end()) return;
    for (const QString& handle : it.value().pendingHandles) {
        m_search->cancelSearch(handle);
        m_sourceIssueByHandle.remove(handle);
    }
    m_sourceSessionsByIssue.erase(it);
}

void ComicTorrents::startSourceSession(const QString& issueId, const QString& seriesTitle,
                                       const QString& editionTitle, const QString& isbn,
                                       const QString& collects, const QStringList& queries)
{
    SourceSession session;
    session.issueId = issueId;
    session.seriesTitle = seriesTitle;
    session.editionTitle = editionTitle;
    session.isbn = isbn;
    session.collects = collects;
    // Built ONCE here, at the facade boundary — seriesId/catalogFormat are not
    // yet threaded through this Q_INVOKABLE surface, so format falls back to
    // whatever ComicEditionIdentity::buildTarget detects inside editionTitle
    // itself (still format-scoped, just not catalog-corroborated). QML-side
    // wiring of seriesId/catalogFormat is a later task.
    session.target = ComicEditionIdentity::buildTarget(issueId, QString(), seriesTitle,
                                                        editionTitle, QString(), isbn, collects);
    // Insert first so an (async) callback can always find its live session.
    m_sourceSessionsByIssue.insert(issueId, session);
    SourceSession& live = m_sourceSessionsByIssue[issueId];
    for (const QString& query : queries) {
        const QString handle = m_search->startSearch(QStringLiteral("comics"),
                                                     QStringLiteral("all"), query, 80);
        if (handle.isEmpty()) continue;
        live.pendingHandles.insert(handle);
        m_sourceIssueByHandle.insert(handle, issueId);
    }
    if (live.pendingHandles.isEmpty()) {
        m_sourceSessionsByIssue.remove(issueId);
        emit sourceSearchFailed(issueId, QStringLiteral("no comic torrent indexers available"));
    }
}

void ComicTorrents::handleSourceResults(const QString& handle,
                                        const QList<TorrentResult>& results)
{
    const QString issueId = m_sourceIssueByHandle.value(handle);
    if (issueId.isEmpty()) return;                 // stale handle from a replaced search
    auto it = m_sourceSessionsByIssue.find(issueId);
    if (it == m_sourceSessionsByIssue.end()) return;
    it.value().results += results;
    emit sourcesUpdated(issueId, sourceRows(it.value()), false);   // cumulative partial
}

void ComicTorrents::handleSourceFinished(const QString& handle)
{
    const QString issueId = m_sourceIssueByHandle.value(handle);
    if (issueId.isEmpty()) return;
    m_sourceIssueByHandle.remove(handle);
    auto it = m_sourceSessionsByIssue.find(issueId);
    if (it == m_sourceSessionsByIssue.end()) return;
    it.value().pendingHandles.remove(handle);
    if (!it.value().pendingHandles.isEmpty()) return;  // more queries still in flight
    const QVariantList rows = sourceRows(it.value());
    if (rows.isEmpty())
        emit sourceSearchFailed(issueId, QStringLiteral("No torrents matched this query."));
    else
        emit sourcesUpdated(issueId, rows, true);
    // Session is retained (identity only, no pending handles) so a manual query
    // can re-rank against the same edition; cancelSourceSearch clears it.
}

QVariantList ComicTorrents::sourceRows(const SourceSession& session) const
{
    return ComicTorrentRanker::toVariantRows(
        ComicTorrentRanker::rankForEdition(session.target, session.results));
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
