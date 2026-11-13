#include "HomeServerCatalog.h"
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <cmath>

namespace {
constexpr int PageSize = 500, MaxItems = 250000, MaxPages = 10000;
QString text(const QJsonValue &v) { return v.isString() ? v.toString().left(4096) : QString(); }
QString identifier(const QJsonValue &v) {
    QString s = v.isString() ? v.toString() : QString();
    if (v.isDouble()) {
        const double n = v.toDouble();
        if (std::isfinite(n) && n > 0 && n <= 9007199254740991.0 && std::floor(n) == n)
            s = QString::number(static_cast<qint64>(n));
    }
    return !s.isEmpty() && s.size() <= 1024 && !s.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")) ? s : QString();
}
qint64 integer(const QJsonValue &v, qint64 fallback = -1) {
    if (!v.isDouble()) return fallback;
    const double n = v.toDouble();
    return std::isfinite(n) && n >= 0 && n <= 9007199254740991.0 && std::floor(n) == n ? static_cast<qint64>(n) : fallback;
}
qint64 positiveId(const QJsonValue &v) {
    if (v.isString()) {
        const auto s = v.toString();
        bool ok = false; const auto n = s.toLongLong(&ok);
        return ok && QRegularExpression("^[0-9]+$").match(s).hasMatch() && n > 0 && n <= 9007199254740991LL ? n : 0;
    }
    const auto n = integer(v, 0); return n > 0 ? n : 0;
}
QString imdb(const QJsonValue &v) {
    const auto s = v.toString().trimmed().toLower();
    return QRegularExpression("^tt[0-9]+$").match(s).hasMatch() ? s : QString();
}
qint64 dateMs(const QJsonValue &value) {
    auto date = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (!date.isValid()) date = QDateTime::fromString(value.toString(), Qt::ISODate);
    return date.isValid() ? qMax<qint64>(0, date.toMSecsSinceEpoch()) : 0;
}
qint64 ticks(const QJsonValue &v) { return integer(v, 0) / 10000; }
qint64 epoch(const QJsonValue &v) { const auto n = integer(v, 0); return n <= 9007199254740LL ? n * 1000 : 0; }
QString baseKind(QString kind) {
    if (kind == "movie") return "movie";
    if (kind == "tv" || kind == "series" || kind == "episode" || kind == "season") return "series";
    return {};
}
void copyNumber(const QJsonObject &from, const char *source, QJsonObject &to, const char *target) {
    const auto n = integer(from.value(source)); if (n >= 0) to.insert(target, n);
}
QJsonObject providerIdentity(const QJsonObject &providers) {
    QJsonObject out;
    const auto id = imdb(providers.value("Imdb")); if (!id.isEmpty()) out.insert("imdbId", id);
    for (auto pair : {qMakePair("Tmdb", "tmdbId"), qMakePair("Tvdb", "tvdbId")}) {
        const auto n = positiveId(providers.value(pair.first)); if (n) out.insert(pair.second, n);
    }
    return out;
}
QJsonObject plexIdentity(const QJsonObject &raw) {
    QJsonObject out; auto guids = raw.value("Guid").toArray();
    if (raw.value("guid").isString()) guids.append(QJsonObject{{"id", raw.value("guid")}});
    for (const auto &entry : guids) {
        const auto value = entry.toObject().value("id").toString();
        if (value.startsWith("imdb://")) { const auto id = imdb(value.mid(7)); if (!id.isEmpty()) out.insert("imdbId", id); }
        for (auto scheme : {QStringLiteral("tmdb"), QStringLiteral("tvdb")})
            if (value.startsWith(scheme + "://")) { const auto n = positiveId(value.mid(scheme.size()+3)); if (n) out.insert(scheme + "Id", n); }
    }
    return out;
}
QJsonObject subtitle(const QJsonObject &raw, bool plex) {
    QJsonObject out;
    const auto index = integer(raw.value(plex ? "index" : "Index"));
    const auto id = identifier(raw.value(plex ? "id" : "Id"));
    if (!id.isEmpty()) out.insert("id", id); else if (index >= 0) out.insert("id", QString::number(index));
    if (index >= 0) out.insert("index", index);
    out.insert("codec", text(raw.value(plex ? "codec" : "Codec")));
    out.insert("language", text(raw.value(plex ? "languageCode" : "Language")));
    out.insert("title", text(raw.value(plex ? "displayTitle" : "DisplayTitle")));
    if (plex && out.value("language").toString().isEmpty()) out.insert("language", text(raw.value("language")));
    if (plex && out.value("title").toString().isEmpty()) out.insert("title", text(raw.value("title")));
    return out;
}
// Accept both normalized items and a type-known request. Missing kind stays unknown.
QJsonObject identity(const QJsonObject &input) {
    auto out = input.value("identity").isObject() ? input.value("identity").toObject() : input;
    auto kind = baseKind(input.value("kind").toString(input.value("type").toString()));
    const auto rawId = input.value("identity").isObject() ? QString() : input.value("id").toString();
    auto parsed = QRegularExpression("^(tt[0-9]+)(?::([0-9]+):([0-9]+))?$").match(rawId);
    QJsonObject extracted;
    if (parsed.hasMatch()) {
        extracted.insert("imdbId", parsed.captured(1));
        if (!parsed.captured(2).isEmpty()) {
            extracted.insert("season", parsed.captured(2).toLongLong()); extracted.insert("episode", parsed.captured(3).toLongLong());
            if (!kind.isEmpty() && kind != "series") return {};
            kind = "series";
        }
    } else {
        parsed = QRegularExpression("^(tmdb|tvdb):(movie|tv)[:/]([0-9]+)(?::([0-9]+):([0-9]+))?$").match(rawId);
        if (parsed.hasMatch()) {
            const auto parsedKind = baseKind(parsed.captured(2));
            if (!kind.isEmpty() && kind != parsedKind) return {};
            kind = parsedKind; extracted.insert(parsed.captured(1)+"Id", parsed.captured(3).toLongLong());
            if (!parsed.captured(4).isEmpty()) { extracted.insert("season", parsed.captured(4).toLongLong()); extracted.insert("episode", parsed.captured(5).toLongLong()); }
        }
    }
    for (auto it = extracted.constBegin(); it != extracted.constEnd(); ++it) {
        if (out.contains(it.key()) && out.value(it.key()) != it.value()) return {};
        out.insert(it.key(), it.value());
    }
    if (kind.isEmpty()) return {};
    QJsonObject clean{{"type", kind}};
    const auto id = imdb(out.value("imdbId")); if (!id.isEmpty()) clean.insert("imdbId", id);
    for (const auto *key : {"tmdbId", "tvdbId"}) { const auto n = positiveId(out.value(key)); if (n) clean.insert(key, n); }
    for (const auto *key : {"season", "episode"}) if (out.contains(key)) {
        const auto n = integer(out.value(key)); if (n < 0 || n > 2147483647) return {}; clean.insert(key, n);
    }
    return clean;
}
QString encoded(QString value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); }
}

namespace HomeServerCatalog {
QJsonObject normalizeJellyfinItem(const QJsonObject &raw, const QJsonObject &library) {
    const QHash<QString, QString> kinds{{"Movie","movie"},{"Series","series"},{"Season","season"},{"Episode","episode"}};
    const auto kind = kinds.value(raw.value("Type").toString()); const auto id = identifier(raw.value("Id"));
    if (kind.isEmpty() || id.isEmpty()) return {};
    auto ids = kind == "episode" ? QJsonObject{} : providerIdentity(raw.value("ProviderIds").toObject());
    if (kind == "episode") { copyNumber(raw,"ParentIndexNumber",ids,"season"); copyNumber(raw,"IndexNumber",ids,"episode"); }
    auto parent = identifier(raw.value(kind == "episode" ? "SeriesId" : "ParentId"));
    if (parent.isEmpty()) parent = identifier(raw.value("ParentId"));
    QJsonArray versions;
    const auto sources = raw.value("MediaSources").toArray(); if (sources.size() > 256) return {};
    for (qsizetype index=0; index<sources.size(); ++index) {
        const auto source = sources.at(index).toObject(); const auto sourceId = identifier(source.value("Id"));
        if (sourceId.isEmpty() || sourceId.contains("://")) continue;
        QJsonObject version{{"id", sourceId}, {"name", text(source.value("Name"))}, {"container", text(source.value("Container"))},
            {"runtimeMs", source.contains("RunTimeTicks") ? ticks(source.value("RunTimeTicks")) : ticks(raw.value("RunTimeTicks"))},
            {"directPlayable", source.value("SupportsDirectPlay").toBool(true)}, {"mediaIndex", index}, {"partIndex", 0}};
        const auto bitrate = integer(source.value("Bitrate")); if (bitrate >= 0) version.insert("bitrateKbps", bitrate/1000);
        QJsonArray subtitles; bool video=false, audio=false;
        for (const auto &value : source.value("MediaStreams").toArray()) {
            const auto stream=value.toObject(); const auto type=stream.value("Type").toString();
            if (type=="Video" && !video) { video=true; version.insert("videoCodec",text(stream.value("Codec"))); copyNumber(stream,"Width",version,"width"); copyNumber(stream,"Height",version,"height"); }
            if (type=="Audio" && !audio) { audio=true; version.insert("audioCodec",text(stream.value("Codec"))); }
            if (type=="Subtitle") subtitles.append(subtitle(stream,false));
        }
        if (subtitles.size()>256) return {};
        version.insert("subtitles",subtitles); versions.append(version);
    }
    const auto user=raw.value("UserData").toObject();
    QJsonObject progress{{"positionMs",ticks(user.value("PlaybackPositionTicks"))},{"durationMs",ticks(raw.value("RunTimeTicks"))},
        {"played",user.value("Played").toBool()},{"updatedAt",dateMs(user.value("LastPlayedDate"))}};
    QJsonObject out{{"id",id},{"libraryId",library.value("id")},{"libraryName",text(library.value("name"))},{"parentId",parent},
        {"kind",kind},{"title",raw.value("Name").toString("Untitled").left(4096)},{"identity",ids},{"versions",versions},{"progress",progress},
        {"addedAt",dateMs(raw.value("DateCreated"))},{"updatedAt",dateMs(raw.value("DateLastSaved"))}};
    copyNumber(raw,"ProductionYear",out,"year"); return out;
}
QJsonObject normalizePlexItem(const QJsonObject &raw, const QJsonObject &library) {
    const QHash<QString,QString> kinds{{"movie","movie"},{"show","series"},{"season","season"},{"episode","episode"}};
    const auto kind=kinds.value(raw.value("type").toString()); const auto id=identifier(raw.value("ratingKey"));
    if (kind.isEmpty() || id.isEmpty()) return {};
    auto ids=kind=="episode"?QJsonObject{}:plexIdentity(raw);
    if (kind=="episode") { copyNumber(raw,"parentIndex",ids,"season"); copyNumber(raw,"index",ids,"episode"); }
    auto parent=identifier(raw.value(kind=="episode"?"grandparentRatingKey":"parentRatingKey"));
    if (parent.isEmpty()) parent=identifier(raw.value("parentRatingKey"));
    QJsonArray versions; const auto mediaList=raw.value("Media").toArray();
    for(qsizetype mediaIndex=0;mediaIndex<mediaList.size();++mediaIndex) {
        const auto media=mediaList.at(mediaIndex).toObject(); const auto parts=media.value("Part").toArray();
        for(qsizetype partIndex=0;partIndex<parts.size();++partIndex) {
            const auto part=parts.at(partIndex).toObject(); const auto key=identifier(part.value("key")); const QUrl keyUrl(key,QUrl::StrictMode);
            if (!key.startsWith("/library/parts/") || !keyUrl.isValid() || !keyUrl.isRelative() || !keyUrl.host().isEmpty() || keyUrl.hasQuery() || keyUrl.hasFragment()) continue;
            const auto decoded=keyUrl.path(QUrl::FullyDecoded);
            bool unsafe=decoded.contains('\\');
            for (const auto &segment:decoded.split('/')) if(segment=="." || segment=="..") unsafe=true;
            if (unsafe) continue;
            QJsonObject version{{"id",key},{"name",media.value("title").toString(raw.value("title").toString()).left(4096)},
                {"container",text(media.value("container"))},{"videoCodec",text(media.value("videoCodec"))},{"audioCodec",text(media.value("audioCodec"))},
                {"runtimeMs",integer(part.value("duration"),integer(media.value("duration"),integer(raw.value("duration"),0)))},
                {"directPlayable",part.value("supportsDirectPlay").toBool(media.value("supportsDirectPlay").toBool(true))},
                {"mediaIndex",mediaIndex},{"partIndex",partIndex}};
            if (version.value("container").toString().isEmpty()) version.insert("container",text(part.value("container")));
            copyNumber(media,"width",version,"width"); copyNumber(media,"height",version,"height"); copyNumber(media,"bitrate",version,"bitrateKbps");
            QJsonArray subtitles;
            for(const auto &value:part.value("Stream").toArray()) {
                const auto stream=value.toObject();
                if (integer(stream.value("streamType"))==3 || stream.value("type")=="subtitle") subtitles.append(subtitle(stream,true));
            }
            if (subtitles.size()>256) return {}; version.insert("subtitles",subtitles); versions.append(version);
            if (versions.size()>256) return {};
        }
    }
    QJsonObject progress{{"positionMs",integer(raw.value("viewOffset"),0)},{"durationMs",integer(raw.value("duration"),0)},
        {"played",integer(raw.value("viewCount"),0)>0},{"updatedAt",epoch(raw.value("lastViewedAt"))}};
    QJsonObject out{{"id",id},{"libraryId",library.value("id")},{"libraryName",text(library.value("name"))},{"parentId",parent},
        {"kind",kind},{"title",raw.value("title").toString("Untitled").left(4096)},{"identity",ids},{"versions",versions},{"progress",progress},
        {"addedAt",epoch(raw.value("addedAt"))},{"updatedAt",epoch(raw.value("updatedAt"))}};
    copyNumber(raw,"year",out,"year"); return out;
}
QJsonArray inheritSeriesIdentity(QJsonArray items) {
    QHash<QString,QJsonObject> parents;
    for(const auto &entry:items) { const auto item=entry.toObject(); parents.insert(item.value("id").toString(),item); }
    for(qsizetype index=0;index<items.size();++index) {
        auto item=items.at(index).toObject(); if(item.value("kind")!="episode") continue;
        const auto original=item.value("identity").toObject(); QJsonObject inherited;
        auto parentId=item.value("parentId").toString(); QSet<QString> seen;
        while(!parentId.isEmpty() && !seen.contains(parentId) && seen.size()<16) {
            seen.insert(parentId); const auto parent=parents.value(parentId);
            if(parent.value("kind")=="series") { inherited=parent.value("identity").toObject(); item.insert("parentId",parentId); break; }
            if(parent.value("kind")!="season") break;
            parentId=parent.value("parentId").toString();
        }
        inherited.remove("season"); inherited.remove("episode");
        for(auto key:{"season","episode"}) if(original.contains(key)) inherited.insert(key,original.value(key));
        item.insert("identity",inherited); items.replace(index,item);
    }
    return items;
}
bool identitiesMatch(const QJsonObject &a,const QJsonObject &b) {
    const auto left=identity(a), right=identity(b);
    if(left.isEmpty() || right.isEmpty() || left.value("type")!=right.value("type")) return false;
    const bool episode=left.contains("season") || left.contains("episode") || right.contains("season") || right.contains("episode");
    if(episode) for(auto key:{"season","episode"})
        if(!left.contains(key) || !right.contains(key) || left.value(key)!=right.value(key)) return false;
    const auto l=left.value("imdbId").toString(),r=right.value("imdbId").toString();
    if(!l.isEmpty() && !r.isEmpty()) return l==r;
    for(auto key:{"tmdbId","tvdbId"}) if(left.contains(key) && right.contains(key) && left.value(key)==right.value(key)) return true;
    return false;
}
QString canonicalVideoId(const QJsonObject &item) {
    if(item.value("kind")=="season") return {};
    const auto ids=identity(item); if(ids.isEmpty()) return {};
    QString id=ids.value("imdbId").toString();
    if(id.isEmpty() && ids.contains("tmdbId")) id="tmdb:"+QString(ids.value("type")=="movie"?"movie:":"tv:")+QString::number(ids.value("tmdbId").toInteger());
    if(id.isEmpty() && ids.value("type")=="series" && ids.contains("tvdbId")) id="tvdb:tv:"+QString::number(ids.value("tvdbId").toInteger());
    if(id.isEmpty()) return {};
    if(item.value("kind")=="episode" || ids.contains("season") || ids.contains("episode")) {
        if(!ids.contains("season") || !ids.contains("episode")) return {};
        id+=':'+QString::number(ids.value("season").toInteger())+':'+QString::number(ids.value("episode").toInteger());
    }
    return id;
}
QVariantList matchingSources(QJsonArray items,QVariantMap requested,QJsonObject connection) {
    QVariantList result; const auto connectionId=connection.value("id").toString();
    if(connectionId.isEmpty() || !connection.value("enabled").toBool(true)) return result;
    const auto query=identity(QJsonObject::fromVariantMap(requested)); if(query.isEmpty()) return result;
    const bool episode=query.contains("season") && query.contains("episode");
    const auto sourceName=QStringLiteral("Your server · ")+connection.value("name").toString();
    for(const auto &value:inheritSeriesIdentity(items)) {
        const auto item=value.toObject();
        if(item.value("kind")!=(episode?"episode":"movie") || !identitiesMatch(item,query)) continue;
        for(const auto &versionValue:item.value("versions").toArray()) {
            const auto version=versionValue.toObject(); const auto versionId=version.value("id").toString(), itemId=item.value("id").toString();
            if(versionId.isEmpty() || itemId.isEmpty()) continue;
            const auto title=version.value("name").toString().isEmpty()?item.value("title").toString():version.value("name").toString();
            auto canonicalItem=item;
            auto canonicalIdentity=item.value("identity").toObject();
            if(query.contains("imdbId")) canonicalIdentity.insert("imdbId",query.value("imdbId"));
            canonicalItem.insert("identity",canonicalIdentity);
            auto homeIdentity=canonicalIdentity; homeIdentity.insert("type",query.value("type"));
            QVariantMap row{{"infoHash","home-server:"+connectionId+':'+encoded(itemId)+':'+encoded(versionId)},
                {"canonicalId",canonicalVideoId(canonicalItem)},
                {"homeIdentity",homeIdentity.toVariantMap()},
                {"homeServer",true},{"connectionId",connectionId},{"itemId",itemId},{"versionId",versionId},
                {"streamKind","Home server"},{"addonId","home-server:"+connectionId},{"addonName",sourceName},{"sourceName",sourceName},
                {"title",title},{"release",title},{"seeders",-1},{"fileIdx",0}};
            const auto height=integer(version.value("height"),0); if(height>0) row.insert("quality",QString::number(height)+"p");
            result.append(row);
        }
    }
    return result;
}
}

HomeServerRefresh::HomeServerRefresh(HomeServerClient *client,QObject *parent):QObject(parent),m_client(client) {}
HomeServerRefresh::~HomeServerRefresh() { cancel(); }
void HomeServerRefresh::cancel() { m_finished=true; m_callback={}; m_items={}; m_libraries={}; m_buffered.clear(); m_libraryItems.clear(); m_ids.clear(); m_token.clear(); }
void HomeServerRefresh::finish(QString error,bool expired) {
    if(m_finished) return;
    m_finished=true;
    Result result; result.error=error; result.expired=expired; result.serverMajor=m_major;
    if(error.isEmpty()) {
        result.items=HomeServerCatalog::inheritSeriesIdentity(m_items);
        for(const auto &value:m_libraries) {
            auto library=value.toObject();
            library.insert("itemIds",m_libraryItems.value(library.value("id").toString()));
            result.libraries.append(library);
        }
    }
    m_items={}; m_libraries={}; m_buffered.clear(); m_libraryItems.clear(); m_ids.clear(); m_token.clear();
    auto callback=std::move(m_callback); if(callback) callback(std::move(result));
}
void HomeServerRefresh::start(QJsonObject connection,QString token,Callback callback) {
    if(m_started) { QTimer::singleShot(0,this,[callback=std::move(callback)] { if(callback) callback({{},"Refresh job already started"}); }); return; }
    m_started=true; m_connection=std::move(connection); m_token=std::move(token); m_callback=std::move(callback);
    const auto provider=m_connection.value("provider").toString();
    if(!m_client || m_token.isEmpty() || !QStringList{"jellyfin","emby","plex"}.contains(provider) ||
       (provider!="plex" && m_connection.value("userId").toString().isEmpty())) {
        QTimer::singleShot(0,this,[this]{finish("Invalid home-server connection");}); return;
    }
    if(provider=="jellyfin") request("/System/Info/Public",true,[this](QJsonObject body) {
        const auto match=QRegularExpression("^([0-9]+)(?:\\.|$)").match(body.value("Version").toString());
        bool valid=false; m_major=match.captured(1).toInt(&valid);
        if(!match.hasMatch() || !valid || m_major<10) { finish("Unsupported or missing Jellyfin server version"); return; }
        libraries();
    }); else libraries();
}
void HomeServerRefresh::request(const QString &path,bool publicInfo,std::function<void(QJsonObject)> callback) {
    if(m_finished) return;
    QPointer<HomeServerRefresh> guard(this);
    m_client->request(m_connection.value("provider").toString(),QUrl(m_connection.value("origin").toString()),path,"GET",publicInfo?QString():m_token,{},
        [guard,callback=std::move(callback)](HomeServerClient::Response response) {
            if(!guard || guard->m_finished) return;
            if(!response.ok()) { guard->finish(response.error.isEmpty()?QString("Server refresh failed"):response.error,response.expired()); return; }
            if(!response.body.isObject()) { guard->finish("Invalid catalog response"); return; }
            callback(response.body.toObject());
        });
}
void HomeServerRefresh::libraries() {
    const auto provider=m_connection.value("provider").toString(); const auto user=encoded(m_connection.value("userId").toString());
    const QString path=provider=="plex"?QString("/library/sections"):(m_major>=12?"/UserViews?userId="+user:"/Users/"+user+"/Views");
    request(path,false,[this,provider](QJsonObject body) {
        const auto container=provider=="plex"?body.value("MediaContainer").toObject():body;
        auto raw=container.value(provider=="plex"?"Directory":"Items");
        if(provider=="plex" && raw.isUndefined() && integer(container.value("size"))==0 &&
           (!container.contains("totalSize") || integer(container.value("totalSize"))==0)) raw=QJsonArray{};
        if(!raw.isArray() || raw.toArray().size()>MaxItems) { finish("Invalid server library response"); return; }
        QSet<QString> ids;
        for(const auto &entry:raw.toArray()) {
            if(!entry.isObject()) { finish("Invalid server library response"); return; }
            const auto object=entry.toObject(); const auto type=object.value(provider=="plex"?"type":"CollectionType").toString();
            const bool movie=type==(provider=="plex"?"movie":"movies"), show=type==(provider=="plex"?"show":"tvshows");
            const auto collectionType=object.value("CollectionType");
            const bool mixed=provider!="plex" &&
                ((collectionType.isString() && (type.isEmpty() || type=="mixed")) ||
                 (collectionType.isUndefined() && object.value("Type")=="CollectionFolder"));
            if(!movie && !show && !mixed) continue;
            const auto id=identifier(object.value(provider=="plex"?"key":"Id"));
            const auto name=object.value(provider=="plex"?"title":"Name");
            if(id.isEmpty() || ids.contains(id) || (mixed && (!name.isString() || name.toString().trimmed().isEmpty()))) { finish("Invalid or repeated library identifier"); return; }
            const QJsonObject library{{"id",id},{"name",text(name)},{"kind",movie?"movie":show?"series":"mixed"}};
            m_snapshotBytes+=QJsonDocument(library).toJson(QJsonDocument::Compact).size();
            if(m_snapshotBytes>64*1024*1024) { finish("Server catalog snapshot size limit exceeded"); return; }
            ids.insert(id); m_libraries.append(library);
        }
        if(m_libraries.isEmpty()) finish(); else page();
    });
}
void HomeServerRefresh::page() {
    if(m_finished) return;
    if(++m_pages>MaxPages) { finish("Server catalog page limit exceeded"); return; }
    const auto library=m_libraries.at(m_library).toObject(); const auto provider=m_connection.value("provider").toString();
    QUrlQuery query; QString path;
    if(provider=="plex") {
        path="/library/sections/"+encoded(library.value("id").toString())+"/all";
        query.addQueryItem("includeGuids","1"); query.addQueryItem("includeMedia","1"); query.addQueryItem("includeExternalMedia","1");
        const int type=library.value("kind")=="series"?m_stage+2:1;
        query.addQueryItem("type",QString::number(type)); query.addQueryItem("X-Plex-Container-Start",QString::number(m_start)); query.addQueryItem("X-Plex-Container-Size",QString::number(PageSize));
    } else {
        const bool modern=m_major>=12; const auto user=m_connection.value("userId").toString();
        path=modern?QString("/Items"):"/Users/"+encoded(user)+"/Items";
        if(modern) query.addQueryItem("userId",user);
        auto add=[&](QString key,QString value) { if(modern) key[0]=key.at(0).toLower(); query.addQueryItem(key,value); };
        add("ParentId",library.value("id").toString()); add("Recursive","true"); add("IncludeItemTypes","Movie,Series,Season,Episode"); add("CollapseBoxSetItems","false");
        add("Fields","ProviderIds,MediaSources,RunTimeTicks,UserData,SeriesId,ParentId,IndexNumber,ParentIndexNumber,ProductionYear,ImageTags,DateCreated,DateLastSaved");
        add("StartIndex",QString::number(m_start)); add("Limit",QString::number(PageSize)); add("EnableTotalRecordCount","true"); add("SortBy","SortName");
    }
    request(path+'?'+query.toString(QUrl::FullyEncoded),false,[this](QJsonObject body){consumePage(body);});
}
void HomeServerRefresh::consumePage(const QJsonObject &body) {
    const auto provider=m_connection.value("provider").toString(); const bool plex=provider=="plex";
    const auto container=plex?body.value("MediaContainer").toObject():body;
    auto rawValue=container.value(plex?"Metadata":"Items");
    if(plex && rawValue.isUndefined() && integer(container.value("size"))==0 &&
       (!container.contains("totalSize") || integer(container.value("totalSize"))==0)) rawValue=QJsonArray{};
    if(!rawValue.isArray()) { finish("Invalid catalog page"); return; }
    const auto raw=rawValue.toArray();
    if(raw.size()>PageSize) { finish("Server catalog page exceeds requested size"); return; }
    const auto totalKey=plex?"totalSize":"TotalRecordCount", offsetKey=plex?"offset":"StartIndex";
    if(container.contains(offsetKey) && integer(container.value(offsetKey))!=m_start) { finish("Unexpected server catalog offset"); return; }
    if(plex && container.contains("size") && integer(container.value("size"))!=raw.size()) { finish("Invalid Plex page size"); return; }
    qint64 total=integer(container.value(totalKey));
    if(total<0 && plex && !container.contains("totalSize") && !container.contains("offset") && m_start==0 && raw.size()<PageSize && integer(container.value("size"))==raw.size()) total=raw.size();
    if(total<0 || total>MaxItems || total<m_start+raw.size() || (m_total>=0 && total!=m_total)) { finish("Missing or inconsistent catalog total"); return; }
    m_total=static_cast<int>(total);
    if(raw.isEmpty() && m_start<m_total) { finish("Incomplete server catalog page"); return; }
    const auto library=m_libraries.at(m_library).toObject();
    for(const auto &entry:raw) {
        if(!entry.isObject()) { finish("Invalid catalog item"); return; }
        const auto item=plex?HomeServerCatalog::normalizePlexItem(entry.toObject(),library):HomeServerCatalog::normalizeJellyfinItem(entry.toObject(),library);
        const auto id=item.value("id").toString();
        if(id.isEmpty() || m_ids.contains(id)) { finish("Invalid or repeated catalog item identifier"); return; }
        m_ids.insert(id);
        m_snapshotBytes+=id.toUtf8().size()+3;
        if(m_snapshotBytes>64*1024*1024) { finish("Server catalog snapshot size limit exceeded"); return; }
        m_libraryItems[library.value("id").toString()].append(id);
        if(m_buffered.contains(id)) {
            const auto previous=m_buffered.value(id);
            auto versions=[](const QJsonObject &object) { QJsonArray ids; for(const auto &v:object.value("versions").toArray()) ids.append(v.toObject().value("id")); return ids; };
            if(previous.value("libraryId")==item.value("libraryId") || previous.value("kind")!=item.value("kind") ||
               previous.value("identity")!=item.value("identity") || previous.value("parentId")!=item.value("parentId") || versions(previous)!=versions(item)) {
                finish("Contradictory repeated catalog item"); return;
            }
            continue;
        }
        const auto bytes=QJsonDocument(item).toJson(QJsonDocument::Compact).size();
        if(bytes>64*1024*1024-m_snapshotBytes) { finish("Server catalog snapshot size limit exceeded"); return; }
        m_snapshotBytes+=bytes; m_buffered.insert(id,item); m_items.append(item);
        if(m_items.size()>MaxItems) { finish("Server catalog item limit exceeded"); return; }
    }
    m_start+=static_cast<int>(raw.size());
    if(m_start<m_total) { page(); return; }
    m_start=0; m_total=-1; m_ids.clear();
    if(plex && library.value("kind")=="series" && ++m_stage<3) { page(); return; }
    m_stage=0; ++m_library;
    if(m_library<m_libraries.size()) page(); else finish();
}
