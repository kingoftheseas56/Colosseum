#include "PorticoDestinationResolver.h"

#include <QHash>
#include <QUrl>
#include <algorithm>

namespace {

struct Provider {
    QString id;
    QString label;
    QString medium;
    QString searchPrefix;
    bool appOnly = false;
};

const QList<Provider> &providers()
{
    static const QList<Provider> value = {
        {QStringLiteral("netflix"), QStringLiteral("Netflix"), QStringLiteral("watch"),
         QStringLiteral("https://netflix.com/search?q=")},
        {QStringLiteral("prime"), QStringLiteral("Prime Video"), QStringLiteral("watch"),
         QStringLiteral("https://primevideo.com/search?phrase=")},
        {QStringLiteral("hbomax"), QStringLiteral("HBO Max"), QStringLiteral("watch"),
         QStringLiteral("https://play.hbomax.com/search?q=")},
        {QStringLiteral("disney"), QStringLiteral("Disney+"), QStringLiteral("watch"),
         QStringLiteral("https://disneyplus.com/search?q=")},
        {QStringLiteral("appletv"), QStringLiteral("Apple TV"), QStringLiteral("watch"),
         QStringLiteral("https://tv.apple.com/search?term=")},
        {QStringLiteral("crunchyroll"), QStringLiteral("Crunchyroll"), QStringLiteral("watch"),
         QStringLiteral("https://crunchyroll.com/search?q=")},
        {QStringLiteral("youtube"), QStringLiteral("YouTube"), QStringLiteral("watch"),
         QStringLiteral("https://youtube.com/results?search_query=")},
        {QStringLiteral("hulu"), QStringLiteral("Hulu"), QStringLiteral("watch"),
         QStringLiteral("https://hulu.com/search?q=")},
        {QStringLiteral("mubi"), QStringLiteral("MUBI"), QStringLiteral("watch"),
         QStringLiteral("https://mubi.com/search/films?query=")},
        {QStringLiteral("spotify"), QStringLiteral("Spotify"), QStringLiteral("listen"),
         QStringLiteral("https://open.spotify.com/search/")},
        {QStringLiteral("ytmusic"), QStringLiteral("YouTube Music"), QStringLiteral("listen"),
         QStringLiteral("https://music.youtube.com/search?q=")},
        {QStringLiteral("applemusic"), QStringLiteral("Apple Music"), QStringLiteral("listen"),
         QStringLiteral("https://music.apple.com/search?term=")},
        {QStringLiteral("kindle"), QStringLiteral("Kindle"), QStringLiteral("read"),
         QStringLiteral("https://amazon.com/s?i=digital-text&k=")},
        {QStringLiteral("playbooks"), QStringLiteral("Google Play Books"), QStringLiteral("read"),
         QStringLiteral("https://play.google.com/store/search?c=books&q=")},
        {QStringLiteral("mangaplus"), QStringLiteral("MANGA Plus"), QStringLiteral("read"),
         QStringLiteral("https://mangaplus.shueisha.co.jp/search_result?keyword=")},
        {QStringLiteral("viz"), QStringLiteral("VIZ"), QStringLiteral("read"),
         QStringLiteral("https://viz.com/search?search=")},
        {QStringLiteral("webtoon"), QStringLiteral("WEBTOON"), QStringLiteral("read"),
         QStringLiteral("https://webtoons.com/en/search?keyword=")},
        {QStringLiteral("dcui"), QStringLiteral("DC Universe Infinite"), QStringLiteral("read"),
         QStringLiteral("https://dcuniverseinfinite.com/search?q=")},
        {QStringLiteral("marvel"), QStringLiteral("Marvel Unlimited"), QStringLiteral("read"),
         QStringLiteral("https://marvel.com/search?query=")},
        {QStringLiteral("kobo"), QStringLiteral("Kobo"), QStringLiteral("read"), {}, true},
        {QStringLiteral("applebooks"), QStringLiteral("Apple Books"), QStringLiteral("read"), {}, true}
    };
    return value;
}

QString sourceLabel(const QString &sourceId)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("stremio"), QStringLiteral("Stremio")},
        {QStringLiteral("openlibrary"), QStringLiteral("Open Library")},
        {QStringLiteral("anilist"), QStringLiteral("AniList")},
        {QStringLiteral("applemusic"), QStringLiteral("Apple Music")},
        {QStringLiteral("youtube"), QStringLiteral("YouTube")},
        {QStringLiteral("webtoon"), QStringLiteral("WEBTOON")},
        {QStringLiteral("globalcomix"), QStringLiteral("GlobalComix")},
        {QStringLiteral("spotify"), QStringLiteral("Spotify")}
    };
    return labels.value(sourceId, sourceId);
}
QString directProviderForSource(const QString &sourceId)
{
    if (sourceId == QStringLiteral("youtube"))
        return QStringLiteral("ytmusic");
    if (sourceId == QStringLiteral("applemusic"))
        return QStringLiteral("applemusic");
    if (sourceId == QStringLiteral("spotify"))
        return QStringLiteral("spotify");
    if (sourceId == QStringLiteral("webtoon"))
        return QStringLiteral("webtoon");
    return {};
}

QVariantMap door(const QString &providerId, const QString &label,
                 const QString &url, bool exact, bool installed,
                 bool appOnly, const QString &reason)
{
    return {
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("label"), label},
        {QStringLiteral("url"), url},
        {QStringLiteral("exact"), exact},
        {QStringLiteral("installed"), installed},
        {QStringLiteral("appOnly"), appOnly},
        {QStringLiteral("actionable"), !url.isEmpty() && !appOnly},
        {QStringLiteral("reason"), reason}
    };
}

} // namespace

QString PorticoDestinationResolver::mediumForKind(const QString &kind)
{
    const QString value = kind.toLower();
    if (value == QStringLiteral("film") || value == QStringLiteral("series")
        || value == QStringLiteral("anime"))
        return QStringLiteral("watch");
    if (value == QStringLiteral("song") || value == QStringLiteral("album")
        || value == QStringLiteral("artist") || value == QStringLiteral("music-video"))
        return QStringLiteral("listen");
    return QStringLiteral("read");
}
QVariantList PorticoDestinationResolver::destinationsFor(
    const PorticoTrend::Item &item, const QStringList &activeApps) const
{
    QVariantList exactDoors;
    QVariantList searchDoors;
    QVariantList appOnlyDoors;
    const QString medium = mediumForKind(item.kind);
    const QString encodedTitle = QString::fromUtf8(QUrl::toPercentEncoding(item.title));

    const QString directProvider = directProviderForSource(item.sourceId);
    if (!item.canonicalUrl.isEmpty()) {
        const QString providerId = directProvider.isEmpty() ? item.sourceId : directProvider;
        exactDoors.push_back(door(
            providerId, sourceLabel(item.sourceId), item.canonicalUrl, true,
            activeApps.contains(providerId), false, QStringLiteral("source")));
    }

    for (const auto &provider : providers()) {
        if (provider.medium != medium)
            continue;

        const bool installed = activeApps.contains(provider.id);
        QString directUrl;
        if (provider.id == QStringLiteral("spotify")) {
            const QString id = item.externalIds.value(QStringLiteral("spotify")).toString();
            if (!id.isEmpty()) {
                QString entity = item.kind == QStringLiteral("album") ? QStringLiteral("album")
                    : item.kind == QStringLiteral("artist") ? QStringLiteral("artist")
                                                            : QStringLiteral("track");
                directUrl = QStringLiteral("https://open.spotify.com/%1/%2").arg(entity, id);
            }
        } else if (provider.id == QStringLiteral("ytmusic")) {
            const QString id = item.externalIds.value(QStringLiteral("youtube")).toString();
            if (!id.isEmpty())
                directUrl = QStringLiteral("https://music.youtube.com/watch?v=") + id;
        }

        if (!directUrl.isEmpty()) {
            const bool duplicate = !exactDoors.isEmpty()
                && exactDoors.first().toMap().value(QStringLiteral("providerId")).toString() == provider.id;
            if (!duplicate)
                exactDoors.push_back(door(provider.id, provider.label, directUrl, true,
                                          installed, false, QStringLiteral("provider-id")));
            continue;
        }

        if (provider.appOnly) {
            appOnlyDoors.push_back(door(provider.id, provider.label, {}, false,
                                        installed, true, QStringLiteral("app-only")));
            continue;
        }

        QString searchUrl = provider.searchPrefix;
        if (provider.id == QStringLiteral("spotify"))
            searchUrl += encodedTitle;
        else
            searchUrl += encodedTitle;
        searchDoors.push_back(door(provider.id, provider.label, searchUrl, false,
                                   installed, false, QStringLiteral("search")));
    }
    auto installedFirst = [](const QVariant &left, const QVariant &right) {
        const auto l = left.toMap();
        const auto r = right.toMap();
        const bool li = l.value(QStringLiteral("installed")).toBool();
        const bool ri = r.value(QStringLiteral("installed")).toBool();
        if (li != ri)
            return li > ri;
        return l.value(QStringLiteral("label")).toString()
            < r.value(QStringLiteral("label")).toString();
    };
    std::sort(searchDoors.begin(), searchDoors.end(), installedFirst);
    std::sort(appOnlyDoors.begin(), appOnlyDoors.end(), installedFirst);

    QVariantList result = exactDoors;
    result.append(searchDoors);
    result.append(appOnlyDoors);
    return result;
}
