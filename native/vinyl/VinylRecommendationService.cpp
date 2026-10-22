#include "VinylRecommendationService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QSet>
#include <QJSValue>
#include <QUuid>
#include <cmath>

namespace {
QVariant plain(const QVariant &value){return value.metaType().id()==qMetaTypeId<QJSValue>()?value.value<QJSValue>().toVariant():value;}
bool offsetNumber(const QVariant &value){
    const int type=value.metaType().id();
    if(type!=QMetaType::Int&&type!=QMetaType::LongLong&&type!=QMetaType::Double&&type!=QMetaType::UInt&&type!=QMetaType::ULongLong)return false;
    const double n=value.toDouble();return std::isfinite(n)&&n>=0&&n<=10000&&std::floor(n)==n;
}
}

VinylRecommendationService::VinylRecommendationService(QObject *parent):QObject(parent) {
    m_debounce.setSingleShot(true);m_debounce.setInterval(300);
    connect(&m_debounce,&QTimer::timeout,this,&VinylRecommendationService::dispatch);
    m_results={{"items",QVariantList()},{"nextCursor",QVariantMap()},{"query",""},{"status","idle"},{"error",""}};
}
QVariantMap VinylRecommendationService::artist(const QVariantMap &raw) {
    QString provider=raw.value("provider").toString(),id=raw.value("providerId").toString();
    const QString oldId=raw.value("id").toString();
    if(provider.isEmpty()) {
        if(oldId.startsWith("ytm:artist:")){provider="youtube-music";id=oldId.mid(11);}
        else {provider="local";id=oldId;}
    }
    const bool legacy=raw.contains("legacyId");
    if((id.isEmpty()&&!legacy)||id.size()>4096||id.contains(QChar(0)))return {};
    if(provider=="youtube-music"&&!legacy&&!QRegularExpression("^UC[A-Za-z0-9_-]{22}$").match(id).hasMatch())return {};
    if(provider!="youtube-music"&&provider!="local"&&provider!="musicbrainz")return {};
    const QString key=QString::fromUtf8(QJsonDocument(QJsonArray{provider,"artist",id}).toJson(QJsonDocument::Compact));
    if(raw.contains("key")&&raw.value("key").toString()!=key)return {};
    QString name=raw.value("name",raw.value("title",raw.value("artist"))).toString();
    if(name.isEmpty())name=id;
    if(name.size()>4096||name.contains(QChar(0)))return {};
    const QString image=raw.value("image",raw.value("cover")).toString();
    if(image.size()>16384||image.contains(QChar(0)))return {};
    QVariantMap out{{"key",key},{"id",key},{"kind","artist"},{"provider",provider},{"providerId",id},
        {"name",name},{"title",name},{"image",image},{"cover",image},{"mappingStatus","unresolved"}};
    if(legacy)out["legacyId"]=raw.value("legacyId");
    if(raw.value("mappingStatus")=="ambiguous"||raw.value("mappingStatus")=="unavailable")out["mappingStatus"]=raw.value("mappingStatus");
    // Display/search identity is provider-native. Only already verified metadata
    // can carry a canonical mapping; UUID-shaped application IDs prove nothing.
    if(raw.value("mappingStatus")=="verified"&&raw.contains("canonicalMbid")) {
        const auto mbid=raw.value("canonicalMbid").toString();
        if(!QUuid(mbid).isNull()&&QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$",QRegularExpression::CaseInsensitiveOption).match(mbid).hasMatch()){
            out["canonicalMbid"]=mbid;out["mappingStatus"]="verified";
        }
    }
    return out;
}
QVariantList VinylRecommendationService::uniqueArtists(const QVariantList &values) {
    QVariantList out;QSet<QString> seen;
    for(const auto &value:values){const auto row=artist(value.toMap());const auto key=row.value("key").toString();
        if(row.isEmpty()||seen.contains(key))continue;seen.insert(key);out.append(row);if(out.size()==512)break;}
    return out;
}
void VinylRecommendationService::cancelSearch() {
    m_debounce.stop();m_request=-1;
    if(m_source){
        if(m_source->metaObject()->indexOfMethod("cancel(QString)")>=0)
            QMetaObject::invokeMethod(m_source,"cancel",Qt::DirectConnection,Q_ARG(QString,QString("taste-artists")));
        else QMetaObject::invokeMethod(m_source,"cancel",Qt::DirectConnection,Q_ARG(QVariant,QVariant("taste-artists")));
    }
    if(m_results.value("status")=="loading"){m_results["status"]=m_results.value("items").toList().isEmpty()?"idle":"ready";emit resultsChanged();}
}
void VinylRecommendationService::invalidate() {
    cancelSearch();m_query.clear();m_cursor.clear();m_draft.clear();m_saved.clear();m_state="unanswered";m_error.clear();
    m_results={{"items",QVariantList()},{"nextCursor",QVariantMap()},{"query",""},{"status","idle"},{"error",""}};
    emit stateChanged();emit resultsChanged();
}
void VinylRecommendationService::setSource(QObject *value) {
    if(m_source==value)return;cancelSearch();if(m_source)disconnect(m_source,nullptr,this,nullptr);m_source=value;
    if(value)connect(value,SIGNAL(completed(QString,int,QVariant,QString)),this,SLOT(accept(QString,int,QVariant,QString)),Qt::QueuedConnection);
    emit stateChanged();
}
void VinylRecommendationService::setPreferences(QObject *value) {
    if(m_preferences==value)return;invalidate();if(m_preferences)disconnect(m_preferences,nullptr,this,nullptr);m_preferences=value;
    if(value)connect(value,SIGNAL(vinylTasteChanged()),this,SLOT(loadPreferences()));loadPreferences();
}
void VinylRecommendationService::setProfileScope(const QString &value){if(m_scope==value)return;invalidate();m_scope=value;loadPreferences();}
void VinylRecommendationService::setProfileGeneration(int value){if(m_generation==value)return;invalidate();m_generation=value;loadPreferences();}
void VinylRecommendationService::setProfileActive(bool value){if(m_active==value)return;invalidate();m_active=value;if(value)loadPreferences();emit stateChanged();}
void VinylRecommendationService::loadPreferences() {
    if(!m_active||m_committing)return;
    m_saved=m_preferences?uniqueArtists(plain(m_preferences->property("vinylTasteArtists")).toList()):QVariantList();
    m_state=m_preferences&&m_preferences->property("vinylTasteAnswered").toBool()?"answered":"unanswered";
    m_draft=m_saved;emit stateChanged();
}
void VinylRecommendationService::setSuggestions(const QVariantList &values){m_suggestions=uniqueArtists(values);if(m_query.isEmpty()&&m_results.value("status")!="loading"){m_results["items"]=m_suggestions;emit resultsChanged();}}
void VinylRecommendationService::beginHomeVisit(){if(!m_active||m_scope.isEmpty())return;if(m_state=="unanswered"){m_state="draft";m_draft=m_saved;emit stateChanged();searchArtists("");}}
void VinylRecommendationService::beginTasteEdit(){if(!m_active)return;cancelSearch();m_draft=m_saved;m_state="editing";m_error.clear();emit stateChanged();searchArtists("");}
void VinylRecommendationService::cancelTasteEdit(){if(m_state!="editing")return;cancelSearch();m_draft=m_saved;m_state="answered";m_error.clear();emit stateChanged();}
void VinylRecommendationService::toggleDraftArtist(const QVariantMap &value) {
    if(!m_active||(m_state!="draft"&&m_state!="editing"))return;
    const auto row=artist(value);if(row.isEmpty())return;const auto key=row.value("key");
    for(int i=0;i<m_draft.size();++i)if(m_draft[i].toMap().value("key")==key){m_draft.removeAt(i);emit stateChanged();return;}
    if(m_draft.size()>=512){m_error="The artist selection limit has been reached.";emit stateChanged();return;}
    m_draft.append(row);m_error.clear();emit stateChanged();
}
bool VinylRecommendationService::commitTaste(bool skip) {
    if(!m_active||!m_preferences||(m_state!="draft"&&m_state!="editing"))return false;
    auto rows=skip?QVariantList():m_draft;
    for(auto &value:rows){auto row=value.toMap();row.remove("id");row.remove("title");row.remove("cover");value=row;}
    bool saved=false;m_committing=true;
    if(m_preferences->metaObject()->indexOfMethod("setVinylTasteSelections(bool,QVariantList)")>=0)
        QMetaObject::invokeMethod(m_preferences,"setVinylTasteSelections",Qt::DirectConnection,Q_RETURN_ARG(bool,saved),Q_ARG(bool,true),Q_ARG(QVariantList,rows));
    else {
        QVariant result;QMetaObject::invokeMethod(m_preferences,"setVinylTasteSelections",Qt::DirectConnection,Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QVariant(true)),Q_ARG(QVariant,QVariant(rows)));saved=result.toBool();
    }
    m_committing=false;
    if(!saved){m_error="Taste choices could not be saved. Try again.";emit stateChanged();return false;}
    cancelSearch();m_error.clear();loadPreferences();return true;
}
void VinylRecommendationService::searchArtists(const QString &value,const QVariantMap &cursor) {
    if(!m_active||(m_state!="draft"&&m_state!="editing"))return;
    const QString query=value.trimmed();
    if(query.size()>300||QJsonDocument::fromVariant(cursor).toJson().size()>16384)return;
    if(!cursor.isEmpty()&&(query!=m_query||cursor!=m_results.value("nextCursor").toMap()))return;
    cancelSearch();m_query=query;m_cursor=cursor;
    if(cursor.isEmpty())m_results["items"]=query.isEmpty()?m_suggestions:QVariantList();
    m_results["query"]=query;m_results["error"]="";m_results["nextCursor"]=QVariantMap();m_results["status"]="loading";
    emit resultsChanged();m_debounce.start();
}
void VinylRecommendationService::dispatch() {
    if(!m_active||!m_source||(m_state!="draft"&&m_state!="editing")){
        m_results["status"]="error";m_results["error"]="Artist search is unavailable. You can still finish or skip.";emit resultsChanged();return;
    }
    const QString operation=m_query.isEmpty()?"discover":"cataloguePage",value=m_query.isEmpty()?"artists":m_query;
    const QVariantMap options=m_query.isEmpty()?QVariantMap{{"region","IN"}}:QVariantMap{{"kind","artists"},{"cursor",m_cursor}};
    int id=-1;
    if(m_source->metaObject()->indexOfMethod("request(QString,QString,QString,QVariantMap)")>=0)
        QMetaObject::invokeMethod(m_source,"request",Qt::DirectConnection,Q_RETURN_ARG(int,id),Q_ARG(QString,QString("taste-artists")),Q_ARG(QString,operation),Q_ARG(QString,value),Q_ARG(QVariantMap,options));
    else {
        QVariant result;QMetaObject::invokeMethod(m_source,"request",Qt::DirectConnection,Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QVariant("taste-artists")),Q_ARG(QVariant,QVariant(operation)),Q_ARG(QVariant,QVariant(value)),Q_ARG(QVariant,QVariant(options)));id=result.isValid()?result.toInt():-1;
    }
    m_request=id;if(id<0){m_results["status"]="error";m_results["error"]="Artist search is unavailable. You can still finish or skip.";emit resultsChanged();}
}
void VinylRecommendationService::accept(const QString &lane,int id,const QVariant &value,const QString &error) {
    if(sender()!=m_source||lane!="taste-artists"||id!=m_request||!m_active||m_request<0)return;
    m_request=-1;const auto data=plain(value).toMap();QString failure=error;
    const auto items=data.value("items").toList();const auto next=data.value("nextCursor").toMap();
    if(failure.isEmpty()&&!m_query.isEmpty()&&(data.value("version").toInt()!=1||data.value("query")!=m_query||data.value("kind")!="artists"||data.value("source")!="youtube-music"
        ||data.value("items").metaType().id()!=QMetaType::QVariantList||data.value("complete").metaType().id()!=QMetaType::Bool
        ||data.value("nextCursor").metaType().id()!=QMetaType::QVariantMap||data.value("complete").toBool()!=next.isEmpty()
        ||!offsetNumber(data.value("offset"))||!offsetNumber(data.value("nextOffset"))
        ||items.size()>200||data.value("offset").toInt()!=m_cursor.value("offset",0).toInt()
        ||data.value("nextOffset").toInt()<data.value("offset").toInt()||data.value("nextOffset").toInt()>10000
        ||(!next.isEmpty()&&(!offsetNumber(next.value("offset"))||next.value("token").toString().isEmpty()
            ||next.value("token")==m_cursor.value("token")||next.value("offset").toInt()!=data.value("nextOffset").toInt()
            ||data.value("nextOffset").toInt()<=data.value("offset").toInt()))))failure="The artist search page is invalid. Try again.";
    if(failure.isEmpty()&&m_query.isEmpty()&&(data.value("items").metaType().id()!=QMetaType::QVariantList||items.size()>200))failure="The suggested artist list is invalid.";
    if(!failure.isEmpty()){m_results["status"]="error";m_results["error"]=failure;emit resultsChanged();return;}
    QVariantList merged=m_cursor.isEmpty()?(m_query.isEmpty()?m_suggestions:QVariantList()):m_results.value("items").toList();
    merged.append(items);merged=uniqueArtists(merged);m_results["items"]=merged;m_results["nextCursor"]=m_query.isEmpty()?QVariantMap():next;
    m_results["status"]=merged.isEmpty()?"empty":"ready";m_results["error"]="";m_results["cached"]=data.value("_cached").toBool();emit resultsChanged();
}
