#include "TheatreDetailProjection.h"
#include "FeedValue.h"

#include <QHash>
#include <QSet>
#include <algorithm>

namespace TheatreDetailProjection {
QVariantMap section(const QString &id, int index, const QString &title,
                    const QString &state, const QVariantMap &data = {}, bool more = false)
{
    auto value = WebFeedValue::section(id, index, title, QStringLiteral("custom"), {}, state, more);
    if (!data.isEmpty()) value.insert(QStringLiteral("data"), data);
    return value;
}

QVariantList build(const FeedContext &ctx, const QVariantMap &meta,
                   const QVariantList &related, const QVariantList &sourceRows)
{
    const QString requested = ctx.params.value(QStringLiteral("id")).toString();
    const QString type = ctx.params.value(QStringLiteral("type")).toString();
    const bool resolved = !meta.isEmpty();
    const QString title = meta.value(QStringLiteral("name"), ctx.params.value(QStringLiteral("title"))).toString();
    const QString resolvedId = meta.value(QStringLiteral("id"), requested).toString();
    const QVariantList videos = meta.value(QStringLiteral("videos")).toList();
    const QVariantList progressRows = ctx.nativeSnapshot.value(QStringLiteral("recent")).toList();
    QVariantList scores;
    const QVariantMap malScore = ctx.nativeSnapshot.value(QStringLiteral("malScore")).toMap();
    if (malScore.value(QStringLiteral("score")).toDouble() > 0)
        scores.append(QVariantMap{{QStringLiteral("provider"), QStringLiteral("mal")},
            {QStringLiteral("value"), malScore.value(QStringLiteral("score"))},
            {QStringLiteral("scale"), 10}, {QStringLiteral("votes"), malScore.value(QStringLiteral("scored_by"))}});
    bool imdbOk = false;
    const double imdbScore = meta.value(QStringLiteral("imdbRating")).toString().toDouble(&imdbOk);
    if (imdbOk && imdbScore > 0)
        scores.append(QVariantMap{{QStringLiteral("provider"), QStringLiteral("imdb")},
            {QStringLiteral("value"), imdbScore}, {QStringLiteral("scale"), 10}});
    QHash<QString, QVariantMap> progress;
    for (const QVariant &value : progressRows) {
        const QVariantMap row = value.toMap();
        progress.insert(row.value(QStringLiteral("id")).toString(), row);
    }
    QSet<int> seen;
    for (const QVariant &value : videos) {
        const QVariantMap video = value.toMap();
        seen.insert(video.value(QStringLiteral("season"), video.value(QStringLiteral("seasonNumber"))).toInt());
    }
    QList<int> seasons = seen.values();
    std::sort(seasons.begin(), seasons.end(), [](int a, int b) { return a == 0 ? false : b == 0 ? true : a < b; });
    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
    const QString order = view.value(QStringLiteral("order"), QStringLiteral("seasons")).toString();
    int selected = view.value(QStringLiteral("season"), ctx.nativeSnapshot.value(QStringLiteral("lastSeason"))).toInt();
    if (!seen.contains(selected) && !seasons.isEmpty()) selected = seasons.first();

    QVariantList seasonRows;
    for (int season : seasons) {
        int count = 0;
        for (const QVariant &value : videos)
            if (value.toMap().value(QStringLiteral("season"), value.toMap().value(QStringLiteral("seasonNumber"))).toInt() == season)
                ++count;
        seasonRows.append(QVariantMap{{QStringLiteral("number"), season},
            {QStringLiteral("label"), season == 0 ? QStringLiteral("Specials") : QStringLiteral("Season %1").arg(season)},
            {QStringLiteral("count"), count}});
    }

    QVariantList allEpisodes;
    QString nextUp;
    for (const QVariant &value : videos) {
        const QVariantMap video = value.toMap();
        const int season = video.value(QStringLiteral("season"), video.value(QStringLiteral("seasonNumber"))).toInt();
        if (order != QLatin1String("absolute") && season != selected) continue;
        const int number = video.value(QStringLiteral("episode"), video.value(QStringLiteral("number"))).toInt();
        const QString episodeId = video.value(QStringLiteral("id")).toString();
        const QVariantMap note = progress.value(episodeId);
        const double ratio = qBound(0.0, note.value(QStringLiteral("progress")).toDouble(), 1.0);
        const bool watched = note.value(QStringLiteral("watched")).toBool() || ratio >= 0.85;
        if (nextUp.isEmpty() && !watched) nextUp = episodeId;
        allEpisodes.append(QVariantMap{{QStringLiteral("id"), episodeId}, {QStringLiteral("season"), season},
            {QStringLiteral("number"), number}, {QStringLiteral("displayNumber"), number},
            {QStringLiteral("title"), video.value(QStringLiteral("title"), video.value(QStringLiteral("name"))).toString()},
            {QStringLiteral("overview"), video.value(QStringLiteral("overview"), video.value(QStringLiteral("description"))).toString()},
            {QStringLiteral("thumbnail"), video.value(QStringLiteral("thumbnail")).toString()},
            {QStringLiteral("airDate"), video.value(QStringLiteral("released")).toString()},
            {QStringLiteral("duration"), video.value(QStringLiteral("runtime")).toString()},
            {QStringLiteral("progress"), ratio}, {QStringLiteral("watched"), watched},
            {QStringLiteral("downloadState"), QString()}, {QStringLiteral("sourceState"), QString()}});
    }
    int windowStart = qMax(0, ctx.visibleCount);
    if (order == QLatin1String("absolute")) {
        int nextIndex = 0;
        for (int i = 0; i < allEpisodes.size(); ++i) {
            if (allEpisodes.at(i).toMap().value(QStringLiteral("id")) == nextUp) {
                nextIndex = i;
                break;
            }
        }
        const int pageCount = (allEpisodes.size() + 99) / 100;
        if (pageCount > 0)
            windowStart = (((nextIndex / 100) + (windowStart / 100)) % pageCount) * 100;
    }
    const QVariantList episodeWindow = allEpisodes.mid(windowStart, 100);
    QVariantList facts;
    for (const auto &pair : {qMakePair(QStringLiteral("Year"), meta.value(QStringLiteral("year")).toString()),
                             qMakePair(QStringLiteral("Runtime"), meta.value(QStringLiteral("runtime")).toString()),
                             qMakePair(QStringLiteral("Rating"), meta.value(QStringLiteral("imdbRating")).toString())})
        if (!pair.second.isEmpty()) facts.append(QVariantMap{{QStringLiteral("label"), pair.first}, {QStringLiteral("value"), pair.second}});
    QVariantList genreList = meta.value(QStringLiteral("genres")).toList();
    QVariantList castPeople;
    for (const QVariant &value : meta.value(QStringLiteral("cast")).toList()) {
        const QVariantMap person = value.toMap();
        const QString name = person.isEmpty() ? value.toString()
            : person.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        castPeople.append(QVariantMap{{QStringLiteral("name"), name},
            {QStringLiteral("role"), person.value(QStringLiteral("role")).toString()},
            {QStringLiteral("image"), person.value(QStringLiteral("image")).toString()}});
    }
    QVariantList out;
    const QString state = resolved ? QStringLiteral("ready") : QStringLiteral("error");
    const QString error = QStringLiteral("Title details are unavailable right now.");
    auto hero = section(QStringLiteral("hero"), 0, QStringLiteral("Title"), state,
        {{QStringLiteral("schema"), QStringLiteral("theatre.hero")}, {QStringLiteral("id"), requested},
         {QStringLiteral("resolvedId"), resolvedId}, {QStringLiteral("type"), type},
         {QStringLiteral("title"), title}, {QStringLiteral("banner"), meta.value(QStringLiteral("background")).toString()},
         {QStringLiteral("cover"), meta.value(QStringLiteral("poster"), ctx.params.value(QStringLiteral("cover"))).toString()},
         {QStringLiteral("logo"), meta.value(QStringLiteral("logo")).toString()},
         {QStringLiteral("year"), meta.value(QStringLiteral("year")).toString()},
         {QStringLiteral("genres"), genreList}, {QStringLiteral("rating"), meta.value(QStringLiteral("imdbRating")).toString()},
         {QStringLiteral("scores"), scores},
         {QStringLiteral("watchedMark"), ctx.nativeSnapshot.value(QStringLiteral("watchedMark")).toInt()},
         {QStringLiteral("runtime"), meta.value(QStringLiteral("runtime")).toString()},
         {QStringLiteral("synopsis"), meta.value(QStringLiteral("description")).toString()},
         {QStringLiteral("saved"), ctx.nativeSnapshot.value(QStringLiteral("saved")).toBool()},
         {QStringLiteral("notify"), ctx.nativeSnapshot.value(QStringLiteral("notify"), true).toBool()},
         {QStringLiteral("primaryLabel"), type == QLatin1String("movie") ? QStringLiteral("Watch") : QStringLiteral("Start Watching")}});
    if (!resolved) hero.insert(QStringLiteral("error"), error);
    out.append(hero);
    out.append(section(QStringLiteral("facts"), 1, QStringLiteral("Facts"), facts.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.facts")}, {QStringLiteral("rows"), facts}}));
    out.append(section(QStringLiteral("seasons"), 2, QStringLiteral("Seasons"), seasons.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.seasons")}, {QStringLiteral("selected"), selected},
                        {QStringLiteral("order"), order}, {QStringLiteral("absoluteAvailable"), !videos.isEmpty()}, {QStringLiteral("rows"), seasonRows}}));
    out.append(section(QStringLiteral("episodes"), 3, QStringLiteral("Episodes"), episodeWindow.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.episodes")}, {QStringLiteral("season"), selected},
                        {QStringLiteral("nextUpId"), nextUp}, {QStringLiteral("windowStart"), windowStart},
                        {QStringLiteral("rows"), episodeWindow}},
                       allEpisodes.size() > qMax(0, ctx.visibleCount) + episodeWindow.size()));
    out.append(section(QStringLiteral("cast"), 4, QStringLiteral("Cast"), castPeople.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.cast")}, {QStringLiteral("people"), castPeople}}));
    const QString sourceTarget = ctx.params.value(QStringLiteral("sourceTarget"),
        type == QLatin1String("movie") ? requested : QString()).toString();
    out.append(section(QStringLiteral("sources"), 5, QStringLiteral("Sources"), sourceRows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.sources")},
                        {QStringLiteral("targetId"), sourceTarget},
                        {QStringLiteral("rows"), sourceRows}}));
    out.append(WebFeedValue::section(QStringLiteral("related"), 6, QStringLiteral("More Like This"),
                                      QStringLiteral("rail"), related,
                                      related.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready")));
    return out;
}

} // namespace TheatreDetailProjection
