#pragma once

#include "MangaResult.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QList>

// Detail-page payload returned by MangaScraper::fetchDetail.
// Decoupled from MangaResult (the search preview) so that
// preview cards stay cheap while detail-page hero gets the full
// metadata. Per brainstorm-md §12 (Codex pass).
struct MangaSeriesDetail {
    MangaResult preview;       // copy of the search-time preview
    QString     synopsis;
    QStringList genres;
    QString     year;
    QString     status;        // "ongoing" | "completed" | "hiatus" | etc.
    QString     author;        // may already be in preview.author; canonicalise on consume
    QString     heroCoverUrl;  // larger/higher-res cover if the source serves one
    QString     sourceUrl;     // detail page URL on the source site
    QList<ChapterInfo> cachedChapters; // only populated if fetchDetail's HTTP response naturally included the chapter list

    QJsonObject toJson() const;
    static MangaSeriesDetail fromJson(const QJsonObject& j);
};
