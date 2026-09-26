#include "ActionRegistry.h"
#include "FeedHttp.h"
#include "FeedRegistry.h"
#include "FeedValue.h"
#include "../ColosseumWebBridge.h"

#include "../../CollectionStore.h"
#include "../../ProgressStore.h"
#include "../../engine/ExtensionsStore.h"
#include "../../engine/ImdbCatalog.h"
#include "../../player/DownloadStore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSharedPointer>
#include <QTimer>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace {
const QString kFeed = QStringLiteral("detail.theatre");
QMutex sourceMutex;
QHash<QString, QVariantMap> sourceChoices;
QHash<QString, QVariantMap> metaCache;

QVariantMap loadMeta(const QString &id, const QString &type)
{
    {
        QMutexLocker lock(&sourceMutex);
        if (metaCache.contains(id)) return metaCache.value(id);
    }
    QVariantMap meta;
    if (id.startsWith(QLatin1String("tt"))) {
        const auto reply = WebFeedHttp::request(QUrl(
            QStringLiteral("https://v3-cinemeta.strem.io/meta/%1/%2.json").arg(type, id)));
        if (reply.ok) meta = reply.json.object().value(QStringLiteral("meta")).toObject().toVariantMap();
    } else if (id.startsWith(QLatin1String("mal:")) || id.startsWith(QLatin1String("kitsu:"))) {
        for (const QString &candidate : {QStringLiteral("series"), QStringLiteral("movie")}) {
            const auto reply = WebFeedHttp::request(QUrl(
                QStringLiteral("https://anime-kitsu.strem.fun/meta/%1/%2.json")
                    .arg(candidate, QString::fromLatin1(QUrl::toPercentEncoding(id)))));
            if (reply.ok) meta = reply.json.object().value(QStringLiteral("meta")).toObject().toVariantMap();
            if (!meta.isEmpty()) break;
        }
        const QString imdb = meta.value(QStringLiteral("imdb_id")).toString();
        if (type == QLatin1String("series") && imdb.startsWith(QLatin1String("tt"))) {
            const auto reply = WebFeedHttp::request(QUrl(
                QStringLiteral("https://v3-cinemeta.strem.io/meta/series/%1.json").arg(imdb)));
            const QVariantMap pivot = reply.ok
                ? reply.json.object().value(QStringLiteral("meta")).toObject().toVariantMap() : QVariantMap{};
            if (!pivot.value(QStringLiteral("videos")).toList().isEmpty()) meta = pivot;
        }
    }
    if (!meta.isEmpty()) {
        QMutexLocker lock(&sourceMutex);
        metaCache.insert(id, meta);
    }
    return meta;
}

QVariantMap episodeFor(const QString &titleId, const QString &episodeId)
{
    QMutexLocker lock(&sourceMutex);
    for (const QVariant &value : metaCache.value(titleId).value(QStringLiteral("videos")).toList()) {
        const QVariantMap video = value.toMap();
        if (video.value(QStringLiteral("id")).toString() == episodeId) return video;
    }
    return {};
}

bool accepts(const QVariantMap &manifest, const QString &type, const QString &id)
{
    const QVariantList resources = manifest.value(QStringLiteral("resources")).toList();
    bool specific = false;
    bool generic = false;
    auto matches = [&type, &id](const QVariantMap &resource) {
        if (!resource.value(QStringLiteral("types")).toStringList().contains(type)) return false;
        const QVariantList prefixes = resource.value(QStringLiteral("idPrefixes")).toList();
        if (prefixes.isEmpty()) return true;
        for (const QVariant &prefix : prefixes)
            if (id.startsWith(prefix.toString())) return true;
        return false;
    };
    for (const QVariant &value : resources) {
        const QVariantMap resource = value.toMap();
        if (resource.value(QStringLiteral("name")) == QLatin1String("stream")) {
            specific = true;
            if (matches(resource)) return true;
        } else if (value.toString() == QLatin1String("stream")) generic = true;
    }
    return !specific && generic && matches(manifest);
}

QUrl streamEndpoint(const QVariantMap &extension, const QString &type, const QString &id)
{
    QUrl url(extension.value(QStringLiteral("transportUrl")).toString());
    if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")))
        return {};
    QString path = url.path();
    if (path.endsWith(QLatin1String("/manifest.json"), Qt::CaseInsensitive))
        path.chop(QStringLiteral("/manifest.json").size());
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    url.setPath(path + QStringLiteral("/stream/") + type + QLatin1Char('/') + id + QStringLiteral(".json"));
    return url;
}

QVariantList resolveSources(const FeedContext &ctx, const QString &target)
{
    QVariantList publicRows;
    QHash<QString, QVariantMap> nextChoices;
    const QString type = ctx.params.value(QStringLiteral("type")).toString();
    int priority = 0;
    for (const QVariant &value : ctx.extensions) {
        const QVariantMap extension = value.toMap();
        if (!extension.value(QStringLiteral("enabled")).toBool()
            || !accepts(extension.value(QStringLiteral("manifest")).toMap(), type, target))
            continue;
        const QUrl endpoint = streamEndpoint(extension, type, target);
        if (endpoint.isEmpty()) continue;
        const auto reply = WebFeedHttp::request(endpoint, {}, 7000);
        const QString provider = extension.value(QStringLiteral("manifest")).toMap()
            .value(QStringLiteral("name"), extension.value(QStringLiteral("id"))).toString();
        if (!reply.ok) continue;
        for (const QVariant &streamValue : reply.json.object().value(QStringLiteral("streams")).toArray().toVariantList()) {
            const QVariantMap stream = streamValue.toMap();
            const QString hash = stream.value(QStringLiteral("infoHash")).toString();
            const QString direct = stream.value(QStringLiteral("url")).toString();
            if (hash.isEmpty() && direct.isEmpty()) continue;
            const QByteArray identity = (ctx.params.value(QStringLiteral("id")).toString()
                + QLatin1Char('|') + target + QLatin1Char('|')
                + QString::number(ctx.subscriptionId) + QLatin1Char('|')
                + QString::number(ctx.generation) + QLatin1Char('|')
                + extension.value(QStringLiteral("id")).toString() + QLatin1Char('|')
                + hash + QLatin1Char('|') + direct).toUtf8();
            const QString key = QString::fromLatin1(QCryptographicHash::hash(
                identity, QCryptographicHash::Sha256).toHex().left(24));
            const QString label = stream.value(QStringLiteral("name"), stream.value(QStringLiteral("title"))).toString();
            const QString hay = label.toLower();
            const QString quality = hay.contains(QLatin1String("2160p")) || hay.contains(QLatin1String("4k"))
                ? QStringLiteral("4K") : hay.contains(QLatin1String("1080p")) ? QStringLiteral("1080p")
                : hay.contains(QLatin1String("720p")) ? QStringLiteral("720p") : QStringLiteral("SD");
            publicRows.append(QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("label"), label},
                {QStringLiteral("quality"), quality}, {QStringLiteral("provider"), provider},
                {QStringLiteral("availability"), QStringLiteral("available")}});
            QVariantMap privateRow = stream;
            const bool torrent = !hash.isEmpty();
            const QVariantMap proxy = stream.value(QStringLiteral("behaviorHints")).toMap()
                .value(QStringLiteral("proxyHeaders")).toMap();
            const QVariantMap headers = proxy.contains(QStringLiteral("request"))
                ? proxy.value(QStringLiteral("request")).toMap() : proxy;
            privateRow.insert(QStringLiteral("infoHash"), torrent ? hash : QStringLiteral("url:") + direct);
            privateRow.insert(QStringLiteral("url"), direct);
            privateRow.insert(QStringLiteral("headers"), torrent ? QVariantMap{} : headers);
            privateRow.insert(QStringLiteral("quality"), quality);
            privateRow.insert(QStringLiteral("rank"), quality == QLatin1String("4K") ? 4
                : quality == QLatin1String("1080p") ? 3 : quality == QLatin1String("720p") ? 2 : 1);
            privateRow.insert(QStringLiteral("release"), label);
            privateRow.insert(QStringLiteral("sourceName"), provider);
            privateRow.insert(QStringLiteral("streamKind"), torrent ? QStringLiteral("Torrent") : QStringLiteral("Direct"));
            privateRow.insert(QStringLiteral("streamLabel"), torrent ? QStringLiteral("P2P stream") : QStringLiteral("HTTP stream"));
            privateRow.insert(QStringLiteral("addonId"), extension.value(QStringLiteral("id")));
            privateRow.insert(QStringLiteral("addonName"), provider);
            privateRow.insert(QStringLiteral("addonPriority"), priority);
            privateRow.insert(QStringLiteral("targetId"), target);
            privateRow.insert(QStringLiteral("titleId"), ctx.params.value(QStringLiteral("id")));
            nextChoices.insert(key, privateRow);
        }
        ++priority;
    }
    QMutexLocker lock(&sourceMutex);
    for (auto it = sourceChoices.begin(); it != sourceChoices.end();) {
        if (it->value(QStringLiteral("titleId")) == ctx.params.value(QStringLiteral("id"))
            && it->value(QStringLiteral("targetId")) == target)
            it = sourceChoices.erase(it);
        else ++it;
    }
    for (auto it = nextChoices.cbegin(); it != nextChoices.cend(); ++it)
        sourceChoices.insert(it.key(), it.value());
    return publicRows;
}

QVariantMap section(const QString &id, int index, const QString &title,
                    const QString &state, const QVariantMap &data = {}, bool more = false)
{
    auto value = WebFeedValue::section(id, index, title, QStringLiteral("custom"), {}, state, more);
    if (!data.isEmpty()) value.insert(QStringLiteral("data"), data);
    return value;
}

bool valid(const QVariantMap &params)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    const QString type = params.value(QStringLiteral("type")).toString();
    return !id.isEmpty() && id.size() <= 180
        && (type == QLatin1String("movie") || type == QLatin1String("series"));
}

QVariantList initial(const QVariantMap &)
{
    return {section(QStringLiteral("hero"), 0, QStringLiteral("Title"), QStringLiteral("loading")),
            section(QStringLiteral("facts"), 1, QStringLiteral("Facts"), QStringLiteral("loading")),
            section(QStringLiteral("seasons"), 2, QStringLiteral("Seasons"), QStringLiteral("loading")),
            section(QStringLiteral("episodes"), 3, QStringLiteral("Episodes"), QStringLiteral("loading")),
            section(QStringLiteral("cast"), 4, QStringLiteral("Cast"), QStringLiteral("loading")),
            section(QStringLiteral("sources"), 5, QStringLiteral("Sources"), QStringLiteral("loading")),
            WebFeedValue::section(QStringLiteral("related"), 6, QStringLiteral("More Like This"),
                                  QStringLiteral("rail"), {}, QStringLiteral("loading"))};
}

void capture(ColosseumWebBridge &bridge, FeedContext &ctx)
{
    const QString id = ctx.params.value(QStringLiteral("id")).toString();
    auto *progress = qobject_cast<ProgressStore *>(bridge.service(QStringLiteral("Progress")));
    auto *collection = qobject_cast<CollectionStore *>(bridge.service(QStringLiteral("Collection")));
    if (progress) {
        QVariantList exactRows;
        for (const QVariant &value : progress->syncEntries()) {
            const QVariantMap row = value.toMap();
            const QString episodeId = row.value(QStringLiteral("id")).toString();
            if (row.value(QStringLiteral("kind")) == QLatin1String("video")
                && (episodeId == id || episodeId.startsWith(id + QLatin1Char(':'))))
                exactRows.append(row);
        }
        ctx.nativeSnapshot.insert(QStringLiteral("recent"), exactRows);
        ctx.nativeSnapshot.insert(QStringLiteral("lastSeason"), progress->lastSeason(id));
    }
    if (collection) {
        ctx.nativeSnapshot.insert(QStringLiteral("saved"), collection->has(QStringLiteral("theatre"), id));
        bool notify = true;
        for (const QVariant &value : collection->items(QStringLiteral("theatre"))) {
            const QVariantMap entry = value.toMap();
            if (entry.value(QStringLiteral("id")).toString() != id) continue;
            notify = entry.value(QStringLiteral("payload")).toMap()
                .value(QStringLiteral("libNotif"), true).toBool();
            break;
        }
        ctx.nativeSnapshot.insert(QStringLiteral("notify"), notify);
    }
    if (auto *extensions = qobject_cast<ExtensionsStore *>(bridge.service(QStringLiteral("Extensions"))))
        ctx.extensions = extensions->installed();
}

QVariantList build(const FeedContext &ctx)
{
    const QString requested = ctx.params.value(QStringLiteral("id")).toString();
    const QString type = ctx.params.value(QStringLiteral("type")).toString();
    const QVariantMap meta = loadMeta(requested, type);
    const bool resolved = !meta.isEmpty();
    const QString title = meta.value(QStringLiteral("name"), ctx.params.value(QStringLiteral("title"))).toString();
    const QString resolvedId = meta.value(QStringLiteral("id"), requested).toString();
    const QVariantList videos = meta.value(QStringLiteral("videos")).toList();
    const QVariantList progressRows = ctx.nativeSnapshot.value(QStringLiteral("recent")).toList();
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
    QVariantList related;
    if (!genreList.isEmpty() && !ctx.paths.imdb.isEmpty()) {
        ImdbCatalog imdb(ctx.paths.imdb);
        if (imdb.ready()) {
            const QVariantList candidates = imdb.titleCatalog(
                {{QStringLiteral("type"), type}, {QStringLiteral("genre"), genreList.first()},
                 {QStringLiteral("order"), QStringLiteral("rating")}}, 0, 16);
            for (const QVariant &value : candidates) {
                QVariantMap candidate = value.toMap();
                if (candidate.value(QStringLiteral("tt")) == resolvedId) continue;
                candidate.insert(QStringLiteral("id"), candidate.value(QStringLiteral("tt")));
                related.append(WebFeedValue::item(candidate, QStringLiteral("Theatre"), type));
                if (related.size() == 12) break;
            }
        }
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
    const QVariantList sourceRows = sourceTarget.isEmpty() ? QVariantList{} : resolveSources(ctx, sourceTarget);
    out.append(section(QStringLiteral("sources"), 5, QStringLiteral("Sources"), sourceRows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                       {{QStringLiteral("schema"), QStringLiteral("theatre.sources")},
                        {QStringLiteral("targetId"), sourceTarget},
                        {QStringLiteral("rows"), sourceRows}}));
    out.append(WebFeedValue::section(QStringLiteral("related"), 6, QStringLiteral("More Like This"),
                                      QStringLiteral("rail"), related,
                                      related.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready")));
    return out;
}

bool identity(const QVariantMap &payload)
{
    return !payload.value(QStringLiteral("id")).toString().isEmpty();
}

void unavailable(ActionRegistry::Completion done, const QString &message)
{
    done({{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}});
}

const bool feedRegistered = [] {
    FeedRegistry::Entry entry;
    entry.name = kFeed;
    entry.valid = &valid;
    entry.initial = &initial;
    entry.build = &build;
    entry.capture = &capture;
    return FeedRegistry::add(entry);
}();
const bool seasonRegistered = ActionRegistry::add({QStringLiteral("detail.theatre.selectSeason"),
    [](const QVariantMap &p) { return identity(p) && p.contains(QStringLiteral("season")); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        const QString id = p.value(QStringLiteral("id")).toString();
        if (!bridge.detailActive(kFeed, id)) return unavailable(done, QStringLiteral("This title is no longer open."));
        const QVariantMap view{{QStringLiteral("season"), p.value(QStringLiteral("season")).toInt()},
                               {QStringLiteral("order"), p.value(QStringLiteral("order"), QStringLiteral("seasons"))}};
        bridge.updateDetail(kFeed, id, {{QStringLiteral("view"), view}});
        if (auto *progress = qobject_cast<ProgressStore *>(bridge.service(QStringLiteral("Progress"))))
            progress->rememberLastSeason(id, view.value(QStringLiteral("season")).toInt());
        done({{QStringLiteral("ok"), true}});
    }});
const bool sourcesRegistered = ActionRegistry::add({QStringLiteral("detail.theatre.loadSources"),
    [](const QVariantMap &p) { return identity(p) && !p.value(QStringLiteral("episodeId")).toString().isEmpty(); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        const QString id = p.value(QStringLiteral("id")).toString();
        if (!bridge.detailActive(kFeed, id)) return unavailable(done, QStringLiteral("This title is no longer open."));
        const QString target = p.value(QStringLiteral("episodeId")).toString();
        const QString type = bridge.detailParams(kFeed, id).value(QStringLiteral("type")).toString();
        if ((type == QLatin1String("movie") && target != id)
            || (type == QLatin1String("series") && episodeFor(id, target).isEmpty()))
            return unavailable(done, QStringLiteral("This episode is no longer available."));
        const auto settled = QSharedPointer<bool>::create(false);
        const auto connection = QSharedPointer<QMetaObject::Connection>::create();
        *connection = QObject::connect(&bridge, &ColosseumWebBridge::feedEvent, &bridge,
            [&bridge, id, target, done, settled, connection](const QVariantMap &envelope) {
                if (*settled || !bridge.isDetailSubscription(envelope.value(QStringLiteral("id")).toInt(), kFeed, id))
                    return;
                const QVariantMap event = envelope.value(QStringLiteral("event")).toMap();
                const QVariantMap source = event.value(QStringLiteral("section")).toMap();
                if (event.value(QStringLiteral("type")) != QLatin1String("section")
                    || source.value(QStringLiteral("id")) != QLatin1String("sources")
                    || source.value(QStringLiteral("state")) == QLatin1String("loading")
                    || source.value(QStringLiteral("data")).toMap().value(QStringLiteral("targetId")) != target)
                    return;
                *settled = true;
                QObject::disconnect(*connection);
                done({{QStringLiteral("ok"), true}});
            });
        if (!bridge.updateDetail(kFeed, id, {{QStringLiteral("sourceTarget"), target}})) {
            *settled = true;
            QObject::disconnect(*connection);
            return unavailable(done, QStringLiteral("This title is no longer open."));
        }
        QTimer::singleShot(25000, &bridge, [done, settled, connection] {
            if (*settled) return;
            *settled = true;
            QObject::disconnect(*connection);
            unavailable(done, QStringLiteral("Sources did not refresh in time."));
        });
    }});
const bool playRegistered = ActionRegistry::add({QStringLiteral("detail.theatre.play"), &identity,
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        const QString id = p.value(QStringLiteral("id")).toString();
        if (!bridge.detailActive(kFeed, id))
            return unavailable(done, QStringLiteral("This title is no longer open."));
        const QString key = p.value(QStringLiteral("sourceKey")).toString();
        if (key.isEmpty()) return unavailable(done, QStringLiteral("Choose a source first."));
        const QVariantMap row = bridge.detailRow(kFeed, id, QStringLiteral("sources"), key);
        if (row.isEmpty()) return unavailable(done, QStringLiteral("This source is no longer available."));
        QVariantMap privateRow;
        {
            QMutexLocker lock(&sourceMutex);
            privateRow = sourceChoices.value(key);
        }
        const QString target = privateRow.value(QStringLiteral("targetId")).toString();
        if (privateRow.isEmpty() || privateRow.value(QStringLiteral("titleId")) != id
            || (!p.value(QStringLiteral("episodeId")).toString().isEmpty()
                && p.value(QStringLiteral("episodeId")) != target))
            return unavailable(done, QStringLiteral("This source is no longer available."));
        const QVariantMap params = bridge.detailParams(kFeed, id);
        const QString hash = privateRow.value(QStringLiteral("infoHash")).toString();
        const QString direct = privateRow.value(QStringLiteral("url")).toString();
        const QString source = hash.isEmpty() ? QStringLiteral("url:") + direct : hash;
        QVariantMap playback{{QStringLiteral("title"), params.value(QStringLiteral("title"))},
                             {QStringLiteral("imdbId"), id}};
        bridge.delegateAction(QStringLiteral("detail.theatre.playSource"),
            {{QStringLiteral("infoHash"), source},
             {QStringLiteral("fileIdx"), privateRow.value(QStringLiteral("fileIdx"), 0)},
             {QStringLiteral("title"), params.value(QStringLiteral("title"))},
             {QStringLiteral("backdrop"), params.value(QStringLiteral("cover"))},
             {QStringLiteral("subType"), params.value(QStringLiteral("type"))},
             {QStringLiteral("subId"), target},
             {QStringLiteral("candidates"), QVariantList{privateRow}},
             {QStringLiteral("playbackContext"), playback}}, done);
    }});
const bool downloadRegistered = ActionRegistry::add({QStringLiteral("detail.theatre.download"), &identity,
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        const QString id = p.value(QStringLiteral("id")).toString();
        if (!bridge.detailActive(kFeed, id))
            return unavailable(done, QStringLiteral("This title is no longer open."));
        const QString key = p.value(QStringLiteral("sourceKey")).toString();
        if (key.isEmpty()) return unavailable(done, QStringLiteral("Choose a download source first."));
        if (bridge.detailRow(kFeed, id, QStringLiteral("sources"), key).isEmpty())
            return unavailable(done, QStringLiteral("This source is no longer available."));
        QVariantMap privateRow;
        {
            QMutexLocker lock(&sourceMutex);
            privateRow = sourceChoices.value(key);
        }
        if (privateRow.isEmpty() || privateRow.value(QStringLiteral("titleId")) != id)
            return unavailable(done, QStringLiteral("This source is no longer available."));
        auto *downloads = qobject_cast<DownloadStore *>(bridge.service(QStringLiteral("Download")));
        if (!downloads) return unavailable(done, QStringLiteral("Video downloads are unavailable."));
        const QVariantMap params = bridge.detailParams(kFeed, id);
        const QString target = privateRow.value(QStringLiteral("targetId")).toString();
        const bool series = params.value(QStringLiteral("type")) == QLatin1String("series");
        const QVariantMap episode = series ? episodeFor(id, target) : QVariantMap{};
        if (series && episode.isEmpty())
            return unavailable(done, QStringLiteral("This episode is no longer available."));
        QVariantMap request{{QStringLiteral("id"), target},
                            {QStringLiteral("kind"), series ? QStringLiteral("episode") : QStringLiteral("movie")},
                            {QStringLiteral("title"), params.value(QStringLiteral("title"))},
                            {QStringLiteral("art"), params.value(QStringLiteral("cover"))}};
        const QString direct = privateRow.value(QStringLiteral("url")).toString();
        if (!direct.isEmpty()) {
            request.insert(QStringLiteral("url"), direct);
            request.insert(QStringLiteral("headers"), privateRow.value(QStringLiteral("headers")).toMap());
        } else {
            request.insert(QStringLiteral("infoHash"), privateRow.value(QStringLiteral("infoHash")));
            request.insert(QStringLiteral("fileIdx"), privateRow.value(QStringLiteral("fileIdx"), 0));
        }
        if (series) {
            request.insert(QStringLiteral("title"), QStringLiteral("%1 - S%2E%3")
                .arg(params.value(QStringLiteral("title")).toString())
                .arg(episode.value(QStringLiteral("season"), episode.value(QStringLiteral("seasonNumber"))).toInt())
                .arg(episode.value(QStringLiteral("episode"), episode.value(QStringLiteral("number"))).toInt()));
            request.insert(QStringLiteral("seriesTitle"), params.value(QStringLiteral("title")));
            request.insert(QStringLiteral("season"), episode.value(QStringLiteral("season"), episode.value(QStringLiteral("seasonNumber"))));
            request.insert(QStringLiteral("episode"), episode.value(QStringLiteral("episode"), episode.value(QStringLiteral("number"))));
            request.insert(QStringLiteral("subtitle"), episode.value(QStringLiteral("title"), episode.value(QStringLiteral("name"))));
        }
        downloads->enqueueBatch({request});
        bool accepted = downloads->hasVideo(target);
        for (const QVariant &value : downloads->jobs())
            if (value.toMap().value(QStringLiteral("id")) == target) accepted = true;
        if (!accepted) return unavailable(done, QStringLiteral("This download could not be queued."));
        done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("jobId"), target}}}});
    }});
const bool markRegistered = ActionRegistry::add({QStringLiteral("detail.theatre.markWatched"),
    [](const QVariantMap &p) { return identity(p) && !p.value(QStringLiteral("episodeId")).toString().isEmpty()
        && p.value(QStringLiteral("watched")).metaType().id() == QMetaType::Bool; },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        const QString id = p.value(QStringLiteral("id")).toString();
        if (!bridge.detailActive(kFeed, id)) return unavailable(done, QStringLiteral("This title is no longer open."));
        if (episodeFor(id, p.value(QStringLiteral("episodeId")).toString()).isEmpty())
            return unavailable(done, QStringLiteral("This episode is no longer available."));
        unavailable(done, QStringLiteral("An exact episode watched mark is not available yet."));
    }});
const bool collectionRegistered = ActionRegistry::add({QStringLiteral("detail.theatre.collection"),
    [](const QVariantMap &p) { return identity(p) && p.value(QStringLiteral("saved")).metaType().id() == QMetaType::Bool; },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        const QString id = p.value(QStringLiteral("id")).toString();
        if (!bridge.detailActive(kFeed, id)) return unavailable(done, QStringLiteral("This title is no longer open."));
        auto *collection = qobject_cast<CollectionStore *>(bridge.service(QStringLiteral("Collection")));
        if (!collection || !collection->healthy()) return unavailable(done, QStringLiteral("Collection is unavailable."));
        const bool saved = p.value(QStringLiteral("saved")).toBool();
        QVariantMap entry;
        if (saved) {
            for (const QVariant &value : collection->items(QStringLiteral("theatre")))
                if (value.toMap().value(QStringLiteral("id")).toString() == id) {
                    entry = value.toMap();
                    break;
                }
            entry.insert(QStringLiteral("id"), id);
            entry.insert(QStringLiteral("type"), bridge.detailParams(kFeed, id).value(QStringLiteral("type")));
            entry.insert(QStringLiteral("title"), p.value(QStringLiteral("title")));
            entry.insert(QStringLiteral("cover"), p.value(QStringLiteral("cover")));
            QVariantMap payload = entry.value(QStringLiteral("payload")).toMap();
            if (p.contains(QStringLiteral("notify")))
                payload.insert(QStringLiteral("libNotif"), p.value(QStringLiteral("notify")).toBool());
            entry.insert(QStringLiteral("payload"), payload);
        }
        const bool ok = saved ? collection->add(QStringLiteral("theatre"), entry)
                              : collection->remove(QStringLiteral("theatre"), id);
        if (!ok) return unavailable(done, QStringLiteral("Collection could not be saved."));
        bridge.updateDetail(kFeed, id, {});
        done({{QStringLiteral("ok"), true}});
    }});
} // namespace
