#include "TankorentSearchService.h"

#include "TorrentIndexer.h"
#include "PirateBayIndexer.h"
#include "ExtTorrentsIndexer.h"
#include "TorrentsCsvIndexer.h"
#include "KnabenIndexer.h"

#include <QCoreApplication>
#include <QSettings>

namespace {

// Single-purpose trackers (YTS = movies, EZTV = TV, Nyaa = anime/manga) ride
// only with their matching media types; general-purpose trackers ride all.
// MOVED VERBATIM from TankorentPage.cpp during the headless-service extraction
// (HELP.md 2026-05-21). Any future edit MUST keep this as the single source
// of truth — the page no longer carries its own copy.
// Colosseum port (2026-07-13): only the 3 keyless, CF-free book indexers survive.
// 1337x/nyaa/yts/eztv were dropped — 1337x needs a QtWebEngine CF-cookie harvester
// (banned), the rest are video/anime lanes Biblio doesn't use.
// 2026-07-16: added "knaben" — a keyless meta-search AGGREGATOR that reaches 1337x
// (and dozens of other trackers) through its own backend, so we get 1337x results
// without ever fighting its Cloudflare wall. Its API is CF-fronted but not
// challenge-walled for Qt's QNAM (proven in-process, tests/knaben_probe.cpp).
// Additive, not a replacement: the 3 direct indexers stay as a resilient floor
// if knaben's domain hops (it's moved .eu -> .org -> .xyz before).
const QHash<QString, QSet<QString>> kMediaTypeIndexers = {
    { "books",      { "piratebay", "exttorrents", "torrentscsv", "knaben" } },
    { "audiobooks", { "piratebay", "exttorrents", "torrentscsv", "knaben" } },
    { "comics",     { "piratebay", "exttorrents", "torrentscsv", "knaben" } },
};

} // anonymous namespace

TankorentSearchService::TankorentSearchService(QNetworkAccessManager* nam,
                                               QObject* parent)
    : QObject(parent), m_nam(nam)
{
}

TankorentSearchService::~TankorentSearchService()
{
    // Defensive: any still-pending contexts get their indexers cleaned up.
    // Callers should cancelSearch() before destroying the service but we
    // don't trust that.
    for (auto& ctx : m_contexts)
        cleanupContext(ctx);
}

QSet<QString> TankorentSearchService::indexerIdsForMediaType(const QString& mediaType)
{
    return kMediaTypeIndexers.value(mediaType);
}

QList<TorrentIndexer*> TankorentSearchService::buildIndexersFor(const QString& mediaType,
                                                                 const QString& sourceFilter)
{
    const QSet<QString> allowed = indexerIdsForMediaType(mediaType);
    const bool hasAllowlist = !allowed.isEmpty();
    const bool explicitSource = (sourceFilter != "all");

    QSettings settings;
    auto wanted = [&](const QString& id) -> bool {
        if (explicitSource) {
            // Explicit source pick bypasses the media-type allowlist
            // (Hemanth 2026-04-20: explicit-pick must not be second-guessed).
            if (sourceFilter != id)
                return false;
        } else if (hasAllowlist && !allowed.contains(id)) {
            return false;
        }
        return settings.value(
            QStringLiteral("tankorent/indexers/%1/enabled").arg(id), true).toBool();
    };

    QList<TorrentIndexer*> out;
    auto addIf = [&](const QString& id, TorrentIndexer* indexer) {
        if (wanted(id))
            out.append(indexer);
        else
            delete indexer;
    };

    addIf("piratebay",    new PirateBayIndexer(m_nam, this));
    addIf("exttorrents",  new ExtTorrentsIndexer(m_nam, this));
    addIf("torrentscsv",  new TorrentsCsvIndexer(m_nam, this));
    addIf("knaben",       new KnabenIndexer(m_nam, this));

    return out;
}

QString TankorentSearchService::startSearch(const QString& mediaType,
                                            const QString& sourceFilter,
                                            const QString& query,
                                            int limit,
                                            const QString& categoryId)
{
    QList<TorrentIndexer*> indexers = buildIndexersFor(mediaType, sourceFilter);
    if (indexers.isEmpty())
        return {};

    const QString handle = QStringLiteral("search-%1").arg(++m_handleSeq);
    SearchContext ctx;
    ctx.activeIndexers = indexers;
    ctx.pendingCount = indexers.size();
    m_contexts.insert(handle, ctx);

    for (auto* idx : indexers) {
        const QString indexerId = idx->id();
        connect(idx, &TorrentIndexer::searchFinished, this,
                [this, handle, indexerId](const QList<TorrentResult>& results) {
            // Re-check the context — caller may have cancelled mid-flight.
            if (!m_contexts.contains(handle))
                return;
            emit resultsReady(handle, results);
            settleOne(handle);
        });
        connect(idx, &TorrentIndexer::searchError, this,
                [this, handle, indexerId](const QString& error) {
            if (!m_contexts.contains(handle))
                return;
            emit indexerError(handle, indexerId, error);
            settleOne(handle);
        });
        // Restore TB2's source-side filtering: if the caller didn't pin a category,
        // ask each indexer for its own book/audiobook category so off-type results
        // (e.g. audiobooks) never come back to be misclassified downstream.
        const QString cat = categoryId.isEmpty() ? idx->categoryFor(mediaType) : categoryId;
        idx->search(query, limit, cat);
    }

    return handle;
}

void TankorentSearchService::cancelSearch(const QString& handle)
{
    auto it = m_contexts.find(handle);
    if (it == m_contexts.end())
        return;
    cleanupContext(it.value());
    m_contexts.erase(it);
    // No searchFinished emit on cancel — callers explicitly asked to stop;
    // the prior in-page cancel didn't fire any completion signal either.
}

bool TankorentSearchService::isActive(const QString& handle) const
{
    auto it = m_contexts.find(handle);
    return it != m_contexts.end() && it.value().pendingCount > 0;
}

void TankorentSearchService::settleOne(const QString& handle)
{
    auto it = m_contexts.find(handle);
    if (it == m_contexts.end())
        return;
    SearchContext& ctx = it.value();
    if (--ctx.pendingCount > 0)
        return;

    // All indexers settled — clean up, emit terminal signal, drop context.
    cleanupContext(ctx);
    m_contexts.erase(it);
    emit searchFinished(handle);
}

void TankorentSearchService::cleanupContext(SearchContext& ctx)
{
    for (auto* idx : ctx.activeIndexers) {
        if (!idx) continue;
        idx->disconnect(this);
        idx->deleteLater();
    }
    ctx.activeIndexers.clear();
    ctx.pendingCount = 0;
}

void TankorentSearchService::selfTest(const QString& query)
{
    const QString handle = startSearch("books", "all", query, 30);
    if (handle.isEmpty()) {
        qInfo() << "[torrent-smoke] NO indexers matched";
        QCoreApplication::quit();
        return;
    }
    connect(this, &TankorentSearchService::resultsReady, this,
            [](const QString&, const QList<TorrentResult>& r) {
                qInfo() << "[torrent-smoke] indexer returned" << r.size() << "rows";
                for (const auto& t : r)
                    if (t.infoHash.size() == 40 && t.seeders > 0)
                        // Show source + origin tracker (knaben carries the real
                        // origin, e.g. 1337x, in `category`) so multi-indexer
                        // provenance is legible in the smoke.
                        qInfo().noquote() << "  [hit]" << t.sourceKey
                                          << (t.category.isEmpty() ? QString() : "(" + t.category + ")")
                                          << t.seeders << "seeders" << t.infoHash << t.title;
            });
    connect(this, &TankorentSearchService::indexerError, this,
            [](const QString&, const QString& id, const QString& e) {
                qInfo() << "[torrent-smoke] ERROR" << id << e;
            });
    connect(this, &TankorentSearchService::searchFinished, this,
            [](const QString&) {
                qInfo() << "[torrent-smoke] finished";
                QCoreApplication::quit();
            });
}
