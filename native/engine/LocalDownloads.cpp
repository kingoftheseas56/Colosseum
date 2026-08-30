#include "LocalDownloads.h"

#include "MangaDownloader.h"
#include "BookDownloader.h"
#include "ComicDownloader.h"
#include "MangaTankobanService.h"
#include "../player/downloadstore.h"

#include <QMetaObject>
#include <QRegularExpression>
#include <QTimer>

namespace {
QString seriesKeyForEpisodeShow(const QString &seriesTitle) {
    return QStringLiteral("show:") + seriesTitle.toLower().simplified();
}

QString boundedError(const QString &reason) {
    QString out = reason;
    out.remove(QChar::Null);
    out.replace(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f]")),
                QStringLiteral(" "));
    return out.simplified().left(240);
}

QVariantList invokeList(QObject *object, const char *method) {
    if (!object)
        return {};
    QVariantList result;
    if (!QMetaObject::invokeMethod(object, method, Qt::DirectConnection,
                                   Q_RETURN_ARG(QVariantList, result))) {
        return {};
    }
    return result;
}

QString failureTitleFallback(const QString &world, const QString &id) {
    static const QRegularExpression volumeId(
        QStringLiteral("^tankoban:[^:]+:volume:([^:]+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = volumeId.match(id);
    if (match.hasMatch())
        return QStringLiteral("Vol. ") + match.captured(1);

    // A routing-shaped id is an implementation detail, never a humane title.
    if (world == QStringLiteral("tankoban")
            && (id.contains(QStringLiteral("tankoban:"), Qt::CaseInsensitive)
                || id.contains(QStringLiteral(":volume:"), Qt::CaseInsensitive))) {
        return QStringLiteral("Tankoban volume");
    }
    return id;
}
} // namespace

LocalDownloads::LocalDownloads(MangaDownloader *manga, BookDownloader *books,
                               ComicDownloader *comics, DownloadStore *videos,
                               MangaTankobanService *volumes, QObject *parent)
    : QObject(parent), m_manga(manga), m_books(books), m_comics(comics), m_videos(videos),
      m_volumes(volumes) {
    initialize();
}

LocalDownloads::LocalDownloads(MangaDownloader *manga, BookDownloader *books,
                               ComicDownloader *comics, DownloadStore *videos,
                               QObject *volumeSource, QObject *parent)
    : QObject(parent), m_manga(manga), m_books(books), m_comics(comics), m_videos(videos),
      m_volumeSource(volumeSource) {
    initialize();
}

void LocalDownloads::initialize() {
    // Every backend mutation bumps the revision; progress signals are chatty
    // (per page / per chunk), so bumps coalesce through a 400 ms timer.
    m_coalesce = new QTimer(this);
    m_coalesce->setSingleShot(true);
    m_coalesce->setInterval(400);
    connect(m_coalesce, &QTimer::timeout, this, [this]() {
        ++m_revision;
        emit changed();
    });
    auto arm = [this]() { armRevision(); };

    if (m_manga) {
        connect(m_manga, &MangaDownloader::progress, this,
                [this, arm](const QString &id, int, int) {
                    clearFailure(QStringLiteral("tankoban"), id); arm();
                });
        connect(m_manga, &MangaDownloader::finished, this,
                [this, arm](const QString &id) {
                    clearFailure(QStringLiteral("tankoban"), id); arm();
                });
        connect(m_manga, &MangaDownloader::failed, this,
                [this, arm](const QString &id, const QString &reason) {
                    rememberFailure(QStringLiteral("tankoban"), id, reason); arm();
                });
        connect(m_manga, &MangaDownloader::removed, this,
                [this, arm](const QString &id) {
                    clearFailure(QStringLiteral("tankoban"), id); arm();
                });
    }
    if (m_books) {
        connect(m_books, &BookDownloader::resolving, this, arm);
        connect(m_books, &BookDownloader::progress, this,
                [this, arm](const QString &id, double, double) {
                    clearFailure(QStringLiteral("biblio"), id); arm();
                });
        connect(m_books, &BookDownloader::finished, this,
                [this, arm](const QString &id, const QString &) {
                    clearFailure(QStringLiteral("biblio"), id); arm();
                });
        connect(m_books, &BookDownloader::failed, this,
                [this, arm](const QString &id, const QString &reason) {
                    rememberFailure(QStringLiteral("biblio"), id, reason); arm();
                });
        connect(m_books, &BookDownloader::removed, this,
                [this, arm](const QString &id) {
                    clearFailure(QStringLiteral("biblio"), id); arm();
                });
    }
    if (m_comics) {
        connect(m_comics, &ComicDownloader::progress, this,
                [this, arm](const QString &id, double, double) {
                    clearFailure(QStringLiteral("tankoban"), id); arm();
                });
        connect(m_comics, &ComicDownloader::finished, this,
                [this, arm](const QString &id) {
                    clearFailure(QStringLiteral("tankoban"), id); arm();
                });
        connect(m_comics, &ComicDownloader::failed, this,
                [this, arm](const QString &id, const QString &reason) {
                    rememberFailure(QStringLiteral("tankoban"), id, reason); arm();
                });
        connect(m_comics, &ComicDownloader::removed, this,
                [this, arm](const QString &id) {
                    clearFailure(QStringLiteral("tankoban"), id); arm();
                });
    }
    if (m_videos) {
        connect(m_videos, &DownloadStore::changed, this, arm);
        connect(m_videos, &DownloadStore::libraryChanged, this, arm);
    }
    if (m_volumes) {
        connect(m_volumes, &MangaTankobanService::recoveryReadyChanged, this, arm);
        connect(m_volumes, &MangaTankobanService::progress, this,
                [this, arm](const QString &id, double, double) {
                    clearFailure(QStringLiteral("tankoban"), id); arm();
                });
        connect(m_volumes, &MangaTankobanService::finished, this,
                [this, arm](const QString &id) {
                    clearFailure(QStringLiteral("tankoban"), id); arm();
                });
        connect(m_volumes, &MangaTankobanService::failed, this,
                [this, arm](const QString &id, const QString &reason) {
                    rememberFailure(QStringLiteral("tankoban"), id, reason); arm();
                });
        connect(m_volumes, &MangaTankobanService::removed, this,
                [this, arm](const QString &id) {
                     clearFailure(QStringLiteral("tankoban"), id); arm();
                 });
    }
    if (m_volumeSource) {
        connect(m_volumeSource, SIGNAL(failed(QString,QString)),
                this, SLOT(onTestVolumeFailed(QString,QString)));
    }
}

void LocalDownloads::armRevision() {
    if (m_coalesce && !m_coalesce->isActive())
        m_coalesce->start();
}

void LocalDownloads::onTestVolumeFailed(const QString &id, const QString &reason) {
    rememberFailure(QStringLiteral("tankoban"), id, reason);
    armRevision();
}

void LocalDownloads::bump() {
    ++m_revision;
    emit changed();
}

QString LocalDownloads::failureKey(const QString &world, const QString &id) {
    return world + QLatin1Char(':') + id;
}

void LocalDownloads::rememberFailure(const QString &world, const QString &id,
                                     const QString &reason) {
    QVariantMap row;
    const QVariantList current = activeJobs();
    for (const QVariant &value : current) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("world")).toString() == world
                && candidate.value(QStringLiteral("id")).toString() == id) {
            // MangaTankobanService keeps the failed acquisition in m_acq long
            // enough for this signal. Its failed-state row is therefore the
            // authoritative title source; excluding it loses the human label
            // and falls through to the internal routing id.
            row = candidate;
            break;
        }
    }
    row.insert(QStringLiteral("world"), world);
    row.insert(QStringLiteral("id"), id);
    if (row.value(QStringLiteral("title")).toString().isEmpty())
        row.insert(QStringLiteral("title"), failureTitleFallback(world, id));
    row.insert(QStringLiteral("state"), QStringLiteral("failed"));
    row.insert(QStringLiteral("error"), boundedError(reason));
    row.insert(QStringLiteral("canPlay"), false);
    row.insert(QStringLiteral("canRetry"), false);
    row.insert(QStringLiteral("canPause"), false);
    row.insert(QStringLiteral("canResume"), false);
    row.insert(QStringLiteral("canCancel"), false);
    row.insert(QStringLiteral("canDismiss"), true);
    m_failures.insert(failureKey(world, id), row);
}

void LocalDownloads::clearFailure(const QString &world, const QString &id) {
    m_failures.remove(failureKey(world, id));
}

void LocalDownloads::dismissFailure(const QString &world, const QString &id) {
    if (m_failures.remove(failureKey(world, id)))
        bump();
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
    // Tankoban volume mode: whole manga volumes ingested by MangaTankobanService.
    // Same series bucket as the series' chapters ("manga:<seriesId>") so a series
    // downloaded both ways stays ONE card.
    if (m_volumes) {
        const QVariantList vols = m_volumes->downloadedVolumes();
        for (const QVariant &v : vols) {
            QVariantMap e = v.toMap();
            e.insert(QStringLiteral("world"), QStringLiteral("tankoban"));
            e.insert(QStringLiteral("kind"), QStringLiteral("manga"));
            e.insert(QStringLiteral("seriesKey"),
                     QStringLiteral("manga:") + e.value(QStringLiteral("seriesId")).toString());
            e.insert(QStringLiteral("title"), e.value(QStringLiteral("label")).toString());
            e.insert(QStringLiteral("subtitle"),
                     QStringLiteral("%1 pages · volume").arg(e.value(QStringLiteral("pages")).toInt()));
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
        // Cluster by author when the engine knows one (new entries persist it);
        // otherwise each book stands as its own card. Honest, never guessed.
        const QString author = e.value(QStringLiteral("author")).toString();
        if (!author.isEmpty()) {
            e.insert(QStringLiteral("seriesKey"),
                     QStringLiteral("author:") + author.toLower().simplified());
            e.insert(QStringLiteral("seriesTitle"), author);
        } else {
            e.insert(QStringLiteral("seriesKey"),
                     QStringLiteral("book:") + e.value(QStringLiteral("id")).toString());
            e.insert(QStringLiteral("seriesTitle"), e.value(QStringLiteral("title")).toString());
        }
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
                {QStringLiteral("updatedAt"), 0.0},
                {QStringLiteral("art"), QString()}
            });
        }
        QVariantMap &s = it.value();
        if (s.value(QStringLiteral("art")).toString().isEmpty())
            s[QStringLiteral("art")] = e.value(QStringLiteral("art")).toString();
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
    if (m_volumes || m_volumeSource) {
        const QVariantList jobs = m_volumes
            ? m_volumes->activeVolumeJobs()
            : invokeList(m_volumeSource, "activeVolumeJobs");
        for (const QVariant &v : jobs) {
            QVariantMap j = v.toMap();
            const double done = j.value(QStringLiteral("done")).toDouble();
            const double total = j.value(QStringLiteral("total")).toDouble();
            const QString title = j.contains(QStringLiteral("title"))
                ? j.value(QStringLiteral("title")).toString()
                : QStringLiteral("%1 — %2")
                    .arg(j.value(QStringLiteral("seriesTitle")).toString(),
                         j.value(QStringLiteral("label")).toString());
            out.append(QVariantMap{
                {QStringLiteral("world"), QStringLiteral("tankoban")},
                {QStringLiteral("id"), j.value(QStringLiteral("id"))},
                {QStringLiteral("seriesTitle"), j.value(QStringLiteral("seriesTitle"))},
                {QStringLiteral("title"), title},
                {QStringLiteral("state"), j.value(QStringLiteral("state"))},
                {QStringLiteral("ratio"), total > 0 ? done / total : 0.0},
                // received/total (bytes): the Downloads page's group progress bar
                // (DownloadsPage.qml, hasKnownTotal) sums these across a group's rows — without
                // them a grouped batch renders with a correct title but no bar and "0 of N
                // landed" against real work in flight (2026-08-05 grouping design).
                {QStringLiteral("received"), done},
                {QStringLiteral("total"), total},
                {QStringLiteral("groupKey"), j.value(QStringLiteral("groupKey"))},
                {QStringLiteral("groupUnit"), j.value(QStringLiteral("groupUnit"))},
                {QStringLiteral("badge"), j.value(QStringLiteral("badge"))},
                {QStringLiteral("detail"), total > 0
                    ? QStringLiteral("%1 of %2 MB").arg(done / 1048576.0, 0, 'f', 0)
                                                   .arg(total / 1048576.0, 0, 'f', 0)
                    : QString()}
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
                {QStringLiteral("seriesTitle"), j.value(QStringLiteral("seriesTitle"))},
                {QStringLiteral("title"), QStringLiteral("%1 — %2")
                    .arg(j.value(QStringLiteral("seriesTitle")).toString(),
                         j.value(QStringLiteral("label")).toString())},
                {QStringLiteral("state"), j.value(QStringLiteral("state"))},
                {QStringLiteral("ratio"), total > 0 ? done / total : 0.0},
                {QStringLiteral("received"), done},
                {QStringLiteral("total"), total},
                {QStringLiteral("groupKey"), j.value(QStringLiteral("groupKey"))},
                {QStringLiteral("groupUnit"), j.value(QStringLiteral("groupUnit"))},
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
        const QVariantList vjobs = m_videos->jobs();
        for (const QVariant &v : vjobs) {
            QVariantMap j = v.toMap();
            const double received = j.value(QStringLiteral("received")).toDouble();
            const double total = j.value(QStringLiteral("total")).toDouble();
            out.append(QVariantMap{
                {QStringLiteral("world"), QStringLiteral("theatre")},
                {QStringLiteral("id"), j.value(QStringLiteral("id"))},
                {QStringLiteral("groupKey"), j.value(QStringLiteral("groupKey"))},
                {QStringLiteral("kind"), j.value(QStringLiteral("kind"))},
                {QStringLiteral("title"), j.value(QStringLiteral("title"))},
                {QStringLiteral("subtitle"), j.value(QStringLiteral("subtitle"))},
                {QStringLiteral("seriesTitle"), j.value(QStringLiteral("seriesTitle"))},
                {QStringLiteral("season"), j.value(QStringLiteral("season"))},
                {QStringLiteral("episode"), j.value(QStringLiteral("episode"))},
                // play-while-arriving (2026-07-20): url + art ride to the page so a
                // live row can open the player on the same source it's pulling.
                {QStringLiteral("art"), j.value(QStringLiteral("art"))},
                {QStringLiteral("url"), j.value(QStringLiteral("url"))},
                {QStringLiteral("state"), j.value(QStringLiteral("state"))},
                {QStringLiteral("error"), j.value(QStringLiteral("error"))},
                {QStringLiteral("canRetry"), j.value(QStringLiteral("state")).toString()
                                                 == QStringLiteral("failed")},
                {QStringLiteral("ratio"), j.value(QStringLiteral("ratio")).toDouble()},
                {QStringLiteral("received"), j.value(QStringLiteral("received"))},
                {QStringLiteral("total"), j.value(QStringLiteral("total"))},
                {QStringLiteral("speed"), j.value(QStringLiteral("speed"))},
                {QStringLiteral("etaSec"), j.value(QStringLiteral("etaSec"))},
                {QStringLiteral("detail"), total > 0
                    ? QStringLiteral("%1 of %2 MB").arg(received / 1048576.0, 0, 'f', 0)
                                                   .arg(total / 1048576.0, 0, 'f', 0)
                    : QString()}
            });
        }
    }
    for (int i = 0; i < out.size(); ++i) {
        QVariantMap row = out[i].toMap();
        const QString world = row.value(QStringLiteral("world")).toString();
        const QString state = row.value(QStringLiteral("state")).toString();
        const bool theatre = world == QStringLiteral("theatre");
        row.insert(QStringLiteral("canPlay"), theatre
            && !row.value(QStringLiteral("url")).toString().isEmpty()
            && state != QStringLiteral("failed"));
        row.insert(QStringLiteral("canRetry"), theatre && state == QStringLiteral("failed"));
        row.insert(QStringLiteral("canPause"), theatre && state == QStringLiteral("downloading"));
        row.insert(QStringLiteral("canResume"), theatre && state == QStringLiteral("paused"));
        row.insert(QStringLiteral("canCancel"),
                   state != QStringLiteral("failed") && state != QStringLiteral("done"));
        row.insert(QStringLiteral("canDismiss"), false);
        out[i] = row;
    }
    for (const QVariantMap &failure : m_failures)
        out.append(failure);
    return out;
}

void LocalDownloads::retry(const QString &world, const QString &id) {
    if (world == QStringLiteral("theatre") && m_videos)
        m_videos->retryJob(id);
    // Other worlds: their engines discard failure payloads today - no blind retry.
}

void LocalDownloads::pause(const QString &world, const QString &id) {
    if (world == QStringLiteral("theatre") && m_videos)
        m_videos->pauseJob(id);
    // Other worlds' engines have no pause yet - honest no-op, no dead buttons drawn.
}

void LocalDownloads::resume(const QString &world, const QString &id) {
    if (world == QStringLiteral("theatre") && m_videos)
        m_videos->resumeJob(id);
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
        {QStringLiteral("active"), [this]() {
            int live = 0;
            const QVariantList aj = activeJobs();
            for (const QVariant &v : aj)
                if (v.toMap().value(QStringLiteral("state")).toString()
                        != QStringLiteral("done"))
                    ++live;
            return live;
        }()}
    };
}

void LocalDownloads::cancel(const QString &world, const QString &id) {
    if (world == QStringLiteral("tankoban")) {
        // Volume-mode ids own the "tankoban:" namespace (VolumeRecord.id).
        if (m_volumes && id.startsWith(QStringLiteral("tankoban:"))) {
            m_volumes->cancel(id);
            return;
        }
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
        // Exact-row discipline: drop the job the user clicked (active, queued,
        // or failed) - NEVER the no-arg active-job cancel (that call is the
        // player panel's own button and killed live downloads from here).
        if (m_videos)
            m_videos->cancelJob(id);
    }
}

QVariantMap LocalDownloads::remove(const QString &world, const QString &id) {
    if (world == QStringLiteral("tankoban")) {
        if (m_volumes && id.startsWith(QStringLiteral("tankoban:"))) {
            return m_volumes->remove(id);
        }
        if (m_manga && m_manga->isDownloaded(id)) {
            return m_manga->deleteChapter(id);
        }
        if (m_comics)
            return m_comics->deleteIssue(id);
    } else if (world == QStringLiteral("biblio")) {
        if (m_books)
            return m_books->deleteBook(id);
    } else if (world == QStringLiteral("theatre")) {
        if (m_videos)
            return m_videos->removeVideo(id);
    }
    return {
        {QStringLiteral("success"), false},
        {QStringLiteral("message"), QStringLiteral("This download could not be found.")}
    };
}
