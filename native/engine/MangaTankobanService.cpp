// native/engine/MangaTankobanService.cpp
#include "engine/MangaTankobanService.h"
#include "engine/DownloadFileOps.h"

#include "engine/MangaTankobanLogic.h"          // MangaTankoban::prepareSeries / volumeId / settingsKey
#include "engine/MangaSynopsisEnricher.h"
#include "engine/MangaVolumeArchiveIngestor.h"
#include "engine/MangaVolumeIndex.h"
#include "engine/MangaVolumePacker.h"
#include "torrent/ComicTorrentMagnet.h"         // infoHash() — extract a canonical hash from a magnet-or-hash
#include "torrent/BookTorrentMagnet.h"          // buildMagnet() — tracker-bearing magnet for a bare infohash
#include "torrent/MangaTorrentIndexer.h"        // Arc 18 M4 coordinator (seam-driven, libtorrent-free)

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#ifdef HAS_LIBTORRENT
#include <QFutureWatcher>
#include <QMetaObject>
#include <QtConcurrent>
#endif
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <utility>

#ifdef HAS_LIBTORRENT
#include "engine/WeebCentralScraper.h"
#include "torrent/MangaTorrentMetainfoResolver.h" // Arc 18 M2 real decode (needs libtorrent link)
#include "torrent/engine/TorrentEngine.h"
#endif

using namespace MangaTankoban;

// ── MangaNyaaSearchAdapter ───────────────────────────────────────────────────

MangaNyaaSearchAdapter::MangaNyaaSearchAdapter(QNetworkAccessManager* nam, QObject* parent)
    : IMangaNyaaSearch(parent), m_source(new MangaNyaaSource(nam, this))
{
    connect(m_source, &MangaNyaaSource::searchSucceeded,
            this, &IMangaNyaaSearch::searchSucceeded);
    connect(m_source, &MangaNyaaSource::searchFailed,
            this, &IMangaNyaaSearch::searchFailed);
}

void MangaNyaaSearchAdapter::search(const SeriesSnapshot& series, const QString& targetVolume)
{
    m_source->search(series, targetVolume);
}

void MangaNyaaSearchAdapter::searchSeries(const SeriesSnapshot& series)
{
    m_source->searchSeries(series);
}

// ── MangaTorrentMetainfoFetcher (Arc 18 M5) ──────────────────────────────────

MangaTorrentMetainfoFetcher::MangaTorrentMetainfoFetcher(QNetworkAccessManager* nam,
                                                         QObject* parent)
    : IMangaTorrentMetainfoFetcher(parent), m_nam(nam)
{
}

void MangaTorrentMetainfoFetcher::fetch(const QString& url, const QString& requestKey)
{
    if (!m_nam || url.isEmpty()) {
        emit fetchFailed(requestKey, QStringLiteral("no fetcher network / empty url"));
        return;
    }
    QNetworkRequest request{QUrl(url)}; // braces: paren form hits the vexing parse
    request.setTransferTimeout(15000); // bounded: one dead host must not stall the merge
    QNetworkReply* reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestKey]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit fetchFailed(requestKey, reply->errorString());
            return;
        }
        emit fetched(requestKey, reply->readAll());
    });
}

// ── MangaTorrentEngineAdapter (real engine, HAS_LIBTORRENT only) ──────────────

#ifdef HAS_LIBTORRENT
MangaTorrentEngineAdapter::MangaTorrentEngineAdapter(TorrentEngine* engine, QObject* parent)
    : IMangaTorrentEngine(parent), m_engine(engine)
{
    if (!m_engine)
        return;

    // Re-emit engine signals via QUEUED connections: a synchronous re-emit that
    // TorrentEngine could fire while we are inside removeTorrent() (teardown)
    // then lands on the transport on the next event-loop turn, never re-entrant.
    connect(m_engine, &TorrentEngine::metadataReady,
            this, &IMangaTorrentEngine::metadataReady, Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::torrentProgress,
            this, &IMangaTorrentEngine::torrentProgress, Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::torrentFinished,
            this, &IMangaTorrentEngine::torrentFinished, Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::torrentError,
            this, &IMangaTorrentEngine::torrentError, Qt::QueuedConnection);
}

QString MangaTorrentEngineAdapter::addMagnet(const QString& magnetUri, const QString& savePath, bool paused)
{
    // Bring the shared engine LIVE on first use — exactly like the comic/book downloaders
    // (ComicTorrentDownloader / BookTorrentDownloader) do. The engine is constructed dormant
    // (empty listen_interfaces, DHT disabled) and only start() → applySettings() turns
    // networking on. The manga-volume path never did this, so a volume download added its
    // magnet to a network-DEAD session → no DHT, no peers, metadata never resolved and it sat
    // at "Finding source…" forever (unless a comic/book download had already started the
    // engine). start() is idempotent (no-op when already running).
    if (!m_engine)
        return {};
    if (!m_engine->isRunning())
        m_engine->start();
    // Nyaa's RSS publishes only nyaa:infoHash — no trackers — so the manga path
    // historically handed the engine BARE magnets and metadata resolution rode
    // DHT alone. On this network DHT is unreliable (2026-08-16: 3 of 4 bootstrap
    // routers dead while every public UDP tracker answers), so bare magnets sit
    // at "resolving" forever. Augment HERE because every add — fresh RSS
    // candidate, ledger replay of a pre-fix stuck row, self-test — funnels
    // through this one seam; a magnet that already carries trackers passes
    // through untouched. Same cure BookTorrentMagnet::buildMagnet ships for
    // the book transport ("bare-DHT metadata is slow").
    QString magnet = magnetUri;
    if (!magnet.contains(QStringLiteral("&tr="), Qt::CaseInsensitive)
        && !magnet.contains(QStringLiteral("?tr="), Qt::CaseInsensitive)) {
        const QString hash = ComicTorrentMagnet::infoHash(magnet);
        if (!hash.isEmpty())
            magnet = BookTorrentMagnet::buildMagnet(hash);
    }
    return m_engine->addMagnet(magnet, savePath, paused);
}

void MangaTorrentEngineAdapter::setFilePriorities(const QString& infoHash, const QVector<int>& priorities)
{
    if (m_engine)
        m_engine->setFilePriorities(infoHash, priorities);
}

void MangaTorrentEngineAdapter::startTorrent(const QString& infoHash, const QString& savePath)
{
    if (m_engine)
        m_engine->startTorrent(infoHash, savePath);
}

void MangaTorrentEngineAdapter::removeTorrent(const QString& infoHash, bool deleteFiles)
{
    if (m_engine)
        m_engine->removeTorrent(infoHash, deleteFiles);
}

QJsonArray MangaTorrentEngineAdapter::torrentFiles(const QString& infoHash) const
{
    return m_engine ? m_engine->torrentFiles(infoHash) : QJsonArray{};
}
#endif // HAS_LIBTORRENT

// ── MangaTankobanService ─────────────────────────────────────────────────────

MangaTankobanService::~MangaTankobanService()
{
    // The indexer is a plain class (no QObject parent) — free it explicitly.
    // Borrowed trio members (DI ctor) and QObject-parented production
    // collaborators clean themselves up through Qt ownership.
    delete m_indexer;
}

#ifdef HAS_LIBTORRENT
MangaTankobanService::MangaTankobanService(QNetworkAccessManager* searchNam,
                                           QNetworkAccessManager* dlNam,
                                           TorrentEngine* torrentEngine,
                                           const QString& rootDir,
                                           QObject* parent)
    : QObject(parent)
{
    QString base = rootDir;
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString mv = base + QStringLiteral("/manga-volumes");
    QDir().mkpath(mv + QStringLiteral("/torrent"));
    QDir().mkpath(mv + QStringLiteral("/staging"));

    m_search = new MangaNyaaSearchAdapter(searchNam, this);

    auto* adapter = new MangaTorrentEngineAdapter(torrentEngine, this);
    m_transport = new MangaVolumeTorrentDownloader(
        adapter,
        mv + QStringLiteral("/torrent/volume-requests.json"),
        mv + QStringLiteral("/torrent"),
        this);

    m_index    = new MangaVolumeIndex(base, this);
    // Startup recovery is deliberately detached from construction. The worker
    // owns a private index instance, so the UI-owned index never crosses thread
    // affinity; the queued completion reloads its repaired ledger before the
    // readiness seam opens index-backed operations.
    m_recoveryReady = false;
    m_ingestor = new MangaVolumeArchiveIngestor(m_index, this);
    // Synopsis lookups (Apple Books / Open Library) MUST ride the IPv4-pinned searchNam,
    // not the bare dlNam — those hosts publish a dead AAAA on this ISP, so an unpinned
    // request stalls ~21s each and the cascade barely advanced (eyes-on 2026-07-15).
    m_enricher = new MangaSynopsisEnricher(searchNam, mv + QStringLiteral("/synopsis-cache.json"), this);

    auto* scraper = new WeebCentralScraper(dlNam, this);
    m_packer   = new MangaVolumePacker(scraper, dlNam, m_index, mv + QStringLiteral("/staging"), this);

    // Arc 18 M5 index-first trio: durable volume identity beside the local
    // volume index. The resolver needs libtorrent, so the trio only builds in
    // engine-enabled builds; without it the façade keeps its legacy behavior.
    // The trio lives for the process lifetime (single production service).
    m_metaResolver = new MangaTorrentMetainfoResolver();
    m_metaFetcher  = new MangaTorrentMetainfoFetcher(dlNam, this);
    m_tindex       = new MangaTorrentIndex(this);
    if (m_tindex->open(mv + QStringLiteral("/torrent-identity.db")))
        m_indexer = new MangaTorrentIndexer(m_metaResolver, m_tindex);
    else
        qWarning() << "[tankoban] torrent identity store failed to open — index-first lookup off";

    wireSignals();

    m_recoveryWatcher = new QFutureWatcher<void>(this);
    connect(m_recoveryWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        m_index->reload();
        m_recoveryReady = true;
        emit recoveryReadyChanged();

        const auto pending = std::exchange(m_pendingTransportFinishes,
                                            QVector<PendingTransportFinish>{});
        for (const PendingTransportFinish& finish : pending)
            onTransportFinished(finish.volumeId, finish.archivePath);

        const auto pendingAcquired = std::exchange(m_pendingAcquired, QVector<QString>{});
        for (const QString& volumeId : pendingAcquired)
            onAcquired(volumeId);

        const QString selfTest = std::exchange(m_pendingSelfTestSpec, QString{});
        if (!selfTest.isEmpty())
            QMetaObject::invokeMethod(this, [this, selfTest]() {
                runDownloadSelfTest(selfTest);
            }, Qt::QueuedConnection);
    });
    m_recoveryWatcher->setFuture(QtConcurrent::run([base]() {
        MangaVolumeIndex recovered(base);
        recovered.heal();
    }));
}
#endif // HAS_LIBTORRENT

MangaTankobanService::MangaTankobanService(IMangaNyaaSearch* search,
                                           MangaVolumeTorrentDownloader* transport,
                                           MangaVolumeIndex* index,
                                           MangaVolumeArchiveIngestor* ingestor,
                                           MangaSynopsisEnricher* enricher,
                                           MangaVolumePacker* packer,
                                           IMangaTorrentMetainfoResolver* metaResolver,
                                           IMangaTorrentMetainfoFetcher* metaFetcher,
                                           MangaTorrentIndex* torrentIndex,
                                           QObject* parent)
    : QObject(parent),
      m_search(search), m_transport(transport), m_index(index),
      m_ingestor(ingestor), m_enricher(enricher), m_packer(packer),
      m_metaResolver(metaResolver), m_metaFetcher(metaFetcher), m_tindex(torrentIndex)
{
    // All-or-nothing: a partial trio cannot index, so it stays fully off and
    // the façade behaves exactly as before Arc 18.
    if (m_metaResolver && m_metaFetcher && m_tindex && m_tindex->isOpen())
        m_indexer = new MangaTorrentIndexer(m_metaResolver, m_tindex);
    wireSignals();
}

void MangaTankobanService::wireSignals()
{
    connect(m_search, &IMangaNyaaSearch::searchSucceeded, this,
            [this](const QString& volId, const QList<MangaNyaaCandidate>& rows) {
                onSourcesFound(volId, rows);
            });
    connect(m_search, &IMangaNyaaSearch::searchFailed, this,
            [this](const QString& volId, const QString& reason) {
                if (indexPipelineActive()) {
                    // Arc 18 M5: a provider ERROR is recorded as an error class
                    // (never negative-cached) and must NOT delete verified
                    // identity — stale verified cards simply stay as they are.
                    m_indexRefreshPending.remove(volId);
                    m_tindex->recordSearchError(volId, QDateTime::currentMSecsSinceEpoch());
                    const QList<VolumeMapping> rows = m_tindex->mappingsForVolume(volId);
                    bool hasVerified = false;
                    for (const VolumeMapping& m : rows)
                        hasVerified = hasVerified || m.status == MappingStatus::Verified;
                    if (!hasVerified)
                        onSourcesFound(volId, {}); // nothing durable → WeebCentral card as before
                    return;
                }
                onSourcesFound(volId, {}); // Nyaa returned nothing → still a WeebCentral card
            });

    connect(m_transport, &MangaVolumeTorrentDownloader::resolving, this,
            [this](const QString& volId) {
                m_acq[volId] = QVariantMap{{QStringLiteral("state"), QStringLiteral("resolving")}};
            });
    connect(m_transport, &MangaVolumeTorrentDownloader::progress, this,
            [this](const QString& volId, double done, double total) {
                m_acq[volId] = QVariantMap{{QStringLiteral("state"), QStringLiteral("downloading")},
                                           {QStringLiteral("done"), done},
                                           {QStringLiteral("total"), total}};
                emit progress(volId, done, total);
            });
    connect(m_transport, &MangaVolumeTorrentDownloader::finished, this,
            [this](const QString& volId, const QString& archivePath) {
                onTransportFinished(volId, archivePath);
            });
    connect(m_transport, &MangaVolumeTorrentDownloader::failed, this,
            [this](const QString& volId, const QString& reason) { onFailed(volId, reason); });

    // Arc 18 M6: live metadata contradicted a persisted identity — demote the
    // mapping so the next lookup re-derives it instead of trusting a stale row.
    if (m_tindex) {
        connect(m_transport, &MangaVolumeTorrentDownloader::expectationViolated, this,
                [this](const QString& volId, const QString& hash, int fileIndex) {
                    m_tindex->updateMappingStatus(volId, hash, fileIndex,
                                                  MappingStatus::NeedsRevalidation,
                                                  QDateTime::currentMSecsSinceEpoch());
                });
    }

    connect(m_ingestor, &MangaVolumeArchiveIngestor::finished, this,
            [this](const QString& volId) { onAcquired(volId); });
    connect(m_ingestor, &MangaVolumeArchiveIngestor::failed, this,
            [this](const QString& volId, const QString& reason) { onFailed(volId, reason); });

    if (m_packer) {
        connect(m_packer, &MangaVolumePacker::progress, this,
                [this](const QString& volId, int done, int total) {
                    m_acq[volId] = QVariantMap{{QStringLiteral("state"), QStringLiteral("packing")},
                                               {QStringLiteral("done"), done},
                                               {QStringLiteral("total"), total}};
                    emit progress(volId, static_cast<double>(done), static_cast<double>(total));
                });
        connect(m_packer, &MangaVolumePacker::finished, this,
                [this](const QString& volId, const QString&) { onAcquired(volId); });
        connect(m_packer, &MangaVolumePacker::failed, this,
                [this](const QString& volId, const QString& reason) { onFailed(volId, reason); });
    }

    if (m_enricher) {
        connect(m_enricher, &MangaSynopsisEnricher::synopsisReady, this,
                [this](const QString& volId, const SynopsisRecord&) {
                    emit synopsisReady(volId);
                    if (m_volumes.contains(volId))
                        emit volumesChanged(m_volumes.value(volId).seriesId);
                });
    }

    if (m_indexer) {
        connect(m_metaFetcher, &IMangaTorrentMetainfoFetcher::fetched, this,
                [this](const QString& key, const QByteArray& bytes) {
                    onMetaFetched(key, bytes);
                });
        connect(m_metaFetcher, &IMangaTorrentMetainfoFetcher::fetchFailed, this,
                [this](const QString& key, const QString& reason) {
                    onMetaFetchFailed(key, reason);
                });
    }
}

// ── QML API ──────────────────────────────────────────────────────────────────

void MangaTankobanService::prepareSeries(QVariantMap descriptor, QVariantList volumes,
                                         QVariantList chapters)
{
    const SeriesSnapshot snap = MangaTankoban::prepareSeries(descriptor, volumes, chapters);
    m_series[snap.seriesId] = snap;
    for (const VolumeRecord& vol : snap.volumes)
        m_volumes[vol.id] = vol;

    // Canonical rendering never waits on synopsis — this is a lazy, best-effort
    // background pass that only augments (Task 3), keyed to the same volume ids.
    if (m_enricher)
        m_enricher->enrichSeries(snap, descriptor.value(QStringLiteral("synopsis")).toString());

    emit volumesChanged(snap.seriesId);
}

QVariantList MangaTankobanService::volumesForSeries(QString seriesId) const
{
    QVariantList out;
    if (!m_series.contains(seriesId))
        return out;
    const auto& vols = m_series.value(seriesId).volumes;
    out.reserve(vols.size());
    for (const VolumeRecord& vol : vols)
        out.append(volumeMap(vol));
    return out;
}

bool MangaTankobanService::modeEnabled(QString seriesId) const
{
    QSettings settings;
    return settings.value(MangaTankoban::settingsKey(seriesId), false).toBool();
}

void MangaTankobanService::setModeEnabled(QString seriesId, bool enabled)
{
    // Switching modes NEVER cancels a transfer or removes a download — it only
    // records the per-series preference.
    QSettings settings;
    settings.setValue(MangaTankoban::settingsKey(seriesId), enabled);
    settings.sync();
}

void MangaTankobanService::searchSources(QString volumeId)
{
    if (!m_volumes.contains(volumeId))
        return; // nothing to search for an unknown volume
    const VolumeRecord vol = m_volumes.value(volumeId);
    const SeriesSnapshot series = m_series.value(vol.seriesId);

    // ── Index-first (Arc 18 M5): identity answers before any network ─────────
    if (indexPipelineActive()) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<VolumeMapping> verified;
        for (const VolumeMapping& m : m_tindex->mappingsForVolume(volumeId)) {
            if (m.status == MappingStatus::Verified)
                verified.append(m);
        }
        if (!verified.isEmpty()) {
            // Instant Torrentio-style answer: STRONG cards now, zero network.
            cacheIndexedCandidates(volumeId, verified);
            QVariantList cards;
            cards.reserve(verified.size() + 1);
            for (const VolumeMapping& m : verified)
                cards.append(indexedSourceCard(m));
            cards.append(weebCardFor(volumeId));
            emit sourcesReady(volumeId, cards);
            // Stale identity fires ONE coalesced background refresh; fresh
            // identity stays fully offline. The freshness clock is when identity
            // was LAST CONFIRMED for this key: the newest mapping's verifiedAt
            // or the last successful discovery refresh, whichever is newer — a
            // sibling volume mapped by a fresh pack is fresh, even though its
            // own search key was never probed.
            const IndexedSearchState st = m_tindex->searchState(volumeId);
            qint64 identityAt = st.lastSuccessAt;
            for (const VolumeMapping& m : verified)
                identityAt = qMax<qint64>(identityAt, m.verifiedAt);
            const bool fresh = identityAt > 0 && now - identityAt < kIdentityTtlMs;
            if (!fresh && !m_indexRefreshPending.contains(volumeId)) {
                m_indexRefreshPending.insert(volumeId);
                m_search->search(series, vol.number);
            }
            return;
        }
        // No verified identity yet. A recent successful-EMPTY provider answer
        // is briefly trusted (negative TTL); a provider ERROR never is.
        if (!m_tindex->searchNegativeCached(volumeId, now)
            && !m_indexRefreshPending.contains(volumeId)) {
            m_indexRefreshPending.insert(volumeId);
            m_search->search(series, vol.number);
            return; // cards land after discovery + metainfo indexing merge
        }
        QVariantList justWeeb{weebCardFor(volumeId)};
        emit sourcesReady(volumeId, justWeeb);
        return;
    }

    m_search->search(series, vol.number);
}

void MangaTankobanService::searchSeriesSources(QString key, QString seriesTitle)
{
    if (key.isEmpty() || seriesTitle.trimmed().isEmpty())
        return;
    MangaTankoban::SeriesSnapshot snap;
    snap.seriesId = key;              // the opaque grouping key, not a real series id
    snap.title = seriesTitle;
    m_search->searchSeries(snap);
}

void MangaTankobanService::downloadNyaa(QString volumeId, QString infoHash)
{
    if (!m_recoveryReady) {
        emit failed(volumeId, QStringLiteral("Local volume index is still recovering."));
        return;
    }
    if (!m_volumes.contains(volumeId)) {
        emit failed(volumeId, QStringLiteral("Unknown volume."));
        return;
    }
    // One acquisition per volume at a time — a second concurrent request would
    // publish/finish twice for the same id.
    if (isInFlight(volumeId)) {
        emit failed(volumeId, QStringLiteral("Already acquiring this volume."));
        return;
    }
    // Recover the FULL cached candidate for this hash. An infoHash that is not
    // among the cached search candidates is rejected — the façade never accepts
    // an arbitrary magnet handed in from QML.
    const QString wanted = infoHash.toLower();
    const QList<MangaNyaaCandidate> cands = m_candidates.value(volumeId);
    for (const MangaNyaaCandidate& c : cands) {
        if (c.infoHash.toLower() == wanted) {
            m_tornDown.remove(volumeId);   // a fresh attempt clears any prior tombstone
            m_chosen[volumeId] = c;
            m_acq[volumeId] = QVariantMap{{QStringLiteral("state"), QStringLiteral("resolving")}};
            // Arc 18 M6: a verified mapping for this exact hash carries its
            // persisted file identity (fileIndex + path) into the transport so
            // live metadata is held to it before any payload starts.
            const MangaVolumeExpectation expectation =
                indexPipelineActive() ? expectationFor(volumeId, wanted)
                                      : MangaVolumeExpectation{};
            // Announce the acquisition the instant it starts (done=total=0 reads as
            // "just began, indeterminate"). Every QML surface — the sources sheet's
            // row disc, the shelf tile, the header count — keys live state off
            // `progress`, and without this tick nothing paints until the first real
            // byte: the invisible-download bug (2026-08-16).
            emit progress(volumeId, 0, 0);
            m_transport->download(m_volumes.value(volumeId), c, expectation);
            return;
        }
    }
    emit failed(volumeId,
                QStringLiteral("Unknown source — infoHash is not among the cached search candidates."));
}

void MangaTankobanService::downloadNyaaBatch(QStringList volumeIds, QString infoHash)
{
    if (volumeIds.isEmpty())
        return;
    if (!m_recoveryReady) {
        for (const QString& volumeId : volumeIds)
            emit failed(volumeId, QStringLiteral("Local volume index is still recovering."));
        return;
    }
    // The engine has NO range search — every search is per volume — so a batch
    // searched exactly one volume and the candidate cache is populated for that
    // probe volume alone. Validate the hash ONCE against it. The guard that stops
    // QML handing in an arbitrary magnet is not weakened here; it is simply not
    // asked N times for the one answer it already gave.
    const QString probeId = volumeIds.first();
    const QString wanted = infoHash.toLower();
    MangaNyaaCandidate chosen;
    bool found = false;
    for (const MangaNyaaCandidate& c : m_candidates.value(probeId)) {
        if (c.infoHash.toLower() == wanted) {
            chosen = c;
            found = true;
            break;
        }
    }
    if (!found) {
        // Refuse the WHOLE batch: an unvalidated magnet must not reach the
        // transport for any volume, and silence would look like success.
        for (const QString& volumeId : volumeIds)
            emit failed(volumeId,
                        QStringLiteral("Unknown source — infoHash is not among the cached search candidates."));
        return;
    }

    // ── Arc 18 M7: batch truth is the INDEXED FILE SET, not the release title.
    // With the pipeline live, every requested volume must hold a VERIFIED
    // mapping under this exact infoHash, each to a distinct isolable file. A
    // title that says "Vols 1-12" is discovery evidence, never batch proof —
    // this closes the first-volume-probe weakness. Partial eligibility refuses
    // the whole batch: "some of your volumes, from a pack that does not cover
    // the set you asked for" is not an honest answer to a set request.
    if (indexPipelineActive()) {
        QSet<int> usedFiles;
        bool fullyCovered = true;
        for (const QString& volumeId : volumeIds) {
            const MangaVolumeExpectation e = expectationFor(volumeId, wanted);
            if (e.fileIndex < 0 || usedFiles.contains(e.fileIndex)) {
                fullyCovered = false;
                break;
            }
            usedFiles.insert(e.fileIndex);
        }
        if (!fullyCovered) {
            for (const QString& volumeId : volumeIds)
                emit failed(volumeId, QStringLiteral(
                    "Batch refused — the indexed file set does not verify every "
                    "requested volume for this torrent."));
            return;
        }
    }

    // A BATCH IS NOT A TRANSACTION (design 2026-07-30 §3): each volume keeps its
    // own state, so one that is unknown or already acquiring reports its own
    // reason and the rest still go. Per-volume bookkeeping is byte-identical to
    // downloadNyaa's — the only thing shared is the one validated candidate.
    for (const QString& volumeId : volumeIds) {
        if (!m_volumes.contains(volumeId)) {
            emit failed(volumeId, QStringLiteral("Unknown volume."));
            continue;
        }
        if (isInFlight(volumeId)) {
            emit failed(volumeId, QStringLiteral("Already acquiring this volume."));
            continue;
        }
        m_tornDown.remove(volumeId);   // a fresh attempt clears any prior tombstone
        m_chosen[volumeId] = chosen;
        m_acq[volumeId] = QVariantMap{{QStringLiteral("state"), QStringLiteral("resolving")}};
        emit progress(volumeId, 0, 0);   // same start-tick as downloadNyaa
        // M6 expectations ride each intent of the batch exactly like the
        // single-volume path (no-op expectation when the pipeline is off).
        m_transport->download(m_volumes.value(volumeId), chosen,
                              indexPipelineActive()
                                  ? expectationFor(volumeId, wanted)
                                  : MangaVolumeExpectation{});
    }
}

void MangaTankobanService::compileWeebCentral(QString volumeId)
{
    if (!m_recoveryReady) {
        emit failed(volumeId, QStringLiteral("Local volume index is still recovering."));
        return;
    }
    if (!m_packer || !m_volumes.contains(volumeId)) {
        emit failed(volumeId, QStringLiteral("WeebCentral fallback is unavailable."));
        return;
    }
    if (isInFlight(volumeId)) {
        emit failed(volumeId, QStringLiteral("Already acquiring this volume."));
        return;
    }
    m_tornDown.remove(volumeId);   // a fresh attempt clears any prior tombstone
    m_acq[volumeId] = QVariantMap{{QStringLiteral("state"), QStringLiteral("packing")},
                                  {QStringLiteral("done"), 0},
                                  {QStringLiteral("total"), 0}};
    emit progress(volumeId, 0, 0);   // same start-tick as downloadNyaa
    const VolumeRecord vol = m_volumes.value(volumeId);
    // Pass the SERIES snapshot title (not the volume title) so the published
    // provenance records the series, matching the nyaa path's provenanceFor.
    m_packer->pack(vol, m_series.value(vol.seriesId).title);
}

void MangaTankobanService::cancel(QString volumeId)
{
    if (!m_acq.contains(volumeId))
        return; // nothing in flight → quiet no-op (never a spurious `removed`)
    // Tombstone FIRST so a stray ingest/pack that completes after teardown is
    // ignored in onAcquired and never resurrects as ready.
    const QString state = m_acq.value(volumeId).value(QStringLiteral("state")).toString();
    m_tornDown.insert(volumeId);
    if (state == QStringLiteral("packing")) {
        if (m_packer)
            m_packer->cancel(volumeId);
    } else {
        m_transport->cancel(volumeId);
    }
    m_acq.remove(volumeId);
    emit removed(volumeId);
}

QVariantMap MangaTankobanService::remove(QString volumeId)
{
    if (!m_recoveryReady)
        return DownloadFileOps::toMap({false, QStringLiteral("Local volume index is still recovering.")});
    const bool inflight = m_acq.contains(volumeId);
    const bool ready = m_index->statusOf(volumeId).value(QStringLiteral("state")).toString()
                           == QStringLiteral("ready");
    if (!inflight && !ready)
        return DownloadFileOps::toMap({true, QString()});
    // Tombstone + arrest any in-flight acquisition so a late finish can't republish.
    m_tornDown.insert(volumeId);
    if (inflight) {
        const QString state = m_acq.value(volumeId).value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("packing")) {
            if (m_packer)
                m_packer->cancel(volumeId);
        } else {
            m_transport->cancel(volumeId);
        }
    }
    m_acq.remove(volumeId);
    m_index->remove(volumeId);
    if (m_index->statusOf(volumeId).value(QStringLiteral("state")).toString()
        == QStringLiteral("ready")) {
        const DownloadFileOps::Result result{
            false, QStringLiteral("Colosseum could not delete the local volume.")};
        qWarning() << "[downloads] delete failed" << volumeId << result.message;
        return DownloadFileOps::toMap(result);
    }
    emit removed(volumeId);
    if (m_volumes.contains(volumeId))
        emit volumesChanged(m_volumes.value(volumeId).seriesId);
    return DownloadFileOps::toMap({true, QString()});
}

QVariantMap MangaTankobanService::statusOf(QString volumeId) const
{
    // The index is authoritative for a published (ready) volume.
    if (m_recoveryReady) {
        const QVariantMap idx = m_index->statusOf(volumeId);
        if (idx.value(QStringLiteral("state")).toString() == QStringLiteral("ready"))
            return idx;
    }
    // Otherwise the façade's own in-flight bookkeeping (resolving / downloading /
    // ingesting / packing / failed) is the single source of truth.
    if (m_acq.contains(volumeId))
        return m_acq.value(volumeId);
    // Finally, honour a live transport intent restored from the ledger on restart
    // (a session where prepareSeries/downloadNyaa were not re-driven). Terminal
    // transport states are deliberately ignored so remove() truly clears to none.
    const QVariantMap t = m_transport->statusOf(volumeId);
    const QString ts = t.value(QStringLiteral("state")).toString();
    if (ts == QStringLiteral("downloading") || ts == QStringLiteral("resolving"))
        return t;
    return QVariantMap{{QStringLiteral("state"), QStringLiteral("none")},
                       {QStringLiteral("done"), 0.0},
                       {QStringLiteral("total"), 0.0}};
}

QVariantList MangaTankobanService::localPages(QString volumeId) const
{
    return m_recoveryReady ? m_index->localPages(volumeId) : QVariantList{};
}

QVariantList MangaTankobanService::downloadedVolumes() const
{
    return m_recoveryReady ? m_index->downloadedVolumes() : QVariantList{};
}

QVariantList MangaTankobanService::activeVolumeJobs() const
{
    QVariantList out;
    for (auto it = m_acq.constBegin(); it != m_acq.constEnd(); ++it) {
        const QVariantMap& acq = it.value();
        const MangaTankoban::VolumeRecord vol = m_volumes.value(it.key());
        const QString label = QStringLiteral("Vol. %1").arg(vol.number);
        // The shared infoHash is the Downloads page's grouping key for a batch
        // (downloadNyaaBatch) — every volume pulled from one torrent groups into one row,
        // the same pattern Theatre seasons already use (2026-08-05 grouping design).
        // m_chosen is empty right after a restart replay (onTransportFinished's own comment:
        // "m_volumes/m_chosen are empty"), which a torrent running for hours across a restart
        // hits routinely, not rarely — fall back to the persisted ledger so grouping survives
        // a restart instead of silently reverting to flat rows for the rest of the session.
        QString hash = m_chosen.value(it.key()).infoHash;
        if (hash.isEmpty() && m_transport)
            hash = m_transport->ledgerRow(it.key()).infoHash;
        out.append(QVariantMap{
            {QStringLiteral("id"), it.key()},
            {QStringLiteral("seriesTitle"), m_series.value(vol.seriesId).title},
            {QStringLiteral("label"), label},
            {QStringLiteral("state"), acq.value(QStringLiteral("state"))},
            {QStringLiteral("done"), acq.value(QStringLiteral("done"), 0.0)},
            {QStringLiteral("total"), acq.value(QStringLiteral("total"), 0.0)},
            {QStringLiteral("groupKey"), hash},
            {QStringLiteral("groupUnit"), QStringLiteral("volumes")},
            {QStringLiteral("badge"), label}
        });
    }
    return out;
}

// ── Test-only end-to-end self-test (COLOSSEUM_TANKOBAN_DLTEST) ────────────────

void MangaTankobanService::runDownloadSelfTest(const QString& spec)
{
    if (!m_recoveryReady) {
        m_pendingSelfTestSpec = spec;
        return;
    }
    // Spec: "<magnet-or-infohash>|<seriesId>|<seriesTitle>|<volumeNumber>".
    const QStringList parts = spec.split(QLatin1Char('|'));
    if (parts.size() != 4) {
        qWarning().noquote() << "[tankoban-dltest] FAIL bad spec — expected "
                                "<magnet-or-infohash>|<seriesId>|<seriesTitle>|<volumeNumber>";
        QCoreApplication::exit(2);
        return;
    }
    const QString magnetOrHash = parts.at(0).trimmed();
    const QString seriesId     = parts.at(1).trimmed();
    const QString seriesTitle  = parts.at(2).trimmed();
    const QString volumeRaw    = parts.at(3).trimmed();

    // Normalize the source: extract the canonical 40-hex infoHash from a bare hash
    // OR a full magnet. An unparseable hash / empty required field is a bad spec.
    const QString hash = ComicTorrentMagnet::infoHash(magnetOrHash);
    if (hash.isEmpty() || seriesId.isEmpty() || volumeRaw.isEmpty()) {
        qWarning().noquote() << "[tankoban-dltest] FAIL bad spec — empty field or "
                                "unparseable magnet/infohash";
        QCoreApplication::exit(2);
        return;
    }

    // ONE canonical snapshot: a single volume for this series, keyed by the same
    // stable id everything downstream (transport, ingestor, index) agrees on.
    const QString number = MangaTankoban::normalizeVolumeNumber(volumeRaw);
    VolumeRecord vol;
    vol.id       = MangaTankoban::volumeId(seriesId, number);
    vol.seriesId = seriesId;
    vol.number   = number;
    vol.title    = seriesTitle + QStringLiteral(" Vol ") + number;

    SeriesSnapshot snap;
    snap.seriesId = seriesId;
    snap.title    = seriesTitle;
    snap.volumes.append(vol);
    m_series[seriesId] = snap;
    m_volumes[vol.id]  = vol;

    // Build the candidate from the supplied source: a bare 40-hex hash is
    // normalized into a tracker-bearing magnet (fast DHT metadata); a full magnet
    // is passed through verbatim.
    MangaNyaaCandidate candidate;
    candidate.infoHash  = hash;
    candidate.magnetUri =
        magnetOrHash.startsWith(QStringLiteral("magnet:?"), Qt::CaseInsensitive)
            ? magnetOrHash
            : BookTorrentMagnet::buildMagnet(hash);
    candidate.title = vol.title;

    const QString volId = vol.id;

    // Clear any prior ready/in-flight row for this id so the run proves a REAL
    // acquisition, never a cached one (a quiet no-op on a first run).
    remove(volId);

    // The façade owns terminal state — verdict on ITS finished/failed for volId.
    connect(this, &MangaTankobanService::finished, this, [this, volId](const QString& id) {
        if (id != volId)
            return;
        const int pages = localPages(volId).size();
        if (pages <= 0) {
            qWarning().noquote() << "[tankoban-dltest] FAIL no reader pages";
            QCoreApplication::exit(2);
            return;
        }
        qInfo().noquote() << "[tankoban-dltest] DONE pages=" << pages;
        QCoreApplication::exit(0);
    });
    connect(this, &MangaTankobanService::failed, this,
            [volId](const QString& id, const QString& reason) {
                if (id != volId)
                    return;
                qWarning().noquote() << "[tankoban-dltest] FAIL" << reason;
                QCoreApplication::exit(2);
            });

    // Hard backstop: never hang — 240 s then an honest failing verdict.
    QTimer::singleShot(240000, this, []() {
        qWarning().noquote() << "[tankoban-dltest] FAIL timeout";
        QCoreApplication::exit(2);
    });

    // Drive the transport exactly as downloadNyaa does after its cache lookup,
    // but with a directly-constructed candidate (no search-candidate cache).
    m_tornDown.remove(volId);
    m_chosen[volId] = candidate;
    m_acq[volId] = QVariantMap{{QStringLiteral("state"), QStringLiteral("resolving")}};
    m_transport->download(vol, candidate);
}

// ── Terminal-state ownership + model assembly ────────────────────────────────

void MangaTankobanService::onSourcesFound(const QString& volumeId,
                                          const QList<MangaNyaaCandidate>& rows)
{
    // ── Index refresh diversion (Arc 18 M5): a discovery that runs while an
    // index refresh is pending feeds the INDEXER, not QML. Cards land only after
    // every metainfo fetch settles (emitMergedSources merges indexed + discovery).
    if (indexPipelineActive() && m_indexRefreshPending.contains(volumeId)) {
        m_indexRefreshPending.remove(volumeId);
        MetaFetch mf;
        if (m_volumes.contains(volumeId)) {
            const VolumeRecord vol = m_volumes.value(volumeId);
            mf.series = m_series.value(vol.seriesId);
        }
        mf.rows = rows;
        // Candidates worth indexing: only ones whose .torrent we can actually
        // fetch, capped so one lookup can never fan into an unbounded fetch
        // storm. Cap-first keeps the pending count stable under synchronous
        // fetcher emission.
        QList<MangaNyaaCandidate> fetchable;
        for (const MangaNyaaCandidate& c : rows) {
            if (c.torrentUrl.isEmpty())
                continue;
            fetchable.append(c);
            if (fetchable.size() >= 4)
                break;
        }
        mf.pending = fetchable.size();
        for (int i = 0; i < fetchable.size(); ++i)
            mf.urlByKey.insert(QStringLiteral("%1#%2").arg(volumeId).arg(i),
                               fetchable.at(i).torrentUrl);
        m_metaFetches.insert(volumeId, mf);
        if (mf.pending == 0) {
            // Nothing fetchable: this WAS the provider's answer — record it
            // (0 rows → negative-TTL'd empty) and show what we already have.
            m_metaFetches.remove(volumeId);
            m_tindex->recordSearchSuccess(volumeId, rows.size(),
                                          QDateTime::currentMSecsSinceEpoch());
            emitMergedSources(volumeId, rows);
            return;
        }
        for (int i = 0; i < fetchable.size(); ++i)
            m_metaFetcher->fetch(fetchable.at(i).torrentUrl,
                                 QStringLiteral("%1#%2").arg(volumeId).arg(i));
        return;
    }

    m_candidates[volumeId] = rows;
    QVariantList maps;
    maps.reserve(rows.size() + 1);
    for (const MangaNyaaCandidate& c : rows)
        maps.append(sourceCard(c));
    // The WeebCentral fallback card is ALWAYS present and ALWAYS last, even when
    // Nyaa returned nothing.
    maps.append(weebCardFor(volumeId));
    emit sourcesReady(volumeId, maps);
}

void MangaTankobanService::onTransportFinished(const QString& volumeId, const QString& archivePath)
{
    if (!m_recoveryReady) {
        m_pendingTransportFinishes.append({volumeId, archivePath});
        return;
    }
    if (m_tornDown.contains(volumeId))
        return; // cancelled/removed before the transfer finished — never ingest

    VolumeRecord vol;
    MangaNyaaCandidate cand;
    if (m_volumes.contains(volumeId)) {
        vol  = m_volumes.value(volumeId);
        cand = m_chosen.value(volumeId);
    } else {
        // Restart replay: the transport re-added the torrent from its journal and
        // it finished before QML re-prepared the series, so m_volumes/m_chosen are
        // empty. Recover a MINIMAL canonical record from the PERSISTED ledger row so
        // a resumed torrent that finishes still ingests exactly ONE canonical ready
        // record — never an orphaned archive. releaseTitle/uploader stay blank (the
        // ledger does not persist them); the canonical id + pages are what matter.
        const VolumeRequestRow row = m_transport->ledgerRow(volumeId);
        if (row.volumeId.isEmpty()) {
            emit failed(volumeId, QStringLiteral("Unknown volume for a finished transfer."));
            return;
        }
        vol.id        = volumeId;
        vol.seriesId  = row.seriesId;
        vol.number    = MangaTankoban::normalizeVolumeNumber(row.volumeNumber);
        cand.infoHash = row.infoHash;
    }
    // ONE façade owns the terminal step: the downloaded archive is ingested
    // through the REAL ingestor→index path (Task 5). onAcquired fires when the
    // index holds the ready volume.
    m_acq[volumeId] = QVariantMap{{QStringLiteral("state"), QStringLiteral("ingesting")}};
    m_ingestor->ingestArchive(provenanceFor(vol, cand), archivePath);
}

void MangaTankobanService::onAcquired(const QString& volumeId)
{
    if (!m_recoveryReady) {
        m_pendingAcquired.append(volumeId);
        return;
    }
    m_acq.remove(volumeId); // the index now reports this volume ready
    if (m_tornDown.contains(volumeId)) {
        // The user cancelled/removed while acquisition was in flight and a stray
        // ingest/pack finished anyway — undo the publish so it never resurrects.
        m_index->remove(volumeId);
        return;
    }
    emit finished(volumeId);
    if (m_volumes.contains(volumeId))
        emit volumesChanged(m_volumes.value(volumeId).seriesId);
}

void MangaTankobanService::onFailed(const QString& volumeId, const QString& reason)
{
    if (m_tornDown.contains(volumeId)) {
        // A torn-down volume never reports a late failure — teardown already told
        // the UI it was removed.
        m_acq.remove(volumeId);
        return;
    }
    m_acq[volumeId] = QVariantMap{{QStringLiteral("state"), QStringLiteral("failed")},
                                  {QStringLiteral("reason"), reason}};
    emit failed(volumeId, reason);
}

bool MangaTankobanService::isInFlight(const QString& volumeId) const
{
    const QString s = m_acq.value(volumeId).value(QStringLiteral("state")).toString();
    return s == QStringLiteral("resolving") || s == QStringLiteral("downloading")
        || s == QStringLiteral("ingesting") || s == QStringLiteral("packing");
}

QVariantMap MangaTankobanService::sourceCard(const MangaNyaaCandidate& candidate) const
{
    QString coverage;
    if (!candidate.coverageLo.isEmpty()) {
        coverage = (candidate.coverageLo == candidate.coverageHi)
            ? QStringLiteral("Vol %1").arg(candidate.coverageLo)
            : QStringLiteral("Vol %1–%2").arg(candidate.coverageLo, candidate.coverageHi);
    }
    return QVariantMap{
        {QStringLiteral("kind"), QStringLiteral("nyaa")},
        {QStringLiteral("infoHash"), candidate.infoHash},
        {QStringLiteral("releaseTitle"), candidate.title},
        {QStringLiteral("uploader"), candidate.uploader},
        {QStringLiteral("tier"), candidate.tier},
        {QStringLiteral("sizeBytes"), QVariant::fromValue<qlonglong>(candidate.sizeBytes)},
        {QStringLiteral("seeders"), candidate.seeders},
        {QStringLiteral("leechers"), candidate.leechers},
        {QStringLiteral("coverage"), coverage},
        {QStringLiteral("coverageLo"), candidate.coverageLo},
        {QStringLiteral("coverageHi"), candidate.coverageHi},
        {QStringLiteral("standalone"), candidate.standalone},
        {QStringLiteral("digital"), candidate.digitalHint},
        {QStringLiteral("enabled"), !candidate.infoHash.isEmpty()},
    };
}

QVariantMap MangaTankobanService::weebCardFor(const QString& volumeId) const
{
    // Enabled ONLY when this volume's chapter map is COMPLETE — every in-range
    // chapter that exists is mapped, so the fallback can build the whole volume. A
    // partial map (some-but-not-all chapters) is offered but disabled, with a
    // concrete reason; the volume + its Nyaa path are unaffected.
    const bool known = m_volumes.contains(volumeId);
    const VolumeRecord vol = known ? m_volumes.value(volumeId) : VolumeRecord{};
    const int chapterCount = known ? vol.chapterIds.size() : 0;
    const bool enabled = known && vol.chapterMapComplete;
    QVariantMap card{
        {QStringLiteral("kind"), QStringLiteral("weebcentral")},
        {QStringLiteral("label"), QStringLiteral("Build from chapters")},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("chapterCount"), chapterCount},
    };
    if (!enabled) {
        if (!known)
            card[QStringLiteral("reason")] = QStringLiteral("This volume has not been prepared.");
        else if (chapterCount == 0)
            card[QStringLiteral("reason")] =
                QStringLiteral("No WeebCentral chapters map to this volume yet.");
        else
            card[QStringLiteral("reason")] =
                QStringLiteral("Only part of this volume's chapters are available yet.");
    } else {
        card[QStringLiteral("reason")] = QString();
    }
    return card;
}

QVariantMap MangaTankobanService::volumeMap(const VolumeRecord& volume) const
{
    QVariantMap m{
        {QStringLiteral("id"), volume.id},
        {QStringLiteral("seriesId"), volume.seriesId},
        {QStringLiteral("number"), volume.number},
        {QStringLiteral("title"), volume.title},
        {QStringLiteral("cover"), volume.cover},
        {QStringLiteral("chapterStart"), volume.chapterStart},
        {QStringLiteral("chapterEnd"), volume.chapterEnd},
        {QStringLiteral("chapterCount"), volume.chapterIds.size()},
    };
    // Acquisition/index state (Task 5) — via the composed statusOf.
    m[QStringLiteral("state")] = statusOf(volume.id).value(QStringLiteral("state"));

    // Cached synopsis (Task 3), merged only when genuinely accepted.
    if (m_enricher) {
        const SynopsisRecord syn = m_enricher->cached(volume.id);
        if (syn.accepted) {
            m[QStringLiteral("synopsis")] = syn.text;
            m[QStringLiteral("synopsisSource")] = syn.source;
            m[QStringLiteral("synopsisSourceUrl")] = syn.sourceUrl;
            m[QStringLiteral("synopsisConfidence")] = syn.confidence;
        }
    }
    return m;
}

VolumeProvenance MangaTankobanService::provenanceFor(const VolumeRecord& volume,
                                                     const MangaNyaaCandidate& candidate) const
{
    VolumeProvenance p;
    p.id           = volume.id;
    p.seriesId     = volume.seriesId;
    p.seriesTitle  = m_series.value(volume.seriesId).title;
    p.volumeNumber = volume.number;
    p.sourceKind   = QStringLiteral("nyaa");
    p.releaseTitle = candidate.title;
    p.uploader     = candidate.uploader;
    p.infoHash     = candidate.infoHash;
    return p;
}

// ── Arc 18 M5 index-first helpers ─────────────────────────────────────────────

bool MangaTankobanService::indexPipelineActive() const
{
    return m_indexer && m_tindex && m_tindex->isOpen() && m_metaFetcher && m_metaResolver;
}

MangaVolumeExpectation MangaTankobanService::expectationFor(const QString& volumeId,
                                                            const QString& normalizedHash) const
{
    MangaVolumeExpectation e;
    if (!m_tindex || !m_tindex->isOpen())
        return e;
    for (const VolumeMapping& m : m_tindex->mappingsForVolume(volumeId)) {
        if (m.status != MappingStatus::Verified || m.infoHash.toLower() != normalizedHash)
            continue;
        e.fileIndex = m.fileIndex;
        for (const IndexedFile& f : m_tindex->filesForTorrent(m.infoHash)) {
            if (f.fileIndex == m.fileIndex) {
                e.filePath = f.path;
                break;
            }
        }
        break;
    }
    return e;
}

QVariantMap MangaTankobanService::indexedSourceCard(const VolumeMapping& mapping) const
{
    // A verified mapping rendered as a card. STRONG means FILE-LEVEL verified
    // identity (the indexer proved this exact fileIndex is this exact volume) —
    // never a trusted-uploader or release-title claim, which stay ordinary
    // discovery cards.
    const IndexedTorrent t = m_tindex->torrentRow(mapping.infoHash);
    QString filePath;
    for (const IndexedFile& f : m_tindex->filesForTorrent(mapping.infoHash)) {
        if (f.fileIndex == mapping.fileIndex) {
            filePath = f.path;
            break;
        }
    }
    QString evidence;
    switch (mapping.evidence) {
    case MappingEvidence::MetainfoExactFilename:
        evidence = QStringLiteral("metainfo_exact_filename"); break;
    case MappingEvidence::MetainfoExactDirectory:
        evidence = QStringLiteral("metainfo_exact_directory"); break;
    case MappingEvidence::RuntimeRevalidated:
        evidence = QStringLiteral("runtime_revalidated"); break;
    case MappingEvidence::ReleaseOnly:
        break; // a verified row never carries release_only; omit the field
    }
    QVariantMap card{
        {QStringLiteral("kind"), QStringLiteral("nyaa")},
        {QStringLiteral("infoHash"), mapping.infoHash},
        {QStringLiteral("releaseTitle"), t.releaseTitle},
        {QStringLiteral("uploader"), t.uploader},
        {QStringLiteral("sizeBytes"), QVariant::fromValue<qlonglong>(t.totalSize)},
        {QStringLiteral("seeders"), t.seeders},
        {QStringLiteral("leechers"), t.leechers},
        {QStringLiteral("enabled"), true},
        // ── index card extras (QML renders these as the verified row) ──
        {QStringLiteral("indexed"), true},
        {QStringLiteral("confidence"), QStringLiteral("STRONG")},
        {QStringLiteral("fileIndex"), mapping.fileIndex},
        {QStringLiteral("filePath"), filePath},
        {QStringLiteral("verifiedAt"), QVariant::fromValue<qlonglong>(mapping.verifiedAt)},
        {QStringLiteral("parserVersion"), mapping.parserVersion},
    };
    if (!evidence.isEmpty())
        card[QStringLiteral("evidence")] = evidence;
    return card;
}

void MangaTankobanService::cacheIndexedCandidates(
    const QString& volumeId, const QList<VolumeMapping>& verified)
{
    // Merge one synthesized candidate per verified mapping into the volume's
    // candidate cache so downloadNyaa's arbitrary-infoHash guard VALIDATES an
    // indexed hash without a live Nyaa search. The magnet is rebuilt from the
    // hash alone (trackers live in the engine config, not the index).
    QList<MangaNyaaCandidate>& cache = m_candidates[volumeId];
    QSet<QString> known;
    for (const MangaNyaaCandidate& c : cache)
        known.insert(c.infoHash.toLower());
    for (const VolumeMapping& m : verified) {
        if (known.contains(m.infoHash.toLower()))
            continue;
        const IndexedTorrent t = m_tindex->torrentRow(m.infoHash);
        MangaNyaaCandidate c;
        c.title      = t.releaseTitle;
        c.uploader   = t.uploader;
        c.magnetUri  = BookTorrentMagnet::buildMagnet(m.infoHash);
        c.infoHash   = m.infoHash;
        c.sizeBytes  = t.totalSize;
        c.seeders    = t.seeders;
        c.leechers   = t.leechers;
        c.discoveredAt = t.discoveredAt;
        cache.append(c);
        known.insert(m.infoHash.toLower());
    }
}

void MangaTankobanService::emitMergedSources(const QString& volumeId,
                                             const QList<MangaNyaaCandidate>& rows)
{
    // Post-refresh card set: indexed STRONG cards first (file-verified truth),
    // then any freshly discovered candidates the index could NOT verify (they
    // keep their ordinary tier labels — never STRONG), then the WeebCentral
    // fallback, always last.
    QVariantList cards;
    QList<VolumeMapping> verified;
    for (const VolumeMapping& m : m_tindex->mappingsForVolume(volumeId)) {
        if (m.status == MappingStatus::Verified)
            verified.append(m);
    }
    QSet<QString> indexedHashes;
    for (const VolumeMapping& m : verified) {
        cards.append(indexedSourceCard(m));
        indexedHashes.insert(m.infoHash.toLower());
    }
    // Discovery rows replace the cache, THEN verified mappings' synthesized
    // candidates merge back in (append-only, hash-deduped) so downloadNyaa's
    // arbitrary-infoHash guard validates an indexed hash post-refresh too.
    m_candidates[volumeId] = rows;
    cacheIndexedCandidates(volumeId, verified);
    for (const MangaNyaaCandidate& c : rows) {
        if (indexedHashes.contains(c.infoHash.toLower()))
            continue;
        QVariantMap card = sourceCard(c);
        card[QStringLiteral("indexed")] = false;
        cards.append(card);
    }
    cards.append(weebCardFor(volumeId));
    emit sourcesReady(volumeId, cards);
}

void MangaTankobanService::settleMetaFetch(QHash<QString, MetaFetch>::iterator it)
{
    // The discovery provider DID answer this key (individual .torrent fetches
    // failing never demotes that), so the refresh records a success — result
    // count 0 among fetchable-less rows earns the negative TTL; anything else
    // leaves the key retryable — then emits whatever truth now exists.
    const QString volumeId = it.key();
    const QList<MangaNyaaCandidate> rows = it->rows;
    m_metaFetches.erase(it);
    m_tindex->recordSearchSuccess(volumeId, rows.size(),
                                  QDateTime::currentMSecsSinceEpoch());
    emitMergedSources(volumeId, rows);
}

void MangaTankobanService::onMetaFetched(const QString& requestKey, const QByteArray& torrentBytes)
{
    // One .torrent landed. Route it to its discovery row via urlByKey, index
    // it, and settle the refresh when the last fetch resolves.
    auto it = m_metaFetches.begin();
    while (it != m_metaFetches.end() && !it->urlByKey.contains(requestKey))
        ++it;
    if (it == m_metaFetches.end())
        return;
    const QString url = it->urlByKey.take(requestKey);
    const MangaNyaaCandidate* row = nullptr;
    for (const MangaNyaaCandidate& c : it->rows) {
        if (c.torrentUrl == url) {
            row = &c;
            break;
        }
    }
    if (row && !torrentBytes.isEmpty()) {
        // Verdicts land in the store; a refusal (combined/ambiguous/lying
        // infoHash) is an audit diagnostic, never a mapping row.
        m_indexer->indexCandidate(it->series, *row, torrentBytes,
                                  QDateTime::currentMSecsSinceEpoch());
    }
    if (--it->pending <= 0)
        settleMetaFetch(it);
}

void MangaTankobanService::onMetaFetchFailed(const QString& requestKey, const QString& reason)
{
    // A failed metainfo fetch never fails the LOOKUP: verified cards stay
    // visible, discovery cards still render, and no failed() signal fires.
    // The discovery itself succeeded, so the refresh settles normally.
    Q_UNUSED(reason)
    auto it = m_metaFetches.begin();
    while (it != m_metaFetches.end() && !it->urlByKey.contains(requestKey))
        ++it;
    if (it == m_metaFetches.end())
        return;
    it->urlByKey.take(requestKey);
    if (--it->pending <= 0)
        settleMetaFetch(it);
}
