#include "VinylCatalogueSource.h"
#include "CollectionStore.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QSet>
#include <QMap>
#include <algorithm>
#include <QDateTime>
#include <QNetworkReply>
#include <QUrl>
#include <QImageReader>
#include <QImage>
#include <QBuffer>
using namespace vinyl::artist;

namespace {
QVariantMap musicIntent(const QVariantMap &album,const QVariantMap &track,int number){
    const auto artists=track.value("artists",album.value("artists")).toStringList();
    const auto type=album.value("type").toString();
    return {{"artist",artists.value(0)},{"album",type=="Song"||type=="Playlist"?QVariant(QString()):album.value("title")},{"title",track.value("title")},
        {"trackNumber",track.value("trackNumber",number)},{"discNumber",track.value("discNumber",0)},
        {"durationMs",qRound64(track.value("seconds").toDouble()*1000)},{"releaseYear",album.value("year").toInt()}};
}
bool validArtistContinuation(const QVariant &value,bool requireParams){
    if(value.metaType().id()!=QMetaType::QVariantMap)return false;
    const auto handle=value.toMap();const auto browseId=handle.value("browseId");
    for(auto it=handle.cbegin();it!=handle.cend();++it)if(it.key()!="browseId"&&it.key()!="params")return false;
    if(browseId.metaType().id()!=QMetaType::QString
       ||!QRegularExpression("^[A-Za-z0-9_-]{1,160}$").match(browseId.toString()).hasMatch())return false;
    if(!handle.contains("params")||handle.value("params").isNull())return !requireParams;
    const auto params=handle.value("params");
    static const QRegularExpression token(QStringLiteral("\\A(?:[A-Za-z0-9_+/=-]|%[0-9A-Fa-f]{2})+\\z"));
    return params.metaType().id()==QMetaType::QString&&params.toString().size()<=4096
        &&token.match(params.toString()).hasMatch();
}
QString trustedArtistImage(const QVariant &value){
    if(value.metaType().id()==QMetaType::QVariantList){
        for(const auto &item:value.toList()){const auto result=trustedArtistImage(item);if(!result.isEmpty())return result;}
        return {};
    }
    if(value.metaType().id()==QMetaType::QVariantMap){
        const auto map=value.toMap();
        for(const auto &key:{QStringLiteral("url"),QStringLiteral("cover"),QStringLiteral("thumbnail"),QStringLiteral("src")}){
            const auto result=trustedArtistImage(map.value(key));if(!result.isEmpty())return result;
        }
        return {};
    }
    if(value.metaType().id()!=QMetaType::QString)return {};
    const QUrl url(value.toString(),QUrl::StrictMode);const auto host=url.host().toLower();
    const bool imageHost=host=="i.ytimg.com"||host.endsWith(".ytimg.com")
        ||host=="googleusercontent.com"||host.endsWith(".googleusercontent.com")
        ||host=="ggpht.com"||host.endsWith(".ggpht.com");
    if(url.scheme()=="https"&&imageHost&&url.userInfo().isEmpty()&&(url.port(-1)==-1||url.port(-1)==443))return url.toString();
    return {};
}
void appendArtistError(QString &target,const QString &message){
    const auto clean=message.trimmed();if(clean.isEmpty()||target.contains(clean))return;
    if(!target.isEmpty())target+='\n';target+=clean;
}
QVariantList mergeArtistRows(QVariantList existing,const QVariantList &incoming){
    QSet<QString> seen;
    for(const auto &entry:existing){const auto map=entry.toMap();auto key=map.value("browseId").toString();if(key.isEmpty())key=map.value("videoId",map.value("id")).toString();if(!key.isEmpty())seen.insert(key);}
    for(const auto &entry:incoming){const auto map=entry.toMap();auto key=map.value("browseId").toString();if(key.isEmpty())key=map.value("videoId",map.value("id")).toString();
        if(!key.isEmpty()&&!seen.contains(key)){seen.insert(key);existing.append(entry);}}
    return existing;
}
}

VinylCatalogueSource::VinylCatalogueSource(VinylLocalSource *local,VinylProvider *provider,CollectionStore *collection,QObject *parent)
 :QObject(parent),m_local(local),m_provider(provider),m_collection(collection),m_bridge(*this,*this,*this) {
    connect(local,&VinylLocalSource::completed,this,[this](auto lane,int id,auto data,auto error){accept(lane,id,data,error,true);});
    connect(provider,&VinylProvider::completed,this,[this](auto lane,int id,auto data,auto error){
        if(m_artistRequests.value(lane,-1)==id){m_artistRequests.remove(lane);acceptArtist(lane,data,error);return;}
        accept(lane,id,data,error,false);
    });
    connect(local,&VinylLocalSource::importCompleted,this,&VinylCatalogueSource::importCompleted);
    connect(local,&VinylLocalSource::importProgress,this,&VinylCatalogueSource::importProgress);
    connect(local,&VinylLocalSource::profileLeaving,this,[this]{cancelAll();m_albums.clear();m_videos.clear();m_remoteTracks.clear();emit profileLeaving();});
    connect(local,&VinylLocalSource::profileEntered,this,&VinylCatalogueSource::profileEntered);
    connect(local,&VinylLocalSource::checkpointRequested,this,&VinylCatalogueSource::checkpointRequested);
    connect(local,&VinylLocalSource::shuttingDown,this,[this]{cancelAll();emit shuttingDown();});
}
void VinylCatalogueSource::setCollectionStore(QObject *store){m_collection=qobject_cast<CollectionStore *>(store);}
void VinylCatalogueSource::setTorrentProvider(QObject *provider){
    if(m_torrent==provider)return;
    cancel("play");if(m_torrent)disconnect(m_torrent,nullptr,this,nullptr);
    m_torrent=provider;
    if(provider)connect(provider,SIGNAL(completed(int,QVariantMap,QString)),this,SLOT(torrentCompleted(int,QVariantMap,QString)),Qt::QueuedConnection);
}
void VinylCatalogueSource::setAlbums(const QVariantList &entries){
    m_local->setAlbums(entries);
    for(const auto &entry:entries){auto album=entry.toMap().value("payload").toMap().value("album").toMap();if(album.isEmpty())album=entry.toMap();if(!album.isEmpty())remember(album);}
}
QVariantMap VinylCatalogueSource::remember(QVariantMap album){
    const auto id=album.value("id").toString();if(id.isEmpty())return album;
    auto prior=m_albums.value(id);
    if(prior.isEmpty())prior=m_albums.value(album.value("browseId").toString());
    // Search summaries must not erase the release context of queued tracks.
    if(album.value("tracks").toList().isEmpty()&&!prior.value("tracks").toList().isEmpty()){
        auto complete=prior;
        for(auto it=album.cbegin();it!=album.cend();++it)if(it.key()!="tracks")complete.insert(it.key(),it.value());
        album=complete;
    }
    if(!album.contains("providerEditions")&&prior.contains("providerEditions"))album["providerEditions"]=prior.value("providerEditions");
    // A paged playlist preview must not erase the full source of queued rows.
    if(album.value("type")=="Playlist"&&!album.value("complete").toBool()
        &&prior.value("tracks").toList().size()>album.value("tracks").toList().size()){
        album["tracks"]=prior.value("tracks");album["complete"]=prior.value("complete");
        album["nextCursor"]=prior.value("nextCursor");album["totalTracks"]=prior.value("totalTracks");
        album["omittedTracks"]=prior.value("omittedTracks");
    }
    m_albums.insert(id,album);
    if(!album.value("browseId").toString().isEmpty())m_albums.insert(album.value("browseId").toString(),album);
    int trackNumber=0;
    for(const auto &item:album.value("tracks").toList()){
        ++trackNumber;
        const auto track=item.toMap();const auto video=track.value("videoId").toString();
        if(QRegularExpression("^[A-Za-z0-9_-]{11}$").match(video).hasMatch()){
            const auto key=track.value("id").toString();m_videos.insert(key,video);
            m_remoteTracks.insert(key,musicIntent(album,track,trackNumber));
        }
    }
    return album;
}
int VinylCatalogueSource::request(const QString &lane,const QString &operation,const QString &value){return request(lane,operation,value,{});}
int VinylCatalogueSource::request(const QString &lane,const QString &operation,const QString &value,const QVariantMap &options){
    if(operation=="songCandidates"&&lane!="home-song-0"&&lane!="home-song-1")return -1;
    const bool resolving=operation=="resolve"||operation=="resolveFallback";
    const auto selectedSource=options.value("source");
    if(resolving&&options.contains("source")
        &&(selectedSource.metaType().id()!=QMetaType::QString
            ||(selectedSource.toString()!="youtube"&&selectedSource.toString()!="torrent")))return -1;
    cancel(lane);const int id=++m_serial;
    bool local=operation=="search"||value.startsWith("vault:")||value.startsWith("vinyl:local-");
    QString remoteValue=value,remoteOperation=operation;
    if(operation=="album"&&m_albums.contains(value)){
        const auto album=m_albums.value(value);
        if(album.value("origin")=="local")local=true;
        else if(album.value("type")=="Playlist") {remoteValue=album.value("playlistId").toString();remoteOperation=lane=="browse"?"playlistPage":"playlist";}
        else remoteValue=album.value("browseId",value).toString();
    }
    // Recommendation destinations use the same provider namespace as search.
    // A summary need not have been opened (and registered) before navigation.
    if(operation=="album"&&!local&&remoteValue.startsWith("ytm:album:"))remoteValue=remoteValue.mid(10);
    if((operation=="resolve"||operation=="resolveFallback"||operation=="relatedVideo")&&!local){remoteValue=m_videos.value(value);}
    if(operation=="resolveVideo") {
        local=false;remoteValue=options.value("videoId").toString();
        if(!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(remoteValue).hasMatch())return -1;
    }
    if(operation=="resolve"&&lane=="play"&&!local&&selectedSource.toString()=="torrent"&&m_torrent&&m_remoteTracks.contains(value)){
        auto intent=m_remoteTracks.value(value);
        const auto browseId=options.value("browseId").toString();
        if(!browseId.isEmpty()){
            const auto album=m_albums.value(browseId);bool found=false;int number=0;
            for(const auto &entry:album.value("tracks").toList()){
                ++number;const auto track=entry.toMap();
                if(track.value("id").toString()==value){intent=musicIntent(album,track,number);found=true;break;}
            }
            if(!found)return -1;
        }
        int transport=-1;
        if(!QMetaObject::invokeMethod(m_torrent,"resolve",Qt::DirectConnection,Q_RETURN_ARG(int,transport),Q_ARG(QVariantMap,intent))||transport<0)return -1;
        m_pending.insert(lane,{id,transport,false,operation,value,true,{}});
        // Resolve, but do not play, YouTube while the preferred torrent gets its
        // short head start. Playback URLs remain ephemeral and request-scoped.
        const int fallback=m_provider->request("playFallback","resolve",remoteValue);
        auto &pending=m_pending[lane];pending.fallbackTransportId=fallback;
        if(fallback<0){pending.fallbackReady=true;pending.fallbackError="The fallback source is unavailable.";}
        return id;
    }
    auto localOptions=options;
    if(local&&resolving)localOptions.remove("source");
    const int transport=local?m_local->request(lane,operation,value,localOptions):m_provider->requestPayload(lane,operation=="resolveFallback"?"resolve":remoteOperation,remoteValue,options);
    if(transport<0)return -1;
    m_pending.insert(lane,{id,transport,local,operation,value});m_pending[lane].options=options;return id;
}
void VinylCatalogueSource::accept(const QString &lane,int id,const QVariant &data,const QString &error,bool local){
    if(lane=="playFallback"&&!local){
        auto it=m_pending.find("play");
        if(it==m_pending.end()||it->fallbackTransportId!=id)return;
        if(it->torrent){it->fallbackReady=true;it->fallbackData=data;it->fallbackError=error;return;}
        accept("play",id,data,error,false);return;
    }
    if(!m_pending.contains(lane))return;const auto pending=m_pending.value(lane);
    if(pending.torrent||pending.local!=local||pending.transportId!=id)return;m_pending.remove(lane);
    QVariant result=data;
    if(error.isEmpty()&&!local&&VinylProvider::homeOperation(pending.operation)){
        QVariantMap normalized;
        if(!VinylProvider::normalizeHome(pending.operation,pending.value,pending.options,data,&normalized)){
            emit completed(lane,pending.id,{},"The recommendation response is invalid.");return;
        }
        if(pending.operation=="recordingMatch"&&normalized.value("status")=="verified"){
            auto track=normalized.value("items").toList().value(0).toMap();QStringList names,ids;
            for(const auto &entry:track.value("artistRefs").toList()){const auto artist=entry.toMap();names.append(artist.value("name").toString());ids.append("ytm:artist:"+artist.value("providerId").toString());}
            track["artists"]=names;track["artistIds"]=ids;
            remember({{"id",track.value("id")},{"browseId",track.value("id")},{"type","Song"},{"title",track.value("title")},
                {"artists",names},{"artistIds",ids},{"origin","youtube-music"},{"cover",track.value("cover")},{"tracks",QVariantList{track}}});
        }
        if(pending.operation=="songCandidates")for(const auto &entry:normalized.value("items").toList())remember(entry.toMap().value("record").toMap());
        emit completed(lane,pending.id,normalized,{});return;
    }
    if(error.isEmpty()){
        if(pending.operation=="album")result=remember(data.toMap());
        else if(pending.operation=="catalogueSearch"){
            auto map=data.toMap();QVariantList albums;
            for(const auto &item:map.value("albums").toList())albums.append(remember(item.toMap()));map["albums"]=albums;result=map;
        }else if(pending.operation=="resolveCatalogueItem"){
            auto map=data.toMap();const auto item=pending.options.value("item").toMap();
            const auto kind=map.value("kind").toString();
            const bool validEnvelope=map.value("version").toInt()==1&&map.value("id").toString()==pending.value
                &&kind==item.value("kind").toString()&&map.value("matched").metaType().id()==QMetaType::Bool;
            if(!validEnvelope){emit completed(lane,pending.id,{},"The catalogue match response is invalid.");return;}
            if(map.value("matched").toBool()){
                if(kind=="artists"){
                    if(map.value("artistId").toString().isEmpty()){
                        emit completed(lane,pending.id,{},"The catalogue match response is invalid.");return;
                    }
                }else{
                    auto recordValue=map.value("record");auto record=recordValue.toMap();
                    if(recordValue.metaType().id()!=QMetaType::QVariantMap||record.value("id").toString().isEmpty()
                        ||record.value("id").toString()!=map.value("albumId").toString()
                        ||record.value("browseId").toString().isEmpty()){
                        emit completed(lane,pending.id,{},"The catalogue match response is invalid.");return;
                    }
                    map["record"]=remember(record);
                }
            }
            result=map;
        }else if(pending.operation=="discover"||pending.operation=="cataloguePage"||pending.operation=="browseCatalogue"){
            auto map=data.toMap();QVariantList records;
            for(const auto &record:map.value("records").toList())records.append(remember(record.toMap()));
            map["records"]=records;result=map;
        }else if(pending.operation=="playlistPage"){
            auto page=data.toMap();auto album=m_albums.value("ytm:playlist:"+pending.value);
            auto tracks=album.value("tracks").toList();
            if(!album.isEmpty()&&page.value("offset").toInt()==tracks.size()){
                tracks.append(page.value("tracks").toList());album["tracks"]=tracks;
                album["complete"]=page.value("complete");album["nextCursor"]=page.value("nextCursor");
                album["totalTracks"]=page.value("totalTracks");album["omittedTracks"]=page.value("omittedTracks");remember(album);
            }
        }else if((pending.operation=="resolve"||pending.operation=="resolveFallback"||pending.operation=="resolveVideo"||pending.operation=="relatedVideo")&&!local){
            auto map=data.toMap();map["trackId"]=pending.value;map["provider"]="youtube";
            map["fallbackReason"]=pending.fallbackReason;result=map;
        }
    }
    emit completed(lane,pending.id,result,error);
}
void VinylCatalogueSource::torrentCompleted(int requestId,const QVariantMap &data,const QString &error){
    const auto it=m_pending.find("play");
    if(sender()!=m_torrent||it==m_pending.end()||!it->torrent||it->transportId!=requestId)return;
    if(error.isEmpty()&&data.value("provider")=="torrent"&&!data.value("url").toString().isEmpty()){
        const auto pending=*it;m_pending.erase(it);auto result=data;result["trackId"]=pending.value;
        m_provider->cancel("playFallback");
        emit completed("play",pending.id,result,{});return;
    }
    it->torrent=false;it->fallbackReason=error.isEmpty()?QStringLiteral("The torrent source returned no playable file."):error;
    it->transportId=it->fallbackTransportId;
    if(it->transportId<0){const auto pending=*it;m_pending.erase(it);emit completed("play",pending.id,{},"The fallback source is unavailable.");}
    else if(it->fallbackReady){const auto pending=*it;accept("play",pending.transportId,pending.fallbackData,pending.fallbackError,false);}
}
void VinylCatalogueSource::cancel(const QString &lane){
    m_pending.remove(lane);if(lane=="play"&&m_torrent)QMetaObject::invokeMethod(m_torrent,"cancel",Qt::DirectConnection);
    if(lane=="play")m_provider->cancel("playFallback");
    m_local->cancel(lane);m_provider->cancel(lane);
}
void VinylCatalogueSource::cancelAll(){for(const auto &lane:m_pending.keys())cancel(lane);m_local->cancelImport();leaveArtist();m_provider->cancelAll();}
void VinylCatalogueSource::leaveArtist(){
    ++m_artworkGeneration;
    const auto replies=m_artworkReplies;m_artworkReplies.clear();
    for(auto *reply:replies){reply->disconnect(this);reply->abort();reply->deleteLater();}
    m_artwork.clear();m_artworkRequest={};
    for(const auto &lane:m_artistRequests.keys())m_provider->cancel(lane);m_artistRequests.clear();m_bridge.leaveArtist();
    m_artist.clear();m_artistAlbums.clear();m_artistSingles.clear();m_artistMedia.clear();m_identity.clear();m_artistError.clear();m_cached=false;m_channel.clear();emit artistChanged();
}
void VinylCatalogueSource::retryArtist(){openArtist(m_artistIntent);}
void VinylCatalogueSource::artistRequest(const QString &lane,const QString &operation,const QString &value,const QVariantMap &payload){
    const auto id=m_provider->requestPayload(lane,operation,value,payload);
    if(id<0){
        if(lane=="artist-identity")m_identity={{"unavailable",true}};
        else if(lane!="artist-artwork")appendArtistError(m_artistError,"This catalogue request could not start.");
    }else m_artistRequests.insert(lane,id);
}
void VinylCatalogueSource::openArtist(const QVariantMap &intent){
    leaveArtist();m_artistIntent=intent;
    auto id=intent.value("channelId",intent.value("id")).toString();if(id.startsWith("ytm:artist:"))id=id.mid(11);
    auto seededCover=trustedArtistImage(intent.value("cover"));
    if(seededCover.isEmpty())seededCover=trustedArtistImage(intent.value("thumbnail"));
    if(seededCover.isEmpty())seededCover=trustedArtistImage(intent.value("image"));
    m_artist={{"name",intent.value("name")},{"id",intent.value("id")}};
    if(!seededCover.isEmpty())m_artist["cover"]=seededCover;
    if(QRegularExpression("^UC[A-Za-z0-9_-]{22}$").match(id).hasMatch()){
        m_channel=id;m_artist["id"]="ytm:artist:"+m_channel;m_artist["channelId"]=m_channel;
        artistRequest("artist","artist",id,{{"overview",true}});
    }else if(!intent.value("name").toString().trimmed().isEmpty())artistRequest("artist","catalogueSearch",intent.value("name").toString());
    emit artistChanged();
}
void VinylCatalogueSource::acceptArtist(const QString &lane,const QVariant &data,const QString &error){
    if(!error.isEmpty()){
        if(lane=="artist-identity")m_identity={{"unavailable",true}};
        else if(lane!="artist-artwork")appendArtistError(m_artistError,error);
        if(lane=="artist-albums"||lane=="artist-singles"||lane=="artist-videos")requestIdentity();
        emit artistChanged();return;
    }
    auto map=data.toMap();
    // The artist UI's cached status reports that a stale fallback was used.
    if(map.value("_stale").toBool())m_cached=true;
    if(lane=="artist-artwork"){loadArtwork(map);return;}
    if(lane=="artist"&&m_channel.isEmpty()){
        QVariantList exact;for(const auto &row:map.value("artists").toList())if(row.toMap().value("name").toString().compare(m_artistIntent.value("name").toString(),Qt::CaseInsensitive)==0)exact.append(row);
        if(exact.size()==1){openArtist(exact.first().toMap());return;}
        m_artistError="Choose an artist from catalogue search to disambiguate this name.";emit artistChanged();return;
    }
    if(lane=="artist"){
        if(map.value("requestedChannelId").toString()!=m_channel){m_artistError="The catalogue returned a different artist.";emit artistChanged();return;}
        const auto seededCover=m_artist.value("cover").toString();
        auto responseCover=trustedArtistImage(map.value("cover"));
        if(responseCover.isEmpty())responseCover=trustedArtistImage(map.value("images"));
        if(responseCover.isEmpty())responseCover=trustedArtistImage(map.value("thumbnails"));
        m_artist=map;m_artist["id"]="ytm:artist:"+m_channel;m_artist["channelId"]=m_channel;
        const auto cover=!responseCover.isEmpty()?responseCover:seededCover;
        if(!cover.isEmpty())m_artist["cover"]=cover;else m_artist.remove("cover");
        auto songs=m_artist.value("songs").toList();
        for(auto &entry:songs){auto song=entry.toMap();auto record=song.value("record").toMap();
            if(record.value("type")=="Song"&&!record.value("id").toString().isEmpty()&&!record.value("tracks").toList().isEmpty())song["record"]=remember(record);
            entry=song;
        }
        m_artist["songs"]=songs;

        const auto continuations=map.value("continuations").toMap();
        auto releaseCards=[this](const QVariant &raw,const QString &kind,bool *valid){
            *valid=false;if(raw.metaType().id()!=QMetaType::QVariantMap)return QVariantList{};
            const auto shelf=raw.toMap();
            if(shelf.value("requestedChannelId").toString()!=m_channel||shelf.value("kind").toString()!=kind
               ||shelf.value("items").metaType().id()!=QMetaType::QVariantList)return QVariantList{};
            *valid=true;QVariantList items;
            for(const auto &entry:shelf.value("items").toList()){
                auto album=entry.toMap();if(album.value("id").toString().isEmpty()||album.value("browseId").toString().isEmpty())continue;
                album["origin"]="youtube-music";album["artists"]=QStringList{m_artist.value("name").toString()};
                album["artistIds"]=QStringList{"ytm:artist:"+m_channel};album["kind"]=kind=="singles"?"single":"album";
                // A card is not an admitted tracklist. Opening it requests the full album.
                album["tracks"]=QVariantList{};items.append(remember(album));
            }
            return items;
        };
        bool albumsValid=false,singlesValid=false,videosValid=false;
        if(map.contains("albumShelf")){m_artistAlbums=releaseCards(map.value("albumShelf"),"albums",&albumsValid);
            if(!albumsValid)appendArtistError(m_artistError,"The album shelf belongs to a different artist or has an invalid shape.");}
        if(map.contains("singleShelf")){m_artistSingles=releaseCards(map.value("singleShelf"),"singles",&singlesValid);
            if(!singlesValid)appendArtistError(m_artistError,"The single shelf belongs to a different artist or has an invalid shape.");}
        if(map.contains("videoShelf")){
            const auto shelf=map.value("videoShelf");
            if(shelf.metaType().id()==QMetaType::QVariantMap&&shelf.toMap().value("requestedChannelId").toString()==m_channel
               &&shelf.toMap().value("items").metaType().id()==QMetaType::QVariantList){
                m_artistMedia=shelf.toMap();m_artistMedia["channelId"]=m_channel;videosValid=true;
            }else appendArtistError(m_artistError,"The video shelf belongs to a different artist or has an invalid shape.");
        }
        auto requestReleaseContinuation=[this,&continuations](const QString &kind,bool shelfValid,bool shelfComplete){
            if(!shelfValid)return;
            const auto handle=continuations.value(kind);
            if(validArtistContinuation(handle,true))artistRequest(kind=="albums"?"artist-albums":"artist-singles","artistReleases",m_channel,
                {{"channelId",m_channel},{"kind",kind},{"continuation",handle}});
            else if(!shelfComplete)
                appendArtistError(m_artistError,kind=="albums"?"The album shelf has no valid continuation.":"The single shelf has no valid continuation.");
        };
        requestReleaseContinuation("albums",albumsValid,map.value("albumShelf").toMap().value("complete").toBool());
        requestReleaseContinuation("singles",singlesValid,map.value("singleShelf").toMap().value("complete").toBool());
        if(videosValid){
            const auto handle=continuations.value("videos");
            if(validArtistContinuation(handle,false))artistRequest("artist-videos","artistVideos",m_channel,{{"channelId",m_channel},{"continuation",handle}});
            else if(!m_artistMedia.value("complete").toBool())appendArtistError(m_artistError,"The video shelf has no valid continuation.");
        }
    }else if(lane=="artist-albums"||lane=="artist-singles"){
        const auto expectedKind=lane=="artist-singles"?QStringLiteral("singles"):QStringLiteral("albums");
        if(map.value("requestedChannelId").toString()!=m_channel||map.value("kind").toString()!=expectedKind
           ||map.value("items").metaType().id()!=QMetaType::QVariantList){
            appendArtistError(m_artistError,"The release continuation belongs to a different artist or has an invalid shape.");requestIdentity();emit artistChanged();return;
        }
        QVariantList items;
        for(const auto &entry:map.value("items").toList()){
            auto album=entry.toMap();album["origin"]="youtube-music";album["artists"]=QStringList{m_artist.value("name").toString()};
            album["artistIds"]=QStringList{"ytm:artist:"+m_channel};album["kind"]=lane=="artist-singles"?"single":"album";
            // A card is not an admitted tracklist. Opening it requests the full album.
            album["tracks"]=QVariantList{};items.append(remember(album));
        }
        if(lane=="artist-albums")m_artistAlbums=mergeArtistRows(m_artistAlbums,items);else m_artistSingles=mergeArtistRows(m_artistSingles,items);
        if(!map.value("complete").toBool())appendArtistError(m_artistError,"This release continuation is partial. Retry the artist catalogue.");
    }else if(lane=="artist-videos"){
        if(map.value("requestedChannelId").toString()!=m_channel||map.value("items").metaType().id()!=QMetaType::QVariantList){
            appendArtistError(m_artistError,"The video continuation belongs to a different artist or has an invalid shape.");requestIdentity();emit artistChanged();return;
        }
        auto media=m_artistMedia;auto incoming=map.value("items").toList();
        if(incoming.isEmpty())incoming=map.value("videos").toList()+map.value("livePerformances").toList();
        media["items"]=mergeArtistRows(media.value("items").toList(),incoming);
        for(const auto &key:{QStringLiteral("videos"),QStringLiteral("live"),QStringLiteral("livePerformances")})
            media[key]=mergeArtistRows(media.value(key).toList(),map.value(key).toList());
        media["channelId"]=m_channel;media["complete"]=map.value("complete");m_artistMedia=media;
        if(!map.value("complete").toBool())appendArtistError(m_artistError,"The video continuation is partial. Retry the artist catalogue.");
    }else if(lane=="artist-identity"){
        if(map.value("source")!="musicbrainz"||!map.value("complete").toBool()||QUuid(map.value("mbid").toString()).isNull()){
            m_identity={{"unavailable",true}};emit artistChanged();return;
        }
        m_identity=map;
        for(const auto &value:map.value("groups").toList()){
            const auto group=value.toMap();const auto versions=group.value("editions").toList();
            for(const auto &version:versions){const auto browseId=version.toMap().value("browseId").toString();
                if(!m_albums.contains(browseId))continue;auto album=m_albums.value(browseId);
                album["providerEditions"]=versions;album["releaseGroupMbid"]=group.value("mbid");album["mappingMethod"]=group.value("matchedBy");
                const QMap<QString,QString> categories{{"albums","Album"},{"singles","Single"},{"compilations","Compilation"},{"soundtracks","Soundtrack"},{"live","Live album"},{"other","Other"}};
                album["category"]=categories.value(group.value("category").toString(),"Album");
                remember(album);
                for(auto *list:{&m_artistAlbums,&m_artistSingles})for(auto &entry:*list)if(entry.toMap().value("browseId")==browseId)entry=album;
            }
        }
        // Canonically matched provider versions share one browse card. Keep all
        // concrete provider IDs in providerEditions and in the source registry.
        for(auto *list:{&m_artistAlbums,&m_artistSingles}){
            QSet<QString> seen;QVariantList grouped;
            for(const auto &entry:*list){const auto album=entry.toMap();auto group=album.value("releaseGroupMbid").toString();
                const auto key=QUuid(group).isNull()?album.value("browseId").toString():"mb-group:"+group;
                if(!seen.contains(key)){seen.insert(key);grouped.append(entry);}}
            *list=grouped;
        }
        // Only MusicBrainz-returned IDs enter the canonical bridge. Provider UUIDs never do.
        const auto mbid=map.value("mbid").toString();
        if(!QUuid(mbid).isNull()){
            const QString key="ytm:artist:"+m_channel;
            if(m_collection&&m_collection->has("Vinyl",key))m_collection->setVinylArtistSaved({{"id",key},{"name",m_artist.value("name")},{"externalIds",QVariantMap{{"musicbrainz",mbid}}}},true);
            m_bridge.enterArtist("active-profile",mbid);m_bridge.requestArtist(mbid);m_bridge.requestDiscography();
            if(m_artworkNam)artistRequest("artist-artwork","artistArtwork",mbid);
        }
    }
    requestIdentity();emit artistChanged();
}
void VinylCatalogueSource::requestIdentity(){
    if(m_channel.isEmpty()||m_artist.value("name").toString().isEmpty()||!m_identity.isEmpty()||m_artistRequests.contains("artist-identity")
       ||m_artistRequests.contains("artist-albums")||m_artistRequests.contains("artist-singles")||m_artistRequests.contains("artist"))return;
    auto cards=[](const QVariantList &rows){QVariantList result;for(const auto &row:rows){const auto item=row.toMap();
        result.append(QVariantMap{{"browseId",item.value("browseId")},{"title",item.value("title")},{"year",item.value("year").toString()},
            {"type",item.value("type")},{"cover",item.value("cover")},{"explicit",item.value("explicit")}});}return result;};
    artistRequest("artist-identity","artistIdentity",m_artist.value("name").toString(),{{"channelId",m_channel},{"releases",QVariantMap{{"albums",cards(m_artistAlbums)},{"singles",cards(m_artistSingles)}}}});
}
void VinylCatalogueSource::request(const ReadRequest &req,std::function<void(ReadReply)> done){
    ReadReply reply;reply.channel=req.channel;reply.token=req.token;reply.profileScopeId=req.profileScopeId;reply.artistMbid=req.artistMbid;reply.cursor=req.cursor;reply.filter=req.filter;
    reply.source={"musicbrainz",req.artistMbid,m_identity.value("fetchedAt").toString()};reply.state=State::Ready;
    if(req.artistMbid!=m_identity.value("mbid").toString()){reply.state=State::Unavailable;reply.message="Authoritative artist identity is unavailable.";}
    else if(req.channel==Channel::Artist)reply.entity={{"artistMbid",req.artistMbid},{"name",m_artist.value("name")}};
    else if(req.channel==Channel::Discography){
        const auto groups=m_identity.value("releaseGroups").toList();bool valid=req.cursor.isEmpty();int offset=0;
        if(!req.cursor.isEmpty()){offset=req.cursor.toInt(&valid);valid=valid&&offset>0&&offset<groups.size();}
        if(!valid){reply.state=State::Error;reply.errorCode="invalid_page";}
        else{for(int i=offset;i<std::min(offset+100,int(groups.size()));++i){auto group=groups[i].toMap();group["releaseGroupMbid"]=group.value("id");reply.items.append(group);}
            if(offset+100<groups.size())reply.nextCursor=QString::number(offset+100);
            if(reply.items.isEmpty())reply.state=State::Empty;
        }
    }else if(req.channel==Channel::Artwork&&!m_artwork.isEmpty()){
        reply.entity=m_artwork;
        if(m_artwork.value("photos").toList().isEmpty())reply.state=State::Empty;
    }else{reply.state=State::Unavailable;reply.message="No canonical media or artist artwork mapping is available.";}
    QTimer::singleShot(0,this,[done=std::move(done),reply]{done(reply);});
}
SavedReply VinylCatalogueSource::read(const QString &profile,const QString &artist){
    SavedReply result;result.profileScopeId=profile;result.artistMbid=artist;
    const bool mapped=!m_channel.isEmpty()&&m_identity.value("mbid").toString()==artist;
    result.state=m_collection&&mapped?State::Ready:State::Unavailable;result.saved=m_collection&&mapped&&m_collection->has("Vinyl","ytm:artist:"+m_channel);return result;
}
SavedReply VinylCatalogueSource::write(const QString &profile,const QString &artist,bool saved){
    auto result=read(profile,artist);
    if(result.state!=State::Ready||!m_collection->setVinylArtistSaved({{"id","ytm:artist:"+m_channel},{"name",m_artist.value("name")},{"externalIds",QVariantMap{{"musicbrainz",artist}}}},saved)){result.state=State::Error;result.message="Artist could not be saved.";}else result.saved=saved;
    return result;
}
CommandReply VinylCatalogueSource::selectRecording(const ExactSelection &){return {false,"mapping_unavailable","This recording has no authoritative edition occurrence. Open its catalogue album."};}
CommandReply VinylCatalogueSource::selectRelease(const ExactSelection &){return {false,"mapping_unavailable","This edition has no authoritative playback mapping. Open its catalogue album."};}

void VinylCatalogueSource::loadArtwork(const QVariantMap &data){
    using namespace vinyl::artwork;
    const auto mbid=m_identity.value("mbid").toString();
    if(!m_artworkNam||data.value("artistMbid").toString()!=mbid||data.value("source")!="theaudiodb"
       ||data.value("usage")!="development"||data.value("termsUrl")!="https://www.theaudiodb.com/docs_terms_of_use.php"){
        m_artwork.clear();emit artistChanged();return;
    }
    m_artworkRequest={};m_artworkRequest.artistMbid=mbid.toStdString();
    m_artworkRequest.artistName=m_artist.value("name").toString().toStdString();
    m_artworkRequest.nowEpoch=QDateTime::currentSecsSinceEpoch();m_artworkRequest.maxSlides=4;
    m_artworkRequest.scope=Scope::LocalPreview;
    const auto generation=m_artworkGeneration;
    for(const auto &item:data.value("photos").toList().mid(0,4)){
        const auto photo=item.toMap();const QUrl url(photo.value("url").toString());
        if(url.scheme()!="https"||!QStringList{"r2.theaudiodb.com","www.theaudiodb.com"}.contains(url.host())
           ||!url.path().startsWith("/images/media/artist/fanart/")||!url.userInfo().isEmpty()
           ||url.hasQuery()||url.port(443)!=443)continue;
        QNetworkRequest request(url);request.setTransferTimeout(15000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,data.value("_cached").toBool()?QNetworkRequest::AlwaysCache:QNetworkRequest::PreferCache);
        auto *reply=m_artworkNam->get(request);m_artworkReplies.insert(reply);
        auto bytes=std::make_shared<QByteArray>();
        connect(reply,&QNetworkReply::readyRead,this,[reply,bytes]{bytes->append(reply->readAll());if(bytes->size()>2*1024*1024)reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,photo,data,generation,url]{
            if(generation!=m_artworkGeneration||!m_artworkReplies.remove(reply))return;
            bytes->append(reply->readAll());
            if(reply->error()==QNetworkReply::NoError&&reply->url()==url
               &&!reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid()&&bytes->size()<=2*1024*1024){
                QBuffer buffer(bytes.get());buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);
                const auto size=reader.size();
                if(size.width()>=1280&&size.height()>=720&&qint64(size.width())*size.height()<=16*1024*1024){
                    const auto image=reader.read();
                    if(!image.isNull()){
                        PhotoCandidate candidate;candidate.artistMbid=m_artworkRequest.artistMbid;
                        candidate.provider="TheAudioDB";candidate.providerAssetId=photo.value("id").toString().toStdString();
                        candidate.sourceUrl=url.toString().toStdString();candidate.kind=PhotoKind::Hero;
                        candidate.width=image.width();candidate.height=image.height();
                        // The provider's artist fanart fields assert the subject/type;
                        // do not admit albums, banners, clearart or arbitrary image URLs.
                        candidate.canonicalArtistConfirmed=true;candidate.actualPhotograph=true;
                        candidate.cleanImage=true;candidate.subjectCropSafe=true;
                        candidate.sha256="sha256:"+QCryptographicHash::hash(*bytes,QCryptographicHash::Sha256).toHex().toStdString();
                        const auto small=image.scaled(9,8,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                        quint64 fingerprint=0;for(int y=0;y<8;++y)for(int x=0;x<8;++x)
                            fingerprint=(fingerprint<<1)|(qGray(small.pixel(x,y))>qGray(small.pixel(x+1,y))?1:0);
                        candidate.phash64="phash64:"+QString::number(fingerprint,16).rightJustified(16,'0').toStdString();
                        candidate.observedAtEpoch=data.value("fetchedAtEpoch").toLongLong();
                        candidate.rights.status=RightsStatus::VerifiedPermitted;
                        candidate.rights.licenseId="TheAudioDB-development-use";
                        candidate.rights.evidenceUrl=data.value("termsUrl").toString().toStdString();
                        candidate.rights.author="Individual creator not supplied by TheAudioDB";
                        candidate.rights.attribution=photo.value("attribution").toString().toStdString();
                        candidate.rights.artistHeroPermitted=true;candidate.rights.localCachePermitted=true;
                        candidate.rights.validUntilEpoch=candidate.observedAtEpoch+7*24*60*60;
                        m_artworkRequest.fetched.push_back(std::move(candidate));
                    }
                }
            }
            reply->deleteLater();if(m_artworkReplies.isEmpty())publishArtwork();
        });
    }
    if(m_artworkReplies.isEmpty())publishArtwork();else emit artistChanged();
}
void VinylCatalogueSource::publishArtwork(){
    const auto decision=vinyl::artwork::ArtistArtworkResolver{}.resolve(m_artworkRequest);
    QVariantList photos;
    for(const auto &frame:decision.slides){const auto &p=frame.photo;
        photos.append(QVariantMap{{"id",QString::fromStdString(p.providerAssetId)},{"cover",QString::fromStdString(frame.displayRef)},
            {"name",QStringLiteral("Artist photo %1").arg(photos.size()+1)},{"era","TheAudioDB"},
            {"sourceUrl",QString::fromStdString(p.rights.evidenceUrl)},{"attribution",QString::fromStdString(p.rights.attribution)},
            {"width",p.width},{"height",p.height},{"focalX",0.5},{"focalY",0.4},{"autoRotate",true}});
    }
    m_artwork={{"artistMbid",QString::fromStdString(m_artworkRequest.artistMbid)},{"photos",photos},
        {"usage","development"},{"source","TheAudioDB"},{"distributionCleared",false}};
    m_bridge.requestArtwork();emit artistChanged();
}
