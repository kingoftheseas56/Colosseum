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
#include <QUuid>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include "VinylBackgroundRead.h"
#include "VinylRecommendationIdentity.h"
#include "VinylSourcePool.h"
#include "RecommendationEntityRef.h"

// Public metadata uses the existing app cache. Resolved audio never enters it.
class VinylProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(int annotationsVersion READ annotationsVersion CONSTANT)
    Q_PROPERTY(QString recommendationCacheRoot READ recommendationCacheRoot CONSTANT)
    Q_PROPERTY(qint64 recommendationCacheNow READ recommendationCacheNow)
public:
    QString recommendationCacheRoot() const {return m_cacheRoot;}
    qint64 recommendationCacheNow() const {return m_clock();}
    int annotationsVersion() const { return 1; }
    explicit VinylProvider(QString helper,QObject *parent=nullptr,qint64 cacheBudgetBytes=128LL*1024*1024,
        std::function<qint64()> clock={},QString cacheRoot={},int mediaIdleMs=600000,int catalogueIdleMs=180000):QObject(parent),m_helper(std::move(helper)),
        m_cacheBudget(qBound<qint64>(1,cacheBudgetBytes,128LL*1024*1024)),
        m_clock(clock?std::move(clock):std::function<qint64()>([]{return QDateTime::currentMSecsSinceEpoch();})),
        m_cacheRoot(cacheRoot.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/vinyl/catalogue":std::move(cacheRoot)),
        m_mediaIdleMs(qBound(1,mediaIdleMs,600000)),m_catalogueIdleMs(qBound(1,catalogueIdleMs,180000)) {
        m_waitingWake=new QTimer(this);m_waitingWake->setSingleShot(true);
        connect(m_waitingWake,&QTimer::timeout,this,&VinylProvider::startWaiting);
        connect(this,&VinylProvider::profileLeaving,this,&VinylProvider::cancelAll);
        connect(this,&VinylProvider::shuttingDown,this,&VinylProvider::cancelAll);
    }
    ~VinylProvider() override {cancelAll();}
    static bool homeOperation(const QString &op){return VinylSourcePool::metadataOperation(op)||QStringList{"artistCandidates","songCandidates","recommendationIdentity","recordingMatch","populationCandidates"}.contains(op);}
    static bool normalizeHome(const QString &op,const QString &value,const QVariantMap &options,const QVariant &raw,QVariantMap *out){
        if(VinylSourcePool::metadataOperation(op))return VinylSourcePool::normalizeMetadataEnvelope(op,value,options,raw,out);
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
        m_suppressReplacement=true;
        ++m_mediaWarm.idleGeneration;
        for(const auto &lane:m_active.keys())if(m_active.value(lane)==m_mediaWarm.process)cancel(lane);
        if(auto *process=m_mediaWarm.process.data()){
            m_mediaWarm.process=nullptr;m_mediaWarm.busy=false;++m_mediaWarm.idleGeneration;
            retireProcess(process);
        }
        m_suppressReplacement=false;
    }
    Q_INVOKABLE int request(const QString &lane,const QString &operation,const QString &value){return requestPayload(lane,operation,value,{});}
    Q_INVOKABLE QVariantMap cachedRecommendation(const QString &operation,const QString &value,const QVariantMap &payload) const {
        return cachedRecommendationAt(m_cacheRoot,m_clock(),operation,value,payload);
    }
    static QVariantMap cachedRecommendationAt(const QString &root,qint64 now,const QString &operation,const QString &value,const QVariantMap &payload) {
        if(!homeOperation(operation))return {};
        auto input=QJsonObject::fromVariantMap(payload);input.remove("refresh");input["operation"]=operation;input["value"]=value;input["cacheSchema"]=2;
        const auto key=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(input).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        const auto path=root+"/"+key+".json";
        auto data=readCacheAt(path,key,operation,value,payload,true,true,now);
        if(data.isEmpty()){data=readCacheAt(path,key,operation,value,payload,true,false,now);if(!data.isEmpty())data["_stale"]=true;}
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
        static const QStringList operations{"browseCategories","browseCatalogue","resolveCatalogueItem","resolveCatalogueArt","chartWeek","wikipediaList","mostStreamed","mediaWarmup","search","catalogueSearch","cataloguePage","playlistPage","discover","playlist","album","resolve","resolveVideo","relatedVideo","artist","artistReleases","artistVideos","artistIdentity","artistArtwork","artistCandidates","songCandidates","recommendationIdentity","recommendationDiscography","chartCandidates","similarArtists","recordingMatch","populationCandidates","lyrics"};
        static const QStringList lanes{"catalogue-categories","catalogue-songs","catalogue-albums","catalogue-artists","catalogue-item","catalogue-art","media-warmup","browse","queue","play","playFallback","prepare-next","video-match","video-stream","playlist-page","search-albums","search-artists","search-songs","search-playlists","taste-artists","discover-new-releases","discover-trending","discover-artists","discover-charts","artist","artist-albums","artist-singles","artist-videos","artist-identity","artist-artwork","home-seed-0","home-seed-1","home-identity","home-population","home-listening-0","home-listening-1","named-collections","lyrics"};
        static const QStringList chartKinds{"songs","albums","artists"};
        static const QStringList wikiLists{"hot100-2025","global-200-2025","year-end-100-2025",
                                         "mainstream-rock-2020s","hot100-1990s"};
        const bool editorial=operation=="chartWeek"||operation=="wikipediaList"||operation=="mostStreamed";
        const bool editorialArt=operation=="resolveCatalogueArt"&&value=="editorial";
        const bool exploreArt=editorialArt||(operation=="resolveCatalogueArt"&&value=="lastfm"&&lane=="explore-art-lastfm");
        const bool heroArt=operation=="resolveCatalogueArt"&&value=="hero";
        const bool exploreBrowse=operation=="browseCatalogue"&&value=="songs"
            &&payload.value("catalogueSource")=="lastfm"&&lane=="explore-tag-"+payload.value("list").toString();
        const bool exploreLane=lane.startsWith("explore-");
        if((editorial||exploreArt||exploreBrowse)!=exploreLane)return -1;
        if(editorial){
            if(!payload.isEmpty())return -1;
            if(operation=="chartWeek"&&(!chartKinds.contains(value)||lane!="explore-week-"+value))return -1;
            if(operation=="wikipediaList"&&(!wikiLists.contains(value)||lane!="explore-"+value))return -1;
            if(operation=="mostStreamed"&&(value!="songs"||lane!="explore-most-streamed"))return -1;
        }
        const bool songLane=lane=="home-song-0"||lane=="home-song-1";
        const bool downloadLane=lane=="download"&&operation=="resolve"&&payload.value("source")=="youtube";
        const bool metadataLane=lane=="home-metadata-0"||lane=="home-metadata-1";
        const bool metadata=VinylSourcePool::metadataOperation(operation);
        if(!operations.contains(operation)||(!lanes.contains(lane)&&!songLane&&!metadataLane&&!downloadLane&&!exploreLane&&!heroArt)||value.size()>300)return -1;
        // Reject malformed enrichment before it can cancel an accepted request.
        if(metadata!=metadataLane||(metadata&&!VinylSourcePool::validMetadataRequest(operation,value,payload)))return -1;
        if(operation=="browseCategories"){
            if(lane!="catalogue-categories"||!value.isEmpty())return -1;
        }else if(operation=="browseCatalogue"){
            if(!exploreBrowse&&(!QStringList{"songs","albums","artists"}.contains(value)||lane!="catalogue-"+value))return -1;
        }else if(operation=="resolveCatalogueItem"){
            const auto item=payload.value("item");
            if(lane!="catalogue-item"||value.isEmpty()||payload.size()!=1
                ||item.metaType().id()!=QMetaType::QVariantMap||item.toMap().value("id").toString()!=value
                ||!QStringList{"songs","albums","artists"}.contains(item.toMap().value("kind").toString()))return -1;
        }else if(operation=="resolveCatalogueArt"){
            const auto items=payload.value("items");
            if((exploreArt ? lane!=(editorialArt ? "explore-art" : "explore-art-lastfm") : heroArt ? lane!="home-hero-art"
                         : lane!="catalogue-art"||(value!="lastfm"&&value!="kworb"))
                ||payload.size()!=1||items.metaType().id()!=QMetaType::QVariantList||items.toList().size()>12)return -1;
        }else if(lane.startsWith("catalogue-")||lane=="home-hero-art")return -1;
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
            &&QStringList{"vinyl","lastfm","jiosaavn"}.contains(payload.value("catalogueSource").toString());
        const bool discoverResolver=operation=="resolveCatalogueItem"||operation=="resolveCatalogueArt";
        const bool chartArt=operation=="resolveCatalogueArt"&&(value=="kworb"||editorialArt||heroArt);
        const bool cacheable=!warming&&!editorial&&operation!="resolve"&&operation!="resolveVideo"&&!discoverBrowse&&!discoverResolver;
        if(home&&!metadata){
            const QSet<QString> allowed{"seed","recording","refresh","region","fetchedAt","limit"};
            for(auto it=payload.cbegin();it!=payload.cend();++it)if(!allowed.contains(it.key()))return -1;
            const QSet<QString> seedFields{"identity","mbid","kind","provider","providerId","id","key","name","title","image","cover","mappingStatus","canonicalMbid","legacyId","mapping"};
            const auto seed=payload.value("seed").toMap();for(auto it=seed.cbegin();it!=seed.cend();++it)if(!seedFields.contains(it.key()))return -1;
        }
        QJsonObject input=QJsonObject::fromVariantMap(payload);input["operation"]=operation;input["value"]=value;
        const auto requestBytes=QJsonDocument(input).toJson(QJsonDocument::Compact);if(requestBytes.size()>(home?256*1024:1024*1024))return -1;
        // Only accepted foreground work may preempt optional startup preparation.
        if(!warming&&(operation=="resolve"||operation=="resolveVideo")){
            m_suppressReplacement=true;
            cancel("media-warmup");
            m_suppressReplacement=false;
        }
        auto cacheInput=input;cacheInput.remove("refresh");if(home)cacheInput["cacheSchema"]=2;
        const auto key=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(cacheInput).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        const auto path=m_cacheRoot+"/"+key+".json";
        WarmSlot *warm=nullptr;
        if(warming||operation=="resolve"||operation=="resolveVideo")warm=&m_mediaWarm;
        else if(operation=="lyrics")warm=&m_lyricsWarm;
        else if(home)warm=homeWorker();
        else if(lane.startsWith("home-listening-"))warm=&m_homeListeningWarm;
        else if(QStringList{"search","catalogueSearch","artist","artistReleases","artistVideos","artistArtwork"}.contains(operation))
            warm=&m_browseWarm;
        else if(chartArt||QStringList{"browseCategories","browseCatalogue","resolveCatalogueItem","cataloguePage","album","relatedVideo"}.contains(operation))
            warm=&m_catalogueWarm;
        // Home cannot occupy a foreground warm worker. Overlap beyond the
        // warm pool and external helpers retain the one-shot contract.
        const bool server=warm&&!warm->busy&&reuseHelpers();
        // Bundled enrichment can outlive the reply even without a free warm slot.
        // While a transient art worker drains, later one-shot polls only read,
        // rather than replacing that worker on every timer tick.
        const bool artReader=!server&&chartArt&&m_tagging.contains(lane);
        const bool snapshotEnrichment=discoverBrowse&&payload.value("catalogueSource")=="vinyl"
            &&QStringList{"chartWeek","new"}.contains(payload.value("list").toString());
        const bool transientTagger=!server&&(snapshotEnrichment||(chartArt&&!artReader))
            &&QFileInfo(m_helper).fileName()=="provider.py";
        const bool lineProtocol=server||transientTagger;
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
            if(artReader)environment.insert("COLOSSEUM_VINYL_ART_POLL_ONLY","1");
            process->setProcessEnvironment(environment);
        }
        m_active.insert(lane,process);auto output=std::make_shared<QByteArray>();
        struct Timing {
            QElapsedTimer elapsed;
            qint64 dispatch=-1, started=-1;
            QJsonObject helper;
        };
        auto timing=std::make_shared<Timing>();
        timing->elapsed.start();
        auto *deadline=new QTimer(process);deadline->setSingleShot(true);
        const bool reused=server&&process->state()==QProcess::Running;
        const bool traceEnabled=qEnvironmentVariableIntValue("COLOSSEUM_CATALOGUE_TRACE")==1;
        const auto trace=[traceEnabled,lane,operation,id,reused](const QString &phase){
            if(!traceEnabled)return;
            qInfo().noquote()<<"[vinyl-provider]"<<QJsonDocument(QJsonObject{{"phase",phase},{"operation",operation},
                {"lane",lane},{"requestId",id},{"reused",reused}}).toJson(QJsonDocument::Compact);
        };
        const bool homeLane=home||lane.startsWith("home-listening-");
        const auto incompleteChart=[operation](const QVariantMap &map,const QString &error){
            return operation=="chartCandidates"&&(!error.isEmpty()||map.value("status")!="ready"
                ||!map.value("complete").toBool()||map.value("items").toList().isEmpty());
        };
        auto publish=[guard=QPointer<VinylProvider>(this),this,process,lane,id,home,homeLane,cacheable,path,key,operation,value,payload,timing,warm,server,deadline,reused,warming,traceEnabled,incompleteChart,transientTagger]
            (QVariant data,QString error,bool cleanReply,bool invalidEnvelope,QVariantMap fallback){
            if(!guard||m_active.value(lane)!=process)return;
            const auto received=timing->elapsed.elapsed();
            const bool ran=timing->dispatch>=0;
            releaseMusicBrainz(process);
            deadline->stop();deadline->deleteLater();
            m_active.remove(lane);m_homeRunning.remove(process);process->disconnect(this);
            if(server){
                warm->busy=false;++warm->idleGeneration;
                if((cleanReply||error.isEmpty())&&process->state()==QProcess::Running){
                    idleWorker(warm,process);
                }else{
                    if(warm->process==process)warm->process=nullptr;
                    retireProcess(process);
                    if(!warming&&(!error.isEmpty()||cleanReply))replaceWorker(warm);
                }
            }else if(transientTagger&&cleanReply&&process->state()==QProcess::Running
                     &&(data.toMap().value("genresPending").toBool()||data.toMap().value("artPending").toBool())){
                if(auto *previous=m_tagging.take(lane))retireProcess(previous);
                m_tagging.insert(lane,process);
                connect(process,&QProcess::readyReadStandardError,process,[process]{process->readAllStandardError();});
                connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process,lane]{
                    if(m_tagging.value(lane)==process)m_tagging.remove(lane);
                    process->deleteLater();
                });
                QTimer::singleShot(m_catalogueIdleMs,this,[this,tagger=QPointer<QProcess>(process),lane]{
                    if(tagger&&m_tagging.value(lane)==tagger.data()){
                        m_tagging.remove(lane);
                        retireProcess(tagger.data());
                    }
                });
            }else{
                retireProcess(process);
            }
            const auto map=data.toMap();
            if(home&&ran&&map.value("sourceStatus")=="rate_limited"){
                const auto retry=qBound(1,map.value("retryAfterSeconds",60).toInt(),3600);
                m_deferred[service(operation,value,payload)]=m_clock()+qint64(retry)*1000;
            }
            const bool unavailable=home&&map.value("status")=="unavailable";
            if(cacheable){
                if(incompleteChart(map,error)){
                    // A failed, empty or truncated refresh cannot replace a good
                    // chart week. Its original timestamp remains authoritative.
                    auto cached=std::move(fallback);
                    if(!cached.value("items").toList().isEmpty()){
                        cached["_cached"]=true;cached["_stale"]=true;
                        cached["_cacheError"]=error.isEmpty()?map.value("error","The chart refresh was incomplete.").toString():error;
                        data=cached;error.clear();
                    }
                }else if(error.isEmpty()&&!unavailable&&!map.value("_cached").toBool()){
                    // Home writes and the accompanying directory scan have
                    // already run in the reply worker.
                    if(!homeLane)writeProviderCache(m_cacheRoot,m_cacheBudget,path,QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));
                }else if(!error.isEmpty()||unavailable){
                    auto cached=homeLane?std::move(fallback):readCache(path,key,operation,value,payload,home,false);
                    if(!cached.isEmpty()){cached["_cached"]=true;cached["_stale"]=true;cached["_cacheError"]=error.isEmpty()?map.value("error","The source is unavailable.").toString():error;data=cached;error.clear();}
                }
            }
            const auto result=data.toMap();
            const auto count=operation=="resolveCatalogueArt"?result.value("covers").toMap().size()
                :data.metaType().id()==QMetaType::QVariantList?data.toList().size()
                :result.value("items",result.value("tracks",result.value("albums"))).toList().size();
            QJsonObject replyTrace{{"operation",operation},{"lane",lane},
                {"requestId",id},{"cacheHit",result.value("_cached").toBool()},{"stale",result.value("_stale").toBool()},
                {"workerReused",reused},{"elapsedMs",timing->elapsed.elapsed()},{"count",count},{"ok",error.isEmpty()&&result.value("status")!="unavailable"}};
            if(operation=="browseCatalogue"&&payload.value("catalogueSource")=="vinyl"){
                replyTrace["source"]="vinyl";
                replyTrace["stale"]=result.value("stale").toBool();
            }
            if(operation=="resolveCatalogueArt")replyTrace["source"]=value;
            if(traceEnabled){
                replyTrace["dispatched"]=ran;
                if(ran){
                    replyTrace["queueMs"]=timing->dispatch;
                    replyTrace["dispatchToReplyMs"]=received-timing->dispatch;
                    if(timing->started>=0)replyTrace["processStartMs"]=timing->started-timing->dispatch;
                    for(const auto &field:{"helperMs","httpMs","providerCallMs","musicBrainzSleepMs"}){
                        const auto n=timing->helper.value(field).toInteger(-1);
                        if(n>=0)replyTrace[field]=n;
                    }
                }
                if(home){
                    // Report the failure class before stale-cache fallback, never the upstream message or seed.
                    replyTrace["invalidEnvelope"]=invalidEnvelope;
                    replyTrace["rateLimited"]=map.value("sourceStatus")=="rate_limited";
                }
            }
            qInfo().noquote()<<"[vinyl-provider]"<<QJsonDocument(replyTrace).toJson(QJsonDocument::Compact);
            emit completed(lane,id,data,error);
            if(guard)QTimer::singleShot(0,this,[this]{startWaiting();});
        };
        auto finishing=std::make_shared<bool>(false);
        auto finish=[this,process,lane,deadline,home,homeLane,cacheable,path,key,operation,value,payload,publish,incompleteChart,finishing]
            (QVariant data,QString error,bool cleanReply=false){
            if(m_active.value(lane)!=process||*finishing)return;
            *finishing=true;deadline->stop();
            if(!homeLane){publish(data,error,cleanReply,false,{});return;}
            struct Reply {QVariant data;QString error;bool invalid=false;QVariantMap fallback;};
            vinyl::backgroundRead(process,
                [data=std::move(data),error=std::move(error),home,cacheable,path,key,operation,value,payload,incompleteChart,
                 now=m_clock(),root=m_cacheRoot,budget=m_cacheBudget]() mutable {
                    Reply reply{std::move(data),std::move(error)};
                    if(home&&reply.error.isEmpty()){
                        QVariantMap normalized;
                        if(!normalizeHome(operation,value,payload,reply.data,&normalized)){
                            reply.data={};reply.error="The recommendation response is invalid.";reply.invalid=true;
                        }else reply.data=normalized;
                    }
                    const auto map=reply.data.toMap();
                    if(!cacheable)return reply;
                    const bool unavailable=home&&map.value("status")=="unavailable";
                    if(incompleteChart(map,reply.error)||!reply.error.isEmpty()||unavailable)
                        reply.fallback=readCacheAt(path,key,operation,value,payload,home,false,now);
                    else if(!map.value("_cached").toBool()){
                        const bool positive=!map.value("items").toList().isEmpty()||map.value("status")=="verified"
                            ||!map.value("artist").toMap().value("releaseGroups").toList().isEmpty();
                        if(!home||positive||(map.value("complete").toBool()&&map.value("status")!="partial")){
                            auto wire=QJsonObject::fromVariantMap(map);
                            if(home)wire={{"version",2},{"requestKey",key},{"storedAtMs",now},{"ttlMs",ttl(operation,map)},{"data",wire}};
                            const auto bytes=home?QJsonDocument(wire).toJson(QJsonDocument::Compact):QJsonDocument::fromVariant(reply.data).toJson(QJsonDocument::Compact);
                            writeProviderCache(root,budget,path,bytes);
                        }
                    }
                    return reply;
                },[publish,cleanReply](Reply reply){publish(reply.data,reply.error,cleanReply,reply.invalid,std::move(reply.fallback));});
        };
        connect(deadline,&QTimer::timeout,this,[finish]{finish({},"The music catalogue took too long. Retry when connected.");});
        connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){
            finish({},error==QProcess::FailedToStart?"The music catalogue runtime is unavailable. Local music is still available.":"The music catalogue helper failed.");
        });
        connect(process,&QProcess::readyReadStandardError,this,[process]{process->readAllStandardError();});
        auto parseReply=[this,finish,operation,timing,traceEnabled,discoverBrowse](const QByteArray &bytes){
            QJsonParseError parse;
            const auto doc=QJsonDocument::fromJson(bytes,&parse);
            const auto object=doc.object();
            if(traceEnabled)timing->helper=object.value("timing").toObject();
            if(parse.error!=QJsonParseError::NoError||!object.value("ok").isBool()
                ||(object.value("ok").toBool()?!object.contains("data"):!object.value("error").isString()))
                finish({},"The music catalogue helper failed.");
            else if(!object.value("ok").toBool()){
                if(operation=="lyrics"&&object.value("errorCode")=="rate_limited")
                    m_deferred["lrclib"]=m_clock()+qint64(qBound(1,object.value("retryAfterSeconds").toInt(60),3600))*1000;
                if(discoverBrowse&&object.value("errorCode")=="catalogue_key_missing"&&!m_catalogueKeyMissingLogged){
                    m_catalogueKeyMissingLogged=true;
                    qWarning("Vinyl's catalogue key is missing");
                }
                finish({},object.value("error").toString("The music catalogue is unavailable."),true);
            }else finish(object.value("data").toVariant(),{},true);
        };
        connect(process,&QProcess::readyReadStandardOutput,this,[process,output,finish,parseReply,lineProtocol]{
            output->append(process->readAllStandardOutput());
            if(output->size()>4*1024*1024+(lineProtocol?1:0)){
                finish({},"The catalogue response exceeded its limit.");return;
            }
            if(lineProtocol){
                const auto newline=output->indexOf('\n');
                if(newline>=0){
                    if(newline!=output->size()-1)finish({},"The music catalogue helper failed.");
                    else parseReply(output->left(newline));
                }
            }
        });
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
            [process,output,finish,parseReply,lineProtocol](int code,QProcess::ExitStatus status){
                output->append(process->readAllStandardOutput());
                if(code!=0||status!=QProcess::NormalExit||output->size()>4*1024*1024+(lineProtocol?1:0)){
                    finish({},"The music catalogue helper failed.");return;
                }
                if(lineProtocol){
                    const auto newline=output->indexOf('\n');
                    if(newline<0||newline!=output->size()-1)finish({},"The music catalogue helper failed.");
                    else parseReply(output->left(newline));
                }else parseReply(*output);
            });
        connect(process,&QProcess::started,this,[trace,timing]{
            timing->started=timing->elapsed.elapsed();
            trace("process-started");
        });
        const bool catalogueObject=QStringList{"browseCategories","browseCatalogue","cataloguePage","catalogueSearch","artist","artistReleases","album","artistVideos","artistIdentity"}.contains(operation);
        const bool checkCache=cacheable&&(home||operation=="discover"||operation=="lyrics"||catalogueObject)&&!payload.value("refresh").toBool();
        if(checkCache&&!homeLane){
            const auto cached=readCache(path,key,operation,value,payload,home,true);
            const bool usable=!cached.isEmpty()&&(operation!="artistIdentity"
                ||(cached.value("source")=="musicbrainz"&&cached.value("complete").toBool()
                    &&!QUuid(cached.value("mbid").toString()).isNull()));
            if(usable){QTimer::singleShot(0,process,[finish,cached]{auto map=cached;map["_cached"]=true;finish(map,{});});return id;}
        }
        if(operation=="lyrics"&&m_deferred.value("lrclib")>m_clock()){
            QTimer::singleShot(0,process,[finish]{finish({},"Lyrics are temporarily rate limited. Retry later.");});return id;
        }
#ifdef Q_OS_WIN
        const auto bundled=QFileInfo(m_helper).absolutePath()+"/runtime/python.exe";
#else
        const auto bundled=QFileInfo(m_helper).absolutePath()+"/runtime/bin/python3";
#endif
        const auto python=qEnvironmentVariable("COLOSSEUM_VINYL_PYTHON",bundled);
        const bool musicBrainz=QStringList{"artistIdentity","recommendationIdentity","recommendationDiscography"}.contains(operation);
        auto launch=[guard=QPointer<VinylProvider>(this),this,process,deadline,lane,python,requestBytes,operation,value,payload,home,lineProtocol,transientTagger,trace,finish,timing,reused]{
            if(!guard||m_active.value(lane)!=process)return;
            // A source may enter backoff while this accepted request waits for
            // a worker slot. Recheck at the point of actual dispatch as well.
            const auto deferred=home?deferredHome(operation,value,payload):QVariantMap();
            if(!deferred.isEmpty()){finish(deferred,{});return;}
            timing->dispatch=timing->elapsed.elapsed();
            if(reused)timing->started=timing->dispatch;
            trace("dispatch");
            if(home)m_homeRunning.insert(process);
            if(home)deadline->start(operation=="artistCandidates"?8000:operation=="recommendationDiscography"?30000
                :operation=="recommendationIdentity"||operation=="songCandidates"?20000:12000);
            else deadline->start(operation=="lyrics"?12000:operation=="discover"?18000:operation=="artistIdentity"?180000:90000);
            if(lineProtocol){
                if(process->state()!=QProcess::Running)process->start(python,{"-I",m_helper,"--serve"});
                if(guard&&m_active.value(lane)==process){
                    process->write(requestBytes+"\n");
                    if(transientTagger)process->closeWriteChannel();
                }
            }else{
                process->start(python,{"-I",m_helper});
                if(guard&&m_active.value(lane)==process){process->write(requestBytes);process->closeWriteChannel();}
            }
        };
        auto dispatch=[this,process,lane,launch,finish,home,musicBrainz,operation,value,payload](QVariantMap cached){
            if(m_active.value(lane)!=process)return;
            if(!cached.isEmpty()){cached["_cached"]=true;finish(cached,{});return;}
            const auto deferred=home?deferredHome(operation,value,payload):QVariantMap();
            if(!deferred.isEmpty()){QTimer::singleShot(0,process,[finish,deferred]{finish(deferred,{});});return;}
            if(home||musicBrainz){
                QueuedWork job{QPointer<QProcess>(process),launch,musicBrainz,home};
                // Foreground identity gets the next eligible MusicBrainz slot.
                if(home)m_waiting.enqueue(std::move(job));
                else m_waiting.prepend(std::move(job));
                QTimer::singleShot(0,this,[this]{startWaiting();});
            }else QTimer::singleShot(0,process,launch);
        };
        if(checkCache&&homeLane){
            vinyl::backgroundRead(process,[path,key,operation,value,payload,home,now=m_clock()]{
                return readCacheAt(path,key,operation,value,payload,home,true,now);
            },std::move(dispatch));
        }else dispatch({});
        return id;
    }
    Q_INVOKABLE void cancel(const QString &lane){
        auto *process=m_active.take(lane);if(!process)return;m_homeRunning.remove(process);releaseMusicBrainz(process);
        for(auto *slot:warmSlots()){
            if(slot->process==process){
                slot->process=nullptr;
                slot->busy=false;
                ++slot->idleGeneration;
                replaceWorker(slot);
            }
        }
        retireProcess(process);QTimer::singleShot(0,this,[this]{startWaiting();});
    }
    Q_INVOKABLE void cancelAll(){
        m_suppressReplacement=true;
        m_waiting.clear();m_waitingWake->stop();for(const auto &lane:m_active.keys())cancel(lane);
        for(auto *process:m_tagging)retireProcess(process);
        m_tagging.clear();
        for(auto *slot:warmSlots()){
            auto *process=slot->process.data();
            slot->process=nullptr;slot->busy=false;++slot->idleGeneration;
            if(process)retireProcess(process);
        }
        m_suppressReplacement=false;
    }
signals:
    void completed(const QString &lane,int requestId,const QVariant &data,const QString &error);
    void profileLeaving();void profileEntered();void checkpointRequested();void shuttingDown();
private:
    QHash<QString,QProcess*> m_tagging;
    bool m_catalogueKeyMissingLogged=false;
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
    std::array<WarmSlot*,7> warmSlots(){
        return {&m_mediaWarm,&m_lyricsWarm,&m_catalogueWarm,&m_browseWarm,&m_homeListeningWarm,&m_homeWarm[0],&m_homeWarm[1]};
    }
    WarmSlot *homeWorker(){
        for(auto &slot:m_homeWarm)
            if(!slot.busy&&slot.process&&slot.process->state()==QProcess::Running)return &slot;
        for(auto &slot:m_homeWarm)
            if(!slot.busy)return &slot;
        return nullptr;
    }
    void idleWorker(WarmSlot *slot,QProcess *process,bool replaceOnExit=true){
        const auto generation=slot->idleGeneration;
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
            [this,slot,process,replaceOnExit](int,QProcess::ExitStatus){
                if(slot->process!=process||slot->busy)return;
                slot->process=nullptr;
                ++slot->idleGeneration;
                process->deleteLater();
                if(replaceOnExit)replaceWorker(slot);
            });
        QTimer::singleShot(slot==&m_mediaWarm?m_mediaIdleMs:m_catalogueIdleMs,this,[this,slot,process,generation]{
            if(slot->process!=process||slot->busy||slot->idleGeneration!=generation)return;
            slot->process=nullptr;
            ++slot->idleGeneration;
            retireProcess(process);
        });
    }
    void replaceWorker(WarmSlot *slot){
        // Restart Home on demand, not after a cancelled/failed background job.
        if(m_suppressReplacement||!reuseHelpers()||slot==&m_homeWarm[0]||slot==&m_homeWarm[1]||slot==&m_homeListeningWarm)return;
        const auto generation=slot->idleGeneration;
        QTimer::singleShot(0,this,[this,slot,generation]{
            if(slot->idleGeneration!=generation||slot->process||m_suppressReplacement||!reuseHelpers())return;
            if(slot==&m_mediaWarm){
                warmPlayback();
                return;
            }
            auto *process=new QProcess(this);
            slot->process=process;
            auto environment=QProcessEnvironment::systemEnvironment();
            environment.insert("COLOSSEUM_VINYL_DISCOVER_CACHE_DIR",QDir(m_cacheRoot).filePath("discover-v1"));
            process->setProcessEnvironment(environment);
            connect(process,&QProcess::errorOccurred,this,[this,slot,process](QProcess::ProcessError){
                if(slot->process!=process||slot->busy)return;
                slot->process=nullptr;
                ++slot->idleGeneration;
                retireProcess(process);
            });
            connect(process,&QProcess::readyReadStandardError,this,[process]{process->readAllStandardError();});
            idleWorker(slot,process,false);
#ifdef Q_OS_WIN
            const auto bundled=QFileInfo(m_helper).absolutePath()+"/runtime/python.exe";
#else
            const auto bundled=QFileInfo(m_helper).absolutePath()+"/runtime/bin/python3";
#endif
            process->start(qEnvironmentVariable("COLOSSEUM_VINYL_PYTHON",bundled),{"-I",m_helper,"--serve"});
        });
    }
    static QString service(const QString &op,const QString &value,const QVariantMap &payload){
        return VinylSourcePool::metadataOperation(op)?VinylSourcePool::metadataSource(op,value,payload)
            :op=="recommendationIdentity"?"musicbrainz":op=="populationCandidates"?"listenbrainz":"youtube-music";
    }
    QVariantMap deferredHome(const QString &op,const QString &value,const QVariantMap &payload)const{
        const auto source=service(op,value,payload);
        const qint64 remaining=m_deferred.value(source)-m_clock();
        if(remaining<=0)return {};
        return {{"version",1},{"seed",payload.value("seed",QVariantMap())},{"items",QVariantList()},
            {"provenance",QVariantMap{{"source",source}}},{"fetchedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"complete",false},{"status","unavailable"},{"sourceStatus","rate_limited"},
            {"retryAfterSeconds",int(qBound<qint64>(qint64(1),(remaining+999)/1000,qint64(3600)))},
            {"error","The source is temporarily rate limited."}};
    }
    static qint64 ttl(const QString &op,const QVariantMap &data){
        if(VinylSourcePool::metadataOperation(op)&&data.value("status")=="partial")return 3600000;
        if(data.value("items").toList().isEmpty()&&data.value("status")!="verified"
            &&data.value("artist").toMap().value("releaseGroups").toList().isEmpty())return 3600000;
        if(VinylSourcePool::metadataOperation(op))return 86400000;
        return op=="recommendationIdentity"?7LL*86400000:op=="populationCandidates"?86400000:6LL*3600000;
    }
    QVariantMap readCache(const QString &path,const QString &key,const QString &op,const QString &value,const QVariantMap &payload,bool home,bool fresh)const{
        return readCacheAt(path,key,op,value,payload,home,fresh,home?m_clock():0);
    }
    static QVariantMap readCacheAt(const QString &path,const QString &key,const QString &op,const QString &value,const QVariantMap &payload,bool home,bool fresh,qint64 now){
        QFile file(path);const QFileInfo info(file);if(info.size()>4*1024*1024||!file.open(QIODevice::ReadOnly))return {};
        const auto doc=QJsonDocument::fromJson(file.readAll());if(!doc.isObject())return {};
        if(!home){const auto age=info.lastModified().secsTo(QDateTime::currentDateTime());if(fresh&&(age<0||age>900))return {};return doc.object().toVariantMap();}
        const auto wire=doc.object();if(wire.value("version").toInt()!=2||wire.value("requestKey")!=key||!wire.value("data").isObject())return {};
        if(!wire.value("storedAtMs").isDouble()||wire.value("storedAtMs").toInteger()<=0||!wire.value("ttlMs").isDouble())return {};
        const auto age=now-wire.value("storedAtMs").toInteger();
        if(fresh&&(age<0||age>wire.value("ttlMs").toInteger()))return {};
        QVariantMap data;if(!normalizeHome(op,value,payload,wire.value("data").toVariant(),&data)||wire.value("ttlMs").toInteger()!=ttl(op,data))return {};return data;
    }
    static void writeProviderCache(const QString &root,qint64 budget,const QString &path,const QByteArray &bytes){
        if(bytes.size()>4*1024*1024)return;
        QDir().mkpath(root);QSaveFile file(path);
        if(file.open(QIODevice::WriteOnly)){file.write(bytes);file.commit();}
        pruneCache(root,budget);
    }
    void prune(){pruneCache(m_cacheRoot,m_cacheBudget);}
    static void pruneCache(const QString &path,qint64 budget){
        QDir root(path);auto files=root.entryInfoList({"*.json"},QDir::Files|QDir::NoSymLinks,QDir::Time|QDir::Reversed);
        qint64 total=0;for(const auto &file:files)total+=file.size();int remaining=files.size();
        for(const auto &file:files){if(total<=budget&&remaining<=4096)break;
            // Another reply may have already pruned this entry.
            if(QFile::remove(file.absoluteFilePath())||!QFileInfo::exists(file.absoluteFilePath())){total-=file.size();--remaining;}
        }
    }
    struct QueuedWork {QPointer<QProcess> process;std::function<void()> launch;bool musicBrainz;bool home;};
    struct MusicBrainzGate {QMutex mutex;QProcess *active=nullptr;qint64 notBefore=0;};
    static MusicBrainzGate &musicBrainzGate(){static MusicBrainzGate gate;return gate;}
    static qint64 monotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    static bool claimMusicBrainz(QProcess *process,int *retryMs){
        auto &gate=musicBrainzGate();QMutexLocker lock(&gate.mutex);const auto now=monotonicMs();
        if(gate.active||now<gate.notBefore){*retryMs=gate.active?1100:int(gate.notBefore-now);return false;}
        gate.active=process;return true;
    }
    static void releaseMusicBrainz(QProcess *process){
        auto &gate=musicBrainzGate();QMutexLocker lock(&gate.mutex);
        if(gate.active==process){gate.active=nullptr;gate.notBefore=monotonicMs()+1100;}
    }
    void startWaiting(){
        QPointer<VinylProvider> guard(this);int retry=0;
        for(qsizetype index=0;index<m_waiting.size();) {
            const auto &job=m_waiting.at(index);
            if(!job.process||!m_active.values().contains(job.process.data())){
                m_waiting.removeAt(index);
                continue;
            }
            // The two-job budget is for Home, not foreground artist identity.
            // Foreground still waits for an in-flight MusicBrainz job and its
            // cooldown; no queued Home identity can jump ahead of it.
            if(job.home&&m_homeRunning.size()>=2)break; // Foreground is prepended.
            // Leave gated jobs in order while unrelated Home work can run.
            if(job.musicBrainz&&(retry||!claimMusicBrainz(job.process.data(),&retry))){
                ++index;
                continue;
            }
            auto ready=m_waiting.takeAt(index);
            ready.launch();if(!guard)return;
        }
        if(retry&&!m_waiting.isEmpty()&&(!m_waitingWake->isActive()||m_waitingWake->remainingTime()>retry))m_waitingWake->start(qMax(1,retry));
    }
    QString m_helper;int m_serial=0;QHash<QString,QProcess*> m_active;
    qint64 m_cacheBudget;std::function<qint64()> m_clock;QString m_cacheRoot;
    QSet<QProcess*> m_homeRunning;QQueue<QueuedWork> m_waiting;QHash<QString,qint64> m_deferred;
    QTimer *m_waitingWake=nullptr;
    WarmSlot m_mediaWarm,m_lyricsWarm,m_catalogueWarm,m_browseWarm,m_homeListeningWarm;
    std::array<WarmSlot,2> m_homeWarm;
    bool m_suppressReplacement=false;
    const int m_mediaIdleMs,m_catalogueIdleMs;
};
