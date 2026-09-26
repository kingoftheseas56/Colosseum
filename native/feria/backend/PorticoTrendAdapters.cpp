#include "PorticoTrendAdapters.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSet>
#include <QSharedPointer>
#include <QUrlQuery>

namespace {

QJsonObject parseJsonObject(const QByteArray &body, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return {};
    }
    return document.object();
}

QString joinObjectNames(const QJsonArray &array)
{
    QStringList values;
    for (const auto &value : array) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            values.push_back(name);
    }
    return values.join(QStringLiteral(", "));
}

PorticoTrend::Shelf baseShelf(const QString &id, const QString &title,
                              const QString &sourceLabel, const QString &medium,
                              const QString &region, const QString &period,
                              const QString &url, const QString &stability)
{
    PorticoTrend::Shelf shelf;
    shelf.id = id;
    shelf.title = title;
    shelf.sourceId = id;
    shelf.sourceLabel = sourceLabel;
    shelf.medium = medium;
    shelf.region = region;
    shelf.period = period;
    shelf.canonicalUrl = url;
    shelf.stability = stability;
    shelf.fetchedAt = QDateTime::currentDateTimeUtc();
    return shelf;
}

QUrl configuredUrl(const QVariantMap &config, const QString &key)
{
    const QString raw = config.value(key).toString().trimmed();
    if (raw.isEmpty())
        return {};
    if (QDir::isAbsolutePath(raw))
        return QUrl::fromLocalFile(QDir::cleanPath(raw));
    const QUrl direct(raw);
    if (direct.isValid() && !direct.scheme().isEmpty())
        return direct;
    return QUrl::fromLocalFile(raw);
}

QStringList parseCsvRow(const QString &line)
{
    QStringList cells;
    QString cell;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                cell += QLatin1Char('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == QLatin1Char(',') && !quoted) {
            cells.push_back(cell);
            cell.clear();
        } else {
            cell += ch;
        }
    }
    cells.push_back(cell);
    return cells;
}

} // namespace
bool StremioCatalogAdapter::isConfigured(const QVariantMap &config) const
{
    const QStringList types = config.value(QStringLiteral("types")).toStringList();
    const bool hasType = !config.value(QStringLiteral("type")).toString().trimmed().isEmpty()
        || !types.isEmpty();
    return !config.value(QStringLiteral("baseUrl")).toString().trimmed().isEmpty()
        && hasType
        && !config.value(QStringLiteral("catalogId")).toString().trimmed().isEmpty();
}

void StremioCatalogAdapter::fetch(const PorticoTrend::Query &query, const QVariantMap &config)
{
    if (!isConfigured(config)) {
        emit failed(id(), QStringLiteral("Stremio requires baseUrl, type/types and catalogId."));
        return;
    }

    QString base = config.value(QStringLiteral("baseUrl")).toString().trimmed();
    if (base.endsWith(QStringLiteral("/manifest.json")))
        base.chop(QStringLiteral("/manifest.json").size());
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);

    QStringList types = config.value(QStringLiteral("types")).toStringList();
    if (types.isEmpty())
        types.push_back(config.value(QStringLiteral("type")).toString());

    struct State {
        int pending = 0;
        QStringList types;
        QHash<QString, PorticoTrend::Shelf> parts;
        QStringList errors;
    };
    auto state = QSharedPointer<State>::create();
    state->pending = types.size();
    state->types = types;

    const QString catalog = QString::fromUtf8(
        QUrl::toPercentEncoding(config.value(QStringLiteral("catalogId")).toString()));

    for (const QString &rawType : types) {
        const QString type = QString::fromUtf8(QUrl::toPercentEncoding(rawType));
        const QUrl url(base + QStringLiteral("/catalog/") + type + QLatin1Char('/')
                       + catalog + QStringLiteral(".json"));
        auto *reply = get(url);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, query, config, rawType, state]() {
            QByteArray body;
            QString error;
            if (takeReply(reply, &body, &error)) {
                QVariantMap typedConfig = config;
                typedConfig.insert(QStringLiteral("type"), rawType);
                auto part = parsePayload(body, query, typedConfig, &error);
                if (error.isEmpty())
                    state->parts.insert(rawType, part);
            }
            if (!error.isEmpty())
                state->errors.push_back(error);

            if (--state->pending != 0)
                return;

            PorticoTrend::Shelf merged;
            for (const QString &configuredType : state->types) {
                const auto partIt = state->parts.constFind(configuredType);
                if (partIt != state->parts.cend() && !partIt->items.isEmpty()) {
                    merged = partIt.value();
                    merged.items.clear();
                    break;
                }
            }
            if (merged.sourceId.isEmpty()) {
                emit failed(id(), state->errors.isEmpty()
                    ? QStringLiteral("Stremio catalog returned no metadata items.")
                    : state->errors.join(QStringLiteral("; ")));
                return;
            }

            QSet<QString> seen;
            qsizetype rank = 0;
            while (merged.items.size() < query.limit) {
                bool anyCandidate = false;
                for (const QString &configuredType : state->types) {
                    const auto partIt = state->parts.constFind(configuredType);
                    if (partIt == state->parts.cend() || rank >= partIt->items.size())
                        continue;
                    anyCandidate = true;
                    const auto &item = partIt->items.at(rank);
                    const QString key = item.id + QLatin1Char('|') + item.kind;
                    if (!seen.contains(key)) {
                        seen.insert(key);
                        merged.items.push_back(item);
                        if (merged.items.size() >= query.limit)
                            break;
                    }
                }
                if (!anyCandidate)
                    break;
                ++rank;
            }
            emit succeeded(id(), merged);
        });
    }
}

PorticoTrend::Shelf StremioCatalogAdapter::parsePayload(
    const QByteArray &body, const PorticoTrend::Query &query,
    const QVariantMap &config, QString *error)
{
    const auto root = parseJsonObject(body, error);
    if (root.isEmpty() && error && !error->isEmpty())
        return {};
    const QString source = config.value(QStringLiteral("sourceLabel"),
                                        QStringLiteral("Stremio Catalog")).toString();
    auto shelf = baseShelf(QStringLiteral("stremio"),
                           config.value(QStringLiteral("title"),
                                        QStringLiteral("Trending to watch")).toString(),
                           source, QStringLiteral("watch"), query.region,
                           config.value(QStringLiteral("period"), QStringLiteral("catalog")).toString(),
                           config.value(QStringLiteral("baseUrl")).toString(),
                           QStringLiteral("public-protocol"));
    const auto metas = root.value(QStringLiteral("metas")).toArray();
    const int count = qMin(query.limit, metas.size());
    for (int i = 0; i < count; ++i) {
        const auto meta = metas.at(i).toObject();
        PorticoTrend::Item item;
        item.id = meta.value(QStringLiteral("id")).toString();
        item.title = meta.value(QStringLiteral("name")).toString();
        item.creator = meta.value(QStringLiteral("director")).toString();
        item.kind = config.value(QStringLiteral("type")).toString() == QStringLiteral("movie")
            ? QStringLiteral("film") : QStringLiteral("series");
        item.imageUrl = meta.value(QStringLiteral("poster")).toString();
        item.canonicalUrl = meta.value(QStringLiteral("website")).toString();
        item.sourceId = QStringLiteral("stremio");
        item.rank = i + 1;
        item.year = meta.value(QStringLiteral("releaseInfo")).toString().left(4).toInt();
        item.genres = jsonStringList(meta.value(QStringLiteral("genres")));
        item.extra.insert(QStringLiteral("description"), meta.value(QStringLiteral("description")).toString());
        if (!item.title.isEmpty())
            shelf.items.push_back(item);
    }
    if (shelf.items.isEmpty() && error)
        *error = QStringLiteral("Stremio catalog returned no metadata items.");
    return shelf;
}

void OpenLibraryTrendAdapter::fetch(const PorticoTrend::Query &query, const QVariantMap &)
{
    QUrl url(QStringLiteral("https://openlibrary.org/search.json"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("q"), QStringLiteral("*:*"));
    params.addQueryItem(QStringLiteral("sort"), QStringLiteral("trending"));
    params.addQueryItem(QStringLiteral("limit"), QString::number(qBound(1, query.limit, 100)));
    params.addQueryItem(QStringLiteral("fields"),
                        QStringLiteral("key,title,author_name,cover_i,first_publish_year,isbn"));
    url.setQuery(params);
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        QByteArray body; QString error;
        if (!takeReply(reply, &body, &error)) { emit failed(id(), error); return; }
        auto shelf = parsePayload(body, query, {}, &error);
        if (!error.isEmpty()) emit failed(id(), error); else emit succeeded(id(), shelf);
    });
}

PorticoTrend::Shelf OpenLibraryTrendAdapter::parsePayload(
    const QByteArray &body, const PorticoTrend::Query &query,
    const QVariantMap &, QString *error)
{
    const auto root = parseJsonObject(body, error);
    if (root.isEmpty() && error && !error->isEmpty())
        return {};
    auto shelf = baseShelf(QStringLiteral("openlibrary"), QStringLiteral("Trending books"),
                           QStringLiteral("Open Library"), QStringLiteral("read"),
                           QStringLiteral("global"), QStringLiteral("trending"),
                           QStringLiteral("https://openlibrary.org/trending"),
                           QStringLiteral("public-api"));
    const auto docs = root.value(QStringLiteral("docs")).toArray();
    const int count = qMin(query.limit, docs.size());
    for (int i = 0; i < count; ++i) {
        const auto doc = docs.at(i).toObject();
        PorticoTrend::Item item;
        item.id = doc.value(QStringLiteral("key")).toString();
        item.title = doc.value(QStringLiteral("title")).toString();
        item.creator = jsonStringList(doc.value(QStringLiteral("author_name"))).join(QStringLiteral(", "));
        item.kind = QStringLiteral("book");
        const int coverId = doc.value(QStringLiteral("cover_i")).toInt();
        if (coverId > 0)
            item.imageUrl = QStringLiteral("https://covers.openlibrary.org/b/id/%1-L.jpg").arg(coverId);
        if (!item.id.isEmpty())
            item.canonicalUrl = QStringLiteral("https://openlibrary.org") + item.id;
        item.sourceId = QStringLiteral("openlibrary");
        item.rank = i + 1;
        item.year = doc.value(QStringLiteral("first_publish_year")).toInt();
        const auto isbns = jsonStringList(doc.value(QStringLiteral("isbn")));
        if (!isbns.isEmpty())
            item.externalIds.insert(QStringLiteral("isbn"), isbns.first());
        if (!item.title.isEmpty())
            shelf.items.push_back(item);
    }
    if (shelf.items.isEmpty() && error)
        *error = QStringLiteral("Open Library returned no trending books.");
    return shelf;
}

void AniListTrendAdapter::fetch(const PorticoTrend::Query &query, const QVariantMap &)
{
    static const QByteArray graphQl = R"(
query ($perPage: Int) {
  Page(page: 1, perPage: $perPage) {
    media(type: MANGA, sort: TRENDING_DESC) {
      id
      idMal
      isAdult
      title { english romaji userPreferred }
      coverImage { extraLarge large }
      siteUrl
      startDate { year }
      genres
      format
    }
  }
})";
    QJsonObject payload;
    payload.insert(QStringLiteral("query"), QString::fromUtf8(graphQl));
    payload.insert(QStringLiteral("variables"),
                   QJsonObject{{QStringLiteral("perPage"), qBound(1, query.limit, 50)}});
    auto *reply = postJson(QUrl(QStringLiteral("https://graphql.anilist.co")),
                           QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        QByteArray body; QString error;
        if (!takeReply(reply, &body, &error)) { emit failed(id(), error); return; }
        auto shelf = parsePayload(body, query, {}, &error);
        if (!error.isEmpty()) emit failed(id(), error); else emit succeeded(id(), shelf);
    });
}

PorticoTrend::Shelf AniListTrendAdapter::parsePayload(
    const QByteArray &body, const PorticoTrend::Query &query,
    const QVariantMap &, QString *error)
{
    const auto root = parseJsonObject(body, error);
    if (root.isEmpty() && error && !error->isEmpty())
        return {};
    if (!root.value(QStringLiteral("errors")).toArray().isEmpty()) {
        if (error)
            *error = root.value(QStringLiteral("errors")).toArray().first().toObject()
                         .value(QStringLiteral("message")).toString();
        return {};
    }
    auto shelf = baseShelf(QStringLiteral("anilist"), QStringLiteral("Trending manga"),
                           QStringLiteral("AniList"), QStringLiteral("read"),
                           QStringLiteral("global"), QStringLiteral("trending"),
                           QStringLiteral("https://anilist.co/search/manga/trending"),
                           QStringLiteral("public-api"));
    const auto media = root.value(QStringLiteral("data")).toObject()
                           .value(QStringLiteral("Page")).toObject()
                           .value(QStringLiteral("media")).toArray();
    const int count = qMin(query.limit, media.size());
    for (int i = 0; i < count; ++i) {
        const auto object = media.at(i).toObject();
        if (object.value(QStringLiteral("isAdult")).toBool(false))
            continue;
        const auto titles = object.value(QStringLiteral("title")).toObject();
        PorticoTrend::Item item;
        item.id = QString::number(object.value(QStringLiteral("id")).toInt());
        item.title = firstNonEmpty({
            titles.value(QStringLiteral("english")).toString(),
            titles.value(QStringLiteral("userPreferred")).toString(),
            titles.value(QStringLiteral("romaji")).toString()
        });
        item.creator.clear();
        item.kind = QStringLiteral("manga");
        const auto cover = object.value(QStringLiteral("coverImage")).toObject();
        item.imageUrl = firstNonEmpty({
            cover.value(QStringLiteral("extraLarge")).toString(),
            cover.value(QStringLiteral("large")).toString()
        });
        item.canonicalUrl = object.value(QStringLiteral("siteUrl")).toString();
        item.sourceId = QStringLiteral("anilist");
        item.rank = i + 1;
        item.year = object.value(QStringLiteral("startDate")).toObject()
                        .value(QStringLiteral("year")).toInt();
        item.genres = jsonStringList(object.value(QStringLiteral("genres")));
        item.externalIds.insert(QStringLiteral("anilist"), item.id);
        const int mal = object.value(QStringLiteral("idMal")).toInt();
        if (mal > 0)
            item.externalIds.insert(QStringLiteral("mal"), mal);
        item.extra.insert(QStringLiteral("format"), object.value(QStringLiteral("format")).toString());
        if (!item.title.isEmpty())
            shelf.items.push_back(item);
    }
    if (shelf.items.isEmpty() && error)
        *error = QStringLiteral("AniList returned no trending manga.");
    return shelf;
}

bool AppleMusicChartsAdapter::isConfigured(const QVariantMap &config) const
{
    return !config.value(QStringLiteral("developerToken")).toString().trimmed().isEmpty();
}

void AppleMusicChartsAdapter::fetch(const PorticoTrend::Query &query, const QVariantMap &config)
{
    if (!isConfigured(config)) {
        emit failed(id(), QStringLiteral("Apple Music requires a developerToken."));
        return;
    }
    const QString storefront = query.region.trimmed().toLower();
    const QString chartType = config.value(QStringLiteral("chartType"),
                                           QStringLiteral("albums")).toString();
    QUrl url(QStringLiteral("https://api.music.apple.com/v1/catalog/%1/charts")
                 .arg(storefront.isEmpty() ? QStringLiteral("us") : storefront));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("types"), chartType);
    params.addQueryItem(QStringLiteral("limit"), QString::number(qBound(1, query.limit, 200)));
    url.setQuery(params);
    QVariantMap headers;
    headers.insert(QStringLiteral("Authorization"),
                   QStringLiteral("Bearer ") + config.value(QStringLiteral("developerToken")).toString());
    auto *reply = get(url, headers);
    connect(reply, &QNetworkReply::finished, this, [this, reply, query, config]() {
        QByteArray body; QString error;
        if (!takeReply(reply, &body, &error)) { emit failed(id(), error); return; }
        auto shelf = parsePayload(body, query, config, &error);
        if (!error.isEmpty()) emit failed(id(), error); else emit succeeded(id(), shelf);
    });
}

PorticoTrend::Shelf AppleMusicChartsAdapter::parsePayload(
    const QByteArray &body, const PorticoTrend::Query &query,
    const QVariantMap &config, QString *error)
{
    const auto root = parseJsonObject(body, error);
    if (root.isEmpty() && error && !error->isEmpty())
        return {};
    const QString chartType = config.value(QStringLiteral("chartType"),
                                           QStringLiteral("albums")).toString();
    const QString title = chartType == QStringLiteral("songs")
        ? QStringLiteral("Top songs") : QStringLiteral("Top albums");
    auto shelf = baseShelf(QStringLiteral("applemusic"), title,
                           QStringLiteral("Apple Music"), QStringLiteral("listen"),
                           query.region.toUpper(), QStringLiteral("chart"),
                           QStringLiteral("https://music.apple.com/"),
                           QStringLiteral("public-api-authenticated"));
    const auto charts = root.value(QStringLiteral("results")).toObject().value(chartType).toArray();
    if (charts.isEmpty()) {
        if (error) *error = QStringLiteral("Apple Music response did not contain %1 charts.").arg(chartType);
        return shelf;
    }
    const auto entries = charts.first().toObject().value(QStringLiteral("data")).toArray();
    const int count = qMin(query.limit, entries.size());
    for (int i = 0; i < count; ++i) {
        const auto entry = entries.at(i).toObject();
        const auto attributes = entry.value(QStringLiteral("attributes")).toObject();
        PorticoTrend::Item item;
        item.id = entry.value(QStringLiteral("id")).toString();
        item.title = attributes.value(QStringLiteral("name")).toString();
        item.creator = attributes.value(QStringLiteral("artistName")).toString();
        item.kind = chartType == QStringLiteral("songs") ? QStringLiteral("song")
                                                          : QStringLiteral("album");
        QString art = attributes.value(QStringLiteral("artwork")).toObject()
                          .value(QStringLiteral("url")).toString();
        art.replace(QStringLiteral("{w}"), QStringLiteral("600"));
        art.replace(QStringLiteral("{h}"), QStringLiteral("600"));
        item.imageUrl = art;
        item.canonicalUrl = attributes.value(QStringLiteral("url")).toString();
        item.sourceId = QStringLiteral("applemusic");
        item.rank = i + 1;
        item.year = yearFromIsoDate(attributes.value(QStringLiteral("releaseDate")).toString());
        item.externalIds.insert(QStringLiteral("appleMusic"), item.id);
        item.genres = jsonStringList(attributes.value(QStringLiteral("genreNames")));
        if (!item.title.isEmpty())
            shelf.items.push_back(item);
    }
    if (shelf.items.isEmpty() && error)
        *error = QStringLiteral("Apple Music chart was empty.");
    return shelf;
}

void YouTubeChartsAdapter::fetch(const PorticoTrend::Query &query, const QVariantMap &config)
{
    const QString category = query.category.isEmpty()
        ? config.value(QStringLiteral("category"), QStringLiteral("songs")).toString()
        : query.category;
    QString selected = QStringLiteral("TRACKS");
    if (category == QStringLiteral("artists"))
        selected = QStringLiteral("ARTISTS");
    else if (category == QStringLiteral("videos"))
        selected = QStringLiteral("VIDEOS");

    const QString region = query.region.isEmpty() ? QStringLiteral("US")
                                                  : query.region.toUpper();
    QJsonObject client{
        {QStringLiteral("clientName"), QStringLiteral("WEB_MUSIC_ANALYTICS")},
        {QStringLiteral("clientVersion"), QStringLiteral("2.0")},
        {QStringLiteral("gl"), region},
        {QStringLiteral("hl"), query.language.isEmpty() ? QStringLiteral("en") : query.language},
        {QStringLiteral("theme"), QStringLiteral("MUSIC")},
        {QStringLiteral("experimentIds"), QJsonArray{}},
        {QStringLiteral("experimentsToken"), QString()}
    };
    QJsonObject context{
        {QStringLiteral("capabilities"), QJsonObject{}},
        {QStringLiteral("client"), client},
        {QStringLiteral("request"), QJsonObject{{QStringLiteral("internalExperimentFlags"), QJsonArray{}}}}
    };
    const QString queryString =
        QStringLiteral("chart_params_type=WEEK&perspective=CHART&flags=viral_video_chart"
                       "&selected_chart=%1&chart_params_id=weekly:0:0:%2")
            .arg(selected, region.toLower());
    QJsonObject payload{
        {QStringLiteral("browseId"), QStringLiteral("FEmusic_analytics_charts_home")},
        {QStringLiteral("context"), context},
        {QStringLiteral("query"), queryString}
    };
    QVariantMap headers;
    headers.insert(QStringLiteral("Referer"), QStringLiteral("https://charts.youtube.com/"));
    auto *reply = postJson(QUrl(QStringLiteral(
        "https://charts.youtube.com/youtubei/v1/browse?alt=json")),
        QJsonDocument(payload).toJson(QJsonDocument::Compact), headers);
    connect(reply, &QNetworkReply::finished, this, [this, reply, query, config]() {
        QByteArray body; QString error;
        if (!takeReply(reply, &body, &error)) { emit failed(id(), error); return; }
        auto shelf = parsePayload(body, query, config, &error);
        if (!error.isEmpty()) emit failed(id(), error); else emit succeeded(id(), shelf);
    });
}

PorticoTrend::Shelf YouTubeChartsAdapter::parsePayload(
    const QByteArray &body, const PorticoTrend::Query &query,
    const QVariantMap &config, QString *error)
{
    const auto root = parseJsonObject(body, error);
    if (root.isEmpty() && error && !error->isEmpty())
        return {};
    const QString category = query.category.isEmpty()
        ? config.value(QStringLiteral("category"), QStringLiteral("songs")).toString()
        : query.category;
    const QString arrayKey = category == QStringLiteral("artists")
        ? QStringLiteral("artistViews")
        : (category == QStringLiteral("videos") ? QStringLiteral("videoViews")
                                                 : QStringLiteral("trackViews"));
    const QString title = category == QStringLiteral("artists")
        ? QStringLiteral("Top artists this week")
        : (category == QStringLiteral("videos") ? QStringLiteral("Top music videos this week")
                                                 : QStringLiteral("Top songs this week"));
    auto shelf = baseShelf(QStringLiteral("youtube"), title,
                           QStringLiteral("YouTube Charts"), QStringLiteral("listen"),
                           query.region.toUpper(), QStringLiteral("weekly"),
                           QStringLiteral("https://charts.youtube.com/"),
                           QStringLiteral("private-web"));
    const auto entries = findFirstArrayByKey(root, arrayKey);
    const int count = qMin(query.limit, entries.size());
    for (int i = 0; i < count; ++i) {
        const auto entry = entries.at(i).toObject();
        PorticoTrend::Item item;
        item.id = entry.value(QStringLiteral("id")).toString();
        item.title = firstNonEmpty({
            entry.value(QStringLiteral("name")).toString(),
            entry.value(QStringLiteral("title")).toString()
        });
        item.creator = joinObjectNames(entry.value(QStringLiteral("artists")).toArray());
        item.kind = category == QStringLiteral("artists") ? QStringLiteral("artist")
                   : (category == QStringLiteral("videos") ? QStringLiteral("music-video")
                                                           : QStringLiteral("song"));
        item.imageUrl = lastThumbnailUrl(entry.value(QStringLiteral("thumbnail")));
        const QString videoId = entry.value(QStringLiteral("encryptedVideoId")).toString();
        if (!videoId.isEmpty())
            item.canonicalUrl = QStringLiteral("https://www.youtube.com/watch?v=") + videoId;
        else if (!item.title.isEmpty())
            item.canonicalUrl = QStringLiteral("https://music.youtube.com/search?q=")
                + QString::fromUtf8(QUrl::toPercentEncoding(item.title));
        item.sourceId = QStringLiteral("youtube");
        item.rank = entry.value(QStringLiteral("chartEntryMetadata")).toObject()
                        .value(QStringLiteral("currentPosition")).toInt(i + 1);
        item.year = entry.value(QStringLiteral("releaseDate")).toObject()
                        .value(QStringLiteral("year")).toInt();
        item.extra.insert(QStringLiteral("views"), entry.value(QStringLiteral("viewCount")).toString());
        item.extra.insert(QStringLiteral("label"), entry.value(QStringLiteral("sublabel")).toString());
        if (!videoId.isEmpty())
            item.externalIds.insert(QStringLiteral("youtube"), videoId);
        if (!item.title.isEmpty())
            shelf.items.push_back(item);
    }
    if (shelf.items.isEmpty() && error)
        *error = QStringLiteral("YouTube Charts response did not contain %1.").arg(arrayKey);
    return shelf;
}

void WebtoonTrendAdapter::fetch(const PorticoTrend::Query &query, const QVariantMap &)
{
    auto *reply = get(QUrl(QStringLiteral("https://www.webtoons.com/en/ranking/trending")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        QByteArray body; QString error;
        if (!takeReply(reply, &body, &error)) { emit failed(id(), error); return; }
        auto shelf = parsePayload(body, query, {}, &error);
        if (!error.isEmpty()) emit failed(id(), error); else emit succeeded(id(), shelf);
    });
}

PorticoTrend::Shelf WebtoonTrendAdapter::parsePayload(
    const QByteArray &body, const PorticoTrend::Query &query,
    const QVariantMap &, QString *error)
{
    auto shelf = baseShelf(QStringLiteral("webtoon"), QStringLiteral("Trending on WEBTOON"),
                           QStringLiteral("WEBTOON"), QStringLiteral("read"),
                           QStringLiteral("global"), QStringLiteral("trending"),
                           QStringLiteral("https://www.webtoons.com/en/ranking/trending"),
                           QStringLiteral("public-page"));
    const QString html = QString::fromUtf8(body);
    static const QRegularExpression card(
        QStringLiteral(R"RX(<a[^>]+href="([^"]+)"[^>]+data-title-index="(\d+)"[^>]+data-webtoon-type="[^"]+"[^>]+data-title-no="([^"]+)"[^>]+data-selected-tab-type="TRENDING"[^>]+data-genre="([^"]*)"[^>]*>(.*?)</a>)RX"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression titleRe(
        QStringLiteral(R"RX(<strong[^>]+class="title"[^>]*>(.*?)</strong>)RX"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression imageRe(
        QStringLiteral(R"RX(<img[^>]+src="([^"]+)"[^>]*>)RX"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = card.globalMatch(html);
    while (match.hasNext() && shelf.items.size() < query.limit) {
        const auto m = match.next();
        const QString block = m.captured(5);
        const auto titleMatch = titleRe.match(block);
        if (!titleMatch.hasMatch())
            continue;
        PorticoTrend::Item item;
        item.id = m.captured(3);
        item.title = decodeHtml(titleMatch.captured(1));
        item.kind = QStringLiteral("webtoon");
        const auto imageMatch = imageRe.match(block);
        if (imageMatch.hasMatch())
            item.imageUrl = decodeHtml(imageMatch.captured(1));
        item.canonicalUrl = decodeHtml(m.captured(1));
        item.sourceId = QStringLiteral("webtoon");
        item.rank = m.captured(2).toInt();
        item.genres = {decodeHtml(m.captured(4))};
        item.externalIds.insert(QStringLiteral("webtoon"), item.id);
        shelf.items.push_back(item);
    }
    if (shelf.items.isEmpty() && error)
        *error = QStringLiteral("WEBTOON ranking markup no longer matched the adapter.");
    return shelf;
}

void GlobalComixTrendAdapter::fetch(const PorticoTrend::Query &query, const QVariantMap &)
{
    auto *reply = get(QUrl(QStringLiteral("https://globalcomix.com/browse")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        QByteArray body; QString error;
        if (!takeReply(reply, &body, &error)) { emit failed(id(), error); return; }
        auto shelf = parsePayload(body, query, {}, &error);
        if (!error.isEmpty()) emit failed(id(), error); else emit succeeded(id(), shelf);
    });
}

PorticoTrend::Shelf GlobalComixTrendAdapter::parsePayload(
    const QByteArray &body, const PorticoTrend::Query &query,
    const QVariantMap &, QString *error)
{
    auto shelf = baseShelf(QStringLiteral("globalcomix"), QStringLiteral("Popular comics"),
                           QStringLiteral("GlobalComix"), QStringLiteral("read"),
                           QStringLiteral("global"), QStringLiteral("last 30 days"),
                           QStringLiteral("https://globalcomix.com/browse"),
                           QStringLiteral("public-page"));
    const QString html = QString::fromUtf8(body);
    static const QRegularExpression card(
        QStringLiteral(R"RX(<a[^>]+id="comic-(\d+)"[^>]+href="([^"]+)"[^>]*>(.*?)</a>)RX"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression image(
        QStringLiteral(R"RX(<img[^>]+alt="Cover image for ([^"]+)"[^>]+src="([^"]+)"[^>]*>)RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trailingYears(
        QStringLiteral(R"RX(\s*\(\d{4}[^)]*\)\s*$)RX"));
    auto match = card.globalMatch(html);
    while (match.hasNext() && shelf.items.size() < query.limit) {
        const auto m = match.next();
        const auto imageMatch = image.match(m.captured(3));
        if (!imageMatch.hasMatch())
            continue;
        PorticoTrend::Item item;
        item.id = m.captured(1);
        item.title = decodeHtml(imageMatch.captured(1));
        item.title.remove(trailingYears);
        item.kind = QStringLiteral("comic");
        item.imageUrl = decodeHtml(imageMatch.captured(2));
        const QString path = decodeHtml(m.captured(2));
        item.canonicalUrl = path.startsWith(QStringLiteral("http"))
            ? path : QStringLiteral("https://globalcomix.com") + path;
        item.sourceId = QStringLiteral("globalcomix");
        item.rank = shelf.items.size() + 1;
        item.externalIds.insert(QStringLiteral("globalComix"), item.id);
        shelf.items.push_back(item);
    }
    if (shelf.items.isEmpty() && error)
        *error = QStringLiteral("GlobalComix browse markup no longer matched the adapter.");
    return shelf;
}

bool SpotifyChartsAdapter::isConfigured(const QVariantMap &config) const
{
    return configuredUrl(config, QStringLiteral("csvUrl")).isValid();
}

void SpotifyChartsAdapter::fetch(const PorticoTrend::Query &query, const QVariantMap &config)
{
    const QUrl url = configuredUrl(config, QStringLiteral("csvUrl"));
    if (!url.isValid()) {
        emit failed(id(), QStringLiteral("Spotify requires a user-authorized Charts CSV URL or local CSV path."));
        return;
    }
    auto *reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, query, config]() {
        QByteArray body; QString error;
        if (!takeReply(reply, &body, &error)) { emit failed(id(), error); return; }
        auto shelf = parsePayload(body, query, config, &error);
        if (!error.isEmpty()) emit failed(id(), error); else emit succeeded(id(), shelf);
    });
}

PorticoTrend::Shelf SpotifyChartsAdapter::parsePayload(
    const QByteArray &body, const PorticoTrend::Query &query,
    const QVariantMap &, QString *error)
{
    auto shelf = baseShelf(QStringLiteral("spotify"), QStringLiteral("Top songs on Spotify"),
                           QStringLiteral("Spotify Charts"), QStringLiteral("listen"),
                           query.region.toUpper(), QStringLiteral("chart export"),
                           QStringLiteral("https://charts.spotify.com/"),
                           QStringLiteral("authorized-export"));
    QString text = QString::fromUtf8(body);
    text.remove(QLatin1Char(13));
    const QStringList lines = text.split(QLatin1Char(10), Qt::SkipEmptyParts);
    int headerRow = -1;
    QStringList headers;
    for (int i = 0; i < lines.size(); ++i) {
        const auto cells = parseCsvRow(lines.at(i));
        const QString lowered = cells.join(QLatin1Char(',')).toLower();
        if (lowered.contains(QStringLiteral("rank"))
            && (lowered.contains(QStringLiteral("track_name"))
                || lowered.contains(QStringLiteral("track name")))) {
            headerRow = i;
            headers = cells;
            break;
        }
    }
    if (headerRow < 0) {
        if (error) *error = QStringLiteral("Spotify CSV header was not recognized.");
        return shelf;
    }
    QHash<QString, int> column;
    for (int i = 0; i < headers.size(); ++i)
        column.insert(headers.at(i).trimmed().toLower().replace(QLatin1Char(' '), QLatin1Char('_')), i);
    auto value = [&column](const QStringList &cells, const QString &name) {
        const int index = column.value(name, -1);
        return index >= 0 && index < cells.size() ? cells.at(index).trimmed() : QString();
    };
    for (int row = headerRow + 1; row < lines.size() && shelf.items.size() < query.limit; ++row) {
        const auto cells = parseCsvRow(lines.at(row));
        const QString title = firstNonEmpty({value(cells, QStringLiteral("track_name")),
                                             value(cells, QStringLiteral("title"))});
        if (title.isEmpty())
            continue;
        PorticoTrend::Item item;
        item.rank = value(cells, QStringLiteral("rank")).toInt();
        if (item.rank <= 0)
            item.rank = shelf.items.size() + 1;
        item.title = title;
        item.creator = firstNonEmpty({value(cells, QStringLiteral("artist_names")),
                                      value(cells, QStringLiteral("artist"))});
        item.kind = QStringLiteral("song");
        item.sourceId = QStringLiteral("spotify");
        const QString uri = firstNonEmpty({value(cells, QStringLiteral("uri")),
                                           value(cells, QStringLiteral("spotify_uri"))});
        item.id = uri.section(QLatin1Char(':'), -1);
        if (!item.id.isEmpty())
            item.canonicalUrl = QStringLiteral("https://open.spotify.com/track/") + item.id;
        item.externalIds.insert(QStringLiteral("spotify"), item.id);
        item.extra.insert(QStringLiteral("streams"), value(cells, QStringLiteral("streams")));
        shelf.items.push_back(item);
    }
    if (shelf.items.isEmpty() && error)
        *error = QStringLiteral("Spotify CSV contained no chart rows.");
    return shelf;
}
