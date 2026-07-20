#include "AudiobookDownloader.h"

#include "../AudioPairingStore.h"
#include "../player/streamserver.h"

#include <QCryptographicHash>
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
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {

constexpr int    kProgressThrottleMs = 500;
constexpr int    kMaxManifestPolls   = 4;       // ~4× (30s /create timeout) → honest fail, not a 6-min hang
constexpr int    kManifestPollMs     = 2000;
constexpr int    kFileTransferTimeoutMs = 120000;

const QStringList& audioExts()
{
    static const QStringList e = {
        QStringLiteral("m4b"), QStringLiteral("mp3"), QStringLiteral("m4a"),
        QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("opus"),
        QStringLiteral("aac"), QStringLiteral("wav")
    };
    return e;
}

QString sanitizeFilename(const QString& raw)
{
    static const QRegularExpression kBadCharRe(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    static const QRegularExpression kWsRe(QStringLiteral("\\s+"));
    QString s = raw;
    s.replace(kBadCharRe, QStringLiteral("_")).replace(kWsRe, QStringLiteral(" "));
    s = s.trimmed();
    while (s.endsWith(QChar('.')) || s.endsWith(QChar(' '))) s.chop(1);
    if (s.isEmpty()) s = QStringLiteral("track");
    if (s.size() > 180) s = s.left(180);
    return s;
}

// natural compare: "Chapter 2" < "Chapter 10" (digit runs compared numerically)
bool naturalLess(const QString& a, const QString& b)
{
    int i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const QChar ca = a[i], cb = b[j];
        if (ca.isDigit() && cb.isDigit()) {
            int si = i, sj = j;
            while (i < a.size() && a[i].isDigit()) ++i;
            while (j < b.size() && b[j].isDigit()) ++j;
            const QStringView na = QStringView(a).mid(si, i - si);
            const QStringView nb = QStringView(b).mid(sj, j - sj);
            // compare by numeric value (strip leading zeros via length then lexicographic)
            const QString sa = na.toString(), sb = nb.toString();
            const long long va = sa.toLongLong(), vb = sb.toLongLong();
            if (va != vb) return va < vb;
        } else {
            const QChar la = ca.toLower(), lb = cb.toLower();
            if (la != lb) return la < lb;
            ++i; ++j;
        }
    }
    return a.size() < b.size();
}

} // namespace

AudiobookDownloader::AudiobookDownloader(QNetworkAccessManager* nam, StreamServer* stream, QObject* parent)
    : QObject(parent), m_nam(nam), m_stream(stream)
{
    loadIndex();
    if (m_stream)
        connect(m_stream, &StreamServer::fetchReady, this, &AudiobookDownloader::onFetchReady);
}

AudiobookDownloader::~AudiobookDownloader()
{
    if (m_active) { cleanupInFlight(m_active); delete m_active; m_active = nullptr; }
    qDeleteAll(m_queue);
    m_queue.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// disk + index
// ─────────────────────────────────────────────────────────────────────────────

QString AudiobookDownloader::baseDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/audiobooks");
}

QString AudiobookDownloader::dirFor(const QString& pairKey) const
{
    const QByteArray h = QCryptographicHash::hash(pairKey.toUtf8(), QCryptographicHash::Sha1);
    return baseDir() + QStringLiteral("/") + QString::fromLatin1(h.toHex().left(16));
}

void AudiobookDownloader::loadIndex()
{
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.dir     = o.value(QStringLiteral("dir")).toString();
        e.title   = o.value(QStringLiteral("title")).toString();
        e.author  = o.value(QStringLiteral("author")).toString();
        e.bytes   = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble());
        const QJsonArray fa = o.value(QStringLiteral("files")).toArray();
        for (const QJsonValue& v : fa) e.files << v.toString();
        // keep only if the directory + at least the first file still exist
        if (!e.files.isEmpty() && QFileInfo::exists(e.files.first()))
            m_index.insert(it.key(), e);
    }
}

void AudiobookDownloader::saveIndex() const
{
    QDir().mkpath(baseDir());
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("dir")]     = it.value().dir;
        o[QStringLiteral("title")]   = it.value().title;
        o[QStringLiteral("author")]  = it.value().author;
        o[QStringLiteral("bytes")]   = static_cast<double>(it.value().bytes);
        o[QStringLiteral("addedAt")] = static_cast<double>(it.value().addedAt);
        QJsonArray fa;
        for (const QString& p : it.value().files) fa.append(p);
        o[QStringLiteral("files")]   = fa;
        root[it.key()] = o;
    }
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ─────────────────────────────────────────────────────────────────────────────
// QML read-model
// ─────────────────────────────────────────────────────────────────────────────

QString AudiobookDownloader::localAudiobook(const QString& pairKey) const
{
    auto it = m_index.constFind(pairKey);
    if (it == m_index.constEnd() || it.value().files.isEmpty()) return {};
    if (!QFileInfo::exists(it.value().files.first())) return {};
    return it.value().dir;
}

bool AudiobookDownloader::isDownloaded(const QString& pairKey) const
{
    return !localAudiobook(pairKey).isEmpty();
}

QStringList AudiobookDownloader::localFiles(const QString& pairKey) const
{
    auto it = m_index.constFind(pairKey);
    if (it == m_index.constEnd()) return {};
    QStringList out;
    for (const QString& p : it.value().files)
        if (QFileInfo::exists(p)) out << p;
    return out;
}

QVariantMap AudiobookDownloader::statusOf(const QString& pairKey) const
{
    QVariantMap s;
    if (isDownloaded(pairKey)) {
        s[QStringLiteral("state")]    = QStringLiteral("done");
        s[QStringLiteral("received")] = static_cast<double>(m_index.value(pairKey).bytes);
        s[QStringLiteral("total")]    = static_cast<double>(m_index.value(pairKey).bytes);
        return s;
    }
    if (m_active && m_active->pairKey == pairKey) {
        s[QStringLiteral("state")]    = m_active->files.isEmpty() ? QStringLiteral("resolving")
                                                                  : QStringLiteral("downloading");
        s[QStringLiteral("received")] = static_cast<double>(m_active->doneBytes + m_active->fileReceived);
        s[QStringLiteral("total")]    = static_cast<double>(m_active->totalBytes);
        return s;
    }
    for (const Job* q : m_queue)
        if (q->pairKey == pairKey) { s[QStringLiteral("state")] = QStringLiteral("queued"); return s; }
    s[QStringLiteral("state")] = QStringLiteral("none");
    return s;
}

QVariantList AudiobookDownloader::activeDownloads() const
{
    QVariantList out;
    const auto describe = [](const Job* j, const QString& state) {
        return QVariantMap{
            {QStringLiteral("id"),       j->pairKey},
            {QStringLiteral("title"),    j->title},
            {QStringLiteral("author"),   j->author},
            {QStringLiteral("state"),    state},
            {QStringLiteral("received"), static_cast<double>(j->doneBytes + j->fileReceived)},
            {QStringLiteral("total"),    static_cast<double>(j->totalBytes)},
        };
    };
    if (m_active)
        out.append(describe(m_active, m_active->files.isEmpty() ? QStringLiteral("resolving")
                                                                : QStringLiteral("downloading")));
    for (const Job* q : m_queue)
        out.append(describe(q, QStringLiteral("queued")));
    return out;
}

QVariantList AudiobookDownloader::downloadedAudiobooks() const
{
    QVariantList out;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        out.append(QVariantMap{
            {QStringLiteral("id"), it.key()},
            {QStringLiteral("title"), e.title},
            {QStringLiteral("author"), e.author},
            {QStringLiteral("dir"), e.dir},
            {QStringLiteral("fileCount"), e.files.size()},
            {QStringLiteral("bytes"), e.bytes},
            {QStringLiteral("addedAt"), e.addedAt},
            {QStringLiteral("missing"), e.files.isEmpty() || !QFile::exists(e.files.first())}
        });
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// download flow
// ─────────────────────────────────────────────────────────────────────────────

bool AudiobookDownloader::isActive(const QString& pairKey) const
{
    if (m_active && m_active->pairKey == pairKey) return true;
    for (const Job* q : m_queue) if (q->pairKey == pairKey) return true;
    return false;
}

AudiobookDownloader::Job* AudiobookDownloader::jobForHash(const QString& infoHash) const
{
    const QString h = infoHash.toLower();
    if (m_active && m_active->infoHash == h) return m_active;
    for (Job* q : m_queue) if (q->infoHash == h) return q;
    return nullptr;
}

void AudiobookDownloader::downloadAudiobook(const QString& pairKey, const QString& infoHashIn,
                                            const QString& title, const QString& author,
                                            const QString& bookId)
{
    const QString infoHash = infoHashIn.trimmed().toLower();
    if (pairKey.isEmpty()) { emit failed(pairKey, QStringLiteral("empty pairKey")); return; }
    // Remember the reader's bookId for the auto-attach at finished(). Only overwrite
    // with a real id — a later empty-bookId call (e.g. the 4-arg selfTest path) must
    // not clobber a bookId a prior call already bound to this pairKey.
    if (!bookId.isEmpty()) m_bookIdFor.insert(pairKey, bookId);
    if (infoHash.size() != 40) { emit failed(pairKey, QStringLiteral("bad infoHash")); return; }
    if (isDownloaded(pairKey)) {
        const QString dir = localAudiobook(pairKey);
        attachToBook(pairKey, dir);           // idempotent re-emit still attaches
        emit finished(pairKey, dir);
        return;
    }
    if (isActive(pairKey)) return;
    if (!m_stream) { emit failed(pairKey, QStringLiteral("stream engine unavailable")); return; }

    Job* job = new Job;
    job->pairKey = pairKey; job->infoHash = infoHash; job->title = title; job->author = author;

    emit resolving(pairKey);

    if (m_active) { m_queue.append(job); emit activeCountChanged(); return; }
    m_active = job;
    emit activeCountChanged();
    // prefetch starts/adopts the engine + registers the torrent. We then race two paths
    // to learn the engine base: its fetchReady signal (fast when warm) and pollEngine
    // (robust when cold — fetchReady can be lost if the engine's /create POST hangs).
    m_stream->prefetch(infoHash, 0);
    pollEngine(job);
}

void AudiobookDownloader::onFetchReady(const QString& url, const QString& infoHash, int /*fileIdx*/)
{
    Job* job = jobForHash(infoHash);
    if (!job || job != m_active) return;         // not ours, or queued (handled when promoted)
    beginManifest(job, url);
}

// Derive the engine base from a stream URL (http://127.0.0.1:<port>/<hash>/0 → …/<hash>)
// and kick off the manifest fetch. Idempotent — first caller (fetchReady OR the watchdog) wins.
void AudiobookDownloader::beginManifest(Job* job, const QString& url)
{
    if (!job || job != m_active || !job->baseUrl.isEmpty() || url.isEmpty()) return;
    QString base = url;
    const int slash = base.lastIndexOf(QChar('/'));
    if (slash > 0) base = base.left(slash);
    job->baseUrl = base;
    requestManifest(job);
}

// Watchdog: fetchReady from StreamServer can be LOST on a cold engine start (its /create
// POST hangs before the signal fires — proven 2026-07-12). So we ALSO poll streamUrl,
// which returns a URL the moment the engine port is known, independent of that POST.
// Whichever path resolves the base first wins (beginManifest is idempotent).
void AudiobookDownloader::pollEngine(Job* job)
{
    if (!job || job != m_active || !job->baseUrl.isEmpty()) return;   // done / superseded
    const QString url = m_stream ? m_stream->streamUrl(job->infoHash, 0) : QString();
    if (!url.isEmpty()) { beginManifest(job, url); return; }
    if (++job->enginePolls > 40) {                                    // ~40s → engine never came up
        failJob(job, QStringLiteral("stream engine did not start"));
        return;
    }
    QTimer::singleShot(1000, this, [this, job]() { if (m_active == job) pollEngine(job); });
}

void AudiobookDownloader::requestManifest(Job* job)
{
    job->createAttempts += 1;
    QNetworkRequest req(QUrl(job->baseUrl + QStringLiteral("/create")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(30000);   // a cold engine can hang /create — time out → retry (poll loop)
    QNetworkReply* reply = m_nam->post(req, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, job]() {
        if (m_active != job) { reply->deleteLater(); return; }   // cancelled/replaced
        onManifestReply(reply, job);
    });
}

void AudiobookDownloader::onManifestReply(QNetworkReply* reply, Job* job)
{
    const QByteArray body = reply->readAll();
    const bool netOk = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();

    // total file count tells metadata-not-yet-loaded (0 files → poll) apart from
    // metadata-present-but-no-audio (>0 files, 0 audio → fail fast, don't poll 12×).
    const int totalFiles = netOk
        ? QJsonDocument::fromJson(body).object().value(QStringLiteral("files")).toArray().size() : 0;
    QList<FileJob> audio = netOk ? parseManifest(body) : QList<FileJob>();

    if (audio.isEmpty()) {
        if (totalFiles > 0) {
            failJob(job, QStringLiteral("this torrent has no audio files (%1 non-audio file(s))").arg(totalFiles));
            return;
        }
        // metadata still loading (no peers/pieces/manifest yet) → poll again
        if (job->createAttempts < kMaxManifestPolls) {
            QTimer::singleShot(kManifestPollMs, this, [this, job]() {
                if (m_active == job) requestManifest(job);
            });
            return;
        }
        failJob(job, QStringLiteral("torrent metadata unavailable (no peers?) after %1 tries").arg(job->createAttempts));
        return;
    }
    qInfo() << "[AudiobookDownloader] manifest:" << totalFiles << "files," << audio.size() << "audio";

    // natural-sort by name, then number for play order + destination filename
    std::sort(audio.begin(), audio.end(), [](const FileJob& a, const FileJob& b) {
        return naturalLess(a.name, b.name);
    });
    const QString dir = dirFor(job->pairKey);
    QDir().mkpath(dir);
    job->totalBytes = 0;
    for (int i = 0; i < audio.size(); ++i) {
        FileJob& fj = audio[i];
        const QString numbered = QStringLiteral("%1 - %2")
            .arg(i + 1, 2, 10, QChar('0')).arg(sanitizeFilename(fj.name));
        fj.finalPath = dir + QStringLiteral("/") + numbered;
        fj.partPath  = fj.finalPath + QStringLiteral(".part");
        job->totalBytes += fj.bytes;
    }
    job->files = audio;
    qInfo() << "[AudiobookDownloader]" << job->pairKey << "→" << audio.size()
            << "audio files," << job->totalBytes << "bytes total";
    job->current = 0;
    startNextFile(job);
}

QList<AudiobookDownloader::FileJob> AudiobookDownloader::parseManifest(const QByteArray& json) const
{
    QList<FileJob> out;
    const QJsonArray files = QJsonDocument::fromJson(json).object().value(QStringLiteral("files")).toArray();
    for (int i = 0; i < files.size(); ++i) {
        const QJsonObject o = files[i].toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        const QString ext = QFileInfo(name).suffix().toLower();
        if (!audioExts().contains(ext)) continue;
        FileJob fj;
        fj.fileIdx = i;                          // the streaming URL segment = original array index
        fj.name = name;
        fj.ext = ext;
        fj.bytes = static_cast<qint64>(o.value(QStringLiteral("length")).toDouble());
        out.append(fj);
    }
    return out;
}

void AudiobookDownloader::startNextFile(Job* job)
{
    if (job->current >= job->files.size()) { finalizeJob(job); return; }
    const FileJob& fj = job->files[job->current];

    // already on disk from a resumed job? skip.
    if (QFileInfo::exists(fj.finalPath) && QFileInfo(fj.finalPath).size() == fj.bytes && fj.bytes > 0) {
        job->doneBytes += fj.bytes;
        job->current += 1;
        startNextFile(job);
        return;
    }

    job->file = new QFile(fj.partPath);
    if (!job->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = job->file->errorString();
        delete job->file; job->file = nullptr;
        failJob(job, QStringLiteral("cannot open .part: %1").arg(err));
        return;
    }
    job->fileReceived = 0;
    job->sanityChecked = false;
    job->lastProgressEmit = 0;

    const QString url = job->baseUrl + QStringLiteral("/") + QString::number(fj.fileIdx);
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Accept", "*/*");
    req.setTransferTimeout(kFileTransferTimeoutMs);
    QNetworkReply* reply = m_nam->get(req);
    job->reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, &AudiobookDownloader::onFileReadyRead);
    connect(reply, &QNetworkReply::finished,  this, &AudiobookDownloader::onFileFinished);
}

void AudiobookDownloader::onFileReadyRead()
{
    if (!m_active || !m_active->reply || !m_active->file) return;
    Job* job = m_active;
    QNetworkReply* reply = job->reply.data();
    if (!reply) return;
    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty()) return;

    const qint64 written = job->file->write(chunk);
    if (written < 0) { failJob(job, QStringLiteral("disk write failed: %1").arg(job->file->errorString())); return; }
    job->fileReceived += written;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (job->lastProgressEmit == 0 || nowMs - job->lastProgressEmit >= kProgressThrottleMs) {
        job->lastProgressEmit = nowMs;
        emit progress(job->pairKey, static_cast<double>(job->doneBytes + job->fileReceived),
                      static_cast<double>(job->totalBytes));
    }
}

void AudiobookDownloader::onFileFinished()
{
    if (!m_active || !m_active->reply) return;
    Job* job = m_active;
    QNetworkReply* reply = job->reply.data();
    if (!reply) return;
    const QNetworkReply::NetworkError err = reply->error();
    const QString errStr = reply->errorString();
    if (err == QNetworkReply::NoError && job->file) {
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty()) { job->file->write(tail); job->fileReceived += tail.size(); }
    }
    reply->deleteLater();
    job->reply.clear();

    FileJob& fj = job->files[job->current];

    if (err != QNetworkReply::NoError) {
        if (job->file) { job->file->close(); job->file->remove(); delete job->file; job->file = nullptr; }
        failJob(job, QStringLiteral("file %1 failed: %2").arg(fj.name, errStr));
        return;
    }
    // short-read guard: Stremio serves the whole file; a truncated body = incomplete pieces.
    if (fj.bytes > 0 && job->fileReceived < fj.bytes) {
        if (job->file) { job->file->close(); job->file->remove(); delete job->file; job->file = nullptr; }
        failJob(job, QStringLiteral("file %1 truncated (%2/%3 bytes)")
                     .arg(fj.name).arg(job->fileReceived).arg(fj.bytes));
        return;
    }

    if (job->file) { job->file->close(); delete job->file; job->file = nullptr; }
    if (QFile::exists(fj.finalPath)) QFile::remove(fj.finalPath);
    if (!QFile::rename(fj.partPath, fj.finalPath)) {
        QFile::remove(fj.partPath);
        failJob(job, QStringLiteral("rename failed for %1").arg(fj.name));
        return;
    }
    job->doneBytes += job->fileReceived;
    emit progress(job->pairKey, static_cast<double>(job->doneBytes), static_cast<double>(job->totalBytes));
    job->current += 1;
    startNextFile(job);
}

void AudiobookDownloader::finalizeJob(Job* job)
{
    Entry e;
    e.dir = dirFor(job->pairKey);
    e.title = job->title; e.author = job->author;
    e.bytes = job->doneBytes;
    e.addedAt = QDateTime::currentMSecsSinceEpoch();
    for (const FileJob& fj : job->files) e.files << fj.finalPath;
    m_index.insert(job->pairKey, e);
    saveIndex();

    const QString pk = job->pairKey;
    const QString dir = e.dir;
    qInfo() << "[AudiobookDownloader] complete" << pk << "files=" << e.files.size() << "dir=" << dir;
    attachToBook(pk, dir);                    // read-along auto-attach (no pairing UI)
    emit finished(pk, dir);

    delete m_active; m_active = nullptr;
    promoteQueue();
    emit activeCountChanged();
}

// Write the read-along pairing under the reader's bookId — the SAME key Task 13's
// Audio tab reads it back by (BookStores::keyFor(<ebook path>)). savePairing is an
// upsert, so the idempotent re-emit path is safe. No-op without a store or a bookId.
void AudiobookDownloader::attachToBook(const QString& pairKey, const QString& dirPath)
{
    if (!m_pairing) return;
    const QString bookId = m_bookIdFor.value(pairKey);
    if (bookId.isEmpty()) return;
    m_pairing->savePairing(bookId, QVariantMap{
        {QStringLiteral("pairKey"), pairKey},
        {QStringLiteral("dirPath"), dirPath},
    });
    qInfo() << "[AudiobookDownloader] attached" << pairKey << "→ book" << bookId;
}

void AudiobookDownloader::failJob(Job* job, const QString& reason)
{
    cleanupInFlight(job);
    const QString pk = job->pairKey;
    qWarning() << "[AudiobookDownloader] FAILED" << pk << reason;
    emit failed(pk, reason);
    if (m_active == job) {
        delete m_active; m_active = nullptr;
        promoteQueue();
    } else {
        m_queue.removeAll(job);
        delete job;
    }
    emit activeCountChanged();
}

void AudiobookDownloader::promoteQueue()
{
    if (m_active || m_queue.isEmpty()) return;
    m_active = m_queue.takeFirst();
    m_stream->prefetch(m_active->infoHash, 0);
    pollEngine(m_active);
}

void AudiobookDownloader::cleanupInFlight(Job* job)
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

void AudiobookDownloader::cancelDownload(const QString& pairKey)
{
    if (m_active && m_active->pairKey == pairKey) {
        failJob(m_active, QStringLiteral("cancelled by user"));
        return;
    }
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i]->pairKey == pairKey) {
            Job* j = m_queue.takeAt(i);
            emit failed(pairKey, QStringLiteral("cancelled by user (queued)"));
            delete j;
            emit activeCountChanged();
            return;
        }
    }
}

void AudiobookDownloader::deleteAudiobook(const QString& pairKey)
{
    auto it = m_index.find(pairKey);
    if (it == m_index.end()) return;
    QDir(it.value().dir).removeRecursively();
    m_index.erase(it);
    saveIndex();
    emit removed(pairKey);
}

// ─────────────────────────────────────────────────────────────────────────────
// dev smoke — env COLOSSEUM_ABB_DLTEST="<pairKey>|<infoHash>"
// ─────────────────────────────────────────────────────────────────────────────

void AudiobookDownloader::selfTest(const QString& pairKey, const QString& infoHash)
{
    qInfo() << "[AudiobookDownloader] selfTest" << pairKey << infoHash;
    connect(this, &AudiobookDownloader::finished, this, [](const QString& pk, const QString& dir) {
        qInfo() << "[AudiobookDownloader] selfTest OK" << pk << "dir=" << dir;
    });
    connect(this, &AudiobookDownloader::failed, this, [](const QString& pk, const QString& why) {
        qWarning() << "[AudiobookDownloader] selfTest FAILED" << pk << why;
    });
    connect(this, &AudiobookDownloader::progress, this, [](const QString& pk, double rcv, double tot) {
        qInfo() << "[AudiobookDownloader] selfTest progress" << pk << rcv << "/" << tot;
    });
    downloadAudiobook(pairKey, infoHash, QStringLiteral("selftest"), QString());
}
