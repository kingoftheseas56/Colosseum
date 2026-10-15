#include "RatingsReviewsProviderReadProjection.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>
#include <QSet>
#include <QUrlQuery>
#include <utility>

namespace {
QString canonicalFixtureId(const QString &id) {
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    if (id == QLatin1String("rt") || id == QLatin1String("rottentomatoes"))
        return QStringLiteral("rotten_tomatoes");
    if (id == QLatin1String("meta"))
        return QStringLiteral("metacritic");
#endif
    return id;
}

QVariantList jsonRows(const QJsonArray &rows, int maxPerProvider = 0) {
    QVariantList out;
    QSet<QString> seen;
    for (const QJsonValue &value : rows) {
        if (!value.isObject())
            continue;
        QVariantMap row = value.toObject().toVariantMap();
        const QString providerId =
            canonicalFixtureId(row.value(QStringLiteral("providerId")).toString());
        if (providerId.isEmpty())
            continue;
        if (maxPerProvider == 1 && seen.contains(providerId))
            continue;
        row.insert(QStringLiteral("providerId"), providerId);
        row.insert(QStringLiteral("fixtureOnly"), true);
        seen.insert(providerId);
        out.append(row);
    }
    return out;
}
}

RatingsReviewsProviderReadProjection::RatingsReviewsProviderReadProjection(QObject *parent)
    : QObject(parent),
      m_urlOpener([](const QUrl &url) { return QDesktopServices::openUrl(url); }) {
    setObjectName(QStringLiteral("ratingsReviewsProviderReadProjection"));
}

QStringList RatingsReviewsProviderReadProjection::canonicalProviderIds() {
    return {QStringLiteral("mal"), QStringLiteral("anilist"),
            QStringLiteral("trakt"), QStringLiteral("simkl"),
            QStringLiteral("imdb"), QStringLiteral("tmdb"),
            QStringLiteral("rotten_tomatoes"), QStringLiteral("metacritic")};
}

QString RatingsReviewsProviderReadProjection::displayName(const QString &providerId) {
    if (providerId == QLatin1String("mal")) return QStringLiteral("MyAnimeList");
    if (providerId == QLatin1String("anilist")) return QStringLiteral("AniList");
    if (providerId == QLatin1String("trakt")) return QStringLiteral("Trakt");
    if (providerId == QLatin1String("simkl")) return QStringLiteral("SIMKL");
    if (providerId == QLatin1String("imdb")) return QStringLiteral("IMDb");
    if (providerId == QLatin1String("tmdb")) return QStringLiteral("TMDB");
    if (providerId == QLatin1String("rotten_tomatoes")) return QStringLiteral("Rotten Tomatoes");
    if (providerId == QLatin1String("metacritic")) return QStringLiteral("Metacritic");
    return providerId;
}

QVariantList RatingsReviewsProviderReadProjection::stringsToVariants(
    const QStringList &values) {
    QVariantList out;
    for (const QString &value : values)
        out.append(value);
    return out;
}

bool RatingsReviewsProviderReadProjection::isSafeSourceUrl(const QUrl &url) {
    if (!url.isValid()
        || url.scheme() != QLatin1String("https")
        || url.host().isEmpty()
        || !url.userName().isEmpty()
        || !url.password().isEmpty()) {
        return false;
    }
    const QSet<QString> forbiddenQueryKeys{
        QStringLiteral("access_token"), QStringLiteral("refresh_token"),
        QStringLiteral("token"), QStringLiteral("api_key"),
        QStringLiteral("apikey"), QStringLiteral("authorization"),
        QStringLiteral("bearer"), QStringLiteral("session"),
        QStringLiteral("sessionid")};
    const QUrlQuery query(url);
    for (const auto &item : query.queryItems()) {
        if (forbiddenQueryKeys.contains(item.first.toLower()))
            return false;
    }
    return true;
}

void RatingsReviewsProviderReadProjection::setUrlOpenerForTests(UrlOpener opener) {
    m_urlOpener = std::move(opener);
}

QVariantMap RatingsReviewsProviderReadProjection::emptyPresentation(
    const QString &world, const QString &kind, const QString &mediaId,
    quint64 profileGeneration, quint64 routeGeneration) const {
    return {{QStringLiteral("world"), world},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("mediaId"), mediaId},
            {QStringLiteral("profileGeneration"), QVariant::fromValue(profileGeneration)},
            {QStringLiteral("routeGeneration"), QVariant::fromValue(routeGeneration)},
            {QStringLiteral("aggregates"), QVariantList{}},
            {QStringLiteral("reviews"), QVariantList{}},
            {QStringLiteral("aggregateEmptyMessage"),
             QStringLiteral("No provider ratings available for this title.")},
            {QStringLiteral("reviewEmptyMessage"),
             QStringLiteral("No provider reviews available for this title.")},
            {QStringLiteral("fixtureOnly"), false}};
}

QVariantMap RatingsReviewsProviderReadProjection::presentation(
    const QString &world, const QString &kind, const QString &mediaId,
    const QVariantMap &readIds, quint64 profileGeneration,
    quint64 routeGeneration) {
    m_reviewSources.clear();
    m_world = world;
    m_kind = kind;
    m_mediaId = mediaId;
    m_profileGeneration = profileGeneration;
    m_routeGeneration = routeGeneration;
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    const QString variant =
        readIds.value(QStringLiteral("fixtureVariant")).toString();
    const bool fixtureTag = qEnvironmentVariable("COLOSSEUM_APPDATA_TAG")
        == QLatin1String("ratings-reviews-delivery-fixture");
    const bool fixtureTitle = world == QLatin1String("theatre")
        && kind == QLatin1String("series")
        && mediaId == QLatin1String("fixture-provider-read-series");
    if (fixtureTag && fixtureTitle && !variant.isEmpty()) {
        QVariantMap fixture =
            fixturePresentation(variant, profileGeneration, routeGeneration);
        fixture.insert(QStringLiteral("world"), world);
        fixture.insert(QStringLiteral("kind"), kind);
        fixture.insert(QStringLiteral("mediaId"), mediaId);
        return fixture;
    }
#else
    Q_UNUSED(readIds);
#endif

    // Package 1 adds no live provider reader. Until a lawful seam is certified,
    // the production page renders an honest empty wall, never guessed/scraped data.
    return emptyPresentation(
        world, kind, mediaId, profileGeneration, routeGeneration);
}

QVariantMap RatingsReviewsProviderReadProjection::fixturePresentation(
    const QString &variant, quint64 profileGeneration, quint64 routeGeneration) {
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    QResource resource(QStringLiteral(":/ratings-reviews/provider_read_v1.json"));
    if (!resource.isValid())
        return {{QStringLiteral("errorCode"), QStringLiteral("fixture_unavailable")}};

    const QByteArray bytes = resource.uncompressedData();
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()
        || document.object().value(QStringLiteral("schema")).toString()
            != QLatin1String("colosseum.ratings-reviews.provider-read-fixture.v1")) {
        return {{QStringLiteral("errorCode"), QStringLiteral("fixture_schema_invalid")}};
    }
    const QJsonObject variants =
        document.object().value(QStringLiteral("variants")).toObject();
    const QJsonObject object = variants.value(variant).toObject();
    if (object.isEmpty())
        return {{QStringLiteral("errorCode"), QStringLiteral("fixture_variant_unknown")}};

    const QJsonArray aggregateJson = object.value(QStringLiteral("aggregates")).toArray();
    const QJsonArray reviewJson = object.value(QStringLiteral("reviews")).toArray();
    QSet<QString> readyReviewProviders;
    for (const QJsonValue &value : reviewJson) {
        const QJsonObject row = value.toObject();
        if (row.value(QStringLiteral("state")).toString() != QLatin1String("ready"))
            continue;
        const QString providerId = canonicalFixtureId(
            row.value(QStringLiteral("providerId")).toString());
        if (readyReviewProviders.contains(providerId))
            return {{QStringLiteral("errorCode"), QStringLiteral("fixture_duplicate_review_provider")}};
        readyReviewProviders.insert(providerId);
    }
    const QVariantList aggregates = jsonRows(aggregateJson);
    const QVariantList reviews = jsonRows(reviewJson);

    for (const QVariant &value : reviews) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("state")).toString() != QLatin1String("ready"))
            continue;
        const QString providerId =
            row.value(QStringLiteral("providerId")).toString();
        const QUrl sourceUrl(
            row.value(QStringLiteral("sourceUrl")).toString());
        if (isSafeSourceUrl(sourceUrl))
            m_reviewSources.insert(providerId, sourceUrl);
    }

    return {{QStringLiteral("profileGeneration"), QVariant::fromValue(profileGeneration)},
            {QStringLiteral("routeGeneration"), QVariant::fromValue(routeGeneration)},
            {QStringLiteral("aggregates"), aggregates},
            {QStringLiteral("reviews"), reviews},
            {QStringLiteral("aggregateEmptyMessage"),
             QStringLiteral("No provider ratings available for this title.")},
            {QStringLiteral("reviewEmptyMessage"),
             QStringLiteral("No provider reviews available for this title.")},
            {QStringLiteral("fixtureOnly"), true},
            {QStringLiteral("errorCode"), QString()}};
#else
    Q_UNUSED(variant);
    Q_UNUSED(profileGeneration);
    Q_UNUSED(routeGeneration);
    return {{QStringLiteral("errorCode"), QStringLiteral("fixture_disabled")}};
#endif
}

bool RatingsReviewsProviderReadProjection::acceptsResult(
    const QString &world, const QString &kind, const QString &mediaId,
    quint64 profileGeneration, quint64 routeGeneration) const {
    return world == m_world
        && kind == m_kind
        && mediaId == m_mediaId
        && profileGeneration == m_profileGeneration
        && routeGeneration == m_routeGeneration;
}

bool RatingsReviewsProviderReadProjection::reviewSourceAvailable(
    const QString &providerId) const {
    const auto it = m_reviewSources.constFind(providerId);
    return it != m_reviewSources.cend() && isSafeSourceUrl(it.value());
}

bool RatingsReviewsProviderReadProjection::openReviewSource(
    const QString &providerId) {
    const auto it = m_reviewSources.constFind(providerId);
    if (it == m_reviewSources.cend()
        || !isSafeSourceUrl(it.value())
        || !m_urlOpener) {
        return false;
    }
    return m_urlOpener(it.value());
}

QVariantList RatingsReviewsProviderReadProjection::normalizeProviderOrder(
    const QVariantList &savedOrder) const {
    const QStringList canonical = canonicalProviderIds();
    QStringList out;
    QSet<QString> seen;

    for (const QVariant &value : savedOrder) {
        const QString id = canonicalFixtureId(value.toString());
        if (!canonical.contains(id) || seen.contains(id))
            continue;
        out.append(id);
        seen.insert(id);
    }
    for (const QString &id : canonical) {
        if (!seen.contains(id))
            out.append(id);
    }
    return stringsToVariants(out);
}

QVariantMap RatingsReviewsProviderReadProjection::moveVisibleProvider(
    const QVariantList &savedOrder, const QVariantList &visibleOrder,
    const QString &providerId, int direction) const {
    const QVariantList normalized = normalizeProviderOrder(savedOrder);
    QStringList full;
    for (const QVariant &value : normalized)
        full.append(value.toString());

    QStringList visible;
    for (const QVariant &value : visibleOrder) {
        const QString id = value.toString();
        if (full.contains(id) && !visible.contains(id))
            visible.append(id);
    }

    const int from = visible.indexOf(providerId);
    const int to = from + (direction < 0 ? -1 : 1);
    if (from < 0 || to < 0 || to >= visible.size()) {
        return {{QStringLiteral("changed"), false},
                {QStringLiteral("savedOrder"), stringsToVariants(full)},
                {QStringLiteral("visibleOrder"), stringsToVariants(visible)},
                {QStringLiteral("announcement"), QString()}};
    }

    visible.move(from, to);
    int vi = 0;
    QSet<QString> visibleSet;
    for (const QString &id : visible)
        visibleSet.insert(id);
    for (int i = 0; i < full.size(); ++i) {
        if (visibleSet.contains(full.at(i)))
            full[i] = visible.at(vi++);
    }

    return {{QStringLiteral("changed"), true},
            {QStringLiteral("savedOrder"), stringsToVariants(full)},
            {QStringLiteral("visibleOrder"), stringsToVariants(visible)},
            {QStringLiteral("providerId"), providerId},
            {QStringLiteral("direction"),
             direction < 0 ? QStringLiteral("left") : QStringLiteral("right")},
            {QStringLiteral("announcement"),
             QStringLiteral("%1 moved to position %2 of %3")
                 .arg(displayName(providerId))
                 .arg(to + 1)
                 .arg(visible.size())}};
}
