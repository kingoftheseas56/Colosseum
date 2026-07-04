#include "LocalDownloads.h"

#include "MangaDownloader.h"
#include "BookDownloader.h"
#include "ComicDownloader.h"
#include "../player/downloadstore.h"

#include <QTimer>

namespace {
QString seriesKeyForEpisodeShow(const QString &seriesTitle) {
    return QStringLiteral("show:") + seriesTitle.toLower().simplified();
}
} // namespace

LocalDownloads::LocalDownloads(MangaDownloader *manga, BookDownloader *books,
                               ComicDownloader *comics, DownloadStore *videos,
                               QObject *parent)
    : QObject(parent), m_manga(manga), m_books(books), m_comics(comics), m_videos(videos) {
    // Every backend mutation bumps the revision; progress signals are chatty
    // (per page / per chunk), so bumps coalesce through a 400 ms timer.
    auto *coalesce = new QTimer(this);
    coalesce->setSingleShot(true);
    coalesce->setInterval(400);
    connect(coalesce, &QTimer::timeout, this, [this]() {
        ++m_revision;
        emit changed();
    });
    auto arm = [coalesce]() { if (!coalesce->isActive()) coalesce->start(); };

    if (m_manga) {
        connect(m_manga, &MangaDownloader::progress, this, arm);
        connect(m_manga, &MangaDownloader::finished, this, arm);
        connect(m_manga, &MangaDownloader::failed, this, arm);
        connect(m_manga, &MangaDownloader::removed, this, arm);
    }
    if (m_books) {
        connect(m_books, &BookDownloader::resolving, this, arm);
        connect(m_books, &BookDownloader::progress, this, arm);
        connect(m_books, &BookDownloader::finished, this, arm);
        connect(m_books, &BookDownloader::failed, this, arm);
        connect(m_books, &BookDownloader::removed, this, arm);
    }
    if (m_comics) {
        connect(m_comics, &ComicDownloader::progress, this, arm);
        connect(m_comics, &ComicDownloader::finished, this, arm);
        connect(m_comics, &ComicDownloader::failed, this, arm);
        connect(m_comics, &ComicDownloader::removed, this, arm);
    }
    if (m_videos) {
        connect(m_videos, &DownloadStore::changed, this, arm);
        connect(m_videos, &DownloadStore::libraryChanged, this, arm);
    }
}

void LocalDownloads::bump() {
    ++m_revision;
    emit changed();
}

// ── item normalization: every backend row becomes the same shape ──
// { id, world, kind, seriesKey, seriesTitle, title, subtitle, bytes, addedAt,
//   missing, pages?, path?, season?, episode? }

QVariantList LocalDownloads::tankobanItems() const {
    QVariantList out;
    if (m_manga) {
        const QVariantList chapters = m_manga->downloadedChapters();
        for (const QVariant &v : chapters) {
            QVariantMap e = v.toMap();
            e.insert(QStringLiteral("world"), QStringLiteral("tankoban"));
            e.insert(QStringLiteral("kind"), QStringLiteral("manga"));
            e.insert(QStringLiteral("seriesKey"),
                     QStringLiteral("manga:") + e.value(QStringLiteral("seriesId")).toString());
            e.insert(QStringLiteral("title"), e.value(QStringLiteral("label")).toString());
            e.insert(QStringLiteral("subtitle"),
                     QStringLiteral("%1 pages").arg(e.value(QStringLiteral("pages")).toInt()));
            out.append(e);
        }
    }
    if (m_comics) {
        const QVariantList issues = m_comics->downloadedIssues();
        for (const QVariant &v : issues) {
            QVariantMap e = v.toMap();
            e.insert(QStringLiteral("world"), QStringLiteral("tankoban"));
            e.insert(QStringLiteral("kind"), QStringLiteral("comic"));
            e.insert(QStringLiteral("seriesKey"),
                     QStringLiteral("comic:") + e.value(QStringLiteral("seriesId")).toString());
            e.insert(QStringLiteral("title"), e.value(QStringLiteral("label")).toString());
            e.insert(QStringLiteral("subtitle"),
                     QStringLiteral("%1 pages · western").arg(e.value(QStringLiteral("pages")).toInt()));
            out.append(e);
        }
    }
    return out;
}

QVariantList LocalDownloads::biblioItems() const {
    QVariantList out;
    if (!m_books)
        return out;
    const QVariantList books = m_books->downloadedBooks();
    for (const QVariant &v : books) {
        QVariantMap e = v.toMap();
        e.insert(QStringLiteral("world"), QStringLiteral("biblio"));
        e.insert(QStringLiteral("kind"), QStringLiteral("book"));
        // Book meta is thin (md5 + title): each book stands as its own series
        // card until richer author/series meta exists. Honest, not clustered.
        e.insert(QStringLiteral("seriesKey"),
                 QStringLiteral("book:") + e.value(QStringLiteral("id")).toString());
        e.insert(QStringLiteral("seriesTitle"), e.value(QStringLiteral("title")).toString());
        const QString path = e.value(QStringLiteral("path")).toString();
        const QString ext = path.section(QLatin1Char('.'), -1).toUpper();
        e.insert(QStringLiteral("subtitle"), ext.size() <= 5 ? ext : QString());
        out.append(e);
    }
    return out;
}

QVariantList LocalDownloads::theatreItems() const {
    QVariantList out;
    if (!m_videos)
        return out;
    const QVariantList vids = m_videos->downloadedVideos();
    for (const QVariant &v : vids) {
        QVariantMap e = v.toMap();
        e.insert(QStringLiteral("world"), QStringLiteral("theatre"));
        const bool episode = e.value(QStringLiteral("kind")).toString() == QStringLiteral("episode");
        const QString seriesTitle = e.value(QStringLiteral("seriesTitle")).toString();
        e.insert(QStringLiteral("seriesKey"),
                 episode && !seriesTitle.isEmpty()
                     ? seriesKeyForEpisodeShow(seriesTitle)
                     : QStringLiteral("movie:") + e.value(QStringLiteral("id")).toString());
        if (!episode)
            e.insert(QStringLiteral("seriesTitle"), e.value(QStringLiteral("title")).toString());
        if (episode) {
            const int s = e.value(QStringLiteral("season")).toInt();
            const int ep = e.value(QStringLiteral("episode")).toInt();
            if (s > 0 || ep > 0)
                e.insert(QStringLiteral("subtitle"),
                         QStringLiteral("S%1E%2").arg(s, 2, 10, QLatin1Char('0')).arg(ep, 2, 10, QLatin1Char('0')));
        }
        out.append(e);
    }
    return out;
}

QVariantList LocalDownloads::itemsForWorld(const QString &world) const {
    if (world == QStringLiteral("tankoban")) return tankobanItems();
    if (world == QStringLiteral("biblio")) return biblioItems();
    if (world == QStringLiteral("theatre")) return theatreItems();
    return {};
}

QVariantList LocalDownloads::series(const QString &world) const {
    // Aggregate items by seriesKey, newest activity first.
    QHash<QString, QVariantMap> agg;
    QStringList order;
    const QVariantList all = itemsForWorld(world);
    for (const QVariant &v : all) {
        const QVariantMap e = v.toMap();
        const QString key = e.value(QStringLiteral("seriesKey")).toString();
        auto it = agg.find(key);
        if (it == agg.end()) {
            order.append(key);
            it = agg.insert(key, QVariantMap{
                {QStringLiteral("key"), key},
                {QStringLiteral("world"), world},
                {QStringLiteral("title"), e.value(QStringLiteral("seriesTitle"))},
                {QStringLiteral("kind"), e.value(QStringLiteral("kind"))},
                {QStringLiteral("itemCount"), 0},
                {QStringLiteral("bytes"), 0.0},
                {QStringLiteral("updatedAt"), 0.0}
            });
        }
        QVariantMap &s = it.value();
        s[QStringLiteral("itemCount")] = s.value(QStringLiteral("itemCount")).toInt() + 1;
        s[QStringLiteral("bytes")] = s.value(QStringLiteral("bytes")).toDouble()
                                     + e.value(QStringLiteral("bytes")).toDouble();
        s[QStringLiteral("updatedAt")] = qMax(s.value(QStringLiteral("updatedAt")).toDouble(),
                                              e.value(QStringLiteral("addedAt")).toDouble());
    }
    QVariantList out;
    for (const QString &key : order)
        out.append(agg.value(key));
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("updatedAt")).toDouble()
             > b.toMap().value(QStringLiteral("updatedAt")).toDouble();
    });
    return out;
}

QVariantList LocalDownloads::items(const QString &world, const QString &seriesKey) const {
    QVariantList out;
    const QVariantList all = itemsForWorld(world);
    for (const QVariant &v : all)
        if (v.toMap().value(QStringLiteral("seriesKey")).toString() == seriesKey)
            out.append(v);
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("addedAt")).toDouble()
             > b.toMap().value(QStringLiteral("addedAt")).toDouble();
    });
    return out;
}

QVariantList LocalDownloads::activeJobs() const {
    QVariantList out;
    if (m_manga) {
        const QVariantList jobs = m_manga->activeChapterJobs();
        for (const QVariant &v : jobs) {
            QVariantMap j = v.toMap();
            const int done = j.value(QStringLiteral("done")).toInt();
            const int total = j.value(QStringLiteral("total")).toInt();
            out.append(QVariantMap{
                {QStringLiteral("world"), QStringLiteral("tankoban")},
                {QStringLiteral("id"), j.value(QStringLiteral("id"))},
                {QStringLiteral("title"), QStringLiteral("%1 — %2")
                    .arg(j.value(QStringLiteral("seriesTitle")).toString(),
                         j.value(QStringLiteral("label")).toString())},
                {QStringLiteral("state"), j.value(QStringLiteral("state"))},
                {QStringLiteral("ratio"), total > 0 ? double(done) / double(total) : 0.0},
                {QStringLiteral("detail"), total > 0
                    ? QStringLiteral("%1 of %2 pages").arg(done).arg(total) : QString()}
            });
        }
    }
    if (m_comics) {
        const QVariantList jobs = m_comics->activeIssueJobs();
        for (const QVariant &v : jobs) {
            QVariantMap j = v.toMap();
            const double done = j.value(QStringLiteral("done")).toDouble();
            const double total = j.value(QStringLiteral("total")).toDouble();
            out.append(QVariantMap{
                {QStringLiteral("world"), QStringLiteral("tankoban")},
                {QStringLiteral("id"), j.value(QStringLiteral("id"))},
                {QStringLiteral("title"), QStringLiteral("%1 — %2")
                    .arg(j.value(QStringLiteral("seriesTitle")).toString(),
                         j.value(QStringLiteral("label")).toString())},
                {QStringLiteral("state"), j.value(QStringLiteral("state"))},
                {QStringLiteral("ratio"), total > 0 ? done / total : 0.0},
                {QStringLiteral("detail"), total > 0
                    ? QStringLiteral("%1 of %2 MB").arg(done / 1048576.0, 0, 'f', 0)
                                                   .arg(total / 1048576.0, 0, 'f', 0)
                    : QString()}
            });
        }
    }
    if (m_books) {
        const QVariantList jobs = m_books->activeBookJobs();
        for (const QVariant &v : jobs) {
            QVariantMap j = v.toMap();
            const double done = j.value(QStringLiteral("done")).toDouble();
            const double total = j.value(QStringLiteral("total")).toDouble();
            out.append(QVariantMap{
                {QStringLiteral("world"), QStringLiteral("biblio")},
                {QStringLiteral("id"), j.value(QStringLiteral("id"))},
                {QStringLiteral("title"), j.value(QStringLiteral("title"))},
                {QStringLiteral("state"), j.value(QStringLiteral("state"))},
                {QStringLiteral("ratio"), total > 0 ? done / total : 0.0},
                {QStringLiteral("detail"), total > 0
                    ? QStringLiteral("%1 of %2 MB").arg(done / 1048576.0, 0, 'f', 1)
                                                   .arg(total / 1048576.0, 0, 'f', 1)
                    : QString()}
            });
        }
    }
    if (m_videos) {
        const QVariantMap st = m_videos->status();
        const QString kind = st.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("preparing") || kind == QStringLiteral("downloading")) {
            const double received = st.value(QStringLiteral("receivedBytes")).toDouble();
            const double total = st.value(QStringLiteral("totalBytes")).toDouble();
            out.append(QVariantMap{
                {QStringLiteral("world"), QStringLiteral("theatre")},
                {QStringLiteral("id"), st.value(QStringLiteral("id"), QStringLiteral("live")).toString()},
                {QStringLiteral("title"), st.value(QStringLiteral("title"),
                                                   QStringLiteral("Video download")).toString()},
                {QStringLiteral("state"), QStringLiteral("downloading")},
                {QStringLiteral("ratio"), st.value(QStringLiteral("ratio")).toDouble()},
                {QStringLiteral("detail"), total > 0
                    ? QStringLiteral("%1 of %2 MB").arg(received / 1048576.0, 0, 'f', 0)
                                                   .arg(total / 1048576.0, 0, 'f', 0)
                    : QString()}
            });
        }
    }
    return out;
}

QVariantMap LocalDownloads::totals() const {
    const QVariantList t = tankobanItems();
    const QVariantList b = biblioItems();
    const QVariantList th = theatreItems();
    double bytes = 0;
    for (const QVariantList *lane : {&t, &b, &th})
        for (const QVariant &v : *lane)
            bytes += v.toMap().value(QStringLiteral("bytes")).toDouble();
    return {
        {QStringLiteral("items"), t.size() + b.size() + th.size()},
        {QStringLiteral("tankoban"), t.size()},
        {QStringLiteral("biblio"), b.size()},
        {QStringLiteral("theatre"), th.size()},
        {QStringLiteral("bytes"), bytes},
        {QStringLiteral("active"), activeJobs().size()}
    };
}

void LocalDownloads::cancel(const QString &world, const QString &id) {
    if (world == QStringLiteral("tankoban")) {
        if (m_manga && m_manga->statusOf(id).value(QStringLiteral("state")).toString()
                != QStringLiteral("none")) {
            m_manga->cancelDownload(id);
            return;
        }
        if (m_comics)
            m_comics->cancelDownload(id);
    } else if (world == QStringLiteral("biblio")) {
        if (m_books)
            m_books->cancelDownload(id);
    } else if (world == QStringLiteral("theatre")) {
        if (m_videos)
            m_videos->cancelDownload();
    }
}

void LocalDownloads::remove(const QString &world, const QString &id) {
    if (world == QStringLiteral("tankoban")) {
        if (m_manga && m_manga->isDownloaded(id)) {
            m_manga->deleteChapter(id);
            return;
        }
        if (m_comics)
            m_comics->deleteIssue(id);
    } else if (world == QStringLiteral("biblio")) {
        if (m_books)
            m_books->deleteBook(id);
    } else if (world == QStringLiteral("theatre")) {
        if (m_videos)
            m_videos->removeVideo(id);
    }
    bump();
}
