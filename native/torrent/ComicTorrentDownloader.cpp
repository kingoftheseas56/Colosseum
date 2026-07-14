#include "ComicTorrentDownloader.h"

#include "ComicTorrentFilePicker.h"
#include "ComicTorrentMagnet.h"
#include "TorrentResult.h"   // humanSize
#include "engine/TorrentEngine.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
constexpr int kProgressThrottleMs = 500;
}

ComicTorrentDownloader::ComicTorrentDownloader(TorrentEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
    if (!m_engine) return;
    connect(m_engine, &TorrentEngine::metadataReady,
            this, &ComicTorrentDownloader::onMetadataReady);
    connect(m_engine, &TorrentEngine::torrentProgress,
            this, &ComicTorrentDownloader::onEngineProgress);
    connect(m_engine, &TorrentEngine::torrentFinished,
            this, &ComicTorrentDownloader::onEngineFinished);
    connect(m_engine, &TorrentEngine::torrentError,
            this, &ComicTorrentDownloader::onEngineFailed);
    connect(m_engine, &TorrentEngine::torrentAddFailed,
            this, &ComicTorrentDownloader::onEngineFailed);
}

ComicTorrentDownloader::~ComicTorrentDownloader()
{
    qDeleteAll(m_byHash);
}

ComicTorrentDownloader::Job* ComicTorrentDownloader::jobForHash(const QString& infoHash) const
{
    return m_byHash.value(infoHash.toLower(), nullptr);
}

ComicTorrentDownloader::Job* ComicTorrentDownloader::jobForIssue(const QString& issueId) const
{
    return jobForHash(m_hashByIssue.value(issueId.trimmed()));
}

bool ComicTorrentDownloader::alive(Job* job) const
{
    return job && m_byHash.value(job->infoHash) == job;
}

void ComicTorrentDownloader::download(const QString& issueIdIn, const QString& infoHashIn,
                                      const QString& title, const QString& magnetUri)
{
    const QString issueId = issueIdIn.trimmed();
    const QString infoHash = infoHashIn.trimmed().toLower();
    if (issueId.isEmpty() || infoHash.size() != 40) {
        emit failed(issueId, QStringLiteral("bad issue id / infoHash"));
        return;
    }
    if (m_hashByIssue.contains(issueId)) return;
    if (m_byHash.contains(infoHash)) {
        emit failed(issueId, QStringLiteral("torrent already serves another comic"));
        return;
    }
    if (!m_engine) {
        emit failed(issueId, QStringLiteral("torrent engine unavailable"));
        return;
    }
    if (!m_engine->isRunning()) m_engine->start();

    auto* job = new Job;
    job->issueId = issueId;
    job->infoHash = infoHash;
    job->title = title;
    job->saveDir = dirFor(infoHash);
    m_byHash.insert(infoHash, job);
    m_hashByIssue.insert(issueId, infoHash);
    emit resolving(issueId);

    QDir().mkpath(job->saveDir);
    const QString added = m_engine->addMagnet(ComicTorrentMagnet::build(infoHash, magnetUri),
                                               job->saveDir, false);
    if (added.isEmpty()) {
        failJob(job, QStringLiteral("engine rejected magnet"));
        return;
    }
    // Hybrid torrents may be requested by v1 BTIH while TorrentEngine reports
    // its canonical handle key from the v2 hash. Every engine signal uses the
    // returned key, so rebind immediately but retain the original save path.
    if (added != job->infoHash) {
        m_byHash.remove(job->infoHash);
        job->infoHash = added;
        m_byHash.insert(job->infoHash, job);
        m_hashByIssue.insert(issueId, job->infoHash);
    }
    const QJsonArray existing = m_engine->torrentFiles(added);
    if (!existing.isEmpty()) applyMetadata(job, existing);
}

void ComicTorrentDownloader::onMetadataReady(const QString& infoHash, const QString&,
                                             qint64, const QJsonArray& files)
{
    Job* job = jobForHash(infoHash);
    if (!alive(job) || job->picked) return;
    applyMetadata(job, files);
}

void ComicTorrentDownloader::applyMetadata(Job* job, const QJsonArray& files)
{
    const QList<ManifestFile> manifest = BookTorrentMagnet::filesToManifest(files);
    const ComicArchiveDecision decision = ComicTorrentFilePicker::decide(job->title, manifest);
    if (decision.candidates.isEmpty()) {
        m_engine->removeTorrent(job->infoHash, true);
        failJob(job, QStringLiteral("this torrent has no CBR/CBZ/CB7/CBT file (%1 file(s))")
                         .arg(manifest.size()));
        return;
    }
    if (decision.requiresChoice) {
        // Ambiguous / multi-volume: pause, zero every priority, and wait for the
        // user to pick one archive. Nothing downloads until chooseFile() lands.
        m_engine->pauseTorrent(job->infoHash);
        m_engine->setFilePriorities(job->infoHash, QVector<int>(manifest.size(), 0));
        job->choosing = true;
        job->candidates = decision.candidates;
        job->manifestSize = manifest.size();
        qInfo() << "[ComicTorrentDownloader]" << job->issueId << "awaiting archive choice among"
                << decision.candidates.size() << "candidates";
        emit fileSelectionRequired(job->issueId, toVariantFiles(decision.candidates));
        return;
    }
    qint64 bytes = 0;
    for (const ManifestFile& file : manifest)
        if (file.idx == decision.selected.idx) bytes = file.length;
    applyPickedFile(job, decision.selected.idx, decision.selected.name, bytes,
                    manifest.size(), true);
}

void ComicTorrentDownloader::applyPickedFile(Job* job, int pickedIdx, const QString& name,
                                             qint64 bytes, int fileCount, bool automatic)
{
    const bool wasChoosing = job->choosing;
    job->pickedIdx = pickedIdx;
    job->fileName = name;
    job->fileName.replace(QLatin1Char('\\'), QLatin1Char('/'));
    job->totalBytes = bytes;
    job->picked = true;
    job->choosing = false;
    job->candidates.clear();
    m_engine->setFilePriorities(job->infoHash,
        BookTorrentMagnet::pickToPriorities(pickedIdx, fileCount));
    // A manual choice resumes the torrent we paused; an auto-pick was never paused.
    if (wasChoosing) m_engine->resumeTorrent(job->infoHash);
    qInfo() << "[ComicTorrentDownloader]" << job->issueId << job->infoHash
            << (automatic ? "auto-picked" : "user-picked") << job->fileName
            << job->totalBytes << "bytes";
    emit fileSelected(job->issueId, job->fileName, automatic);
}

bool ComicTorrentDownloader::chooseFile(const QString& issueId, int fileIndex)
{
    Job* job = jobForIssue(issueId);
    if (!alive(job) || !job->choosing) return false;
    // Only an eligible comic archive index is accepted — never a non-comic file.
    const ComicArchiveCandidate* chosen = nullptr;
    for (const ComicArchiveCandidate& c : job->candidates)
        if (c.index == fileIndex) { chosen = &c; break; }
    if (!chosen) return false;
    const int idx = chosen->index;
    const QString name = chosen->name;
    const qint64 bytes = chosen->bytes;
    applyPickedFile(job, idx, name, bytes, job->manifestSize, false);
    return true;
}

QVariantList ComicTorrentDownloader::toVariantFiles(
    const QList<ComicArchiveCandidate>& candidates) const
{
    QVariantList out;
    out.reserve(candidates.size());
    for (const ComicArchiveCandidate& c : candidates) {
        out.append(QVariantMap{
            {QStringLiteral("index"), c.index},
            {QStringLiteral("name"), c.name},
            {QStringLiteral("extension"), c.extension},
            {QStringLiteral("sizeBytes"), QVariant::fromValue(c.bytes)},
            {QStringLiteral("sizeText"), humanSize(c.bytes)},
            {QStringLiteral("exactTitle"), c.exactTitle},
            {QStringLiteral("tokenCoverage"), c.tokenCoverage}
        });
    }
    return out;
}

void ComicTorrentDownloader::onEngineProgress(const QString& infoHash, float fraction,
                                              int, int, int, int)
{
    Job* job = jobForHash(infoHash);
    if (!alive(job) || !job->picked || job->totalBytes <= 0) return;
    job->received = static_cast<qint64>(fraction * static_cast<float>(job->totalBytes));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (job->lastProgressEmit == 0 || now - job->lastProgressEmit >= kProgressThrottleMs) {
        job->lastProgressEmit = now;
        emit progress(job->issueId, static_cast<double>(job->received),
                      static_cast<double>(job->totalBytes));
    }
}

void ComicTorrentDownloader::onEngineFinished(const QString& infoHash)
{
    Job* job = jobForHash(infoHash);
    if (!alive(job) || !job->picked) return;
    finalizeJob(job);
}

void ComicTorrentDownloader::onEngineFailed(const QString& infoHash, const QString& message)
{
    Job* job = jobForHash(infoHash);
    if (!alive(job)) return;
    m_engine->removeTorrent(infoHash, true);
    failJob(job, message.isEmpty() ? QStringLiteral("engine error") : message);
}

void ComicTorrentDownloader::finalizeJob(Job* job)
{
    const QString path = job->saveDir + QLatin1Char('/') + job->fileName;
    if (!QFileInfo::exists(path)) {
        m_engine->removeTorrent(job->infoHash, true);
        failJob(job, QStringLiteral("finished but comic archive is missing: %1")
                         .arg(job->fileName));
        return;
    }
    m_engine->removeTorrent(job->infoHash, false);
    const QString issueId = job->issueId;
    m_hashByIssue.remove(issueId);
    m_byHash.remove(job->infoHash);
    delete job;
    emit finished(issueId, path);
}

void ComicTorrentDownloader::failJob(Job* job, const QString& reason)
{
    const QString issueId = job->issueId;
    qWarning() << "[ComicTorrentDownloader] FAILED" << issueId << reason;
    m_hashByIssue.remove(issueId);
    m_byHash.remove(job->infoHash);
    delete job;
    emit failed(issueId, reason);
}

bool ComicTorrentDownloader::cancel(const QString& issueId)
{
    Job* job = jobForIssue(issueId);
    if (!alive(job)) return false;
    if (m_engine) m_engine->removeTorrent(job->infoHash, true);
    failJob(job, QStringLiteral("cancelled by user"));
    return true;
}

QVariantMap ComicTorrentDownloader::statusOf(const QString& issueId) const
{
    QVariantMap status{{QStringLiteral("state"), QStringLiteral("none")},
                       {QStringLiteral("done"), 0.0},
                       {QStringLiteral("total"), 0.0}};
    if (Job* job = jobForIssue(issueId)) {
        status[QStringLiteral("state")] = job->choosing ? QStringLiteral("choosing")
                                        : job->picked   ? QStringLiteral("downloading")
                                                        : QStringLiteral("resolving");
        status[QStringLiteral("done")] = static_cast<double>(job->received);
        status[QStringLiteral("total")] = static_cast<double>(job->totalBytes);
    }
    return status;
}

QVariantList ComicTorrentDownloader::activeJobs() const
{
    QVariantList rows;
    for (Job* job : m_byHash) {
        rows.append(QVariantMap{
            {QStringLiteral("id"), job->issueId},
            {QStringLiteral("label"), job->title},
            {QStringLiteral("state"), job->choosing ? QStringLiteral("choosing")
                                    : job->picked   ? QStringLiteral("downloading")
                                                    : QStringLiteral("resolving")},
            {QStringLiteral("done"), static_cast<double>(job->received)},
            {QStringLiteral("total"), static_cast<double>(job->totalBytes)}
        });
    }
    return rows;
}

QString ComicTorrentDownloader::baseDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/comics-torrent");
}

QString ComicTorrentDownloader::dirFor(const QString& infoHash) const
{
    return baseDir() + QLatin1Char('/') + infoHash.toLower();
}
