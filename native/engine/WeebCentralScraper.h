#pragma once

#include "MangaScraper.h"

class WeebCentralScraper : public MangaScraper
{
    Q_OBJECT

public:
    explicit WeebCentralScraper(QNetworkAccessManager* nam, QObject* parent = nullptr)
        : MangaScraper(nam, parent) {}

    QString sourceId() const override { return QStringLiteral("weebcentral"); }
    QString sourceName() const override { return QStringLiteral("WeebCentral"); }

    void search(const QString& query, int limit = 60) override;
    void fetchChapters(const QString& seriesId) override;
    void fetchPages(const QString& chapterId) override;
    void fetchDetail(const MangaResult& preview) override;

    // MangaPlus paired-page fetch (double_page_v2). Overrides the base-class
    // default so only WeebCentral uses grouped pages; ReadComics falls back to
    // the flat fetchPages.
    void fetchPagesPaired(const QString& chapterId) override;

    static QList<ChapterInfo> parseChaptersHtmlForTest(const QString& html, const QString& source)
    { return parseChaptersHtml(html, source); }

    static QList<PageInfo> parsePagesPairedHtmlForTest(const QString& html)
    { return parsePagesPairedHtml(html); }

private:
    static QList<MangaResult> parseSearchHtml(const QString& html);
    static QList<ChapterInfo> parseChaptersHtml(const QString& html, const QString& source);
    static QList<PageInfo>    parsePagesHtml(const QString& html);
    static QList<PageInfo>    parsePagesPairedHtml(const QString& html);
};
