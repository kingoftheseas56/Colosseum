#include "AudiobookDownloader.h"
#include "DownloadFileOps.h"
#include "AudiobookTorrentTransport.h"

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
#include <utility>

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
    if (m_torrent) m_torrent->disconnect(this);
    if (m_active && m_torrent && m_active->nativeTorrent) m_torrent->cancel(m_active->generation);
    if (m_active) { cleanupInFlight(m_active); delete m_active; m_active = nullptr; }
    qDeleteAll(m_queue);
    m_queue.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// disk + index
// ─────────────────────────────────────────────────────────────────────────────

void AudiobookDownloader::setTorrentTransport(AudiobookTorrentTransport* transport)
{
    if (m_active || !m_queue.isEmpty() || m_torrent || !transport) return;
    m_torrent = transport;
    transport->setParent(this);
    connect(transport, &AudiobookTorrentTransport::phaseChanged, this,
            [this](quint64 generation, const QString& phase) {
        if (!m_active || !m_active->nativeTorrent || m_active->generation != generation) return;
        m_active->nativePhase = phase;
        const QString key = m_active->signalKey;
        emit resolving(key); // canonical stopping/finishing state is already visible
    });
    connect(transport, &AudiobookTorrentTransport::cancelled, this, [this](quint64 generation) {
        if (m_active && m_active->nativeTorrent && m_active->generation == generation)
            retireCancelledJob(m_active);
    });
    connect(transport, &AudiobookTorrentTransport::progress, this,
            [this](quint64 generation, qint64 received, qint64 total) {
        if (!m_active || !m_active->nativeTorrent || m_active->generation != generation) return;
        m_active->doneBytes = received;
        m_active->totalBytes = total;
        const QString key = m_active->signalKey;
        emit progress(key, double(received), double(total));
    });
    connect(transport, &AudiobookTorrentTransport::completed, this,
            [this](quint64 generation, const QString& dir, const QStringList& files, qint64 bytes) {
        if (!m_active || !m_active->nativeTorrent || m_active->generation != generation) return;
        m_active->completedDir = dir;
        m_active->doneBytes = bytes;
        m_active->totalBytes = bytes;
        for (const QString& path : files) {
            FileJob file; file.finalPath = path;
            m_active->files.append(file);
        }
        finalizeJob(m_active);
    });
    connect(transport, &AudiobookTorrentTransport::failed, this,
            [this](quint64 generation, const QString& reason) {
        if (m_active && m_active->nativeTorrent && m_active->generation == generation)
            failJob(m_active, reason);
    });
    connect(transport, &QObject::destroyed, this, [this] {
        if (m_active && m_active->nativeTorrent)
            failJob(m_active, QStringLiteral("Audiobook torrent transport is unavailable."));
    });
}

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
        // Old root keys are bare pairKeys. Never rehash, move, or reinterpret their folders.
        e.recordingKey = it.key();
        e.pairKey = o.value(QStringLiteral("pairKey")).toString();
        e.bundle = o.value(QStringLiteral("bundle")).toBool()
                || it.key().startsWith(QStringLiteral("bundle#src:"));
        if (e.bundle) e.pairKey.clear();
        else if (e.pairKey.isEmpty() && !it.key().contains(QStringLiteral("#src:"))
                 && !it.key().contains(QStringLiteral("#as:")))
            e.pairKey = it.key();
        e.dir = o.value(QStringLiteral("dir")).toString();
        e.title = o.value(QStringLiteral("title")).toString();
        e.author = o.value(QStringLiteral("author")).toString();
        e.audiosiloRecordingId = o.value(QStringLiteral("audiosiloRecordingId")).toString();
        e.format = o.value(QStringLiteral("format")).toString();
        e.abridgement = o.value(QStringLiteral("abridgement")).toString();
        e.language = o.value(QStringLiteral("language")).toString();
        e.sourceKind = o.value(QStringLiteral("sourceKind")).toString();
        e.sourceRef = o.value(QStringLiteral("sourceRef")).toString();
        for (const QJsonValue& name : o.value(QStringLiteral("narrators")).toArray())
            if (name.isString()) e.narrators << name.toString();
        e.bookId = o.value(QStringLiteral("bookId")).toString();
        e.bookPath = o.value(QStringLiteral("bookPath")).toString();
        e.bytes = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble());
        e.lastPlayedAt = static_cast<qint64>(o.value(QStringLiteral("lastPlayedAt")).toDouble());
        e.preferred = o.value(QStringLiteral("preferred")).toBool();
        for (const QJsonValue& v : o.value(QStringLiteral("files")).toArray())
            if (v.isString()) e.files << v.toString();
        if (!e.files.isEmpty() && QFileInfo::exists(e.files.first()))
            m_index.insert(it.key(), e);
    }
}

void AudiobookDownloader::saveIndex() const
{
    QDir().mkpath(baseDir());
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        QJsonObject o;
        o[QStringLiteral("recordingKey")] = it.key();
        o[QStringLiteral("pairKey")] = e.pairKey;
        o[QStringLiteral("dir")] = e.dir;
        o[QStringLiteral("title")] = e.title;
        o[QStringLiteral("author")] = e.author;
        o[QStringLiteral("audiosiloRecordingId")] = e.audiosiloRecordingId;
        o[QStringLiteral("narrators")] = QJsonArray::fromStringList(e.narrators);
        o[QStringLiteral("format")] = e.format;
        o[QStringLiteral("abridgement")] = e.abridgement;
        o[QStringLiteral("language")] = e.language;
        o[QStringLiteral("sourceKind")] = e.sourceKind;
        o[QStringLiteral("sourceRef")] = e.sourceRef;
        o[QStringLiteral("bundle")] = e.bundle;
        o[QStringLiteral("preferred")] = e.preferred;
        o[QStringLiteral("lastPlayedAt")] = static_cast<double>(e.lastPlayedAt);
        o[QStringLiteral("bookId")] = e.bookId;
        o[QStringLiteral("bookPath")] = e.bookPath;
        o[QStringLiteral("bytes")] = static_cast<double>(e.bytes);
        o[QStringLiteral("addedAt")] = static_cast<double>(e.addedAt);
        o[QStringLiteral("files")] = QJsonArray::fromStringList(e.files);
        root.insert(it.key(),o);
    }
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QString AudiobookDownloader::resolveRecordingKey(const QString& key) const
{
    if (key.isEmpty()) return {};
    // A legacy bare pairKey is BOTH an index root key and a work key. Resolve
    // its selection first even when the old item exists alongside newer ones.
    // Explicit recording keys belong to a different (or no) work key.
    if (m_index.contains(key) && m_index.value(key).pairKey != key)
        return key;
    QString only, preferred, recent;
    qint64 latest = 0;
    int count = 0, preferredCount = 0;
    bool recentTie = false;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        if (e.bundle || e.pairKey != key) continue;
        ++count;
        only = it.key();
        if (e.preferred) { ++preferredCount; preferred = it.key(); }
        if (e.lastPlayedAt > latest) {
            latest = e.lastPlayedAt; recent = it.key(); recentTie = false;
        } else if (e.lastPlayedAt && e.lastPlayedAt == latest) recentTie = true;
    }
    if (preferredCount == 1) return preferred;
    if (preferredCount > 1) return {};
    if (latest && !recentTie) return recent;
    return count == 1 ? only : QString();
}

bool AudiobookDownloader::setDefaultRecording(const QString& pairKey,
                                               const QString& recordingKey)
{
    auto selected = m_index.find(recordingKey);
    if (selected == m_index.end() || selected->bundle || selected->pairKey != pairKey)
        return false;
    for (auto it = m_index.begin(); it != m_index.end(); ++it)
        if (it->pairKey == pairKey) it->preferred = it.key() == recordingKey;
    saveIndex();
    return true;
}

bool AudiobookDownloader::markRecordingPlayed(const QString& recordingKey)
{
    const QString resolved = resolveRecordingKey(recordingKey);
    auto it = m_index.find(resolved);
    if (it == m_index.end()) return false;
    it->lastPlayedAt = QDateTime::currentMSecsSinceEpoch();
    saveIndex();
    return true;
}

QVariantList AudiobookDownloader::recordingsForWork(const QString& pairKey) const
{
    QVariantList out;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        if (e.pairKey != pairKey || e.bundle) continue;
        out.append(QVariantMap{
            {QStringLiteral("recordingKey"),it.key()},
            {QStringLiteral("pairKey"),e.pairKey},
            {QStringLiteral("title"),e.title},
            {QStringLiteral("narrators"),e.narrators},
            {QStringLiteral("format"),e.format},
            {QStringLiteral("abridgement"),e.abridgement},
            {QStringLiteral("language"),e.language},
            {QStringLiteral("source"),e.sourceKind},
            {QStringLiteral("sourceKind"),e.sourceKind},
            {QStringLiteral("sourceRef"),e.sourceRef},
            {QStringLiteral("audiosiloRecordingId"),e.audiosiloRecordingId},
            {QStringLiteral("local"),!e.files.isEmpty() && QFileInfo::exists(e.files.first())},
            {QStringLiteral("bundle"),false},
            {QStringLiteral("preferred"),e.preferred},
            {QStringLiteral("lastPlayedAt"),e.lastPlayedAt}
        });
    }
    std::sort(out.begin(),out.end(),[](const QVariant& a,const QVariant& b) {
        return a.toMap().value(QStringLiteral("recordingKey")).toString()
             < b.toMap().value(QStringLiteral("recordingKey")).toString();
    });
    return out;
}

QString AudiobookDownloader::localAudiobook(const QString& key) const
{
    const auto it = m_index.constFind(resolveRecordingKey(key));
    if (it == m_index.constEnd() || it->files.isEmpty()) return {};
    return QFileInfo::exists(it->files.first()) ? it->dir : QString();
}

bool AudiobookDownloader::isDownloaded(const QString& key) const
{
    return !localAudiobook(key).isEmpty();
}

QStringList AudiobookDownloader::localFiles(const QString& key) const
{
    const auto it = m_index.constFind(resolveRecordingKey(key));
    if (it == m_index.constEnd()) return {};
    QStringList out;
    for (const QString& p : it->files)
        if (QFileInfo::exists(p)) out << p;
    return out;
}

QVariantMap AudiobookDownloader::statusOf(const QString& key) const
{
    const QString resolved = resolveRecordingKey(key);
    QVariantMap s;
    if (!resolved.isEmpty() && isDownloaded(resolved)) {
        s[QStringLiteral("state")] = QStringLiteral("done");
        s[QStringLiteral("received")] = static_cast<double>(m_index.value(resolved).bytes);
        s[QStringLiteral("total")] = static_cast<double>(m_index.value(resolved).bytes);
        return s;
    }
    if (m_active && (m_active->recordingKey == key || m_active->signalKey == key)) {
        s[QStringLiteral("state")] = m_active->nativeTorrent ? m_active->nativePhase
            : (m_active->files.isEmpty() ? QStringLiteral("resolving") : QStringLiteral("downloading"));
        s[QStringLiteral("received")] = static_cast<double>(m_active->doneBytes + m_active->fileReceived);
        s[QStringLiteral("total")] = static_cast<double>(m_active->totalBytes);
        return s;
    }
    for (const Job* q : m_queue)
        if (q->recordingKey == key || q->signalKey == key) {
            s[QStringLiteral("state")] = QStringLiteral("queued");
            return s;
        }
    s[QStringLiteral("state")] = QStringLiteral("none");
    return s;
}

QVariantList AudiobookDownloader::activeDownloads() const
{
    QVariantList out;
    const auto describe = [](const Job* j, const QString& state) {
        return QVariantMap{
            {QStringLiteral("id"),j->recordingKey},
            {QStringLiteral("recordingKey"),j->recordingKey},
            {QStringLiteral("pairKey"),j->pairKey},
            {QStringLiteral("title"),j->title},
            {QStringLiteral("author"),j->author},
            {QStringLiteral("state"),state},
            {QStringLiteral("received"),static_cast<double>(j->doneBytes + j->fileReceived)},
            {QStringLiteral("total"),static_cast<double>(j->totalBytes)}
        };
    };
    if (m_active)
        out.append(describe(m_active, m_active->nativeTorrent ? m_active->nativePhase
                  : (m_active->files.isEmpty() ? QStringLiteral("resolving") : QStringLiteral("downloading"))));
    for (const Job* q : m_queue) out.append(describe(q,QStringLiteral("queued")));
    for (const QVariantMap& failure : m_failures) out.append(failure);
    return out;
}

QVariantList AudiobookDownloader::downloadedAudiobooks() const
{
    QVariantList out;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        out.append(QVariantMap{
            {QStringLiteral("id"),it.key()},
            {QStringLiteral("recordingKey"),it.key()},
            {QStringLiteral("pairKey"),e.pairKey},
            {QStringLiteral("title"),e.title},
            {QStringLiteral("author"),e.author},
            {QStringLiteral("dir"),e.dir},
            {QStringLiteral("fileCount"),e.files.size()},
            {QStringLiteral("bytes"),e.bytes},
            {QStringLiteral("addedAt"),e.addedAt},
            {QStringLiteral("bookId"),e.bookId},
            {QStringLiteral("bookPath"),e.bookPath},
            {QStringLiteral("audiosiloRecordingId"),e.audiosiloRecordingId},
            {QStringLiteral("narrators"),e.narrators},
            {QStringLiteral("format"),e.format},
            {QStringLiteral("abridgement"),e.abridgement},
            {QStringLiteral("language"),e.language},
            {QStringLiteral("sourceKind"),e.sourceKind},
            {QStringLiteral("sourceRef"),e.sourceRef},
            {QStringLiteral("bundle"),e.bundle},
            {QStringLiteral("missing"),e.files.isEmpty() || !QFileInfo::exists(e.files.first())}
        });
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// download flow
// ─────────────────────────────────────────────────────────────────────────────

bool AudiobookDownloader::isActive(const QString& recordingKey) const
{
    if (m_active && m_active->recordingKey == recordingKey) return true;
    for (const Job* q : m_queue) if (q->recordingKey == recordingKey) return true;
    return false;
}

AudiobookDownloader::Job* AudiobookDownloader::jobForHash(const QString& infoHash) const
{
    const QString h = infoHash.toLower();
    if (m_active && m_active->infoHash == h) return m_active;
    for (Job* q : m_queue) if (q->infoHash == h) return q;
    return nullptr;
}

void AudiobookDownloader::downloadAudiobook(const QString& pairKey, const QString& infoHash,
                                            const QString& title, const QString& author,
                                            const QString& bookId, const QString& bookPath)
{
    beginDownload({{QStringLiteral("pairKey"),pairKey},
                   {QStringLiteral("infoHash"),infoHash},
                   {QStringLiteral("title"),title},
                   {QStringLiteral("author"),author},
                   {QStringLiteral("bookId"),bookId},
                   {QStringLiteral("bookPath"),bookPath},
                   {QStringLiteral("sourceKind"),QStringLiteral("abb")}},true);
}

void AudiobookDownloader::downloadRecording(const QVariantMap& request)
{
    beginDownload(request,false);
}

void AudiobookDownloader::beginDownload(const QVariantMap& request, bool legacyCaller)
{
    const bool bundle = request.value(QStringLiteral("bundle")).toBool();
    const QString pairKey = bundle ? QString() : request.value(QStringLiteral("pairKey")).toString();
    const QString hash = request.value(QStringLiteral("infoHash")).toString().trimmed().toLower();
    const QString title = request.value(QStringLiteral("title")).toString();
    const QString author = request.value(QStringLiteral("author")).toString();
    QString asid = request.value(QStringLiteral("audiosiloRecordingId")).toString().trimmed();
    const QString suppliedKey = request.value(QStringLiteral("recordingKey")).toString();
    if (!bundle && asid.isEmpty() && suppliedKey.startsWith(pairKey + QStringLiteral("#as:")))
        asid = suppliedKey.mid(pairKey.size() + 4);
    const QString recordingKey = bundle ? QStringLiteral("bundle#src:") + hash
        : (asid.isEmpty() ? pairKey + QStringLiteral("#src:") + hash
                          : pairKey + QStringLiteral("#as:") + asid);
    const QString signalKey = legacyCaller ? pairKey : recordingKey;
    if (!bundle && pairKey.isEmpty()) {
        emit failed(signalKey,QStringLiteral("empty pairKey")); return;
    }
    if (!QRegularExpression(QStringLiteral("^[0-9a-f]{40}$")).match(hash).hasMatch()) {
        const QString reason = QStringLiteral("bad infoHash");
        rememberFailure(recordingKey,title,author,reason);
        emit failed(signalKey,reason);
        return;
    }
    if (!suppliedKey.isEmpty() && suppliedKey != recordingKey) {
        emit failed(signalKey,QStringLiteral("recordingKey conflicts with source identity"));
        return;
    }
    QPointer<AudiobookDownloader> entryGuard(this);
    if (m_failures.remove(recordingKey)) {
        emit failuresChanged();
        if (!entryGuard) return;
    }
    if (!bundle) {
        const QString bookId = request.value(QStringLiteral("bookId")).toString();
        const QString bookPath = request.value(QStringLiteral("bookPath")).toString();
        if (!bookId.isEmpty()) m_bookIdFor.insert(recordingKey,bookId);
        if (!bookPath.isEmpty()) m_bookPathFor.insert(recordingKey,bookPath);
    }
    if (isDownloaded(recordingKey)) {
        const QString dir = localAudiobook(recordingKey);
        attachToBook(recordingKey,dir);
        emit finished(signalKey,dir);
        return;
    }
    if (isActive(recordingKey)) return;
    if (m_sourceEnabledCheck && !m_sourceEnabledCheck()) {
        emit failed(signalKey,QStringLiteral("Enable AudioBookBay in Extensions to download this audiobook."));
        return;
    }
    if (!m_stream && !m_torrent) {
        const QString reason = QStringLiteral("stream engine unavailable");
        rememberFailure(recordingKey,title,author,reason);
        emit failed(signalKey,reason);
        return;
    }
    Job* job = new Job;
    job->generation = ++m_nextGeneration;
    job->recordingKey = recordingKey;
    job->signalKey = signalKey;
    job->pairKey = pairKey;
    job->infoHash = hash;
    job->title = title;
    job->author = author;
    job->audiosiloRecordingId = asid;
    job->bundle = bundle;
    job->format = request.value(QStringLiteral("format")).toString();
    job->abridgement = request.value(QStringLiteral("abridgement")).toString();
    job->language = request.value(QStringLiteral("language")).toString();
    job->sourceKind = request.value(QStringLiteral("sourceKind")).toString();
    job->sourceRef = request.value(QStringLiteral("sourceRef")).toString();
    const QVariant rawNames = request.value(QStringLiteral("narrators"));
    if (rawNames.canConvert<QStringList>()) job->narrators = rawNames.toStringList();
    m_queue.append(job);
    QPointer<AudiobookDownloader> guard(this);
    emit resolving(signalKey);
    if (!guard) return;
    emit activeCountChanged();
    if (guard) promoteQueue();
}

void AudiobookDownloader::onFetchReady(const QString& url, const QString& infoHash, int /*fileIdx*/)
{
    Job* job = jobForHash(infoHash);
    if (!job || job != m_active || job->nativeTorrent) return;
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
        failJob(job, QStringLiteral("Audiobook engine did not provide a stream route in time."));
        return;
    }
    const quint64 generation = job->generation;
    QTimer::singleShot(1000, this, [this, generation]() {
        if (m_active && m_active->generation == generation) pollEngine(m_active);
    });
}

void AudiobookDownloader::requestManifest(Job* job)
{
    job->createAttempts += 1;
    QNetworkRequest req(QUrl(job->baseUrl + QStringLiteral("/create")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(30000);   // a cold engine can hang /create — time out → retry (poll loop)
    QNetworkReply* reply = m_nam->post(req, QByteArray("{}"));
    job->reply = reply;
    const quint64 generation = job->generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        if (!m_active || m_active->generation != generation || m_active->reply != reply) {
            reply->deleteLater(); return;
        }
        m_active->reply.clear();
        onManifestReply(reply, m_active);
    });
}

void AudiobookDownloader::onManifestReply(QNetworkReply* reply, Job* job)
{
    const auto error = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = error == QNetworkReply::NoError ? reply->readAll() : QByteArray();
    reply->deleteLater();
    if (status >= 400) {
        failJob(job, QStringLiteral("Audiobook manifest request failed (HTTP %1).").arg(status));
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(body, &parseError);
    if (error == QNetworkReply::NoError && (parseError.error != QJsonParseError::NoError
        || !document.isObject() || !document.object().value(QStringLiteral("files")).isArray())) {
        failJob(job, QStringLiteral("Audiobook engine returned an invalid file manifest.")); return;
    }
    const int totalFiles = document.object().value(QStringLiteral("files")).toArray().size();
    QList<FileJob> audio = error == QNetworkReply::NoError ? parseManifest(body) : QList<FileJob>();
    if (audio.isEmpty()) {
        if (totalFiles > 0) {
            failJob(job, QStringLiteral("this torrent has no audio files (%1 non-audio file(s))").arg(totalFiles)); return;
        }
        job->manifestReason = error != QNetworkReply::NoError
            ? QStringLiteral("Audiobook manifest network request failed (error %1, HTTP %2).").arg(int(error)).arg(status)
            : QStringLiteral("Audiobook torrent metadata was not ready after %1 tries.").arg(job->createAttempts);
        if (job->createAttempts < kMaxManifestPolls) {
            const quint64 generation = job->generation;
            QTimer::singleShot(kManifestPollMs, this, [this, generation]() {
                if (m_active && m_active->generation == generation) requestManifest(m_active);
            }); return;
        }
        failJob(job, job->manifestReason); return;
    }
    qInfo() << "[AudiobookDownloader] manifest:" << totalFiles << "files," << audio.size() << "audio";

    // natural-sort by name, then number for play order + destination filename
    std::sort(audio.begin(), audio.end(), [](const FileJob& a, const FileJob& b) {
        return naturalLess(a.name, b.name);
    });
    const QString dir = dirFor(job->recordingKey);
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
    qInfo() << "[AudiobookDownloader]" << job->recordingKey << "→" << audio.size()
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
    if (!m_active || !m_active->reply || !m_active->file || sender() != m_active->reply.data()) return;
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
        emit progress(job->signalKey, static_cast<double>(job->doneBytes + job->fileReceived),
                      static_cast<double>(job->totalBytes));
    }
}

void AudiobookDownloader::onFileFinished()
{
    if (!m_active || !m_active->reply || sender() != m_active->reply.data()) return;
    Job* job = m_active;
    QNetworkReply* reply = job->reply.data();
    if (!reply) return;
    const QNetworkReply::NetworkError err = reply->error();
    const QString errStr = QStringLiteral("network error %1, HTTP %2").arg(int(err)).arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
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
    const quint64 generation = job->generation;
    const QString key = job->signalKey;
    QPointer<AudiobookDownloader> guard(this);
    emit progress(key, static_cast<double>(job->doneBytes), static_cast<double>(job->totalBytes));
    if (!guard || !m_active || m_active->generation != generation) return;
    job->current += 1;
    startNextFile(job);
}

void AudiobookDownloader::finalizeJob(Job* job)
{
    Entry e;
    e.recordingKey = job->recordingKey;
    e.pairKey = job->pairKey;
    e.bundle = job->bundle;
    e.dir = job->nativeTorrent ? job->completedDir : dirFor(job->recordingKey);
    e.title = job->title;
    e.author = job->author;
    e.audiosiloRecordingId = job->audiosiloRecordingId;
    e.narrators = job->narrators;
    e.format = job->format;
    e.abridgement = job->abridgement;
    e.language = job->language;
    e.sourceKind = job->sourceKind;
    e.sourceRef = job->sourceRef;
    e.bookId = job->bundle ? QString() : m_bookIdFor.value(job->recordingKey);
    e.bookPath = job->bundle ? QString() : m_bookPathFor.value(job->recordingKey);
    e.bytes = job->doneBytes;
    e.addedAt = QDateTime::currentMSecsSinceEpoch();
    for (const FileJob& fj : job->files) e.files << fj.finalPath;
    m_index.insert(job->recordingKey,e);
    saveIndex();

    const QString key = job->recordingKey, signalKey = job->signalKey;
    const QString dir = e.dir;
    qInfo() << "[AudiobookDownloader] complete" << key << "files=" << e.files.size() << "dir=" << dir;
    delete m_active; m_active = nullptr;
    QPointer<AudiobookDownloader> guard(this);
    attachToBook(key,dir);
    if (!guard) return;
    emit finished(signalKey,dir);
    if (!guard) return;
    emit activeCountChanged();
    if (guard) promoteQueue();
}

// A read-along pairing retains the work key but pins the selected recording.
void AudiobookDownloader::attachToBook(const QString& recordingKey, const QString& dirPath)
{
    if (!m_pairing) return;
    const auto it = m_index.constFind(recordingKey);
    if (it == m_index.constEnd() || it->bundle || it->pairKey.isEmpty()) return;
    const QString bookId = m_bookIdFor.value(recordingKey, it->bookId);
    if (bookId.isEmpty()) return;
    QVariantMap pairing{
        {QStringLiteral("pairKey"),it->pairKey},
        {QStringLiteral("recordingKey"),recordingKey},
        {QStringLiteral("dirPath"),dirPath},
        {QStringLiteral("audioDir"),dirPath}
    };
    const QString bookPath = m_bookPathFor.value(recordingKey,it->bookPath);
    if (!bookPath.isEmpty()) pairing.insert(QStringLiteral("bookPath"),bookPath);
    m_pairing->savePairing(bookId,pairing);
    qInfo() << "[AudiobookDownloader] attached" << recordingKey << "→ book" << bookId;
}

void AudiobookDownloader::failJob(Job* job, const QString& reason)
{
    cleanupInFlight(job);
    if (job->nativeTorrent && m_torrent) m_torrent->cancel(job->generation);
    const QString key = job->recordingKey, signalKey = job->signalKey;
    const QString title = job->title, author = job->author;
    const qint64 received = job->doneBytes + job->fileReceived, total = job->totalBytes;
    const QString failure = reason; // reason may alias the retired Job
    if (m_active == job) m_active = nullptr;
    else m_queue.removeAll(job);
    delete job; // retire before rememberFailure's failuresChanged observer
    QPointer<AudiobookDownloader> guard(this);
    rememberFailure(key, title, author, failure, received, total);
    if (!guard) return;
    if (isActive(key)) return; // a failuresChanged observer already retried this recording
    qWarning() << "[AudiobookDownloader] FAILED" << key << failure;
    emit failed(signalKey, failure);
    if (!guard) return;
    emit activeCountChanged();
    if (guard) promoteQueue();
}

void AudiobookDownloader::rememberFailure(const QString& pairKey, const QString& title,
                                          const QString& author, const QString& reason,
                                          qint64 received, qint64 total)
{
    m_failures.insert(pairKey, QVariantMap{
        {QStringLiteral("id"), pairKey},
        {QStringLiteral("title"), title},
        {QStringLiteral("author"), author},
        {QStringLiteral("state"), QStringLiteral("failed")},
        {QStringLiteral("error"), reason.simplified().left(240)},
        {QStringLiteral("received"), static_cast<double>(received)},
        {QStringLiteral("total"), static_cast<double>(total)}
    });
    emit failuresChanged();
}

void AudiobookDownloader::promoteQueue()
{
    if (m_active || m_queue.isEmpty()) return;
    m_active = m_queue.takeFirst();
    const quint64 generation = m_active->generation;
    m_active->nativeTorrent = bool(m_torrent);
    QPointer<AudiobookDownloader> guard(this);
    const QString key = m_active->signalKey;
    emit resolving(key);
    if (!guard || !m_active || m_active->generation != generation) return;
    if (m_active->nativeTorrent) {
        m_torrent->start(generation, m_active->infoHash, dirFor(m_active->recordingKey));
    } else {
        m_stream->prefetch(m_active->infoHash, 0);
        if (guard && m_active && m_active->generation == generation) pollEngine(m_active);
    }
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

void AudiobookDownloader::cancelDownload(const QString& key)
{
    Job* target = nullptr;
    if (m_active && m_active->recordingKey == key) target = m_active;
    for (Job* j : m_queue) if (j->recordingKey == key) target = j;
    if (!target) {
        // A bare work key may only cancel when it identifies one in-flight recording.
        int matches = 0;
        if (m_active && m_active->pairKey == key) { target = m_active; ++matches; }
        for (Job* j : m_queue) if (j->pairKey == key) { target = j; ++matches; }
        if (matches != 1) return;
    }
    cancelJob(target);
}

QVariantMap AudiobookDownloader::deleteAudiobook(const QString& key)
{
    const QString resolved = resolveRecordingKey(key);
    auto it = m_index.find(resolved);
    if (it == m_index.end()) {
        for (const Entry& e : m_index)
            if (e.pairKey == key)
                return DownloadFileOps::toMap({false,QStringLiteral("Choose a recording before deleting this work.")});
        return DownloadFileOps::toMap({true,QString()});
    }
    const auto result = DownloadFileOps::removeTree(it->dir);
    if (!result.success) {
        qWarning() << "[downloads] delete failed" << resolved << result.message;
        return DownloadFileOps::toMap(result);
    }
    m_index.erase(it);
    m_failures.remove(resolved);
    m_bookIdFor.remove(resolved);
    m_bookPathFor.remove(resolved);
    saveIndex();
    emit removed(key);
    return DownloadFileOps::toMap(result);
}

QVariantMap AudiobookDownloader::removeIncompleteDownloads()
{
    if (m_active || !m_queue.isEmpty())
        return DownloadFileOps::toMap({false,QStringLiteral("Audiobook downloads are still stopping.")});
    if (!m_index.isEmpty())
        return DownloadFileOps::toMap({false,QStringLiteral("Some downloaded audiobooks could not be deleted.")});
    return DownloadFileOps::toMap(DownloadFileOps::removeTree(baseDir()));
}

void AudiobookDownloader::cancelJob(Job* job)
{
    if (job->nativeTorrent && m_torrent) {
        m_torrent->cancel(job->generation);
        return;
    }
    retireCancelledJob(job);
}

void AudiobookDownloader::retireCancelledJob(Job* job)
{
    cleanupInFlight(job);
    const QString key = job->recordingKey, signalKey = job->signalKey;
    const bool native = job->nativeTorrent || bool(m_torrent);
    if (m_active == job) m_active = nullptr;
    else m_queue.removeAll(job);
    delete job;
    const auto result = native ? DownloadFileOps::Result{true,{}}
                               : DownloadFileOps::removeTree(dirFor(key));
    QPointer<AudiobookDownloader> guard(this);
    if (!result.success) {
        rememberFailure(key,{},{},result.message);
        if (!guard) return;
        emit failed(signalKey,result.message);
    } else emit removed(signalKey);
    if (!guard) return;
    emit activeCountChanged();
    if (guard) promoteQueue();
}

void AudiobookDownloader::dismissFailure(const QString& key)
{
    QString toRemove = key;
    if (!m_failures.contains(key)) {
        // Preserve the old work-key facade only when exactly one failure matches.
        int count = 0;
        for (auto it = m_failures.constBegin(); it != m_failures.constEnd(); ++it)
            if (it.key().startsWith(key + QStringLiteral("#src:"))
                || it.key().startsWith(key + QStringLiteral("#as:"))) {
                toRemove = it.key();
                ++count;
            }
        if (count != 1) return;
    }
    QPointer<AudiobookDownloader> entryGuard(this);
    if (m_failures.remove(toRemove)) {
        emit failuresChanged();
        if (!entryGuard) return;
    }
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
