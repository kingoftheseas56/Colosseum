#include "TheatreFeed.h"
#include "ActionRegistry.h"
#include "FeedValue.h"
#include "../ColosseumWebBridge.h"

#include "../../ProgressStore.h"
#include "../../engine/ImdbCatalog.h"
#include "../../engine/MalCatalog.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <climits>
#include <algorithm>
#include <utility>

namespace {
bool visible(const QVariantMap &row, bool showExplicit)
{
    // ExplicitContentPolicy.js:visible. Only source adult flags and exact
    // sexual classifications are excluded, never age ratings or title text.
    if (showExplicit) return true;
    if (row.value(QStringLiteral("explicit")).toBool()
        || row.value(QStringLiteral("behaviorHints")).toMap()
               .value(QStringLiteral("adult")).toBool()) return false;
    static const QSet<QString> blocked{QStringLiteral("hentai"),
        QStringLiteral("erotica"), QStringLiteral("pornography"),
        QStringLiteral("sexually explicit"), QStringLiteral("adult film")};
    for (const QString &field : {QStringLiteral("genres"), QStringLiteral("subjects"),
                                 QStringLiteral("tags"), QStringLiteral("categories")}) {
        const QVariant tagValue = row.value(field);
        const QVariantList tags = tagValue.canConvert<QVariantList>()
            ? tagValue.toList() : QVariantList{tagValue};
        for (const QVariant &tag : tags) {
            const QString name = tag.canConvert<QVariantMap>()
                ? tag.toMap().value(QStringLiteral("name")).toString()
                : tag.toString();
            if (blocked.contains(name.trimmed().toLower())) return false;
        }
    }
    return true;
}

QVariantList mediaItems(const QVariantList &rows, const QString &kind,
                        bool showExplicit)
{
    QVariantList out;
    for (const QVariant &value : rows) {
        QVariantMap row = value.toMap();
        if (!visible(row, showExplicit)) continue;
        if (row.contains(QStringLiteral("tt"))) {
            const QString tt = row.value(QStringLiteral("tt")).toString();
            row.insert(QStringLiteral("id"), tt);
            row.insert(QStringLiteral("cover"),
                       QStringLiteral("https://live.metahub.space/poster/small/%1/img").arg(tt));
        }
        QString itemKind = kind;
        if (itemKind.isEmpty()) {
            const QString id = row.value(QStringLiteral("id")).toString();
            const QString type = row.value(QStringLiteral("type")).toString();
            itemKind = id.startsWith(QLatin1String("mal:"))
                || row.contains(QStringLiteral("mal_id")) || type == QLatin1String("anime")
                ? QStringLiteral("anime")
                : type == QLatin1String("series") ? QStringLiteral("series")
                : QStringLiteral("movie");
        }
        out.append(WebFeedValue::item(row, QStringLiteral("Theatre"), itemKind));
    }
    return out;
}

QVariantMap shelfRoute(const QString &tab, const QString &key, bool showExplicit)
{
    return {{QStringLiteral("v"), 1}, {QStringLiteral("world"), QStringLiteral("Theatre")},
            {QStringLiteral("source"), QStringLiteral("catalogue")},
            {QStringLiteral("facet"), QVariantMap{{QStringLiteral("tab"), tab},
                                                   {QStringLiteral("rowKey"), key},
                                                   {QStringLiteral("medium"), tab == QLatin1String("anime")
                                                       ? QStringLiteral("anime") : tab == QLatin1String("shows")
                                                       ? QStringLiteral("series") : QStringLiteral("movie")}}},
            {QStringLiteral("explicit"), showExplicit}, {QStringLiteral("pageSize"), 24}};
}

void append(QVariantList &out, const QString &tab, const QString &key,
            const QString &title, const QString &kind, const QVariantList &rows,
            bool showExplicit, int cap = 20)
{
    const QVariantList cards = mediaItems(rows.mid(0, cap), kind, showExplicit);
    if (cards.isEmpty()) return;
    QVariantMap section = WebFeedValue::section(
        QStringLiteral("theatre.%1.%2").arg(tab, key), out.size(), title,
        QStringLiteral("rail"), cards);
    section.insert(QStringLiteral("seeAll"), QVariantMap{
        {QStringLiteral("route"), shelfRoute(tab, key, showExplicit)}});
    out.append(section);
}

void imdbShelf(QVariantList &out, ImdbCatalog &imdb, const QString &tab,
               const QString &key, const QString &title, QVariantMap query,
               bool showExplicit, int cap = 20)
{
    query.insert(QStringLiteral("excludeAnime"), true);
    append(out, tab, key, title, tab == QLatin1String("shows")
           ? QStringLiteral("series") : QStringLiteral("movie"),
           imdb.titleCatalog(query, 0, cap), showExplicit, cap);
}

void malShelf(QVariantList &out, MalCatalog &mal, const QString &key,
              const QString &title, const QVariantMap &query,
              bool showExplicit, int cap = 20)
{
    append(out, QStringLiteral("anime"), key, title, QStringLiteral("anime"),
           mal.animeCatalog(query, 0, cap), showExplicit, cap);
}

void appendGenres(QVariantList &out, const QString &tab, bool showExplicit)
{
    // TheatreGenreApi.js:mosaicGenres and TheatreCatalogPage.qml:genreMosaic.
    const QStringList names = tab == QLatin1String("movies")
        ? QStringList{QStringLiteral("Action"), QStringLiteral("Drama"), QStringLiteral("Comedy"),
                      QStringLiteral("Sci-Fi"), QStringLiteral("Thriller"), QStringLiteral("Horror"),
                      QStringLiteral("Romance"), QStringLiteral("Animation"), QStringLiteral("Adventure"),
                      QStringLiteral("Crime"), QStringLiteral("Mystery"), QStringLiteral("Fantasy"),
                      QStringLiteral("Documentary")}
        : tab == QLatin1String("shows")
        ? QStringList{QStringLiteral("Drama"), QStringLiteral("Comedy"), QStringLiteral("Crime"),
                      QStringLiteral("Sci-Fi"), QStringLiteral("Thriller"), QStringLiteral("Mystery"),
                      QStringLiteral("Action"), QStringLiteral("Animation"), QStringLiteral("Adventure"),
                      QStringLiteral("Fantasy"), QStringLiteral("Documentary"), QStringLiteral("Romance"),
                      QStringLiteral("Horror")}
        : QStringList{QStringLiteral("Action"), QStringLiteral("Adventure"), QStringLiteral("Comedy"),
                      QStringLiteral("Drama"), QStringLiteral("Fantasy"), QStringLiteral("Horror"),
                      QStringLiteral("Mystery"), QStringLiteral("Romance"), QStringLiteral("Sci-Fi"),
                      QStringLiteral("Slice of Life"), QStringLiteral("Sports"),
                      QStringLiteral("Supernatural")};
    const QString medium = tab == QLatin1String("movies") ? QStringLiteral("movie")
        : tab == QLatin1String("shows") ? QStringLiteral("series") : QStringLiteral("anime");
    auto target = [&medium, showExplicit](const QString &source, const QVariantMap &facet) {
        return QVariantMap{{QStringLiteral("route"), QVariantMap{
            {QStringLiteral("v"), 1}, {QStringLiteral("world"), QStringLiteral("Theatre")},
            {QStringLiteral("source"), source}, {QStringLiteral("facet"), facet},
            {QStringLiteral("explicit"), showExplicit}, {QStringLiteral("pageSize"), 24}}}};
    };
    QVariantList choices;
    for (const QString &name : names)
        choices.append(QVariantMap{{QStringLiteral("key"), medium + QLatin1Char(':') + name},
            {QStringLiteral("label"), name},
            {QStringLiteral("target"), target(QStringLiteral("genre"),
                {{QStringLiteral("medium"), medium}, {QStringLiteral("name"), name}})}});
    choices.append(QVariantMap{{QStringLiteral("key"), medium + QStringLiteral(":all-genres")},
        {QStringLiteral("label"), QStringLiteral("Explore all genres")},
        {QStringLiteral("target"), target(QStringLiteral("genreIndex"),
            {{QStringLiteral("medium"), medium}})}});
    QVariantMap section = WebFeedValue::section(
        QStringLiteral("theatre.%1.genres").arg(tab), out.size(),
        tab == QLatin1String("movies") ? QStringLiteral("Movie Genres")
            : tab == QLatin1String("shows") ? QStringLiteral("Show Genres")
            : QStringLiteral("Anime Genres"), QStringLiteral("tiles"), {});
    section.insert(QStringLiteral("choices"), choices);
    out.append(section);
}

void movieRows(QVariantList &out, ImdbCatalog &imdb, bool showExplicit)
{
    // TheatreCatalogRules.js:41-64 MOVIE_ROWS/indexQueryFor and
    // TheatreApi.js:753 loadMoviesShowsDeep (offline IMDb phase).
    const QString t = QStringLiteral("movies");
    const QString type = QStringLiteral("movie");
    imdbShelf(out, imdb, t, QStringLiteral("top10"), QStringLiteral("Top 10"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("votes")}}, showExplicit, 10);
    imdbShelf(out, imdb, t, QStringLiteral("recently-released"), QStringLiteral("Recently Released"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("year")},
         {QStringLiteral("votesMin"), 500}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("top-rated"), QStringLiteral("Top Rated"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("rating")},
         {QStringLiteral("ratingMin"), 8.0}, {QStringLiteral("votesMin"), 200000}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("hidden-gems"), QStringLiteral("Hidden Gems"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("rating")},
         {QStringLiteral("ratingMin"), 7.4}, {QStringLiteral("votesMin"), 10000},
         {QStringLiteral("votesMax"), 100000}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("cult-classics"), QStringLiteral("Cult Classics"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("rating")},
         {QStringLiteral("ratingMin"), 7.2}, {QStringLiteral("votesMin"), 10000},
         {QStringLiteral("votesMax"), 250000}, {QStringLiteral("yearTo"), 1999}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("under-two-hours"), QStringLiteral("Under Two Hours"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("votes")},
         {QStringLiteral("runtimeMax"), 120}, {QStringLiteral("votesMin"), 5000}}, showExplicit);
    for (const auto &pair : {QPair<QString, QString>{QStringLiteral("Documentary"), QStringLiteral("documentary-movies")},
                             {QStringLiteral("Animation"), QStringLiteral("animated-movies")}})
        imdbShelf(out, imdb, t, pair.second,
            pair.first + QStringLiteral(" Movies"),
            {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("votes")},
             {QStringLiteral("genre"), pair.first}, {QStringLiteral("votesMin"), 5000}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("international-cinema"), QStringLiteral("International Cinema"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("votes")},
         {QStringLiteral("notLang"), QStringLiteral("en")}, {QStringLiteral("votesMin"), 5000},
         {QStringLiteral("notGenre"), QStringList{QStringLiteral("Animation")}}}, showExplicit);
    for (const auto &pair : {QPair<QString, QString>{QStringLiteral("ja"), QStringLiteral("Japanese Cinema")},
                             {QStringLiteral("ko"), QStringLiteral("Korean Cinema")},
                             {QStringLiteral("fr"), QStringLiteral("French Cinema")}})
        imdbShelf(out, imdb, t, pair.second.split(QLatin1Char(' ')).first().toLower()
                  + QStringLiteral("-cinema"), pair.second,
            {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("rating")},
             {QStringLiteral("lang"), pair.first}, {QStringLiteral("votesMin"), 5000},
             {QStringLiteral("notGenre"), QStringList{QStringLiteral("Animation")}}}, showExplicit);
    for (const int year : {2020, 2010, 2000, 1990, 1980, 1970})
        imdbShelf(out, imdb, t, QString::number(year) + QStringLiteral("s-movies"),
            QString::number(year) + QStringLiteral("s Movies"),
            {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("votes")},
             {QStringLiteral("yearFrom"), year}, {QStringLiteral("yearTo"), year + 9},
             {QStringLiteral("votesMin"), 5000}}, showExplicit);
    appendGenres(out, t, showExplicit);
}

void showRows(QVariantList &out, ImdbCatalog &imdb, bool showExplicit)
{
    // TheatreCatalogRules.js:65-85 SHOW_ROWS/indexQueryFor.
    const QString t = QStringLiteral("shows");
    const QString type = QStringLiteral("series");
    imdbShelf(out, imdb, t, QStringLiteral("top10"), QStringLiteral("Top 10"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("votes")}}, showExplicit, 10);
    imdbShelf(out, imdb, t, QStringLiteral("recently-premiered"), QStringLiteral("Recently Premiered"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("year")},
         {QStringLiteral("votesMin"), 500}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("top-rated"), QStringLiteral("Top Rated"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("rating")},
         {QStringLiteral("ratingMin"), 8.2}, {QStringLiteral("votesMin"), 100000}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("hidden-gems"), QStringLiteral("Hidden Gems"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("rating")},
         {QStringLiteral("ratingMin"), 7.5}, {QStringLiteral("votesMin"), 5000},
         {QStringLiteral("votesMax"), 75000}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("cult-classics"), QStringLiteral("Cult Classics"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("rating")},
         {QStringLiteral("ratingMin"), 7.5}, {QStringLiteral("votesMin"), 5000},
         {QStringLiteral("votesMax"), 150000}, {QStringLiteral("yearTo"), 1999}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("long-running-series"), QStringLiteral("Long-Running Series"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("episodes")},
         {QStringLiteral("episodesMin"), 100}}, showExplicit);
    imdbShelf(out, imdb, t, QStringLiteral("limited-series"), QStringLiteral("Limited Series"),
        {{QStringLiteral("type"), QStringLiteral("mini")}, {QStringLiteral("order"), QStringLiteral("votes")},
         {QStringLiteral("votesMin"), 5000}}, showExplicit);
    for (const auto &pair : {QPair<QString, QString>{QStringLiteral("Drama"), QStringLiteral("drama-series")},
                             {QStringLiteral("Comedy"), QStringLiteral("comedy-series")},
                             {QStringLiteral("Documentary"), QStringLiteral("documentary-series")},
                             {QStringLiteral("Animation"), QStringLiteral("animated-series")}})
        imdbShelf(out, imdb, t, pair.second, pair.first + QStringLiteral(" Series"),
            {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("votes")},
             {QStringLiteral("genre"), pair.first}, {QStringLiteral("votesMin"), 5000}}, showExplicit);
    for (const auto &group : {QPair<QString, QStringList>{QStringLiteral("crime-and-mystery"),
                                 {QStringLiteral("Crime"), QStringLiteral("Mystery")}},
                              {QStringLiteral("science-fiction-and-fantasy"),
                                 {QStringLiteral("Sci-Fi"), QStringLiteral("Fantasy")}}}) {
        QVariantList merged;
        QSet<QString> seen;
        for (const QString &genre : group.second) {
            const QVariantList part = imdb.titleCatalog(
                {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("votes")},
                 {QStringLiteral("genre"), genre}, {QStringLiteral("votesMin"), 5000},
                 {QStringLiteral("excludeAnime"), true}}, 0, 20);
            for (const QVariant &value : part) {
                const QString tt = value.toMap().value(QStringLiteral("tt")).toString();
                if (!seen.contains(tt)) { seen.insert(tt); merged.append(value); }
            }
        }
        std::sort(merged.begin(), merged.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("votes")).toInt()
                 > b.toMap().value(QStringLiteral("votes")).toInt();
        });
        append(out, t, group.first, group.first == QLatin1String("crime-and-mystery")
            ? QStringLiteral("Crime and Mystery") : QStringLiteral("Science Fiction and Fantasy"),
            QStringLiteral("series"), merged, showExplicit);
    }
    imdbShelf(out, imdb, t, QStringLiteral("korean-drama"), QStringLiteral("Korean Drama"),
        {{QStringLiteral("type"), type}, {QStringLiteral("order"), QStringLiteral("rating")},
         {QStringLiteral("lang"), QStringLiteral("ko")}, {QStringLiteral("votesMin"), 5000},
         {QStringLiteral("notGenre"), QStringList{QStringLiteral("Animation"), QStringLiteral("Reality-TV"),
             QStringLiteral("Game-Show"), QStringLiteral("Talk-Show"), QStringLiteral("News")}}}, showExplicit);
    appendGenres(out, t, showExplicit);
}

void animeRows(QVariantList &out, MalCatalog &mal, bool showExplicit)
{
    // TheatreCatalogRules.js:86-113 ANIME_ROWS; TheatreApi.js:841 animeQueryFor.
    malShelf(out, mal, QStringLiteral("top10"), QStringLiteral("Top 10"),
        {{QStringLiteral("order"), QStringLiteral("members")}}, showExplicit, 10);
    for (const auto &pair : {QPair<QString, QString>{QStringLiteral("airing-now"), QStringLiteral("members")},
                             {QStringLiteral("top-airing"), QStringLiteral("score")}})
        malShelf(out, mal, pair.first, pair.first == QLatin1String("airing-now")
                 ? QStringLiteral("Airing Now") : QStringLiteral("Top Airing"),
            {{QStringLiteral("status"), QStringLiteral("Currently Airing")},
             {QStringLiteral("order"), pair.second}}, showExplicit);
    malShelf(out, mal, QStringLiteral("upcoming-season"), QStringLiteral("Upcoming Season"),
        {{QStringLiteral("status"), QStringLiteral("Not yet aired")},
         {QStringLiteral("order"), QStringLiteral("members")}}, showExplicit);
    for (const auto &pair : {QPair<QString, QString>{QStringLiteral("TV"), QStringLiteral("Top Series")},
                             {QStringLiteral("Movie"), QStringLiteral("Top Anime Movies")}})
        malShelf(out, mal, pair.first == QLatin1String("TV") ? QStringLiteral("top-series")
                 : QStringLiteral("top-anime-movies"), pair.second,
            {{QStringLiteral("type"), pair.first}, {QStringLiteral("order"), QStringLiteral("score")},
             {QStringLiteral("voteFloor"), 5000}}, showExplicit);
    malShelf(out, mal, QStringLiteral("most-popular"), QStringLiteral("Most Popular"),
        {{QStringLiteral("order"), QStringLiteral("members")}}, showExplicit);
    malShelf(out, mal, QStringLiteral("top-rated"), QStringLiteral("Top Rated"),
        {{QStringLiteral("order"), QStringLiteral("score")}, {QStringLiteral("voteFloor"), 5000}}, showExplicit);
    malShelf(out, mal, QStringLiteral("hidden-gems"), QStringLiteral("Hidden Gems"),
        {{QStringLiteral("order"), QStringLiteral("score")}, {QStringLiteral("voteFloor"), 2000},
         {QStringLiteral("membersMin"), 20000}, {QStringLiteral("membersMax"), 150000}}, showExplicit);
    for (const int year : {2020, 2010, 2000})
        malShelf(out, mal, QString::number(year) + QStringLiteral("s-anime"),
            QString::number(year) + QStringLiteral("s Anime"),
            {{QStringLiteral("order"), QStringLiteral("members")},
             {QStringLiteral("yearFrom"), year}, {QStringLiteral("yearTo"), year + 9}}, showExplicit);
    malShelf(out, mal, QStringLiteral("1990s-earlier"), QStringLiteral("1990s and Earlier"),
        {{QStringLiteral("order"), QStringLiteral("members")},
         {QStringLiteral("yearTo"), 1999}}, showExplicit);
    for (const QString &tag : {QStringLiteral("Action"), QStringLiteral("Romance"),
                               QStringLiteral("Slice of Life"), QStringLiteral("Mecha"),
                               QStringLiteral("Fantasy"), QStringLiteral("Sci-Fi"),
                               QStringLiteral("Psychological"), QStringLiteral("Horror")}) {
        QString key = tag.toLower(); key.replace(QLatin1Char(' '), QLatin1Char('-'));
        if (tag == QLatin1String("Action")) key = QStringLiteral("action-and-adventure");
        if (tag == QLatin1String("Sci-Fi")) key = QStringLiteral("science-fiction");
        malShelf(out, mal, key, tag == QLatin1String("Action")
                 ? QStringLiteral("Action and Adventure") : tag,
            {{QStringLiteral("order"), QStringLiteral("members")},
             {QStringLiteral("tag"), tag}}, showExplicit);
    }
    QVariantList horror = mal.animeCatalog(
        {{QStringLiteral("order"), QStringLiteral("members")},
         {QStringLiteral("tag"), QStringLiteral("Horror")}}, 0, 20);
    const QVariantList supernatural = mal.animeCatalog(
        {{QStringLiteral("order"), QStringLiteral("members")},
         {QStringLiteral("tag"), QStringLiteral("Supernatural")}}, 0, 20);
    QSet<int> seen;
    for (const QVariant &value : horror)
        seen.insert(value.toMap().value(QStringLiteral("mal_id")).toInt());
    for (const QVariant &value : supernatural) {
        const int id = value.toMap().value(QStringLiteral("mal_id")).toInt();
        if (!seen.contains(id)) { seen.insert(id); horror.append(value); }
    }
    append(out, QStringLiteral("anime"), QStringLiteral("horror-and-supernatural"),
        QStringLiteral("Horror and Supernatural"), QStringLiteral("anime"), horror, showExplicit);
    appendGenres(out, QStringLiteral("anime"), showExplicit);
}

struct FinishedShow { QString root; QVariantMap entry; };

QList<FinishedShow> finishedShows(const QVariantList &recent)
{
    // TheatreWorld.qml:70 recomputeNextUp; NextUp.js:21 finishedShows; and
    // EpisodeBrowser.js:seriesRootId. A show's newest progress entry decides.
    QList<FinishedShow> out;
    QSet<QString> seen;
    for (const QVariant &value : recent) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("kind")).toString() != QLatin1String("video")) continue;
        const QString id = row.value(QStringLiteral("id")).toString();
        if (id.startsWith(QLatin1String("vault:"))) continue;
        const QStringList parts = id.split(QLatin1Char(':'));
        if (parts.size() < 3) continue;
        bool validSeason = false, validEpisode = false;
        parts.at(parts.size() - 2).toInt(&validSeason);
        parts.last().toInt(&validEpisode);
        if (!validSeason || !validEpisode) continue;
        const QString root = id.startsWith(QLatin1String("tt"))
            ? parts.first() : parts.mid(0, 2).join(QLatin1Char(':'));
        if (seen.contains(root)) continue;
        seen.insert(root);
        if (row.value(QStringLiteral("watched")).toBool()
            || row.value(QStringLiteral("progress")).toDouble() >= 0.90)
            out.append({root, row});
        if (out.size() >= 8) break;
    }
    return out;
}

QByteArray nextUpCacheKey(const QList<FinishedShow> &finished, bool showExplicit)
{
    QByteArray key = showExplicit ? "explicit:" : "standard:";
    for (const FinishedShow &candidate : finished) {
        key += candidate.root.toUtf8() + '|';
        key += candidate.entry.value(QStringLiteral("id")).toString().toUtf8() + '|';
        key += QByteArray::number(candidate.entry.value(QStringLiteral("updatedAt")).toLongLong()) + ';';
    }
    return QCryptographicHash::hash(key, QCryptographicHash::Sha256);
}

QMutex &nextUpCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QByteArray, QVariantList> &nextUpCache()
{
    static QHash<QByteArray, QVariantList> cache;
    return cache;
}

bool cachedNextUp(const QByteArray &key, QVariantList &cards)
{
    QMutexLocker guard(&nextUpCacheMutex());
    if (!nextUpCache().contains(key)) return false;
    cards = nextUpCache().value(key);
    return true;
}

void storeNextUp(const QByteArray &key, const QVariantList &cards)
{
    QMutexLocker guard(&nextUpCacheMutex());
    if (nextUpCache().size() >= 32) nextUpCache().clear();
    nextUpCache().insert(key, cards);
}

QVariantMap nextEpisode(const QVariantList &videos, const QString &nowId,
                        const QString &root)
{
    // NextUp.js:42 nextEpisodeFromMeta. Specials (S0) never follow a regular season.
    const QStringList parts = nowId.split(QLatin1Char(':'));
    const int currentSeason = parts.value(parts.size() - 2).toInt();
    const int currentEpisode = parts.last().toInt();
    int bestSeason = INT_MAX, bestEpisode = INT_MAX;
    QVariantMap best;
    for (const QVariant &value : videos) {
        const QVariantMap video = value.toMap();
        const int season = video.value(QStringLiteral("season"),
            video.value(QStringLiteral("seasonNumber"))).toInt();
        const int episode = video.value(QStringLiteral("episode"),
            video.value(QStringLiteral("number"))).toInt();
        if (season <= 0 || season < currentSeason
            || (season == currentSeason && episode <= currentEpisode)) continue;
        if (season < bestSeason || (season == bestSeason && episode < bestEpisode)) {
            bestSeason = season;
            bestEpisode = episode;
            best = video;
        }
    }
    if (best.isEmpty()) return {};
    if (best.value(QStringLiteral("id")).toString().isEmpty())
        best.insert(QStringLiteral("id"), root + QLatin1Char(':')
            + QString::number(bestSeason) + QLatin1Char(':') + QString::number(bestEpisode));
    best.insert(QStringLiteral("season"), bestSeason);
    best.insert(QStringLiteral("episode"), bestEpisode);
    return best;
}

QJsonObject fetchJson(QNetworkAccessManager &manager, const QUrl &url)
{
    QNetworkReply *reply = manager.get(QNetworkRequest(url));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    if (!reply->isFinished()) loop.exec();
    if (!reply->isFinished()) reply->abort();
    const QByteArray data = reply->error() == QNetworkReply::NoError
        ? reply->readAll() : QByteArray{};
    reply->deleteLater();
    return QJsonDocument::fromJson(data).object();
}

QVariantMap fetchSeriesMeta(QNetworkAccessManager &manager, const QString &root)
{
    // TheatreApi.js:loadMeta("series", show). This manager and its nested event
    // loop are created only in the feed worker; the GUI receives immutable data.
    const QUrl url(QStringLiteral("https://v3-cinemeta.strem.io/meta/series/%1.json")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(root))));
    return fetchJson(manager, url).value(QStringLiteral("meta")).toObject().toVariantMap();
}

QVariantList liveCatalogue(QNetworkAccessManager &manager, const QString &type,
                           const QString &genre = {})
{
    // TheatreApi.js:cinemetaCatalog's two-rung fallback.
    QString path = QStringLiteral("/catalog/%1/top").arg(type);
    if (!genre.isEmpty()) path += QStringLiteral("/genre=%1")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(genre)));
    path += QStringLiteral(".json");
    QJsonObject json = fetchJson(manager,
        QUrl(QStringLiteral("https://cinemeta-catalogs.strem.io/top") + path));
    if (!json.value(QStringLiteral("metas")).isArray())
        json = fetchJson(manager, QUrl(QStringLiteral("https://v3-cinemeta.strem.io") + path));
    return json.value(QStringLiteral("metas")).toArray().toVariantList();
}

QVariantList liveItems(const QVariantList &metas, const QString &kind,
                       bool showExplicit, int cap)
{
    QVariantList rows;
    for (const QVariant &value : metas) {
        const QVariantMap meta = value.toMap();
        const QString id = meta.value(QStringLiteral("id"),
            meta.value(QStringLiteral("imdb_id"))).toString();
        if (id.isEmpty()) continue;
        QVariantMap row{{QStringLiteral("id"), id},
                        {QStringLiteral("type"), kind},
                        {QStringLiteral("title"), meta.value(QStringLiteral("name"),
                            meta.value(QStringLiteral("title")))},
                        {QStringLiteral("cover"), meta.value(QStringLiteral("poster"))},
                        {QStringLiteral("backdrop"), meta.value(QStringLiteral("background"))},
                        {QStringLiteral("genres"), meta.value(QStringLiteral("genres"),
                            meta.value(QStringLiteral("genre")))},
                        {QStringLiteral("behaviorHints"), meta.value(QStringLiteral("behaviorHints"))}};
        if (id.startsWith(QLatin1String("tt"))) row.insert(QStringLiteral("tt"), id);
        const double rating = meta.value(QStringLiteral("imdbRating")).toDouble();
        if (rating > 0) row.insert(QStringLiteral("rating"), rating);
        const QString release = meta.value(QStringLiteral("releaseInfo")).toString();
        bool validYear = false;
        const int year = (release.isEmpty() ? meta.value(QStringLiteral("year")).toString()
                                            : release.left(4)).toInt(&validYear);
        if (validYear && year > 0) row.insert(QStringLiteral("year"), year);
        if (!visible(row, showExplicit)) continue;
        rows.append(row);
        if (rows.size() >= cap) break;
    }
    return mediaItems(rows, kind, showExplicit);
}

void updateSection(QVariantList &sections, const QString &id, const QString &title,
                   const QString &layout, const QVariantList &cards, int insertAt,
                   bool showExplicit, const QString &tab, const QString &rowKey)
{
    if (cards.isEmpty()) return; // retain the bundled fallback on network failure
    QVariantMap replacement;
    int index = -1;
    for (int i = 0; i < sections.size(); ++i)
        if (sections.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            index = i; replacement = sections.at(i).toMap(); break;
        }
    if (index < 0) {
        replacement = WebFeedValue::section(id, insertAt, title, layout, cards);
        if (!rowKey.isEmpty())
            replacement.insert(QStringLiteral("seeAll"), QVariantMap{
                {QStringLiteral("route"), shelfRoute(tab, rowKey, showExplicit)}});
        sections.insert(qBound(0, insertAt, sections.size()), replacement);
    } else {
        replacement.insert(QStringLiteral("items"), cards);
        replacement.insert(QStringLiteral("state"), QStringLiteral("ready"));
        sections[index] = replacement;
    }
    for (int i = 0; i < sections.size(); ++i) {
        QVariantMap section = sections.at(i).toMap();
        section.insert(QStringLiteral("index"), i);
        sections[i] = section;
    }
}

void enrichMovieShow(QVariantList &sections, QNetworkAccessManager &manager,
                     const QString &tab, bool showExplicit)
{
    // TheatreApi.js:loadMoviesShowsDeep live Cinemeta phase. Bundled IMDb
    // sections remain when the provider does not answer.
    const QString kind = tab == QLatin1String("movies")
        ? QStringLiteral("movie") : QStringLiteral("series");
    const QVariantList source = liveCatalogue(manager, kind);
    updateSection(sections, QStringLiteral("theatre.%1.top10").arg(tab),
        QStringLiteral("Top 10"), QStringLiteral("rail"),
        liveItems(source, kind, showExplicit, 10), 2, showExplicit, tab,
        QStringLiteral("top10"));
    QVariantList recent = liveItems(source, kind, showExplicit, 100);
    std::stable_sort(recent.begin(), recent.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("year")).toInt()
             > b.toMap().value(QStringLiteral("year")).toInt();
    });
    QVariantList dated;
    for (const QVariant &value : recent) {
        if (value.toMap().value(QStringLiteral("year")).toInt() <= 0) continue;
        dated.append(value);
        if (dated.size() == 20) break;
    }
    const QString key = tab == QLatin1String("movies")
        ? QStringLiteral("recently-released") : QStringLiteral("recently-premiered");
    updateSection(sections, QStringLiteral("theatre.%1.%2").arg(tab, key),
        tab == QLatin1String("movies") ? QStringLiteral("Recently Released")
            : QStringLiteral("Recently Premiered"), QStringLiteral("rail"), dated, 3,
        showExplicit, tab, key);
    if (tab == QLatin1String("shows")) {
        QVariantList airing;
        for (const QVariant &value : source)
            if (value.toMap().value(QStringLiteral("status")).toString()
                == QLatin1String("Continuing")) airing.append(value);
        updateSection(sections, QStringLiteral("theatre.shows.currently-airing"),
            QStringLiteral("Currently Airing"), QStringLiteral("rail"),
            liveItems(airing, kind, showExplicit, 20), 4, showExplicit, tab,
            QStringLiteral("currently-airing"));
    }
}

void enrichAnime(QVariantList &sections, QNetworkAccessManager &manager,
                 bool showExplicit)
{
    // TheatreApi.js:LIVE_ANIME. Bundled MAL shelves survive a live miss.
    // Jikan's Kitsu fallback remains a parity gap until the provider ladder
    // can be moved without delaying the first section.
    struct LiveRow { const char *key; const char *title; const char *path; };
    static const LiveRow rows[] = {
        {"top10", "Top 10", "/top/anime?filter=bypopularity"},
        {"airing-now", "Airing Now", "/seasons/now"},
        {"top-airing", "Top Airing", "/top/anime?filter=airing"},
        {"upcoming-season", "Upcoming Season", "/seasons/upcoming"},
        {"most-popular", "Most Popular", "/top/anime?filter=bypopularity"},
        {"top-rated", "Top Rated", "/top/anime"}
    };
    int insertAt = 2;
    for (const LiveRow &row : rows) {
        QString url = QStringLiteral("https://api.jikan.moe/v4")
            + QString::fromLatin1(row.path);
        url += url.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?');
        url += showExplicit ? QStringLiteral("sfw=false") : QStringLiteral("sfw=true");
        const QVariantList source = fetchJson(manager, QUrl(url))
            .value(QStringLiteral("data")).toArray().toVariantList();
        const QString key = QString::fromLatin1(row.key);
        const QVariantList cards = mediaItems(source.mid(0, key == QLatin1String("top10") ? 10 : 20),
            QStringLiteral("anime"), showExplicit);
        updateSection(sections, QStringLiteral("theatre.anime.") + key,
            QString::fromLatin1(row.title), QStringLiteral("rail"), cards,
            insertAt++, showExplicit, QStringLiteral("anime"), key);
    }
}

void enrichExtensions(QVariantList &sections, QNetworkAccessManager &manager,
                      const FeedContext &context, const QString &tab)
{
    // AddonClient.js:413 theatreCatalogSpecs and TheatreCatalogRules.js:346 placeExtensions.
    // Only installed, enabled, non-core catalogues with no required extra are browsable.
    const QString type = tab == QLatin1String("movies")
        ? QStringLiteral("movie") : QStringLiteral("series");
    QHash<QString, int> placement;
    int houseSlot = 10;
    for (const QVariant &value : sections) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (id == QLatin1String("theatre.featured")) placement.insert(id, -20);
        else if (id == QLatin1String("theatre.nextUp")) placement.insert(id, -10);
        else if (id.endsWith(QLatin1String(".genres"))) placement.insert(id, 1000);
        else { placement.insert(id, houseSlot); houseSlot += 10; }
    }
    static const QHash<QString, int> serviceSlots{
        {QStringLiteral("netflix"), 15}, {QStringLiteral("prime"), 16},
        {QStringLiteral("disney"), 17}, {QStringLiteral("hbo"), 18},
        {QStringLiteral("appletv"), 19}, {QStringLiteral("amc"), 25},
        {QStringLiteral("fx"), 26}};
    int installedOrder = 0;
    for (const QVariant &value : context.extensions) {
        const QVariantMap extension = value.toMap();
        if (!extension.value(QStringLiteral("enabled")).toBool()
            || extension.value(QStringLiteral("core")).toBool()) continue;
        const QString transport = extension.value(QStringLiteral("transportUrl")).toString();
        QUrl base(transport);
        if (!base.isValid() || !QStringList{QStringLiteral("http"), QStringLiteral("https")}
            .contains(base.scheme().toLower())) continue;
        QString baseText = transport;
        if (baseText.endsWith(QLatin1String("/manifest.json"), Qt::CaseInsensitive))
            baseText.chop(14);
        const QVariantMap manifest = extension.value(QStringLiteral("manifest")).toMap();
        const QString extensionId = extension.value(QStringLiteral("id")).toString();
        const QString extensionName = manifest.value(QStringLiteral("name"), extensionId).toString();
        for (const QVariant &catalogValue : manifest.value(QStringLiteral("catalogs")).toList()) {
            const QVariantMap catalog = catalogValue.toMap();
            const QString catalogId = catalog.value(QStringLiteral("id")).toString();
            if (catalogId.isEmpty() || catalog.value(QStringLiteral("type")).toString() != type) continue;
            bool required = false;
            for (const QVariant &extra : catalog.value(QStringLiteral("extra")).toList())
                required |= extra.toMap().value(QStringLiteral("isRequired")).toBool();
            if (required) continue;
            const QString title = catalog.value(QStringLiteral("name"), extensionName).toString();
            const QString hay = (extensionId + QLatin1Char(' ') + extensionName
                + QLatin1Char(' ') + base.host() + QLatin1Char(' ') + title).toLower();
            static const QList<QPair<QString, QString>> brands{
                {QStringLiteral("netflix"), QStringLiteral("netflix|\\bnfx\\b")},
                {QStringLiteral("prime"), QStringLiteral("prime\\s*video|primevideo|amazon\\s*prime")},
                {QStringLiteral("disney"), QStringLiteral("disney|hotstar")},
                {QStringLiteral("hbo"), QStringLiteral("hbo\\s*max|hbomax|\\bhbo\\b|\\bmax\\b")},
                {QStringLiteral("appletv"), QStringLiteral("apple\\s*tv|appletv|\\batv\\+?\\b")},
                {QStringLiteral("amc"), QStringLiteral("\\bamc\\+?\\b")},
                {QStringLiteral("fx"), QStringLiteral("\\bfxnow\\b|\\bfx\\b")}};
            QString brand;
            for (const auto &candidate : brands)
                if (QRegularExpression(candidate.second).match(hay).hasMatch()) {
                    brand = candidate.first; break;
                }
            const QUrl url(baseText + QStringLiteral("/catalog/%1/%2.json").arg(type,
                QString::fromUtf8(QUrl::toPercentEncoding(catalogId))));
            const QVariantList cards = liveItems(fetchJson(manager, url)
                .value(QStringLiteral("metas")).toArray().toVariantList(), type,
                context.showExplicit, 20);
            if (cards.isEmpty()) continue;
            const QString hash = QString::fromLatin1(QCryptographicHash::hash(
                (transport + QLatin1Char(':') + catalogId).toUtf8(),
                QCryptographicHash::Sha256).toHex().left(12));
            QVariantMap section = WebFeedValue::section(
                QStringLiteral("theatre.%1.extension.%2").arg(tab, hash), 0, title,
                QStringLiteral("rail"), cards);
            section.insert(QStringLiteral("seeAll"), QVariantMap{{QStringLiteral("route"),
                QVariantMap{{QStringLiteral("v"), 1},
                    {QStringLiteral("world"), QStringLiteral("Theatre")},
                    {QStringLiteral("source"), QStringLiteral("catalogue")},
                    {QStringLiteral("facet"), QVariantMap{
                        {QStringLiteral("kind"), QStringLiteral("extension")},
                        {QStringLiteral("medium"), type},
                        {QStringLiteral("extensionId"), extensionId},
                        {QStringLiteral("transportUrl"), transport},
                        {QStringLiteral("catalogueType"), type},
                        {QStringLiteral("catalogueId"), catalogId},
                        {QStringLiteral("requiredExtraPolicy"), QStringLiteral("skip")}}},
                    {QStringLiteral("explicit"), context.showExplicit},
                    {QStringLiteral("pageSize"), 24}}}});
            placement.insert(section.value(QStringLiteral("id")).toString(),
                serviceSlots.value(brand, 2000 + installedOrder));
            sections.append(section);
            ++installedOrder;
        }
    }
    std::stable_sort(sections.begin(), sections.end(), [&](const QVariant &a, const QVariant &b) {
        return placement.value(a.toMap().value(QStringLiteral("id")).toString(), 3000)
            < placement.value(b.toMap().value(QStringLiteral("id")).toString(), 3000);
    });
    for (int i = 0; i < sections.size(); ++i) {
        QVariantMap section = sections.at(i).toMap();
        section.insert(QStringLiteral("index"), i);
        sections[i] = section;
    }
}

void enrichDiscover(QVariantList &sections, QNetworkAccessManager &manager,
                    bool showExplicit)
{
    // TheatreWorld.qml:182 loadCatalog; TheatreApi.js:1161 loadTheatre. Live Cinemeta
    // order replaces the provisional bundled rows only when a rung answers.
    const QVariantList movies = liveItems(liveCatalogue(manager, QStringLiteral("movie")),
        QStringLiteral("movie"), showExplicit, 12);
    const QVariantList shows = liveItems(liveCatalogue(manager, QStringLiteral("series")),
        QStringLiteral("series"), showExplicit, 12);
    const QVariantList anime = liveItems(liveCatalogue(manager, QStringLiteral("series"),
        QStringLiteral("Anime")), QStringLiteral("anime"), showExplicit, 12);
    QVariantList featured;
    if (!movies.isEmpty()) featured.append(movies.first());
    if (!shows.isEmpty()) featured.append(shows.first());
    if (!anime.isEmpty()) featured.append(anime.first());
    updateSection(sections, QStringLiteral("theatre.featured"),
        QStringLiteral("Featured in Theatre"), QStringLiteral("hero"), featured,
        0, showExplicit, QStringLiteral("discover"), {});
    updateSection(sections, QStringLiteral("theatre.discover.movies"),
        QStringLiteral("Top Movies"), QStringLiteral("rail"), movies,
        sections.size(), showExplicit, QStringLiteral("discover"), QStringLiteral("movies"));
    updateSection(sections, QStringLiteral("theatre.discover.shows"),
        QStringLiteral("Top Shows"), QStringLiteral("rail"), shows,
        sections.size(), showExplicit, QStringLiteral("discover"), QStringLiteral("shows"));
    updateSection(sections, QStringLiteral("theatre.discover.anime"),
        QStringLiteral("Top Anime"), QStringLiteral("rail"), anime,
        sections.size(), showExplicit, QStringLiteral("discover"), QStringLiteral("anime"));
}

struct LibraryRow {
    QVariantMap item;
    QString state;
    QString type;
    QString airing;
    QString title;
    qint64 addedAt = 0;
    qint64 lastWatchedAt = 0;
    int year = 0;
    int newCount = 0;
    bool downloaded = false;
};

QString libraryState(bool series, double rawProgress, qint64 progressAt,
                     const QVariantMap &facts)
{
    // LibraryApi.js:14 watchState. Manual marks win; provider marks yield only to
    // explicitly newer local activity. An episode percentage cannot complete a show.
    const int mark = facts.value(QStringLiteral("mark")).toInt();
    const bool manual = facts.value(QStringLiteral("manual"), true).toBool();
    const qint64 actionAt = facts.value(QStringLiteral("actionAt")).toLongLong();
    const qint64 completedAt = facts.value(QStringLiteral("completedAt")).toLongLong();
    const double progress = series && rawProgress >= 0.90 ? 0.5 : rawProgress;
    const bool partial = progress > 0 && progress < 0.90;
    if (manual && mark == 1) return QStringLiteral("watched");
    if (manual && mark == -1) return partial ? QStringLiteral("progress")
                                              : QStringLiteral("unwatched");
    if (!manual && (mark == 1 || mark == -1)) {
        const bool newerPartial = partial && actionAt > 0 && progressAt > actionAt;
        const bool newerCompletion = completedAt > 0 && actionAt > 0 && completedAt > actionAt;
        if (newerPartial && (!newerCompletion || progressAt > completedAt))
            return QStringLiteral("progress");
        if (newerCompletion) return QStringLiteral("watched");
        return mark == 1 ? QStringLiteral("watched") : QStringLiteral("unwatched");
    }
    if (completedAt > 0 && !(partial && progressAt > completedAt))
        return QStringLiteral("watched");
    if (!series && progress >= 0.90) return QStringLiteral("watched");
    return partial ? QStringLiteral("progress") : QStringLiteral("unwatched");
}

QVariantList librarySections(const FeedContext &context, int firstIndex)
{
    const QVariantMap view = context.params.value(QStringLiteral("view")).toMap();
    const QString selectedType = view.value(QStringLiteral("type")).toString();
    const QString selectedState = view.value(QStringLiteral("state")).toString();
    const QString selectedAiring = view.value(QStringLiteral("airing")).toString();
    const QString selectedSort = view.value(QStringLiteral("sort"),
        QStringLiteral("lastWatched")).toString();
    const QString needle = view.value(QStringLiteral("query")).toString().trimmed();
    QSet<QString> downloads;
    for (const QString &id : context.downloadedIds) downloads.insert(id);
    QList<LibraryRow> rows;
    QVariantMap counts{{QStringLiteral("saved"), 0}, {QStringLiteral("inProgress"), 0},
                       {QStringLiteral("unwatched"), 0}, {QStringLiteral("watched"), 0},
                       {QStringLiteral("newEpisodes"), 0}, {QStringLiteral("downloaded"), 0}};
    for (const QVariant &value : context.collection) {
        const QVariantMap entry = value.toMap();
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || !visible(entry, context.showExplicit)) continue;
        const QString type = id.startsWith(QLatin1String("mal:"))
            || entry.contains(QStringLiteral("mal_id")) ? QStringLiteral("anime")
            : entry.value(QStringLiteral("type")).toString() == QLatin1String("series")
            ? QStringLiteral("series") : QStringLiteral("movie");
        const bool series = type != QLatin1String("movie");
        QVariantMap progress;
        for (const QVariant &candidate : context.recent) {
            const QVariantMap p = candidate.toMap();
            const QString pid = p.value(QStringLiteral("id")).toString();
            if (p.value(QStringLiteral("kind")).toString() == QLatin1String("video")
                && (pid == id || pid.startsWith(id + QLatin1Char(':')))) {
                progress = p; break;
            }
        }
        const QVariantMap payload = entry.value(QStringLiteral("payload")).toMap();
        const double fraction = qBound(0.0, progress.value(QStringLiteral("progress")).toDouble(), 1.0);
        const qint64 progressAt = progress.value(QStringLiteral("updatedAt")).toLongLong();
        const QString state = libraryState(series, fraction, progressAt,
            context.libraryFacts.value(id).toMap());
        const bool downloaded = downloads.contains(id);
        const int newCount = payload.value(QStringLiteral("libNotif"), true).toBool()
            ? qBound(0, payload.value(QStringLiteral("libNewCount")).toInt(), 99) : 0;
        QString yearText = payload.value(QStringLiteral("libYear"),
            payload.value(QStringLiteral("year"))).toString();
        if (yearText.isEmpty()) {
            const auto match = QRegularExpression(QStringLiteral("\\d{4}"))
                .match(payload.value(QStringLiteral("releaseInfo")).toString());
            if (match.hasMatch()) yearText = match.captured();
        }
        QVariantMap item = mediaItems({entry}, {}, context.showExplicit).value(0).toMap();
        if (item.isEmpty()) continue;
        if (fraction > 0) item.insert(QStringLiteral("progress"), fraction);
        const QString sub = progress.value(QStringLiteral("sub")).toString();
        const bool resume = fraction > 0 || state == QLatin1String("progress");
        QVariantList menu{
            QVariantMap{{QStringLiteral("key"), QStringLiteral("resume")},
                {QStringLiteral("label"), resume
                    ? QStringLiteral("Resume") + (sub.isEmpty() ? QString() : QLatin1Char(' ') + sub)
                    : QStringLiteral("Play")},
                {QStringLiteral("target"), QVariantMap{{QStringLiteral("intent"), QStringLiteral("resume")}}}},
            QVariantMap{{QStringLiteral("key"), QStringLiteral("detail")},
                {QStringLiteral("label"), QStringLiteral("Details")},
                {QStringLiteral("target"), QVariantMap{{QStringLiteral("intent"), QStringLiteral("details")}}}}
        };
        if (fraction > 0)
            menu.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("dismiss")},
                {QStringLiteral("label"), QStringLiteral("Dismiss progress")},
                {QStringLiteral("target"), QVariantMap{{QStringLiteral("act"),
                    QStringLiteral("world.theatre.dismiss")}}}});
        menu.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("watch")},
            {QStringLiteral("label"), state == QLatin1String("watched")
                ? QStringLiteral("Mark unwatched") : QStringLiteral("Mark watched")},
            {QStringLiteral("target"), QVariantMap{{QStringLiteral("act"),
                QStringLiteral("world.theatre.markWatched")},
                {QStringLiteral("payload"), QVariantMap{{QStringLiteral("watched"),
                    state != QLatin1String("watched")}}}}}});
        menu.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("remove")},
            {QStringLiteral("label"), QStringLiteral("Remove from Library")},
            {QStringLiteral("warn"), true},
            {QStringLiteral("target"), QVariantMap{{QStringLiteral("act"),
                QStringLiteral("collection.remove")}}}});
        item.insert(QStringLiteral("menu"), menu);
        if (newCount > 0) item.insert(QStringLiteral("badge"), QStringLiteral("New episode"));
        else if (downloaded) item.insert(QStringLiteral("badge"), QStringLiteral("Downloaded"));
        else if (state == QLatin1String("watched")) item.insert(QStringLiteral("badge"), QStringLiteral("Watched"));
        if (yearText.toInt() > 0) item.insert(QStringLiteral("year"), yearText.toInt());
        LibraryRow row{item, state, type,
            payload.value(QStringLiteral("libAiring")).toString(),
            entry.value(QStringLiteral("title")).toString(),
            entry.value(QStringLiteral("addedAt")).toLongLong(),
            progressAt > 0 ? progressAt : entry.value(QStringLiteral("addedAt")).toLongLong(),
            yearText.toInt(), newCount, downloaded};
        rows.append(row);
        counts[QStringLiteral("saved")] = counts.value(QStringLiteral("saved")).toInt() + 1;
        const QString countKey = state == QLatin1String("progress")
            ? QStringLiteral("inProgress") : state;
        counts[countKey] = counts.value(countKey).toInt() + 1;
        if (newCount > 0) counts[QStringLiteral("newEpisodes")] =
            counts.value(QStringLiteral("newEpisodes")).toInt() + 1;
        if (downloaded) counts[QStringLiteral("downloaded")] =
            counts.value(QStringLiteral("downloaded")).toInt() + 1;
    }
    auto choice = [](const QString &key, const QString &label,
                     const QString &field, const QString &value,
                     bool selected, const QString &count = QString()) {
        QVariantMap result{{QStringLiteral("key"), key}, {QStringLiteral("label"), label},
            {QStringLiteral("selected"), selected},
            {QStringLiteral("target"), QVariantMap{{QStringLiteral("view"),
                QVariantMap{{field, value}}}}}};
        if (!count.isEmpty()) result.insert(QStringLiteral("sublabel"), count);
        return result;
    };
    QVariantList controls;
    for (const auto &option : {QPair<QString, QString>{QString(), QStringLiteral("All saved")},
             {QStringLiteral("inProgress"), QStringLiteral("In progress")},
             {QStringLiteral("unwatched"), QStringLiteral("Unwatched")},
             {QStringLiteral("watched"), QStringLiteral("Watched")},
             {QStringLiteral("newEpisodes"), QStringLiteral("New episodes")},
             {QStringLiteral("downloaded"), QStringLiteral("Downloaded")}}) {
        const QString countKey = option.first.isEmpty() ? QStringLiteral("saved") : option.first;
        controls.append(choice(QStringLiteral("state:") + countKey, option.second,
            QStringLiteral("state"), option.first, selectedState == option.first,
            QString::number(counts.value(countKey).toInt())));
    }
    for (const auto &option : {QPair<QString, QString>{QString(), QStringLiteral("All types")},
             {QStringLiteral("movie"), QStringLiteral("Movies")},
             {QStringLiteral("series"), QStringLiteral("Shows")},
             {QStringLiteral("anime"), QStringLiteral("Anime")}})
        controls.append(choice(QStringLiteral("type:") + option.first, option.second,
            QStringLiteral("type"), option.first, selectedType == option.first));
    for (const auto &option : {QPair<QString, QString>{QString(), QStringLiteral("Any airing")},
             {QStringLiteral("ongoing"), QStringLiteral("Ongoing")},
             {QStringLiteral("ended"), QStringLiteral("Ended")}})
        controls.append(choice(QStringLiteral("airing:") + option.first, option.second,
            QStringLiteral("airing"), option.first, selectedAiring == option.first));
    for (const auto &option : {QPair<QString, QString>{QStringLiteral("lastWatched"), QStringLiteral("Last watched")},
             {QStringLiteral("added"), QStringLiteral("Recently added")},
             {QStringLiteral("az"), QStringLiteral("A–Z")},
             {QStringLiteral("year"), QStringLiteral("Year")}})
        controls.append(choice(QStringLiteral("sort:") + option.first, option.second,
            QStringLiteral("sort"), option.first, selectedSort == option.first));
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const LibraryRow &row) {
        return (!selectedType.isEmpty() && row.type != selectedType)
            || (selectedState == QLatin1String("inProgress") && row.state != QLatin1String("progress"))
            || (selectedState == QLatin1String("unwatched") && row.state != QLatin1String("unwatched"))
            || (selectedState == QLatin1String("watched") && row.state != QLatin1String("watched"))
            || (selectedState == QLatin1String("newEpisodes") && row.newCount == 0)
            || (selectedState == QLatin1String("downloaded") && !row.downloaded)
            || (!selectedAiring.isEmpty() && row.airing != selectedAiring)
            || (!needle.isEmpty() && !row.title.contains(needle, Qt::CaseInsensitive));
    }), rows.end());
    std::stable_sort(rows.begin(), rows.end(), [&](const LibraryRow &a, const LibraryRow &b) {
        if (selectedSort == QLatin1String("added")) return a.addedAt > b.addedAt;
        if (selectedSort == QLatin1String("az")) return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
        if (selectedSort == QLatin1String("year")) return a.year > b.year;
        return a.lastWatchedAt > b.lastWatchedAt;
    });
    QVariantList cards;
    for (const LibraryRow &row : std::as_const(rows)) cards.append(row.item);
    QVariantMap filters = WebFeedValue::section(QStringLiteral("theatre.library.controls"),
        firstIndex, QStringLiteral("Your Collection"), QStringLiteral("chips"), {});
    filters.insert(QStringLiteral("choices"), controls);
    QVariantMap library = WebFeedValue::section(QStringLiteral("theatre.library"), firstIndex + 1,
        QStringLiteral("Library"), QStringLiteral("grid"), cards,
        cards.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    if (counts.value(QStringLiteral("saved")).toInt() == 0) {
        library.insert(QStringLiteral("emptyTitle"), QStringLiteral("Your library is empty"));
        library.insert(QStringLiteral("emptyText"),
            QStringLiteral("Press play on anything, or tap + Library — it lands here."));
    } else if (cards.isEmpty()) {
        library.insert(QStringLiteral("emptyTitle"), QStringLiteral("Nothing matches these filters"));
    }
    return {filters, library};
}

bool validTheatreView(const QVariantMap &params)
{
    if (!params.contains(QStringLiteral("view"))) return true;
    if (!params.value(QStringLiteral("view")).canConvert<QVariantMap>()) return false;
    const QVariantMap view = params.value(QStringLiteral("view")).toMap();
    if (params.value(QStringLiteral("tab")).toString() != QLatin1String("library"))
        return view.isEmpty();
    for (auto it = view.cbegin(); it != view.cend(); ++it) {
        if (it.value().metaType().id() != QMetaType::QString) return false;
        const QString value = it.value().toString();
        const QString key = it.key();
        if (key == QLatin1String("query")) {
            if (value.size() > 120) return false;
        } else if (key == QLatin1String("type")) {
            if (!QStringList{QString(), QStringLiteral("movie"),
                QStringLiteral("series"), QStringLiteral("anime")}.contains(value)) return false;
        } else if (key == QLatin1String("state")) {
            if (!QStringList{QString(), QStringLiteral("inProgress"),
                QStringLiteral("unwatched"), QStringLiteral("watched"),
                QStringLiteral("newEpisodes"), QStringLiteral("downloaded")}.contains(value)) return false;
        } else if (key == QLatin1String("airing")) {
            if (!QStringList{QString(), QStringLiteral("ongoing"),
                QStringLiteral("ended")}.contains(value)) return false;
        } else if (key == QLatin1String("sort")) {
            if (!QStringList{QStringLiteral("lastWatched"), QStringLiteral("added"),
                QStringLiteral("az"), QStringLiteral("year")}.contains(value)) return false;
        } else return false;
    }
    return true;
}

bool validExtensionRoute(const QVariantMap &params)
{
    const QVariantMap route = params.value(QStringLiteral("route")).toMap();
    const QVariantMap facet = route.value(QStringLiteral("facet")).toMap();
    const QString medium = facet.value(QStringLiteral("medium")).toString();
    return route.value(QStringLiteral("v")).toInt() == 1
        && route.value(QStringLiteral("world")).toString() == QLatin1String("Theatre")
        && route.value(QStringLiteral("source")).toString() == QLatin1String("catalogue")
        && facet.value(QStringLiteral("kind")).toString() == QLatin1String("extension")
        && QStringList{QStringLiteral("movie"), QStringLiteral("series")}.contains(medium)
        && route.value(QStringLiteral("pageSize")).toInt() > 0
        && route.value(QStringLiteral("pageSize")).toInt() <= 100
        && route.contains(QStringLiteral("explicit"))
        && facet.value(QStringLiteral("catalogueType")).toString() == medium
        && facet.value(QStringLiteral("requiredExtraPolicy")).toString() == QLatin1String("skip")
        && !facet.value(QStringLiteral("extensionId")).toString().isEmpty()
        && !facet.value(QStringLiteral("transportUrl")).toString().isEmpty()
        && !facet.value(QStringLiteral("catalogueId")).toString().isEmpty();
}

QVariantList extensionPage(const FeedContext &context)
{
    const QVariantMap route = context.params.value(QStringLiteral("route")).toMap();
    const QVariantMap facet = route.value(QStringLiteral("facet")).toMap();
    const QString extensionId = facet.value(QStringLiteral("extensionId")).toString();
    const QString transport = facet.value(QStringLiteral("transportUrl")).toString();
    const QString type = facet.value(QStringLiteral("catalogueType")).toString();
    const QString catalogId = facet.value(QStringLiteral("catalogueId")).toString();
    bool installed = false;
    for (const QVariant &value : context.extensions) {
        const QVariantMap extension = value.toMap();
        if (extension.value(QStringLiteral("id")).toString() != extensionId
            || extension.value(QStringLiteral("transportUrl")).toString() != transport
            || !extension.value(QStringLiteral("enabled")).toBool()
            || extension.value(QStringLiteral("core")).toBool()) continue;
        for (const QVariant &catalogValue : extension.value(QStringLiteral("manifest"))
                 .toMap().value(QStringLiteral("catalogs")).toList()) {
            const QVariantMap catalog = catalogValue.toMap();
            if (catalog.value(QStringLiteral("type")).toString() != type
                || catalog.value(QStringLiteral("id")).toString() != catalogId) continue;
            bool required = false;
            for (const QVariant &extra : catalog.value(QStringLiteral("extra")).toList())
                required |= extra.toMap().value(QStringLiteral("isRequired")).toBool();
            installed = !required;
            break;
        }
        if (installed) break;
    }
    auto result = [](const QVariantList &cards, const QString &state,
                     bool more, const QString &error = QString()) {
        QVariantMap section = WebFeedValue::section(QStringLiteral("seeAll.results"), 0,
            QStringLiteral("See All"), QStringLiteral("grid"), cards, state, more);
        if (!error.isEmpty()) section.insert(QStringLiteral("error"), error);
        return QVariantList{section};
    };
    if (!installed)
        return result({}, QStringLiteral("error"), false,
            QStringLiteral("This catalogue is no longer available."));
    QUrl base(transport);
    if (!base.isValid() || !QStringList{QStringLiteral("http"), QStringLiteral("https")}
        .contains(base.scheme().toLower()))
        return result({}, QStringLiteral("error"), false,
            QStringLiteral("Catalogue transport is invalid."));
    QString baseText = transport;
    if (baseText.endsWith(QLatin1String("/manifest.json"), Qt::CaseInsensitive))
        baseText.chop(14);
    const int wanted = qMin(5000, qMax(route.value(QStringLiteral("pageSize")).toInt(),
        context.visibleCount));
    QVariantList cards;
    QSet<QString> seen;
    QNetworkAccessManager manager;
    bool failed = false;
    bool sourceHasMore = false;
    for (int offset = 0; offset < 5000 && cards.size() <= wanted; offset += 100) {
        QString path = QStringLiteral("/catalog/%1/%2").arg(type,
            QString::fromUtf8(QUrl::toPercentEncoding(catalogId)));
        if (offset > 0) path += QStringLiteral("/skip=%1").arg(offset);
        const QJsonObject json = fetchJson(manager, QUrl(baseText + path + QStringLiteral(".json")));
        if (!json.value(QStringLiteral("metas")).isArray()) { failed = true; break; }
        const QVariantList metas = json.value(QStringLiteral("metas")).toArray().toVariantList();
        sourceHasMore = metas.size() >= 100;
        const QVariantList page = liveItems(metas, type, context.showExplicit, 100);
        for (const QVariant &value : page) {
            const QVariantMap item = value.toMap();
            const QString key = item.value(QStringLiteral("key")).toString();
            if (seen.contains(key)) continue;
            seen.insert(key);
            cards.append(item);
        }
        if (metas.isEmpty() || !sourceHasMore) break;
    }
    const bool more = cards.size() > wanted || sourceHasMore;
    if (cards.size() > wanted) cards = cards.mid(0, wanted);
    return result(cards, failed && cards.isEmpty() ? QStringLiteral("error")
        : cards.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
        more && !failed, failed && cards.isEmpty()
            ? QStringLiteral("Catalogue could not be loaded.") : QString());
}
} // namespace

namespace {
QString shelfCacheKey(const FeedContext &context, const QString &tab)
{
    const auto stamp = [](const QString &path) {
        const QFileInfo file(path);
        return file.absoluteFilePath() + QLatin1Char(':')
            + QString::number(file.size()) + QLatin1Char(':')
            + QString::number(file.lastModified().toMSecsSinceEpoch());
    };
    return tab + QLatin1Char(':') + QString::number(context.showExplicit)
        + QLatin1Char(':') + stamp(context.paths.imdb)
        + QLatin1Char(':') + stamp(context.paths.mal);
}

QMutex &shelfCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, QVariantList> &shelfCache()
{
    static QHash<QString, QVariantList> cache;
    return cache;
}

QVariantMap nextUpSection(const FeedContext &context)
{
    const QList<FinishedShow> finished = finishedShows(context.recent);
    QVariantList cards;
    const bool hasCached = finished.isEmpty()
        || cachedNextUp(nextUpCacheKey(finished, context.showExplicit), cards);
    return WebFeedValue::section(QStringLiteral("theatre.nextUp"), 1,
        QStringLiteral("Next Up"), QStringLiteral("rail"), cards,
        !hasCached ? QStringLiteral("loading")
            : cards.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
}

QVariantList buildFull(const FeedContext &context)
{
    const QString tab = context.params.value(QStringLiteral("tab")).toString();
    QVariantList out;
    ImdbCatalog imdb(context.paths.imdb, nullptr,
        QStringLiteral("web_theatre_imdb_") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    MalCatalog mal(context.paths.mal, nullptr,
        QStringLiteral("web_theatre_mal_") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    // The top region is shared above the tab bar, including on Library.
    const QVariantList movies = imdb.ready() ? imdb.titleCatalog(
        {{QStringLiteral("type"), QStringLiteral("movie")},
         {QStringLiteral("order"), QStringLiteral("rating")},
         {QStringLiteral("ratingMin"), 8.0}, {QStringLiteral("votesMin"), 200000},
         {QStringLiteral("excludeAnime"), true}}, 0, 12) : QVariantList{};
    const QVariantList shows = imdb.ready() ? imdb.titleCatalog(
        {{QStringLiteral("type"), QStringLiteral("series")},
         {QStringLiteral("order"), QStringLiteral("rating")},
         {QStringLiteral("ratingMin"), 8.2}, {QStringLiteral("votesMin"), 100000},
         {QStringLiteral("excludeAnime"), true}}, 0, 12) : QVariantList{};
    const QVariantList anime = mal.ready() ? mal.animeCatalog(
        {{QStringLiteral("order"), QStringLiteral("score")},
         {QStringLiteral("voteFloor"), 5000}}, 0, 12) : QVariantList{};
    QVariantList featured;
    if (!movies.isEmpty()) featured.append(mediaItems({movies.first()}, QStringLiteral("movie"), context.showExplicit));
    if (!shows.isEmpty()) featured.append(mediaItems({shows.first()}, QStringLiteral("series"), context.showExplicit));
    if (!anime.isEmpty()) featured.append(mediaItems({anime.first()}, QStringLiteral("anime"), context.showExplicit));
    out.append(WebFeedValue::section(QStringLiteral("theatre.featured"), out.size(),
        QStringLiteral("Featured in Theatre"), QStringLiteral("hero"), featured,
        featured.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready")));
    const QList<FinishedShow> finished = finishedShows(context.recent);
    QVariantList cached;
    const bool hasCached = finished.isEmpty()
        || cachedNextUp(nextUpCacheKey(finished, context.showExplicit), cached);
    out.append(WebFeedValue::section(QStringLiteral("theatre.nextUp"), out.size(),
        QStringLiteral("Next Up"), QStringLiteral("rail"), cached,
        !hasCached ? QStringLiteral("loading")
            : cached.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready")));
    if (tab == QLatin1String("library")) {
        // LibraryPage.qml:61 computeRows; LibraryApi.js:98/126/153 filters,
        // sort and ledger; CONTRACT §13: native owns view/ledger.
        out.append(librarySections(context, out.size()));
    } else if (tab == QLatin1String("movies") && imdb.ready()) movieRows(out, imdb, context.showExplicit);
    else if (tab == QLatin1String("shows") && imdb.ready()) showRows(out, imdb, context.showExplicit);
    else if (tab == QLatin1String("anime") && mal.ready()) animeRows(out, mal, context.showExplicit);
    else if (tab == QLatin1String("discover")) {
        // Continue Watching is a separate `continue` subscription in the web surface.
        append(out, tab, QStringLiteral("movies"), QStringLiteral("Top Movies"),
            QStringLiteral("movie"), movies, context.showExplicit, 12);
        append(out, tab, QStringLiteral("shows"), QStringLiteral("Top Shows"),
            QStringLiteral("series"), shows, context.showExplicit, 12);
        append(out, tab, QStringLiteral("anime"), QStringLiteral("Top Anime"),
            QStringLiteral("anime"), anime, context.showExplicit, 12);
    }
    if (out.size() == 2)
        out.append(WebFeedValue::section(QStringLiteral("theatre.%1.unavailable").arg(tab), out.size(),
            QStringLiteral("Theatre"), QStringLiteral("list"), {}, QStringLiteral("error")));
    return out;
}
} // namespace

QVariantList TheatreFeed::build(const FeedContext &context)
{
    const QString tab = context.params.value(QStringLiteral("tab")).toString();
    if (tab != QLatin1String("movies") && tab != QLatin1String("shows")
        && tab != QLatin1String("anime")) return buildFull(context);

    const QString key = shelfCacheKey(context, tab);
    QVariantList cached;
    {
        QMutexLocker lock(&shelfCacheMutex());
        cached = shelfCache().value(key);
    }
    if (!cached.isEmpty()) {
        cached[1] = nextUpSection(context);
        return cached;
    }

    // A fresh tab sends its first real shelf without waiting for every catalogue query.
    // The bridge calls this builder on a worker-owned SQLite connection.
    QVariantList out{
        WebFeedValue::section(QStringLiteral("theatre.featured"), 0,
            QStringLiteral("Featured in Theatre"), QStringLiteral("hero"), {},
            QStringLiteral("loading")),
        nextUpSection(context)
    };
    if (tab == QLatin1String("anime")) {
        MalCatalog mal(context.paths.mal, nullptr,
            QStringLiteral("web_theatre_mal_") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (mal.ready())
            malShelf(out, mal, QStringLiteral("top10"), QStringLiteral("Top 10"),
                {{QStringLiteral("order"), QStringLiteral("members")}}, context.showExplicit, 10);
    } else {
        ImdbCatalog imdb(context.paths.imdb, nullptr,
            QStringLiteral("web_theatre_imdb_") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (imdb.ready())
            imdbShelf(out, imdb, tab, QStringLiteral("top10"), QStringLiteral("Top 10"),
                {{QStringLiteral("type"), tab == QLatin1String("shows")
                    ? QStringLiteral("series") : QStringLiteral("movie")},
                 {QStringLiteral("order"), QStringLiteral("votes")}}, context.showExplicit, 10);
    }
    if (out.size() == 2)
        out.append(WebFeedValue::section(QStringLiteral("theatre.%1.unavailable").arg(tab), 2,
            QStringLiteral("Theatre"), QStringLiteral("list"), {}, QStringLiteral("error")));
    return out;
}

QVariantList TheatreFeed::enrich(const FeedContext &context)
{
    const QString tab = context.params.value(QStringLiteral("tab")).toString();
    QVariantList updated = context.baseSections;
    if (tab == QLatin1String("movies") || tab == QLatin1String("shows")
        || tab == QLatin1String("anime")) {
        const QString key = shelfCacheKey(context, tab);
        bool cached = false;
        {
            QMutexLocker lock(&shelfCacheMutex());
            cached = shelfCache().contains(key);
        }
        if (!cached) {
            updated = buildFull(context);
            QMutexLocker lock(&shelfCacheMutex());
            if (shelfCache().size() >= 8) shelfCache().clear();
            shelfCache().insert(key, updated);
        }
    }
    QNetworkAccessManager manager;
    if (tab == QLatin1String("movies") || tab == QLatin1String("shows")) {
        enrichMovieShow(updated, manager, tab, context.showExplicit);
        enrichExtensions(updated, manager, context, tab);
    } else if (tab == QLatin1String("anime")) {
        enrichAnime(updated, manager, context.showExplicit);
    } else if (tab == QLatin1String("discover")) {
        enrichDiscover(updated, manager, context.showExplicit);
    }
    const QList<FinishedShow> finished = finishedShows(context.recent);
    QVariantList cards;
    const QByteArray cacheKey = nextUpCacheKey(finished, context.showExplicit);
    const bool hasCached = cachedNextUp(cacheKey, cards);
    if (!hasCached) for (const FinishedShow &candidate : finished) {
        const QVariantMap meta = fetchSeriesMeta(manager, candidate.root);
        const QVariantMap next = nextEpisode(meta.value(QStringLiteral("videos")).toList(),
            candidate.entry.value(QStringLiteral("id")).toString(), candidate.root);
        if (next.isEmpty()) continue; // no honest card for a missing/ended episode list
        const int season = next.value(QStringLiteral("season")).toInt();
        const int episode = next.value(QStringLiteral("episode")).toInt();
        QString title = meta.value(QStringLiteral("name")).toString();
        if (title.isEmpty())
            title = candidate.entry.value(QStringLiteral("title")).toString();
        QString subtitle = QStringLiteral("S%1 · E%2").arg(season).arg(episode);
        const QString episodeTitle = next.value(QStringLiteral("name"),
            next.value(QStringLiteral("title"))).toString();
        if (!episodeTitle.isEmpty()) subtitle += QStringLiteral(" · ") + episodeTitle;
        const QVariantMap row{
            {QStringLiteral("id"), next.value(QStringLiteral("id"))},
            {QStringLiteral("tt"), candidate.root},
            {QStringLiteral("type"), QStringLiteral("series")},
            {QStringLiteral("title"), title},
            {QStringLiteral("cover"), meta.value(QStringLiteral("poster"),
                candidate.entry.value(QStringLiteral("cover")))},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("progress"), 0},
            {QStringLiteral("resume"), QVariantMap{
                {QStringLiteral("showId"), candidate.root},
                {QStringLiteral("season"), season},
                {QStringLiteral("episode"), episode},
                {QStringLiteral("unitId"), next.value(QStringLiteral("id"))}}}};
        QVariantMap card = WebFeedValue::item(row, QStringLiteral("Theatre"),
            QStringLiteral("series"));
        card.insert(QStringLiteral("badge"), QStringLiteral("S%1 · E%2")
            .arg(season).arg(episode));
        cards.append(card);
    }
    if (!hasCached && !finished.isEmpty()) storeNextUp(cacheKey, cards);
    for (QVariant &value : updated) {
        QVariantMap section = value.toMap();
        if (section.value(QStringLiteral("id")).toString()
            != QLatin1String("theatre.nextUp")) continue;
        section.insert(QStringLiteral("items"), cards);
        section.insert(QStringLiteral("state"), cards.isEmpty()
            ? QStringLiteral("empty") : QStringLiteral("ready"));
        value = section;
        break;
    }
    return updated;
}

namespace {
const bool registered = FeedRegistry::add({
    QStringLiteral("world"), QStringLiteral("Theatre"),
    [](const QVariantMap &params) {
        return validTheatreView(params) && QStringList{QStringLiteral("discover"), QStringLiteral("movies"),
                           QStringLiteral("shows"), QStringLiteral("anime"),
                           QStringLiteral("library")}
            .contains(params.value(QStringLiteral("tab")).toString());
    },
    [](const QVariantMap &) -> QVariantList {
        return {WebFeedValue::section(QStringLiteral("world"), 0,
            QStringLiteral("Loading"), QStringLiteral("list"), {},
            QStringLiteral("loading"))};
    }, &TheatreFeed::build, true, true, true, false, &TheatreFeed::enrich});

const bool extensionPageRegistered = FeedRegistry::add({
    QStringLiteral("seeAll"), QStringLiteral("TheatreExtension"), &validExtensionRoute,
    [](const QVariantMap &) -> QVariantList {
        return {WebFeedValue::section(QStringLiteral("seeAll.results"), 0,
            QStringLiteral("See All"), QStringLiteral("grid"), {}, QStringLiteral("loading"))};
    }, &extensionPage, false, false, true});

bool validLibraryAction(const QVariantMap &payload)
{
    const QVariantMap item = payload.value(QStringLiteral("item")).toMap();
    const QVariantMap ref = item.value(QStringLiteral("ref")).toMap();
    return item.value(QStringLiteral("world")).toString() == QLatin1String("Theatre")
        && !ref.value(QStringLiteral("id")).toString().isEmpty();
}

const bool markWatchedRegistered = ActionRegistry::add({
    QStringLiteral("world.theatre.markWatched"),
    [](const QVariantMap &payload) {
        return validLibraryAction(payload)
            && payload.value(QStringLiteral("watched")).metaType().id() == QMetaType::Bool;
    },
    [](ColosseumWebBridge &bridge, const QVariantMap &payload, ActionRegistry::Completion done) {
        auto *progress = qobject_cast<ProgressStore *>(bridge.service(QStringLiteral("Progress")));
        if (!progress || !progress->healthy()) {
            done({{QStringLiteral("ok"), false}, {QStringLiteral("error"),
                QStringLiteral("Progress is unavailable.")}});
            return;
        }
        const QString id = payload.value(QStringLiteral("item")).toMap()
            .value(QStringLiteral("ref")).toMap().value(QStringLiteral("id")).toString();
        const bool watched = payload.value(QStringLiteral("watched")).toBool();
        progress->setWatchedMark(id, watched);
        if (progress->watchedMark(id) != (watched ? 1 : -1)
            || !progress->watchedMarkIsManual(id)) {
            done({{QStringLiteral("ok"), false}, {QStringLiteral("error"),
                QStringLiteral("The watched mark could not be saved.")}});
            return;
        }
        done({{QStringLiteral("ok"), true}});
    }});

const bool dismissRegistered = ActionRegistry::add({
    QStringLiteral("world.theatre.dismiss"), &validLibraryAction,
    [](ColosseumWebBridge &bridge, const QVariantMap &payload, ActionRegistry::Completion done) {
        auto *progress = qobject_cast<ProgressStore *>(bridge.service(QStringLiteral("Progress")));
        if (!progress || !progress->healthy()) {
            done({{QStringLiteral("ok"), false}, {QStringLiteral("error"),
                QStringLiteral("Progress is unavailable.")}});
            return;
        }
        const QString id = payload.value(QStringLiteral("item")).toMap()
            .value(QStringLiteral("ref")).toMap().value(QStringLiteral("id")).toString();
        progress->forget(QStringLiteral("video"), id);
        done({{QStringLiteral("ok"), true}});
    }});
} // namespace
