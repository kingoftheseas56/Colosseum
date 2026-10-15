#include "engine/MangaVolumePacker.h"

#include "engine/MangaScraper.h"
#include "engine/MangaTankobanLogic.h"
#include "engine/MangaVolumeArchiveIngestor.h"
#include "engine/MangaVolumeIndex.h"

#include <QByteArray>
#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QVector>

#include <algorithm>

namespace MangaTankoban {
namespace {

const QByteArray kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36";

} // namespace

MangaVolumePacker::MangaVolumePacker(MangaScraper* scraper, QNetworkAccessManager* nam,
                                     MangaVolumeIndex* index, const QString& stagingRoot,
                                     QObject* parent)
    : QObject(parent)
    , m_scraper(scraper)
    , m_nam(nam)
    , m_index(index)
    , m_ingestor(new MangaVolumeArchiveIngestor(index, this))
    , m_stagingRoot(QDir::cleanPath(stagingRoot))
{
}

MangaVolumePacker::~MangaVolumePacker()
{
    if (m_job)
        teardown(m_job);
    for (const std::shared_ptr<Job>& j : m_queue)
        teardown(j);
}

QString MangaVolumePacker::stagingDirFor(const VolumeRecord& volume) const
{
    const QString vid = MangaTankoban::volumeId(volume.seriesId, volume.number);
    const QString h = QString::fromLatin1(
        QCryptographicHash::hash(vid.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    return m_stagingRoot + QStringLiteral("/wc-pack-") + h;
}

bool MangaVolumePacker::complete(const VolumeRecord& volume) const
{
    const QString vid = MangaTankoban::volumeId(volume.seriesId, volume.number);
    return m_index->statusOf(vid).value(QStringLiteral("state")).toString()
           == QLatin1String("ready");
}

void MangaVolumePacker::pack(const VolumeRecord& volume, const QString& seriesTitle)
{
    const QString vid = MangaTankoban::volumeId(volume.seriesId, volume.number);
    if (volume.chapterIds.isEmpty()) {
        emit failed(vid, QStringLiteral("volume has no chapters"));
        return;
    }
    if (!m_scraper || !m_nam || !m_index) {
        emit failed(vid, QStringLiteral("packer is missing a scraper, NAM, or index"));
        return;
    }

    auto job = std::make_shared<Job>();
    job->volume      = volume;
    job->seriesTitle = seriesTitle;
    job->volumeId    = vid;
    job->stagingDir  = stagingDirFor(volume);

    QDir(job->stagingDir).removeRecursively();
    if (!QDir().mkpath(job->stagingDir)) {
        emit failed(vid, QStringLiteral("cannot create staging dir"));
        return;
    }

    // Serialize: only one pack runs at a time. If a pack is already active, queue
    // this one (staging is prepared) and start it when the active job reaches a
    // terminal state — so two concurrent volumes never share the scraper's
    // uncorrelated pagesReady emit or clobber each other's m_job.
    if (m_job && !m_job->cancelled) {
        m_queue.append(job);
        return;
    }

    m_job = job;
    startChapter(job);
}

void MangaVolumePacker::advanceQueue()
{
    if (!m_queue.isEmpty()) {
        m_job = m_queue.takeFirst();
        startChapter(m_job);
    } else {
        m_job.reset();
    }
}

void MangaVolumePacker::startChapter(const std::shared_ptr<Job>& job)
{
    if (job->cancelled)
        return;
    if (job->chapterIdx >= job->volume.chapterIds.size()) {
        finalize(job);
        return;
    }

    const QString chapterId = job->volume.chapterIds.at(job->chapterIdx);

    // One connection per chapter; disconnect inside the handler on first fire so a
    // later chapter's fetchPages never cross-talks into this one.
    job->pagesConn = connect(m_scraper, &MangaScraper::pagesReady, this,
        [this, job](const QList<PageInfo>& pages) {
            QObject::disconnect(job->pagesConn);
            onPages(job, pages);
        });

    m_scraper->fetchPages(chapterId);
}

void MangaVolumePacker::onPages(const std::shared_ptr<Job>& job, const QList<PageInfo>& pages)
{
    if (job->cancelled)
        return;
    if (pages.isEmpty()) {
        failJob(job, QStringLiteral("chapter %1 returned no pages")
                         .arg(job->volume.chapterIds.value(job->chapterIdx)));
        return;
    }

    job->knownTotal += pages.size();

    const int     chapterIdx = job->chapterIdx;
    const QString label      = chapterLabel(job->volume.chapterIds.at(chapterIdx), chapterIdx + 1);
    const int     total      = pages.size();

    // Buffer every image by its in-chapter index; write in page order once the
    // whole chapter has arrived, so on-disk names + savedNames stay in page order
    // regardless of network completion order.
    auto bytesByIndex = std::make_shared<QMap<int, QByteArray>>();
    auto extByIndex   = std::make_shared<QMap<int, QString>>();
    auto arrived      = std::make_shared<int>(0);

    for (int p = 0; p < total; ++p) {
        if (job->cancelled)
            return;
        const QString imageUrl = pages.at(p).imageUrl;

        QNetworkRequest req{QUrl(imageUrl)};
        req.setRawHeader("User-Agent", kUserAgent);
        req.setRawHeader("Referer", "https://weebcentral.com/");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply* reply = m_nam->get(req);
        job->replies.insert(reply);

        connect(reply, &QNetworkReply::finished, this,
            [this, job, reply, p, total, chapterIdx, label, imageUrl,
             bytesByIndex, extByIndex, arrived]() {
                job->replies.remove(reply);
                const QNetworkReply::NetworkError err = reply->error();
                const QString    errStr = reply->errorString();
                const QByteArray data   = reply->readAll();
                reply->deleteLater();

                if (job->cancelled)
                    return;
                if (err != QNetworkReply::NoError) {
                    failJob(job, QStringLiteral("page download failed: ") + errStr);
                    return;
                }
                if (!looksLikeImage(data)) {
                    failJob(job, QStringLiteral("downloaded page is not a valid image"));
                    return;
                }

                bytesByIndex->insert(p, data);
                extByIndex->insert(p, extFor(imageUrl, data));
                if (++(*arrived) != total)
                    return;

                // Whole chapter is in and valid — commit its pages in page order.
                for (int i = 0; i < total; ++i) {
                    const QString name = QStringLiteral("c%1_%2.%3")
                        .arg(label)
                        .arg(i + 1, 3, 10, QChar('0'))
                        .arg(extByIndex->value(i));
                    const QString path = job->stagingDir + QChar('/') + name;
                    QFile f(path);
                    if (!f.open(QIODevice::WriteOnly)) {
                        failJob(job, QStringLiteral("cannot write page ") + name);
                        return;
                    }
                    f.write(bytesByIndex->value(i));
                    f.close();
                    job->savedNames.append(name);
                    job->groups.append(chapterIdx);
                    job->done += 1;
                    emit progress(job->volumeId, job->done, job->knownTotal);
                }

                job->chapterIdx += 1;
                startChapter(job);
            });
    }
}

void MangaVolumePacker::finalize(const std::shared_ptr<Job>& job)
{
    if (job->cancelled)
        return;

    VolumeProvenance prov;
    prov.id           = job->volumeId;
    prov.seriesId     = job->volume.seriesId;
    prov.seriesTitle  = job->seriesTitle;   // the SERIES title, not the volume title
    prov.volumeNumber = job->volume.number;
    prov.sourceKind   = QStringLiteral("weebcentral");
    // A clear, always-non-empty release label (the volume title may be empty).
    prov.releaseTitle = QStringLiteral("WeebCentral · Vol ") + job->volume.number;
    prov.chapterIds   = job->volume.chapterIds;

    // The publish path natural-sorts the staging filenames before it renames them
    // to page_NNN, so the per-page group vector it receives must be in that same
    // natural-sorted order. Sort (name, group) pairs with the identical collator so
    // the groups line up no matter what order we happened to save the pages in.
    QList<QPair<QString, int>> pairs;
    pairs.reserve(job->savedNames.size());
    for (int i = 0; i < job->savedNames.size(); ++i)
        pairs.append(qMakePair(job->savedNames.at(i), job->groups.value(i, 0)));
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(pairs.begin(), pairs.end(),
              [&coll](const QPair<QString, int>& a, const QPair<QString, int>& b) {
                  return coll.compare(a.first, b.first) < 0;
              });
    QVector<int> groupVec;
    groupVec.reserve(pairs.size());
    for (const QPair<QString, int>& pr : pairs)
        groupVec.append(pr.second);

    // Record observable results BEFORE publish consumes the staging directory.
    m_lastSavedNames = job->savedNames;
    m_lastGroups     = job->groups;

    const QString vid        = job->volumeId;
    const QString stagingDir = job->stagingDir;
    const QString finalDir   = m_index->pagesDirFor(prov);

    // Atomic hand-off: finalize + publish into the shared index (consumes staging).
    if (!m_ingestor->publish(prov, stagingDir, groupVec)) {
        QDir(stagingDir).removeRecursively();
        emit failed(vid, QStringLiteral("index publish rejected the prepared volume"));
        if (m_job == job)
            advanceQueue();   // terminal — hand off to the next queued volume
        return;
    }

    emit finished(vid, finalDir);
    if (m_job == job)
        advanceQueue();       // terminal — hand off to the next queued volume
}

void MangaVolumePacker::failJob(const std::shared_ptr<Job>& job, const QString& reason)
{
    if (job->cancelled)
        return;            // already torn down / notified
    job->cancelled = true; // latch: stop every further page + chapter for this job
    const QString vid = job->volumeId;
    teardown(job);
    emit failed(vid, reason);
    if (m_job == job)
        advanceQueue();    // terminal — hand off to the next queued volume
}

void MangaVolumePacker::teardown(const std::shared_ptr<Job>& job)
{
    QObject::disconnect(job->pagesConn);
    // Copy first: abort() can re-enter finished() synchronously; disconnecting the
    // reply from us keeps that from re-invoking our per-page handler.
    const QSet<QNetworkReply*> replies = job->replies;
    job->replies.clear();
    for (QNetworkReply* r : replies) {
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    QDir(job->stagingDir).removeRecursively();
}

void MangaVolumePacker::cancel(const QString& volumeId)
{
    const QString vid = volumeId.trimmed();
    // Active job: latch cancel, tear down its staging, and advance to the next
    // queued volume so a cancel never strands the queue.
    if (m_job && m_job->volumeId == vid) {
        auto job = m_job;
        if (job->cancelled)
            return;
        job->cancelled = true;
        teardown(job);
        advanceQueue();
        return;
    }
    // Queued (displaced) job: remove it from the queue and tear down its page-less
    // staging so a queued volume's cancel is actually effective; the active job is
    // untouched and no signal is emitted (same as the active-cancel path).
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i)->volumeId == vid) {
            auto job = m_queue.takeAt(i);
            job->cancelled = true;
            teardown(job);
            return;
        }
    }
}

// Parse a chapter label from an (opaque, in production) chapterId. A trailing
// numeric run is honored only at a non-alphanumeric (or start) boundary, so a
// ULID never yields a bogus number — those fall back to the 1-based volume
// ordinal. "wc-chapter-10" -> "010", "wc-chapter-10.5" -> "010.5".
QString MangaVolumePacker::chapterLabel(const QString& chapterId, int ordinal1Based)
{
    static const QRegularExpression re(
        QStringLiteral("(?:^|[^0-9A-Za-z])(\\d+)(?:\\.(\\d+))?$"));
    const QRegularExpressionMatch m = re.match(chapterId.trimmed());

    QString intPart, frac;
    if (m.hasMatch()) {
        intPart = m.captured(1);
        frac    = m.captured(2);
    } else {
        intPart = QString::number(ordinal1Based);
    }

    bool ok = false;
    const int n = intPart.toInt(&ok);
    QString label = ok ? QStringLiteral("%1").arg(n, 3, 10, QChar('0')) : intPart;
    if (!frac.isEmpty())
        label += QLatin1Char('.') + frac;
    return label;
}

// Extension for a downloaded page: prefer the URL's suffix, else sniff magic bytes,
// else default to jpg. jpeg is normalized to jpg.
QString MangaVolumePacker::extFor(const QString& imageUrl, const QByteArray& bytes)
{
    const QString suf = QFileInfo(QUrl(imageUrl).path()).suffix().toLower();
    static const QSet<QString> known{
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("gif"), QStringLiteral("webp")};
    if (known.contains(suf))
        return suf == QLatin1String("jpeg") ? QStringLiteral("jpg") : suf;

    if (bytes.size() >= 12) {
        const auto u = [&](int i) { return static_cast<unsigned char>(bytes.at(i)); };
        if (u(0) == 0xFF && u(1) == 0xD8 && u(2) == 0xFF) return QStringLiteral("jpg");
        if (u(0) == 0x89 && u(1) == 0x50 && u(2) == 0x4E && u(3) == 0x47) return QStringLiteral("png");
        if (u(0) == 0x47 && u(1) == 0x49 && u(2) == 0x46) return QStringLiteral("gif");
        if (bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP") return QStringLiteral("webp");
    }
    return QStringLiteral("jpg");
}

// A downloaded page must be a REAL image. A soft-block answers an image URL with
// HTTP 200 + homepage HTML; the magic-byte gate rejects that. Accepts the four
// formats WeebCentral serves: JPEG / PNG / GIF / WebP.
bool MangaVolumePacker::looksLikeImage(const QByteArray& d)
{
    if (d.size() < 12)
        return false;
    const auto u = [&](int i) { return static_cast<unsigned char>(d.at(i)); };
    if (u(0) == 0xFF && u(1) == 0xD8 && u(2) == 0xFF) return true;                 // JPEG
    if (u(0) == 0x89 && u(1) == 0x50 && u(2) == 0x4E && u(3) == 0x47) return true; // PNG
    if (u(0) == 0x47 && u(1) == 0x49 && u(2) == 0x46) return true;                 // GIF
    if (d.startsWith("RIFF") && d.mid(8, 4) == "WEBP") return true;               // WebP
    return false;
}

} // namespace MangaTankoban
