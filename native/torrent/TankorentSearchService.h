#pragma once

#include "TorrentResult.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QSet>

class QNetworkAccessManager;
class TorrentIndexer;

// Headless fan-out for Tankorent's federated torrent search. Owns indexer
// instantiation + dispatch + per-indexer signal forwarding. UI pages and
// background services (e.g. BookCatalogueAggregator) consume it via the
// 3-signal contract below.
//
// Concurrent searches: each startSearch() returns a unique handle. Multiple
// in-flight searches are supported (BookCatalogueAggregator fan-outs one per
// seriesId); the page-side UI only ever has one in flight at a time.
//
// QSettings contract preserved verbatim from TankorentPage's prior in-page
// dispatch: per-indexer enable flag at `tankorent/indexers/<id>/enabled`
// (default true) is honored exactly as the prior logic did.
//
// Authored as a follow-on to TORRENT_PERSISTENCE_COLLAPSE Phase 4 — see
// agents/HELP.md 2026-05-21 for the contract handshake with Agent 2.
class TankorentSearchService : public QObject
{
    Q_OBJECT
public:
    explicit TankorentSearchService(QNetworkAccessManager* nam,
                                    QObject* parent = nullptr);
    ~TankorentSearchService() override;

    // mediaType: "books" / "audiobooks" / "videos" / "comics" / ""
    //   When set, restricts to indexers in the internal allowlist for that
    //   type. Empty means no media-type filter (only sourceFilter applies).
    // sourceFilter: "all" or a specific indexer id (e.g. "piratebay"). When
    //   not "all", honors the explicit pick and skips the media-type allowlist.
    // categoryId: per-indexer opaque category string, passed through to
    //   TorrentIndexer::search verbatim (PirateBay uses numeric, etc).
    // Returns: handle string callers use to correlate signals; empty if no
    //   indexers matched the filter (no signals will fire for empty).
    QString startSearch(const QString& mediaType,
                        const QString& sourceFilter,
                        const QString& query,
                        int limit,
                        const QString& categoryId = {});

    void cancelSearch(const QString& handle);

    // True if `handle` has any indexers still pending. False once all
    // indexers in the batch have settled (success or error), or if the
    // handle was never started / already cancelled.
    bool isActive(const QString& handle) const;

signals:
    // Emitted once per indexer when that indexer's search completes
    // successfully. `results` is that indexer's partial slice; callers
    // typically accumulate across all `resultsReady` for the handle.
    void resultsReady(const QString& handle,
                      const QList<TorrentResult>& results);

    // Emitted once per indexer that errors out (network failure, parse
    // error, etc). Decrements the pending counter same as resultsReady.
    void indexerError(const QString& handle,
                      const QString& indexerId,
                      const QString& error);

    // Emitted exactly once per handle after all indexers in the batch
    // have settled. Callers use this to flip UI state (button visibility,
    // status text) without having to maintain their own pending counter.
    void searchFinished(const QString& handle);

protected:
    // Hook for tests: subclasses (TestableSearchService in tests) override
    // this to inject MockTorrentIndexer instances. Default impl reads
    // QSettings + the media-type allowlist + per-id enabled flag and
    // instantiates the real concrete indexer subclasses.
    virtual QList<TorrentIndexer*> buildIndexersFor(const QString& mediaType,
                                                    const QString& sourceFilter);

private:
    struct SearchContext {
        QList<TorrentIndexer*> activeIndexers;
        int pendingCount = 0;
    };

    // Snapshot of media-type → indexer-id allowlist. Was kMediaTypeIndexers
    // in TankorentPage; moved here so headless callers get the same routing.
    static QSet<QString> indexerIdsForMediaType(const QString& mediaType);

    void settleOne(const QString& handle);
    void cleanupContext(SearchContext& ctx);

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, SearchContext> m_contexts;
    quint64 m_handleSeq = 0;
};
