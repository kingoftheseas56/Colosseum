#include "MangaVolumeTorrentDownloader.h"

#include "MangaVolumeFilePicker.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>

using MangaTankoban::MangaNyaaCandidate;
using MangaTankoban::VolumeRecord;
using MangaTankoban::VolumeRequestRow;

namespace {
constexpr int kProgressThrottleMs = 500;
} // namespace

MangaVolumeTorrentDownloader::MangaVolumeTorrentDownloader(IMangaTorrentEngine* engine,
                                                           const QString& ledgerPath,
                                                           const QString& saveRoot,
                                                           QObject* parent)
    : QObject(parent), m_engine(engine), m_ledger(ledgerPath), m_saveRoot(saveRoot)
{
    if (m_engine) {
        connect(m_engine, &IMangaTorrentEngine::metadataReady,
                this, &MangaVolumeTorrentDownloader::onMetadataReady);
        connect(m_engine, &IMangaTorrentEngine::torrentProgress,
                this, &MangaVolumeTorrentDownloader::onProgress);
        connect(m_engine, &IMangaTorrentEngine::torrentFinished,
                this, &MangaVolumeTorrentDownloader::onFinished);
        connect(m_engine, &IMangaTorrentEngine::torrentError,
                this, &MangaVolumeTorrentDownloader::onError);
    }
    replayActive();
}

MangaVolumeTorrentDownloader::~MangaVolumeTorrentDownloader()
{
    qDeleteAll(m_jobs);
}

QString MangaVolumeTorrentDownloader::saveDirFor(const QString& infoHash) const
{
    return m_saveRoot + QLatin1Char('/') + infoHash.toLower();
}

QString MangaVolumeTorrentDownloader::reasonFor(int pickFailure)
{
    using MangaVolumeFilePicker::PickFailure;
    switch (static_cast<PickFailure>(pickFailure)) {
    case PickFailure::NoArchive:      return QStringLiteral("torrent has no comic archive");
    case PickFailure::TargetMissing:  return QStringLiteral("requested volume is not in this torrent");
    case PickFailure::Ambiguous:      return QStringLiteral("two archives match this volume equally");
    case PickFailure::CombinedArchive:return QStringLiteral("only a combined multi-volume archive covers this volume");
    case PickFailure::None:           break;
    }
    return QStringLiteral("could not isolate this volume");
}

// ── Replay after restart ────────────────────────────────────────────────────
// Reconstruct every still-in-flight intent from the journal and re-add each
// torrent PAUSED so metadata is re-inspected before any payload resumes. The
// prior pick is forgotten deliberately: libtorrent re-emits metadata on re-add
// and the file is re-resolved from that, keeping the restart path honest.
void MangaVolumeTorrentDownloader::replayActive()
{
    const QList<VolumeRequestRow> rows = m_ledger.active();
    for (const VolumeRequestRow& row : rows) {
        Job* job = m_jobs.value(row.infoHash);
        if (!job) {
            job = new Job;
            job->infoHash  = row.infoHash;
            job->magnetUri = row.magnetUri;
            job->saveDir   = row.savePath.isEmpty() ? saveDirFor(row.infoHash) : row.savePath;
            m_jobs.insert(row.infoHash, job);
        }
        Intent intent;
        intent.volumeId     = row.volumeId;
        intent.volumeNumber = row.volumeNumber;
        intent.seriesId     = row.seriesId;
        job->intents.append(intent);
        m_hashByVolume.insert(row.volumeId, row.infoHash);
        m_ledger.setState(row.volumeId, QStringLiteral("awaiting_metadata"));
    }
    for (Job* job : m_jobs) {
        QDir().mkpath(job->saveDir);
        if (m_engine)
            m_engine->addMagnet(job->magnetUri, job->saveDir, /*paused=*/true);
    }
}

void MangaVolumeTorrentDownloader::writeLedgerRow(Job* job, const VolumeRecord& volume,
                                                  const QString& state)
{
    VolumeRequestRow row;
    row.volumeId        = volume.id;
    row.infoHash        = job->infoHash;
    row.magnetUri       = job->magnetUri;
    row.seriesId        = volume.seriesId;
    row.volumeNumber    = volume.number;
    row.savePath        = job->saveDir;
    row.pickedFileIndex = -1;
    row.state           = state;
    m_ledger.upsert(row);
}

void MangaVolumeTorrentDownloader::addIntent(Job* job, const VolumeRecord& volume,
                                             const QString& state)
{
    Intent intent;
    intent.volumeId     = volume.id;
    intent.volumeNumber = volume.number;
    intent.seriesId     = volume.seriesId;
    job->intents.append(intent);
    m_hashByVolume.insert(volume.id, job->infoHash);
    writeLedgerRow(job, volume, state);
}

MangaVolumeTorrentDownloader::Intent*
MangaVolumeTorrentDownloader::intentFor(Job* job, const QString& volumeId) const
{
    for (Intent& intent : job->intents)
        if (intent.volumeId == volumeId)
            return &intent;
    return nullptr;
}

void MangaVolumeTorrentDownloader::download(const VolumeRecord& volume,
                                            const MangaNyaaCandidate& candidate)
{
    const QString volumeId = volume.id;
    const QString hash = candidate.infoHash.trimmed().toLower();
    if (volumeId.isEmpty() || hash.size() != 40) {
        emit failed(volumeId, QStringLiteral("bad volume id / infoHash"));
        return;
    }
    if (m_hashByVolume.contains(volumeId))
        return;  // this exact volume is already requested
    if (!m_engine) {
        emit failed(volumeId, QStringLiteral("torrent engine unavailable"));
        return;
    }

    Job* job = m_jobs.value(hash);
    if (!job) {
        // Brand-new torrent: add PAUSED, journal the intent, and wait for metadata
        // before touching the payload. Nyaa magnets are v1 BTIH, so the engine's
        // signals key on the same infoHash we passed (a later task adds hybrid
        // v1/v2 rebind if a source ever needs it).
        job = new Job;
        job->infoHash  = hash;
        job->magnetUri = candidate.magnetUri;
        job->saveDir   = saveDirFor(hash);
        m_jobs.insert(hash, job);

        QDir().mkpath(job->saveDir);
        m_engine->addMagnet(candidate.magnetUri, job->saveDir, /*paused=*/true);
        addIntent(job, volume, QStringLiteral("awaiting_metadata"));
        emit resolving(volumeId);

        // Mirror ComicTorrent: if the engine already holds metadata (e.g. a
        // re-add of a live torrent) resolve immediately instead of waiting.
        const QJsonArray existing = m_engine->torrentFiles(hash);
        if (!existing.isEmpty()) {
            job->metadataKnown = true;
            job->files = existing;
            resolveJob(job);
        }
        return;
    }

    // Existing torrent: join it, grow the union. Never re-add the magnet.
    // A prior request for this volume may have gone terminal (finished / failed /
    // cancelled removes it from m_hashByVolume, so the dedup guard above lets it
    // through). REVIVE that same intent in place rather than appending a duplicate
    // or silently rewinding an unrelated terminal ledger row.
    if (Intent* existing = intentFor(job, volumeId)) {
        if (!existing->terminal)
            return;  // already live on this job — nothing to do
        existing->terminal         = false;
        existing->pickedIndex      = -1;
        existing->pickedName.clear();
        existing->fileSize         = 0;
        existing->received         = 0;
        existing->lastProgressEmit = 0;
        m_hashByVolume.insert(volumeId, job->infoHash);
        writeLedgerRow(job, volume, QStringLiteral("awaiting_metadata")); // deliberate revive
        emit resolving(volumeId);
        if (job->metadataKnown)
            resolveJob(job);
        return;
    }

    addIntent(job, volume, QStringLiteral("awaiting_metadata"));
    emit resolving(volumeId);
    if (job->metadataKnown)
        resolveJob(job);
}

// ── Resolve every live intent against known metadata, then start the payload ──
void MangaVolumeTorrentDownloader::resolveJob(Job* job)
{
    QVector<int> picked;
    for (Intent& intent : job->intents) {
        if (intent.terminal)
            continue;
        if (intent.pickedIndex >= 0) {   // already resolved on a prior pass
            picked.append(intent.pickedIndex);
            continue;
        }
        const MangaVolumeFilePicker::MangaVolumePick p =
            MangaVolumeFilePicker::pick(intent.volumeNumber, job->files);
        if (p.index < 0 || p.failure != MangaVolumeFilePicker::PickFailure::None) {
            failIntent(intent, reasonFor(static_cast<int>(p.failure)));
            continue;
        }
        intent.pickedIndex = p.index;
        intent.pickedName  = p.path;
        intent.pickedName.replace(QLatin1Char('\\'), QLatin1Char('/'));
        intent.fileSize    = p.size;
        picked.append(p.index);
        m_ledger.markDownloading(intent.volumeId, p.index);
    }

    if (!hasLiveIntent(job)) {
        // Every requested volume failed to resolve — nothing worth keeping.
        tearDown(job, /*deleteFiles=*/true);
        return;
    }

    const QVector<int> priorities =
        MangaVolumeFilePicker::unionPriorities(picked, job->files.size());
    m_engine->setFilePriorities(job->infoHash, priorities);
    if (!job->started) {
        m_engine->startTorrent(job->infoHash, job->saveDir);
        job->started = true;
    }
}

void MangaVolumeTorrentDownloader::onMetadataReady(const QString& infoHash, const QString&,
                                                   qint64, const QJsonArray& files)
{
    Job* job = m_jobs.value(infoHash.toLower());
    if (!job)
        return;
    job->metadataKnown = true;
    job->files = files;
    resolveJob(job);
}

void MangaVolumeTorrentDownloader::onProgress(const QString& infoHash, float fraction,
                                              int, int, int, int)
{
    Job* job = m_jobs.value(infoHash.toLower());
    if (!job)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (Intent& intent : job->intents) {
        if (intent.terminal || intent.pickedIndex < 0 || intent.fileSize <= 0)
            continue;
        intent.received = static_cast<qint64>(fraction * static_cast<float>(intent.fileSize));
        if (intent.lastProgressEmit == 0 || now - intent.lastProgressEmit >= kProgressThrottleMs) {
            intent.lastProgressEmit = now;
            emit progress(intent.volumeId, static_cast<double>(intent.received),
                          static_cast<double>(intent.fileSize));
        }
    }
}

void MangaVolumeTorrentDownloader::onFinished(const QString& infoHash)
{
    Job* job = m_jobs.value(infoHash.toLower());
    if (!job)
        return;

    // Emit finished per requested volume INDEPENDENTLY so each can be ingested
    // on its own; verify the resolved archive exists before claiming success.
    bool anySuccess = false;
    for (Intent& intent : job->intents) {
        if (intent.terminal || intent.pickedIndex < 0)
            continue;
        m_ledger.setState(intent.volumeId, QStringLiteral("validating"));
        const QString path = job->saveDir + QLatin1Char('/') + intent.pickedName;
        if (!QFileInfo::exists(path)) {
            failIntent(intent, QStringLiteral("finished but archive is missing: %1")
                                   .arg(intent.pickedName));
            continue;
        }
        intent.terminal = true;
        m_ledger.setState(intent.volumeId, QStringLiteral("completed"));
        m_hashByVolume.remove(intent.volumeId);
        anySuccess = true;
        emit finished(intent.volumeId, path);
    }

    if (!hasLiveIntent(job)) {
        // Keep the files when at least one volume landed; delete a wholly-missing
        // payload (mirrors ComicTorrentDownloader::finalizeJob).
        tearDown(job, /*deleteFiles=*/!anySuccess);
    }
}

void MangaVolumeTorrentDownloader::onError(const QString& infoHash, const QString& message)
{
    Job* job = m_jobs.value(infoHash.toLower());
    if (!job)
        return;
    const QString reason = message.isEmpty() ? QStringLiteral("engine error") : message;
    for (Intent& intent : job->intents)
        if (!intent.terminal)
            failIntent(intent, reason);
    tearDown(job, /*deleteFiles=*/true);
}

bool MangaVolumeTorrentDownloader::cancel(const QString& volumeId)
{
    const QString hash = m_hashByVolume.value(volumeId);
    Job* job = m_jobs.value(hash);
    if (!job)
        return false;

    bool found = false;
    for (Intent& intent : job->intents) {
        if (intent.volumeId == volumeId && !intent.terminal) {
            intent.terminal = true;
            m_ledger.setState(volumeId, QStringLiteral("cancelled"));
            m_hashByVolume.remove(volumeId);
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    if (hasLiveIntent(job)) {
        // Other volumes still ride this torrent — stop only the cancelled file by
        // re-narrowing the priority union to the survivors.
        const QVector<int> priorities =
            MangaVolumeFilePicker::unionPriorities(livePickedIndices(job), job->files.size());
        if (job->metadataKnown)
            m_engine->setFilePriorities(job->infoHash, priorities);
        return true;
    }

    // Last live intent cancelled — tear the torrent down.
    tearDown(job, /*deleteFiles=*/true);
    return true;
}

QVariantMap MangaVolumeTorrentDownloader::statusOf(const QString& volumeId) const
{
    QVariantMap status{{QStringLiteral("state"), QStringLiteral("none")},
                       {QStringLiteral("done"), 0.0},
                       {QStringLiteral("total"), 0.0}};
    const QString hash = m_hashByVolume.value(volumeId);
    if (Job* job = m_jobs.value(hash)) {
        for (const Intent& intent : job->intents) {
            if (intent.volumeId != volumeId || intent.terminal)
                continue;
            status[QStringLiteral("state")] =
                (job->metadataKnown && intent.pickedIndex >= 0) ? QStringLiteral("downloading")
                                                                : QStringLiteral("resolving");
            status[QStringLiteral("done")]  = static_cast<double>(intent.received);
            status[QStringLiteral("total")] = static_cast<double>(intent.fileSize);
            return status;
        }
    }
    // No live intent in memory — fall back to the journal so a terminal or
    // restart-restored outcome survives (never reports "none" for a volume the
    // ledger still remembers as completed / failed / cancelled).
    const VolumeRequestRow row = m_ledger.row(volumeId);
    if (!row.volumeId.isEmpty())
        status[QStringLiteral("state")] = row.state;
    return status;
}

void MangaVolumeTorrentDownloader::failIntent(Intent& intent, const QString& reason)
{
    intent.terminal = true;
    m_ledger.setState(intent.volumeId, QStringLiteral("failed"));
    m_hashByVolume.remove(intent.volumeId);
    emit failed(intent.volumeId, reason);
}

bool MangaVolumeTorrentDownloader::hasLiveIntent(const Job* job) const
{
    for (const Intent& intent : job->intents)
        if (!intent.terminal)
            return true;
    return false;
}

QVector<int> MangaVolumeTorrentDownloader::livePickedIndices(const Job* job) const
{
    QVector<int> indices;
    for (const Intent& intent : job->intents)
        if (!intent.terminal && intent.pickedIndex >= 0)
            indices.append(intent.pickedIndex);
    return indices;
}

void MangaVolumeTorrentDownloader::tearDown(Job* job, bool deleteFiles)
{
    // Idempotent guard: if this job is no longer the one registered under its
    // hash, a prior tearDown already handled it (e.g. a re-entrant engine signal).
    if (m_jobs.value(job->infoHash) != job)
        return;

    // Detach from every map FIRST. If m_engine->removeTorrent() synchronously
    // re-emits torrentError / torrentFinished (the real adapter can), the re-entrant
    // slot looks the job up, finds nothing, and returns — no double free.
    const QString hash = job->infoHash;
    for (const Intent& intent : job->intents)
        m_hashByVolume.remove(intent.volumeId);
    m_jobs.remove(hash);

    if (m_engine)
        m_engine->removeTorrent(hash, deleteFiles);
    delete job;
}
