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
#include <QImageReader>
#include <QImage>
#include <QBuffer>
using namespace vinyl::artist;

VinylCatalogueSource::VinylCatalogueSource(VinylLocalSource *local,VinylProvider *provider,CollectionStore *collection,QObject *parent)
 :QObject(parent),m_local(local),m_provider(provider),m_collection(collection),m_bridge(*this,*this,*this) {
    connect(local,&VinylLocalSource::completed,this,[this](auto lane,int id,auto data,auto error){accept(lane,id,data,error,true);});
    connect(provider,&VinylProvider::completed,this,[this](auto lane,int id,auto data,auto error){
        if(m_artistRequests.value(lane,-1)==id){m_artistRequests.remove(lane);acceptArtist(lane,data,error);return;}
        accept(lane,id,data,error,false);
    });
    connect(local,&VinylLocalSource::importCompleted,this,&VinylCatalogueSource::importCompleted);
    connect(local,&VinylLocalSource::importProgress,this,&VinylCatalogueSource::importProgress);
    connect(local,&VinylLocalSource::profileLeaving,this,[this]{cancelAll();m_albums.clear();m_videos.clear();emit profileLeaving();});
    connect(local,&VinylLocalSource::profileEntered,this,&VinylCatalogueSource::profileEntered);
    connect(local,&VinylLocalSource::checkpointRequested,this,&VinylCatalogueSource::checkpointRequested);
    connect(local,&VinylLocalSource::shuttingDown,this,[this]{cancelAll();emit shuttingDown();});
}
void VinylCatalogueSource::setCollectionStore(QObject *store){m_collection=qobject_cast<CollectionStore *>(store);}
void VinylCatalogueSource::setAlbums(const QVariantList &entries){
    m_local->setAlbums(entries);
    for(const auto &entry:entries){auto album=entry.toMap().value("payload").toMap().value("album").toMap();if(album.isEmpty())album=entry.toMap();if(!album.isEmpty())remember(album);}
}
QVariantMap VinylCatalogueSource::remember(QVariantMap album){
    const auto id=album.value("id").toString();if(id.isEmpty())return album;
    const auto prior=m_albums.value(id);
    if(!album.contains("providerEditions")&&prior.contains("providerEditions"))album["providerEditions"]=prior.value("providerEditions");
    m_albums.insert(id,album);
    if(!album.value("browseId").toString().isEmpty())m_albums.insert(album.value("browseId").toString(),album);
    for(const auto &item:album.value("tracks").toList()){
        const auto track=item.toMap();const auto video=track.value("videoId").toString();
        if(QRegularExpression("^[A-Za-z0-9_-]{11}$").match(video).hasMatch())m_videos.insert(track.value("id").toString(),video);
    }
    return album;
}
int VinylCatalogueSource::request(const QString &lane,const QString &operation,const QString &value){return request(lane,operation,value,{});}
int VinylCatalogueSource::request(const QString &lane,const QString &operation,const QString &value,const QVariantMap &options){
    cancel(lane);const int id=++m_serial;
    bool local=operation=="search"||value.startsWith("vault:")||value.startsWith("vinyl:local-");
    QString remoteValue=value;
    if(operation=="album"&&m_albums.contains(value)){
        const auto album=m_albums.value(value);
        if(album.value("origin")=="local")local=true;
        else remoteValue=album.value("browseId",value).toString();
    }
    if(operation=="resolve"&&!local){remoteValue=m_videos.value(value);}
    const int transport=local?m_local->request(lane,operation,value,options):m_provider->request(lane,operation,remoteValue);
    if(transport<0)return -1;
    m_pending.insert(lane,{id,transport,local,operation,value});return id;
}
void VinylCatalogueSource::accept(const QString &lane,int id,const QVariant &data,const QString &error,bool local){
    if(!m_pending.contains(lane))return;const auto pending=m_pending.value(lane);
    if(pending.local!=local||pending.transportId!=id)return;m_pending.remove(lane);
    QVariant result=data;
    if(error.isEmpty()){
        if(pending.operation=="album")result=remember(data.toMap());
        else if(pending.operation=="catalogueSearch"){
            auto map=data.toMap();QVariantList albums;
            for(const auto &item:map.value("albums").toList())albums.append(remember(item.toMap()));map["albums"]=albums;result=map;
        }else if(pending.operation=="resolve"&&!local){auto map=data.toMap();map["trackId"]=pending.value;result=map;}
    }
    emit completed(lane,pending.id,result,error);
}
void VinylCatalogueSource::cancel(const QString &lane){m_pending.remove(lane);m_local->cancel(lane);m_provider->cancel(lane);}
void VinylCatalogueSource::cancelAll(){for(const auto &lane:m_pending.keys())cancel(lane);m_local->cancelImport();leaveArtist();}
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
    if(id<0)m_artistError="This catalogue request could not start.";else m_artistRequests.insert(lane,id);
}
void VinylCatalogueSource::openArtist(const QVariantMap &intent){
    leaveArtist();m_artistIntent=intent;
    auto id=intent.value("channelId",intent.value("id")).toString();if(id.startsWith("ytm:artist:"))id=id.mid(11);
    m_artist={{"name",intent.value("name")},{"id",intent.value("id")}};
    if(QRegularExpression("^UC[A-Za-z0-9_-]{22}$").match(id).hasMatch()){
        m_channel=id;artistRequest("artist","artist",id);
        artistRequest("artist-albums","artistReleases",id,{{"channelId",id},{"kind","albums"}});
        artistRequest("artist-singles","artistReleases",id,{{"channelId",id},{"kind","singles"}});
        artistRequest("artist-videos","artistVideos",id);
    }else if(!intent.value("name").toString().trimmed().isEmpty())artistRequest("artist","catalogueSearch",intent.value("name").toString());
    emit artistChanged();
}
void VinylCatalogueSource::acceptArtist(const QString &lane,const QVariant &data,const QString &error){
    if(!error.isEmpty()){if(lane=="artist-identity")m_identity={{"unavailable",true}};m_artistError+=(m_artistError.isEmpty()?QString():"\n")+error;emit artistChanged();return;}
    auto map=data.toMap();if(map.value("_cached").toBool())m_cached=true;
    if(lane=="artist-artwork"){loadArtwork(map);return;}
    if(lane=="artist"&&m_channel.isEmpty()){
        QVariantList exact;for(const auto &row:map.value("artists").toList())if(row.toMap().value("name").toString().compare(m_artistIntent.value("name").toString(),Qt::CaseInsensitive)==0)exact.append(row);
        if(exact.size()==1){openArtist(exact.first().toMap());return;}
        m_artistError="Choose an artist from catalogue search to disambiguate this name.";emit artistChanged();return;
    }
    if(lane=="artist"){
        if(map.value("requestedChannelId").toString()!=m_channel){m_artistError="The catalogue returned a different artist.";emit artistChanged();return;}
        m_artist=map;m_artist["id"]="ytm:artist:"+m_channel;
    }else if(lane=="artist-albums"||lane=="artist-singles"){
        if(map.value("requestedChannelId").toString()!=m_channel){m_artistError="The release list belongs to a different artist.";emit artistChanged();return;}
        QVariantList items;
        for(const auto &entry:map.value("items").toList()){
            auto album=entry.toMap();album["origin"]="youtube-music";album["artists"]=QStringList{m_artist.value("name").toString()};
            album["artistIds"]=QStringList{"ytm:artist:"+m_channel};album["kind"]=lane=="artist-singles"?"single":"album";
            // A card is not an admitted tracklist. Opening it requests the full album.
            album["tracks"]=QVariantList{};items.append(remember(album));
        }
        if(lane=="artist-albums")m_artistAlbums=items;else m_artistSingles=items;
        if(!map.value("complete").toBool())m_artistError="This release list is partial. Retry to load its continuation.";
    }else if(lane=="artist-videos"){
        if(map.value("requestedChannelId").toString()!=m_channel){m_artistError="The media list belongs to a different artist.";emit artistChanged();return;}
        m_artistMedia=map;if(!map.value("complete").toBool())m_artistError="Showing a limited video shelf; more videos may be available.";
    }else if(lane=="artist-identity"){
        if(map.value("source")!="musicbrainz"||!map.value("complete").toBool()){
            m_identity={{"unavailable",true}};m_artistError="Canonical identity is incomplete; provider catalogue remains available.";emit artistChanged();return;
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
        m_artistError="Artist artwork identity or source is unavailable.";emit artistChanged();return;
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
