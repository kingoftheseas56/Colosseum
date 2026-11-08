#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>
#include <optional>

namespace Colosseum::HomeServers {

enum class ServerKind { Jellyfin, Emby };

struct ServerSession {
    QString userId;
    QString serverId;
    QByteArray accessToken;
};

struct ServerView {
    QString id;
    QString name;
    QString collectionType;
};

struct ServerItem {
    QString id;
    QString serverId;
    QString userId;
    QString name;
    QString type;
    QString imdbId;
    QString tmdbId;
    QString tvdbId;
    QString seriesId;
    QString parentId;
    int seasonNumber = -1;
    int episodeNumber = -1;
    int productionYear = 0;
    qint64 runTimeTicks = 0;
    QString dateCreated;
    QJsonArray mediaSources;
    QJsonObject userData;

    bool hasExternalId() const;
    bool matchesImdb(const QString &imdb) const;
    bool matchesEpisodeOf(const ServerItem &series, const QString &seriesImdb,
                          int season, int episode) const;
};

template <typename T>
struct ServerResult {
    int status = 0;
    QString error;
    T value{};
    bool ok() const { return status == 200 && error.isEmpty(); }
};

struct ItemPage {
    QList<ServerItem> items;
    int totalRecordCount = 0;
    int startIndex = 0;
    int returnedCount = 0; // Raw server page size, including unrecognized rows.
};

struct TransportReply {
    int status = 0;
    QByteArray body;
};

class JellyfinEmbyClient final : public QObject {
public:
    using ReplyDone = std::function<void(TransportReply)>;
    using Transport = std::function<void(const QNetworkRequest &request,
                                         bool post,
                                         const QByteArray &body,
                                         ReplyDone completion)>;
    using AuthDone = std::function<void(ServerResult<ServerSession>)>;
    using ViewsDone = std::function<void(ServerResult<QList<ServerView>>)>;
    using PageDone = std::function<void(ServerResult<ItemPage>)>;

    JellyfinEmbyClient(ServerKind kind, QUrl serverUrl, QString deviceId,
                       QObject *parent = nullptr, Transport transport = {});

    bool isValid() const;
    QUrl apiBaseUrl() const;
    void authenticate(const QString &username, const QString &password, AuthDone done);
    void fetchViews(const ServerSession &session, ViewsDone done);
    void fetchItemsPage(const ServerSession &session, const QString &viewId,
                        int startIndex, int pageSize, PageDone done);

private:
    QUrl endpoint(const QString &path) const;
    QNetworkRequest request(const QUrl &url, const QByteArray &token) const;
    void send(const QUrl &url, const QByteArray &token, bool post,
              const QByteArray &body, ReplyDone done);
    static bool validId(const QString &id);
    static bool validToken(const QByteArray &token);

    ServerKind m_kind;
    QUrl m_apiBase;
    QString m_deviceId;
    QNetworkAccessManager m_network;
    Transport m_transport;
};

} // namespace Colosseum::HomeServers
