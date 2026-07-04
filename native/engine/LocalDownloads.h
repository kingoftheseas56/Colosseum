#pragma once

// LocalDownloads — the unified read-model behind the Downloads page.
// Normalizes all four download backbones (MangaDownloader, BookDownloader,
// ComicDownloader, player DownloadStore) into one world → series → item shape
// so QML only renders. It owns NO files and NO network: every action routes to
// the owning backend. Progress (resume) is deliberately NOT consulted here —
// downloads answer "what exists locally", not "where do I resume".
// Design: chatgpt_requests/20260629-171426-…-plan-review/response.md (ratified),
// layout ratified 2026-07-04 (agents/colosseum-downloads-mock.html).

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class MangaDownloader;
class BookDownloader;
class ComicDownloader;
class DownloadStore;

class LocalDownloads : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QVariantMap totals READ totals NOTIFY changed)

public:
    LocalDownloads(MangaDownloader *manga, BookDownloader *books,
                   ComicDownloader *comics, DownloadStore *videos,
                   QObject *parent = nullptr);

    int revision() const { return m_revision; }
    QVariantMap totals() const;

    // worlds: "tankoban" | "biblio" | "theatre"
    Q_INVOKABLE QVariantList series(const QString &world) const;
    Q_INVOKABLE QVariantList items(const QString &world, const QString &seriesKey) const;
    Q_INVOKABLE QVariantList activeJobs() const;    // cross-world, for the Now-Arriving strip

    Q_INVOKABLE void cancel(const QString &world, const QString &id);
    Q_INVOKABLE void remove(const QString &world, const QString &id);
    Q_INVOKABLE void retry(const QString &world, const QString &id);   // theatre only in v1

signals:
    void changed();

private:
    void bump();
    QVariantList tankobanItems() const;   // manga chapters + western issues, one lane
    QVariantList biblioItems() const;
    QVariantList theatreItems() const;
    QVariantList itemsForWorld(const QString &world) const;

    MangaDownloader *m_manga = nullptr;
    BookDownloader *m_books = nullptr;
    ComicDownloader *m_comics = nullptr;
    DownloadStore *m_videos = nullptr;
    int m_revision = 0;
};
