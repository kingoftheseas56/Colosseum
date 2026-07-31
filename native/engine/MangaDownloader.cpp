#include "MangaDownloader.h"
#include "DownloadFileOps.h"
#include "WeebCentralScraper.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QDebug>

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------
MangaDownloader::MangaDownloader(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    loadIndex();
}

MangaDownloader::~MangaDownloader() = default;

// ---------------------------------------------------------------------------
// disk paths
// ---------------------------------------------------------------------------
QString MangaDownloader::baseDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/manga");
    QDir().mkpath(dir);
    return dir;
}

QString MangaDownloader::safeSeg(const QString& v)
{
    QString s;
    s.reserve(v.size());
    for (const QChar c : v) {
        if (c.isLetterOrNumber() || c == '.' || c == '_' || c == '-')
            s.append(c);
        else
            s.append('_');
    }
    while (s.startsWith('.')) s.remove(0, 1);
    if (s.isEmpty()) s = QStringLiteral("_");
    return s;
}

QString MangaDownloader::chapterDir(const QString& seriesId, const QString& chapterId) const
{
    // chapter segment = readable prefix + short stable hash, so two different
    // chapterIds that sanitise to the same prefix never collide on disk.
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(chapterId.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
    const QString chapterSeg = safeSeg(chapterId).left(48) + QStringLiteral("-") + hash;
    return baseDir() + QStringLiteral("/") + safeSeg(seriesId) + QStringLiteral("/") + chapterSeg;
}

QString MangaDownloader::extForContentType(const QString& ct, const QString& fallbackUrl)
{
    const QString c = ct.toLower();
    if (c.contains(QLatin1String("jpeg")) || c.contains(QLatin1String("jpg"))) return QStringLiteral("jpg");
    if (c.contains(QLatin1String("png")))  return QStringLiteral("png");
    if (c.contains(QLatin1String("webp"))) return QStringLiteral("webp");
    if (c.contains(QLatin1String("avif"))) return QStringLiteral("avif");
    if (c.contains(QLatin1String("gif")))  return QStringLiteral("gif");
    // fall back to the URL's own suffix
    const QString suffix = QUrl(fallbackUrl).fileName().section('.', -1).toLower();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) return QStringLiteral("jpg");
    if (suffix == QLatin1String("png") || suffix == QLatin1String("webp")
        || suffix == QLatin1String("avif") || suffix == QLatin1String("gif")) return suffix;
    return QStringLiteral("jpg");
}

// ---------------------------------------------------------------------------
// index persistence  (<appdata>/manga/index.json)
// ---------------------------------------------------------------------------
void MangaDownloader::loadIndex()
{
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    bool healed = false;   // any repair or prune this load -> persist the healed index
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.seriesId     = o.value(QStringLiteral("seriesId")).toString();
        e.seriesTitle  = o.value(QStringLiteral("seriesTitle")).toString();
        e.chapterLabel = o.value(QStringLiteral("chapterLabel")).toString();
        e.dir          = o.value(QStringLiteral("dir")).toString();
        e.bytes        = qint64(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt      = qint64(o.value(QStringLiteral("addedAt")).toDouble());
        for (const QJsonValue v : o.value(QStringLiteral("files")).toArray())
            e.files.append(v.toString());
        // self-heal, REPAIR-then-prune (2026-07-12): a dir that no longer exists is
        // first assumed MOVED, not gone — the org-rename relocated the whole manga
        // tree while the ledger kept old absolute paths (prune-only destroyed live
        // Bleach / One Piece entries; caught pre-commit). Re-root the path tail
        // (everything after the last "/manga/") onto the current base; only an entry
        // whose files are missing THERE TOO is a true ghost, and only then dropped.
        if (!e.dir.isEmpty() && !QDir(e.dir).exists()) {
            const QString marker = QStringLiteral("/manga/");
            const int at = e.dir.lastIndexOf(marker);
            QString repairedDir;
            if (at >= 0) {
                const QString tail = e.dir.mid(at + marker.size());
                const QString candidate = baseDir() + QStringLiteral("/") + tail;
                if (!tail.isEmpty() && QDir(candidate).exists())
                    repairedDir = candidate;
            }
            if (!repairedDir.isEmpty()) {
                qInfo("[downloads] repaired ledger path '%s' (%s -> %s)",
                      qUtf8Printable(it.key()), qUtf8Printable(e.dir),
                      qUtf8Printable(repairedDir));
                e.dir = repairedDir;
                healed = true;
            } else {
                qInfo("[downloads] pruning ghost entry '%s' (dir missing)",
                      qUtf8Printable(it.key()));
                healed = true;
                continue;
            }
        }
        if (!e.files.isEmpty())
            m_index.insert(it.key(), e);
    }
    qInfo("[downloads] loaded index: %d chapters", int(m_index.size()));
    if (healed) saveIndex();   // durable heal: repaired paths stick, ghosts stay gone
}

void MangaDownloader::saveIndex() const
{
    QJsonObject entries;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        QJsonArray files;
        for (const QString& f : e.files) files.append(f);
        entries.insert(it.key(), QJsonObject{
            {QStringLiteral("seriesId"),     e.seriesId},
            {QStringLiteral("seriesTitle"),  e.seriesTitle},
            {QStringLiteral("chapterLabel"), e.chapterLabel},
            {QStringLiteral("dir"),          e.dir},
            {QStringLiteral("bytes"),        double(e.bytes)},
            {QStringLiteral("addedAt"),      double(e.addedAt)},
            {QStringLiteral("files"),        files},
        });
    }
    const QJsonObject root{{QStringLiteral("schemaVersion"), 1},
                           {QStringLiteral("entries"), entries}};
    // atomic write so a crash mid-save never corrupts the index
    QSaveFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

void MangaDownloader::writeEntry(const Job* job)
{
    Entry e;
    e.seriesId     = job->seriesId;
    e.seriesTitle  = job->seriesTitle;
    e.chapterLabel = job->chapterLabel;
    e.dir          = job->dir;
    e.files        = job->files;
    e.bytes        = job->bytes;
    e.addedAt      = QDateTime::currentMSecsSinceEpoch();
    m_index.insert(job->chapterId, e);
    saveIndex();
}

// ---------------------------------------------------------------------------
// QML entry points
// ---------------------------------------------------------------------------
void MangaDownloader::downloadChapter(const QString& chapterId, const QString& seriesId,
                                      const QString& seriesTitle, const QString& chapterLabel)
{
    if (chapterId.isEmpty()) return;
    if (isDownloaded(chapterId)) { emit finished(chapterId); return; }
    if (m_active.contains(chapterId)) return;
    for (const Job* q : m_queue) if (q->chapterId == chapterId) return;   // already queued

    Job* job = new Job;
    job->chapterId    = chapterId;
    job->seriesId     = seriesId;
    job->seriesTitle  = seriesTitle;
    job->chapterLabel = chapterLabel;
    job->dir          = chapterDir(seriesId, chapterId);
    m_queue.enqueue(job);
    emit progress(chapterId, 0, 0);    // surfaces "queued" immediately
    pumpQueue();
}

QVariantList MangaDownloader::localPages(const QString& chapterId) const
{
    QVariantList out;
    const auto it = m_index.constFind(chapterId);
    if (it == m_index.constEnd()) return out;
    const Entry& e = it.value();
    for (int i = 0; i < e.files.size(); ++i) {
        if (e.files[i].isEmpty()) continue;
        out.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("url"),
             QUrl::fromLocalFile(e.dir + QStringLiteral("/") + e.files[i]).toString()},
            {QStringLiteral("group"), -1}});
    }
    return out;
}

bool MangaDownloader::isDownloaded(const QString& chapterId) const
{
    const auto it = m_index.constFind(chapterId);
    return it != m_index.constEnd() && !it.value().files.isEmpty();
}

QVariantMap MangaDownloader::statusOf(const QString& chapterId) const
{
    if (isDownloaded(chapterId)) {
        const int n = m_index.value(chapterId).files.size();
        return {{QStringLiteral("state"), QStringLiteral("done")},
                {QStringLiteral("done"), n}, {QStringLiteral("total"), n}};
    }
    if (const Job* job = m_active.value(chapterId, nullptr)) {
        const bool started = job->total > 0;
        return {{QStringLiteral("state"),
                 started ? QStringLiteral("downloading") : QStringLiteral("queued")},
                {QStringLiteral("done"), job->done}, {QStringLiteral("total"), job->total}};
    }
    for (const Job* q : m_queue)
        if (q->chapterId == chapterId)
            return {{QStringLiteral("state"), QStringLiteral("queued")},
                    {QStringLiteral("done"), 0}, {QStringLiteral("total"), 0}};
    return {{QStringLiteral("state"), QStringLiteral("none")},
            {QStringLiteral("done"), 0}, {QStringLiteral("total"), 0}};
}


QVariantList MangaDownloader::downloadedChapters() const
{
    QVariantList out;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        const QString first = e.files.isEmpty()
            ? QString() : e.dir + QStringLiteral("/") + e.files.first();
        const bool missing = first.isEmpty() || !QFile::exists(first);
        out.append(QVariantMap{
            {QStringLiteral("id"), it.key()},
            {QStringLiteral("seriesId"), e.seriesId},
            {QStringLiteral("seriesTitle"), e.seriesTitle},
            {QStringLiteral("label"), e.chapterLabel},
            {QStringLiteral("pages"), e.files.size()},
            {QStringLiteral("bytes"), e.bytes},
            {QStringLiteral("addedAt"), e.addedAt},
            {QStringLiteral("missing"), missing},
            // First page = the chapter's own local cover (Downloads-page art).
            {QStringLiteral("art"), missing
                ? QString() : QUrl::fromLocalFile(first).toString()}
        });
    }
    return out;
}

QVariantList MangaDownloader::activeChapterJobs() const
{
    QVariantList out;
    auto jobRow = [](const Job* j, const QString& state) {
        return QVariantMap{
            {QStringLiteral("id"), j->chapterId},
            {QStringLiteral("seriesId"), j->seriesId},
            {QStringLiteral("seriesTitle"), j->seriesTitle},
            {QStringLiteral("label"), j->chapterLabel},
            {QStringLiteral("state"), state},
            {QStringLiteral("done"), j->done},
            {QStringLiteral("total"), j->total}
        };
    };
    for (auto it = m_active.constBegin(); it != m_active.constEnd(); ++it)
        out.append(jobRow(it.value(), it.value()->total > 0
                          ? QStringLiteral("downloading") : QStringLiteral("queued")));
    for (const Job* q : m_queue)
        out.append(jobRow(q, QStringLiteral("queued")));
    return out;
}

// ---------------------------------------------------------------------------
// delete / cancel
// ---------------------------------------------------------------------------
QVariantMap MangaDownloader::deleteChapter(const QString& chapterId)
{
    const auto it = m_index.constFind(chapterId);
    if (it == m_index.constEnd())
        return DownloadFileOps::toMap({true, QString()});
    const QString dir = it.value().dir;
    const auto result = DownloadFileOps::removeTree(dir);
    if (!result.success) {
        qWarning() << "[downloads] delete failed" << chapterId << result.message;
        return DownloadFileOps::toMap(result);
    }
    m_index.remove(chapterId);
    m_thumbCache.remove(chapterId);
    saveIndex();
    qInfo("[downloads] deleted '%s'", qUtf8Printable(chapterId));
    emit removed(chapterId);
    return DownloadFileOps::toMap(result);
}

void MangaDownloader::cancelDownload(const QString& chapterId)
{
    // queued (not yet started) -> drop it from the queue
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i)->chapterId == chapterId) {
            Job* j = m_queue.at(i);
            m_queue.removeAt(i);
            delete j;
            emit removed(chapterId);
            return;
        }
    }
    // in-flight -> flag + abort replies; finalize once all slots have drained
    Job* job = m_active.value(chapterId, nullptr);
    if (!job) return;
    job->cancelled = true;
    const QList<QNetworkReply*> replies = job->replies;
    for (QNetworkReply* r : replies) if (r) r->abort();   // abort -> finished -> cancelled branch
    if (m_active.value(chapterId, nullptr) == job && job->inFlight == 0) finalizeCancel(job);
}

void MangaDownloader::finalizeCancel(Job* job)
{
    if (!job->dir.isEmpty()) QDir(job->dir).removeRecursively();   // drop partials
    const QString id = job->chapterId;
    qInfo("[downloads] cancelled '%s'", qUtf8Printable(id));
    cleanupJob(job);
    emit removed(id);
}

// ---------------------------------------------------------------------------
// chapter thumbnails — first page; downloaded -> local, else scrape once
// (capped concurrency, session cache). Always answers via thumbReady().
// ---------------------------------------------------------------------------
void MangaDownloader::fetchThumb(const QString& seriesId, const QString& chapterId)
{
    if (chapterId.isEmpty()) return;
    if (m_thumbCache.contains(chapterId)) { emit thumbReady(chapterId, m_thumbCache.value(chapterId)); return; }
    if (isDownloaded(chapterId)) {
        const QVariantList lp = localPages(chapterId);
        const QString url = lp.isEmpty() ? QString()
                          : lp.first().toMap().value(QStringLiteral("url")).toString();
        m_thumbCache.insert(chapterId, url);
        emit thumbReady(chapterId, url);
        return;
    }
    if (m_thumbInflight.contains(chapterId)) return;
    for (const ThumbReq& q : m_thumbQueue) if (q.chapterId == chapterId) return;
    m_thumbQueue.enqueue(ThumbReq{seriesId, chapterId});
    pumpThumbs();
}

void MangaDownloader::pumpThumbs()
{
    while (m_thumbActive < THUMB_CONCURRENCY && !m_thumbQueue.isEmpty()) {
        const ThumbReq req = m_thumbQueue.dequeue();
        const QString cid = req.chapterId;
        m_thumbActive++;
        m_thumbInflight.insert(cid);
        auto* sc = new WeebCentralScraper(m_nam, this);
        // `cacheable` separates AN ANSWER from A FAILURE. A scrape that succeeded
        // is final and worth remembering, even when the chapter genuinely has no
        // pages. A scrape that ERRORED is not an answer — WeebCentral throttles
        // under a burst, and caching that empty string made the miss PERMANENT for
        // the session: fetchThumb returns early on any cached key, so the volume
        // (or chapter) showed a numbered placeholder forever and never retried.
        // Found from eyes-on 2026-07-31: One Piece volumes 101/102/110 and the
        // Latest-chapters rows, all blank while volume 1 had its cover.
        auto settle = [this, sc, cid](const QString& url, bool cacheable) {
            if (!m_thumbInflight.contains(cid)) return;   // already settled by the other signal
            if (cacheable)
                m_thumbCache.insert(cid, url);
            emit thumbReady(cid, url);
            m_thumbInflight.remove(cid);
            m_thumbActive--;
            sc->deleteLater();
            pumpThumbs();
        };
        connect(sc, &MangaScraper::pagesReady, this, [settle](const QList<PageInfo>& pages) {
            settle(pages.isEmpty() ? QString() : pages.first().imageUrl, /*cacheable=*/true);
        });
        connect(sc, &MangaScraper::errorOccurred, this, [settle](const QString&) {
            settle(QString(), /*cacheable=*/false);   // transient — must stay retryable
        });
        sc->fetchPages(cid);
    }
}

// ---------------------------------------------------------------------------
// dev smoke — headless end-to-end proof of the pipeline
// ---------------------------------------------------------------------------
void MangaDownloader::selfTest(const QString& seriesTitle)
{
    connect(this, &MangaDownloader::finished, this, [this](const QString& cid) {
        qInfo("[dl-selftest] FINISHED %s -> localPages=%d",
              qUtf8Printable(cid), int(localPages(cid).size()));
    });
    connect(this, &MangaDownloader::failed, this, [](const QString& cid, const QString& reason) {
        qWarning("[dl-selftest] FAILED %s: %s", qUtf8Printable(cid), qUtf8Printable(reason));
    });

    auto* probe = new WeebCentralScraper(m_nam, this);
    connect(probe, &MangaScraper::searchFinished, this,
            [this, probe, seriesTitle](const QList<MangaResult>& r) {
        if (r.isEmpty()) { qWarning("[dl-selftest] '%s': 0 results", qUtf8Printable(seriesTitle)); return; }
        const QString sid = r.first().id;
        connect(probe, &MangaScraper::chaptersReady, this,
                [this, seriesTitle, sid](const QList<ChapterInfo>& ch) {
            if (ch.isEmpty()) { qWarning("[dl-selftest] '%s': 0 chapters", qUtf8Printable(seriesTitle)); return; }
            ChapterInfo pick = ch.first();
            for (const ChapterInfo& c : ch)   // earliest numbered chapter = the smallest download
                if (c.chapterNumber > 0 && (pick.chapterNumber <= 0 || c.chapterNumber < pick.chapterNumber))
                    pick = c;
            const QString label = pick.name.isEmpty()
                ? QStringLiteral("Chapter %1").arg(pick.chapterNumber) : pick.name;
            qInfo("[dl-selftest] %d chapters; downloading '%s' / '%s' (id=%s)",
                  int(ch.size()), qUtf8Printable(seriesTitle), qUtf8Printable(label), qUtf8Printable(pick.id));
            downloadChapter(pick.id, sid, seriesTitle, label);
        });
        probe->fetchChapters(sid);
    });
    probe->search(seriesTitle);
}

// ---------------------------------------------------------------------------
// ledger self-heal proof (env COLOSSEUM_INDEX_SELFTEST=1)
// ---------------------------------------------------------------------------
void MangaDownloader::indexSelfTest()
{
    const QString base = baseDir();

    // lane (a): true ghost — dir missing under the current base, no repair possible.
    Entry ghost;
    ghost.seriesId = QStringLiteral("selfheal:ghost");
    ghost.seriesTitle = QStringLiteral("SelfHeal Ghost");
    ghost.dir = base + QStringLiteral("/__selfheal_ghost_missing__");
    ghost.files.append(QStringLiteral("page_000.jpg"));
    m_index.insert(QStringLiteral("selfheal:ghost/issue-1"), ghost);

    // lane (b): repairable — the files REALLY exist under the current base, but the
    // ledger's dir points at a fake old root (the org-rename shape that destroyed the
    // Bleach / One Piece entries, caught pre-commit 2026-07-12). Must SURVIVE the
    // reload with dir re-rooted onto the current base — never be pruned.
    const QString realDir = base + QStringLiteral("/__selfheal_repair_target__");
    QDir().mkpath(realDir);
    QFile pageMarker(realDir + QStringLiteral("/page_000.jpg"));
    if (pageMarker.open(QIODevice::WriteOnly)) { pageMarker.write("x"); pageMarker.close(); }
    Entry stale;
    stale.seriesId = QStringLiteral("selfheal:repair");
    stale.seriesTitle = QStringLiteral("SelfHeal Repair");
    stale.dir = QStringLiteral("C:/__selfheal_fake_old_root__/manga/__selfheal_repair_target__");
    stale.files.append(QStringLiteral("page_000.jpg"));
    m_index.insert(QStringLiteral("selfheal:repair/issue-1"), stale);

    saveIndex();
    m_index.clear();
    loadIndex();

    const bool ghostPruned = !m_index.contains(QStringLiteral("selfheal:ghost/issue-1"));
    const bool staleRepaired = m_index.contains(QStringLiteral("selfheal:repair/issue-1"))
        && m_index.value(QStringLiteral("selfheal:repair/issue-1")).dir == realDir;
    if (ghostPruned && staleRepaired)
        qInfo("INDEX-SELFTEST OK");
    else
        qInfo("INDEX-SELFTEST FAILED (ghost pruned=%d, stale repaired=%d)",
              int(ghostPruned), int(staleRepaired));

    // clean up both fixtures (ledger + disk)
    m_index.remove(QStringLiteral("selfheal:ghost/issue-1"));
    m_index.remove(QStringLiteral("selfheal:repair/issue-1"));
    saveIndex();
    QDir(realDir).removeRecursively();
}

// ---------------------------------------------------------------------------
// queue pump + per-job lifecycle
// ---------------------------------------------------------------------------
void MangaDownloader::pumpQueue()
{
    while (m_active.size() < MAX_CONCURRENT_CHAPTERS && !m_queue.isEmpty()) {
        Job* job = m_queue.dequeue();
        m_active.insert(job->chapterId, job);
        beginJob(job);
    }
}

void MangaDownloader::beginJob(Job* job)
{
    QDir().mkpath(job->dir);
    job->scraper = new WeebCentralScraper(m_nam, this);
    connect(job->scraper, &MangaScraper::pagesReady, this,
            [this, job](const QList<PageInfo>& pages) { onPagesReady(job, pages); });
    connect(job->scraper, &MangaScraper::errorOccurred, this,
            [this, job](const QString& e) { failJob(job, e); });
    job->scraper->fetchPages(job->chapterId);
}

void MangaDownloader::onPagesReady(Job* job, const QList<PageInfo>& pages)
{
    if (pages.isEmpty()) { failJob(job, QStringLiteral("no pages found")); return; }

    job->pages = pages;
    job->total = pages.size();
    job->files = QStringList(job->total, QString());

    // resume: count any page already on disk (> 1 KB) from a prior interrupted run
    QDir dir(job->dir);
    for (int i = 0; i < job->total; ++i) {
        const QStringList hits =
            dir.entryList({QStringLiteral("page_%1.*").arg(i, 3, 10, QChar('0'))}, QDir::Files);
        for (const QString& name : hits) {
            const qint64 sz = QFileInfo(dir.filePath(name)).size();
            if (sz > MIN_VALID_BYTES) {
                job->files[i] = name;
                job->done++;
                job->bytes += sz;
                break;
            }
        }
    }

    emit progress(job->chapterId, job->done, job->total);
    if (job->done == job->total) { finishJob(job); return; }
    pumpImages(job);
}

void MangaDownloader::pumpImages(Job* job)
{
    if (job->failedFlag) {
        if (job->inFlight == 0) failJob(job, QStringLiteral("image download failed"));
        return;
    }
    while (job->inFlight < IMAGE_CONCURRENCY && job->nextDispatch < job->total) {
        const int i = job->nextDispatch++;
        if (!job->files[i].isEmpty()) continue;   // resumed page — already on disk
        job->inFlight++;
        fetchImage(job, i, 0);
    }
}

// A downloaded page must be a REAL image. Some sources soft-block a bursty client by
// answering an image URL with HTTP 200 + their homepage HTML (the "blank pages" bug) —
// magic-byte + content-type validation rejects that so it's never saved as a .jpg.
// Accepts JPEG/PNG/GIF/WebP (everything WeebCentral serves).
static bool looksLikeImage(const QByteArray& d, const QString& ct)
{
    const QString c = ct.toLower();
    if (c.contains(QLatin1String("html")) || c.startsWith(QLatin1String("text/"))) return false;
    if (d.size() >= 12) {
        const unsigned char* b = reinterpret_cast<const unsigned char*>(d.constData());
        if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return true;                 // JPEG
        if (b[0] == 0x89 && b[1] == 0x50 && b[2] == 0x4E && b[3] == 0x47) return true; // PNG
        if (b[0] == 0x47 && b[1] == 0x49 && b[2] == 0x46) return true;                 // GIF
        if (qstrncmp(d.constData(), "RIFF", 4) == 0
            && qstrncmp(d.constData() + 8, "WEBP", 4) == 0) return true;               // WebP
    }
    if (d.startsWith("<!DOCTYPE") || d.startsWith("<!doctype") || d.startsWith("<html")) return false;
    return c.startsWith(QLatin1String("image/"));   // declared image, unknown magic — allow (rare CDN)
}

// Decide what a finished page-image reply IS, purely from the transport error + body.
// Some sources serve real image bytes on an HTTP error status (e.g. a 404 sets
// QNetworkReply::ContentNotFoundError yet readAll() still carries the real image) — trust
// magic bytes, not status. So the image check comes FIRST and wins regardless of err. Only
// if the body is NOT an image do we split by transport state: a clean HTTP 200 that returned
// sizeable HTML is the soft-block (throttle); anything else (connection error, empty,
// non-image on a real error status) is a plain error. Both non-image verdicts feed the
// same retry ladder now that the cool-wave pacing (the retired preset-pages source,
// cut 2026-07-12) is gone.
MangaDownloader::PageVerdict MangaDownloader::classifyPageReply(QNetworkReply::NetworkError err,
                                                               const QByteArray& body,
                                                               const QString& contentType)
{
    if (body.size() > MIN_VALID_BYTES && looksLikeImage(body, contentType))
        return PageVerdict::Accept;
    if (err == QNetworkReply::NoError && body.size() > MIN_VALID_BYTES)
        return PageVerdict::SoftBlock;   // clean 200 + sizeable non-image HTML = throttle
    return PageVerdict::Error;
}

void MangaDownloader::fetchImage(Job* job, int pageIndex, int attempt)
{
    if (job->cancelled) { job->inFlight--; if (job->inFlight == 0) finalizeCancel(job); return; }

    const QString url = job->pages[pageIndex].imageUrl;
    QUrl u(url);
    const QString host = u.host();
    QNetworkRequest req{u};
    req.setRawHeader("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0 Safari/537.36");
    req.setRawHeader("Referer", "https://weebcentral.com/");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);   // we persist to disk ourselves
    req.setTransferTimeout(30000);

    // IPv4 pin (dead-IPv6 machine): rewrite host → IP, keep the real hostname for TLS
    // (peerVerifyName) and the Host header, HTTP/2 off — same recipe as CachingNam.
    // WeebCentral's page-image CDN hosts (hot.planeptune.us, lowee.us, …) DRIFT and were
    // never in the boot-time pin list, so they were the ONE network path still exposed to
    // this ISP's dead-IPv6 stall (curl/happy-eyeballs hid it; downloads intermittently
    // "Failed"). Resolve + cache their real IPv4 on first use so every page fetch takes the
    // working IPv4 route — the same fix every other host in the app already has.
    if (!m_pins.contains(host) && !m_pinTried.contains(host)) {
        m_pinTried.insert(host);
        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress& a : info.addresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                m_pins.insert(host, a.toString());
                qInfo("[downloads] pinned CDN host %s -> %s (dead-IPv6 guard)",
                      qUtf8Printable(host), qUtf8Printable(a.toString()));
                break;
            }
        }
    }
    const QString ipv4 = m_pins.value(host);
    if (!ipv4.isEmpty()) {
        req.setRawHeader("Host", host.toUtf8());
        req.setPeerVerifyName(host);
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        u.setHost(ipv4);
        req.setUrl(u);
    }

    QNetworkReply* reply = m_nam->get(req);
    job->replies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, job, pageIndex, attempt, reply]() {
        reply->deleteLater();
        job->replies.removeOne(reply);
        if (job->cancelled) { job->inFlight--; if (job->inFlight == 0) finalizeCancel(job); return; }
        // readAll() carries the body even on an error status (some sources serve real image
        // bytes on an HTTP 404), so we read it BEFORE branching on error() — the magic bytes,
        // not the status, decide.
        const QByteArray data = reply->readAll();
        const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        // classifyPageReply() accepts ANY reply whose body IS an image (trust magic bytes,
        // not status); a clean-200 sizeable non-image is a soft-block, everything else a real
        // error — both non-image verdicts feed the same retry ladder below.
        const PageVerdict verdict = classifyPageReply(reply->error(), data, ct);

        if (verdict == PageVerdict::Accept) {
            const QString ext = extForContentType(ct, job->pages[pageIndex].imageUrl);
            const QString name = QStringLiteral("page_%1.%2")
                                     .arg(pageIndex, 3, 10, QChar('0')).arg(ext);
            QSaveFile out(job->dir + QStringLiteral("/") + name);
            if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit()) {
                onImageSaved(job, pageIndex, name, data.size());
                return;
            }
        }

        // failure path (SoftBlock + Error alike): retry with 2/4/8s backoff, then mark failed
        if (attempt + 1 < MAX_IMAGE_RETRIES) {
            const int backoffMs = 2000 << attempt;
            QTimer::singleShot(backoffMs, this,
                               [this, job, pageIndex, attempt]() { fetchImage(job, pageIndex, attempt + 1); });
            return;   // slot stays held across the backoff
        }
        // Diagnostic: name the CDN host + the real Qt transport error, so an intermittent
        // dead-IPv6 / CDN stall is pinpointable from the log instead of a bare "failed".
        qWarning("[downloads] page %d of '%s' failed after %d attempts "
                 "(host=%s err=%d '%s' ct='%s' bytes=%lld)",
                 pageIndex, qUtf8Printable(job->chapterId), MAX_IMAGE_RETRIES,
                 qUtf8Printable(QUrl(job->pages[pageIndex].imageUrl).host()),
                 int(reply->error()), qUtf8Printable(reply->errorString()),
                 qUtf8Printable(ct), static_cast<long long>(data.size()));
        job->failedFlag = true;
        job->inFlight--;
        pumpImages(job);
    });
}

void MangaDownloader::onImageSaved(Job* job, int pageIndex, const QString& fileName, qint64 size)
{
    job->files[pageIndex] = fileName;
    job->done++;
    job->bytes += size;
    job->inFlight--;
    emit progress(job->chapterId, job->done, job->total);
    if (job->done == job->total) { finishJob(job); return; }
    pumpImages(job);
}

void MangaDownloader::finishJob(Job* job)
{
    writeEntry(job);
    qInfo("[downloads] finished '%s' — %d pages, %.1f MB",
          qUtf8Printable(job->chapterId), job->total, double(job->bytes) / (1024.0 * 1024.0));
    const QString id = job->chapterId;
    cleanupJob(job);
    emit finished(id);
}

void MangaDownloader::failJob(Job* job, const QString& reason)
{
    // keep partial files on disk so a re-download resumes instead of restarting
    qWarning("[downloads] FAILED '%s': %s", qUtf8Printable(job->chapterId), qUtf8Printable(reason));
    const QString id = job->chapterId;
    cleanupJob(job);
    emit failed(id, reason);
}

void MangaDownloader::cleanupJob(Job* job)
{
    m_active.remove(job->chapterId);
    if (job->scraper) job->scraper->deleteLater();
    delete job;
    pumpQueue();   // free slot -> start the next queued chapter
}
