#include "ComicTorrentDownloader.h"

#include "ComicTorrentFilePicker.h"
#include "ComicTorrentMagnet.h"
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
    const PickedFile picked = ComicTorrentFilePicker::pick(job->title, manifest);
    if (picked.idx < 0) {
        m_engine->removeTorrent(job->infoHash, true);
        failJob(job, QStringLiteral("this torrent has no CBR/CBZ/CB7/CBT file (%1 file(s))")
                         .arg(manifest.size()));
        return;
    }
    job->pickedIdx = picked.idx;
    job->fileName = picked.name;
    job->fileName.replace(QLatin1Char('\\'), QLatin1Char('/'));
    for (const ManifestFile& file : manifest)
        if (file.idx == picked.idx) job->totalBytes = file.length;
    job->picked = true;
    m_engine->setFilePriorities(job->infoHash,
        BookTorrentMagnet::pickToPriorities(picked.idx, manifest.size()));
    qInfo() << "[ComicTorrentDownloader]" << job->issueId << job->infoHash
            << "picked" << job->fileName << job->totalBytes << "bytes";
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
        status[QStringLiteral("state")] = job->picked ? QStringLiteral("downloading")
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
            {QStringLiteral("state"), job->picked ? QStringLiteral("downloading")
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
