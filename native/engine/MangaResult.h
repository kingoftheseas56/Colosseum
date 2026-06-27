#pragma once

#include <QString>
#include <QMetaType>

struct MangaResult {
    QString id;
    QString url;
    QString title;
    QString author;
    QString thumbnailUrl;
    QString source;       // "weebcentral" or "readcomicsonline"
    QString status;       // "Ongoing", "Completed", etc.
    QString type;         // "manga" or "comic"
};
Q_DECLARE_METATYPE(MangaResult)

struct ChapterInfo {
    QString id;
    QString url;
    QString name;
    double  chapterNumber = 0.0;
    qint64  dateUpload    = 0;   // ms epoch
    QString source;
    bool    isVolumeScanned = false;  // WeebCentral violet tick: chapter is from a volume scan
};
Q_DECLARE_METATYPE(ChapterInfo)

struct PageInfo {
    int     index = 0;
    QString imageUrl;
    // MangaPlus (double_page_v2) facing-pair number. Pages sharing a pageGroup
    // are one two-page view; the packer stitches a 2-image group into one wide
    // spread. -1 = ungrouped (long_strip / flat fetch path).
    int     pageGroup = -1;
};
Q_DECLARE_METATYPE(PageInfo)

// Maps a raw scraper source key (e.g. "weebcentral") to its user-facing
// display name (e.g. "WeebCentral"). Keeps UI strings consistent across
// TankoyomiPage / MangaResultsGrid / AddMangaDialog without a per-site map.
// Unknown keys fall through to the raw value so nothing vanishes.
inline QString mangaSourceDisplayName(const QString& key)
{
    if (key == QLatin1String("weebcentral"))      return QStringLiteral("WeebCentral");
    if (key == QLatin1String("readcomicsonline")) return QStringLiteral("ReadComicsOnline");
    if (key == QLatin1String("readallcomics"))    return QStringLiteral("ReadAllComics");
    return key;
}
