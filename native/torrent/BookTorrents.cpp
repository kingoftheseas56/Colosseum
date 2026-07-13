#include "BookTorrents.h"

#include "TankorentSearchService.h"
#include "BookTorrentDownloader.h"
#include "BookTorrentRanker.h"

BookTorrents::BookTorrents(QNetworkAccessManager* searchNam, QNetworkAccessManager* dlNam,
                           StreamServer* stream, QObject* parent)
    : QObject(parent),
      m_search(new TankorentSearchService(searchNam, this)),
      m_dl(new BookTorrentDownloader(dlNam, stream, this))
{
    // Search wiring — connected ONCE to member slots (never per-search, which would
    // stack connections and, with Qt::UniqueConnection being a no-op for lambdas, cause
    // duplicated emissions + use-after-free of a heap accumulator).
    connect(m_search, &TankorentSearchService::resultsReady,   this, &BookTorrents::onIndexerResults);
    connect(m_search, &TankorentSearchService::searchFinished, this, &BookTorrents::onSearchDone);
    // Re-broadcast downloader signals so QML rows bind by infoHash.
    connect(m_dl, &BookTorrentDownloader::resolving, this, &BookTorrents::resolving);
    connect(m_dl, &BookTorrentDownloader::progress,  this, &BookTorrents::progress);
    connect(m_dl, &BookTorrentDownloader::finished,  this, &BookTorrents::finished);
    connect(m_dl, &BookTorrentDownloader::failed,    this, &BookTorrents::failed);
}

void BookTorrents::search(const QString& title, const QString& author)
{
    m_title = title; m_author = author;
    if (!m_handle.isEmpty()) m_search->cancelSearch(m_handle);   // supersede the prior open shelf
    m_accum.clear();
    m_handle = m_search->startSearch(QStringLiteral("books"), QStringLiteral("all"),
                                     title + QStringLiteral(" ") + author, 30);
    if (m_handle.isEmpty()) { emit resultsReady({}); emit searchFinished(); }
}

void BookTorrents::onIndexerResults(const QString& handle, const QList<TorrentResult>& r)
{
    if (handle != m_handle) return;     // drop stale results from a superseded search
    m_accum += r;
}

void BookTorrents::onSearchDone(const QString& handle)
{
    if (handle != m_handle) return;
    const auto ranked = BookTorrentRanker::rank(m_title, m_author, m_accum);
    QVariantList rows;
    for (const auto& rt : ranked) {
        if (rt.src.infoHash.isEmpty()) continue;    // undownloadable without a hash
        if (!BookTorrentRanker::isReadableBook(rt.src)) continue;   // reading shelf = readable books only;
                                                                    // audiobooks/video have no reader here
        QVariantMap m;
        m[QStringLiteral("title")]     = rt.src.title;
        m[QStringLiteral("infoHash")]  = rt.src.infoHash;
        m[QStringLiteral("seeders")]   = rt.src.seeders;
        m[QStringLiteral("sizeBytes")] = static_cast<double>(rt.src.sizeBytes);
        m[QStringLiteral("size")]      = humanSize(rt.src.sizeBytes);
        m[QStringLiteral("format")]    = rt.formatGuess;
        m[QStringLiteral("pack")]      = rt.pack;
        m[QStringLiteral("source")]    = rt.src.sourceName;
        rows.push_back(m);
    }
    m_handle.clear(); m_accum.clear();
    emit resultsReady(rows);
    emit searchFinished();
}

void BookTorrents::download(const QString& infoHash, const QString& title, const QString& author)
{
    m_dl->download(infoHash, title, author);
}
bool        BookTorrents::isDownloaded(const QString& h) const { return m_dl->isDownloaded(h); }
QString     BookTorrents::localFile(const QString& h) const    { return m_dl->localFile(h); }
QVariantMap BookTorrents::statusOf(const QString& h) const     { return m_dl->statusOf(h); }
