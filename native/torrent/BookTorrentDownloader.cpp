#include "BookTorrentDownloader.h"

#include "BookTorrentFilePicker.h"
#include "BookTorrentMagnet.h"
#include "engine/TorrentEngine.h"
#include "../engine/DownloadMediaPaths.h"
#include "../engine/DownloadFileOps.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>
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
        connect(m_engine, &TorrentEngine::ownedRemovalSettled, this,
                [this](const QString &token, bool success, int error) {
            for (Job *job : m_active) {
                if (job->settlementToken != token)
                    continue;
                if (success) {
                    job->released = true;
                    publishReleasedJob(job);
                } else {
                    failJob(job, tr("Could not release the ebook download (error %1).").arg(error));
                }
                return;
            }
        }, Qt::QueuedConnection);
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
    if (DownloadMediaPaths::moving()) {
        emit failed(infoHash, tr("Downloads are moving. Try again when the move finishes."));
        return;
    }
    if (infoHash.size() != 40) { emit failed(infoHash, QStringLiteral("bad infoHash")); return; }
    if (m_deleting.contains(infoHash)) {
        // The old hash directory is still being removed on the worker. Retain
        // the latest replacement request so it starts only after that worker
        // has settled, avoiding a stale delete racing a new download.
        m_deferredDownloads.insert(infoHash, DeferredDownload{title, author});
        return;
    }
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
    // engine) reuses the existing libtorrent handle. The metadata_received_alert is
    // single-shot per handle, and on a duplicate add the engine rebuilds the record with
    // metadataReady=false — so neither hasMetadata() (reads that stale flag) nor a
    // re-fired alert will drive the pick. torrentFiles() reads the LIVE handle's file
    // list, so a non-empty result means metadata is already here: apply it now, or the
    // job hangs forever at "resolving". (Codex cross-model review 2026-07-13.)
    const QJsonArray existing = m_engine->torrentFiles(hash);
    if (!existing.isEmpty()) applyMetadata(job, existing);
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
    if (!alive(job) || !job->picked || !job->settlementToken.isEmpty()) return;
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
    if (DownloadMediaPaths::enabled()) {
        const QString stagingDir = QFileInfo(dirFor(job->infoHash)).canonicalFilePath();
        if (!DownloadMediaPaths::destinationParentsSafe(stagingDir, onDisk)) {
            failJob(job, tr("The ebook torrent returned an invalid storage path."));
            return;
        }
        job->settlementToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!m_engine->requestOwnedRemoval(job->settlementToken, job->infoHash, stagingDir, false))
            failJob(job, tr("Could not confirm that this ebook owns the torrent storage."));
        return;
    }
    m_engine->removeTorrent(job->infoHash, /*deleteFiles=*/false);   // stop tracking, keep bytes
    indexFinishedJob(job, onDisk);
}

void BookTorrentDownloader::publishReleasedJob(Job *job)
{
    if (!alive(job) || !job->released || job->copying)
        return;
    const QString source = QDir(dirFor(job->infoHash)).filePath(job->fileName);
    if (job->destinationPath.isEmpty()) {
        job->destinationPath = DownloadMediaPaths::allocate(DownloadMediaPaths::bookPath(
            job->title, job->author, QFileInfo(source).suffix()), false, {}, QStringLiteral("book-torrent:") + job->infoHash);
    }
    const QString target = job->destinationPath;
    if (target.isEmpty() || DownloadMediaPaths::waitingForDrive(target)) {
        emit progress(job->infoHash, static_cast<double>(job->received), static_cast<double>(job->totalBytes));
        return;
    }
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        failJob(job, tr("Colosseum could not create the ebook download folder."));
        return;
    }
    job->copying = true;
    const QString token = job->settlementToken;
    const QString hash = job->infoHash;
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, source, target, token, hash] {
        const bool copied = watcher->result();
        watcher->deleteLater();
        Job *live = jobForHash(hash);
        if (!live || live->settlementToken != token) {
            if (copied)
                QFile::remove(target);
            return;
        }
        if (!copied) {
            failJob(live, tr("Colosseum could not verify the ebook copy. Its source is still in place."));
            return;
        }
        indexFinishedJob(live, target);
        if (m_index.value(hash).path == target)
            QFile::remove(source);
    });
    watcher->setFuture(QtConcurrent::run([source, target] {
        return !DownloadMediaPaths::waitingForDrive(target) && DownloadMediaPaths::copyVerified(source, target);
    }));
}

void BookTorrentDownloader::downloadLocationAvailable()
{
    const auto jobs = m_active.values();
    for (Job *job : jobs)
        publishReleasedJob(job);
}

void BookTorrentDownloader::indexFinishedJob(Job *job, const QString &path)
{
    Entry e{ path, job->title, job->author, job->totalBytes, QDateTime::currentMSecsSinceEpoch() };
    m_index.insert(job->infoHash, e);
    if (!saveIndex()) {
        m_index.remove(job->infoHash);
        failJob(job, tr("Colosseum could not record the completed ebook."));
        return;
    }
    const QString hash = job->infoHash;
    qInfo() << "[BookTorrentDownloader] complete" << hash << "→" << path;
    m_active.remove(hash);
    delete job;
    emit finished(hash, path);
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
        if (m_engine && j->settlementToken.isEmpty()) m_engine->removeTorrent(h, /*deleteFiles=*/true);
        delete j;
        emit failed(h, QStringLiteral("cancelled by user"));
    }
}

// Remove a completed (or in-flight) download and its files from disk — used by the
// book page's "one copy per book" replace: a new download deletes the previous one.
void BookTorrentDownloader::deleteDownload(const QString& infoHash)
{
    const QString h = infoHash.toLower();
    if (m_deleting.contains(h) || DownloadMediaPaths::moving()) return;
    const QString file = m_index.value(h).path;
    if (DownloadMediaPaths::waitingForDrive(file))
        return;
    if (Job* j = m_active.take(h)) {           // still downloading → stop + drop engine files
        if (m_engine && j->settlementToken.isEmpty()) m_engine->removeTorrent(h, /*deleteFiles=*/true);
        delete j;
    }
    const QString path = dirFor(h);
    m_deleting.insert(h);
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, h]() {
        const bool deleted = watcher->result();
        watcher->deleteLater();
        m_deleting.remove(h);
        if (!deleted) {
            qWarning() << "[BookTorrentDownloader] delete failed" << h;
            emit failed(h, tr("Colosseum could not delete the ebook."));
            return;
        }
        if (m_index.remove(h)) saveIndex();
        emit removed(h);
        const bool hasDeferred = m_deferredDownloads.contains(h);
        const auto deferred = m_deferredDownloads.take(h);
        if (hasDeferred)
            download(h, deferred.title, deferred.author);
    });
    watcher->setFuture(QtConcurrent::run([path, file]() {
        if (!file.isEmpty() && !DownloadMediaPaths::isWithin(path, file))
            return !QFileInfo::exists(file) || QFile::remove(file);
        if (path.isEmpty() || !QDir(path).exists()) return true;
        return QDir(path).removeRecursively() && !QDir(path).exists();
    }));
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
    if (it == m_index.constEnd() || it.value().path.isEmpty() || DownloadMediaPaths::waitingForDrive(it->path)
        || !QFileInfo::exists(it.value().path)) return {};
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
        if (j->released && !j->copying)
            s[QStringLiteral("error")] = tr("Waiting for the download drive.");
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
        e.path    = DownloadMediaPaths::resolved(o.value(QStringLiteral("path")).toString());
        e.title   = o.value(QStringLiteral("title")).toString();
        e.author  = o.value(QStringLiteral("author")).toString();
        e.bytes   = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble());
        if (!e.path.isEmpty() && (QFileInfo::exists(e.path) || DownloadMediaPaths::waitingForDrive(e.path)))
            m_index.insert(it.key().toLower(), e);
    }
}

bool BookTorrentDownloader::saveIndex() const
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
    QSaveFile f(baseDir() + QStringLiteral("/index.json"));
    const QByteArray contents = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return f.open(QIODevice::WriteOnly) && f.write(contents) == contents.size() && f.commit();
}

QList<DownloadLocation::Item> BookTorrentDownloader::managedDownloadItems() const
{
    QList<DownloadLocation::Item> items;
    for (const Entry &entry : m_index) {
        items.append({entry.path, DownloadMediaPaths::bookPath(entry.title, entry.author,
            QFileInfo(entry.path).suffix()), {}});
    }
    return items;
}

void BookTorrentDownloader::refreshDownloadPaths()
{
    for (Entry &entry : m_index)
        entry.path = DownloadMediaPaths::resolved(entry.path);
    saveIndex();
}

QVariantList BookTorrentDownloader::downloadedBooks() const
{
    QVariantList rows;
    for (auto it = m_index.cbegin(); it != m_index.cend(); ++it) {
        rows.append(QVariantMap{
            {QStringLiteral("id"), it.key()}, {QStringLiteral("title"), it->title},
            {QStringLiteral("author"), it->author}, {QStringLiteral("path"), it->path},
            {QStringLiteral("bytes"), it->bytes}, {QStringLiteral("addedAt"), it->addedAt},
            {QStringLiteral("missing"), localFile(it.key()).isEmpty()}});
    }
    return rows;
}

QVariantList BookTorrentDownloader::activeBookJobs() const
{
    QVariantList rows;
    for (const Job *job : m_active) {
        QVariantMap row = statusOf(job->infoHash);
        row.insert(QStringLiteral("id"), job->infoHash);
        row.insert(QStringLiteral("title"), job->title);
        row.insert(QStringLiteral("done"), row.value(QStringLiteral("received")));
        rows.append(row);
    }
    return rows;
}

QVariantMap BookTorrentDownloader::removeCompletedBook(const QString &infoHash)
{
    const QString hash = infoHash.toLower();
    const auto it = m_index.constFind(hash);
    if (it == m_index.constEnd())
        return DownloadFileOps::toMap({true, {}});
    if (DownloadMediaPaths::moving() || DownloadMediaPaths::waitingForDrive(it->path))
        return DownloadFileOps::toMap({false, tr("The download location is unavailable.")});
    const auto result = DownloadFileOps::removeFile(it->path);
    if (result.success) {
        m_index.remove(hash);
        saveIndex();
        emit removed(hash);
    }
    return DownloadFileOps::toMap(result);
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
