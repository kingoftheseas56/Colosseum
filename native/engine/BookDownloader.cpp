#include "BookDownloader.h"

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
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

namespace {

constexpr const char* kLibGenBase = "https://libgen.li";

// Match LibGen's UA — some CDNs (cdn2.booksdl.lc) flag bare Qt / curl defaults.
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

constexpr qint64 kDiskSpaceSafetyBytes = 50LL * 1024 * 1024;
constexpr int    kProgressThrottleMs    = 500;
constexpr qint64 kProgressThrottleBytes = 512LL * 1024;
constexpr int    kMaxAttempts           = 3;       // per URL, 2/4/8s backoff

int attemptDelayMs(int attempt)
{
    switch (attempt) {
    case 0:  return 0;       // first try immediate
    case 1:  return 2000;
    case 2:  return 4000;
    default: return 8000;
    }
}

QString sanitizeFilename(const QString& raw)
{
    static const QRegularExpression kBadCharRe(
        QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    QString s = raw;
    s.replace(kBadCharRe, QStringLiteral("_"));
    s = s.trimmed();
    while (s.endsWith(QChar('.')) || s.endsWith(QChar(' '))) s.chop(1);
    if (s.isEmpty()) s = QStringLiteral("download");
    if (s.size() > 200) s = s.left(200);
    return s;
}

QString filenameFromContentDisposition(const QString& cd)
{
    if (cd.isEmpty()) return {};
    static const QRegularExpression kFilenameStarRe(
        QStringLiteral(R"RX(filename\*\s*=\s*(?:UTF-8|utf-8)'[^']*'([^;]+))RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kFilenameRe(
        QStringLiteral(R"RX(filename\s*=\s*"([^"]+)")RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kFilenameBareRe(
        QStringLiteral(R"RX(filename\s*=\s*([^;]+))RX"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = kFilenameStarRe.match(cd);
    if (m.hasMatch()) return QUrl::fromPercentEncoding(m.captured(1).toLatin1()).trimmed();
    m = kFilenameRe.match(cd);
    if (m.hasMatch()) return m.captured(1).trimmed();
    m = kFilenameBareRe.match(cd);
    if (m.hasMatch()) return m.captured(1).trimmed();
    return {};
}

} // namespace

BookDownloader::BookDownloader(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    loadIndex();
}

BookDownloader::~BookDownloader()
{
    if (m_active) {
        closeAndDeletePart(*m_active);
        delete m_active;
        m_active = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// disk + index
// ─────────────────────────────────────────────────────────────────────────────

QString BookDownloader::baseDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/books");
}

void BookDownloader::loadIndex()
{
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.path    = o.value(QStringLiteral("path")).toString();
        e.title   = o.value(QStringLiteral("title")).toString();
        e.bytes   = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble());
        // Drop stale entries whose file was deleted outside the app.
        if (!e.path.isEmpty() && QFileInfo::exists(e.path))
            m_index.insert(it.key(), e);
    }
}

void BookDownloader::saveIndex() const
{
    QDir().mkpath(baseDir());
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("path")]    = it.value().path;
        o[QStringLiteral("title")]   = it.value().title;
        o[QStringLiteral("bytes")]   = static_cast<double>(it.value().bytes);
        o[QStringLiteral("addedAt")] = static_cast<double>(it.value().addedAt);
        root[it.key()] = o;
    }
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void BookDownloader::writeEntry(const InFlight& f)
{
    Entry e;
    e.path    = f.finalPath;
    e.title   = f.title;
    e.bytes   = f.receivedBytes;
    e.addedAt = QDateTime::currentMSecsSinceEpoch();
    m_index.insert(f.md5, e);
    saveIndex();
}

// ─────────────────────────────────────────────────────────────────────────────
// QML entry points
// ─────────────────────────────────────────────────────────────────────────────

QString BookDownloader::localBook(const QString& md5) const
{
    auto it = m_index.constFind(md5.trimmed().toLower());
    if (it == m_index.constEnd()) return {};
    if (!QFileInfo::exists(it.value().path)) return {};
    return it.value().path;
}

bool BookDownloader::isDownloaded(const QString& md5) const
{
    return !localBook(md5).isEmpty();
}

bool BookDownloader::isActive(const QString& md5) const
{
    const QString m = md5.trimmed().toLower();
    if (m_active && m_active->md5 == m) return true;
    for (const InFlight& q : m_queue)
        if (q.md5 == m) return true;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().md5 == m) return true;
    return false;
}

QVariantMap BookDownloader::statusOf(const QString& md5) const
{
    const QString m = md5.trimmed().toLower();
    QVariantMap s;
    if (isDownloaded(m)) {
        s[QStringLiteral("state")]    = QStringLiteral("done");
        s[QStringLiteral("received")] = static_cast<double>(m_index.value(m).bytes);
        s[QStringLiteral("total")]    = static_cast<double>(m_index.value(m).bytes);
        return s;
    }
    if (m_active && m_active->md5 == m) {
        s[QStringLiteral("state")]    = QStringLiteral("downloading");
        s[QStringLiteral("received")] = static_cast<double>(m_active->receivedBytes);
        s[QStringLiteral("total")]    = static_cast<double>(m_active->expectedBytes);
        return s;
    }
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it) {
        if (it.value().md5 == m) { s[QStringLiteral("state")] = QStringLiteral("resolving"); return s; }
    }
    for (const InFlight& q : m_queue) {
        if (q.md5 == m) { s[QStringLiteral("state")] = QStringLiteral("queued"); return s; }
    }
    s[QStringLiteral("state")] = QStringLiteral("none");
    return s;
}

void BookDownloader::downloadBook(const QString& md5In, const QString& suggestedName,
                                  const QString& title, double expectedBytes)
{
    const QString md5 = md5In.trimmed().toLower();
    if (md5.isEmpty()) { emit failed(md5, QStringLiteral("empty md5")); return; }

    // Idempotent: already on disk → just re-announce it.
    if (isDownloaded(md5)) { emit finished(md5, localBook(md5)); return; }
    // Already resolving / downloading / queued → no-op.
    if (isActive(md5)) return;

    emit resolving(md5);

    // Resolve LibGen's ephemeral get.php URL right before streaming (key ~60s).
    const QUrl target(QStringLiteral("%1/ads.php?md5=%2").arg(QString::fromLatin1(kLibGenBase), md5));
    QNetworkRequest req(target);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_nam->get(req);
    ResolveCtx ctx;
    ctx.md5           = md5;
    ctx.suggestedName = suggestedName.isEmpty() ? (md5 + QStringLiteral(".epub")) : suggestedName;
    ctx.title         = title;
    ctx.expectedBytes = static_cast<qint64>(expectedBytes);
    m_resolving.insert(reply, ctx);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onResolveFinished(reply); });
}

void BookDownloader::onResolveFinished(QNetworkReply* reply)
{
    if (!reply) return;
    const ResolveCtx ctx = m_resolving.take(reply);
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError err = reply->error();
    const QString errStr = reply->errorString();
    reply->deleteLater();

    if (ctx.md5.isEmpty()) return;  // cancelled

    if (err != QNetworkReply::NoError) {
        emit failed(ctx.md5, QStringLiteral("LibGen /ads.php fetch failed: %1").arg(errStr));
        return;
    }
    const QStringList urls = parseResolveHtml(body);
    if (urls.isEmpty()) {
        emit failed(ctx.md5, QStringLiteral("LibGen /ads.php returned no get.php links"));
        return;
    }
    qInfo() << "[BookDownloader] resolved" << urls.size() << "mirror URL(s) for" << ctx.md5;
    startDownload(ctx.md5, ctx.title, urls, ctx.suggestedName, ctx.expectedBytes);
}

QStringList BookDownloader::parseResolveHtml(const QByteArray& html) const
{
    const QString text = QString::fromUtf8(html);
    // Direct-download link: <a href="get.php?md5=X&key=Y">. RX( )RX delimiters
    // so the embedded )" can't terminate the raw string early.
    static const QRegularExpression kGetRe(
        QStringLiteral(R"RX(<a[^>]*href="(get\.php\?[^"]*md5=[a-fA-F0-9]{32}[^"]*)"[^>]*>)RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kLibraryLolRe(
        QStringLiteral(R"RX(<a[^>]*href="(https?://[^"]*library\.lol[^"]+)"[^>]*>)RX"),
        QRegularExpression::CaseInsensitiveOption);

    QStringList urls;
    QSet<QString> seen;
    auto pushUnique = [&](const QString& c) {
        if (c.isEmpty() || seen.contains(c)) return;
        seen.insert(c);
        urls.append(c);
    };

    auto getIt = kGetRe.globalMatch(text);
    while (getIt.hasNext()) {
        QString rel = getIt.next().captured(1);
        rel.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        pushUnique(QStringLiteral("%1/%2").arg(QString::fromLatin1(kLibGenBase), rel));
    }
    auto lolIt = kLibraryLolRe.globalMatch(text);
    while (lolIt.hasNext()) {
        QString url = lolIt.next().captured(1);
        url.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        pushUnique(url);
    }
    return urls;
}

void BookDownloader::cancelDownload(const QString& md5In)
{
    const QString md5 = md5In.trimmed().toLower();
    // Resolving (ads.php in flight)
    for (auto it = m_resolving.begin(); it != m_resolving.end(); ++it) {
        if (it.value().md5 == md5) {
            QNetworkReply* r = it.key();
            m_resolving.erase(it);
            if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
            emit failed(md5, QStringLiteral("cancelled by user"));
            return;
        }
    }
    // Active stream
    if (m_active && m_active->md5 == md5) {
        failAndCleanup(*m_active, QStringLiteral("cancelled by user"));
        return;
    }
    // Queued
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].md5 == md5) {
            m_queue.removeAt(i);
            emit failed(md5, QStringLiteral("cancelled by user (queued)"));
            return;
        }
    }
}

void BookDownloader::deleteBook(const QString& md5In)
{
    const QString md5 = md5In.trimmed().toLower();
    auto it = m_index.find(md5);
    if (it == m_index.end()) return;
    QFile::remove(it.value().path);
    m_index.erase(it);
    saveIndex();
    emit removed(md5);
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP streaming download (ported from TB2 BookDownloader, HTTP path)
// ─────────────────────────────────────────────────────────────────────────────

void BookDownloader::startDownload(const QString& md5, const QString& title,
                                   const QStringList& urls, const QString& suggestedName,
                                   qint64 expectedBytes)
{
    InFlight f;
    f.md5           = md5;
    f.title         = title;
    f.urls          = urls;
    f.suggestedName = sanitizeFilename(suggestedName);
    f.expectedBytes = expectedBytes;

    if (m_active) {
        m_queue.append(std::move(f));
        return;
    }
    m_active = new InFlight(std::move(f));
    startAttempt(*m_active);
}

void BookDownloader::startAttempt(InFlight& f)
{
    if (f.urlIdx >= f.urls.size()) {
        failAndCleanup(f, QStringLiteral("all mirror URLs exhausted"));
        return;
    }
    const QString url = f.urls.value(f.urlIdx);
    if (url.isEmpty()) { startNextUrlOrFail(f); return; }

    // Disk-space pre-check when LibGen gave us a usable size.
    if (f.expectedBytes > 0) {
        const QStorageInfo storage(baseDir());
        if (storage.isValid() && storage.isReady()
            && storage.bytesAvailable() < f.expectedBytes + kDiskSpaceSafetyBytes) {
            failAndCleanup(f, QStringLiteral("insufficient disk space for download"));
            return;
        }
    }

    const int delay = attemptDelayMs(f.attempt);
    if (delay <= 0) {
        // issue now
        if (!pickTargetFilename(f)) { failAndCleanup(f, QStringLiteral("could not prepare destination path")); return; }
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
        req.setRawHeader("Accept", "*/*");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = m_nam->get(req);
        f.reply = reply;
        connect(reply, &QNetworkReply::readyRead,        this, &BookDownloader::onReadyRead);
        connect(reply, &QNetworkReply::finished,         this, &BookDownloader::onFinished);
        connect(reply, &QNetworkReply::downloadProgress, this, &BookDownloader::onProgressFromReply);
    } else {
        const QString md5 = f.md5;
        QTimer::singleShot(delay, this, [this, md5]() {
            if (!m_active || m_active->md5 != md5) return;
            // Re-enter with delay already served (attempt 0-path issues the request).
            m_active->attempt = 0;        // collapse to immediate-issue branch
            startAttempt(*m_active);
        });
    }
}

void BookDownloader::onReadyRead()
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
        if (detectStaleHtml(chunk, ct)) {
            qWarning() << "[BookDownloader] stale key for" << f.urls.value(f.urlIdx)
                       << "(Content-Type=" << ct << ") — failing over";
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
            f.reply.clear();
            if (f.file) { f.file->close(); f.file->remove(); delete f.file; f.file = nullptr; }
            startNextUrlOrFail(f);   // stale key is URL-level, skip this URL's retries
            return;
        }
        // Honour a safe Content-Disposition filename (only finalPath; partPath
        // stays in sync with the already-open QFile, renamed at finalize).
        const QString cd = reply->header(QNetworkRequest::ContentDispositionHeader).toString();
        const QString cdName = filenameFromContentDisposition(cd);
        if (!cdName.isEmpty()) {
            const QString sane = sanitizeFilename(cdName);
            if (!sane.isEmpty()) f.finalPath = QDir(baseDir()).absoluteFilePath(sane);
        }
    }

    if (f.file) {
        const qint64 written = f.file->write(chunk);
        if (written < 0) { failAndCleanup(f, QStringLiteral("disk write failed: %1").arg(f.file->errorString())); return; }
        f.receivedBytes += written;
    }
}

void BookDownloader::onProgressFromReply(qint64 received, qint64 total)
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsedMs = (f.lastProgressEmit == 0) ? (kProgressThrottleMs + 1)
                                                       : (nowMs - f.lastProgressEmit);
    const qint64 deltaBytes = received - f.lastProgressBytes;
    if (elapsedMs >= kProgressThrottleMs || deltaBytes >= kProgressThrottleBytes) {
        f.lastProgressEmit = nowMs;
        f.lastProgressBytes = received;
        emit progress(f.md5, static_cast<double>(received), static_cast<double>(total));
    }
}

void BookDownloader::onFinished()
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
        qWarning() << "[BookDownloader] reply error" << err << "http=" << httpStatus << errString;
        retryOrFailover(f, QStringLiteral("HTTP error: %1 (status %2)").arg(errString).arg(httpStatus));
        return;
    }
    emit progress(f.md5, static_cast<double>(f.receivedBytes), static_cast<double>(f.receivedBytes));
    finalizeSuccess(f);
}

void BookDownloader::finalizeSuccess(InFlight& f)
{
    if (f.file) { f.file->close(); delete f.file; f.file = nullptr; }

    if (f.receivedBytes <= 0) {
        QFile::remove(f.partPath);
        failAndCleanup(f, QStringLiteral("server returned empty body"));
        return;
    }
    if (QFile::exists(f.finalPath)) QFile::remove(f.finalPath);
    if (!QFile::rename(f.partPath, f.finalPath)) {
        const QString reason = QStringLiteral("rename %1 -> %2 failed").arg(f.partPath, f.finalPath);
        QFile::remove(f.partPath);
        failAndCleanup(f, reason);
        return;
    }

    const QString md5 = f.md5;
    const QString finalPath = f.finalPath;
    qInfo() << "[BookDownloader] complete md5=" << md5 << "path=" << finalPath
            << "bytes=" << f.receivedBytes;

    writeEntry(f);
    emit finished(md5, finalPath);

    delete m_active; m_active = nullptr;
    if (!m_queue.isEmpty()) {
        m_active = new InFlight(std::move(m_queue.takeFirst()));
        startAttempt(*m_active);
    }
}

void BookDownloader::retryOrFailover(InFlight& f, const QString& reason)
{
    closeAndDeletePart(f);
    f.attempt += 1;
    if (f.attempt < kMaxAttempts) { startAttempt(f); return; }
    qInfo() << "[BookDownloader] url exhausted, failover:" << reason;
    startNextUrlOrFail(f);
}

void BookDownloader::startNextUrlOrFail(InFlight& f)
{
    f.urlIdx += 1;
    f.attempt = 0;
    if (f.urlIdx >= f.urls.size()) { failAndCleanup(f, QStringLiteral("all mirror URLs failed")); return; }
    startAttempt(f);
}

void BookDownloader::failAndCleanup(InFlight& f, const QString& reason)
{
    closeAndDeletePart(f);
    const QString md5 = f.md5;
    emit failed(md5, reason);
    delete m_active; m_active = nullptr;
    if (!m_queue.isEmpty()) {
        m_active = new InFlight(std::move(m_queue.takeFirst()));
        startAttempt(*m_active);
    }
}

void BookDownloader::closeAndDeletePart(InFlight& f)
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
}

bool BookDownloader::detectStaleHtml(const QByteArray& firstChunk, const QString& contentType) const
{
    if (contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive)) return true;
    if (firstChunk.size() >= 5) {
        const QByteArray head = firstChunk.left(512).trimmed().toLower();
        if (head.startsWith("<!doctype html") || head.startsWith("<html") || head.startsWith("<!doctype"))
            return true;
    }
    return false;
}

bool BookDownloader::pickTargetFilename(InFlight& f)
{
    QDir dir(baseDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qWarning() << "[BookDownloader] mkpath failed for" << baseDir();
        return false;
    }
    QString chosen = f.suggestedName;
    if (chosen.isEmpty()) chosen = f.md5 + QStringLiteral(".epub");
    f.finalPath = dir.absoluteFilePath(chosen);
    f.partPath  = f.finalPath + QStringLiteral(".part");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// dev smoke
// ─────────────────────────────────────────────────────────────────────────────

void BookDownloader::selfTest(const QString& md5)
{
    qInfo() << "[BookDownloader] selfTest resolving + downloading md5=" << md5;
    connect(this, &BookDownloader::finished, this, [](const QString& m, const QString& path) {
        qInfo() << "[BookDownloader] selfTest OK md5=" << m << "saved=" << path;
    });
    connect(this, &BookDownloader::failed, this, [](const QString& m, const QString& why) {
        qWarning() << "[BookDownloader] selfTest FAILED md5=" << m << "reason=" << why;
    });
    connect(this, &BookDownloader::progress, this, [](const QString& m, double rcv, double tot) {
        qInfo() << "[BookDownloader] selfTest progress md5=" << m << rcv << "/" << tot;
    });
    downloadBook(md5, QString(), QStringLiteral("selftest"), 0);
}
