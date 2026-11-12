#include "HomeServerProgressClient.h"
#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <cmath>
#include <limits>

namespace {
QString encoded(const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); }
bool safeString(const QString &value,int limit) {
    return !value.isEmpty() && value.size()<=limit && !value.contains(QRegularExpression("[\\x00-\\x1f\\x7f]"));
}
bool nonnegativeInteger(const QJsonValue &value) {
    if(!value.isDouble()) return false;
    const auto n=value.toInteger(-1);
    return n>=0 && value.toDouble()==static_cast<double>(n);
}
bool validUserData(const QJsonValue &value) {
    if(!value.isObject()) return false;
    const auto body=value.toObject();
    if(!nonnegativeInteger(body.value("PlaybackPositionTicks")) || !nonnegativeInteger(body.value("PlayCount")) ||
       body.value("PlayCount").toInteger()>std::numeric_limits<int>::max() || !body.value("IsFavorite").isBool() || !body.value("Played").isBool()) return false;
    if(body.contains("Likes") && !body.value("Likes").isNull() && !body.value("Likes").isBool()) return false;
    if(body.contains("LastPlayedDate") && !body.value("LastPlayedDate").isNull() && !body.value("LastPlayedDate").isString()) return false;
    return true;
}
bool activityTime(const QJsonValue &value,qint64 &milliseconds) {
    milliseconds=0;
    if(value.isUndefined() || value.isNull()) return true;
    if(!value.isString()) return false;
    const auto text=value.toString();
    if(text.isEmpty()) return true;
    auto date=QDateTime::fromString(text,Qt::ISODateWithMs);
    if(!date.isValid()) date=QDateTime::fromString(text,Qt::ISODate);
    if(!date.isValid()) return false;
    milliseconds=qMax<qint64>(0,date.toMSecsSinceEpoch());
    return true;
}
int serverMajor(const QJsonObject &connection) {
    const auto value=connection.value("serverMajor");
    if(!nonnegativeInteger(value) || value.toInteger()>std::numeric_limits<int>::max()) return 0;
    return static_cast<int>(value.toInteger());
}
}
HomeServerProgressJob::HomeServerProgressJob(HomeServerClient *client,QObject *parent):QObject(parent),m_client(client) {}
HomeServerProgressJob::~HomeServerProgressJob() { cancel(); }
void HomeServerProgressJob::cancel() {
    m_finished=true;
    m_callback={};
    m_token.clear();
    m_playback={};
}
void HomeServerProgressJob::finish(QString error,bool expired) {
    if(m_finished) return;
    m_finished=true;
    m_token.clear();
    m_playback={};
    auto callback=std::move(m_callback);
    if(callback) callback({std::move(error),expired});
}
void HomeServerProgressJob::start(QJsonObject connection,QJsonObject item,HomeServerPlaybackJob::Result playback,
                                  QString token,QString event,HomeServerProgressState state,Callback callback) {
    if(m_started || m_finished) return;
    m_started=true;
    m_connection=std::move(connection);m_item=std::move(item);m_playback=std::move(playback);
    m_token=std::move(token);m_event=std::move(event);m_state=state;m_callback=std::move(callback);
    m_major=serverMajor(m_connection);
    const auto provider=m_connection.value("provider").toString();
    const bool knownProvider=QStringList{"jellyfin","emby","plex"}.contains(provider);
    const bool selected=m_event=="start" || m_event=="progress" || m_event=="stop";
    const bool knownEvent=selected || m_event=="sync" || m_event=="watched";
    const auto origin=QUrl(m_connection.value("origin").toString(),QUrl::StrictMode);
    if(!m_client || !knownProvider || !knownEvent || !safeString(m_token,65536) || !safeString(m_item.value("id").toString(),1024) ||
       HomeServerClient::relativeUrl(origin,"progress-validation").isEmpty() || (provider!="plex" && !safeString(m_connection.value("userId").toString(),1024))) {
        finish("Invalid home-server progress request");return;
    }
    // Tick conversion must remain within signed 64-bit range. Restrict timestamp
    // output to the ISO calendar range supported by every provider.
    constexpr auto MaxMs=std::numeric_limits<qint64>::max()/10000;
    constexpr qint64 MaxTimestamp=253402300799999LL;
    if(m_state.positionMs<0 || m_state.durationMs<0 || m_state.updatedAt<0 || m_state.durationMs>MaxMs || m_state.updatedAt>MaxTimestamp) {
        finish("Invalid home-server progress state");return;
    }
    if(m_state.durationMs>0) m_state.positionMs=qMin(m_state.positionMs,m_state.durationMs);
    if(m_state.positionMs>MaxMs || (m_event=="sync" && m_state.updatedAt<=0) || (m_event=="watched" && !m_state.writePlayed)) {
        finish("Invalid home-server progress state");return;
    }
    if(provider=="jellyfin" && (m_event=="sync" || m_event=="watched" || m_state.writePlayed) && m_major<10) {
        finish("Jellyfin server version is required for progress synchronization");return;
    }
    if(selected && (!m_playback.error.isEmpty() || m_playback.expired || !safeString(m_playback.mediaSourceId,1024) ||
       !QRegularExpression("^[A-Za-z0-9_-]{1,256}$").match(m_playback.playSessionId).hasMatch() ||
       (provider!="plex" && !QStringList{"DirectPlay","DirectStream","Transcode"}.contains(m_playback.playMethod)))) {
        finish("The selected playback source and session are required");return;
    }
    if(m_event=="watched") watched();
    else if(m_event=="sync") synchronize();
    else report();
}
void HomeServerProgressJob::request(QString path,QByteArray method,QJsonValue body,std::function<void(QJsonValue)> callback) {
    if(m_finished) return;
    if(!m_client) { finish("Home-server progress client is unavailable");return; }
    QPointer<HomeServerProgressJob> guard(this);
    m_client->request(m_connection.value("provider").toString(),QUrl(m_connection.value("origin").toString()),std::move(path),std::move(method),m_token,std::move(body),
        [guard,callback=std::move(callback)](HomeServerClient::Response response) {
            if(!guard || guard->m_finished) return;
            if(!response.ok()) {guard->finish(response.error.isEmpty()?QString("Home-server progress request failed"):response.error,response.expired());return;}
            callback(std::move(response.body));
        });
}
void HomeServerProgressJob::report() {
    const auto provider=m_connection.value("provider").toString();
    const auto id=m_item.value("id").toString();
    if(provider=="plex") {
        if(m_event=="sync" && m_playback.playSessionId.isEmpty()) m_playback.playSessionId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        // X-Plex-* headers are explicitly supported as query parameters. The
        // token stays in HomeServerClient's native request headers.
        QUrlQuery query;
        query.addQueryItem("ratingKey",id);
        query.addQueryItem("key","/library/metadata/"+id);
        query.addQueryItem("identifier","com.plexapp.plugins.library");
        query.addQueryItem("state",m_event=="stop" || m_event=="sync"?"stopped":m_state.paused?"paused":"playing");
        query.addQueryItem("time",QString::number(m_state.positionMs));
        if(m_state.durationMs>0) query.addQueryItem("duration",QString::number(m_state.durationMs));
        query.addQueryItem("X-Plex-Session-Identifier",m_playback.playSessionId);
        if(m_event=="sync") {
            // Accepted protocol mapping: epoch seconds. Plex documents the
            // offline timestamp's meaning, but not its exact unit; live gap.
            query.addQueryItem("updated",QString::number(m_state.updatedAt/1000));
            query.addQueryItem("offline","1");
        }
        request("/:/timeline?"+query.toString(QUrl::FullyEncoded),"POST",{},[this](QJsonValue){watched();});
        return;
    }
    QJsonObject body{{"ItemId",id},{"MediaSourceId",m_playback.mediaSourceId},{"PlaySessionId",m_playback.playSessionId},
        {"PositionTicks",m_state.positionMs*10000}};
    if(m_event!="stop") {
        body.insert("IsPaused",m_state.paused);body.insert("PlayMethod",m_playback.playMethod);body.insert("CanSeek",true);
    }
    // Device identity is already present in Authorization. It is not a member
    // of the providers' session DTOs. Stopped DTOs also omit start-only fields.
    const QString path=m_event=="start"?"/Sessions/Playing":m_event=="progress"?"/Sessions/Playing/Progress":"/Sessions/Playing/Stopped";
    request(path,"POST",body,[this](QJsonValue){watched();});
}
QString HomeServerProgressJob::userDataPath() const {
    const auto id=encoded(m_item.value("id").toString()),user=m_connection.value("userId").toString();
    if(m_connection.value("provider")=="jellyfin" && m_major>=12) {
        QUrlQuery query;query.addQueryItem("userId",user);
        return "/UserItems/"+id+"/UserData?"+query.toString(QUrl::FullyEncoded);
    }
    return "/Users/"+encoded(user)+"/Items/"+id+"/UserData";
}
void HomeServerProgressJob::synchronize() {
    if(m_connection.value("provider")=="plex") {
        const auto id=m_item.value("id").toString();
        request("/library/metadata/"+encoded(id),"GET",{},[this,id](QJsonValue value) {
            const auto container=value.toObject().value("MediaContainer").toObject();
            const auto metadata=container.value("Metadata");
            if(!metadata.isArray() || metadata.toArray().size()!=1 ||
               (container.contains("size") && (!nonnegativeInteger(container.value("size")) || container.value("size").toInteger()!=1))) {
                finish("Invalid Plex progress metadata response");return;
            }
            const auto item=metadata.toArray().first().toObject();
            const auto key=item.value("ratingKey");
            const auto ratingKey=key.isString()?key.toString():nonnegativeInteger(key)?QString::number(key.toInteger()):QString();
            if(ratingKey!=id) {finish("Plex progress metadata does not match the requested item");return;}
            qint64 remoteTime=0;
            const auto timestamp=item.value("lastViewedAt");
            if(!timestamp.isUndefined() && !timestamp.isNull()) {
                if(!nonnegativeInteger(timestamp) || timestamp.toInteger()>253402300799LL) {finish("Invalid Plex progress activity timestamp");return;}
                remoteTime=timestamp.toInteger()*1000;
            }
            if(remoteTime>m_state.updatedAt) {finish();return;}
            const auto viewCount=item.value("viewCount");
            if(m_state.writePlayed && !viewCount.isUndefined()) {
                if(!nonnegativeInteger(viewCount)) {finish("Invalid Plex played-state response");return;}
                if((viewCount.toInteger()>0)==m_state.played) m_state.writePlayed=false;
            }
            report();
        });
        return;
    }
    const auto path=userDataPath();
    request(path,"GET",{},[this,path](QJsonValue value) {
        if(!validUserData(value)) {finish("Invalid home-server user data response");return;}
        auto body=value.toObject();
        qint64 remoteTime=0;
        if(!activityTime(body.value("LastPlayedDate"),remoteTime)) {finish("Invalid home-server progress activity timestamp");return;}
        if(remoteTime>m_state.updatedAt) {finish();return;}
        if(m_state.writePlayed && body.value("Played").toBool()==m_state.played) m_state.writePlayed=false;
        // Preserve the complete native DTO, including play count, favorites,
        // likes and the freshly read played flag unless explicitly changed.
        body.insert("PlaybackPositionTicks",m_state.positionMs*10000);
        if(m_state.writePlayed) body.insert("Played",m_state.played);
        body.insert("LastPlayedDate",QDateTime::fromMSecsSinceEpoch(m_state.updatedAt,Qt::UTC).toString(Qt::ISODateWithMs));
        request(path,"POST",body,[this](QJsonValue){watched();});
    });
}
void HomeServerProgressJob::watched() {
    if(!m_state.writePlayed) {finish();return;}
    const auto provider=m_connection.value("provider").toString(),id=m_item.value("id").toString();
    QUrlQuery query;
    if(provider=="plex") {
        query.addQueryItem("key",id);query.addQueryItem("identifier","com.plexapp.plugins.library");
        request(QString(m_state.played?"/:/scrobble?":"/:/unscrobble?")+query.toString(QUrl::FullyEncoded),"PUT",{},[this](QJsonValue){finish();});
        return;
    }
    QString path;
    const bool modern=provider=="jellyfin" && m_major>=12;
    if(modern) {path="/UserPlayedItems/"+encoded(id);query.addQueryItem("userId",m_connection.value("userId").toString());}
    else path="/Users/"+encoded(m_connection.value("userId").toString())+"/PlayedItems/"+encoded(id);
    if(m_state.played && m_state.updatedAt>0) {
        const auto time=QDateTime::fromMSecsSinceEpoch(m_state.updatedAt,Qt::UTC);
        query.addQueryItem(modern?"datePlayed":"DatePlayed",modern?time.toString(Qt::ISODateWithMs):time.toString("yyyyMMddHHmmss"));
    }
    if(!query.isEmpty()) path+='?'+query.toString(QUrl::FullyEncoded);
    request(path,m_state.played?"POST":"DELETE",{},[this](QJsonValue){finish();});
}
