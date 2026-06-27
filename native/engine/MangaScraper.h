#pragma once

#include "MangaResult.h"
#include "MangaSeriesDetail.h"
#include <QObject>
#include <QList>

class QNetworkAccessManager;

class MangaScraper : public QObject
{
    Q_OBJECT

public:
    explicit MangaScraper(QNetworkAccessManager* nam, QObject* parent = nullptr)
        : QObject(parent), m_nam(nam) {}

    virtual QString sourceId() const = 0;
    virtual QString sourceName() const = 0;

    virtual void search(const QString& query, int limit = 60) = 0;
    virtual void fetchChapters(const QString& seriesId) = 0;
    virtual void fetchPages(const QString& chapterId) = 0;

    // Fetch pages already grouped into MangaPlus facing-pairs (PageInfo.pageGroup
    // set). Default falls back to the flat fetchPages for scrapers that have no
    // paired endpoint (e.g. ReadComics). Result still arrives via pagesReady().
    virtual void fetchPagesPaired(const QString& chapterId) { fetchPages(chapterId); }

    // NEW (v1 merger): fetch detail-page hero metadata (synopsis,
    // genres, year, status, hero cover URL) given a search-time
    // preview. Result delivered via detailReady(). Concrete scrapers
    // SHOULD also populate cachedChapters if their detail page
    // already returns the chapter list, so the detail view can
    // skip a separate fetchChapters() round-trip.
    virtual void fetchDetail(const MangaResult& preview) = 0;

signals:
    void searchFinished(const QList<MangaResult>& results);
    void chaptersReady(const QList<ChapterInfo>& chapters);
    void pagesReady(const QList<PageInfo>& pages);
    void errorOccurred(const QString& message);

    // NEW (v1 merger): emitted when fetchDetail completes.
    void detailReady(const MangaSeriesDetail& detail);

protected:
    QNetworkAccessManager* m_nam;
};
