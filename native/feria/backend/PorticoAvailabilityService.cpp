#include "PorticoAvailabilityService.h"
#include "PorticoCanonicalizer.h"
#include "PorticoDestinationResolver.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSet>
#include <QUrl>

namespace {
QVariantMap state(const QString &status, const QString &region)
{
    return {{"status", status}, {"region", region}, {"source", "JustWatch"},
            {"offers", QVariantList{}},
            {"attributionUrl", "https://www.justwatch.com/" + region.toLower()}};
}
QString providerId(const QString &name)
{
    static const QHash<QString, QString> aliases{
        {"netflixbasicwithads", "netflix"}, {"amazonprime", "prime"},
        {"amazonprimevideowithads", "prime"}, {"amazon", "prime"},
        {"disneyplus", "disney"}, {"max", "hbomax"}, {"itunes", "appletv"},
        {"appletvplus", "appletv"}, {"huluwithads", "hulu"}};
    return aliases.value(name, name);
}
QString offerType(const QString &type)
{
    static const QHash<QString, QString> labels{
        {"FLATRATE", "Subscription"}, {"RENT", "Rent"}, {"BUY", "Buy"},
        {"FREE", "Free"}, {"ADS", "Free with ads"}, {"FAST", "Live channel"}};
    return labels.value(type, "Listed offer");
}
}

PorticoAvailabilityService::PorticoAvailabilityService(QObject *parent) : QObject(parent) {}

QString PorticoAvailabilityService::key(const QVariantMap &item, const QString &region)
{
    return region.toUpper() + '|' + PorticoCanonicalizer::canonicalKey(PorticoTrend::itemFromVariantMap(item));
}

QVariantMap PorticoAvailabilityService::lookup(const QVariantMap &item, const QString &region) const
{
    if (PorticoDestinationResolver::mediumForKind(item.value("kind").toString()) != "watch")
        return state("unsupported", region.toUpper());
    return m_cache.value(key(item, region), state("idle", region.toUpper()));
}

void PorticoAvailabilityService::request(const QVariantMap &item, const QString &region, bool force)
{
    const QString country = region.toUpper();
    if (item.value("title").toString().trimmed().isEmpty()
        || PorticoDestinationResolver::mediumForKind(item.value("kind").toString()) != "watch") return;
    const QString cacheKey = key(item, country);
    const auto cached = m_cache.value(cacheKey);
    if (cached.value("status") == "loading") return;
    const qint64 age = cached.value("checkedAt").toDateTime().secsTo(QDateTime::currentDateTimeUtc());
    const int lifetime = cached.value("status") == "error" ? 60 : 21600;
    if (!force && !cached.isEmpty() && age >= 0 && age < lifetime) return;
    if (m_cache.size() >= 128) {
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (it.value().value("status") != "loading") { m_cache.erase(it); break; }
        }
    }
    m_cache.insert(cacheKey, state("loading", country));
    emit changed();
    // Public website endpoint; errors never turn catalog/search hints into confirmed offers.
    static const QString query = QStringLiteral(R"(
query($country:Country!, $query:String!) {
  popularTitles(country:$country, first:10, filter:{searchQuery:$query}) {
    edges { node { objectType
      content(country:$country,language:en) { title originalReleaseYear fullPath externalIds { imdbId } }
      offers(country:$country,platform:WEB) { monetizationType standardWebURL package { clearName technicalName } }
    } }
  }
})");
    QNetworkRequest req(QUrl("https://apis.justwatch.com/graphql"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("User-Agent", "Colosseum-Feria/1.0");
    req.setTransferTimeout(15000);
    const QJsonObject variables{{"country", country}, {"query", item.value("title").toString()}};
    auto *reply = m_network.post(req, QJsonDocument(QJsonObject{{"query", query}, {"variables", variables}}).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, item, country, cacheKey] {
        auto result = reply->error() == QNetworkReply::NoError
            ? parse(item, country, reply->readAll()) : state("error", country);
        result.insert("checkedAt", QDateTime::currentDateTimeUtc());
        m_cache.insert(cacheKey, result);
        reply->deleteLater();
        emit changed();
    });
}

QVariantMap PorticoAvailabilityService::parse(const QVariantMap &item, const QString &region, const QByteArray &body)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(body, &error);
    const auto root = document.object();
    const auto titles = root.value("data").toObject().value("popularTitles").toObject();
    if (error.error != QJsonParseError::NoError || !root.value("errors").toArray().isEmpty()
        || !titles.value("edges").isArray()) return state("error", region);
    const QString imdb = item.value("externalIds").toMap().value("imdb").toString();
    const QString kind = item.value("kind").toString();
    const QString objectType = kind == "film" ? "MOVIE" : "SHOW";
    QJsonObject matched;
    for (const auto &edge : titles.value("edges").toArray()) {
        const auto node = edge.toObject().value("node").toObject();
        const auto content = node.value("content").toObject();
        if (node.value("objectType").toString() != objectType) continue;
        if (!imdb.isEmpty()) {
            if (content.value("externalIds").toObject().value("imdbId").toString() != imdb) continue;
        } else {
            // Name-only matches cannot safely distinguish remakes and namesakes.
            const int year = item.value("year").toInt();
            if (year <= 0 || content.value("originalReleaseYear").toInt() != year
                || PorticoCanonicalizer::normalizedText(content.value("title").toString())
                    != PorticoCanonicalizer::normalizedText(item.value("title").toString())) continue;
        }
        if (!matched.isEmpty()) return state("unmatched", region);
        matched = node;
    }
    if (matched.isEmpty()) return state("unmatched", region);
    if (!matched.value("offers").isArray()) return state("error", region);
    auto result = state("ready", region);
    const QString path = matched.value("content").toObject().value("fullPath").toString();
    if (path.startsWith('/' + region.toLower() + '/'))
        result.insert("attributionUrl", "https://www.justwatch.com" + path);
    QVariantList offers;
    QSet<QString> seen;
    for (const auto &value : matched.value("offers").toArray()) {
        const auto offer = value.toObject();
        const auto package = offer.value("package").toObject();
        QString url = offer.value("standardWebURL").toString();
        url.replace("\\u0026", "&");
        const QUrl parsed(url);
        if (!parsed.isValid() || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()
            || (parsed.scheme() != "https" && parsed.scheme() != "http")) continue;
        const QString id = providerId(package.value("technicalName").toString());
        const QString type = offerType(offer.value("monetizationType").toString());
        const QString identity = id + '|' + url + '|' + type;
        if (id.isEmpty() || seen.contains(identity)) continue;
        seen.insert(identity);
        offers.append(QVariantMap{{"providerId", id}, {"label", package.value("clearName").toString()},
            {"url", url}, {"exact", true}, {"actionable", true}, {"appOnly", false},
            {"reason", "availability"}, {"availabilityConfirmed", true}, {"offerType", type},
            {"availabilitySource", "JustWatch"}, {"region", region}});
    }
    result.insert("offers", offers);
    return result;
}
