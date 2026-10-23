#include "VinylRecommendationService.h"
#include "VinylProvider.h"
#include "VinylRecommendationIdentity.h"
#include "DeterministicRanker.h"
#include "FeedbackTastePersistence.h"
#include "VinylRecommendationHistory.h"
#include "account/ActivityStore.h"
#include "account/HistoryStore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJSValue>
#include <QMap>
#include <QSet>
#include <QScopedValueRollback>
#include <QUuid>
#include <QJsonArray>
#include <algorithm>

using namespace vinyl;
using namespace vinyl::recommendation;
namespace {
QByteArray wire(const QVariant &value){return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);}
QString digest(const QVariant &value){return QString::fromLatin1(QCryptographicHash::hash(wire(value),QCryptographicHash::Sha256).toHex());}
QVariant plainHome(const QVariant &value){return value.metaType().id()==qMetaTypeId<QJSValue>()?value.value<QJSValue>().toVariant():value;}
void cancelLane(QObject *source,const QString &lane){
    if(!source)return;
    if(source->metaObject()->indexOfMethod("cancel(QString)")>=0)QMetaObject::invokeMethod(source,"cancel",Qt::DirectConnection,Q_ARG(QString,lane));
    else QMetaObject::invokeMethod(source,"cancel",Qt::DirectConnection,Q_ARG(QVariant,QVariant(lane)));
}
QVariantMap cachedHome(QObject *source,const QString &op,const QString &value,const QVariantMap &options){
    if(!source||source->metaObject()->indexOfMethod("cachedRecommendation(QString,QString,QVariantMap)")<0)return {};
    QVariantMap result;QMetaObject::invokeMethod(source,"cachedRecommendation",Qt::DirectConnection,Q_RETURN_ARG(QVariantMap,result),Q_ARG(QString,op),Q_ARG(QString,value),Q_ARG(QVariantMap,options));return result;
}
int requestHome(QObject *source,const QString &lane,const QString &op,const QString &value,const QVariantMap &options){
    if(!source)return -1;int id=-1;
    if(source->metaObject()->indexOfMethod("request(QString,QString,QString,QVariantMap)")>=0)
        QMetaObject::invokeMethod(source,"request",Qt::DirectConnection,Q_RETURN_ARG(int,id),Q_ARG(QString,lane),Q_ARG(QString,op),Q_ARG(QString,value),Q_ARG(QVariantMap,options));
    else{QVariant result;QMetaObject::invokeMethod(source,"request",Qt::DirectConnection,Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QVariant(lane)),Q_ARG(QVariant,QVariant(op)),Q_ARG(QVariant,QVariant(value)),Q_ARG(QVariant,QVariant(options)));if(result.isValid())id=result.toInt();}
    return id;
}
std::optional<RecommendationEntityRef> typed(const QVariantMap &row){
    const auto converted=VinylRecommendationIdentity::fromCatalogue(row);
    if(!converted.value("ok").toBool())return {};
    return RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(converted.value("ref").toMap())).value;
}
QVariantMap policy(){return {{"version","vinyl-home-v1"},{"seedAffinity",300},{"maxCandidates",300},{"maxGenerator",60},{"maxShelf",12},{"maxUncachedArtists",6},{"maxConcurrent",2},{"deadlineMs",8000},{"newWindowDays",90},{"maxArtist",3},{"maxRecordingReleaseGroup",2},{"historyComplete",false}};}
bool readExposure(const QString &scope,const QString &snapshot,qint64 now,quint64 reset,qint64 historyReset,
                  RecommendationExposureLog &log,QVariantList &retained) {
    if(snapshot.isEmpty())return true;
    const auto bytes=snapshot.toUtf8();if(bytes.size()>4*1024*1024 || snapshot.contains(QChar::Null))return false;
    const auto doc=QJsonDocument::fromJson(bytes);if(!doc.isObject())return false;
    const auto root=doc.object();
    if(root.value("version")!=2 || root.value("profileId").toString()!=scope || !root.value("records").isArray())return false;
    if(root.value("activityResetGeneration").toInteger(-1)!=qint64(reset)
        || root.value("historyResetGeneration").toInteger(-1)!=historyReset)return true;
    const auto rows=root.value("records").toArray();if(rows.size()>2000)return false;
    RecommendationExposureLog checked(log.profileId());QVariantList checkedRows;
    for(const auto &value:rows) {
        if(!value.isObject())return false;const auto row=value.toObject();
        const auto ref=RecommendationEntityRef::fromJson(row.value("entity").toObject());
        const auto id=RecommendationExposureId::parse(row.value("exposureId").toString());
        const auto evidence=ExposureEvidenceRef::create(ExposureEvidenceKind::CatalogueContext,"home:"+row.value("shelf").toString());
        const auto at=row.value("atMs").toInteger(-1);const auto rank=row.value("rankPosition").toInt(-1);
        if(!ref.ok() || !id.ok() || !evidence.ok() || row.value("shelf").toString().isEmpty())return false;
        const auto record=RecommendationExposureRecord::create(log.profileId(),*id.value,*ref.value,at,rank,row.value("configVersion").toString(),{*evidence.value});
        if(!record.ok())return false;
        if(at>now || at<now-14LL*24*3600000)continue;
        if(!checked.append(*record.value).ok)return false;checkedRows.append(row.toVariantMap());
    }log=std::move(checked);retained=std::move(checkedRows);return true;
}
}

struct VinylRecommendationService::HomeState {
    struct Job {QString operation,value,key;QVariantMap seed;bool force=false;};
    struct Active {int id;Job job;};
    QVariantMap collections,snapshot,history;
    QVariantList sections,seeds;
    QMap<QString,QVariantMap> evidence,identities,population;
    QMap<QString,Active> active;
    QList<Job> waiting;
    QSet<QString> failed,optionalUnavailable;
    QString seedHash,status="idle",error,visitId;
    std::function<qint64()> clock;
    QTimer *deadline=nullptr;
    qint64 evaluatedAt=0;
    quint64 epoch=0;
    int revision=0,cursor=0,nextCursor=0;
    bool visited=false,refreshed=false;
    bool persisting=false;
    quint64 activityReset=0;
    qint64 historyReset=0;
};

QVariantMap VinylRecommendationService::historySnapshot()const{return m_home->history;}
void VinylRecommendationService::setActivityStore(QObject *value) {
    if(m_activityStore==value)return;
    if(m_activityStore)disconnect(m_activityStore,nullptr,this,nullptr);m_activityStore=value;
    if(auto store=qobject_cast<ActivityStore*>(value)) {
        // Binding an already-reset owner is not a new privacy reset.
        m_home->activityReset=store->resetGeneration();
        connect(store,&ActivityStore::changed,this,&VinylRecommendationService::historyInputsChanged);
        connect(store,&ActivityStore::retentionEnabledChanged,this,[this]{historyInputsChanged();});
    }
    historyInputsChanged();emit stateChanged();
}
void VinylRecommendationService::setHistoryStore(QObject *value) {
    if(m_historyStore==value)return;
    if(m_historyStore)disconnect(m_historyStore,nullptr,this,nullptr);m_historyStore=value;
    if(auto store=qobject_cast<HistoryStore*>(value)){
        m_home->historyReset=store->syncResetGeneration();
        connect(store,&HistoryStore::changed,this,&VinylRecommendationService::historyInputsChanged);
    }
    historyInputsChanged();emit stateChanged();
}
void VinylRecommendationService::setKeepHistory(bool value) {
    if(m_keepHistory==value)return;m_keepHistory=value;historyInputsChanged();emit stateChanged();
}
bool VinylRecommendationService::storeExposure(const QString &snapshot) {
    if(!m_active || !m_preferences)return false;
    if(m_preferences->property("vinylRecommendationExposureSnapshot").toString()==snapshot)return true;
    bool saved=false;QScopedValueRollback<bool> guard(m_home->persisting,true);
    QMetaObject::invokeMethod(m_preferences,"setVinylRecommendationExposureSnapshot",Qt::DirectConnection,
        Q_RETURN_ARG(bool,saved),Q_ARG(QString,snapshot));return saved;
}
void VinylRecommendationService::historyInputsChanged() {
    if(!m_home || m_home->persisting || !m_active)return;
    const auto activity=qobject_cast<ActivityStore*>(m_activityStore.data());
    const auto history=qobject_cast<HistoryStore*>(m_historyStore.data());
    const auto activityReset=activity?activity->resetGeneration():0;
    const auto historyReset=history?history->syncResetGeneration():0;
    if(!m_keepHistory || (activity && !activity->retentionEnabled())
        || activityReset!=m_home->activityReset || historyReset!=m_home->historyReset)storeExposure({});
    m_home->activityReset=activityReset;m_home->historyReset=historyReset;
    if(m_home->visited && m_state=="answered") {m_home->evaluatedAt=m_home->clock();rebuildHome();}
}
bool VinylRecommendationService::setFeedback(const QVariantMap &raw,const QString &action,int generation) {
    if(!m_active || m_scope.isEmpty() || !m_preferences || generation!=m_generation)return false;
    const auto profile=ProfileScopeId::parse(m_scope);const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(raw));
    if(!profile.ok() || !ref.ok())return false;
    TasteProfileState taste(*profile.value);const auto snapshot=m_preferences->property("vinylFeedbackSnapshot").toString();
    if(!snapshot.isEmpty()){auto restored=FeedbackTastePersistence::restore(*profile.value,snapshot.toUtf8());if(!restored.ok())return false;taste=*restored.value;}
    quint64 revision=1;
    for(const auto &state:taste.states())revision=std::max({revision,state.explicitFeedback().revision()+1,
        state.suppression().hiddenRevision()+1,state.suppression().snoozeRevision()+1,state.suppression().doNotRecommendRevision()+1});
    const auto now=m_home->clock();TasteMutationStatus result;
    if(action=="like" || action=="dislike" || action=="neutral")result=taste.applyExplicitFeedback(*ref.value,
        action=="like"?ExplicitTasteFeedback::Positive:action=="dislike"?ExplicitTasteFeedback::Negative:ExplicitTasteFeedback::Neutral,revision,now);
    else if(action=="hide" || action=="unhide")result=taste.setHidden(*ref.value,action=="hide",revision,now);
    else if(action=="snooze-7d" || action=="unsnooze")result=taste.setSnoozedUntil(*ref.value,action=="snooze-7d"?std::optional<qint64>(now+7LL*24*3600000):std::nullopt,revision,now);
    else if((action=="exclude-artist" || action=="allow-artist") && ref.value->kind()==CanonicalEntityKind::Artist)
        result=taste.setDoNotRecommend(*ref.value,action=="exclude-artist",revision,now);
    else return false;
    if(!result.ok)return false;const auto captured=FeedbackTastePersistence::capture(taste);if(!captured.ok())return false;
    bool saved=false;{
        QScopedValueRollback<bool> guard(m_home->persisting,true);
        QMetaObject::invokeMethod(m_preferences,"setVinylFeedbackSnapshot",Qt::DirectConnection,Q_RETURN_ARG(bool,saved),Q_ARG(QString,QString::fromUtf8(*captured.value)));
    }
    if(saved){m_home->evaluatedAt=now;rebuildHome();}return saved;
}
bool VinylRecommendationService::recordVisibleItems(const QString &shelf,const QString &snapshot,const QVariantList &refs,int generation) {
    if(!m_active || generation!=m_generation || !m_keepHistory || !m_home->visited || refs.isEmpty() || refs.size()>12
        || snapshot!=m_home->snapshot.value("exposureSnapshotId").toString() || snapshot.isEmpty())return false;
    const auto activity=qobject_cast<ActivityStore*>(m_activityStore.data());
    if(!activity || !activity->retentionEnabled() || !activity->healthy())return false;
    const auto profile=ProfileScopeId::parse(m_scope);if(!profile.ok())return false;
    QVariantList items;
    for(const auto &section:m_home->sections)if(section.toMap().value("id")==shelf)items=section.toMap().value("items").toList();
    if(items.isEmpty())return false;
    const auto now=m_home->clock();RecommendationExposureLog log(*profile.value);QVariantList retained;
    const auto prior=m_preferences?m_preferences->property("vinylRecommendationExposureSnapshot").toString():QString();
    if(!readExposure(m_scope,prior,now,m_home->activityReset,m_home->historyReset,log,retained))return false;
    QSet<QString> existing;for(const auto &entry:retained)existing.insert(entry.toMap().value("exposureId").toString());
    for(const auto &raw:refs) {
        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(raw.toMap()));if(!ref.ok())return false;
        int rank=0;for(int i=0;i<items.size();++i)if(items[i].toMap().value("ref").toMap()==raw.toMap()){rank=i+1;break;}
        if(!rank)return false;
        const auto id="visible-"+digest(QVariantList{m_home->visitId,shelf,ref.value->stableKey()});
        if(existing.contains(id))continue;existing.insert(id);
        retained.append(QVariantMap{{"exposureId",id},{"entity",raw},{"atMs",now},{"rankPosition",rank},
            {"configVersion","vinyl-home-v1"},{"shelf",shelf}});
    }
    std::sort(retained.begin(),retained.end(),[](const auto &a,const auto &b){return a.toMap().value("atMs").toLongLong()<b.toMap().value("atMs").toLongLong();});
    if(retained.size()>2000)retained=retained.mid(retained.size()-2000);
    const auto captured=QString::fromUtf8(wire(QVariantMap{{"version",2},{"profileId",m_scope},{"records",retained},
        {"activityResetGeneration",QVariant::fromValue(m_home->activityReset)},{"historyResetGeneration",m_home->historyReset}}));
    if(captured==prior)return true;if(!storeExposure(captured))return false;
    m_home->evaluatedAt=now;rebuildHome();return true;
}

void VinylRecommendationService::initializeHome(std::function<qint64()> clock){
    m_home=std::make_shared<HomeState>();m_home->clock=clock?std::move(clock):std::function<qint64()>([]{return QDateTime::currentMSecsSinceEpoch();});
    m_home->deadline=new QTimer(this);m_home->deadline->setSingleShot(true);
    connect(m_home->deadline,&QTimer::timeout,this,[this]{
        auto incomplete=[this](const HomeState::Job &job){if(job.operation=="artistCandidates")m_home->failed.insert(job.key);else m_home->optionalUnavailable.insert(job.operation+":"+job.key);};
        for(const auto &active:m_home->active)incomplete(active.job);
        for(const auto &job:m_home->waiting)incomplete(job);
        cancelHome(false);if(!m_home->failed.isEmpty())m_home->error="Some catalogue enrichment did not finish.";rebuildHome();
    });
}
VinylRecommendationService::~VinylRecommendationService(){cancelHome(false);}
void VinylRecommendationService::setSource(QObject *value){
    if(m_source==value)return;const bool visited=m_home->visited;cancelSearch();cancelHome(true);
    if(m_source)disconnect(m_source,nullptr,this,nullptr);m_source=value;
    if(value)connect(value,SIGNAL(completed(QString,int,QVariant,QString)),this,SLOT(accept(QString,int,QVariant,QString)),Qt::QueuedConnection);
    m_home->visited=visited;homeSeedsChanged();emit stateChanged();
}
QVariantMap VinylRecommendationService::homeCollections() const{return m_home->collections;}
QVariantList VinylRecommendationService::homeSections() const{return m_home->sections;}
QString VinylRecommendationService::homeStatus() const{return m_home->status;}
QString VinylRecommendationService::homeError() const{return m_home->error;}
int VinylRecommendationService::homeRevision() const{return m_home->revision;}
QVariantMap VinylRecommendationService::homeSnapshot() const{return m_home->snapshot;}
void VinylRecommendationService::setHomeCollections(const QVariantMap &value){
    if(value==m_home->collections)return;m_home->collections=value;
    homeSeedsChanged();if(m_home->visited&&m_state=="answered")rebuildHome();emit homeChanged();
}
void VinylRecommendationService::cancelHome(bool clear){
    if(!m_home)return;++m_home->epoch;m_home->deadline->stop();
    const auto lanes=m_home->active.keys();m_home->active.clear();m_home->waiting.clear();for(const auto &lane:lanes)cancelLane(m_source,lane);
    if(clear){m_home->evidence.clear();m_home->identities.clear();m_home->population.clear();m_home->failed.clear();m_home->optionalUnavailable.clear();m_home->sections.clear();m_home->snapshot.clear();m_home->seeds.clear();m_home->seedHash.clear();m_home->status="idle";m_home->error.clear();m_home->visited=false;m_home->refreshed=false;m_home->cursor=0;m_home->nextCursor=0;++m_home->revision;emit homeChanged();}
}
void VinylRecommendationService::clearHomeProfile(){m_home->collections.clear();m_home->history.clear();m_home->visitId.clear();cancelHome(true);}
void VinylRecommendationService::homeSeedsChanged(){
    QVariantList input=m_saved;
    for(const auto &entry:m_home->collections.value("artists").toList()){
        auto row=entry.toMap();row["id"]=row.value("artistId",row.value("id"));input.append(row);
    }
    auto seeds=uniqueArtists(input);
    std::sort(seeds.begin(),seeds.end(),[](const QVariant &a,const QVariant &b){return a.toMap().value("key").toString()<b.toMap().value("key").toString();});
    const auto hash=digest(seeds);if(hash==m_home->seedHash)return;
    const bool visited=m_home->visited;cancelHome(true);m_home->visited=visited;m_home->seeds=seeds;m_home->seedHash=hash;
    if(visited&&m_state=="answered")QTimer::singleShot(0,this,[this]{refreshHome(false);});
}
void VinylRecommendationService::visitHome(){m_home->visited=true;m_home->visitId=QUuid::createUuid().toString(QUuid::WithoutBraces);refreshHome(false);}
void VinylRecommendationService::refreshHome(bool force){
    if(!m_active||m_scope.isEmpty()||m_state!="answered")return;
    m_home->visited=true;
    if(!force&&(!m_home->active.isEmpty()||!m_home->waiting.isEmpty())){rebuildHome();return;}
    cancelHome(false);m_home->failed.clear();m_home->optionalUnavailable.clear();m_home->error.clear();m_home->evaluatedAt=m_home->clock();
    if(force&&m_home->refreshed)m_home->cursor=m_home->nextCursor;
    m_home->refreshed=true;
    for(const auto &value:m_home->seeds){const auto seed=value.toMap();if(seed.value("provider")!="youtube-music")continue;
        const auto key=seed.value("key").toString(),id=seed.value("providerId").toString();
        auto cached=cachedHome(m_source,"artistCandidates",id,{{"seed",seed}});QVariantMap normalized;
        if(!cached.isEmpty()&&VinylProvider::normalizeHome("artistCandidates",id,{{"seed",seed}},cached,&normalized))m_home->evidence[key]=normalized;
        auto identity=cachedHome(m_source,"recommendationIdentity",id,{{"seed",seed}});
        if(!identity.isEmpty()&&VinylProvider::normalizeHome("recommendationIdentity",id,{{"seed",seed}},identity,&normalized))m_home->identities[key]=normalized;
    }
    const int size=m_home->seeds.size();int count=0;
    for(int step=0;step<size&&count<6;++step){const int at=(m_home->cursor+step)%size;const auto seed=m_home->seeds[at].toMap();
        if(seed.value("provider")!="youtube-music")continue;const auto key=seed.value("key").toString();
        auto cached=m_home->evidence.value(key);const auto fetched=QDateTime::fromString(cached.value("fetchedAt").toString(),Qt::ISODateWithMs);
        const auto age=fetched.isValid()?fetched.msecsTo(QDateTime::fromMSecsSinceEpoch(m_home->evaluatedAt,Qt::UTC)):-1;
        const bool fresh=!cached.isEmpty()&&!cached.value("_stale").toBool()&&age>=0&&age<=6LL*3600000
            &&(!cached.value("items").toList().isEmpty()||cached.value("complete").toBool());
        if(!force&&fresh)continue;
        if(!cached.isEmpty()){cached["_stale"]=true;m_home->evidence[key]=cached;}
        m_home->waiting.append({"artistCandidates",seed.value("providerId").toString(),key,seed,force});++count;m_home->nextCursor=(at+1)%size;
    }
    if(!m_home->waiting.isEmpty())m_home->deadline->start(8000);
    rebuildHome();pumpHome();
}
void VinylRecommendationService::pumpHome(){
    while(m_active&&m_home->active.size()<2&&!m_home->waiting.isEmpty()){
        int at=-1;QString lane;
        for(int i=0;i<m_home->waiting.size();++i){const auto &job=m_home->waiting[i];
            if(job.operation=="artistCandidates")lane=!m_home->active.contains("home-seed-0")?"home-seed-0":"home-seed-1";
            else lane=job.operation=="recommendationIdentity"?"home-identity":"home-population";
            if(!m_home->active.contains(lane)){at=i;break;}
        }
        if(at<0)break;const auto job=m_home->waiting.takeAt(at);
        const int id=requestHome(m_source,lane,job.operation,job.value,{{"seed",job.seed},{"refresh",job.force}});
        if(id<0){if(job.operation=="artistCandidates")m_home->failed.insert(job.key);else m_home->optionalUnavailable.insert(job.operation+":"+job.key);continue;}
        m_home->active.insert(lane,{id,job});
    }
    if(m_home->active.isEmpty()&&m_home->waiting.isEmpty())m_home->deadline->stop();
    rebuildHome();
}
bool VinylRecommendationService::acceptHome(const QString &lane,int id,const QVariant &raw,const QString &error){
    if(!lane.startsWith("home-"))return false;
    if(!m_active||sender()!=m_source||!m_home->active.contains(lane)||m_home->active.value(lane).id!=id)return true;
    const auto job=m_home->active.take(lane).job;QVariantMap data;QString failure=error;
    if(failure.isEmpty()&&!VinylProvider::normalizeHome(job.operation,job.value,{{"seed",job.seed}},plainHome(raw),&data))failure="The recommendation response is invalid.";
    if(data.value("status")=="unavailable")failure=data.value("error","The catalogue is unavailable.").toString();
    if(!failure.isEmpty()){
        if(job.operation=="artistCandidates"){m_home->failed.insert(job.key);m_home->error="Some catalogue enrichment is unavailable.";}
        else m_home->optionalUnavailable.insert(job.operation+":"+job.key);
    }
    else if(job.operation=="artistCandidates"){
        m_home->evidence[job.key]=data;
        auto cached=cachedHome(m_source,"recommendationIdentity",job.value,{{"seed",job.seed}});QVariantMap checked;
        if(!cached.isEmpty()&&VinylProvider::normalizeHome("recommendationIdentity",job.value,{{"seed",job.seed}},cached,&checked))m_home->identities[job.key]=checked;
        else m_home->waiting.append({"recommendationIdentity",job.value,job.key,job.seed,false});
    }else if(job.operation=="recommendationIdentity"){
        m_home->identities[job.key]=data;
        if(data.value("status")=="verified"){
            const QVariantMap seed{{"identity","canonical"},{"kind","artist"},{"mbid",data.value("mbid")}};
            m_home->waiting.append({"populationCandidates",data.value("mbid").toString(),job.key,seed,false});
        }
    }else m_home->population[job.key]=data;
    pumpHome();return true;
}

void VinylRecommendationService::rebuildHome(){
    if(!m_active||m_state!="answered")return;
    auto profile=ProfileScopeId::parse(m_scope);if(!profile.ok())return;
    const auto now=m_home->evaluatedAt>0?m_home->evaluatedAt:m_home->clock();
    TasteProfileState taste(*profile.value);
    const auto feedback=m_preferences?m_preferences->property("vinylFeedbackSnapshot").toString():QString();
    if(!feedback.isEmpty()){const auto restored=FeedbackTastePersistence::restore(*profile.value,feedback.toUtf8());
        if(!restored.ok()){m_home->sections.clear();m_home->status="error";m_home->error="Saved feedback could not be read.";emit homeChanged();return;}taste=*restored.value;}
    m_home->history=m_keepHistory?VinylRecommendationHistory::project(qobject_cast<ActivityStore*>(m_activityStore.data()),
        qobject_cast<HistoryStore*>(m_historyStore.data()),m_home->collections.value("albums").toList(),now):QVariantMap();
    const auto derived=VinylRecommendationHistory::derive(m_scope,m_home->history,now);
    if(!derived.ok())return;
    const auto manualSessions=m_home->history.value("qualifiedManualSessions").toInt();
    const double seedMultiplier=manualSessions<=30?1.0:manualSessions<=100?0.75:0.5;
    QSet<QString> savedSeedKeys;
    for(const auto &value:m_home->collections.value("artists").toList()) {
        const auto saved=artist(value.toMap());if(!saved.isEmpty())savedSeedKeys.insert(saved.value("key").toString());
    }
    RecommendationExposureLog exposure(*profile.value);QVariantList retainedExposure;
    if(m_keepHistory && qobject_cast<ActivityStore*>(m_activityStore.data()) && qobject_cast<ActivityStore*>(m_activityStore.data())->retentionEnabled()) {
        const auto stored=m_preferences?m_preferences->property("vinylRecommendationExposureSnapshot").toString():QString();
        const bool valid=readExposure(m_scope,stored,now,m_home->activityReset,m_home->historyReset,exposure,retainedExposure);
        // Old-generation/expired private records are removed after their owner
        // bindings are known; valid post-reset records survive a fresh service.
        if(valid && retainedExposure.isEmpty() && !stored.isEmpty()) {
            const auto envelope=QJsonDocument::fromJson(stored.toUtf8()).object();
            if(m_historyStore || envelope.value("historyResetGeneration").toInteger(-1)==0)storeExposure({});
        }
    }
    struct Node {QVariantMap card;QSet<QString> sections;QVector<RecommendationCandidate> candidates;QVector<SeedAffinityEvidence> affinities;};
    QMap<QString,Node> nodes;QMap<QString,QSet<QString>> generators;QVariantList sourceSnapshot;
    auto seedIdentity=[this](const QVariantMap &seed)->std::optional<RecommendationEntityRef>{
        const auto identity=m_home->identities.value(seed.value("key").toString());
        if(identity.value("status")=="verified"){
            const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(identity.value("ref").toMap()));
            if(ref.ok()&&ref.value->isCanonical()&&ref.value->kind()==CanonicalEntityKind::Artist)return *ref.value;
        }
        return typed(seed);
    };
    auto add=[&](const QVariantMap &raw,const QVariantMap &seed,const QString &generator,const QString &section,bool library){
        const auto converted=VinylRecommendationIdentity::fromCatalogue(raw);if(!converted.value("ok").toBool())return;
        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(converted.value("ref").toMap()));const auto seedRef=seedIdentity(seed);
        if(!ref.ok()||!seedRef)return;const auto key=ref.value->stableKey();
        if(!nodes.contains(key)&&nodes.size()>=300)return;
        if(!generators[generator].contains(key)&&generators[generator].size()>=60)return;
        const auto destination=converted.value("destination").toMap();if(destination.value("id").toString().isEmpty()||raw.value("title").toString().isEmpty())return;
        // A seed shelf is not an exact recording-match attestation. Prefer its
        // usable album/artist destinations until recording qualification exists.
        if(destination.value("kind")=="song"&&!destination.value("playable").toBool())return;
        generators[generator].insert(key);auto &node=nodes[key];
        auto card=raw;card["ref"]=converted.value("ref");card["key"]=key;card["typedKey"]=converted.value("ref");card["destination"]=destination;
        card["kind"]=destination.value("kind")=="artist"?"artists":destination.value("kind")=="song"?"songs":"albums";
        card["id"]=destination.value("id");card["albumId"]=destination.value("id");card["artistId"]=destination.value("id");card["round"]=destination.value("kind")=="artist";
        QStringList names,artistIds;for(const auto &entry:raw.value("artistRefs").toList()){const auto artist=entry.toMap();names.append(artist.value("name").toString());artistIds.append("ytm:artist:"+artist.value("providerId").toString());}
        card["subtitle"]=names.join(QStringLiteral(" · "));card["cover"]=raw.value("cover",raw.value("image"));
        if(destination.value("kind")=="song"){
            QVariantMap track{{"id",destination.value("id")},{"videoId",destination.value("providerId")},{"title",raw.value("title")},{"seconds",raw.value("seconds")},{"artists",names},{"artistIds",artistIds},{"available",destination.value("playable")}};
            card["record"]=QVariantMap{{"id",destination.value("id")},{"browseId",destination.value("id")},{"type","Song"},{"origin","youtube-music"},{"title",raw.value("title")},{"cover",card.value("cover")},{"artists",names},{"artistIds",artistIds},{"tracks",QVariantList{track}}};card["trackIndex"]=0;
        }
        if(node.card.isEmpty()||wire(card)<wire(node.card))node.card=card;
        node.sections.insert(section);
        const auto date=QDate::fromString(raw.value("releaseDate").toString(),Qt::ISODate);const auto today=QDateTime::fromMSecsSinceEpoch(now,Qt::UTC).date();
        if(section=="from-your-artists"&&destination.value("kind")=="album"&&date.isValid()&&date<=today&&date.daysTo(today)<=90)node.sections.insert("new-from-your-artists");
        RecommendationCanonicalContext context;
        auto primary=raw.value("artistRefs").toList();auto artistRef=primary.isEmpty()?seedRef:typed(primary.first().toMap());
        if(!primary.isEmpty()&&primary.first().toMap().value("provider")==seed.value("provider")
            &&primary.first().toMap().value("providerId")==seed.value("providerId"))artistRef=seedRef;
        if(artistRef){if(artistRef->isCanonical())context.artistMbid=ArtistMbid::parse(artistRef->mbidString()).value;else context.providerArtist=*artistRef;}
        const auto group=ReleaseGroupMbid::parse(raw.value("releaseGroupMbid").toString());if(group.ok())context.releaseGroupMbid=*group.value;
        const auto provenance=RecommendationCandidateProvenance::create(*RecommendationGeneratorId::parse(generator).value,
            RecommendationSourceFamily::Editorial,
            *ProviderId::parse(raw.value("provider").toString()).value,*ProviderItemId::parse(raw.value("providerId").toString()).value,{},
            *RecommendationSeed::create(*seedRef,m_saved.contains(seed)?"selected-artist":"saved-artist").value);
        if(!provenance.ok())return;
        const auto contribution=RecommendationContribution::create(context,*provenance.value,{},
            {*RecommendationReasonEvidence::create(RecommendationEvidenceKind::EditorialSelection,
                library?"Current library membership":"Provider artist shelf: "+raw.value("relation").toString()).value});
        if(!contribution.ok())return;
        const auto candidate=RecommendationCandidate::create(*ref.value,*contribution.value,destination.value("playable").toBool());
        if(candidate.ok()){node.candidates.append(*candidate.value);node.affinities.append({*ref.value,*seedRef,
            library || savedSeedKeys.contains(seed.value("key").toString())?1.0:seedMultiplier});}
    };
    // Round-robin admission prevents the first returned artist consuming a generator budget.
    for(int index=0;index<60;++index)for(const auto &value:m_home->seeds){const auto seed=value.toMap();const auto data=m_home->evidence.value(seed.value("key").toString());
        const auto items=data.value("items").toList();if(index>=items.size())continue;const auto item=items[index].toMap();const auto relation=item.value("relation").toString();
        add(item,seed,relation=="related-artist"?"youtube-related-artists":relation=="top-recording"?"youtube-top-recordings":"youtube-own-releases",relation=="related-artist"?"related-discoveries":"from-your-artists",false);
    }
    for(const auto &seedValue:m_home->seeds){const auto seed=seedValue.toMap();sourceSnapshot.append(m_home->evidence.value(seed.value("key").toString()));
        for(const auto &value:m_home->collections.value("albums").toList()){const auto saved=value.toMap(),album=saved.value("album").toMap();const auto ids=album.value("artistIds").toStringList();
            const auto artistId=seed.value("provider")=="youtube-music"?"ytm:artist:"+seed.value("providerId").toString():seed.value("providerId").toString();
            if(!ids.contains(artistId))continue;auto row=album;const bool yt=album.value("origin")=="youtube-music";
            row["provider"]=yt?"youtube-music":"local";row["providerId"]=yt?album.value("providerIds").toMap().value("youtube-music",album.value("browseId")):album.value("id");row["kind"]="album";
            if(yt)row["id"]="ytm:album:"+row.value("providerId").toString();
            QVariantList credits;const auto names=album.value("artists").toStringList();
            for(int i=0;i<ids.size();++i) {
                const auto id=ids[i];const bool artistYt=id.startsWith("ytm:artist:");
                if(yt && !artistYt)continue;
                QVariantMap credit{{"kind","artist"},{"provider",artistYt?"youtube-music":"local"},
                    {"providerId",artistYt?id.mid(11):id},{"name",names.value(i)}};
                if(typed(credit))credits.append(credit);
            }
            row["artistRefs"]=credits;row["relation"]="library-membership";
            add(row,seed,"library-membership","from-your-artists",true);
        }
    }
    QVariantList suppressedItems;
    for(const auto &node:nodes) {
        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(node.card.value("ref").toMap()));
        if(!ref.ok())continue;const auto own=taste.find(*ref.value);
        const bool hidden=own && own->suppression().hidden(),snoozed=own && own->suppression().isSnoozedAt(now);
        QVariantList excluded;
        if(own && own->suppression().doNotRecommend() && ref.value->kind()==CanonicalEntityKind::Artist)
            excluded.append(QVariantMap{{"ref",node.card.value("ref")},{"name",node.card.value("title")}});
        for(const auto &value:node.card.value("artistRefs").toList()) {
            const auto credit=value.toMap();if(const auto artistRef=typed(credit)) {
                if(const auto state=taste.find(*artistRef);state && state->suppression().doNotRecommend())
                    excluded.append(QVariantMap{{"ref",artistRef->toJson().toVariantMap()},{"name",credit.value("name",credit.value("title"))}});
            }
        }
        if(hidden || snoozed || !excluded.isEmpty())suppressedItems.append(QVariantMap{
            {"ref",node.card.value("ref")},{"title",node.card.value("title")},{"artistRefs",node.card.value("artistRefs")},
            {"seedArtistRef",node.card.value("seedArtistRef")},{"hidden",hidden},{"snoozed",snoozed},
            {"excluded",!excluded.isEmpty()},{"excludedArtistRefs",excluded}});
    }
    const bool loading=!m_home->active.isEmpty()||!m_home->waiting.isEmpty();QVariantList sections;QSet<QString> prior,distinctSeeds;
    for(const auto &seed:m_home->seeds)if(const auto ref=seedIdentity(seed.toMap()))distinctSeeds.insert(ref->stableKey());
    const QStringList sectionIds{"from-your-artists","related-discoveries","new-from-your-artists"};
    const QStringList titles{"From your artists","Related discoveries","New from your artists"};
    for(int sectionIndex=0;sectionIndex<sectionIds.size();++sectionIndex){const auto section=sectionIds[sectionIndex];QVector<RecommendationCandidate> pool;RankerFeatureContext features;
        for(auto it=nodes.cbegin();it!=nodes.cend();++it)if(it->sections.contains(section)){
            bool suppressed=false;
            // Every credited artist is checked, including collaborators. The
            // primary artist used for diversity is not the full suppression set.
            for(const auto &credit:it->card.value("artistRefs").toList())if(const auto artist=typed(credit.toMap())) {
                if(const auto state=taste.find(*artist))suppressed|=state->suppression().doNotRecommend()
                    || state->suppression().hidden() || state->suppression().isSnoozedAt(now);
            }
            if(suppressed)continue;pool+=it->candidates;features.seedAffinities+=it->affinities;
        }
        if(pool.isEmpty())continue;
        DeterministicRankerConfig config;config.version="vinyl-home-v1";config.nowMs=now;config.maxResults=300;config.seedAffinityWeight=300;
        config.maxPerArtist=distinctSeeds.size()==1&&section!="related-discoveries"?0:3;config.maxRecordingsPerReleaseGroup=2;
        const auto ranked=rankDeterministically(RecommendationCandidateSet::normalize(pool),taste,
            *derived.value,exposure,features,config);
        if(!ranked.ok())continue;QVariantList preferred,repeated;
        for(const auto &entry:ranked.value->ranked()){auto card=nodes.value(entry.entity().stableKey()).card;QVariantList reasons;
            for(const auto &e:entry.explanation()){QVariantMap reason{{"kind",rankEvidenceKindName(e.kind)},{"sourceId",e.sourceId},{"scoreDelta",e.scoreDelta}};if(e.relatedEntity)reason["seed"]=e.relatedEntity->toJson().toVariantMap();reasons.append(reason);}
            card["score"]=entry.score();card["reasons"]=reasons;card["evidence"]=reasons;card["configVersion"]=config.version;
            if(prior.contains(entry.entity().stableKey()))repeated.append(card);else preferred.append(card);
        }
        preferred.append(repeated);if(preferred.size()>12)preferred=preferred.mid(0,12);if(preferred.isEmpty())continue;
        prior.clear();for(const auto &card:preferred)prior.insert(card.toMap().value("key").toString());
        bool stale=false;QString fetched;
        for(const auto &data:m_home->evidence){stale|=data.value("_stale").toBool();if(data.value("fetchedAt").toString()>fetched)fetched=data.value("fetchedAt").toString();}
        sections.append(QVariantMap{{"id",section},{"key",section},{"title",titles[sectionIndex]},{"kind","recommendations"},{"items",preferred},
            {"status",stale||!m_home->failed.isEmpty()?"stale":loading?"loading":"ready"},{"loading",loading},{"error",m_home->error},{"fetchedAt",fetched},
            {"canRetry",!loading},{"configVersion",config.version},{"evaluatedAtMs",now}});
    }
    m_home->status=loading?"loading":sections.isEmpty()?m_home->failed.isEmpty()?"empty":"error":m_home->failed.isEmpty()?"ready":"stale";
    QVariantMap mappings;for(auto it=m_home->identities.cbegin();it!=m_home->identities.cend();++it)mappings[it.key()]=it.value();
    const QVariantMap inputs{{"catalogue",sourceSnapshot},{"identities",mappings},{"library",m_home->collections.value("albums")},{"feedback",feedback},{"seeds",m_home->seeds},{"history",m_home->history}};
    auto configPolicy=policy();configPolicy["seedDecay"]=QVariantList{30,100,1.0,0.75,0.5};configPolicy["historyVersion"]=1;
    const auto sourceHash=digest(inputs);
    const QVariantMap snapshot{{"configVersion","vinyl-home-v1"},{"evaluatedAtMs",now},{"sourceHash",sourceHash},{"configHash",digest(configPolicy)},{"seedHash",m_home->seedHash},{"candidateCount",nodes.size()},{"historyComplete",false},{"optionalUnavailableCount",m_home->optionalUnavailable.size()},
        {"qualifiedManualSessions",manualSessions},{"seedMultiplier",seedMultiplier},{"exposureSnapshotId",digest(QVariantList{m_home->visitId,sourceHash})},{"exposureCount",exposure.records().size()},{"suppressedItems",suppressedItems}};
    if(sections!=m_home->sections||snapshot!=m_home->snapshot){m_home->sections=sections;m_home->snapshot=snapshot;++m_home->revision;}
    emit homeChanged();
}
