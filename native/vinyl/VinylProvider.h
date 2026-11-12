#pragma once
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QFile>
#include <QDateTime>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QDebug>
#include <functional>
#include <memory>
#include "VinylRecommendationIdentity.h"
#include "RecommendationEntityRef.h"

// Public metadata uses the existing app cache. Resolved audio never enters it.
class VinylProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(int annotationsVersion READ annotationsVersion CONSTANT)
public:
    int annotationsVersion() const { return 1; }
    explicit VinylProvider(QString helper,QObject *parent=nullptr,qint64 cacheBudgetBytes=128LL*1024*1024,
        std::function<qint64()> clock={},QString cacheRoot={},int mediaIdleMs=600000,int catalogueIdleMs=180000):QObject(parent),m_helper(std::move(helper)),
        m_cacheBudget(qBound<qint64>(1,cacheBudgetBytes,128LL*1024*1024)),
        m_clock(clock?std::move(clock):std::function<qint64()>([]{return QDateTime::currentMSecsSinceEpoch();})),
        m_cacheRoot(cacheRoot.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/vinyl/catalogue":std::move(cacheRoot)),
        m_mediaIdleMs(qBound(1,mediaIdleMs,600000)),m_catalogueIdleMs(qBound(1,catalogueIdleMs,180000)) {
        connect(this,&VinylProvider::profileLeaving,this,&VinylProvider::cancelAll);
        connect(this,&VinylProvider::shuttingDown,this,&VinylProvider::cancelAll);
    }
    ~VinylProvider() override {cancelAll();}
    static bool homeOperation(const QString &op){return QStringList{"artistCandidates","songCandidates","recommendationIdentity","recordingMatch","populationCandidates"}.contains(op);}
    static bool normalizeHome(const QString &op,const QString &value,const QVariantMap &options,const QVariant &raw,QVariantMap *out){
        if(raw.metaType().id()!=QMetaType::QVariantMap||QJsonDocument::fromVariant(raw).toJson(QJsonDocument::Compact).size()>256*1024)return false;
        auto data=raw.toMap();
        const QSet<QString> fields{"version","seed","items","provenance","fetchedAt","complete","status","capabilities","artist","mapping","mappingStatus","mbid","error","sourceStatus","retryAfterSeconds","videoId","ref","key","destination","_cached","_cacheError","_stale"};
        for(auto it=data.cbegin();it!=data.cend();++it)if(!fields.contains(it.key()))return false;
        const auto version=data.value("version");
        if((version.metaType().id()!=QMetaType::Int&&version.metaType().id()!=QMetaType::LongLong&&version.metaType().id()!=QMetaType::Double)||version.toDouble()!=1
            ||data.value("complete").metaType().id()!=QMetaType::Bool||data.value("items").metaType().id()!=QMetaType::QVariantList
            ||data.value("seed").metaType().id()!=QMetaType::QVariantMap||data.value("provenance").metaType().id()!=QMetaType::QVariantMap
            ||!QDateTime::fromString(data.value("fetchedAt").toString(),Qt::ISODateWithMs).isValid())return false;
        const auto seed=data.value("seed").toMap(),expected=options.value("seed").toMap();
        if(seed!=expected)return false;
        if(op=="artistCandidates"||op=="recommendationIdentity"){
            if(seed.value("kind")!="artist"||seed.value("provider")!="youtube-music"||seed.value("providerId")!=value)return false;
            const auto checked=VinylRecommendationIdentity::fromCatalogue(seed);
            if(!checked.value("ok").toBool())return false;
        }else if(op=="songCandidates"){
            const auto recording=options.value("recording").toMap();
            if(seed.value("kind")!="artist"||seed.value("provider")!="youtube-music"
                ||!VinylRecommendationIdentity::fromCatalogue(seed).value("ok").toBool()
                ||!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(value).hasMatch()
                ||recording.value("kind")!="song"||recording.value("provider")!="youtube-music"
                ||recording.value("providerId")!=value||recording.value("seedArtistRef").toMap()!=seed)return false;
        }else if(op=="populationCandidates"){
            auto checked=vinyl::RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(seed));
            if(!checked.ok()||!checked.value->isCanonical()||checked.value->kind()!=vinyl::CanonicalEntityKind::Artist||checked.value->mbidString()!=value)return false;
        }
        const QString source=op=="recommendationIdentity"?"musicbrainz":op=="populationCandidates"?"listenbrainz":"youtube-music";
        if(data.value("provenance").toMap().value("source")!=source)return false;
        const QString status=data.value("status").toString();
        if(!QStringList{"ready","empty","unavailable","partial","unresolved","ambiguous","verified"}.contains(status))return false;
        const auto items=data.value("items").toList();
        if(items.size()>60||((status=="unavailable"||status=="empty"||status=="ambiguous"||status=="unresolved")&&!items.isEmpty())
            ||(status=="ready"&&items.isEmpty())||(status=="verified"&&!data.value("complete").toBool()))return false;
        QVariantList normalized;QSet<QString> seen;
        const QSet<QString> itemFields{"kind","provider","providerId","id","title","name","image","cover","relation","artistRefs","artists","year","releaseDate","dateSource","releaseType","seconds","videoId","mapping","mappingStatus","playbackMatch","ref","key","destination","available","artistMbid","artistName","recordingMbid","seedArtistRef","version","albumRef","record","trackIndex","candidateSources","videoType"};
        for(const auto &entry:items){
            if(entry.metaType().id()!=QMetaType::QVariantMap)return false;
            auto item=entry.toMap();for(auto it=item.cbegin();it!=item.cend();++it)if(!itemFields.contains(it.key()))return false;
            if(op=="populationCandidates"){
                auto ref=vinyl::RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(item.value("ref").toMap()));
                auto artist=vinyl::ArtistMbid::parse(item.value("artistMbid").toString());
                if(!ref.ok()||!ref.value->isCanonical()||ref.value->kind()!=vinyl::CanonicalEntityKind::Recording||!artist.ok())return false;
                item["key"]=ref.value->stableKey();item["destination"]=QVariantMap();item["available"]=false;
            }else{
                if(item.value("provider")!="youtube-music")return false;
                if(op=="artistCandidates"&&(item.value("seedArtistRef").toMap()!=seed
                    ||!QStringList{"own-release","top-recording","related-artist"}.contains(item.value("relation").toString())))return false;
                if(op=="songCandidates"){
                    const auto sources=item.value("candidateSources").toStringList();
                    if(item.value("kind")!="song"||item.value("seedArtistRef").toMap()!=seed||sources.isEmpty()
                        ||sources.size()>2||!QStringList{"song-radio","song-related"}.contains(item.value("relation").toString()))return false;
                    QSet<QString> unique;
                    for(const auto &source:sources){if(!QStringList{"radio","related"}.contains(source)||unique.contains(source))return false;unique.insert(source);}
                    if(item.value("relation")=="song-radio"&&!unique.contains("radio"))return false;
                    if(item.value("relation")=="song-related"&&!unique.contains("related"))return false;
                }
                if(item.contains("albumRef")){
                    const auto album=item.value("albumRef").toMap();
                    if(album.value("kind")!="album"||album.value("provider")!="youtube-music"
                        ||!VinylRecommendationIdentity::fromCatalogue(album).value("ok").toBool())return false;
                }
                const auto converted=op=="songCandidates"?VinylRecommendationIdentity::fromProviderSong(item)
                    :VinylRecommendationIdentity::fromCatalogue(item);
                if(!converted.value("ok").toBool())return false;
                for(const auto &field:{"ref","key","destination","artists","mappingStatus"})item[field]=converted.value(field);
            }
            const auto key=item.value("key").toString();if(seen.contains(key))continue;seen.insert(key);normalized.append(item);
        }
        if(op=="recommendationIdentity"){
            if(!items.isEmpty())return false;
            if(status=="verified"){
                auto artist=seed;artist["mapping"]=data.value("mapping");
                const auto converted=VinylRecommendationIdentity::fromCatalogue(artist);
                if(converted.value("mappingStatus")!="verified"||data.value("mbid")!=data.value("mapping").toMap().value("mbid"))return false;
                for(const auto &field:{"ref","key","destination"})data[field]=converted.value(field);
            }
        }
        if(op=="recordingMatch"&&status=="verified"){
            if(normalized.size()!=1||data.value("videoId")!=normalized[0].toMap().value("providerId")
                ||!normalized[0].toMap().value("destination").toMap().value("playable").toBool())return false;
            const auto expectedRecording=options.value("recording").toMap(),item=normalized[0].toMap();
            auto folded=[](QString text){text=text.normalized(QString::NormalizationForm_KD).toLower();text.remove(QRegularExpression("[\\p{M}]"));return text.replace(QChar(0x2019),'\'').replace(QChar(0x2010),'-');};
            if(expectedRecording.value("title").toString().isEmpty()||folded(expectedRecording.value("title").toString())!=folded(item.value("title").toString())
                ||expectedRecording.value("seconds").toDouble()<=0||qAbs(expectedRecording.value("seconds").toDouble()-item.value("seconds").toDouble())>2)return false;
            QSet<QString> expectedArtists,actualArtists;
            for(const auto &entry:expectedRecording.value("artistRefs").toList()){const auto artist=entry.toMap();
                if(artist.value("provider")!="youtube-music"||artist.value("kind")!="artist"||!VinylRecommendationIdentity::fromCatalogue(artist).value("ok").toBool())return false;
                expectedArtists.insert(artist.value("providerId").toString());}
            for(const auto &entry:item.value("artistRefs").toList()){const auto artist=entry.toMap();
                if(artist.value("provider")!="youtube-music"||artist.value("kind")!="artist"||!VinylRecommendationIdentity::fromCatalogue(artist).value("ok").toBool())return false;
                actualArtists.insert(artist.value("providerId").toString());}
            if(expectedArtists.isEmpty()||expectedArtists!=actualArtists)return false;
            auto versions=[folded](const QVariantMap &row){QSet<QString> markers;
                const auto explicitVersion=folded(row.value("version").toString());
                auto matches=QRegularExpression("\\b(live|cover|remaster(?:ed)?|remix|acoustic|instrumental|karaoke|radio edit|demo|mono|stereo)\\b").globalMatch(explicitVersion+" "+folded(row.value("title").toString()));
                while(matches.hasNext())markers.insert(matches.next().captured(1));
                if(!explicitVersion.isEmpty()&&!QStringList{"studio","original","album","album version","original version"}.contains(explicitVersion))markers.insert(explicitVersion);
                return markers;};
            if(versions(expectedRecording)!=versions(item))return false;
        }
        data["items"]=normalized;*out=data;return true;
    }
    Q_INVOKABLE void warmPlayback(){
        if(!reuseHelpers()||m_mediaWarm.process||m_active.contains("media-warmup"))return;
        requestPayload("media-warmup","mediaWarmup",{},{});
    }
    Q_INVOKABLE void invalidatePlaybackTransport(){
        for(const auto &lane:m_active.keys())if(m_active.value(lane)==m_mediaWarm.process)cancel(lane);
        if(auto *process=m_mediaWarm.process.data()){
            m_mediaWarm.process=nullptr;m_mediaWarm.busy=false;++m_mediaWarm.idleGeneration;
            retireProcess(process);
        }
    }
    Q_INVOKABLE int request(const QString &lane,const QString &operation,const QString &value){return requestPayload(lane,operation,value,{});}
    Q_INVOKABLE QVariantMap cachedRecommendation(const QString &operation,const QString &value,const QVariantMap &payload) const {
        if(!homeOperation(operation))return {};
        auto input=QJsonObject::fromVariantMap(payload);input.remove("refresh");input["operation"]=operation;input["value"]=value;input["cacheSchema"]=2;
        const auto key=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(input).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        const auto path=m_cacheRoot+"/"+key+".json";
        auto data=readCache(path,key,operation,value,payload,true,true);
        if(data.isEmpty()){data=readCache(path,key,operation,value,payload,true,false);if(!data.isEmpty())data["_stale"]=true;}
        if(!data.isEmpty())data["_cached"]=true;return data;
    }
    Q_INVOKABLE QVariantMap annotationCache(const QString &key) const {
        if(!key.startsWith("genius-web-v1:")||key.size()>4096)return {};
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),QCryptographicHash::Sha256).toHex());
        return readCache(m_cacheRoot+"/"+hash+".json",hash,{},{},{},false,true);
    }
    Q_INVOKABLE void storeAnnotationCache(const QString &key,const QVariantMap &data) {
        if(!key.startsWith("genius-web-v1:")||key.size()>4096||data.isEmpty())return;
        const auto bytes=QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
        if(bytes.size()>2*1024*1024)return;
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),QCryptographicHash::Sha256).toHex());
        if(!QDir().mkpath(m_cacheRoot))return;
        QSaveFile file(m_cacheRoot+"/"+hash+".json");
        if(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size())file.commit();
        prune();
    }
    Q_INVOKABLE int requestPayload(const QString &lane,const QString &operation,const QString &value,const QVariantMap &payload){
        static const QStringList operations{"browseCategories","browseCatalogue","resolveCatalogueItem","resolveCatalogueArt","mediaWarmup","search","catalogueSearch","cataloguePage","playlistPage","discover","playlist","album","resolve","resolveVideo","relatedVideo","artist","artistReleases","artistVideos","artistIdentity","artistArtwork","artistCandidates","songCandidates","recommendationIdentity","recordingMatch","populationCandidates","lyrics"};
        static const QStringList lanes{"catalogue-categories","catalogue-songs","catalogue-albums","catalogue-artists","catalogue-item","catalogue-art","media-warmup","browse","queue","play","playFallback","prepare-next","video-match","video-stream","playlist-page","search-albums","search-artists","search-songs","search-playlists","taste-artists","discover-new-releases","discover-trending","discover-artists","discover-charts","artist","artist-albums","artist-singles","artist-videos","artist-identity","artist-artwork","home-seed-0","home-seed-1","home-identity","home-population","home-listening-0","home-listening-1","named-collections","lyrics"};
        const bool songLane=lane=="home-song-0"||lane=="home-song-1";
        if(!operations.contains(operation)||(!lanes.contains(lane)&&!songLane)||value.size()>300)return -1;
        if(operation=="browseCategories"){
            if(lane!="catalogue-categories"||!value.isEmpty())return -1;
        }else if(operation=="browseCatalogue"){
            if(!QStringList{"songs","albums","artists"}.contains(value)||lane!="catalogue-"+value)return -1;
        }else if(operation=="resolveCatalogueItem"){
            const auto item=payload.value("item");
            if(lane!="catalogue-item"||value.isEmpty()||payload.size()!=1
                ||item.metaType().id()!=QMetaType::QVariantMap||item.toMap().value("id").toString()!=value
                ||!QStringList{"songs","albums","artists"}.contains(item.toMap().value("kind").toString()))return -1;
        }else if(operation=="resolveCatalogueArt"){
            const auto items=payload.value("items");
            if(lane!="catalogue-art"||value!="lastfm"||payload.size()!=1
                ||items.metaType().id()!=QMetaType::QVariantList||items.toList().size()>12)return -1;
        }else if(lane.startsWith("catalogue-"))return -1;
        const bool warming=operation=="mediaWarmup";
        if(warming!=(lane=="media-warmup")||(warming&&(!value.isEmpty()||!payload.isEmpty()||!reuseHelpers())))return -1;
        if(lane=="prepare-next"&&(operation!="resolve"||payload.value("source")!="youtube"))return -1;
        if(songLane&&operation!="songCandidates")return -1;
        if(operation=="songCandidates"&&!songLane)return -1;
        if(lane.startsWith("home-listening-") && operation!="album" && operation!="artistCandidates")return -1;
        // Collections may read exact album metadata, never resolve playback or
        // take ownership of an existing browse/play/queue request.
        if(lane=="named-collections" && operation!="album")return -1;
        cancel(lane);const int id=++m_serial;const bool home=homeOperation(operation);
        const bool discoverBrowse=operation=="browseCatalogue"
            &&QStringList{"lastfm","jiosaavn"}.contains(payload.value("catalogueSource").toString());
        const bool discoverResolver=operation=="resolveCatalogueItem"||operation=="resolveCatalogueArt";
        const bool cacheable=!warming&&operation!="resolve"&&operation!="resolveVideo"&&!discoverBrowse&&!discoverResolver;
        if(home){
            const QSet<QString> allowed{"seed","recording","refresh","region","fetchedAt","limit"};
            for(auto it=payload.cbegin();it!=payload.cend();++it)if(!allowed.contains(it.key()))return -1;
            const QSet<QString> seedFields{"identity","mbid","kind","provider","providerId","id","key","name","title","image","cover","mappingStatus","canonicalMbid","legacyId","mapping"};
            const auto seed=payload.value("seed").toMap();for(auto it=seed.cbegin();it!=seed.cend();++it)if(!seedFields.contains(it.key()))return -1;
        }
        QJsonObject input=QJsonObject::fromVariantMap(payload);input["operation"]=operation;input["value"]=value;
        const auto requestBytes=QJsonDocument(input).toJson(QJsonDocument::Compact);if(requestBytes.size()>(home?256*1024:1024*1024))return -1;
        // Only accepted foreground work may preempt optional startup preparation.
        if(!warming&&(operation=="resolve"||operation=="resolveVideo"))cancel("media-warmup");
        auto cacheInput=input;cacheInput.remove("refresh");if(home)cacheInput["cacheSchema"]=2;
        const auto key=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(cacheInput).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        const auto path=m_cacheRoot+"/"+key+".json";
        WarmSlot *warm=nullptr;
        if(warming||operation=="resolve"||operation=="resolveVideo")warm=&m_mediaWarm;
        else if(QStringList{"browseCategories","browseCatalogue","resolveCatalogueItem","cataloguePage","catalogueSearch","album","relatedVideo"}.contains(operation))
            warm=&m_catalogueWarm;
        // Only one worker per group can be reused. Overlapping ordinary
        // search/play requests still launch directly; Home's existing cap is
        // independent and unchanged.
        // External/fake transport helpers keep the original one-shot contract.
        const bool server=warm&&!warm->busy&&reuseHelpers();
        QProcess *process=nullptr;
        if(server){
            if(warm->process&&warm->process->state()==QProcess::Running){
                process=warm->process.data();process->disconnect(this);
            }else{
                if(warm->process)retireProcess(warm->process.data());
                process=new QProcess(this);warm->process=process;
            }
            warm->busy=true;++warm->idleGeneration;
        }else process=new QProcess(this);
        if(process->state()!=QProcess::Running){
            auto environment=QProcessEnvironment::systemEnvironment();
            environment.insert("COLOSSEUM_VINYL_DISCOVER_CACHE_DIR",QDir(m_cacheRoot).filePath("discover-v1"));
            process->setProcessEnvironment(environment);
        }
        m_active.insert(lane,process);auto output=std::make_shared<QByteArray>();
        auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
        auto *deadline=new QTimer(process);deadline->setSingleShot(true);
        const bool reused=server&&process->state()==QProcess::Running;
        const bool traceEnabled=qEnvironmentVariableIntValue("COLOSSEUM_CATALOGUE_TRACE")==1;
        const auto trace=[traceEnabled,lane,operation,id,reused](const QString &phase){
            if(!traceEnabled)return;
            qInfo().noquote()<<"[vinyl-provider]"<<QJsonDocument(QJsonObject{{"phase",phase},{"operation",operation},
                {"lane",lane},{"requestId",id},{"reused",reused}}).toJson(QJsonDocument::Compact);
        };
        auto finish=[guard=QPointer<VinylProvider>(this),this,process,lane,id,home,cacheable,path,key,operation,value,payload,elapsed,warm,server,deadline,reused](QVariant data,QString error){
            if(!guard||m_active.value(lane)!=process)return;
            const bool ran=m_running.contains(process);
            deadline->stop();deadline->deleteLater();
            m_active.remove(lane);m_running.remove(process);process->disconnect(this);
            if(server){
                warm->busy=false;++warm->idleGeneration;
                if(error.isEmpty()&&process->state()==QProcess::Running){
                    const auto idleGeneration=warm->idleGeneration;
                    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
                        [this,warm,process](int,QProcess::ExitStatus){
                            if(warm->process==process&&!warm->busy){
                                warm->process=nullptr;++warm->idleGeneration;process->deleteLater();
                            }
                        });
                    QTimer::singleShot(warm==&m_mediaWarm?m_mediaIdleMs:m_catalogueIdleMs,this,[this,warm,process,idleGeneration]{
                        if(warm->process!=process||warm->busy||warm->idleGeneration!=idleGeneration)return;
                        warm->process=nullptr;++warm->idleGeneration;
                        retireProcess(process);
                    });
                }else{
                    if(warm->process==process)warm->process=nullptr;
                    retireProcess(process);
                }
            }else{
                retireProcess(process);
            }
            if(home&&error.isEmpty()){
                QVariantMap normalized;
                if(!normalizeHome(operation,value,payload,data,&normalized)){data={};error="The recommendation response is invalid.";}
                else data=normalized;
            }
            const auto map=data.toMap();
            if(home&&ran&&map.value("sourceStatus")=="rate_limited"){
                const auto retry=qBound(1,map.value("retryAfterSeconds",60).toInt(),3600);
                m_deferred[service(operation)]=m_clock()+qint64(retry)*1000;
            }
            const bool unavailable=home&&map.value("status")=="unavailable";
            if(cacheable){
                if(error.isEmpty()&&!unavailable&&!map.value("_cached").toBool()){
                    const bool positive=!map.value("items").toList().isEmpty()||map.value("status")=="verified";
                    if(!home||positive||(map.value("complete").toBool()&&map.value("status")!="partial")){
                        QJsonObject wire=QJsonObject::fromVariantMap(map);
                        if(home)wire={{"version",2},{"requestKey",key},{"storedAtMs",m_clock()},{"ttlMs",ttl(operation,map)},{"data",wire}};
                        const auto bytes=home?QJsonDocument(wire).toJson(QJsonDocument::Compact):QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
                        if(bytes.size()<=4*1024*1024){QDir().mkpath(m_cacheRoot);QSaveFile file(path);if(file.open(QIODevice::WriteOnly)){file.write(bytes);file.commit();}prune();}
                    }
                }else if(!error.isEmpty()||unavailable){
                    auto cached=readCache(path,key,operation,value,payload,home,false);
                    if(!cached.isEmpty()){cached["_cached"]=true;cached["_stale"]=true;cached["_cacheError"]=error.isEmpty()?map.value("error","The source is unavailable.").toString():error;data=cached;error.clear();}
                }
            }
            const auto result=data.toMap();
            const auto count=data.metaType().id()==QMetaType::QVariantList?data.toList().size()
                :result.value("items",result.value("tracks",result.value("albums"))).toList().size();
            qInfo().noquote()<<"[vinyl-provider]"<<QJsonDocument(QJsonObject{{"operation",operation},{"lane",lane},
                {"requestId",id},{"cacheHit",result.value("_cached").toBool()},{"stale",result.value("_stale").toBool()},
                {"workerReused",reused},{"elapsedMs",elapsed->elapsed()},{"count",count},{"ok",error.isEmpty()&&result.value("status")!="unavailable"}}).toJson(QJsonDocument::Compact);
            emit completed(lane,id,data,error);
            if(guard)QTimer::singleShot(0,this,[this]{startWaiting();});
        };
        connect(deadline,&QTimer::timeout,this,[finish]{finish({},"The music catalogue took too long. Retry when connected.");});
        connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){
            finish({},error==QProcess::FailedToStart?"The music catalogue runtime is unavailable. Local music is still available.":"The music catalogue helper failed.");
        });
        connect(process,&QProcess::readyReadStandardError,this,[process]{process->readAllStandardError();});
        auto parseReply=[this,finish,operation](const QByteArray &bytes){
            QJsonParseError parse;
            const auto doc=QJsonDocument::fromJson(bytes,&parse);
            const auto object=doc.object();
            if(parse.error!=QJsonParseError::NoError||!object.value("ok").isBool())
                finish({},"The music catalogue helper failed.");
            else if(!object.value("ok").toBool()){
                if(operation=="lyrics"&&object.value("errorCode")=="rate_limited")
                    m_deferred["lrclib"]=m_clock()+qint64(qBound(1,object.value("retryAfterSeconds").toInt(60),3600))*1000;
                finish({},object.value("error").toString("The music catalogue is unavailable."));
            }else finish(object.value("data").toVariant(),{});
        };
        connect(process,&QProcess::readyReadStandardOutput,this,[process,output,finish,parseReply,server]{
            output->append(process->readAllStandardOutput());
            if(output->size()>4*1024*1024+(server?1:0)){
                finish({},"The catalogue response exceeded its limit.");return;
            }
            if(server){
                const auto newline=output->indexOf('\n');
                if(newline>=0){
                    if(newline!=output->size()-1)finish({},"The music catalogue helper failed.");
                    else parseReply(output->left(newline));
                }
            }
        });
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
            [process,output,finish,parseReply,server](int code,QProcess::ExitStatus status){
                output->append(process->readAllStandardOutput());
                if(code!=0||status!=QProcess::NormalExit||output->size()>4*1024*1024+(server?1:0)){
                    finish({},"The music catalogue helper failed.");return;
                }
                if(server){
                    const auto newline=output->indexOf('\n');
                    if(newline<0||newline!=output->size()-1)finish({},"The music catalogue helper failed.");
                    else parseReply(output->left(newline));
                }else parseReply(*output);
            });
        connect(process,&QProcess::started,this,[trace]{trace("process-started");});
        const bool catalogueObject=QStringList{"browseCategories","browseCatalogue","cataloguePage","catalogueSearch","artist","artistReleases","album","artistVideos"}.contains(operation);
        if(cacheable&&(home||operation=="discover"||operation=="lyrics"||catalogueObject)&&!payload.value("refresh").toBool()){
            const auto cached=readCache(path,key,operation,value,payload,home,true);
            if(!cached.isEmpty()){QTimer::singleShot(0,process,[finish,cached]{auto map=cached;map["_cached"]=true;finish(map,{});});return id;}
        }
        if(operation=="lyrics"&&m_deferred.value("lrclib")>m_clock()){
            QTimer::singleShot(0,process,[finish]{finish({},"Lyrics are temporarily rate limited. Retry later.");});return id;
        }
        if(home&&m_deferred.value(service(operation))>m_clock()){
            QVariantMap data{{"version",1},{"seed",payload.value("seed",QVariantMap())},{"items",QVariantList()},
                {"provenance",QVariantMap{{"source",service(operation)}}},{"fetchedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"complete",false},{"status","unavailable"},{"sourceStatus","rate_limited"},{"retryAfterSeconds",(m_deferred.value(service(operation))-m_clock()+999)/1000},
                {"error","The source is temporarily rate limited."}};
            QTimer::singleShot(0,process,[finish,data]{finish(data,{});});return id;
        }
#ifdef Q_OS_WIN
        const auto bundled=QFileInfo(m_helper).absolutePath()+"/runtime/python.exe";
#else
        const auto bundled=QFileInfo(m_helper).absolutePath()+"/runtime/bin/python3";
#endif
        const auto python=qEnvironmentVariable("COLOSSEUM_VINYL_PYTHON",bundled);
        auto launch=[guard=QPointer<VinylProvider>(this),this,process,deadline,lane,python,requestBytes,operation,home,server,trace]{
            if(!guard||m_active.value(lane)!=process)return;
            trace("dispatch");
            if(home){m_running.insert(process);if(operation=="songCandidates")deadline->start(20000);}
            else deadline->start(operation=="lyrics"?12000:operation=="discover"?18000:operation=="artistIdentity"?180000:90000);
            if(server){
                if(process->state()!=QProcess::Running)process->start(python,{"-I",m_helper,"--serve"});
                if(guard&&m_active.value(lane)==process)process->write(requestBytes+"\n");
            }else{
                process->start(python,{"-I",m_helper});
                if(guard&&m_active.value(lane)==process){process->write(requestBytes);process->closeWriteChannel();}
            }
        };
        if(home){if(operation!="songCandidates")deadline->start(8000);m_waiting.enqueue({QPointer<QProcess>(process),launch});QTimer::singleShot(0,this,[this]{startWaiting();});}
        else QTimer::singleShot(0,process,launch);
        return id;
    }
    Q_INVOKABLE void cancel(const QString &lane){
        auto *process=m_active.take(lane);if(!process)return;m_running.remove(process);
        for(auto *slot:{&m_mediaWarm,&m_catalogueWarm}){
            if(slot->process==process){slot->process=nullptr;slot->busy=false;++slot->idleGeneration;}
        }
        retireProcess(process);QTimer::singleShot(0,this,[this]{startWaiting();});
    }
    Q_INVOKABLE void cancelAll(){
        m_waiting.clear();for(const auto &lane:m_active.keys())cancel(lane);
        for(auto *slot:{&m_mediaWarm,&m_catalogueWarm}){
            auto *process=slot->process.data();
            slot->process=nullptr;slot->busy=false;++slot->idleGeneration;
            if(process)retireProcess(process);
        }
    }
signals:
    void completed(const QString &lane,int requestId,const QVariant &data,const QString &error);
    void profileLeaving();void profileEntered();void checkpointRequested();void shuttingDown();
private:
    bool reuseHelpers() const {
        return qEnvironmentVariable("COLOSSEUM_VINYL_REUSE_HELPERS")!="0"&&QFileInfo(m_helper).fileName()=="provider.py";
    }
    // Detach before asynchronous kill so provider destruction never waits for
    // Python. Disconnect and lane removal fence all obsolete request callbacks.
    void retireProcess(QProcess *process){
        process->disconnect(this);process->setParent(nullptr);
        if(process->state()==QProcess::NotRunning){process->deleteLater();return;}
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);
        connect(process,&QProcess::errorOccurred,process,[process](QProcess::ProcessError){
            if(process->state()==QProcess::NotRunning)process->deleteLater();
        });
        process->kill();
        if(process->state()==QProcess::NotRunning)process->deleteLater();
    }
    struct WarmSlot {
        QPointer<QProcess> process;
        bool busy=false;
        quint64 idleGeneration=0;
    };
    static QString service(const QString &op){return op=="recommendationIdentity"?"musicbrainz":op=="populationCandidates"?"listenbrainz":"youtube-music";}
    static qint64 ttl(const QString &op,const QVariantMap &data){
        if(data.value("items").toList().isEmpty()&&data.value("status")!="verified")return 3600000;
        return op=="recommendationIdentity"?7LL*86400000:op=="populationCandidates"?86400000:6LL*3600000;
    }
    QVariantMap readCache(const QString &path,const QString &key,const QString &op,const QString &value,const QVariantMap &payload,bool home,bool fresh)const{
        QFile file(path);const QFileInfo info(file);if(info.size()>4*1024*1024||!file.open(QIODevice::ReadOnly))return {};
        const auto doc=QJsonDocument::fromJson(file.readAll());if(!doc.isObject())return {};
        if(!home){const auto age=info.lastModified().secsTo(QDateTime::currentDateTime());if(fresh&&(age<0||age>900))return {};return doc.object().toVariantMap();}
        const auto wire=doc.object();if(wire.value("version").toInt()!=2||wire.value("requestKey")!=key||!wire.value("data").isObject())return {};
        if(!wire.value("storedAtMs").isDouble()||wire.value("storedAtMs").toInteger()<=0||!wire.value("ttlMs").isDouble())return {};
        const auto age=m_clock()-wire.value("storedAtMs").toInteger();
        if(fresh&&(age<0||age>wire.value("ttlMs").toInteger()))return {};
        QVariantMap data;if(!normalizeHome(op,value,payload,wire.value("data").toVariant(),&data)||wire.value("ttlMs").toInteger()!=ttl(op,data))return {};return data;
    }
    void prune(){
        QDir root(m_cacheRoot);auto files=root.entryInfoList({"*.json"},QDir::Files|QDir::NoSymLinks,QDir::Time|QDir::Reversed);
        qint64 total=0;for(const auto &file:files)total+=file.size();int remaining=files.size();
        for(const auto &file:files){if(total<=m_cacheBudget&&remaining<=4096)break;if(QFile::remove(file.absoluteFilePath())){total-=file.size();--remaining;}}
    }
    void startWaiting(){
        while(m_running.size()<2&&!m_waiting.isEmpty()){auto job=m_waiting.dequeue();if(job.first&&m_active.values().contains(job.first.data()))job.second();}
    }
    QString m_helper;int m_serial=0;QHash<QString,QProcess*> m_active;
    qint64 m_cacheBudget;std::function<qint64()> m_clock;QString m_cacheRoot;
    QSet<QProcess*> m_running;QQueue<QPair<QPointer<QProcess>,std::function<void()>>> m_waiting;QHash<QString,qint64> m_deferred;
    WarmSlot m_mediaWarm,m_catalogueWarm;
    const int m_mediaIdleMs,m_catalogueIdleMs;
};
