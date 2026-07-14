#include "ComicDownloader.h"

#include "ComicDlsParse.h"
#include "torrent/ComicTorrents.h"
#include "torrent/ComicTorrentMagnet.h"

#include <QCollator>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

namespace {

// GetComics fronts with Cloudflare-adjacent checks: a browser UA + the site
// Referer is what the live-proven curl path used (2026-07-04).
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0 Safari/537.36";
constexpr const char* kReferer = "https://getcomics.org/";

constexpr qint64 kDiskSpaceSafetyBytes = 50LL * 1024 * 1024;
constexpr int    kProgressThrottleMs    = 500;
constexpr qint64 kProgressThrottleBytes = 512LL * 1024;
constexpr int    kMaxAttempts           = 3;   // per URL, 2/4/8s backoff

int attemptDelayMs(int attempt)
{
    switch (attempt) {
    case 0:  return 0;
    case 1:  return 2000;
    case 2:  return 4000;
    default: return 8000;
    }
}

// filesystem-safe path segment (MangaDownloader's convention)
QString safeSeg(const QString& v)
{
    QString out;
    out.reserve(v.size());
    for (const QChar c : v) {
        if (c.isLetterOrNumber() || c == QChar('.') || c == QChar('_') || c == QChar('-')
            || c == QChar(' '))
            out.append(c);
        else
            out.append(QChar('_'));
    }
    out = out.trimmed();
    while (out.endsWith(QChar('.'))) out.chop(1);
    if (out.isEmpty()) out = QStringLiteral("item");
    return out.left(80);
}

QString hash10(const QString& v)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(v.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
}

bool isImageFile(const QString& name)
{
    static const QSet<QString> kExts = { QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("gif"),
        QStringLiteral("avif"), QStringLiteral("bmp") };
    return kExts.contains(QFileInfo(name).suffix().toLower());
}

bool looksLikeHtml(const QByteArray& firstChunk, const QString& contentType)
{
    if (contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive)) return true;
    const QByteArray head = firstChunk.left(512).trimmed().toLower();
    return head.startsWith("<!doctype") || head.startsWith("<html");
}

QString sevenZipPath()
{
    const QString p = QStringLiteral("C:/Program Files/7-Zip/7z.exe");
    return QFileInfo::exists(p) ? p : QString();
}

QString bsdtarPath()
{
    const QString sys = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(sys)) return sys;
    return QStandardPaths::findExecutable(QStringLiteral("tar"));
}

} // namespace

ComicDownloader::ComicDownloader(QNetworkAccessManager* nam, QObject* parent)
    : ComicDownloader(nam, nullptr, nullptr, parent)
{
}

ComicDownloader::ComicDownloader(QNetworkAccessManager* nam, QNetworkAccessManager* searchNam,
                                 TorrentEngine* torrentEngine, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    loadIndex();
    if (searchNam && torrentEngine) {
        m_torrents = new ComicTorrents(searchNam, torrentEngine, this);
        connect(m_torrents, &ComicTorrents::progress, this, &ComicDownloader::progress);
        connect(m_torrents, &ComicTorrents::failed, this, &ComicDownloader::failed);
        connect(m_torrents, &ComicTorrents::archiveReady, this,
                [this](const QString& issueId, const QString& seriesId,
                       const QString& seriesTitle, const QString& issueLabel,
                       const QString& archivePath) {
            ingestLocalArchive(issueId, seriesId, seriesTitle, issueLabel, archivePath);
        });
        connect(m_torrents, &ComicTorrents::sourcesUpdated,
                this, &ComicDownloader::torrentSourcesUpdated);
        connect(m_torrents, &ComicTorrents::sourceSearchFailed,
                this, &ComicDownloader::torrentSourceSearchFailed);
    }
}

ComicDownloader::~ComicDownloader()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
    if (m_active) {
        closeAndDeletePart(*m_active);
        cleanupExtract(*m_active);
        delete m_active;
        m_active = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// disk + index
// ─────────────────────────────────────────────────────────────────────────────

QString ComicDownloader::baseDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/comics");
}

QString ComicDownloader::issueDir(const QString& seriesId, const QString& label,
                                  const QString& id) const
{
    return baseDir() + QChar('/') + safeSeg(seriesId) + QChar('/')
           + safeSeg(label) + QChar('-') + hash10(id);
}

void ComicDownloader::loadIndex()
{
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.seriesId    = o.value(QStringLiteral("seriesId")).toString();
        e.seriesTitle = o.value(QStringLiteral("seriesTitle")).toString();
        e.label       = o.value(QStringLiteral("label")).toString();
        e.dir         = o.value(QStringLiteral("dir")).toString();
        e.bytes       = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt     = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble());
        for (const QJsonValue& v : o.value(QStringLiteral("files")).toArray())
            e.files.append(v.toString());
        // Drop stale entries whose dir was deleted outside the app.
        if (!e.dir.isEmpty() && QDir(e.dir).exists() && !e.files.isEmpty())
            m_index.insert(it.key(), e);
    }
}

void ComicDownloader::saveIndex() const
{
    QDir().mkpath(baseDir());
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("seriesId")]    = it.value().seriesId;
        o[QStringLiteral("seriesTitle")] = it.value().seriesTitle;
        o[QStringLiteral("label")]       = it.value().label;
        o[QStringLiteral("dir")]         = it.value().dir;
        o[QStringLiteral("bytes")]       = static_cast<double>(it.value().bytes);
        o[QStringLiteral("addedAt")]     = static_cast<double>(it.value().addedAt);
        QJsonArray files;
        for (const QString& n : it.value().files) files.append(n);
        o[QStringLiteral("files")] = files;
        root[it.key()] = o;
    }
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ─────────────────────────────────────────────────────────────────────────────
// QML entry points
// ─────────────────────────────────────────────────────────────────────────────

QVariantList ComicDownloader::localPages(const QString& issueId) const
{
    QVariantList out;
    auto it = m_index.constFind(issueId.trimmed());
    if (it == m_index.constEnd()) return out;
    const QDir dir(it.value().dir);
    if (!dir.exists()) return out;
    int idx = 0;
    for (const QString& name : it.value().files) {
        const QString abs = dir.absoluteFilePath(name);
        if (!QFileInfo::exists(abs)) continue;
        QVariantMap m;
        m[QStringLiteral("index")] = idx++;
        m[QStringLiteral("url")]   = QUrl::fromLocalFile(abs).toString();
        m[QStringLiteral("group")] = -1;
        out.append(m);
    }
    return out;
}

bool ComicDownloader::isDownloaded(const QString& issueId) const
{
    auto it = m_index.constFind(issueId.trimmed());
    return it != m_index.constEnd() && QDir(it.value().dir).exists();
}

void ComicDownloader::ingestLocalArchive(const QString& issueIdIn, const QString& seriesId,
                                          const QString& seriesTitle, const QString& issueLabel,
                                          const QString& archivePathIn)
{
    const QString id = issueIdIn.trimmed();
    const QString archivePath = QDir::cleanPath(archivePathIn);
    const QFileInfo archive(archivePath);
    static const QSet<QString> allowed{
        QStringLiteral("cbr"), QStringLiteral("cbz"),
        QStringLiteral("cb7"), QStringLiteral("cbt")
    };
    if (id.isEmpty()) {
        emit failed(id, QStringLiteral("empty issue id"));
        return;
    }
    if (!archive.isFile() || !allowed.contains(archive.suffix().toLower())) {
        emit failed(id, QStringLiteral("local comic archive missing or unsupported"));
        return;
    }
    if (isDownloaded(id)) {
        QFile::remove(archivePath);
        emit finished(id);
        return;
    }
    if (m_active && m_active->id == id) return;
    for (const InFlight& queued : m_queue)
        if (queued.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;

    InFlight flight;
    flight.id = id;
    flight.seriesId = seriesId;
    flight.seriesTitle = seriesTitle;
    flight.label = issueLabel.isEmpty() ? id : issueLabel;
    flight.archivePath = archive.absoluteFilePath();
    flight.receivedBytes = archive.size();
    flight.expectedBytes = archive.size();
    flight.localArchive = true;

    emit progress(id, static_cast<double>(flight.receivedBytes),
                  static_cast<double>(flight.expectedBytes));
    if (m_active) {
        m_queue.append(std::move(flight));
        return;
    }
    m_active = new InFlight(std::move(flight));
    beginExtract(*m_active);
}

QVariantMap ComicDownloader::statusOf(const QString& issueId) const
{
    const QString id = issueId.trimmed();
    QVariantMap s;
    s[QStringLiteral("done")]  = 0.0;
    s[QStringLiteral("total")] = 0.0;
    if (isDownloaded(id)) {
        s[QStringLiteral("state")] = QStringLiteral("done");
        s[QStringLiteral("done")]  = static_cast<double>(m_index.value(id).bytes);
        s[QStringLiteral("total")] = static_cast<double>(m_index.value(id).bytes);
        return s;
    }
    if (m_active && m_active->id == id) {
        s[QStringLiteral("state")] = m_active->extracting ? QStringLiteral("extracting")
                                                          : QStringLiteral("downloading");
        s[QStringLiteral("done")]  = static_cast<double>(m_active->receivedBytes);
        s[QStringLiteral("total")] = static_cast<double>(
            m_active->extracting ? m_active->receivedBytes : m_active->expectedBytes);
        return s;
    }
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) { s[QStringLiteral("state")] = QStringLiteral("resolving"); return s; }
    for (const InFlight& q : m_queue)
        if (q.id == id) { s[QStringLiteral("state")] = QStringLiteral("queued"); return s; }
    if (m_torrents) {
        const QVariantMap torrent = m_torrents->statusOf(id);
        if (torrent.value(QStringLiteral("state")).toString() != QStringLiteral("none"))
            return torrent;
    }
    s[QStringLiteral("state")] = QStringLiteral("none");
    return s;
}

void ComicDownloader::downloadIssueTorrent(const QString& issueIdIn, const QString& seriesId,
                                            const QString& seriesTitle, const QString& issueLabel,
                                            const QString& query)
{
    const QString id = issueIdIn.trimmed();
    if (id.isEmpty() || query.trimmed().isEmpty()) {
        emit failed(id, QStringLiteral("empty issue id / torrent query"));
        return;
    }
    if (isDownloaded(id)) {
        emit finished(id);
        return;
    }
    if (!m_torrents) {
        emit failed(id, QStringLiteral("comic torrent service unavailable"));
        return;
    }
    if (m_torrents->contains(id)) return;
    if (m_active && m_active->id == id) return;
    for (const InFlight& queued : m_queue)
        if (queued.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;
    m_torrents->downloadIssue(id, seriesId, seriesTitle, issueLabel, query);
}

void ComicDownloader::searchTorrentSources(const QString& issueIdIn, const QString& seriesTitle,
                                           const QString& editionTitle, const QString& isbn,
                                           const QString& collects)
{
    const QString id = issueIdIn.trimmed();
    if (!m_torrents) {
        emit torrentSourceSearchFailed(id, QStringLiteral("comic torrent service unavailable"));
        return;
    }
    m_torrents->searchSources(id, seriesTitle, editionTitle, isbn, collects);
}

void ComicDownloader::searchTorrentSourcesQuery(const QString& issueIdIn, const QString& query)
{
    const QString id = issueIdIn.trimmed();
    if (!m_torrents) {
        emit torrentSourceSearchFailed(id, QStringLiteral("comic torrent service unavailable"));
        return;
    }
    m_torrents->searchSourcesQuery(id, query);
}

void ComicDownloader::cancelTorrentSourceSearch(const QString& issueIdIn)
{
    if (m_torrents) m_torrents->cancelSourceSearch(issueIdIn.trimmed());
}

void ComicDownloader::downloadTorrentSource(const QString& issueIdIn, const QString& seriesId,
                                            const QString& seriesTitle, const QString& issueLabel,
                                            const QString& infoHash, const QString& releaseTitle,
                                            const QString& magnetUri)
{
    const QString id = issueIdIn.trimmed();
    if (id.isEmpty() || infoHash.trimmed().isEmpty()) {
        emit failed(id, QStringLiteral("empty issue id / infoHash"));
        return;
    }
    if (isDownloaded(id)) { emit finished(id); return; }
    if (!m_torrents) {
        emit failed(id, QStringLiteral("comic torrent service unavailable"));
        return;
    }
    if (m_torrents->contains(id)) return;
    if (m_active && m_active->id == id) return;
    for (const InFlight& queued : m_queue)
        if (queued.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;
    // The user has chosen a source — stop browsing and acquire it. The canonical
    // edition title (issueLabel) is the archive picker's matching title; the
    // chosen torrent's releaseTitle is diagnostic-only and never becomes it.
    m_torrents->cancelSourceSearch(id);
    qInfo() << "[ComicDownloader] torrent source chosen" << id << "release=" << releaseTitle;
    m_torrents->downloadInfoHash(id, seriesId, seriesTitle, issueLabel, infoHash,
                                 /*pickerTitle=*/issueLabel, magnetUri);
}

void ComicDownloader::downloadIssue(const QString& issueIdIn, const QString& postUrl,
                                    const QString& seriesId, const QString& seriesTitle,
                                    const QString& issueLabel, double expectedBytes)
{
    const QString id = issueIdIn.trimmed();
    if (id.isEmpty() || postUrl.isEmpty()) { emit failed(id, QStringLiteral("empty issue id / post url")); return; }
    if (isDownloaded(id)) { emit finished(id); return; }
    if (m_active && m_active->id == id) return;
    for (const InFlight& q : m_queue) if (q.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;

    // Resolve the FULL signed DOWNLOAD NOW href from the release post.
    QNetworkRequest req{QUrl(postUrl)};
    req.setRawHeader("User-Agent", kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    InFlight f;
    f.id            = id;
    f.postUrl       = postUrl;
    f.seriesId      = seriesId;
    f.seriesTitle   = seriesTitle;
    f.label         = issueLabel.isEmpty() ? id : issueLabel;
    f.expectedBytes = static_cast<qint64>(expectedBytes);
    m_resolving.insert(reply, f);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onResolveFinished(reply); });
}

void ComicDownloader::onResolveFinished(QNetworkReply* reply)
{
    if (!reply) return;
    InFlight f = m_resolving.take(reply);
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError err = reply->error();
    const QString errStr = reply->errorString();
    reply->deleteLater();

    if (f.id.isEmpty()) return;   // cancelled

    if (err != QNetworkReply::NoError) {
        emit failed(f.id, QStringLiteral("release post fetch failed: %1").arg(errStr));
        return;
    }
    f.urls = parsePostHtml(body);
    if (f.urls.isEmpty()) {
        // mirror-only post (only pixeldrain, which is dropped ISP-side): no usable source.
        // "no-source" prefix = TERMINAL — the UI must not offer an unwinnable retry.
        emit failed(f.id, QStringLiteral("no-source | no direct download link on this release"));
        return;
    }
    qInfo() << "[ComicDownloader] resolved" << f.urls.size() << "link(s) for" << f.id << f.label;
    startDownload(std::move(f));
}

QStringList ComicDownloader::parsePostHtml(const QByteArray& html) const
{
    // extracted to a free function (ComicDlsParse) so the contract is testable
    return parseDlsLinks(html);
}

void ComicDownloader::cancelDownload(const QString& issueIdIn)
{
    const QString id = issueIdIn.trimmed();
    for (auto it = m_resolving.begin(); it != m_resolving.end(); ++it) {
        if (it.value().id == id) {
            QNetworkReply* r = it.key();
            m_resolving.erase(it);
            if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
            emit failed(id, QStringLiteral("cancelled by user"));
            return;
        }
    }
    if (m_active && m_active->id == id) {
        if (m_active->extracting && m_proc) {
            m_proc->disconnect(this);
            m_proc->kill();
            m_proc->waitForFinished(1000);
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        failAndCleanup(*m_active, QStringLiteral("cancelled by user"));
        return;
    }
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].id == id) {
            if (m_queue[i].localArchive && !m_queue[i].archivePath.isEmpty())
                QFile::remove(m_queue[i].archivePath);
            m_queue.removeAt(i);
            emit failed(id, QStringLiteral("cancelled by user (queued)"));
            return;
        }
    }
    if (m_torrents) m_torrents->cancel(id);
}

void ComicDownloader::deleteIssue(const QString& issueIdIn)
{
    const QString id = issueIdIn.trimmed();
    auto it = m_index.find(id);
    if (it == m_index.end()) return;
    QDir(it.value().dir).removeRecursively();
    m_index.erase(it);
    saveIndex();
    emit removed(id);
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP streaming download (BookDownloader lineage)
// ─────────────────────────────────────────────────────────────────────────────

void ComicDownloader::startDownload(InFlight&& f)
{
    if (m_active) {
        m_queue.append(std::move(f));
        return;
    }
    m_active = new InFlight(std::move(f));
    startAttempt(*m_active);
}

void ComicDownloader::startAttempt(InFlight& f)
{
    if (f.urlIdx >= f.urls.size()) {
        failAndCleanup(f, QStringLiteral("no-source | all download links exhausted"));
        return;
    }
    const QString url = f.urls.value(f.urlIdx);
    if (url.isEmpty()) { startNextUrlOrFail(f); return; }

    // Disk-space pre-check: archive + extracted pages live together briefly,
    // so budget ~2.5× the archive when the post told us the size.
    if (f.expectedBytes > 0) {
        const QStorageInfo storage(baseDir());
        if (storage.isValid() && storage.isReady()
            && storage.bytesAvailable() < f.expectedBytes * 5 / 2 + kDiskSpaceSafetyBytes) {
            failAndCleanup(f, QStringLiteral("insufficient disk space for download + extract"));
            return;
        }
    }

    const int delay = attemptDelayMs(f.attempt);
    if (delay <= 0) {
        QDir().mkpath(baseDir());
        f.archivePath = baseDir() + QStringLiteral("/dl_") + hash10(f.id) + QStringLiteral(".archive");
        f.partPath    = f.archivePath + QStringLiteral(".part");
        f.file = new QFile(f.partPath);
        if (!f.file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString err = f.file->errorString();
            delete f.file; f.file = nullptr;
            failAndCleanup(f, QStringLiteral("cannot open .part file: %1").arg(err));
            return;
        }
        f.receivedBytes = 0; f.sanityChecked = false;
        f.lastProgressEmit = 0; f.lastProgressBytes = 0;

        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("User-Agent", kUserAgent);
        req.setRawHeader("Referer", kReferer);
        req.setRawHeader("Accept", "*/*");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = m_nam->get(req);
        f.reply = reply;
        connect(reply, &QNetworkReply::readyRead,        this, &ComicDownloader::onReadyRead);
        connect(reply, &QNetworkReply::finished,         this, &ComicDownloader::onFinished);
        connect(reply, &QNetworkReply::downloadProgress, this, &ComicDownloader::onProgressFromReply);
        // Mirror rotation discipline: if the signed link redirects to a host we
        // KNOW is blocked from this ISP (pixeldrain, probed dead 2026-07-10),
        // abort and advance to the next candidate immediately — never sit out
        // the socket timeout on a dead host.
        connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl& to) {
            if (!m_active || m_active->reply.data() != reply) return;
            if (to.host().contains(QStringLiteral("pixeldrain"), Qt::CaseInsensitive)) {
                qInfo() << "[ComicDownloader] redirect to blocked host" << to.host() << "— skipping mirror";
                m_active->redirectBlocked = true;
                reply->abort();
            }
        });
    } else {
        const QString id = f.id;
        QTimer::singleShot(delay, this, [this, id]() {
            if (!m_active || m_active->id != id) return;
            m_active->attempt = 0;   // collapse to the immediate-issue branch
            startAttempt(*m_active);
        });
    }
}

void ComicDownloader::onReadyRead()
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    QNetworkReply* reply = f.reply.data();
    if (!reply) return;

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty()) return;

    if (!f.sanityChecked) {
        f.sanityChecked = true;
        const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (looksLikeHtml(chunk, ct)) {
            qWarning() << "[ComicDownloader] got HTML (ad-gate/interstitial) from"
                       << f.urls.value(f.urlIdx) << "— failing over";
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
            f.reply.clear();
            if (f.file) { f.file->close(); f.file->remove(); delete f.file; f.file = nullptr; }
            startNextUrlOrFail(f);
            return;
        }
    }

    if (f.file) {
        const qint64 written = f.file->write(chunk);
        if (written < 0) { failAndCleanup(f, QStringLiteral("disk write failed: %1").arg(f.file->errorString())); return; }
        f.receivedBytes += written;
    }
}

void ComicDownloader::onProgressFromReply(qint64 received, qint64 total)
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    if (total > 0) f.expectedBytes = total;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsedMs = (f.lastProgressEmit == 0) ? (kProgressThrottleMs + 1)
                                                       : (nowMs - f.lastProgressEmit);
    const qint64 deltaBytes = received - f.lastProgressBytes;
    if (elapsedMs >= kProgressThrottleMs || deltaBytes >= kProgressThrottleBytes) {
        f.lastProgressEmit = nowMs;
        f.lastProgressBytes = received;
        emit progress(f.id, static_cast<double>(received), static_cast<double>(total));
    }
}

void ComicDownloader::onFinished()
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    QNetworkReply* reply = f.reply.data();
    if (!reply) return;

    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (err == QNetworkReply::NoError) {
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty() && f.file) { f.file->write(tail); f.receivedBytes += tail.size(); }
    }
    reply->deleteLater();
    f.reply.clear();

    if (err != QNetworkReply::NoError) {
        qWarning() << "[ComicDownloader] reply error" << err << "http=" << httpStatus << errString;
        if (f.redirectBlocked) {                       // deliberate abort — skip this URL, don't retry it
            f.redirectBlocked = false;
            closeAndDeletePart(f);
            startNextUrlOrFail(f);
            return;
        }
        retryOrFailover(f, QStringLiteral("HTTP error: %1 (status %2)").arg(errString).arg(httpStatus));
        return;
    }
    if (f.file) { f.file->close(); delete f.file; f.file = nullptr; }
    if (f.receivedBytes <= 0) {
        QFile::remove(f.partPath);
        failAndCleanup(f, QStringLiteral("server returned empty body"));
        return;
    }
    if (QFile::exists(f.archivePath)) QFile::remove(f.archivePath);
    if (!QFile::rename(f.partPath, f.archivePath)) {
        QFile::remove(f.partPath);
        failAndCleanup(f, QStringLiteral("archive rename failed"));
        return;
    }
    emit progress(f.id, static_cast<double>(f.receivedBytes), static_cast<double>(f.receivedBytes));
    qInfo() << "[ComicDownloader] archive complete id=" << f.id << "bytes=" << f.receivedBytes
            << "— extracting";
    beginExtract(f);
}

void ComicDownloader::retryOrFailover(InFlight& f, const QString& reason)
{
    closeAndDeletePart(f);
    f.attempt += 1;
    if (f.attempt < kMaxAttempts) { startAttempt(f); return; }
    qInfo() << "[ComicDownloader] link exhausted, failover:" << reason;
    startNextUrlOrFail(f);
}

void ComicDownloader::startNextUrlOrFail(InFlight& f)
{
    f.urlIdx += 1;
    f.attempt = 0;
    // Every resolved mirror failed (CF-blocked / HTML-gated / offline) — no usable source.
    // "no-source" prefix = TERMINAL: JLU #1 (2024) lands here because its only comicfiles
    // mirror sits behind a CF managed challenge and its MEGA mirror isn't a direct-HTTP host.
    if (f.urlIdx >= f.urls.size()) {
        failAndCleanup(f, QStringLiteral("no-source | all mirrors unavailable (blocked or offline)"));
        return;
    }
    startAttempt(f);
}

void ComicDownloader::failAndCleanup(InFlight& f, const QString& reason)
{
    closeAndDeletePart(f);
    cleanupExtract(f);
    const QString id = f.id;
    emit failed(id, reason);
    delete m_active; m_active = nullptr;
    startNextQueued();
}

void ComicDownloader::closeAndDeletePart(InFlight& f)
{
    if (f.reply) {
        QNetworkReply* r = f.reply.data();
        if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
        f.reply.clear();
    }
    if (f.file) {
        f.file->close();
        const QString path = f.file->fileName();
        delete f.file; f.file = nullptr;
        QFile::remove(path);
    } else if (!f.partPath.isEmpty() && QFile::exists(f.partPath)) {
        QFile::remove(f.partPath);
    }
    if (!f.archivePath.isEmpty() && QFile::exists(f.archivePath))
        QFile::remove(f.archivePath);
}

void ComicDownloader::startNextQueued()
{
    if (!m_queue.isEmpty()) {
        m_active = new InFlight(std::move(m_queue[0]));
        m_queue.removeAt(0);
        if (m_active->localArchive)
            beginExtract(*m_active);
        else
            startAttempt(*m_active);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// extraction: OS bsdtar (reads RAR + zip) → 7-Zip fallback → pages dir
// ─────────────────────────────────────────────────────────────────────────────

void ComicDownloader::beginExtract(InFlight& f)
{
    f.extracting = true;
    f.extractTmp = f.archivePath + QStringLiteral(".x");
    QDir(f.extractTmp).removeRecursively();
    if (!QDir().mkpath(f.extractTmp)) {
        failAndCleanup(f, QStringLiteral("cannot create extract dir"));
        return;
    }
    runExtractor(f, 0);
}

void ComicDownloader::runExtractor(InFlight& f, int which)
{
    QString exe;
    QStringList args;
    if (which == 0) {
        exe = bsdtarPath();
        args = { QStringLiteral("-xf"), QDir::toNativeSeparators(f.archivePath),
                 QStringLiteral("-C"), QDir::toNativeSeparators(f.extractTmp) };
    } else {
        exe = sevenZipPath();
        args = { QStringLiteral("x"), QStringLiteral("-y"),
                 QStringLiteral("-o") + QDir::toNativeSeparators(f.extractTmp),
                 QDir::toNativeSeparators(f.archivePath) };
    }
    if (exe.isEmpty()) {
        if (which == 0) { runExtractor(f, 1); return; }
        failAndCleanup(f, QStringLiteral("no archive extractor available (tar/7z)"));
        return;
    }
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
    m_proc = new QProcess(this);
    m_proc->setProgram(exe);
    m_proc->setArguments(args);
    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, which](int code, QProcess::ExitStatus) { onExtractDone(code, which); });
    qInfo() << "[ComicDownloader] extracting with" << exe;
    m_proc->start();
}

void ComicDownloader::onExtractDone(int exitCode, int which)
{
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
    if (!m_active || !m_active->extracting) return;
    InFlight& f = *m_active;
    if (exitCode != 0) {
        qWarning() << "[ComicDownloader] extractor" << which << "exit" << exitCode;
        if (which == 0 && !sevenZipPath().isEmpty()) {
            QDir(f.extractTmp).removeRecursively();
            QDir().mkpath(f.extractTmp);
            runExtractor(f, 1);
            return;
        }
        failAndCleanup(f, QStringLiteral("archive extraction failed (not a cbr/cbz?)"));
        return;
    }
    finalizeExtract(f);
}

void ComicDownloader::finalizeExtract(InFlight& f)
{
    // Collect images (recursive — many archives nest one folder), natural-sorted
    // by relative path so "…-0002" follows "…-0001" and 10 follows 9.
    QStringList rel;
    QDirIterator it(f.extractTmp, QDir::Files, QDirIterator::Subdirectories);
    const int prefixLen = f.extractTmp.length() + 1;
    while (it.hasNext()) {
        const QString abs = it.next();
        if (isImageFile(abs)) rel.append(abs.mid(prefixLen));
    }
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(rel.begin(), rel.end(), [&coll](const QString& a, const QString& b) {
        return coll.compare(a, b) < 0;
    });
    if (rel.isEmpty()) {
        failAndCleanup(f, QStringLiteral("archive contained no pages"));
        return;
    }

    const QString dirPath = issueDir(f.seriesId, f.label, f.id);
    QDir(dirPath).removeRecursively();
    if (!QDir().mkpath(dirPath)) {
        failAndCleanup(f, QStringLiteral("cannot create pages dir"));
        return;
    }
    QStringList files;
    for (int i = 0; i < rel.size(); ++i) {
        const QString src = f.extractTmp + QChar('/') + rel[i];
        const QString ext = QFileInfo(rel[i]).suffix().toLower();
        const QString name = QStringLiteral("page_%1.%2")
                                 .arg(i, 3, 10, QChar('0')).arg(ext);
        if (!QFile::rename(src, dirPath + QChar('/') + name)) {
            failAndCleanup(f, QStringLiteral("failed placing page %1").arg(i));
            return;
        }
        files.append(name);
    }
    cleanupExtract(f);
    QFile::remove(f.archivePath);

    Entry e;
    e.seriesId    = f.seriesId;
    e.seriesTitle = f.seriesTitle;
    e.label       = f.label;
    e.dir         = dirPath;
    e.files       = files;
    e.bytes       = f.receivedBytes;
    e.addedAt     = QDateTime::currentMSecsSinceEpoch();
    m_index.insert(f.id, e);
    saveIndex();

    const QString id = f.id;
    qInfo() << "[ComicDownloader] complete id=" << id << "pages=" << files.size()
            << "dir=" << dirPath;
    emit finished(id);

    delete m_active; m_active = nullptr;
    startNextQueued();
}

void ComicDownloader::cleanupExtract(InFlight& f)
{
    if (!f.extractTmp.isEmpty()) QDir(f.extractTmp).removeRecursively();
    f.extracting = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// dev smoke
// ─────────────────────────────────────────────────────────────────────────────

void ComicDownloader::selfTest(const QString& postUrl)
{
    const QString id = QStringLiteral("selftest-") + hash10(postUrl);
    qInfo() << "[ComicDownloader] selfTest resolving + downloading" << postUrl << "id=" << id;
    connect(this, &ComicDownloader::finished, this, [this](const QString& i) {
        qInfo() << "[ComicDownloader] selfTest OK id=" << i
                << "pages=" << localPages(i).size();
    });
    connect(this, &ComicDownloader::failed, this, [](const QString& i, const QString& why) {
        qWarning() << "[ComicDownloader] selfTest FAILED id=" << i << "reason=" << why;
    });
    downloadIssue(id, postUrl, QStringLiteral("gc:selftest"), QStringLiteral("selftest"),
                  QStringLiteral("selftest"), 0);
}

void ComicDownloader::selfTestTorrent(const QString& magnetOrHash, const QString& seriesTitle,
                                      const QString& issueLabel)
{
    const QString infoHash = ComicTorrentMagnet::infoHash(magnetOrHash);
    const QString id = QStringLiteral("torrent-selftest-") + hash10(infoHash + issueLabel);
    if (!m_torrents) {
        qWarning() << "[comic-torrent-dl] FAIL service unavailable";
        QCoreApplication::exit(1);
        return;
    }
    deleteIssue(id);
    connect(this, &ComicDownloader::finished, this, [this, id](const QString& finishedId) {
        if (finishedId != id) return;
        const int pages = localPages(id).size();
        if (pages <= 0) {
            qWarning() << "[comic-torrent-dl] FAIL no reader pages" << id;
            QCoreApplication::exit(1);
            return;
        }
        qInfo() << "[comic-torrent-dl] DONE" << id << "pages=" << pages;
        QCoreApplication::exit(0);
    });
    connect(this, &ComicDownloader::failed, this, [id](const QString& failedId,
                                                        const QString& reason) {
        if (failedId != id) return;
        qWarning() << "[comic-torrent-dl] FAIL" << id << reason;
        QCoreApplication::exit(1);
    });
    m_torrents->downloadInfoHash(id, QStringLiteral("gc:torrent-selftest"),
                                 seriesTitle, issueLabel, infoHash,
                                 seriesTitle + QLatin1Char(' ') + issueLabel,
                                 magnetOrHash.startsWith(QStringLiteral("magnet:?"))
                                     ? magnetOrHash : QString());
}

QVariantList ComicDownloader::downloadedIssues() const
{
    QVariantList out;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        const bool missing = e.files.isEmpty()
            || !QFile::exists(e.dir + QStringLiteral("/") + e.files.first());
        out.append(QVariantMap{
            {QStringLiteral("id"), it.key()},
            {QStringLiteral("seriesId"), e.seriesId},
            {QStringLiteral("seriesTitle"), e.seriesTitle},
            {QStringLiteral("label"), e.label},
            {QStringLiteral("pages"), e.files.size()},
            {QStringLiteral("bytes"), e.bytes},
            {QStringLiteral("addedAt"), e.addedAt},
            {QStringLiteral("missing"), missing}
        });
    }
    return out;
}

QVariantList ComicDownloader::activeIssueJobs() const
{
    QVariantList out;
    auto row = [](const InFlight& f, const QString& state) {
        return QVariantMap{
            {QStringLiteral("id"), f.id},
            {QStringLiteral("seriesId"), f.seriesId},
            {QStringLiteral("seriesTitle"), f.seriesTitle},
            {QStringLiteral("label"), f.label},
            {QStringLiteral("state"), state},
            {QStringLiteral("done"), double(f.receivedBytes)},
            {QStringLiteral("total"), double(f.expectedBytes)}
        };
    };
    if (m_active)
        out.append(row(*m_active, m_active->extracting
                       ? QStringLiteral("extracting") : QStringLiteral("downloading")));
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        out.append(row(it.value(), QStringLiteral("resolving")));
    for (const InFlight& q : m_queue)
        out.append(row(q, QStringLiteral("queued")));
    if (m_torrents) {
        const QVariantList torrentJobs = m_torrents->activeJobs();
        for (const QVariant& job : torrentJobs) out.append(job);
    }
    return out;
}
