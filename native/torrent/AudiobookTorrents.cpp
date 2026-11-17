#include "AudiobookTorrents.h"

#include "TankorentSearchService.h"
#include "BookTorrentRanker.h"

#include <QSet>
#include <QStringList>
#include <algorithm>

AudiobookTorrents::AudiobookTorrents(QNetworkAccessManager* searchNam, QObject* parent)
    : QObject(parent), m_search(new TankorentSearchService(searchNam, this))
{
    wire();
}

AudiobookTorrents::AudiobookTorrents(TankorentSearchService* search, QObject* parent)
    : QObject(parent), m_search(search)
{
    if (m_search) m_search->setParent(this);
    wire();
}

void AudiobookTorrents::wire()
{
    // Connected ONCE to member slots (never per-search: stacked connections would
    // duplicate emissions).
    connect(m_search, &TankorentSearchService::resultsReady,   this, &AudiobookTorrents::onIndexerResults);
    connect(m_search, &TankorentSearchService::searchFinished, this, &AudiobookTorrents::onSearchDone);
}

void AudiobookTorrents::search(const QString& title, const QString& author)
{
    m_title = title; m_author = author;
    if (!m_handle.isEmpty()) m_search->cancelSearch(m_handle);   // supersede the prior search
    m_accum.clear();
    m_handle = m_search->startSearch(QStringLiteral("audiobooks"), QStringLiteral("all"),
                                     (title + QStringLiteral(" ") + author).trimmed(), 30);
    // Empty handle = every indexer switched off (tankorent/indexers/<id>/enabled):
    // answer immediately with nothing rather than waiting for a signal that never comes.
    if (m_handle.isEmpty()) { emit resultsReady({}); emit searchFinished(); }
}

void AudiobookTorrents::onIndexerResults(const QString& handle, const QList<TorrentResult>& r)
{
    if (handle != m_handle) return;     // drop stale results from a superseded search
    m_accum += r;
}

void AudiobookTorrents::onSearchDone(const QString& handle)
{
    if (handle != m_handle) return;
    const QVariantList rows = buildRows(m_title, m_author, m_accum);
    m_handle.clear(); m_accum.clear();
    emit resultsReady(rows);
    emit searchFinished();
}

QVariantList AudiobookTorrents::buildRows(const QString& title, const QString& author,
                                          const QList<TorrentResult>& raw)
{
    // Reuse the ranker's dedup + title scoring (tier, then seeders), then let author
    // agreement break ties inside a title bucket. stable_sort keeps the seeder order.
    auto ranked = BookTorrentRanker::rank(title, author, raw);
    const QStringList authorToks = BookTorrentRanker::stripArticles(author)
                                       .split(' ', Qt::SkipEmptyParts);
    auto authorHit = [&](const RankedTorrent& rt) {
        if (authorToks.isEmpty()) return false;
        const QStringList t = BookTorrentRanker::stripArticles(rt.src.title).split(' ', Qt::SkipEmptyParts);
        const QSet<QString> have(t.cbegin(), t.cend());
        for (const auto& tok : authorToks) if (!have.contains(tok)) return false;
        return true;
    };
    auto bucket = [](int tier) { return tier == 3 ? 2 : tier; };   // same merge as the ranker
    std::stable_sort(ranked.begin(), ranked.end(), [&](const RankedTorrent& a, const RankedTorrent& b) {
        const int ba = bucket(a.matchTier), bb = bucket(b.matchTier);
        if (ba != bb) return ba > bb;
        return authorHit(a) && !authorHit(b);
    });

    QVariantList rows;
    for (const auto& rt : ranked) {
        if (rt.src.infoHash.isEmpty()) continue;                  // undownloadable without a hash
        if (!BookTorrentRanker::isAudiobook(rt.src)) continue;    // audio only
        QVariantMap m;
        m[QStringLiteral("title")]     = rt.src.title;
        m[QStringLiteral("infoHash")]  = rt.src.infoHash;
        m[QStringLiteral("seeders")]   = rt.src.seeders;
        m[QStringLiteral("sizeBytes")] = static_cast<double>(rt.src.sizeBytes);
        m[QStringLiteral("size")]      = humanSize(rt.src.sizeBytes);
        m[QStringLiteral("source")]    = rt.src.sourceName;
        m[QStringLiteral("format")]    = BookTorrentRanker::guessAudioFormat(rt.src.title);
        m[QStringLiteral("bundle")]    = BookTorrentRanker::looksLikeBundle(rt.src.title);
        rows.push_back(m);
    }
    return rows;
}
