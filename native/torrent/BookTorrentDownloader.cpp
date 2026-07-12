#include "BookTorrentDownloader.h"

#include "BookTorrentFilePicker.h"
#include "player/streamserver.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int kProgressThrottleMs = 500;
constexpr int kMaxManifestPolls   = 4;      // ~4× (30s /create timeout) → honest fail, not a hang
constexpr int kManifestPollMs     = 2000;
constexpr int kMaxEnginePolls     = 40;     // ~40s cold-engine watchdog
constexpr int kCreateTimeoutMs    = 30000;
constexpr int kFileTransferTimeoutMs = 120000;
}

BookTorrentDownloader::BookTorrentDownloader(QNetworkAccessManager* nam, StreamServer* stream, QObject* parent)
    : QObject(parent), m_nam(nam), m_stream(stream)
{
    loadIndex();
    if (m_stream)
        connect(m_stream, &StreamServer::fetchReady, this, &BookTorrentDownloader::onFetchReady);
}

BookTorrentDownloader::~BookTorrentDownloader()
{
    for (Job* j : m_active) { cleanupInFlight(j); delete j; }
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

BookTorrentDownloader::Job* BookTorrentDownloader::jobForReply(QNetworkReply* r) const
{
    if (!r) return nullptr;
    for (Job* j : m_active) if (j->reply.data() == r) return j;
    return nullptr;
}

// ── entry point ────────────────────────────────────────────────────────────────

void BookTorrentDownloader::download(const QString& infoHashIn, const QString& title, const QString& author)
{
    const QString infoHash = infoHashIn.trimmed().toLower();
    if (infoHash.size() != 40) { emit failed(infoHash, QStringLiteral("bad infoHash")); return; }
    if (isDownloaded(infoHash)) { emit finished(infoHash, m_index.value(infoHash).path); return; }
    if (m_active.contains(infoHash)) return;                          // already in flight (no-op)
    if (!m_stream) { emit failed(infoHash, QStringLiteral("stream engine unavailable")); return; }

    auto* job = new Job{};
    job->infoHash = infoHash; job->title = title; job->author = author;
    m_active.insert(infoHash, job);
    emit resolving(infoHash);
    // Race two paths to the engine base: fetchReady (fast when warm) and pollEngine
    // (robust when cold — fetchReady can be lost if the engine's /create POST hangs).
    m_stream->prefetch(infoHash, 0);
    pollEngine(job);
}

// ── engine handshake (adapted from AudiobookDownloader) ─────────────────────────

void BookTorrentDownloader::onFetchReady(const QString& url, const QString& infoHash, int /*fileIdx*/)
{
    Job* job = jobForHash(infoHash);
    if (!alive(job)) return;
    beginManifest(job, url);
}

// Derive the engine base from a stream URL (…/<hash>/0 → …/<hash>) and start the manifest.
// Idempotent — whichever path (fetchReady OR watchdog) resolves the base first wins.
void BookTorrentDownloader::beginManifest(Job* job, const QString& url)
{
    if (!alive(job) || !job->baseUrl.isEmpty() || url.isEmpty()) return;
    QString base = url;
    const int slash = base.lastIndexOf(QChar('/'));
    if (slash > 0) base = base.left(slash);
    job->baseUrl = base;
    requestManifest(job);
}

// Watchdog: fetchReady can be LOST on a cold engine (its /create POST hangs before the
// signal). So we ALSO poll streamUrl, which returns a URL the moment the port is known.
void BookTorrentDownloader::pollEngine(Job* job)
{
    if (!alive(job) || !job->baseUrl.isEmpty()) return;
    const QString url = m_stream ? m_stream->streamUrl(job->infoHash, 0) : QString();
    if (!url.isEmpty()) { beginManifest(job, url); return; }
    if (++job->enginePolls > kMaxEnginePolls) { failJob(job, QStringLiteral("stream engine did not start")); return; }
    QTimer::singleShot(1000, this, [this, job]() { if (alive(job)) pollEngine(job); });
}

void BookTorrentDownloader::requestManifest(Job* job)
{
    job->createAttempts += 1;
    QNetworkRequest req(QUrl(job->baseUrl + QStringLiteral("/create")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(kCreateTimeoutMs);   // a cold engine can hang /create → time out, retry
    QNetworkReply* reply = m_nam->post(req, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, job]() {
        if (!alive(job)) { reply->deleteLater(); return; }
        onManifestReply(reply, job);
    });
}

void BookTorrentDownloader::onManifestReply(QNetworkReply* reply, Job* job)
{
    const QByteArray body = reply->readAll();
    const bool netOk = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();

    const QJsonArray arr = netOk
        ? QJsonDocument::fromJson(body).object().value(QStringLiteral("files")).toArray()
        : QJsonArray();

    if (arr.isEmpty()) {
        // metadata still loading (no peers/pieces yet) → poll again, bounded
        if (job->createAttempts < kMaxManifestPolls) {
            QTimer::singleShot(kManifestPollMs, this, [this, job]() { if (alive(job)) requestManifest(job); });
            return;
        }
        failJob(job, QStringLiteral("torrent metadata unavailable (no peers?) after %1 tries").arg(job->createAttempts));
        return;
    }

    QList<ManifestFile> mfs;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr[i].toObject();
        mfs.push_back({ i, o.value(QStringLiteral("name")).toString(),
                        static_cast<qint64>(o.value(QStringLiteral("length")).toDouble()) });
    }
    const PickedFile pick = BookTorrentFilePicker::pick(job->title, job->author, mfs);
    if (pick.idx < 0) {
        failJob(job, QStringLiteral("this torrent has no ebook file (%1 file(s))").arg(mfs.size()));
        return;
    }
    job->pickedIdx = pick.idx; job->fileName = pick.name; job->ext = pick.ext;
    job->totalBytes = 0;
    for (const auto& m : mfs) if (m.idx == pick.idx) job->totalBytes = m.length;
    qInfo() << "[BookTorrentDownloader]" << job->infoHash << "→ picked" << job->fileName
            << "(" << mfs.size() << "files," << job->totalBytes << "bytes )";
    startFile(job);
}

// ── single-file streaming ───────────────────────────────────────────────────────

void BookTorrentDownloader::startFile(Job* job)
{
    QDir().mkpath(dirFor(job->infoHash));
    const QString safe = QFileInfo(job->fileName).fileName();
    job->finalPath = dirFor(job->infoHash) + QStringLiteral("/")
                     + (safe.isEmpty() ? QStringLiteral("book.") + job->ext : safe);
    job->partPath  = job->finalPath + QStringLiteral(".part");
    job->file = new QFile(job->partPath);
    if (!job->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = job->file->errorString();
        delete job->file; job->file = nullptr;
        failJob(job, QStringLiteral("cannot open .part: %1").arg(err));
        return;
    }
    job->received = 0;
    job->lastProgressEmit = 0;
    const QString url = job->baseUrl + QStringLiteral("/") + QString::number(job->pickedIdx);
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Accept", "*/*");
    req.setTransferTimeout(kFileTransferTimeoutMs);
    job->reply = m_nam->get(req);                            // plain GET, no Range → whole file
    connect(job->reply, &QNetworkReply::readyRead, this, &BookTorrentDownloader::onFileReadyRead);
    connect(job->reply, &QNetworkReply::finished,  this, &BookTorrentDownloader::onFileFinished);
}

void BookTorrentDownloader::onFileReadyRead()
{
    auto* r = qobject_cast<QNetworkReply*>(sender());
    Job* job = jobForReply(r);
    if (!job || !job->file) return;
    const QByteArray chunk = r->readAll();
    if (chunk.isEmpty()) return;
    const qint64 written = job->file->write(chunk);
    if (written < 0) { failJob(job, QStringLiteral("disk write failed: %1").arg(job->file->errorString())); return; }
    job->received += written;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (job->lastProgressEmit == 0 || nowMs - job->lastProgressEmit >= kProgressThrottleMs) {
        job->lastProgressEmit = nowMs;
        emit progress(job->infoHash, static_cast<double>(job->received), static_cast<double>(job->totalBytes));
    }
}

void BookTorrentDownloader::onFileFinished()
{
    auto* r = qobject_cast<QNetworkReply*>(sender());
    Job* job = jobForReply(r);
    if (!job) return;
    const QNetworkReply::NetworkError err = r->error();
    const QString errStr = r->errorString();
    if (err == QNetworkReply::NoError && job->file) {
        const QByteArray tail = r->readAll();
        if (!tail.isEmpty()) { job->file->write(tail); job->received += tail.size(); }
    }
    r->deleteLater();
    job->reply.clear();

    if (err != QNetworkReply::NoError) {
        if (job->file) { job->file->close(); job->file->remove(); delete job->file; job->file = nullptr; }
        failJob(job, QStringLiteral("download failed: %1").arg(errStr));
        return;
    }
    // short-read guard: Stremio serves the whole file; a truncated body = incomplete pieces.
    if (job->totalBytes > 0 && job->received < job->totalBytes) {
        if (job->file) { job->file->close(); job->file->remove(); delete job->file; job->file = nullptr; }
        failJob(job, QStringLiteral("truncated (%1/%2 bytes)").arg(job->received).arg(job->totalBytes));
        return;
    }
    finalizeJob(job);
}

void BookTorrentDownloader::finalizeJob(Job* job)
{
    if (job->file) { job->file->close(); delete job->file; job->file = nullptr; }
    if (QFile::exists(job->finalPath)) QFile::remove(job->finalPath);
    if (!QFile::rename(job->partPath, job->finalPath)) {
        QFile::remove(job->partPath);
        failJob(job, QStringLiteral("rename failed for %1").arg(job->fileName));
        return;
    }
    Entry e{ job->finalPath, job->title, job->author, job->totalBytes, QDateTime::currentMSecsSinceEpoch() };
    m_index.insert(job->infoHash, e);
    saveIndex();
    const QString hash = job->infoHash, path = job->finalPath;
    qInfo() << "[BookTorrentDownloader] complete" << hash << "→" << path;
    m_active.remove(hash);
    delete job;
    emit finished(hash, path);
}

void BookTorrentDownloader::failJob(Job* job, const QString& reason)
{
    const QString hash = job->infoHash;
    cleanupInFlight(job);
    qWarning() << "[BookTorrentDownloader] FAILED" << hash << reason;
    m_active.remove(hash);
    delete job;
    emit failed(hash, reason);
}

void BookTorrentDownloader::cleanupInFlight(Job* job)
{
    if (job->reply) {
        QNetworkReply* r = job->reply.data();
        if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
        job->reply.clear();
    }
    if (job->file) {
        job->file->close();
        const QString p = job->file->fileName();
        delete job->file; job->file = nullptr;
        QFile::remove(p);
    }
}

void BookTorrentDownloader::cancelDownload(const QString& infoHash)
{
    const QString h = infoHash.toLower();
    if (Job* j = m_active.take(h)) {
        cleanupInFlight(j);
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
        s[QStringLiteral("state")]    = j->baseUrl.isEmpty() ? QStringLiteral("resolving")
                                                             : QStringLiteral("downloading");
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
