#include "JellyfinEmbyClient.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <memory>
#include <utility>

namespace Colosseum::HomeServers {
namespace {
constexpr qsizetype kMaxReplyBytes = 8 * 1024 * 1024;
constexpr int kDeadlineMs = 15000;
constexpr auto kClientVersion = "1.4.0";

bool parseObject(const QByteArray &body, QJsonObject *object)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    *object = document.object();
    return true;
}

QString providerId(const QJsonObject &ids, const QString &key)
{
    for (auto it = ids.constBegin(); it != ids.constEnd(); ++it) {
        if (it.key().compare(key, Qt::CaseInsensitive) == 0)
            return it.value().toString().trimmed();
    }
    return {};
}

QString normalizedImdb(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^tt[0-9]{6,10}$"));
    const QString normalized = id.trimmed().toLower();
    return pattern.match(normalized).hasMatch() ? normalized : QString();
}
}

bool ServerItem::hasExternalId() const
{
    return !imdbId.isEmpty() || !tmdbId.isEmpty() || !tvdbId.isEmpty();
}

bool ServerItem::matchesImdb(const QString &imdb) const
{
    const QString id = normalizedImdb(imdb);
    return !id.isEmpty() && id == normalizedImdb(imdbId);
}

bool ServerItem::matchesEpisodeOf(const ServerItem &series,
                                  const QString &seriesImdb,
                                  int season, int episode) const
{
    return type == QLatin1String("Episode") && series.type == QLatin1String("Series")
        && !serverId.isEmpty() && serverId == series.serverId
        && !userId.isEmpty() && userId == series.userId
        && !series.id.isEmpty() && seriesId == series.id
        && season >= 0 && episode > 0 && seasonNumber == season
        && episodeNumber == episode && series.matchesImdb(seriesImdb);
}

JellyfinEmbyClient::JellyfinEmbyClient(ServerKind kind, QUrl serverUrl,
                                     QString deviceId, QObject *parent,
                                     Transport transport)
    : QObject(parent), m_kind(kind), m_deviceId(std::move(deviceId)),
      m_transport(std::move(transport))
{
    static const QRegularExpression devicePattern(QStringLiteral("^[a-zA-Z0-9._:-]{1,128}$"));
    if (!serverUrl.isValid() || serverUrl.isRelative()
        || (serverUrl.scheme() != QLatin1String("http")
            && serverUrl.scheme() != QLatin1String("https"))
        || serverUrl.host().isEmpty() || !serverUrl.userInfo().isEmpty()
        || serverUrl.hasQuery() || serverUrl.hasFragment()
        || !devicePattern.match(m_deviceId).hasMatch())
        return;

    QString prefix = serverUrl.path(QUrl::FullyDecoded);
    const QStringList segments = prefix.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (prefix.contains(QLatin1String("//")) || prefix.contains(QLatin1Char('\\'))
        || segments.contains(QLatin1String(".")) || segments.contains(QLatin1String("..")))
        return;
    while (prefix.endsWith(QLatin1Char('/')))
        prefix.chop(1);
    // The supplied URL is the API base, including any /emby or proxy prefix.
    // Do not guess or duplicate prefixes; connection discovery can probe later.
    serverUrl.setPath(prefix);
    m_apiBase = serverUrl;
}

bool JellyfinEmbyClient::isValid() const
{
    return m_apiBase.isValid() && !m_apiBase.host().isEmpty();
}

QUrl JellyfinEmbyClient::apiBaseUrl() const
{
    return m_apiBase;
}

bool JellyfinEmbyClient::validId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z0-9_-]{1,128}$"));
    return pattern.match(id).hasMatch();
}

bool JellyfinEmbyClient::validToken(const QByteArray &token)
{
    return !token.isEmpty() && token.size() <= 65536
        && std::all_of(token.cbegin(), token.cend(), [](unsigned char c) {
               return c >= 0x21 && c <= 0x7e;
           });
}

QUrl JellyfinEmbyClient::endpoint(const QString &path) const
{
    QUrl url = m_apiBase;
    url.setPath(m_apiBase.path() + path);
    return url;
}

QNetworkRequest JellyfinEmbyClient::request(const QUrl &url,
                                            const QByteArray &token) const
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                     QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                     QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,
                     QNetworkRequest::Manual);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Cache-Control", "no-store");
    const QByteArray scheme = m_kind == ServerKind::Emby
        ? QByteArrayLiteral("Emby ") : QByteArrayLiteral("MediaBrowser ");
    const QByteArray client = scheme + QByteArrayLiteral(
        "Client=\"Colosseum\", Device=\"Colosseum\", DeviceId=\"")
        + m_deviceId.toLatin1() + QByteArrayLiteral("\", Version=\"")
        + QByteArray(kClientVersion) + QByteArrayLiteral("\"");
    if (m_kind == ServerKind::Jellyfin) {
        // Jellyfin requires the standard Authorization scheme on newer servers.
        // Percent-encode to prevent a token from altering the parameter grammar.
        const QByteArray auth = token.isEmpty() ? client
            : client + QByteArrayLiteral(", Token=\"")
                + QUrl::toPercentEncoding(QString::fromLatin1(token))
                + QByteArrayLiteral("\"");
        req.setRawHeader("Authorization", auth);
    } else {
        req.setRawHeader("X-Emby-Authorization", client);
        if (!token.isEmpty())
            req.setRawHeader("X-Emby-Token", token);
    }
    return req;
}

void JellyfinEmbyClient::send(const QUrl &url, const QByteArray &token,
                              bool post, const QByteArray &body, ReplyDone done)
{
    const auto safe = QPointer<JellyfinEmbyClient>(this);
    auto guarded = [safe, done = std::move(done)](TransportReply reply) mutable {
        if (safe)
            done(std::move(reply));
    };
    const QNetworkRequest req = request(url, token);
    if (m_transport) {
        m_transport(req, post, body, std::move(guarded));
        return;
    }
    QNetworkReply *reply = post ? m_network.post(req, body) : m_network.get(req);
    auto payload = std::make_shared<QByteArray>();
    auto oversized = std::make_shared<bool>(false);
    auto drain = [reply, payload, oversized]() {
        if (*oversized)
            return;
        if (reply->bytesAvailable() > kMaxReplyBytes - payload->size()) {
            *oversized = true;
            reply->abort();
            return;
        }
        payload->append(reply->readAll());
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    QTimer::singleShot(kDeadlineMs, reply, [reply]() {
        if (!reply->isFinished())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [reply, payload, oversized, drain,
             done = std::move(guarded)]() mutable {
                drain();
                const int status = *oversized ? 0
                    : reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                done({status, *oversized ? QByteArray() : *payload});
                reply->deleteLater();
            });
}

void JellyfinEmbyClient::authenticate(const QString &username,
                                      const QString &password, AuthDone done)
{
    if (!isValid() || username.trimmed().isEmpty() || username.size() > 320
        || password.isEmpty() || password.size() > 1024) {
        done({400, QStringLiteral("Invalid server or credentials"), {}});
        return;
    }
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("Username"), username},
        {QStringLiteral("Pw"), password}
    }).toJson(QJsonDocument::Compact);
    send(endpoint(QStringLiteral("/Users/AuthenticateByName")), {}, true, body,
         [done = std::move(done)](TransportReply reply) mutable {
             ServerResult<ServerSession> result;
             result.status = reply.status;
             QJsonObject json;
             if (reply.status != 200 || !parseObject(reply.body, &json)) {
                 result.error = QStringLiteral("Authentication failed");
             } else {
                 const QJsonObject user = json.value(QStringLiteral("User")).toObject();
                 result.value.userId = user.value(QStringLiteral("Id")).toString();
                 result.value.serverId = json.value(QStringLiteral("ServerId")).toString();
                 result.value.accessToken =
                     json.value(QStringLiteral("AccessToken")).toString().toUtf8();
                 if (!JellyfinEmbyClient::validId(result.value.userId)
                     || result.value.serverId.isEmpty()
                     || !JellyfinEmbyClient::validToken(result.value.accessToken)) {
                     result.value = {};
                     result.error = QStringLiteral("Invalid authentication response");
                 }
             }
             done(std::move(result));
         });
}

void JellyfinEmbyClient::fetchViews(const ServerSession &session, ViewsDone done)
{
    if (!isValid() || !validId(session.userId)
        || !validToken(session.accessToken)) {
        done({400, QStringLiteral("Invalid session"), {}});
        return;
    }
    QUrl url = m_kind == ServerKind::Jellyfin
        ? endpoint(QStringLiteral("/UserViews"))
        : endpoint(QStringLiteral("/Users/") + session.userId + QStringLiteral("/Views"));
    if (m_kind == ServerKind::Jellyfin) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("userId"), session.userId);
        url.setQuery(query);
    }
    send(url, session.accessToken, false, {},
         [done = std::move(done)](TransportReply reply) mutable {
             ServerResult<QList<ServerView>> result;
             result.status = reply.status;
             QJsonObject json;
             if (reply.status != 200 || !parseObject(reply.body, &json)
                 || !json.value(QStringLiteral("Items")).isArray()) {
                 result.error = QStringLiteral("Failed to load server libraries");
             } else {
                 const auto items = json.value(QStringLiteral("Items")).toArray();
                 for (const QJsonValue &item : items) {
                     const QJsonObject object = item.toObject();
                     const QString id = object.value(QStringLiteral("Id")).toString();
                     if (!JellyfinEmbyClient::validId(id))
                         continue;
                     result.value.append({id,
                         object.value(QStringLiteral("Name")).toString(),
                         object.value(QStringLiteral("CollectionType")).toString()});
                 }
             }
             done(std::move(result));
         });
}

void JellyfinEmbyClient::fetchItemsPage(const ServerSession &session,
                                        const QString &viewId, int startIndex,
                                        int pageSize, PageDone done)
{
    if (!isValid() || !validId(session.userId) || !validId(viewId)
        || !validToken(session.accessToken) || startIndex < 0
        || pageSize < 1 || pageSize > 200) {
        done({400, QStringLiteral("Invalid page request"), {}});
        return;
    }
    QUrl url = m_kind == ServerKind::Jellyfin
        ? endpoint(QStringLiteral("/Items"))
        : endpoint(QStringLiteral("/Users/") + session.userId + QStringLiteral("/Items"));
    QUrlQuery query;
    if (m_kind == ServerKind::Jellyfin)
        query.addQueryItem(QStringLiteral("userId"), session.userId);
    query.addQueryItem(QStringLiteral("ParentId"), viewId);
    query.addQueryItem(QStringLiteral("StartIndex"), QString::number(startIndex));
    query.addQueryItem(QStringLiteral("Limit"), QString::number(pageSize));
    query.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("IncludeItemTypes"),
                       QStringLiteral("Movie,Series,Season,Episode"));
    query.addQueryItem(QStringLiteral("CollapseBoxSetItems"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
    query.addQueryItem(QStringLiteral("Fields"),
                       QStringLiteral("ProviderIds,MediaStreams,DateCreated,ParentId,Path"));
    query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("EnableMediaSources"), QStringLiteral("true"));
    url.setQuery(query);
    send(url, session.accessToken, false, {},
         [serverId = session.serverId, userId = session.userId,
          startIndex, pageSize, done = std::move(done)](TransportReply reply) mutable {
             ServerResult<ItemPage> result;
             result.status = reply.status;
             QJsonObject json;
             if (reply.status != 200 || !parseObject(reply.body, &json)
                 || !json.value(QStringLiteral("Items")).isArray()
                 || !json.value(QStringLiteral("TotalRecordCount")).isDouble()) {
                 result.error = QStringLiteral("Failed to read server items");
             } else {
                 const auto items = json.value(QStringLiteral("Items")).toArray();
                 const int total = json.value(QStringLiteral("TotalRecordCount")).toInt(-1);
                 if (total < 0 || items.size() > pageSize
                      || total < startIndex || total - startIndex < items.size()) {
                     result.error = QStringLiteral("Invalid server page");
                 } else {
                     result.value.startIndex = startIndex;
                     result.value.totalRecordCount = total;
                     result.value.returnedCount = items.size();
                     for (const QJsonValue &value : items) {
                         const QJsonObject object = value.toObject();
                         const QString id = object.value(QStringLiteral("Id")).toString();
                         const QString type = object.value(QStringLiteral("Type")).toString();
                         if (!JellyfinEmbyClient::validId(id)
                             || (type != QLatin1String("Movie")
                                 && type != QLatin1String("Series")
                                 && type != QLatin1String("Season")
                                 && type != QLatin1String("Episode")))
                             continue;
                         const QJsonObject provider = object.value(
                             QStringLiteral("ProviderIds")).toObject();
                         ServerItem item;
                         item.id = id;
                         item.serverId = serverId;
                         item.userId = userId;
                         item.name = object.value(QStringLiteral("Name")).toString();
                         item.type = type;
                         item.imdbId = providerId(provider, QStringLiteral("Imdb"));
                         item.tmdbId = providerId(provider, QStringLiteral("Tmdb"));
                         item.tvdbId = providerId(provider, QStringLiteral("Tvdb"));
                         item.seriesId = object.value(QStringLiteral("SeriesId")).toString();
                         item.parentId = object.value(QStringLiteral("ParentId")).toString();
                         item.seasonNumber = type == QLatin1String("Season")
                             ? object.value(QStringLiteral("IndexNumber")).toInt(-1)
                             : object.value(QStringLiteral("ParentIndexNumber")).toInt(-1);
                         item.episodeNumber = type == QLatin1String("Episode")
                             ? object.value(QStringLiteral("IndexNumber")).toInt(-1) : -1;
                         item.productionYear = object.value(
                             QStringLiteral("ProductionYear")).toInt();
                         item.runTimeTicks = object.value(
                             QStringLiteral("RunTimeTicks")).toInteger();
                         item.dateCreated = object.value(
                             QStringLiteral("DateCreated")).toString();
                         item.mediaSources = object.value(QStringLiteral("MediaSources")).toArray();
                         item.userData = object.value(QStringLiteral("UserData")).toObject();
                         result.value.items.append(std::move(item));
                     }
                 }
             }
             done(std::move(result));
         });
}

} // namespace Colosseum::HomeServers
