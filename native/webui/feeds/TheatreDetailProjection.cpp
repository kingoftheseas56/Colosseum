#include "TheatreDetailProjection.h"
#include "FeedValue.h"

#include <QDate>
#include <QHash>
#include <QSet>
#include <QStringList>
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
    const QVariantMap animeOrder = ctx.nativeSnapshot.value(QStringLiteral("animeOrder")).toMap();
    const bool anime = requested.startsWith(QLatin1String("mal:"))
        || requested.startsWith(QLatin1String("kitsu:"))
        || animeOrder.value(QStringLiteral("status")) == QLatin1String("mapped");
    const bool resolved = !meta.isEmpty();
    const QString title = meta.value(QStringLiteral("name"), ctx.params.value(QStringLiteral("title"))).toString();
    const QString resolvedId = meta.value(QStringLiteral("id"), requested).toString();
    const QVariantList videos = meta.value(QStringLiteral("videos")).toList();
    const QVariantList progressRows = ctx.nativeSnapshot.value(QStringLiteral("recent")).toList();
    const QVariantMap downloadStates = ctx.nativeSnapshot.value(QStringLiteral("downloadStates")).toMap();
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
    const bool absoluteAvailable = anime && animeOrder.value(QStringLiteral("absoluteComplete")).toBool()
        && !animeOrder.value(QStringLiteral("episodes")).toList().isEmpty();
    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
    const QString requestedOrder = view.value(QStringLiteral("order"),
        animeOrder.value(QStringLiteral("defaultOrder"), QStringLiteral("seasons"))).toString();
    const QString order = absoluteAvailable && requestedOrder == QLatin1String("absolute")
        ? QStringLiteral("absolute") : QStringLiteral("seasons");
    int selected = view.value(QStringLiteral("season"), ctx.nativeSnapshot.value(QStringLiteral("lastSeason"), -1)).toInt();
    if (!seen.contains(selected) && !seasons.isEmpty()) selected = seasons.first();

    QVariantList seasonRows;
    for (int season : seasons) {
        int count = 0;
        QDate first;
        QDate last;
        for (const QVariant &value : videos) {
            const QVariantMap video = value.toMap();
            if (video.value(QStringLiteral("season"), video.value(QStringLiteral("seasonNumber"))).toInt() != season)
                continue;
            ++count;
            const QDate aired = QDate::fromString(video.value(QStringLiteral("released")).toString().left(10), Qt::ISODate);
            if (aired.isValid()) {
                if (!first.isValid() || aired < first) first = aired;
                if (!last.isValid() || aired > last) last = aired;
            }
        }
        seasonRows.append(QVariantMap{{QStringLiteral("number"), season},
            {QStringLiteral("label"), season == 0 ? QStringLiteral("Specials") : QStringLiteral("Season %1").arg(season)},
            {QStringLiteral("count"), count},
            {QStringLiteral("from"), first.isValid() ? first.toString(QStringLiteral("MMM yyyy")) : QString()},
            {QStringLiteral("to"), last.isValid() ? last.toString(QStringLiteral("MMM yyyy")) : QString()}});
    }

    QVariantList allEpisodes;
    QString nextUp;
    const QVariantList orderedVideos = order == QLatin1String("absolute")
        ? animeOrder.value(QStringLiteral("episodes")).toList() : videos;
    for (const QVariant &value : orderedVideos) {
        const QVariantMap video = value.toMap();
        const int season = video.value(QStringLiteral("season"), video.value(QStringLiteral("seasonNumber"))).toInt();
        if (order == QLatin1String("absolute") && season == 0) continue;
        if (order != QLatin1String("absolute") && season != selected) continue;
        const int number = video.value(QStringLiteral("episode"), video.value(QStringLiteral("number"))).toInt();
        const int displayNumber = order == QLatin1String("absolute")
            ? video.value(QStringLiteral("absoluteNumber")).toInt() : number;
        const QString episodeId = video.value(QStringLiteral("id")).toString();
        const QVariantMap note = progress.value(episodeId);
        const QVariantMap download = downloadStates.value(episodeId).toMap();
        const double ratio = qBound(0.0, note.value(QStringLiteral("progress")).toDouble(), 1.0);
        const bool watched = note.value(QStringLiteral("watched")).toBool() || ratio >= 0.85;
        if (nextUp.isEmpty() && !watched) nextUp = episodeId;
        allEpisodes.append(QVariantMap{{QStringLiteral("id"), episodeId}, {QStringLiteral("season"), season},
            {QStringLiteral("number"), number}, {QStringLiteral("displayNumber"), displayNumber},
            {QStringLiteral("title"), video.value(QStringLiteral("title"), video.value(QStringLiteral("name"))).toString()},
            {QStringLiteral("overview"), video.value(QStringLiteral("overview"), video.value(QStringLiteral("description"))).toString()},
            {QStringLiteral("thumbnail"), video.value(QStringLiteral("thumbnail")).toString()},
            {QStringLiteral("airDate"), video.value(QStringLiteral("released")).toString()},
            {QStringLiteral("duration"), video.value(QStringLiteral("runtime")).toString()},
            {QStringLiteral("progress"), ratio}, {QStringLiteral("watched"), watched},
            {QStringLiteral("downloadState"), download.value(QStringLiteral("state")).toString()},
            {QStringLiteral("downloadProgress"), download.value(QStringLiteral("progress")).toDouble()},
            {QStringLiteral("sourceState"), QString()}});
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
    QString primaryLabel = QStringLiteral("Play");
    QString primaryTargetId = type == QLatin1String("movie") ? requested : nextUp;
    if (type == QLatin1String("movie")) {
        const double position = progress.value(requested).value(QStringLiteral("progress")).toDouble();
        if (position > 0 && position < 0.85) primaryLabel = QStringLiteral("Resume");
    } else {
        if (primaryTargetId.isEmpty() && !allEpisodes.isEmpty())
            primaryTargetId = allEpisodes.first().toMap().value(QStringLiteral("id")).toString();
        for (const QVariant &value : allEpisodes) {
            const QVariantMap episode = value.toMap();
            if (episode.value(QStringLiteral("id")) != primaryTargetId) continue;
            const double position = episode.value(QStringLiteral("progress")).toDouble();
            primaryLabel = QStringLiteral("%1 S%2 E%3")
                .arg(position > 0 && position < 0.85 ? QStringLiteral("Resume") : QStringLiteral("Play"))
                .arg(episode.value(QStringLiteral("season")).toInt())
                .arg(episode.value(QStringLiteral("number")).toInt());
            break;
        }
    }
    QString logo = meta.value(QStringLiteral("logo")).toString();
    if (logo.isEmpty() && resolvedId.startsWith(QLatin1String("tt")))
        logo = QStringLiteral("https://images.metahub.space/logo/medium/%1/img").arg(resolvedId);
    QVariantList genreList = meta.value(QStringLiteral("genres")).toList();
    QVariantList facts;
    const auto addFact = [&facts](const QString &label, const QString &value) {
        if (!value.trimmed().isEmpty())
            facts.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}});
    };
    addFact(QStringLiteral("Status"), meta.value(QStringLiteral("status")).toString());
    addFact(QStringLiteral("Released"), meta.value(QStringLiteral("year")).toString());
    addFact(QStringLiteral("Runtime"), meta.value(QStringLiteral("runtime")).toString());
    QStringList genres;
    for (const QVariant &genre : genreList) {
        const QString name = genre.toString().trimmed();
        if (!name.isEmpty()) genres.append(name);
    }
    addFact(QStringLiteral("Genres"), genres.join(QStringLiteral(" · ")));
    for (const QVariant &entry : scores) {
        const QVariantMap score = entry.toMap();
        const QString provider = score.value(QStringLiteral("provider")).toString();
        const QString label = provider == QLatin1String("mal") ? QStringLiteral("MAL")
            : provider == QLatin1String("imdb") ? QStringLiteral("IMDb") : provider;
        addFact(label, QStringLiteral("%1 / %2").arg(score.value(QStringLiteral("value")).toString(),
                                                   score.value(QStringLiteral("scale")).toString()));
    }
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
         {QStringLiteral("logo"), logo},
         {QStringLiteral("kind"), anime ? QStringLiteral("anime") : type},
         {QStringLiteral("year"), meta.value(QStringLiteral("year")).toString()},
         {QStringLiteral("genres"), genreList}, {QStringLiteral("rating"), meta.value(QStringLiteral("imdbRating")).toString()},
         {QStringLiteral("scores"), scores},
         {QStringLiteral("watchedMark"), ctx.nativeSnapshot.value(QStringLiteral("watchedMark")).toInt()},
         {QStringLiteral("runtime"), meta.value(QStringLiteral("runtime")).toString()},
         {QStringLiteral("synopsis"), meta.value(QStringLiteral("description")).toString()},
         {QStringLiteral("saved"), ctx.nativeSnapshot.value(QStringLiteral("saved")).toBool()},
         {QStringLiteral("notify"), ctx.nativeSnapshot.value(QStringLiteral("notify"), true).toBool()},
         {QStringLiteral("primaryLabel"), primaryLabel},
         {QStringLiteral("primaryTargetId"), primaryTargetId}});
    if (!resolved) hero.insert(QStringLiteral("error"), error);
    out.append(hero);
    out.append(section(QStringLiteral("seasons"), 1, QStringLiteral("Seasons"), seasons.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.seasons")}, {QStringLiteral("selected"), selected},
                        {QStringLiteral("order"), order}, {QStringLiteral("absoluteAvailable"), absoluteAvailable},
                        {QStringLiteral("totalCount"), animeOrder.value(QStringLiteral("episodes")).toList().size()},
                        {QStringLiteral("rows"), seasonRows}}));
    out.append(section(QStringLiteral("episodes"), 2, QStringLiteral("Episodes"), episodeWindow.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.episodes")}, {QStringLiteral("season"), selected},
                        {QStringLiteral("nextUpId"), nextUp}, {QStringLiteral("windowStart"), windowStart},
                        {QStringLiteral("totalCount"), allEpisodes.size()},
                        {QStringLiteral("rows"), episodeWindow}},
                       allEpisodes.size() > qMax(0, ctx.visibleCount) + episodeWindow.size()));
    out.append(section(QStringLiteral("cast"), 3, QStringLiteral("Cast"), castPeople.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.cast")}, {QStringLiteral("people"), castPeople}}));
    out.append(WebFeedValue::section(QStringLiteral("related"), 4, QStringLiteral("More Like This"),
                                      QStringLiteral("rail"), related,
                                      related.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready")));
    out.append(section(QStringLiteral("facts"), 5, QStringLiteral("Information"),
                       facts.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.facts")}, {QStringLiteral("rows"), facts}}));
    const QString sourceTarget = ctx.params.value(QStringLiteral("sourceTarget"),
        type == QLatin1String("movie") ? requested : QString()).toString();
    out.append(section(QStringLiteral("sources"), 6, QStringLiteral("Sources"), sourceRows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.sources")},
                        {QStringLiteral("targetId"), sourceTarget},
                        {QStringLiteral("rows"), sourceRows}}));
    return out;
}

} // namespace TheatreDetailProjection
