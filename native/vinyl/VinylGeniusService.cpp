#include "VinylGeniusService.h"
#include "backend/bc3-annotation-prototype/AnnotationMeaningPlane.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
constexpr int maxBytes=2*1024*1024;
QString numericId(const QVariant &value) {
    if(value.metaType().id()==QMetaType::Bool || value.metaType().id()==QMetaType::QString)return {};
    bool ok=false;const auto n=value.toDouble(&ok);
    return ok&&std::isfinite(n)&&n>0&&n<=9007199254740991.0&&std::floor(n)==n
        ?QString::number(qint64(n)):QString();
}
QString sourceUrl(const QVariant &value) {
    const QUrl url(value.toString());
    return url.isValid()&&url.scheme()=="https"&&QStringList{"genius.com","www.genius.com"}.contains(url.host())
        &&url.userInfo().isEmpty()&&url.port(-1)==-1&&url.query().isEmpty()
        ?url.toString():QString();
}
QSet<QString> versions(QString title) {
    QSet<QString> result;
    auto matches=QRegularExpression("\\b(live|cover|remix|acoustic|instrumental|karaoke|demo|radio edit|remaster(?:ed)?|sped up|slowed|clean|explicit|mono|stereo|translation)\\b")
        .globalMatch(VinylGeniusService::normalizedText(title));
    while(matches.hasNext()){auto tag=matches.next().captured(1);if(tag.startsWith("remaster"))tag="remaster";result.insert(tag);}
    return result;
}
QString titleKey(QString text) {
    text.remove(QRegularExpression("\\s*[\\[(](?:feat\\.?|ft\\.?)\\s+[^\\])]+[\\])]",QRegularExpression::CaseInsensitiveOption));
    return VinylGeniusService::normalizedText(text);
}
QStringList artistNames(const QVariantMap &context) {
    auto names=context.value("artistNames").toStringList();
    if(names.isEmpty())names.append(context.value("artistName").toString());
    return names;
}
bool artistMatches(const QVariantMap &context,const QVariantMap &song) {
    QSet<QString> source;
    auto artists=song.value("primary_artists").toList();
    if(artists.isEmpty())artists.append(song.value("primary_artist"));
    for(const auto &artist:artists)source.insert(VinylGeniusService::normalizedText(artist.toMap().value("name").toString()));
    const auto expected=artistNames(context);
    return !expected.isEmpty()&&!expected.first().isEmpty()
        &&source.contains(VinylGeniusService::normalizedText(expected.first()));
}
QVariantList textualLines(const QVariantList &input) {
    QVariantList result;
    for(const auto &value:input){const auto row=value.toMap();result.append(QVariantMap{{"text",row.value("text")},{"cueId",row.value("cueId")}});}
    return result;
}
}

VinylGeniusService::VinylGeniusService(QObject *parent,QNetworkAccessManager *network)
    :QObject(parent),m_network(network?network:new QNetworkAccessManager(this)) {}
VinylGeniusService::~VinylGeniusService(){cancel();}
void VinylGeniusService::cancel(){
    ++m_generation;
    if(m_reply){auto *reply=m_reply.data();m_reply=nullptr;reply->disconnect(this);reply->abort();reply->deleteLater();}
}
void VinylGeniusService::setProvider(QObject *provider){if(m_provider==provider)return;m_provider=provider;emit providerChanged();schedule();}
void VinylGeniusService::setContext(const QVariantMap &context){if(m_context==context)return;m_context=context;emit contextChanged();schedule();}
void VinylGeniusService::setDocumentId(const QString &id){if(m_documentId==id)return;m_documentId=id;emit documentIdChanged();schedule();}
void VinylGeniusService::setLyrics(const QVariantList &lyrics){
    const auto text=textualLines(lyrics);if(m_lyrics==text)return;m_lyrics=text;emit lyricsChanged();schedule();
}
void VinylGeniusService::schedule(bool refresh){
    m_refresh=refresh;
    cancel();m_projection.clear();m_referents.clear();m_seen.clear();m_songId.clear();m_error.clear();m_status="unavailable";emit dataChanged();
    if(m_scheduled)return;m_scheduled=true;
    QTimer::singleShot(0,this,[this]{m_scheduled=false;begin();});
}
bool VinylGeniusService::validContext() const {
    const auto recording=vinyl::RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(m_context.value("recording").toMap()));
    const auto title=m_context.value("trackName").toString(),artist=m_context.value("artistName").toString();
    return recording.ok()&&recording.value->kind()==vinyl::CanonicalEntityKind::Recording
        &&vinyl::QueueOccurrenceId::parse(m_context.value("occurrenceId").toString()).ok()
        &&vinyl::ProfileScopeId::parse(m_context.value("profileScope").toString()).ok()
        &&QRegularExpression("^ttd:sha256:[0-9a-f]{64}$").match(m_documentId).hasMatch()
        &&!title.trimmed().isEmpty()&&title.size()<=300&&!artist.trimmed().isEmpty()&&artist.size()<=300
        &&!m_lyrics.isEmpty()&&m_lyrics.size()<=10000;
}
QString VinylGeniusService::normalizedText(QString text){
    text=text.normalized(QString::NormalizationForm_KC).toCaseFolded();
    text.replace(QChar(0x2019),'\'').replace(QChar(0x2018),'\'');
    text.replace(QRegularExpression("[^\\p{L}\\p{N}\\p{M}']+")," ");
    return text.simplified();
}
QVariantList VinylGeniusService::matchingSongs(const QVariantMap &context,const QVariantMap &search){
    QVariantList result;QSet<QString> seen;
    for(const auto &value:search.value("hits").toList()){
        const auto hit=value.toMap(),song=hit.value("result").toMap();const auto id=numericId(song.value("id"));
        if(hit.value("type")!="song"||id.isEmpty()||seen.contains(id)||sourceUrl(song.value("url")).isEmpty())continue;
        if(titleKey(song.value("title").toString())!=titleKey(context.value("trackName").toString())
            ||versions(song.value("title").toString())!=versions(context.value("trackName").toString())||!artistMatches(context,song))continue;
        seen.insert(id);result.append(song);
    }
    return result;
}
void VinylGeniusService::begin(){
    if(!validContext())return;
    if(QDateTime::currentMSecsSinceEpoch()<m_deferredUntil){fail("Genius is temporarily rate limited.","rate_limited");return;}
    m_status="loading";m_error.clear();emit dataChanged();
    QUrlQuery query;query.addQueryItem("q",artistNames(m_context).first()+" "+m_context.value("trackName").toString());query.addQueryItem("per_page","10");
    fetch("search?"+query.toString(QUrl::FullyEncoded),[this](QVariantMap data){
        const auto candidates=matchingSongs(m_context,data);
        if(candidates.size()!=1){m_status=candidates.isEmpty()?"unavailable":"ambiguous";emit dataChanged();return;}
        m_songId=numericId(candidates.first().toMap().value("id"));emit dataChanged();
        fetch("songs/"+m_songId,[this](QVariantMap data){
            const auto song=data.value("song").toMap();
            const QVariantMap hit{{"hits",QVariantList{QVariantMap{{"type","song"},{"result",song}}}}};
            if(numericId(song.value("id"))!=m_songId||matchingSongs(m_context,hit).size()!=1){fail("The Genius song no longer matches this recording.","ambiguous");return;}
            const auto expected=m_context.value("albumName").toString(),actual=song.value("album").toMap().value("name").toString();
            if(!expected.isEmpty()&&!actual.isEmpty()&&titleKey(expected)!=titleKey(actual)){fail("The Genius release does not match the selected album.","ambiguous");return;}
            fetchPage(1);
        });
    });
}
void VinylGeniusService::fetch(const QString &path,std::function<void(QVariantMap)> completed){
    const auto generation=m_generation;
    const QString key="genius-web-v1:"+path;
    QVariantMap cached;
    if(m_provider&&!m_refresh)QMetaObject::invokeMethod(m_provider,"annotationCache",Qt::DirectConnection,Q_RETURN_ARG(QVariantMap,cached),Q_ARG(QString,key));
    if(!cached.isEmpty()){QTimer::singleShot(0,this,[this,generation,cached,completed]{if(generation==m_generation)completed(cached);});return;}
    // Sequential pacing bounds bursts even when a song has several referent pages.
    QTimer::singleShot(300,this,[this,generation,path,key,completed]{
        if(generation!=m_generation)return;
        QNetworkRequest request(QUrl("https://genius.com/api/"+path));
        request.setRawHeader("User-Agent","Colosseum/1.0 (music annotations)");
        request.setRawHeader("Accept","application/json");
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(12000);
        auto *reply=m_network->get(request);m_reply=reply;
        auto bytes=std::make_shared<QByteArray>();auto oversized=std::make_shared<bool>(false);
        connect(reply,&QIODevice::readyRead,this,[reply,bytes,oversized]{bytes->append(reply->readAll());if(bytes->size()>maxBytes){*oversized=true;reply->abort();}});
        QTimer::singleShot(12000,reply,[reply]{if(!reply->isFinished())reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[this,reply,generation,key,bytes,oversized,completed]{
            reply->deleteLater();if(m_reply==reply)m_reply=nullptr;if(generation!=m_generation)return;
            bytes->append(reply->readAll());const auto http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if(http==429){bool ok=false;auto seconds=reply->rawHeader("Retry-After").toInt(&ok);if(!ok)seconds=60;
                m_deferredUntil=QDateTime::currentMSecsSinceEpoch()+qBound(1,seconds,3600)*1000LL;
                fail("Genius is temporarily rate limited.","rate_limited");return;}
            if(*oversized||bytes->size()>maxBytes){fail("The Genius response exceeded its size limit.");return;}
            if(reply->error()!=QNetworkReply::NoError||http!=200){fail(QString("Genius could not be reached (HTTP %1).").arg(http));return;}
            const auto document=QJsonDocument::fromJson(*bytes);const auto root=document.object();
            if(!document.isObject()||root.value("meta").toObject().value("status").toInt()!=200||!root.value("response").isObject()){
                fail("The Genius response was not valid metadata.");return;}
            const auto data=root.value("response").toObject().toVariantMap();
            if(m_provider)QMetaObject::invokeMethod(m_provider,"storeAnnotationCache",Qt::DirectConnection,Q_ARG(QString,key),Q_ARG(QVariantMap,data));
            completed(data);
        });
    });
}
void VinylGeniusService::fetchPage(int page){
    fetch(QString("referents?song_id=%1&per_page=50&page=%2&text_format=plain").arg(m_songId).arg(page),[this,page](QVariantMap data){
        if(data.value("referents").metaType().id()!=QMetaType::QVariantList){fail("The Genius passage response is invalid.");return;}
        const auto rows=data.value("referents").toList();int added=0;
        for(const auto &value:rows){const auto row=value.toMap();const auto id=numericId(row.value("id"));
            if(id.isEmpty()||numericId(row.value("song_id"))!=m_songId||m_seen.contains(id))continue;
            m_seen.insert(id);m_referents.append(row);++added;}
        if(rows.size()>=50&&page<8&&added>0)fetchPage(page+1);else publish();
    });
}
void VinylGeniusService::publish(){
    m_projection=align(m_documentId,m_lyrics,m_referents);
    m_status=annotationCount()>0?"ready":"unavailable";m_refresh=false;emit dataChanged();
}
void VinylGeniusService::fail(const QString &message,const QString &status){m_projection.clear();m_status=status;m_error=message;emit dataChanged();}
void VinylGeniusService::retry(){schedule(true);}

QVariantMap VinylGeniusService::align(const QString &documentId,const QVariantList &lyrics,const QVariantList &referents){
    QString joined;QList<int> starts,ends;
    for(const auto &value:lyrics){const auto text=normalizedText(value.toMap().value("text").toString());
        if(!joined.isEmpty()&&!text.isEmpty())joined+=' ';starts.append(joined.size());joined+=text;ends.append(joined.size());}
    QVariantMap notes,anchors,groupEnds;QSet<QString> annotations,seen;int matched=0,rejected=0,ambiguous=0;
    for(const auto &value:referents){
        const auto row=value.toMap();const auto refId=numericId(row.value("id"));
        if(refId.isEmpty()||seen.contains(refId)||row.value("is_description").toBool()||row.value("is_image").toBool())continue;
        seen.insert(refId);const auto fragment=normalizedText(row.value("fragment").toString());
        if(fragment.isEmpty()||fragment.size()>16000){++rejected;continue;}
        QList<int> occurrences;int at=0,searchFrom=0;
        // Include overlaps: "again again" occurs twice in "again again again".
        while((at=joined.indexOf(fragment,searchFrom))>=0){const int end=at+fragment.size();searchFrom=at+1;
            if((at==0||joined[at-1]==' ')&&(end==joined.size()||joined[end]==' '))occurrences.append(at);
            if(occurrences.size()>1)break;}
        if(occurrences.size()!=1){++rejected;if(occurrences.size()>1)++ambiguous;continue;}
        // BC3 verifies the selected normalized text range; BC1 owns its exact document/cue identity.
        const int from=occurrences.first(),to=from+fragment.size();int selectedOrdinal=0,selectedAt=joined.indexOf(fragment);
        while(selectedAt>=0&&selectedAt<from){selectedAt=joined.indexOf(fragment,selectedAt+fragment.size());++selectedOrdinal;}
        const auto anchor=vinyl::AnnotationTextAnchor::create(joined,fragment,selectedOrdinal);
        if(selectedAt!=from||!anchor.ok()||anchor.value->rangeStart()!=from){++rejected;continue;}
        int first=-1,last=-1;
        for(int i=0;i<starts.size();++i)if(ends[i]>from&&starts[i]<to){if(first<0)first=i;last=i;}
        if(first<0){++rejected;continue;}
        QVariantList accepted;
        for(const auto &item:row.value("annotations").toList()){
            const auto raw=item.toMap();const auto id=numericId(raw.value("id"));
            const auto text=raw.value("body").toMap().value("plain");
            const auto url=sourceUrl(raw.value("url"));
            if(id.isEmpty()||raw.value("deleted").toBool()||text.metaType().id()!=QMetaType::QString
                ||text.toString().trimmed().isEmpty()||text.toString().size()>65536||url.isEmpty())continue;
            const auto linkRef=numericId(raw.value("referent_id"));if(!linkRef.isEmpty()&&linkRef!=refId)continue;
            QVariantList authors;QSet<QString> authorIds;
            for(const auto &author:raw.value("authors").toList()){
                const auto user=author.toMap().value("user").toMap();const auto name=user.value("name").toString().trimmed(),userId=numericId(user.value("id"));
                if(name.isEmpty()||name.size()>200||authorIds.contains(userId)||authors.size()>=16)continue;
                authorIds.insert(userId);authors.append(QVariantMap{{"name",name},{"url",sourceUrl(user.value("url"))},{"id",userId}});
            }
            QVariantMap note{{"id",id},{"referentId",refId},{"source","genius"},{"sourceUrl",url},{"text",text.toString().trimmed()},
                {"authors",authors},{"startLine",first},{"endLine",last},{"documentId",documentId},
                {"startCueId",lyrics[first].toMap().value("cueId")},{"endCueId",lyrics[last].toMap().value("cueId")}};
            if(raw.value("verified").metaType().id()==QMetaType::Bool)note["verified"]=raw.value("verified");
            const auto votes=raw.value("votes_total");bool ok=false;const auto n=votes.toDouble(&ok);
            if(ok&&votes.metaType().id()!=QMetaType::Bool&&votes.metaType().id()!=QMetaType::QString&&std::isfinite(n)&&std::floor(n)==n&&qAbs(n)<2147483647)note["votes"]=int(n);
            accepted.append(note);annotations.insert(id);
        }
        if(accepted.isEmpty()){++rejected;continue;}++matched;
        for(int i=first;i<=last;++i){const auto key=QString::number(i);auto list=notes.value(key).toList();
            for(const auto &note:accepted){bool duplicate=false;for(const auto &old:list)if(old.toMap().value("id")==note.toMap().value("id")){duplicate=true;break;}if(!duplicate)list.append(note);}
            notes[key]=list;}
    }
    QString previous;int group=-1;
    for(int i=0;i<lyrics.size();++i){const auto key=QString::number(i);QStringList ids;
        for(const auto &note:notes.value(key).toList())ids.append(note.toMap().value("id").toString());ids.sort();
        const auto signature=ids.join(',');if(signature.isEmpty()){previous.clear();group=-1;continue;}
        if(signature!=previous)group=i;anchors[key]=group;groupEnds[QString::number(group)]=i;previous=signature;}
    return {{"notes",notes},{"anchors",anchors},{"groupEndLines",groupEnds},{"annotationCount",annotations.size()},
        {"matchedReferentCount",matched},{"rejectedReferentCount",rejected},{"ambiguousReferentCount",ambiguous}};
}
