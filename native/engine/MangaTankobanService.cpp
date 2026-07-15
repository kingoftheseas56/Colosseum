// native/engine/MangaTankobanService.cpp
#include "engine/MangaTankobanService.h"

#include "engine/MangaTankobanLogic.h"          // MangaTankoban::prepareSeries / volumeId / settingsKey
#include "engine/MangaSynopsisEnricher.h"
#include "engine/MangaVolumeArchiveIngestor.h"
#include "engine/MangaVolumeIndex.h"
#include "engine/MangaVolumePacker.h"
#include "torrent/ComicTorrentMagnet.h"         // infoHash() — extract a canonical hash from a magnet-or-hash
#include "torrent/BookTorrentMagnet.h"          // buildMagnet() — tracker-bearing magnet for a bare infohash

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#ifdef HAS_LIBTORRENT
#include "engine/WeebCentralScraper.h"
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

// ── MangaTorrentEngineAdapter (real engine, HAS_LIBTORRENT only) ──────────────

#ifdef HAS_LIBTORRENT
MangaTorrentEngineAdapter::MangaTorrentEngineAdapter(TorrentEngine* engine, QObject* parent)
    : IMangaTorrentEngine(parent), m_engine(engine)
{
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
    return m_engine->addMagnet(magnetUri, savePath, paused);
}

void MangaTorrentEngineAdapter::setFilePriorities(const QString& infoHash, const QVector<int>& priorities)
{
    m_engine->setFilePriorities(infoHash, priorities);
}

void MangaTorrentEngineAdapter::startTorrent(const QString& infoHash, const QString& savePath)
{
    m_engine->startTorrent(infoHash, savePath);
}

void MangaTorrentEngineAdapter::removeTorrent(const QString& infoHash, bool deleteFiles)
{
    m_engine->removeTorrent(infoHash, deleteFiles);
}

QJsonArray MangaTorrentEngineAdapter::torrentFiles(const QString& infoHash) const
{
    return m_engine->torrentFiles(infoHash);
}
#endif // HAS_LIBTORRENT

// ── MangaTankobanService ─────────────────────────────────────────────────────

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
    m_ingestor = new MangaVolumeArchiveIngestor(m_index, this);
    // Synopsis lookups (Apple Books / Open Library) MUST ride the IPv4-pinned searchNam,
    // not the bare dlNam — those hosts publish a dead AAAA on this ISP, so an unpinned
    // request stalls ~21s each and the cascade barely advanced (eyes-on 2026-07-15).
    m_enricher = new MangaSynopsisEnricher(searchNam, mv + QStringLiteral("/synopsis-cache.json"), this);

    auto* scraper = new WeebCentralScraper(dlNam, this);
    m_packer   = new MangaVolumePacker(scraper, dlNam, m_index, mv + QStringLiteral("/staging"), this);

    wireSignals();
}
#endif // HAS_LIBTORRENT

MangaTankobanService::MangaTankobanService(IMangaNyaaSearch* search,
                                           MangaVolumeTorrentDownloader* transport,
                                           MangaVolumeIndex* index,
                                           MangaVolumeArchiveIngestor* ingestor,
                                           MangaSynopsisEnricher* enricher,
                                           MangaVolumePacker* packer,
                                           QObject* parent)
    : QObject(parent),
      m_search(search), m_transport(transport), m_index(index),
      m_ingestor(ingestor), m_enricher(enricher), m_packer(packer)
{
    wireSignals();
}

void MangaTankobanService::wireSignals()
{
    connect(m_search, &IMangaNyaaSearch::searchSucceeded, this,
            [this](const QString& volId, const QList<MangaNyaaCandidate>& rows) {
                onSourcesFound(volId, rows);
            });
    connect(m_search, &IMangaNyaaSearch::searchFailed, this,
            [this](const QString& volId, const QString&) {
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
    m_search->search(series, vol.number);
}

void MangaTankobanService::downloadNyaa(QString volumeId, QString infoHash)
{
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
            m_transport->download(m_volumes.value(volumeId), c);
            return;
        }
    }
    emit failed(volumeId,
                QStringLiteral("Unknown source — infoHash is not among the cached search candidates."));
}

void MangaTankobanService::compileWeebCentral(QString volumeId)
{
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

void MangaTankobanService::remove(QString volumeId)
{
    const bool inflight = m_acq.contains(volumeId);
    const bool ready = m_index->statusOf(volumeId).value(QStringLiteral("state")).toString()
                           == QStringLiteral("ready");
    if (!inflight && !ready)
        return; // nothing to remove → quiet no-op (never a spurious `removed`)
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
    emit removed(volumeId);
    if (m_volumes.contains(volumeId))
        emit volumesChanged(m_volumes.value(volumeId).seriesId);
}

QVariantMap MangaTankobanService::statusOf(QString volumeId) const
{
    // The index is authoritative for a published (ready) volume.
    const QVariantMap idx = m_index->statusOf(volumeId);
    if (idx.value(QStringLiteral("state")).toString() == QStringLiteral("ready"))
        return idx;
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
    return m_index->localPages(volumeId);
}

// ── Test-only end-to-end self-test (COLOSSEUM_TANKOBAN_DLTEST) ────────────────

void MangaTankobanService::runDownloadSelfTest(const QString& spec)
{
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
