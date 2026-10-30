#pragma once
#include <QObject>
#include <QProcess>
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
#include <functional>
#include <memory>
#include "VinylRecommendationIdentity.h"
#include "RecommendationEntityRef.h"

// Public metadata uses the existing app cache. Resolved audio never enters it.
class VinylProvider : public QObject {
    Q_OBJECT
public:
    explicit VinylProvider(QString helper,QObject *parent=nullptr,qint64 cacheBudgetBytes=128LL*1024*1024,
        std::function<qint64()> clock={},QString cacheRoot={}):QObject(parent),m_helper(std::move(helper)),
        m_cacheBudget(qBound<qint64>(1,cacheBudgetBytes,128LL*1024*1024)),
        m_clock(clock?std::move(clock):std::function<qint64()>([]{return QDateTime::currentMSecsSinceEpoch();})),
        m_cacheRoot(cacheRoot.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/vinyl/catalogue":std::move(cacheRoot)) {}
    ~VinylProvider() override {cancelAll();}
    static bool homeOperation(const QString &op){return QStringList{"artistCandidates","recommendationIdentity","recordingMatch","populationCandidates"}.contains(op);}
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
        const QSet<QString> itemFields{"kind","provider","providerId","id","title","name","image","cover","relation","artistRefs","artists","year","releaseDate","dateSource","releaseType","seconds","videoId","mapping","mappingStatus","playbackMatch","ref","key","destination","available","artistMbid","artistName","recordingMbid","seedArtistRef","version"};
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
                const auto converted=VinylRecommendationIdentity::fromCatalogue(item);
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
    Q_INVOKABLE int requestPayload(const QString &lane,const QString &operation,const QString &value,const QVariantMap &payload){
        static const QStringList operations{"search","catalogueSearch","cataloguePage","playlistPage","discover","playlist","album","resolve","artist","artistReleases","artistVideos","artistIdentity","artistArtwork","artistCandidates","recommendationIdentity","recordingMatch","populationCandidates"};
        static const QStringList lanes{"browse","queue","play","playFallback","playlist-page","search-albums","search-artists","search-songs","search-playlists","taste-artists","discover-new-releases","discover-trending","discover-artists","discover-charts","artist","artist-albums","artist-singles","artist-videos","artist-identity","artist-artwork","home-seed-0","home-seed-1","home-identity","home-population","named-collections"};
        if(!operations.contains(operation)||!lanes.contains(lane)||value.size()>300)return -1;
        // Collections may read exact album metadata, never resolve playback or
        // take ownership of an existing browse/play/queue request.
        if(lane=="named-collections" && operation!="album")return -1;
        cancel(lane);const int id=++m_serial;const bool home=homeOperation(operation),cacheable=operation!="resolve";
        if(home){
            const QSet<QString> allowed{"seed","recording","refresh","region","fetchedAt","limit"};
            for(auto it=payload.cbegin();it!=payload.cend();++it)if(!allowed.contains(it.key()))return -1;
            const QSet<QString> seedFields{"identity","mbid","kind","provider","providerId","id","key","name","title","image","cover","mappingStatus","canonicalMbid","legacyId","mapping"};
            const auto seed=payload.value("seed").toMap();for(auto it=seed.cbegin();it!=seed.cend();++it)if(!seedFields.contains(it.key()))return -1;
        }
        QJsonObject input=QJsonObject::fromVariantMap(payload);input["operation"]=operation;input["value"]=value;
        const auto requestBytes=QJsonDocument(input).toJson(QJsonDocument::Compact);if(requestBytes.size()>(home?256*1024:1024*1024))return -1;
        auto cacheInput=input;cacheInput.remove("refresh");if(home)cacheInput["cacheSchema"]=2;
        const auto key=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(cacheInput).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        const auto path=m_cacheRoot+"/"+key+".json";
        auto *process=new QProcess(this);m_active.insert(lane,process);auto output=std::make_shared<QByteArray>();
        auto finish=[this,process,lane,id,home,cacheable,path,key,operation,value,payload](QVariant data,QString error){
            if(m_active.value(lane)!=process)return;
            const bool ran=m_running.contains(process);
            m_active.remove(lane);m_running.remove(process);process->disconnect(this);
            if(process->state()!=QProcess::NotRunning)process->kill();process->deleteLater();
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
            emit completed(lane,id,data,error);
            QTimer::singleShot(0,this,[this]{startWaiting();});
        };
        auto *deadline=new QTimer(process);deadline->setSingleShot(true);
        connect(deadline,&QTimer::timeout,this,[finish]{finish({},"The music catalogue took too long. Retry when connected.");});
        connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish({},"The music catalogue runtime is unavailable. Local music is still available.");});
        connect(process,&QProcess::readyReadStandardError,this,[process]{process->readAllStandardError();});
        connect(process,&QProcess::readyReadStandardOutput,this,[process,output,finish]{output->append(process->readAllStandardOutput());if(output->size()>4*1024*1024)finish({},"The catalogue response exceeded its limit.");});
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[process,output,finish](int code,QProcess::ExitStatus status){
            output->append(process->readAllStandardOutput());QJsonParseError parse;const auto doc=QJsonDocument::fromJson(*output,&parse);const auto object=doc.object();
            if(output->size()>4*1024*1024||code!=0||status!=QProcess::NormalExit||parse.error!=QJsonParseError::NoError||!object.value("ok").isBool())finish({},"The music catalogue helper failed.");
            else if(!object.value("ok").toBool())finish({},object.value("error").toString("The music catalogue is unavailable."));
            else finish(object.value("data").toVariant(),{});
        });
        if((home||operation=="discover")&&!payload.value("refresh").toBool()){
            const auto cached=readCache(path,key,operation,value,payload,home,true);
            if(!cached.isEmpty()){QTimer::singleShot(0,process,[finish,cached]{auto map=cached;map["_cached"]=true;finish(map,{});});return id;}
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
        auto launch=[this,process,deadline,lane,python,requestBytes,operation,home]{
            if(m_active.value(lane)!=process)return;
            if(home)m_running.insert(process);
            else deadline->start(operation=="discover"?18000:operation=="artistIdentity"?180000:90000);
            process->start(python,{"-I",m_helper});
            if(m_active.value(lane)==process){process->write(requestBytes);process->closeWriteChannel();}
        };
        if(home){deadline->start(8000);m_waiting.enqueue({QPointer<QProcess>(process),launch});QTimer::singleShot(0,this,[this]{startWaiting();});}
        else QTimer::singleShot(0,process,launch);
        return id;
    }
    Q_INVOKABLE void cancel(const QString &lane){
        auto *process=m_active.take(lane);if(!process)return;m_running.remove(process);
        process->disconnect(this);process->kill();process->waitForFinished(1000);process->deleteLater();QTimer::singleShot(0,this,[this]{startWaiting();});
    }
    Q_INVOKABLE void cancelAll(){m_waiting.clear();for(const auto &lane:m_active.keys())cancel(lane);}
signals:
    void completed(const QString &lane,int requestId,const QVariant &data,const QString &error);
    void profileLeaving();void profileEntered();void checkpointRequested();void shuttingDown();
private:
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
};
