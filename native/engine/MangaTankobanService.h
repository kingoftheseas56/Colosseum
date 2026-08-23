#pragma once

// The single Tankoban "volume mode" façade the QML page talks to (`TankobanVolumes`).
//
// Tasks 1–7 built the organs; this composes them into ONE object that owns the
// whole lifecycle of a tankōbon volume: it turns a MangaFire snapshot into
// canonical volume records (Task 1), searches Nyaa for a per-volume source
// (Task 2), lazily enriches each volume's synopsis (Task 3), downloads a chosen
// candidate through the restart-safe torrent transport (Task 6) or synthesizes
// the volume from WeebCentral chapters (Task 7), and ingests the result into the
// durable local index (Task 5) so it reads through MangaReader like any other
// download. ONE object owns terminal ("ready") state, so QML never has to reason
// about which collaborator finished.
//
// Testability by dependency injection: the DI constructor takes already-built
// collaborators (a fake Nyaa search, the real transport over a fake torrent
// engine, and real index/ingestor/enricher over a temp dir), so the whole
// search→choose→download→ingest→ready pipeline is provable offline without
// libtorrent (see tests/manga_tankoban_service_harness.cpp). The production
// constructor builds the same collaborators over the shared runtime NAMs and the
// real TorrentEngine.
//
// The concrete IMangaTorrentEngine adapter over the real (non-virtual)
// TorrentEngine lives here too, gated on HAS_LIBTORRENT so the harness (which
// never links libtorrent) compiles the façade without the engine.

#include "engine/MangaTankobanTypes.h"                 // VolumeRecord, SeriesSnapshot
#include "engine/MangaVolumeIndex.h"                   // MangaVolumeIndex, VolumeProvenance
#include "torrent/IMangaTorrentMetainfoResolver.h"     // TorrentMetainfo (Arc 18 seam)
#include "torrent/MangaNyaaSource.h"                   // MangaNyaaCandidate, MangaNyaaSource
#include "torrent/MangaTorrentIndex.h"                 // durable volume identity (Arc 18 M3)
#include "torrent/MangaVolumeTorrentDownloader.h"      // IMangaTorrentEngine, MangaVolumeTorrentDownloader

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class TorrentEngine;

namespace MangaTankoban {
class MangaVolumeArchiveIngestor;
class MangaSynopsisEnricher;
class MangaVolumePacker;
class MangaTorrentIndexer;
}

// ── Nyaa-search seam ─────────────────────────────────────────────────────────
// A minimal abstraction over MangaNyaaSource so the façade can be driven by a
// deterministic fake in tests. The real adapter forwards search() and re-emits
// the source's signals 1:1.
class IMangaNyaaSearch : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IMangaNyaaSearch() override = default;
    virtual void search(const MangaTankoban::SeriesSnapshot& series,
                        const QString& targetVolume) = 0;
    // Series-level search (catalogue-independence Slice 4, 2026-08-20): no
    // volume target — a shelf-less series' "Search nyaa" entry. series.seriesId
    // here is the caller's opaque result key (searchSeriesSources builds it).
    virtual void searchSeries(const MangaTankoban::SeriesSnapshot& series) = 0;
signals:
    void searchSucceeded(const QString& volumeId,
                         const QList<MangaTankoban::MangaNyaaCandidate>& rows);
    void searchFailed(const QString& volumeId, const QString& reason);
};

// Real Nyaa search: owns a MangaNyaaSource over the pinned/uncached search NAM.
class MangaNyaaSearchAdapter : public IMangaNyaaSearch {
    Q_OBJECT
public:
    explicit MangaNyaaSearchAdapter(QNetworkAccessManager* nam, QObject* parent = nullptr);
    void search(const MangaTankoban::SeriesSnapshot& series,
                const QString& targetVolume) override;
    void searchSeries(const MangaTankoban::SeriesSnapshot& series) override;
private:
    MangaTankoban::MangaNyaaSource* m_source = nullptr;
};

// ── Metainfo fetch seam (Arc 18 M5) ──────────────────────────────────────────
// Pulls one .torrent's BYTES for indexing — the only new network verb Arc 18
// adds. The service's indexer turns fetched bytes + a discovered candidate into
// verified volume mappings; without this seam the whole index-first path is off
// and the façade behaves exactly as before. `requestKey` groups results per
// search so concurrent volumes never cross wires.
class IMangaTorrentMetainfoFetcher : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IMangaTorrentMetainfoFetcher() override = default;
    virtual void fetch(const QString& url, const QString& requestKey) = 0;
signals:
    void fetched(const QString& requestKey, const QByteArray& torrentBytes);
    void fetchFailed(const QString& requestKey, const QString& reason);
};

// Real fetcher: one bounded GET (15 s transfer deadline) over a QNAM.
class MangaTorrentMetainfoFetcher : public IMangaTorrentMetainfoFetcher {
    Q_OBJECT
public:
    explicit MangaTorrentMetainfoFetcher(QNetworkAccessManager* nam,
                                         QObject* parent = nullptr);
    void fetch(const QString& url, const QString& requestKey) override;
private:
    QNetworkAccessManager* m_nam = nullptr;
};

#ifdef HAS_LIBTORRENT
// Real torrent-engine seam: wraps the concrete, non-virtual TorrentEngine and
// forwards addMagnet/setFilePriorities/startTorrent/removeTorrent/torrentFiles,
// re-emitting metadataReady/torrentProgress/torrentFinished/torrentError. The
// re-emit is connected Qt::QueuedConnection so a synchronous engine callback
// during removeTorrent can never re-enter the transport's teardown.
class MangaTorrentEngineAdapter : public IMangaTorrentEngine {
    Q_OBJECT
public:
    explicit MangaTorrentEngineAdapter(TorrentEngine* engine, QObject* parent = nullptr);
    QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused) override;
    void setFilePriorities(const QString& infoHash, const QVector<int>& priorities) override;
    void startTorrent(const QString& infoHash, const QString& savePath) override;
    void removeTorrent(const QString& infoHash, bool deleteFiles) override;
    QJsonArray torrentFiles(const QString& infoHash) const override;
private:
    TorrentEngine* m_engine = nullptr;
};
#endif // HAS_LIBTORRENT

class MangaTankobanService : public QObject {
    Q_OBJECT
public:
    // Production: builds every collaborator internally over the shared runtime
    // NAMs and the real TorrentEngine. `rootDir` defaults to AppDataLocation.
    MangaTankobanService(QNetworkAccessManager* searchNam,
                         QNetworkAccessManager* dlNam,
                         TorrentEngine* torrentEngine,
                         const QString& rootDir = QString(),
                         QObject* parent = nullptr);
    ~MangaTankobanService() override;   // frees the owned (non-QObject) indexer

    // Dependency-injected (tests): the caller owns every collaborator; the
    // façade only wires their signals and drives them. `packer` may be null when
    // the caller never exercises the WeebCentral fallback.
    //
    // Arc 18 M5 index-first trio (all optional, all-or-nothing): `metaResolver`
    // + `metaFetcher` + `torrentIndex` switch on Torrentio-style lookup —
    // verified mappings answer searchSources instantly, misses refresh through
    // discovery→metainfo→indexer→merged cards. Any of the three null keeps the
    // façade byte-identical to its pre-Arc-18 behavior.
    MangaTankobanService(IMangaNyaaSearch* search,
                         MangaVolumeTorrentDownloader* transport,
                         MangaTankoban::MangaVolumeIndex* index,
                         MangaTankoban::MangaVolumeArchiveIngestor* ingestor,
                         MangaTankoban::MangaSynopsisEnricher* enricher,
                         MangaTankoban::MangaVolumePacker* packer,
                         MangaTankoban::IMangaTorrentMetainfoResolver* metaResolver = nullptr,
                         IMangaTorrentMetainfoFetcher* metaFetcher = nullptr,
                         MangaTankoban::MangaTorrentIndex* torrentIndex = nullptr,
                         QObject* parent = nullptr);

    // ── QML API ─────────────────────────────────────────────────────────────
    Q_INVOKABLE void prepareSeries(QVariantMap descriptor, QVariantList volumes,
                                   QVariantList chapters);
    Q_INVOKABLE QVariantList volumesForSeries(QString seriesId) const;
    Q_INVOKABLE bool modeEnabled(QString seriesId) const;
    Q_INVOKABLE void setModeEnabled(QString seriesId, bool enabled);
    Q_INVOKABLE void searchSources(QString volumeId);
    // Series-level search (catalogue-independence Slice 4, 2026-08-20): the
    // shelf-less page's "Search nyaa" primary action, and the series-mode picker
    // entry. `key` is an opaque caller-chosen id (QML uses "series:"+seriesId)
    // results are grouped under — this series is never prepared into m_series/
    // m_volumes, so there is no volumeId to reuse. `sourcesReady`/`failed` land
    // on that same key.
    Q_INVOKABLE void searchSeriesSources(QString key, QString seriesTitle);
    Q_INVOKABLE void downloadNyaa(QString volumeId, QString infoHash);
    // Acquire a whole batch of volumes from ONE chosen torrent (design 2026-07-30).
    // Measured 2026-07-30 (tests/manga_volume_pack_probe.md): on-target SINGLE-volume
    // torrents are essentially absent — 0 in seven of eight top series — so for most
    // series the multi-volume pack IS the torrent route, not an exotic one.
    // The transport is already multi-intent (one Job per infoHash carrying an Intent
    // per volume, file priorities unioned), so this adds no download machinery.
    Q_INVOKABLE void downloadNyaaBatch(QStringList volumeIds, QString infoHash);
    Q_INVOKABLE void compileWeebCentral(QString volumeId);
    Q_INVOKABLE void cancel(QString volumeId);
    Q_INVOKABLE QVariantMap remove(QString volumeId);
    Q_INVOKABLE QVariantMap statusOf(QString volumeId) const;
    Q_INVOKABLE QVariantList localPages(QString volumeId) const;

    // ── Downloads-page surface (LocalDownloads composes these) ──────────────
    // Every published (ready) volume as a normalized row incl. first-page art.
    Q_INVOKABLE QVariantList downloadedVolumes() const;
    // Every acquisition currently in flight: {id, seriesTitle, label, state
    // ("resolving"|"downloading"|"ingesting"|"packing"), done, total}.
    Q_INVOKABLE QVariantList activeVolumeJobs() const;

    // ── Test-only end-to-end self-test (COLOSSEUM_TANKOBAN_DLTEST) ─────────────
    // Honest end-to-end proof, wired from main.cpp only when the env var is set
    // (an idle app never calls it, so it touches no network). Spec:
    //   "<magnet-or-infohash>|<seriesId>|<seriesTitle>|<volumeNumber>"
    // Builds ONE canonical snapshot + one candidate (a bare 40-hex hash becomes a
    // tracker-bearing magnet; a full magnet is used verbatim), then drives the
    // SAME transport→ingest→index path downloadNyaa uses AFTER its cache lookup —
    // bypassing the search-candidate cache. On the façade's finished(volumeId) it
    // asserts localPages(volumeId).size() > 0, prints "[tankoban-dltest] DONE" and
    // QCoreApplication::exit(0). ANY failure prints "[tankoban-dltest] FAIL
    // <reason>" and exit(2). A 240 s hard backstop guarantees a verdict.
    Q_INVOKABLE void runDownloadSelfTest(const QString& spec);

signals:
    void volumesChanged(const QString& seriesId);
    void sourcesReady(const QString& volumeId, const QVariantList& results);
    void progress(const QString& volumeId, double done, double total);
    void finished(const QString& volumeId);
    void failed(const QString& volumeId, const QString& reason);
    void removed(const QString& volumeId);
    void synopsisReady(const QString& volumeId);

private:
    void wireSignals();
    void onSourcesFound(const QString& volumeId,
                        const QList<MangaTankoban::MangaNyaaCandidate>& rows);
    void onTransportFinished(const QString& volumeId, const QString& archivePath);
    void onAcquired(const QString& volumeId);          // index now holds a ready volume
    void onFailed(const QString& volumeId, const QString& reason);
    // True while an acquisition is genuinely running (resolving / downloading /
    // ingesting / packing). A terminal "failed" state is NOT in-flight.
    bool isInFlight(const QString& volumeId) const;

    QVariantMap sourceCard(const MangaTankoban::MangaNyaaCandidate& candidate) const;
    QVariantMap weebCardFor(const QString& volumeId) const;
    QVariantMap volumeMap(const MangaTankoban::VolumeRecord& volume) const;
    MangaTankoban::VolumeProvenance provenanceFor(const MangaTankoban::VolumeRecord& volume,
                                                  const MangaTankoban::MangaNyaaCandidate& candidate) const;

    // ── Arc 18 M5 index-first lookup ─────────────────────────────────────────
    // Per-volume metainfo fetches during a refresh. Declared before the methods
    // that take its iterator.
    struct MetaFetch {
        MangaTankoban::SeriesSnapshot series;
        QList<MangaTankoban::MangaNyaaCandidate> rows;
        int pending = 0;
        // fetch requestKey ("volumeId#n") -> torrentUrl it was issued for, so a
        // fetched(bytes) callback can find ITS discovery row across concurrent
        // volume refreshes (the fetched signal carries only the key + bytes).
        QHash<QString, QString> urlByKey;
    };

    // True when the full trio (resolver + fetcher + open store) is live.
    bool indexPipelineActive() const;
    // The persisted file identity (fileIndex + path) of this volume's Verified
    // mapping under `normalizedHash` — the M6 transport expectation. Default
    // (fileIndex -1) when no verified row matches.
    MangaVolumeExpectation expectationFor(const QString& volumeId,
                                          const QString& normalizedHash) const;
    // A verified mapping rendered as a source card; STRONG means file-level
    // verified identity (never trusted-uploader/title evidence).
    QVariantMap indexedSourceCard(const MangaTankoban::VolumeMapping& mapping) const;
    // Cache verified mappings' candidates so downloadNyaa's arbitrary-infoHash
    // guard validates an indexed hash without a live Nyaa search.
    void cacheIndexedCandidates(const QString& volumeId,
                                const QList<MangaTankoban::VolumeMapping>& verified);
    // Emit `rows`-merged cards after a discovery refresh: indexed STRONG cards
    // first, then the discovery cards, then the WeebCentral fallback.
    void emitMergedSources(const QString& volumeId,
                           const QList<MangaTankoban::MangaNyaaCandidate>& rows);
    void onMetaFetched(const QString& requestKey, const QByteArray& torrentBytes);
    void onMetaFetchFailed(const QString& requestKey, const QString& reason);
    // Last fetch of a refresh resolved: record the provider answer and emit the
    // merged card set. Shared by the fetched/failed callbacks.
    void settleMetaFetch(QHash<QString, MetaFetch>::iterator it);
    // Identity freshness: verified rows older than this trigger ONE coalesced
    // background refresh on the next lookup (cards still answer instantly).
    static constexpr qint64 kIdentityTtlMs = 7 * 24 * 60 * 60 * 1000LL;

    // Collaborators. Parented to `this` in the production ctor; borrowed (not
    // owned) in the DI ctor.
    IMangaNyaaSearch* m_search = nullptr;
    MangaVolumeTorrentDownloader* m_transport = nullptr;
    MangaTankoban::MangaVolumeIndex* m_index = nullptr;
    MangaTankoban::MangaVolumeArchiveIngestor* m_ingestor = nullptr;
    MangaTankoban::MangaSynopsisEnricher* m_enricher = nullptr;
    MangaTankoban::MangaVolumePacker* m_packer = nullptr;

    // Arc 18 index-first trio (borrowed in both ctors; indexer owned).
    MangaTankoban::IMangaTorrentMetainfoResolver* m_metaResolver = nullptr;
    IMangaTorrentMetainfoFetcher* m_metaFetcher = nullptr;
    MangaTankoban::MangaTorrentIndex* m_tindex = nullptr;
    MangaTankoban::MangaTorrentIndexer* m_indexer = nullptr; // built when the trio is live

    // Canonical model + acquisition bookkeeping, all keyed by the same ids.
    QHash<QString, MangaTankoban::SeriesSnapshot> m_series;                     // seriesId -> snapshot
    QHash<QString, MangaTankoban::VolumeRecord> m_volumes;                      // volumeId -> record
    QHash<QString, QList<MangaTankoban::MangaNyaaCandidate>> m_candidates;      // volumeId -> cached search
    QHash<QString, MangaTankoban::MangaNyaaCandidate> m_chosen;                 // volumeId -> chosen candidate
    QHash<QString, QVariantMap> m_acq;                                          // volumeId -> in-flight status
    QSet<QString> m_tornDown;   // volumeIds explicitly cancelled/removed — never resurrect as ready

    // Arc 18 M5 refresh bookkeeping.
    QSet<QString> m_indexRefreshPending;   // volumeIds with a discovery/index refresh in flight
    QHash<QString, MetaFetch> m_metaFetches;
};
