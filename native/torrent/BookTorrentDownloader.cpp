#include "BookTorrentDownloader.h"

#include "BookTorrentFilePicker.h"
#include "BookTorrentMagnet.h"
#include "engine/TorrentEngine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QVector>

namespace {
constexpr int kProgressThrottleMs = 500;
}

BookTorrentDownloader::BookTorrentDownloader(TorrentEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
    loadIndex();
    if (m_engine) {
        connect(m_engine, &TorrentEngine::metadataReady,   this, &BookTorrentDownloader::onMetadataReady);
        connect(m_engine, &TorrentEngine::torrentProgress, this, &BookTorrentDownloader::onEngineProgress);
        connect(m_engine, &TorrentEngine::torrentFinished, this, &BookTorrentDownloader::onEngineFinished);
        connect(m_engine, &TorrentEngine::torrentError,     this, &BookTorrentDownloader::onEngineFailed);
        connect(m_engine, &TorrentEngine::torrentAddFailed, this, &BookTorrentDownloader::onEngineFailed);
    }
}

BookTorrentDownloader::~BookTorrentDownloader()
{
    for (Job* j : m_active) delete j;
    m_active.clear();
}

// ── job bookkeeping ───────────────────────────────────────────────────────────

bool BookTorrentDownloader::alive(Job* job) const
{
    return job && m_active.value(job->infoHash) == job;
}

BookTorrentDownloader::Job* BookTorrentDownloader::jobForHash(const QString& infoHash) const
{
    return m_active.value(infoHash.toLower(), nullptr);
}

// ── entry point ────────────────────────────────────────────────────────────────

void BookTorrentDownloader::download(const QString& infoHashIn, const QString& title, const QString& author)
{
    const QString infoHash = infoHashIn.trimmed().toLower();
    if (infoHash.size() != 40) { emit failed(infoHash, QStringLiteral("bad infoHash")); return; }
    if (isDownloaded(infoHash)) { emit finished(infoHash, m_index.value(infoHash).path); return; }
    if (m_active.contains(infoHash)) return;                 // already in flight
    if (!m_engine) { emit failed(infoHash, QStringLiteral("torrent engine unavailable")); return; }

    // Lazy-wake the born-asleep engine on first real download — idle stays no-network.
    if (!m_engine->isRunning()) m_engine->start();

    auto* job = new Job{};
    job->infoHash = infoHash; job->title = title; job->author = author;
    m_active.insert(infoHash, job);
    emit resolving(infoHash);

    QDir().mkpath(dirFor(infoHash));
    const QString hash = m_engine->addMagnet(BookTorrentMagnet::buildMagnet(infoHash),
                                             dirFor(infoHash), /*paused=*/false);
    if (hash.isEmpty()) { failJob(job, QStringLiteral("engine rejected magnet")); return; }

    // A duplicate add (cancel+retry, or the same torrent already active on the shared
    // engine) reuses the existing handle. metadata_received_alert is single-shot per
    // handle, so metadataReady won't re-fire — drive the pick from the engine's existing
    // file list, or the job would hang forever at "resolving".
    if (m_engine->hasMetadata(hash)) applyMetadata(job, m_engine->torrentFiles(hash));
}

// ── engine handlers ──────────────────────────────────────────────────────────────

void BookTorrentDownloader::onMetadataReady(const QString& infoHash, const QString&,
                                            qint64, const QJsonArray& files)
{
    Job* job = jobForHash(infoHash);
    if (!alive(job) || job->picked) return;
    applyMetadata(job, files);
}

// Pick the one ebook out of the resolved file list, tell the engine to fetch only it,
// and record the choice on the job. Shared by the metadataReady signal path and the
// duplicate-add synthesize path in download() (a reused handle won't re-emit the alert).
void BookTorrentDownloader::applyMetadata(Job* job, const QJsonArray& files)
{
    const QList<ManifestFile> mfs = BookTorrentMagnet::filesToManifest(files);
    const PickedFile pick = BookTorrentFilePicker::pick(job->title, job->author, mfs);
    if (pick.idx < 0) {
        m_engine->removeTorrent(job->infoHash, /*deleteFiles=*/true);
        failJob(job, QStringLiteral("this torrent has no ebook file (%1 file(s))").arg(mfs.size()));
        return;
    }
    job->pickedIdx = pick.idx;
    job->fileName  = pick.name;
    // libtorrent file paths use '\' on Windows; forward-slash the stored index path and
    // the finished() path so the reader opens a clean separator (Windows resolves either).
    job->fileName.replace(QLatin1Char('\\'), QLatin1Char('/'));
    job->totalBytes = 0;
    for (const auto& m : mfs) if (m.idx == pick.idx) job->totalBytes = m.length;
    job->picked = true;
    m_engine->setFilePriorities(job->infoHash, BookTorrentMagnet::pickToPriorities(pick.idx, mfs.size()));
    qInfo() << "[BookTorrentDownloader]" << job->infoHash << "→ picked" << job->fileName
            << "(" << mfs.size() << "files," << job->totalBytes << "bytes )";
}

void BookTorrentDownloader::onEngineProgress(const QString& infoHash, float progress,
                                             int, int, int, int)
{
    Job* job = jobForHash(infoHash);
    if (!alive(job) || !job->picked || job->totalBytes <= 0) return;
    job->received = static_cast<qint64>(progress * static_cast<float>(job->totalBytes));
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (job->lastProgressEmit == 0 || nowMs - job->lastProgressEmit >= kProgressThrottleMs) {
        job->lastProgressEmit = nowMs;
        emit this->progress(infoHash, static_cast<double>(job->received),
                            static_cast<double>(job->totalBytes));
    }
}

void BookTorrentDownloader::onEngineFinished(const QString& infoHash)
{
    Job* job = jobForHash(infoHash);
    if (!alive(job) || !job->picked) return;   // pre-pick "finished" cannot happen; guard anyway
    finalizeJob(job);
}

void BookTorrentDownloader::onEngineFailed(const QString& infoHash, const QString& message)
{
    Job* job = jobForHash(infoHash);
    if (!alive(job)) return;
    m_engine->removeTorrent(infoHash, /*deleteFiles=*/true);
    failJob(job, message.isEmpty() ? QStringLiteral("engine error") : message);
}

void BookTorrentDownloader::finalizeJob(Job* job)
{
    // The engine saved the picked file under the per-book savePath, preserving the
    // torrent-relative path. Point the index straight at it — no move (a move could
    // race the engine's file handle). Release the engine handle but KEEP the file.
    const QString onDisk = dirFor(job->infoHash) + QStringLiteral("/") + job->fileName;
    if (!QFileInfo::exists(onDisk)) {
        m_engine->removeTorrent(job->infoHash, /*deleteFiles=*/true);
        failJob(job, QStringLiteral("finished but file missing: %1").arg(job->fileName));
        return;
    }
    m_engine->removeTorrent(job->infoHash, /*deleteFiles=*/false);   // stop tracking, keep bytes
    Entry e{ onDisk, job->title, job->author, job->totalBytes, QDateTime::currentMSecsSinceEpoch() };
    m_index.insert(job->infoHash, e);
    saveIndex();
    const QString hash = job->infoHash;
    qInfo() << "[BookTorrentDownloader] complete" << hash << "→" << onDisk;
    m_active.remove(hash);
    delete job;
    emit finished(hash, onDisk);
}

void BookTorrentDownloader::failJob(Job* job, const QString& reason)
{
    const QString hash = job->infoHash;
    qWarning() << "[BookTorrentDownloader] FAILED" << hash << reason;
    m_active.remove(hash);
    delete job;
    emit failed(hash, reason);
}

void BookTorrentDownloader::cancelDownload(const QString& infoHash)
{
    const QString h = infoHash.toLower();
    if (Job* j = m_active.take(h)) {
        if (m_engine) m_engine->removeTorrent(h, /*deleteFiles=*/true);
        delete j;
        emit failed(h, QStringLiteral("cancelled by user"));
    }
}

// ── disk + index ────────────────────────────────────────────────────────────────

QString BookTorrentDownloader::baseDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/books-torrent");
}

QString BookTorrentDownloader::dirFor(const QString& infoHash) const
{
    return baseDir() + QStringLiteral("/") + infoHash.toLower();
}

QString BookTorrentDownloader::localFile(const QString& infoHash) const
{
    auto it = m_index.constFind(infoHash.toLower());
    if (it == m_index.constEnd() || it.value().path.isEmpty() || !QFileInfo::exists(it.value().path)) return {};
    return it.value().path;
}

bool BookTorrentDownloader::isDownloaded(const QString& infoHash) const
{
    return !localFile(infoHash).isEmpty();
}

QVariantMap BookTorrentDownloader::statusOf(const QString& infoHash) const
{
    const QString h = infoHash.toLower();
    QVariantMap s;
    if (isDownloaded(h)) {
        s[QStringLiteral("state")]    = QStringLiteral("done");
        s[QStringLiteral("received")] = static_cast<double>(m_index.value(h).bytes);
        s[QStringLiteral("total")]    = static_cast<double>(m_index.value(h).bytes);
        return s;
    }
    if (Job* j = m_active.value(h, nullptr)) {
        s[QStringLiteral("state")] = j->picked ? QStringLiteral("downloading") : QStringLiteral("resolving");
        s[QStringLiteral("received")] = static_cast<double>(j->received);
        s[QStringLiteral("total")]    = static_cast<double>(j->totalBytes);
        return s;
    }
    s[QStringLiteral("state")] = QStringLiteral("none");
    s[QStringLiteral("received")] = 0; s[QStringLiteral("total")] = 0;
    return s;
}

void BookTorrentDownloader::loadIndex()
{
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.path    = o.value(QStringLiteral("path")).toString();
        e.title   = o.value(QStringLiteral("title")).toString();
        e.author  = o.value(QStringLiteral("author")).toString();
        e.bytes   = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble());
        if (!e.path.isEmpty() && QFileInfo::exists(e.path))   // prune ghosts
            m_index.insert(it.key().toLower(), e);
    }
}

void BookTorrentDownloader::saveIndex() const
{
    QDir().mkpath(baseDir());
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("path")]    = it.value().path;
        o[QStringLiteral("title")]   = it.value().title;
        o[QStringLiteral("author")]  = it.value().author;
        o[QStringLiteral("bytes")]   = static_cast<double>(it.value().bytes);
        o[QStringLiteral("addedAt")] = static_cast<double>(it.value().addedAt);
        root[it.key()] = o;
    }
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ── dev smoke — env COLOSSEUM_TORRENT_DLTEST="<infoHash>|<title>" ────────────────

void BookTorrentDownloader::selfTest(const QString& infoHash, const QString& title)
{
    qInfo() << "[bt-dl] selfTest" << infoHash << title;
    connect(this, &BookTorrentDownloader::finished, this, [](const QString& h, const QString& p) {
        qInfo() << "[bt-dl] DONE" << h << p; QCoreApplication::quit();
    });
    connect(this, &BookTorrentDownloader::failed, this, [](const QString& h, const QString& why) {
        qWarning() << "[bt-dl] FAIL" << h << why; QCoreApplication::quit();
    });
    connect(this, &BookTorrentDownloader::progress, this, [](const QString&, double r, double t) {
        if (t > 0) qInfo() << "[bt-dl] progress" << static_cast<int>(100.0 * r / t) << "%";
    });
    download(infoHash, title, QString());
}
