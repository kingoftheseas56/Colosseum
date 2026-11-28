#include "VinylRecommendationService.h"
#include "VinylProvider.h"
#include "VinylRecommendationIdentity.h"
#include "VinylSourcePool.h"
#include "DeterministicRanker.h"
#include "FeedbackTastePersistence.h"
#include "VinylRecommendationHistory.h"
#include "VinylHomeListening.h"
#include "account/ActivityStore.h"
#include "account/HistoryStore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJSValue>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QScopedValueRollback>
#include <QUuid>
#include <QJsonArray>
#include <algorithm>
#include <iterator>

using namespace vinyl;
using namespace vinyl::recommendation;
namespace {
QByteArray wire(const QVariant &value){return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);}
QString digest(const QVariant &value){return QString::fromLatin1(QCryptographicHash::hash(wire(value),QCryptographicHash::Sha256).toHex());}
QString crateKind(QString kind){if(kind=="saved-song")return "songs";return kind;}
QString crateAlias(const QString &kind,const QString &id){return "item/"+crateKind(kind)+"/"+id;}
// Automatic choices fill unused day-one slots; pins and qualified-history order stay first.
QVariantList fillCrate(QVariantList retained, const QVariantList &listeningCards,
                       const QVariantList &albums, const QVariantList &songs, const QVariantList &picked) {
    auto identityKey=[](const QVariantMap &card) {
        const auto ref=vinyl::RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(card.value("ref").toMap()));
        if(ref.ok())return ref.value->stableKey();
        return card.value("key",card.value("id")).toString();
    };
    auto present=[](QVariantMap card){const auto seed=card.value("seedArtist").toMap();
        if(card.value("kind")=="radio"&&seed.value("kind")=="artist"&&!seed.value("name").toString().isEmpty()){
            card["kind"]="artists";card["title"]=seed.value("name");card["artistId"]="ytm:artist:"+seed.value("providerId").toString();
        }return card;};
    QSet<QString> pickedIds;for(const auto &v:picked)pickedIds.insert(v.toMap().value("providerId").toString());
    auto artists=[&](const QVariantMap &card){QSet<QString> ids;auto refs=card.value("artistRefs").toList();refs.append(card.value("seedArtist"));
        for(const auto &v:refs){const auto ref=v.toMap();if(ref.value("provider")=="youtube-music"&&pickedIds.contains(ref.value("providerId").toString()))ids.insert(ref.value("providerId").toString());}
        auto names=card.value("record").toMap().value("artistIds").toStringList();names.append(card.value("artistId").toString());
        for(auto id:names)if(id.startsWith("ytm:artist:")&&pickedIds.contains(id.mid(11)))ids.insert(id.mid(11));return ids;};
    auto releases=[](const QVariantMap &card){QSet<QString> ids;const bool album=crateKind(card.value("kind").toString())=="albums";
        const auto ref=card.value("albumRef").toMap(),record=card.value("record").toMap();
        for(const auto &v:{ref.value("providerId"),ref.value("mbid"),card.value("releaseGroupMbid")})if(!v.toString().isEmpty())ids.insert(v.toString());
        if(album)for(const auto &v:{card.value("id"),card.value("albumId"),card.value("providerId")})if(!v.toString().isEmpty())ids.insert(v.toString());
        if(record.value("type")!="Song")for(const auto &v:{record.value("id"),record.value("browseId"),record.value("providerIds").toMap().value("youtube-music")})if(!v.toString().isEmpty())ids.insert(v.toString());
        auto aliases=ids;for(const auto &id:aliases)if(id.startsWith("ytm:album:"))ids.insert(id.mid(10));return ids;};
    QSet<QString> used,albumIds;QMap<QString,int> counts;
    auto remember=[&](const QVariantMap &card){used.insert(identityKey(card));for(const auto &id:artists(card))++counts[id];
        if(crateKind(card.value("kind").toString())=="albums")albumIds.unite(releases(card));};
    for(auto &entry:retained){entry=present(entry.toMap());remember(entry.toMap());}
    QMap<QString,QVariantList> groups{{"albums",albums},{"songs",songs}};
    for(const auto &v:listeningCards){const auto card=present(v.toMap());groups[card.value("kind").toString()].append(card);}
    QSet<QString> unplacedArtists;for(const auto &v:groups["artists"])unplacedArtists.unite(artists(v.toMap()));
    const QStringList order{"albums","artists","songs","albums","mixes","artists","albums","songs",
        "artists","albums","songs","artists","mixes","albums","artists","songs"};
    while(retained.size()<16){
        const auto last=retained.isEmpty()?QVariantMap():retained.last().toMap();const auto lastKind=crateKind(last.value("kind").toString());
        const bool repeat=retained.size()>1&&lastKind==crateKind(retained[retained.size()-2].toMap().value("kind").toString());
        QStringList choices{order[retained.size()%16],"albums","artists","songs","mixes"};choices.removeDuplicates();bool added=false;
        if(lastKind=="artists"&&choices.first()==lastKind){choices.removeAll(lastKind);choices.append(lastKind);}
        for(const auto &kind:choices){if(repeat&&kind==lastKind)continue;auto &group=groups[kind];int best=-1,bestCount=5;
            for(int index=0;index<group.size();++index){const auto card=group[index].toMap();const auto key=identityKey(card);const auto credits=artists(card);auto ids=releases(card);
                if(crateKind(card.value("kind").toString())!=kind||key.isEmpty()||used.contains(key)||card.value("id").toString().isEmpty()||card.value("title").toString().isEmpty()
                    ||card.value("unavailable").toBool()||(card.contains("available")&&!card.value("available").toBool())
                    ||QStringList{"loading","error","unavailable"}.contains(card.value("status").toString()))continue;
                if(kind!="mixes"&&credits.isEmpty())continue;if(kind=="albums"&&ids.intersects(albumIds))continue;
                if(((kind=="songs"&&lastKind=="albums")||(kind=="albums"&&lastKind=="songs"))&&ids.intersects(releases(last)))continue;
                int count=0;for(const auto &id:credits)count=qMax(count,counts.value(id));if(count>=4)continue;
                if(kind!="artists"&&std::any_of(credits.cbegin(),credits.cend(),[&](const QString &id){return counts.value(id)>=3&&unplacedArtists.contains(id);}))continue;
                bool unrepresented=false;for(const auto &id:pickedIds)unrepresented|=counts.value(id)==0;if(count>=3&&unrepresented)continue;
                if(count<bestCount){best=index;bestCount=count;}
            }
            if(best<0)continue;auto card=group.takeAt(best).toMap();const auto key=identityKey(card);
            if(card.value("key").toString().isEmpty())card["key"]=key;card["pinned"]=false;remember(card);retained.append(card);if(kind=="artists")unplacedArtists.subtract(artists(card));added=true;break;
        }if(!added)break;
    }
    return retained;
}
QVariantList catalogueCrateCards(const QVariantMap &collections,const QString &filterKind={},const QString &filterId={}){
    QVariantList cards;
    const auto historyCatalogue=collections.value("catalogueAlbums").toList()+collections.value("albums").toList();
    const auto rememberCrate=[&](QVariantMap card){
        if(!card.value("id").toString().isEmpty()&&!card.value("title").toString().isEmpty())cards.append(std::move(card));
    };
    if(filterKind!="songs")for(const auto &value:historyCatalogue){
        const auto entry=value.toMap(),album=entry.value("album",entry).toMap();if(album.isEmpty())continue;
        const bool yt=album.value("origin")=="youtube-music";
        auto raw=album;raw["kind"]="album";raw["provider"]=yt?"youtube-music":"local";
        raw["providerId"]=yt?album.value("providerIds").toMap().value("youtube-music",album.value("browseId")):album.value("id");
        if(filterKind=="albums"&&album.value("id")!=filterId)continue;
        if(yt)raw["id"]="ytm:album:"+raw.value("providerId").toString();
        const auto converted=VinylRecommendationIdentity::fromCatalogue(raw);if(!converted.value("ok").toBool())continue;
        QVariantMap card{{"id",album.value("id")},{"albumId",album.value("id")},{"kind","albums"},
            {"key",converted.value("key")},{"ref",converted.value("ref")},{"typedKey",converted.value("ref")},
            {"destination",converted.value("destination")},{"title",album.value("title")},{"cover",album.value("cover")},
            {"provider",raw.value("provider")},{"providerId",raw.value("providerId")},
            {"subtitle",album.value("artists").toStringList().join(QStringLiteral(" · "))},{"record",album}};
        if(filterKind.isEmpty()||filterKind=="albums")rememberCrate(card);
        if(filterKind=="albums")continue;
        const auto ids=album.value("artistIds").toStringList(),names=album.value("artists").toStringList();
        for(int index=0;index<ids.size();++index){
            if(!filterId.isEmpty()&&ids[index]!=filterId)continue;
            const bool remote=ids[index].startsWith("ytm:artist:");
            const QVariantMap artist{{"kind","artist"},{"provider",remote?"youtube-music":"local"},
                {"providerId",remote?ids[index].mid(11):ids[index]},{"id",ids[index]}};
            const auto identity=VinylRecommendationIdentity::fromCatalogue(artist);if(!identity.value("ok").toBool())continue;
            rememberCrate({{"id",ids[index]},{"artistId",ids[index]},{"kind","artists"},{"title",names.value(index)},
                {"provider",artist.value("provider")},{"providerId",artist.value("providerId")},
                {"key",identity.value("key")},{"ref",identity.value("ref")},{"destination",identity.value("destination")},{"cover",album.value("cover")}});
        }
    }
    if(filterKind.isEmpty()||filterKind=="songs")for(const auto &value:collections.value("catalogueSongs",collections.value("songs")).toList()){
        auto card=value.toMap();const auto row=card.value("row").toMap(),track=row.value("track").toMap();if(row.isEmpty())continue;
        if(!filterId.isEmpty()&&card.value("id")!=filterId)continue;
        const auto edition=row.value("edition").toMap(),album=edition.value("sourceAlbum").toMap();
        const bool yt=edition.value("origin")=="youtube-music"||album.value("origin")=="youtube-music";
        const auto identity=VinylRecommendationIdentity::fromCatalogue({{"kind","song"},{"provider",yt?"youtube-music":"local"},
            {"providerId",yt?track.value("videoId"):row.value("trackId")}});
        if(!identity.value("ok").toBool())continue;
        card["kind"]="saved-song";card["key"]=identity.value("key");card["ref"]=identity.value("ref");
        card["destination"]=QVariantMap{{"kind","saved-song"},{"id",card.value("id")}};
        card["provider"]=yt?"youtube-music":"local";card["providerId"]=yt?track.value("videoId"):row.value("trackId");
        card["albumId"]=album.value("providerIds").toMap().value("youtube-music",row.value("albumId"));
        rememberCrate(card);
    }
    return cards;
}
QVariant plainHome(const QVariant &value){return value.metaType().id()==qMetaTypeId<QJSValue>()?value.value<QJSValue>().toVariant():value;}
QVariantMap datedRelease(QVariantMap row,const QVariantMap &identity,const QVariantMap &discography){
    if(row.value("kind")!="album"||identity.value("status")!="verified"
        ||discography.value("artist").toMap().value("id")!=identity.value("mbid"))return row;
    const auto title=row.value("title").toString().trimmed().toCaseFolded();
    QVariantMap matched;
    for(const auto &entry:discography.value("artist").toMap().value("releaseGroups").toList()){
        const auto group=entry.toMap();
        if(group.value("title").toString().trimmed().toCaseFolded()!=title)continue;
        if(!matched.isEmpty())return row; // Ambiguous titles do not supply a date.
        matched=group;
    }
    const auto date=matched.value("first-release-date").toString();
    if(!QRegularExpression("^[0-9]{4}-[0-9]{2}-[0-9]{2}$").match(date).hasMatch()||!QDate::fromString(date,Qt::ISODate).isValid())return row;
    const auto year=row.value("year").toString();if(!year.isEmpty()&&year!=date.left(4))return row;
    row["releaseDate"]=date;row["dateSource"]="musicbrainz";
    row["dateEvidence"]=QVariantMap{{"artistMbid",identity.value("mbid")},{"releaseGroupMbid",matched.value("id")}};
    // Date evidence does not promote a title match to canonical album identity.
    return row;
}
QString artistNameKey(const QString &name) {
    return name.normalized(QString::NormalizationForm_C).simplified().toCaseFolded();
}
QString hitTitle(QString title) {
    static const QRegularExpression annotation(
        R"(\s*[\(\[]\s*(?:(?:feat(?:uring)?|ft|with)\b|(?:[0-9]{4}\s+)?remaster\w*\b|explicit\b|clean\b|radio\s+edit\b)[^\)\]]*[\)\]])");
    static const QRegularExpression suffix(
        R"(\s+(?:(?:feat(?:uring)?|ft)\.?\s+|with\.\s+|[-–—]\s*(?:with\b|(?:[0-9]{4}\s+)?remaster\w*\b|explicit\b|clean\b)|(?:[-–—]\s*)?radio\s+edit\b).*$)");
    static const QRegularExpression punctuation(R"([^\p{L}\p{N}\p{M}])");
    return title.normalized(QString::NormalizationForm_KC).toCaseFolded().remove(annotation).remove(suffix).remove(punctuation);
}
QList<QVariantMap> relatedRows(const QVariantMap &data) {
    QList<QVariantMap> result;
    QSet<QString> channels;
    for(const auto &entry:data.value("items").toList()) {
        const auto row=entry.toMap();const auto id=row.value("providerId").toString();
        if(row.value("relation")!="related-artist"||channels.contains(id))continue;
        channels.insert(id);result.append(row);
        if(result.size()==10)break; // Same bound as the YouTube related-artist shelf.
    }
    return result;
}
struct SimilarArtist {
    QVariantMap card;
    QString mbid;
    QSet<QString> names;
    QMap<QString,QMap<QString,double>> bySeed;
    double strength() const {
        double sum=0;
        for(const auto &sources:bySeed)for(double score:sources)sum+=score;
        return sum;
    }
};
QVariantMap matchingChannel(const SimilarArtist &match,const QList<QVariantMap> &related) {
    QVariantMap result;
    for(const auto &row:related) {
        if(!match.names.contains(artistNameKey(row.value("title").toString())))continue;
        if(!result.isEmpty())return {}; // Multiple channels with the same name are ambiguous.
        result=row;
    }
    return result;
}
QList<SimilarArtist> similarArtists(const QMap<QString,QVariantMap> &evidence,const QSet<QString> &seeds) {
    struct Observation {QVariantMap card;QString seed,source,name,mbid;double score;};
    QList<Observation> observations;
    QMap<QString,QSet<QString>> idsByName;
    for(const auto &data:evidence) {
        const auto seed=data.value("seed").toMap().value("mbid").toString();
        if(!seeds.contains(seed))continue;
        const auto items=data.value("items").toList();
        double peak=0;
        int lastRank=1;
        for(const auto &value:items) {
            const auto item=value.toMap();
            peak=qMax(peak,item.value("similarity").toDouble());
            lastRank=qMax(lastRank,item.value("rank").toInt());
        }
        for(const auto &value:items) {
            const auto item=value.toMap();
            const auto card=item.value("catalogueItem").toMap();
            const auto name=artistNameKey(item.value("title").toString());
            if(name.isEmpty()||card.isEmpty())continue;
            const auto mbid=card.value("hints").toMap().value("mbidEvidence").toMap().value("artist").toString().toLower();
            if(!mbid.isEmpty())idsByName[name].insert(mbid);
            const double rank=1.0-double(item.value("rank").toInt()-1)/qMax(1,lastRank-1);
            const double score=peak>0&&item.contains("similarity")?(rank+item.value("similarity").toDouble()/peak)/2:rank;
            // Independent source/seed support must outweigh one strong listing.
            observations.append({item,seed,item.value("provider").toString(),name,mbid,1.1+score});
        }
    }
    QMap<QString,SimilarArtist> combined;
    for(const auto &entry:observations) {
        auto mbid=entry.mbid;
        const auto ids=idsByName.value(entry.name);
        if(mbid.isEmpty()&&ids.size()==1)mbid=*ids.begin();
        if(mbid.isEmpty()&&ids.size()>1)continue; // No name-only joins across conflicting IDs.
        auto &artist=combined[mbid.isEmpty()?"name:"+entry.name:"mbid:"+mbid];
        artist.mbid=mbid;
        if(ids.size()<2)artist.names.insert(entry.name);
        auto &score=artist.bySeed[entry.seed][entry.source];
        score=qMax(score,entry.score); // Last.fm aliases are not independent sources.
        if(artist.card.isEmpty()||wire(entry.card)<wire(artist.card))artist.card=entry.card;
    }
    auto result=combined.values();
    std::stable_sort(result.begin(),result.end(),[](const auto &a,const auto &b){return a.strength()>b.strength();});
    return result;
}
void cancelLane(QObject *source,const QString &lane){
    if(!source)return;
    if(source->metaObject()->indexOfMethod("cancel(QString)")>=0)QMetaObject::invokeMethod(source,"cancel",Qt::DirectConnection,Q_ARG(QString,lane));
    else QMetaObject::invokeMethod(source,"cancel",Qt::DirectConnection,Q_ARG(QVariant,QVariant(lane)));
}
struct HomeCacheQuery {QString operation,value;QVariantMap options;};
struct HomeCache {
    QString root;
    qint64 now=0;
    explicit HomeCache(QObject *source) {
        if(source){root=source->property("recommendationCacheRoot").toString();
            if(!root.isEmpty())now=source->property("recommendationCacheNow").toLongLong();}
    }
    QVariantMap read(const HomeCacheQuery &query) const {
        const auto &[op,value,options]=query;
        // The provider validates and normalizes the cache envelope on read.
        return VinylProvider::cachedRecommendationAt(root,now,op,value,options);
    }
};
// Legacy/QML test ports supply in-memory fixtures on their owning thread.
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
QVariantMap policy(){return {{"version","vinyl-home-v1"},{"seedAffinity",300},{"maxCandidates",300},{"maxGenerator",60},{"maxShelf",12},{"maxUncachedArtists",6},{"maxConcurrent",2},{"deadlineMs",8000},{"newWindowMonths",12},{"maxArtist",3},{"maxRecordingReleaseGroup",2},{"historyComplete",false}};}
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
    struct Job {QString operation,value,key;QVariantMap seed;bool force=false;QVariantMap options;bool enrichment=false;bool anchorOnly=false;};
    struct Active {int id;Job job;};
    QVariantMap collections,snapshot,history;
    QVariantList sections,seeds;
    QStringList excludedArtistProviderIds;
    QMap<QString,QVariantMap> evidence,identities,population;
    QMap<QString,QVariantMap> charts,similarity,discography,neighbors,parents,topTracks;
    QVariantMap anchor;
    QSet<QString> metadataAttempted,metadataReading;
    QMap<QString,QVariantMap> metadataCache;
    bool seedCachePending=false;
    bool firstCorePublicationPending=false;
    bool composing=false,recompose=false,recomposeImmediate=false;
    quint64 compositionInputs=0;
    QTimer *visitTimer=nullptr;
    QVariantMap pool;
    QMap<QString,QVariantMap> crateCandidates;
    QVariantMap crateCandidate(const QVariantMap &item) const {
        const auto key=item.value("key").toString();
        const auto original=item.value("libraryItem",item).toMap();
        const auto id=original.value("id").toString(),kind=crateKind(item.value("kind").toString());
        auto candidate=crateCandidates.value(crateAlias(kind,id));
        if(candidate.isEmpty())candidate=crateCandidates.value(key);
        // Library pinning is independent of the first deferred Home publication.
        // Only resolve an owned item from the current catalogue, not caller data.
        if(candidate.isEmpty()&&!id.isEmpty()&&(kind=="songs"||kind=="albums"||kind=="artists")){
            const auto owned=catalogueCrateCards(collections,kind,id);
            if(!owned.isEmpty())candidate=owned.last().toMap();
        }
        return candidate;
    }
    bool forceMetadata=false;
    bool neighborsPrepared=false;
    QMap<QString,Active> active;
    QList<Job> waiting;
    QStringList retireAnchorJobs() {
        auto retire=[&](const Job &job){
            if(!job.anchorOnly)return false;
            metadataAttempted.remove(digest(QVariantList{job.operation,job.value,job.options}));
            return true;
        };
        waiting.removeIf(retire);
        QStringList lanes;
        for(auto it=active.begin();it!=active.end();) {
            if(retire(it->job)){lanes.append(it.key());it=active.erase(it);}
            else ++it;
        }
        return lanes;
    }
    static bool core(const Job &job){return job.operation=="artistCandidates"&&!job.enrichment;}
    bool corePending() const {
        return std::any_of(waiting.cbegin(),waiting.cend(),core)
            ||std::any_of(active.cbegin(),active.cend(),[](const Active &work){return core(work.job);});
    }
    struct SongJob {QString key,video;QVariantMap seed,recording;};
    struct SongActive {int id;SongJob job;};
    QList<SongJob> songWaiting;
    QMap<QString,SongActive> songActive;
    QHash<QString,quint64> songCacheReads;
    quint64 songCacheSerial=0;
    QMap<QString,QVariantMap> songEvidence,previousSongEvidence;
    QMap<QString,QVariantMap> retainedSongEvidence() const {
        const bool usable=std::any_of(songEvidence.cbegin(),songEvidence.cend(),[](const QVariantMap &data){return !data.value("items").toList().isEmpty();});
        auto retained=usable?songEvidence:previousSongEvidence;
        // One prior selection: at most two requests for each of six artists.
        while(retained.size()>12)retained.erase(std::prev(retained.end()));
        for(auto &data:retained)data["_stale"]=true;
        return retained;
    }
    QSet<QString> songAttempted,songFailed;
    QVariantList songSeeds,quickPicks;
    QString songSelection,listeningHash;
    QTimer *songDeadline=nullptr;
    bool listeningRebuildQueued=false;
    QSet<QString> failed,optionalUnavailable;
    QString seedHash,status="idle",error,visitId;
    std::function<qint64()> clock;
    // Independent of the current refresh, seed display fields and date window.
    QHash<QString,qint64> canonicalRetryAt;
    static QString canonicalWorkKey(const QString &op,const QString &value){
        return op=="recommendationIdentity"||op=="recommendationDiscography"?op+":"+value:QString();
    }
    bool canonicalDeferred(const QString &op,const QString &value) const {
        const auto key=canonicalWorkKey(op,value);
        return !key.isEmpty()&&canonicalRetryAt.value(key)>clock();
    }
    void deferCanonicalFailure(const Job &job,int retryAfterSeconds=0){
        const auto key=canonicalWorkKey(job.operation,job.value);
        if(!key.isEmpty())canonicalRetryAt[key]=clock()+1000LL*qBound(300,retryAfterSeconds,3600);
    }
    QTimer *deadline=nullptr;
    QTimer *publishTimer=nullptr;
    QTimer *inputsTimer=nullptr;
    bool dirty=true,collectionsDirty=false;
    qint64 evaluatedAt=0;
    quint64 epoch=0;
    int revision=0,cursor=0,nextCursor=0;
    bool visited=false,refreshed=false;
    bool persisting=false;
    quint64 activityReset=0;
    qint64 historyReset=0;
    std::unique_ptr<VinylHomeListening> listening;
    QVariantList carousel;
    QMap<QString,QVariantMap> retainedCarousel;
    QString selectionKey;
    QStringList selectionKeys, previousKeys;
    qint64 selectionSerial=0;
};

// Value-only input/output of ranking. No owner objects, SQL handles, QML
// values, or callbacks cross the worker boundary.
struct VinylRecommendationService::HomeComposition {
    QString scope,feedback,storedExposure,visitId,seedHash,status,error;
    quint64 activityReset=0;
    qint64 historyReset=0,now=0;
    bool keepHistory=false,historyStoreBound=false,loading=false;
    bool clearExposure=false,feedbackError=false,valid=false;
    VinylRecommendationHistory::Inputs historyInputs;
    QVariantList seeds,saved,baselineQuickPicks,songSeeds,sections,quickPicks,crateAlbums,crateSongs;
    QStringList selectionKeys,excludedArtistProviderIds;
    QVariantMap collections,history,snapshot,charts,chartWeeks;
    QMap<QString,QVariantMap> chartFeeds;
    QVariantList catalogueCrateCards, playedAlbums, similarArtistCards;
    QVariantMap anchor, rowCandidates;
    QMap<QString,int> crateCounts;
    QMap<QString,qint64> crateLast;
    QMap<QString,QVariantMap> evidence,identities,discography,similarity,neighbors,parents,songEvidence,previousSongEvidence,topTracks;
    QMap<QString,QVariantMap> retainedCarousel;
    // Keep node iteration/alias insertion order unchanged in publication.
    QVariantList rankedCrateCards;
    QSet<QString> failed;
    qsizetype optionalUnavailable=0,songAttempted=0,songFailed=0,songPending=0;
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
        || activityReset!=m_home->activityReset || historyReset!=m_home->historyReset){
        storeExposure({});
        m_home->selectionKey.clear();m_home->selectionKeys.clear();m_home->previousKeys.clear();m_home->carousel.clear();
        // Do not retain a published history-derived model during the deferred window.
        m_home->history.clear();m_home->sections.clear();m_home->pool.clear();m_home->snapshot.clear();
        m_home->anchor.clear();
        const auto retired=m_home->retireAnchorJobs();
        for(const auto &lane:retired)cancelLane(m_source,lane);
        if(!retired.isEmpty())QTimer::singleShot(0,this,[this,epoch=m_home->epoch]{
            if(m_active&&m_home->epoch==epoch)pumpHome();
        });
        const bool hadQuickPicks=!m_home->quickPicks.isEmpty();
        m_home->quickPicks.clear();
        ++m_home->revision;
        emit homeChanged();
        if(hadQuickPicks)emit listeningChanged();
        if(m_preferences && !m_preferences->property("vinylHomeSelectionSnapshot").toString().isEmpty()
            &&m_preferences->metaObject()->indexOfMethod("setVinylHomeSelectionSnapshot(QString)")>=0){
            bool saved=false;QScopedValueRollback<bool> guard(m_home->persisting,true);
            QMetaObject::invokeMethod(m_preferences,"setVinylHomeSelectionSnapshot",Qt::DirectConnection,
                Q_RETURN_ARG(bool,saved),Q_ARG(QString,QString()));
        }
    }
    m_home->activityReset=activityReset;m_home->historyReset=historyReset;
    markHomeDirty();
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
    if(saved){++m_home->compositionInputs;m_home->evaluatedAt=now;rebuildHome(true);}return saved;
}
bool VinylRecommendationService::isCratePinned(const QVariantMap &item,int generation) const {
    if(!m_active||generation!=m_generation||!m_preferences)return false;
    const auto pins=m_preferences->property("vinylCratePins").toList();if(pins.isEmpty())return false;
    auto key=m_home->crateCandidate(item).value("key").toString();if(key.isEmpty())key=item.value("key").toString();
    for(const auto &pin:pins)if(pin.toMap().value("key")==key)return true;
    return false;
}
bool VinylRecommendationService::setCratePin(const QVariantMap &item,bool pinned,int generation) {
    if(!m_active||m_scope.isEmpty()||generation!=m_generation||!m_preferences)return false;
    const auto card=m_home->crateCandidate(item);
    auto key=card.value("key").toString();if(key.isEmpty()&&!pinned)key=item.value("key").toString();
    if(key.isEmpty())return false;
    auto pins=m_preferences->property("vinylCratePins").toList();
    pins.removeIf([&](const QVariant &value){return value.toMap().value("key")==key;});
    if(pinned){
        if(card.isEmpty()||card.value("unavailable").toBool())return false;
        QVariantMap snapshot;
        for(const auto &field:{"key","id","kind","title","artist","albumTitle","subtitle","cover","provider","providerId","albumId","artistId","mixId","playlistId","editionId","trackId","trackIndex","ref","seedArtist","catalogueItem"})
            if(card.contains(field))snapshot[field]=card.value(field);
        // Keep the same known display artist after live record/credit metadata disappears.
        // Store text only; these names do not attest an artist identity.
        if(snapshot.value("artist").toString().isEmpty()) {
            const auto displayNames=[](const QVariant &values) {
                QStringList names;
                for(const auto &value:values.toList()) {
                    const auto row=value.toMap();
                    const auto name=(value.metaType().id()==QMetaType::QString?value.toString():row.value("name",row.value("title")).toString()).trimmed();
                    if(!name.isEmpty())names.append(name);
                }
                return names.join(QStringLiteral(" · "));
            };
            const auto record=card.value("record").toMap();
            const auto track=record.value("tracks").toList().value(card.value("trackIndex").toInt()).toMap();
            auto artist=displayNames(track.value("artists"));
            if(artist.isEmpty())artist=displayNames(card.value("artistRefs"));
            if(artist.isEmpty())artist=displayNames(record.value("artists"));
            if(artist.isEmpty())artist=card.value("row").toMap().value("artist").toString();
            if(!artist.isEmpty())snapshot["artist"]=artist;
        }
        pins.append(snapshot);
    }
    bool saved=false;{
        QScopedValueRollback<bool> guard(m_home->persisting,true);
        QMetaObject::invokeMethod(m_preferences,"setVinylCratePins",Qt::DirectConnection,
            Q_RETURN_ARG(bool,saved),Q_ARG(QVariantList,pins));
    }
    if(saved){++m_home->compositionInputs;rebuildHome(true);}return saved;
}
bool VinylRecommendationService::recordVisibleItems(const QString &shelf,const QString &snapshot,const QVariantList &refs,int generation) {
    if(!m_active || generation!=m_generation || !m_keepHistory || !m_home->visited || refs.isEmpty() || refs.size()>12
        || snapshot!=m_home->snapshot.value("exposureSnapshotId").toString() || snapshot.isEmpty())return false;
    const auto activity=qobject_cast<ActivityStore*>(m_activityStore.data());
    if(!activity || !activity->retentionEnabled() || !activity->healthy())return false;
    const auto profile=ProfileScopeId::parse(m_scope);if(!profile.ok())return false;
    QVariantList items;
    const QString homeKey=shelf=="from-your-artists"?"albums-for-you":shelf=="related-discoveries"?"artists-like-yours":
        shelf=="rediscover-library"?"rediscover":shelf;
    for(const auto &entry:m_home->pool.value("rows").toList()) {
        const auto row=entry.toMap();if(row.value("key")==homeKey)items=row.value("items").toList();
    }
    if(items.isEmpty()&&shelf!="because-you-played") {
        for(const auto &section:m_home->sections)if(section.toMap().value("id")==shelf)items=section.toMap().value("items").toList();
        if(shelf=="quick-picks")items=m_home->quickPicks;
    }
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
    // Persist current-visit exposure for the next visit, keeping this Home composition stable.
    return true;
}

void VinylRecommendationService::initializeHome(std::function<qint64()> clock){
    m_home=std::make_shared<HomeState>();m_home->clock=clock?std::move(clock):std::function<qint64()>([]{return QDateTime::currentMSecsSinceEpoch();});
    m_home->deadline=new QTimer(this);m_home->deadline->setSingleShot(true);
    m_home->publishTimer=new QTimer(this);m_home->publishTimer->setSingleShot(true);
    m_home->publishTimer->setInterval(100);
    // Input invalidations have a separate window from catalogue publication.
    m_home->inputsTimer=new QTimer(this);
    m_home->inputsTimer->setObjectName("homeInputsTimer");
    m_home->inputsTimer->setSingleShot(true);
    m_home->inputsTimer->setInterval(2000);
    connect(m_home->inputsTimer,&QTimer::timeout,this,&VinylRecommendationService::flushHomeInputs);
    m_home->visitTimer=new QTimer(this);
    m_home->visitTimer->setObjectName("homeVisitTimer");
    m_home->visitTimer->setSingleShot(true);
    connect(m_home->visitTimer,&QTimer::timeout,this,[this]{
        if(!m_home->visited||!canEnterVinyl())return;
        if(m_home->dirty)flushHomeInputs();
        else refreshHome(false);
    });
    connect(m_home->publishTimer,&QTimer::timeout,this,[this]{if(m_active&&m_home->visited)rebuildHome();});
    m_home->songDeadline=new QTimer(this);m_home->songDeadline->setSingleShot(true);
    connect(m_home->songDeadline,&QTimer::timeout,this,[this]{
        for(const auto &work:m_home->songActive)m_home->songFailed.insert(work.job.key);
        for(const auto &job:m_home->songWaiting)m_home->songFailed.insert(job.key);
        cancelSongCandidates(false);rebuildHome();
    });
    m_home->listening=std::make_unique<VinylHomeListening>(nullptr,
        [this]{emit listeningChanged();queueListeningRebuild();},
        [this](const QVariantList &songs,bool append){emit homeRadioBatchReady(songs,append);});
    connect(m_home->deadline,&QTimer::timeout,this,[this]{
        QStringList lanes;
        for(auto it=m_home->active.begin();it!=m_home->active.end();) {
            if(HomeState::core(it->job)){m_home->failed.insert(it->job.key);lanes.append(it.key());it=m_home->active.erase(it);}else ++it;
        }
        m_home->waiting.removeIf([this](const HomeState::Job &job){if(!HomeState::core(job))return false;m_home->failed.insert(job.key);return true;});
        for(const auto &lane:lanes)cancelLane(m_source,lane);
        if(!m_home->failed.isEmpty())m_home->error="Some catalogue enrichment did not finish.";
        queueHomeEnrichment();pumpHome();
    });
}
VinylRecommendationService::~VinylRecommendationService(){cancelSongCandidates(false);cancelHome(false);}
void VinylRecommendationService::setSource(QObject *value){
    if(m_source==value)return;const bool visited=m_home->visited;cancelSearch();m_home->canonicalRetryAt.clear();cancelHome(true);
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
QVariantMap VinylRecommendationService::homeSourcePool() const{return m_home->pool;}
QVariantList VinylRecommendationService::homeCarouselItems() const{return m_home->carousel;}
QVariantList VinylRecommendationService::homeListeningCards() const{return m_home->listening->cards();}
QVariantList VinylRecommendationService::homeQuickPicks() const{return m_home->quickPicks;}
QVariantMap VinylRecommendationService::homeMixView() const{return m_home->listening->view();}
bool VinylRecommendationService::homeRadioActive() const{return m_home->listening->radioActive();}
QString VinylRecommendationService::homeRadioState() const{return m_home->listening->radioState();}
QVariantMap VinylRecommendationService::homeRadioContext() const{return m_home->listening->radioContext();}
void VinylRecommendationService::openHomeMix(const QString &kind){if(m_active&&canEnterVinyl())m_home->listening->openMix(kind);}
void VinylRecommendationService::startHomeRadio(const QVariantMap &artist){if(m_active&&canEnterVinyl())m_home->listening->startRadio(artist);}
void VinylRecommendationService::refillHomeRadio(const QStringList &queuedTracks){if(m_active&&canEnterVinyl())m_home->listening->refillRadio(queuedTracks);}
void VinylRecommendationService::stopHomeRadio(){m_home->listening->stopRadio();}
void VinylRecommendationService::restoreHomeRadio(const QVariantMap &context,const QStringList &queuedTracks){if(m_active&&canEnterVinyl())m_home->listening->restoreRadio(context,queuedTracks);}
void VinylRecommendationService::refreshHomeSelection(){
    if(!m_active||!canEnterVinyl())return;
    ++m_home->compositionInputs;
    selectHomeCarousel(true);emit homeChanged();
    refreshHome(false);
}
void VinylRecommendationService::setHomeCollections(const QVariantMap &value){
    if(value==m_home->collections)return;m_home->collections=value;
    m_home->collectionsDirty=true;
    markHomeDirty();
}
void VinylRecommendationService::cancelHome(bool clear){
    if(!m_home)return;++m_home->epoch;m_home->deadline->stop();m_home->publishTimer->stop();
    m_home->inputsTimer->stop();m_home->visitTimer->stop();
    m_home->seedCachePending=false;m_home->firstCorePublicationPending=false;
    m_home->metadataReading.clear();m_home->metadataCache.clear();
    m_home->recompose=false;m_home->recomposeImmediate=false;
    if(clear)m_home->dirty=true;
    if(clear){cancelSongCandidates(true);m_home->crateCandidates.clear();m_home->quickPicks.clear();m_home->listeningHash.clear();m_home->listening->clear();m_home->carousel.clear();m_home->selectionKey.clear();m_home->selectionKeys.clear();m_home->previousKeys.clear();m_home->selectionSerial=0;}
    const auto lanes=m_home->active.keys();m_home->active.clear();m_home->waiting.clear();for(const auto &lane:lanes)cancelLane(m_source,lane);
    if(clear){m_home->evidence.clear();m_home->identities.clear();m_home->population.clear();m_home->charts.clear();m_home->similarity.clear();m_home->topTracks.clear();m_home->anchor.clear();m_home->discography.clear();m_home->neighbors.clear();m_home->parents.clear();m_home->metadataAttempted.clear();m_home->pool.clear();m_home->failed.clear();m_home->optionalUnavailable.clear();m_home->sections.clear();m_home->snapshot.clear();m_home->seeds.clear();m_home->seedHash.clear();m_home->excludedArtistProviderIds.clear();m_home->status="idle";m_home->error.clear();m_home->visited=false;m_home->refreshed=false;m_home->cursor=0;m_home->nextCursor=0;++m_home->revision;emit homeChanged();}
}
void VinylRecommendationService::clearHomeProfile(){m_home->collections.clear();m_home->history.clear();m_home->visitId.clear();m_home->canonicalRetryAt.clear();cancelHome(true);}
void VinylRecommendationService::homeSeedsChanged(bool refresh){
    QVariantList input=m_saved;
    for(const auto &entry:m_home->collections.value("artists").toList()){
        auto row=entry.toMap();row["id"]=row.value("artistId",row.value("id"));input.append(row);
    }
    auto seeds=uniqueArtists(input);
    std::sort(seeds.begin(),seeds.end(),[](const QVariant &a,const QVariant &b){return a.toMap().value("key").toString()<b.toMap().value("key").toString();});
    const auto hash=digest(seeds);if(hash==m_home->seedHash)return;
    const bool visited=m_home->visited;cancelHome(true);m_home->visited=visited;m_home->seeds=seeds;m_home->seedHash=hash;
    if(visited&&refresh)QTimer::singleShot(0,this,[this,epoch=m_home->epoch]{if(m_home->visited&&m_home->epoch==epoch)refreshHome(false);});
}
void VinylRecommendationService::markHomeDirty() {
    ++m_home->compositionInputs;
    m_home->dirty=true;
    if(m_active&&m_home->visited&&!m_home->inputsTimer->isActive())
        m_home->inputsTimer->start();
}
void VinylRecommendationService::flushHomeInputs() {
    if(!m_active||!m_home->visited||!m_home->dirty)return;
    m_home->inputsTimer->stop();
    if(m_home->collectionsDirty){
        m_home->collectionsDirty=false;
        homeSeedsChanged(false);
    }
    m_home->evaluatedAt=m_home->clock();
    if(!m_home->refreshed)refreshHome(false);
    else rebuildHome();
}
void VinylRecommendationService::leaveHome() {
    m_home->visited=false;
    m_home->visitTimer->stop();
    m_home->inputsTimer->stop();
    m_home->publishTimer->stop();
}
void VinylRecommendationService::visitHome(){
    m_home->visited=true;
    ++m_home->compositionInputs;
    m_home->visitId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_home->visitTimer->start(0);
}
void VinylRecommendationService::refreshHome(bool force){
    if(!m_active||m_scope.isEmpty())return;
    if(!m_home->visited&&!force){markHomeDirty();return;}
    if(!force&&m_home->seedCachePending)return;
    if(!force&&(!m_home->active.isEmpty()||!m_home->waiting.isEmpty()||!m_home->metadataReading.isEmpty())){rebuildHome();return;}
    cancelHome(false);m_home->failed.clear();m_home->optionalUnavailable.clear();m_home->error.clear();m_home->evaluatedAt=m_home->clock();
    for(auto it=m_home->canonicalRetryAt.begin();it!=m_home->canonicalRetryAt.end();){
        if(it.value()<=m_home->evaluatedAt)it=m_home->canonicalRetryAt.erase(it);
        else ++it;
    }
    m_home->metadataAttempted.clear();m_home->topTracks.clear();m_home->forceMetadata=force;m_home->neighborsPrepared=false;
    if(force){const auto retained=m_home->retainedSongEvidence();cancelSongCandidates(true);m_home->previousSongEvidence=retained;}
    if(force&&m_home->refreshed)m_home->cursor=m_home->nextCursor;
    m_home->refreshed=true;
    const HomeCache cache(m_source);
    if(!cache.root.isEmpty()){
        m_home->seedCachePending=true;
        vinyl::backgroundRead(this,[cache,seeds=m_home->seeds]{
            QMap<QString,QVariantMap> evidence,identities;
            for(const auto &value:seeds){
                const auto seed=value.toMap();if(seed.value("provider")!="youtube-music")continue;
                const auto key=seed.value("key").toString(),id=seed.value("providerId").toString();
                auto candidates=cache.read({"artistCandidates",id,{{"seed",seed}}});
                if(!candidates.isEmpty())evidence.insert(key,std::move(candidates));
                auto identity=cache.read({"recommendationIdentity",id,{{"seed",seed}}});
                if(!identity.isEmpty())identities.insert(key,std::move(identity));
            }
            return std::make_pair(evidence,identities);
        },[this,epoch=m_home->epoch,generation=m_generation,force](auto values){
            if(!m_active||m_generation!=generation||m_home->epoch!=epoch)return;
            m_home->seedCachePending=false;
            m_home->evidence.insert(values.first);m_home->identities.insert(values.second);
            finishHomeRefresh(force);
        });
        return;
    }
    for(const auto &value:m_home->seeds){const auto seed=value.toMap();if(seed.value("provider")!="youtube-music")continue;
        const auto key=seed.value("key").toString(),id=seed.value("providerId").toString();
        auto cached=cachedHome(m_source,"artistCandidates",id,{{"seed",seed}});QVariantMap normalized;
        if(!cached.isEmpty()&&VinylProvider::normalizeHome("artistCandidates",id,{{"seed",seed}},cached,&normalized))m_home->evidence[key]=normalized;
        auto identity=cachedHome(m_source,"recommendationIdentity",id,{{"seed",seed}});
        if(!identity.isEmpty()&&VinylProvider::normalizeHome("recommendationIdentity",id,{{"seed",seed}},identity,&normalized))m_home->identities[key]=normalized;
    }
    finishHomeRefresh(force);
}
void VinylRecommendationService::finishHomeRefresh(bool force){
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
    // Give the disk-backed visit its first core publication before starting
    // optional cache reads. In-memory ports can still supply their cached
    // metadata in this pass, with no asynchronous wait.
    m_home->firstCorePublicationPending=!HomeCache(m_source).root.isEmpty();
    if(!m_home->firstCorePublicationPending)queueHomeEnrichment();
    pumpHome(true);
}
void VinylRecommendationService::queueHomeEnrichment(){
    // Listening and anchor callbacks must not start a batch ahead of the new
    // seed refresh or its first core publication.
    if(!m_home->refreshed||m_home->seedCachePending||m_home->firstCorePublicationPending)return;
    const auto today=QDateTime::fromMSecsSinceEpoch(m_home->evaluatedAt,Qt::UTC).date();
    const HomeCache cache(m_source);
    QMap<QString,HomeCacheQuery> reads;
    auto enqueue=[&](const QString &op,const QString &value,const QString &key,const QVariantMap &seed,QVariantMap options=QVariantMap(),bool anchorOnly=false) {
        options["seed"]=seed;
        const auto token=digest(QVariantList{op,value,options});
        const bool deferred=m_home->canonicalDeferred(op,value);
        if(deferred)m_home->optionalUnavailable.insert(op+":"+key);
        if(m_home->metadataAttempted.contains(token))return;
        QVariantMap checked;
        bool valid=false;
        if(cache.root.isEmpty()){
            const auto cached=cachedHome(m_source,op,value,options);
            valid=!cached.isEmpty()&&VinylProvider::normalizeHome(op,value,options,cached,&checked);
        }else{
            if(!m_home->metadataCache.contains(token)){
                if(!m_home->metadataReading.contains(token)){
                    m_home->metadataReading.insert(token);
                    reads.insert(token,{op,value,options});
                }
                return;
            }
            checked=m_home->metadataCache.take(token);
            valid=!checked.isEmpty();
        }
        m_home->metadataAttempted.insert(token);
        auto retain=[&](const QVariantMap &data){
            if(op=="recommendationIdentity")m_home->identities[key]=data;
            else if(op=="recommendationDiscography")m_home->discography[key]=data;
            else if(op=="similarArtists")m_home->similarity[token]=data;
            else if(op=="artistTopTracks")m_home->topTracks[key]=data;
            else if(op=="chartCandidates")m_home->charts[key]=data;
            else if(op=="artistCandidates")m_home->evidence[key]=data;
        };
        if(valid)retain(checked);
        if(deferred)return;
        const auto fetched=QDateTime::fromString(checked.value("fetchedAt").toString(),Qt::ISODateWithMs);
        const auto age=fetched.isValid()?fetched.msecsTo(QDateTime::fromMSecsSinceEpoch(m_home->evaluatedAt,Qt::UTC)):-1;
        const bool completeIdentity=op!="recommendationIdentity"||checked.value("status")!="verified"
            ||!VinylSourcePool::artistNames(checked).isEmpty();
        // Home retry/rotation is not a force-refresh of independent global feeds.
        // Reuse good daily charts; still retry missing or stale feeds.
        const bool freshChart=op=="chartCandidates"&&checked.value("status")=="ready"
            &&checked.value("complete").toBool()&&!checked.value("items").toList().isEmpty();
        if(valid&&completeIdentity&&(!m_home->forceMetadata||freshChart)
            &&!checked.value("_stale").toBool()&&age>=0&&age<86400000)return;
        m_home->waiting.append({op,value,key,seed,m_home->forceMetadata,options,true,anchorOnly});
    };
    for(const auto &value:m_home->seeds){const auto seed=value.toMap();if(seed.value("provider")!="youtube-music")continue;
        const auto key=seed.value("key").toString();
        // Identity and aliases do not depend on an optional discography request.
        enqueue("recommendationIdentity",seed.value("providerId").toString(),key,seed);
    }
    for(const QString &country:{"us","gb","jp","kr","in","br","ng","de","fr","mx"})
        for(const QString &kind:{"albums","songs"}){const auto feed="apple:"+country+":"+kind;enqueue("chartCandidates",feed,feed,{});}
    for(const QString &feed:{"lastfm:tracks","lastfm:artists"})enqueue("chartCandidates",feed,feed,{});
    auto artistMetadata=[&](const QVariantMap &seed,bool selected){
        const auto key=seed.value("key").toString();
        const auto identity=m_home->identities.value(key);
        if(identity.value("status")!="verified")return;
        const auto mbid=identity.value("mbid").toString();
        const QVariantMap canonical{{"identity","canonical"},{"kind","artist"},{"mbid",mbid}};
        enqueue("recommendationDiscography",mbid,key,canonical,{{"since",today.addMonths(-12).toString(Qt::ISODate)},{"until",today.toString(Qt::ISODate)}});
        if(!selected)return;
        for(const auto &name:VinylSourcePool::artistNames(identity))
            enqueue("similarArtists",mbid,key,canonical,{{"source","lastfm"},{"artistName",name}});
        enqueue("similarArtists",mbid,key,canonical,{{"source","listenbrainz"}});
        enqueue("populationCandidates",mbid,key,canonical);
    };
    bool ownComplete=!m_home->corePending();int recent=0;
    QVariantMap bySeed;
    for(const auto &value:m_home->seeds){const auto seed=value.toMap();const auto key=seed.value("key").toString();artistMetadata(seed,true);
        const auto identity=m_home->identities.value(key);
        const bool identityPending=identity.isEmpty()&&!m_home->optionalUnavailable.contains("recommendationIdentity:"+key);
        ownComplete&=!identityPending&&(identity.value("status")!="verified"||m_home->discography.contains(key)
            ||m_home->optionalUnavailable.contains("recommendationDiscography:"+key));
        for(const auto &group:m_home->discography.value(key).value("artist").toMap().value("releaseGroups").toList())
            if(VinylSourcePool::isNewRelease(group.toMap().value("first-release-date").toString(),today))++recent;
        QVariantList related;
        for(const auto &entry:m_home->evidence.value(key).value("items").toList())
            if(entry.toMap().value("relation")=="related-artist")related.append(entry);
        bySeed[key]=related;
    }
    if(ownComplete&&!m_home->neighborsPrepared&&m_home->metadataReading.isEmpty()){
      m_home->neighborsPrepared=true;m_home->neighbors.clear();m_home->parents.clear();
      for(const auto &entry:VinylSourcePool::newReleaseNeighbors(m_home->seeds,bySeed,recent)){
        const auto row=entry.toMap();
        const auto seed=artist({{"provider",row.value("provider")},{"providerId",row.value("providerId")},
            {"id",row.value("id")},{"name",row.value("title",row.value("name"))},{"image",row.value("cover")}});
        const auto key=seed.value("key").toString();if(seed.isEmpty())continue;
        m_home->neighbors[key]=seed;
        if(!m_home->parents.contains(key))for(const auto &picked:m_home->seeds){
            bool found=false;for(const auto &candidate:bySeed.value(picked.toMap().value("key").toString()).toList())
                if(candidate.toMap().value("providerId")==seed.value("providerId")){m_home->parents[key]=picked.toMap();found=true;break;}
            if(found)break;
        }
        enqueue("artistCandidates",seed.value("providerId").toString(),key,seed);
        enqueue("recommendationIdentity",seed.value("providerId").toString(),key,seed);
      }
    }
    for(const auto &seed:m_home->neighbors)artistMetadata(seed,false);
    const auto anchor=m_home->anchor;
    if(!anchor.isEmpty()) {
        const auto key=anchor.value("key").toString(), id=anchor.value("providerId").toString();
        const auto identity=m_home->identities.value(key);
        if(anchor.value("provider")=="youtube-music") {
            if(!m_home->corePending()&&!m_home->evidence.contains(key))enqueue("artistCandidates",id,key,anchor,{},true);
            if(identity.isEmpty())enqueue("recommendationIdentity",id,key,anchor,{},true);
        }
        const auto mbid=identity.value("mbid",anchor.value("canonicalMbid")).toString();
        if(!mbid.isEmpty()) {
            const QVariantMap canonical{{"identity","canonical"},{"kind","artist"},{"mbid",mbid}};
            for(const auto &name:VinylSourcePool::artistNames(identity))
                enqueue("similarArtists",mbid,key,canonical,{{"source","lastfm"},{"artistName",name}},true);
            enqueue("similarArtists",mbid,key,canonical,{{"source","listenbrainz"}},true);
            if(!m_home->corePending()) {
                const auto related=relatedRows(m_home->evidence.value(key));
                for(const auto &match:similarArtists(m_home->similarity,{mbid})) {
                    const auto row=matchingChannel(match,related);
                    if(row.isEmpty())continue;
                    const auto seed=artist({{"provider",row.value("provider")},{"providerId",row.value("providerId")},{"name",row.value("title")}});
                    const auto neighborKey=seed.value("key").toString();
                    if(!seed.isEmpty()&&!m_home->evidence.contains(neighborKey))
                        enqueue("artistCandidates",seed.value("providerId").toString(),neighborKey,m_home->neighbors.value(neighborKey,seed),{},true);
                }
            }
        }
    }
    // Only artists with an old track in the existing album-backed pool need a hit list.
    QSet<QString> oldTrackArtists;
    for(const auto &entry:m_home->listening->quickPicks()) {
        const auto card=entry.toMap(), record=card.value("record").toMap();
        const int year=record.value("year").toInt();
        if(year<=0||year>today.year()-6||record.value("type")=="Song")continue;
        const auto track=record.value("tracks").toList().value(card.value("trackIndex").toInt()).toMap();
        auto ids=track.value("artistIds").toStringList();if(ids.isEmpty())ids=record.value("artistIds").toStringList();
        oldTrackArtists.insert(ids.value(0));
    }
    for(const auto &value:m_home->seeds) {
        const auto seed=value.toMap();const auto id=seed.value("providerId").toString();
        if(seed.value("provider")!="youtube-music"||!oldTrackArtists.contains("ytm:artist:"+id))continue;
        enqueue("artistTopTracks",id,id,{{"kind","artist"},{"provider","youtube-music"},{"providerId",id},{"name",seed.value("name")}});
    }
    if(!reads.isEmpty())vinyl::backgroundRead(this,[cache,reads]{
        QMap<QString,QVariantMap> result;
        for(auto it=reads.cbegin();it!=reads.cend();++it)result.insert(it.key(),cache.read(it.value()));
        return result;
    },[this,epoch=m_home->epoch,generation=m_generation](const auto &result){
        if(!m_active||m_generation!=generation||m_home->epoch!=epoch)return;
        m_home->metadataCache.insert(result);
        for(const auto &key:result.keys())m_home->metadataReading.remove(key);
        queueHomeEnrichment();pumpHome();
    });
}
void VinylRecommendationService::pumpHome(bool publishNow){
    if(m_home->seedCachePending)return;
    if(!m_home->corePending())pumpSongCandidates();
    while(m_active&&m_home->active.size()+m_home->songActive.size()<2&&!m_home->waiting.isEmpty()){
        int at=-1;QString lane;
        for(int i=0;i<m_home->waiting.size();++i){const auto &job=m_home->waiting[i];
            if(job.operation=="artistCandidates")lane=!m_home->active.contains("home-seed-0")?"home-seed-0":"home-seed-1";
            else if(VinylSourcePool::metadataOperation(job.operation))lane=!m_home->active.contains("home-metadata-0")?"home-metadata-0":"home-metadata-1";
            else lane=job.operation=="recommendationIdentity"?"home-identity":"home-population";
            if(!m_home->active.contains(lane)){at=i;break;}
        }
        if(at<0)break;const auto job=m_home->waiting.takeAt(at);
        if(m_home->canonicalDeferred(job.operation,job.value)){
            m_home->optionalUnavailable.insert(job.operation+":"+job.key);
            continue;
        }
        auto options=job.options;options["seed"]=job.seed;options["refresh"]=job.force;
        const int id=requestHome(m_source,lane,job.operation,job.value,options);
        if(id<0){
            if(HomeState::core(job))m_home->failed.insert(job.key);
            else{
                m_home->optionalUnavailable.insert(job.operation+":"+job.key);
                if(job.operation=="artistTopTracks")m_home->topTracks.remove(job.key);
                if(!HomeState::canonicalWorkKey(job.operation,job.value).isEmpty()){
                    m_home->deferCanonicalFailure(job);
                    queueHomeEnrichment();
                }
            }
            continue;
        }
        m_home->active.insert(lane,{id,job});
    }
    if(!m_home->corePending())m_home->deadline->stop();
    // Several optional replies can arrive together. Publish one ranked model
    // for the batch instead of rebuilding every shelf for each metadata packet.
    if(publishNow||m_home->corePending()){m_home->publishTimer->stop();rebuildHome();}
    else if(!m_home->publishTimer->isActive())m_home->publishTimer->start();
    // The synchronous owner/fixture path can notify blocking observers. The
    // native path only queues the value composition before starting the timer.
    if(m_home->corePending()&&!m_home->deadline->isActive())m_home->deadline->start(8000);
    pumpSongCandidates();
}
bool VinylRecommendationService::acceptHome(const QString &lane,int id,const QVariant &raw,const QString &error){
    if(lane.startsWith("home-song-"))return acceptSongCandidates(lane,id,raw,error);
    if(lane.startsWith("home-listening-")){
        if(m_active&&sender()==m_source)m_home->listening->accept(lane,id,raw,error);
        return true;
    }
    if(!lane.startsWith("home-"))return false;
    if(!m_active||sender()!=m_source||!m_home->active.contains(lane)||m_home->active.value(lane).id!=id)return true;
    const auto job=m_home->active.take(lane).job;QVariantMap data;QString failure=error;
    auto options=job.options;options["seed"]=job.seed;
    if(failure.isEmpty()&&!VinylProvider::normalizeHome(job.operation,job.value,options,plainHome(raw),&data))failure="The recommendation response is invalid.";
    const bool failed=!failure.isEmpty()||data.value("status")=="unavailable";
    // A stale fallback still supplies usable evidence, but its refresh failed.
    if(failed||!data.value("_cacheError").toString().isEmpty())
        m_home->deferCanonicalFailure(job,data.value("retryAfterSeconds").toInt());
    if(failed){
        if(HomeState::core(job)){m_home->failed.insert(job.key);m_home->error="Some catalogue enrichment is unavailable.";}
        else m_home->optionalUnavailable.insert(job.operation+":"+job.key);
    }
    else if(job.operation=="artistCandidates")m_home->evidence[job.key]=data;
    else if(job.operation=="recommendationIdentity")m_home->identities[job.key]=data;
    else if(job.operation=="recommendationDiscography")m_home->discography[job.key]=data;
    else if(job.operation=="similarArtists")m_home->similarity[digest(QVariantList{job.operation,job.value,options})]=data;
    else if(job.operation=="artistTopTracks")m_home->topTracks[job.key]=data;
    else if(job.operation=="chartCandidates"){
        // The provider already preserves last-good charts across failed refreshes.
        m_home->charts[job.key]=data;
    }else m_home->population[job.key]=data;
    if(failed&&job.operation=="artistTopTracks")m_home->topTracks.remove(job.key);
    if(job.operation=="artistCandidates"||job.operation=="similarArtists"||job.operation=="recommendationIdentity"
        ||(job.operation=="recommendationDiscography"&&!m_home->neighbors.contains(job.key)))queueHomeEnrichment();
    pumpHome(HomeState::core(job)
        ||(job.operation=="recommendationIdentity"&&!m_home->neighbors.contains(job.key)));
    if(HomeState::core(job)&&m_home->corePending())m_home->deadline->start(8000);
    return true;
}

void VinylRecommendationService::queueListeningRebuild() {
    if(!m_home||!m_home->listening)return;
    const auto hash=digest(m_home->listening->quickPicks());
    if(hash==m_home->listeningHash)return;m_home->listeningHash=hash;
    if(m_home->listeningRebuildQueued)return;m_home->listeningRebuildQueued=true;
    QTimer::singleShot(0,this,[this]{
        m_home->listeningRebuildQueued=false;
        if(!m_active||!canEnterVinyl()||!m_home->visited||!m_home->refreshed||m_home->seedCachePending)return;
        // An empty listening pool still needs publication, but cannot add hit lists.
        if(m_home->listening->quickPicks().isEmpty()){rebuildHome();return;}
        queueHomeEnrichment();pumpHome(true);
    });
}

void VinylRecommendationService::cancelSongCandidates(bool clear) {
    m_home->songDeadline->stop();
    const auto lanes=m_home->songActive.keys();m_home->songActive.clear();m_home->songWaiting.clear();
    for(const auto &lane:lanes)cancelLane(m_source,lane);
    if(clear){m_home->songCacheReads.clear();m_home->songEvidence.clear();m_home->previousSongEvidence.clear();m_home->songAttempted.clear();m_home->songFailed.clear();m_home->songSeeds.clear();m_home->songSelection.clear();}
}

void VinylRecommendationService::queueSongCandidates() {
    if(!m_active||!canEnterVinyl()||!m_home->visited||!m_source)return;
    QStringList excludedSeeds;
    for(const auto &entry:m_home->seeds)if(m_home->excludedArtistProviderIds.contains(entry.toMap().value("providerId").toString()))excludedSeeds.append(entry.toMap().value("providerId").toString());
    const auto selection=m_home->selectionKey+"/"+QString::number(m_home->selectionSerial)+"/"+digest(excludedSeeds);
    if(selection!=m_home->songSelection){const auto retained=m_home->retainedSongEvidence();cancelSongCandidates(true);m_home->previousSongEvidence=retained;m_home->songSelection=selection;}
    bool candidatesChanged=false;int artistCount=0;
    QList<HomeState::SongJob> desiredJobs;QSet<QString> desiredKeys;QVariantList desiredSeeds;
    for(const auto &value:m_home->seeds) {
        const auto selected=value.toMap();
        if(selected.value("provider")!="youtube-music"||m_home->excludedArtistProviderIds.contains(selected.value("providerId").toString()))continue;
        const auto data=m_home->evidence.value(selected.value("key").toString());
        const auto seed=data.value("seed").toMap();if(seed.isEmpty())continue;
        if(++artistCount>6)break;
        QVariantList songs;QSet<QString> videos;
        for(const auto &entry:data.value("items").toList()) {
            const auto song=entry.toMap();const auto video=song.value("providerId").toString();
            if(song.value("kind")!="song"||song.value("relation")!="top-recording"
                ||song.value("seedArtistRef").toMap()!=seed||!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(video).hasMatch()
                ||videos.contains(video))continue;
            videos.insert(video);songs.append(song);
        }
        std::sort(songs.begin(),songs.end(),[&](const QVariant &a,const QVariant &b){
            return digest(QVariantList{selection,seed.value("providerId"),a.toMap().value("providerId")})
                <digest(QVariantList{selection,seed.value("providerId"),b.toMap().value("providerId")});
        });
        QVariantList chosen;QSet<QString> albums;
        for(int pass=0;pass<2&&chosen.size()<2;++pass)for(const auto &entry:songs) {
            if(chosen.contains(entry))continue;
            const auto album=entry.toMap().value("albumRef").toMap().value("providerId").toString();
            if(pass==0&&!album.isEmpty()&&albums.contains(album))continue;
            chosen.append(entry);if(!album.isEmpty())albums.insert(album);if(chosen.size()==2)break;
        }
        for(const auto &entry:chosen) {
            const auto recording=entry.toMap();const auto video=recording.value("providerId").toString();
            const auto key=selected.value("key").toString()+"/"+video;
            desiredKeys.insert(key);desiredSeeds.append(recording);desiredJobs.append({key,video,seed,recording});
        }
    }
    // Artist shelves can change within one daily selection. Retire their old
    // song jobs rather than accumulating successive sets under the same key.
    const bool displaced=std::any_of(m_home->songEvidence.keyBegin(),m_home->songEvidence.keyEnd(),[&](const QString &key){return !desiredKeys.contains(key);});
    if(displaced)m_home->previousSongEvidence=m_home->retainedSongEvidence();
    QStringList retiredLanes;
    for(auto it=m_home->songActive.begin();it!=m_home->songActive.end();) {
        if(!desiredKeys.contains(it->job.key)){retiredLanes.append(it.key());it=m_home->songActive.erase(it);candidatesChanged=true;}else ++it;
    }
    for(const auto &lane:retiredLanes)cancelLane(m_source,lane);
    m_home->songWaiting.removeIf([&](const HomeState::SongJob &job){return !desiredKeys.contains(job.key);});
    for(auto it=m_home->songEvidence.begin();it!=m_home->songEvidence.end();) {
        if(!desiredKeys.contains(it.key())){it=m_home->songEvidence.erase(it);candidatesChanged=true;}else ++it;
    }
    for(auto *keys:{&m_home->songAttempted,&m_home->songFailed})for(auto it=keys->begin();it!=keys->end();) {
        if(!desiredKeys.contains(*it))it=keys->erase(it);else ++it;
    }
    for(auto it=m_home->songCacheReads.begin();it!=m_home->songCacheReads.end();){
        if(!desiredKeys.contains(it.key()))it=m_home->songCacheReads.erase(it);else ++it;
    }
    if(m_home->songSeeds!=desiredSeeds){m_home->songSeeds=desiredSeeds;candidatesChanged=true;}
    const HomeCache cache(m_source);
    QList<HomeState::SongJob> reads;
    const auto readSerial=++m_home->songCacheSerial;
    for(const auto &job:desiredJobs) {
        if(m_home->songAttempted.contains(job.key))continue;m_home->songAttempted.insert(job.key);candidatesChanged=true;
        if(!cache.root.isEmpty()){
            m_home->songCacheReads.insert(job.key,readSerial);reads.append(job);
        }else{
            const QVariantMap options{{"seed",job.seed},{"recording",job.recording}};
            const auto cached=cachedHome(m_source,"songCandidates",job.video,options);QVariantMap checked;
            if(!cached.isEmpty()&&VinylProvider::normalizeHome("songCandidates",job.video,options,cached,&checked))m_home->songEvidence[job.key]=checked;
            m_home->songWaiting.append(job);
        }
    }
    if(!reads.isEmpty())vinyl::backgroundRead(this,[cache,reads]{
        QMap<QString,QVariantMap> result;
        for(const auto &job:reads)result.insert(job.key,cache.read({"songCandidates",job.video,{{"seed",job.seed},{"recording",job.recording}}}));
        return result;
    },[this,generation=m_generation,readSerial,reads](const auto &result){
        if(!m_active||m_generation!=generation)return;
        bool applied=false;
        for(const auto &job:reads){
            if(m_home->songCacheReads.value(job.key)!=readSerial)continue;
            m_home->songCacheReads.remove(job.key);applied=true;
            const auto checked=result.value(job.key);
            if(!checked.isEmpty())m_home->songEvidence[job.key]=checked;
            // The normal request callback also registers exact playback records.
            m_home->songWaiting.append(job);
        }
        if(applied)pumpHome(true);
    });
    if(candidatesChanged) {
        QTimer::singleShot(0,this,[this,epoch=m_home->epoch]{if(canEnterVinyl()&&m_home->epoch==epoch)rebuildHome();});
    }
    pumpSongCandidates();
}

void VinylRecommendationService::pumpSongCandidates() {
    if(!m_active||!m_source)return;
    while(m_home->songActive.size()+m_home->active.size()<2&&!m_home->songWaiting.isEmpty()) {
        const auto job=m_home->songWaiting.takeFirst();
        const auto lane=m_home->songActive.contains("home-song-0")?QStringLiteral("home-song-1"):QStringLiteral("home-song-0");
        const int id=requestHome(m_source,lane,"songCandidates",job.video,{{"seed",job.seed},{"recording",job.recording}});
        if(id<0){m_home->songFailed.insert(job.key);continue;}
        m_home->songActive.insert(lane,{id,job});
    }
    if((!m_home->songActive.isEmpty()||!m_home->songWaiting.isEmpty())&&!m_home->songDeadline->isActive())m_home->songDeadline->start(90000);
    if(m_home->songActive.isEmpty()&&m_home->songWaiting.isEmpty())m_home->songDeadline->stop();
}

bool VinylRecommendationService::acceptSongCandidates(const QString &lane,int id,const QVariant &raw,const QString &error) {
    if(!m_active||sender()!=m_source||!m_home->songActive.contains(lane)||m_home->songActive.value(lane).id!=id)return true;
    const auto job=m_home->songActive.take(lane).job;QVariantMap data;
    if(!error.isEmpty()||!VinylProvider::normalizeHome("songCandidates",job.video,{{"seed",job.seed},{"recording",job.recording}},plainHome(raw),&data)
        ||data.value("status")=="unavailable")m_home->songFailed.insert(job.key);
    else {m_home->songEvidence[job.key]=data;m_home->songFailed.remove(job.key);}
    // Publish the settled song batch even when canonical back-off leaves no
    // other replies to trigger a rebuild. Keep intermediate replies batched.
    const bool songBatchFinished=m_home->songActive.isEmpty()&&m_home->songWaiting.isEmpty()&&m_home->songCacheReads.isEmpty();
    pumpHome(songBatchFinished);
    return true;
}

void VinylRecommendationService::rebuildHome(bool immediate){
    if(!m_active)return;
    // Late catalogue/listening replies may finish off-screen. Keep their evidence,
    // but never project history or rank it until Home is visible again.
    // Explicit feedback/pin mutations can also publish while off-screen.
    if(!m_home->visited&&!immediate){markHomeDirty();return;}
    if(!immediate&&m_home->seedCachePending)return;
    if(immediate&&m_home->collectionsDirty){
        m_home->collectionsDirty=false;
        homeSeedsChanged();
    }
    if(m_home->composing){m_home->recompose=true;m_home->recomposeImmediate|=immediate;return;}
    if(!m_home->collectionsDirty){
        m_home->dirty=false;
        m_home->inputsTimer->stop();
    }
    HomeComposition work;
    work.scope=m_scope;work.keepHistory=m_keepHistory;work.historyStoreBound=!m_historyStore.isNull();
    work.now=m_home->evaluatedAt>0?m_home->evaluatedAt:m_home->clock();
    if(m_keepHistory)work.historyInputs=VinylRecommendationHistory::capture(qobject_cast<ActivityStore*>(m_activityStore.data()),qobject_cast<HistoryStore*>(m_historyStore.data()));
    if(m_preferences){
        work.feedback=m_preferences->property("vinylFeedbackSnapshot").toString();
        work.storedExposure=m_preferences->property("vinylRecommendationExposureSnapshot").toString();
    }
    work.loading=m_home->corePending();work.saved=m_saved;work.baselineQuickPicks=m_home->listening->quickPicks();
    work.seeds=m_home->seeds;work.seedHash=m_home->seedHash;work.collections=m_home->collections;work.visitId=m_home->visitId;
    work.evidence=m_home->evidence;work.identities=m_home->identities;work.discography=m_home->discography;
    work.similarity=m_home->similarity;work.neighbors=m_home->neighbors;work.parents=m_home->parents;
    work.topTracks=m_home->topTracks;
    work.chartFeeds=m_home->charts;
    work.activityReset=m_home->activityReset;work.historyReset=m_home->historyReset;work.selectionKeys=m_home->selectionKeys;
    work.songEvidence=m_home->songEvidence;work.previousSongEvidence=m_home->previousSongEvidence;
    work.songSeeds=m_home->songSeeds;work.songAttempted=m_home->songAttempted.size();work.songFailed=m_home->songFailed.size();
    work.songPending=m_home->songWaiting.size()+m_home->songActive.size()+m_home->songCacheReads.size();
    work.failed=m_home->failed;work.error=m_home->error;work.optionalUnavailable=m_home->optionalUnavailable.size();
    if(HomeCache(m_source).root.isEmpty()&&work.historyInputs.activityDatabase.isEmpty()){
        publishHome(composeHome(std::move(work)));return;
    }
    m_home->composing=true;
    vinyl::backgroundRead(this,[work=std::move(work)]() mutable {return composeHome(std::move(work));},
        [this,epoch=m_home->epoch,generation=m_generation,inputs=m_home->compositionInputs,immediate](HomeComposition result){
            m_home->composing=false;
            const bool again=std::exchange(m_home->recompose,false);
            const bool againImmediate=std::exchange(m_home->recomposeImmediate,false);
            if(m_active&&m_generation==generation&&m_home->epoch==epoch&&(m_home->visited||immediate)
                &&m_home->compositionInputs==inputs
                &&(immediate||result.loading==m_home->corePending()))
                publishHome(std::move(result));
            if(again&&m_active&&(m_home->visited||againImmediate))rebuildHome(againImmediate);
        });
}

VinylRecommendationService::HomeComposition VinylRecommendationService::composeHome(HomeComposition work){
    auto profile=ProfileScopeId::parse(work.scope);if(!profile.ok())return work;
    const auto now=work.now;
    TasteProfileState taste(*profile.value);
    const auto &feedback=work.feedback;
    if(!feedback.isEmpty()){const auto restored=FeedbackTastePersistence::restore(*profile.value,feedback.toUtf8());
        if(!restored.ok()){work.feedbackError=true;return work;}taste=*restored.value;}
    QSet<QString> excludedArtistProviderIds;
    for(const auto &state:taste.states()) {
        if(state.canonical().kind()!=CanonicalEntityKind::Artist)continue;
        const auto &suppression=state.suppression();
        if(!suppression.doNotRecommend()&&!suppression.hidden()&&!suppression.isSnoozedAt(now))continue;
        const auto &artist=state.canonical();
        if(const auto provider=artist.providerRef()) {
            if(provider->providerId().value()=="youtube-music"&&!provider->itemId().value().isEmpty())
                excludedArtistProviderIds.insert(provider->itemId().value());
        }else if(artist.isCanonical()) {
            const auto mbid=artist.mbidString().toLower();
            for(const auto &value:work.seeds) {
                const auto seed=value.toMap();
                if(seed.value("provider")!="youtube-music")continue;
                bool matched=seed.value("mappingStatus")=="verified"
                    &&seed.value("canonicalMbid").toString().toLower()==mbid;
                if(!matched) {
                    const auto identity=work.identities.value(seed.value("key").toString());
                    if(identity.value("status")=="verified") {
                        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(identity.value("ref").toMap()));
                        matched=ref.ok()&&*ref.value==artist;
                    }
                }
                if(matched&&!seed.value("providerId").toString().isEmpty())
                    excludedArtistProviderIds.insert(seed.value("providerId").toString());
            }
        }
    }
    work.excludedArtistProviderIds.clear();
    for(const auto &id:excludedArtistProviderIds)work.excludedArtistProviderIds.append(id);
    std::sort(work.excludedArtistProviderIds.begin(),work.excludedArtistProviderIds.end());
    const auto historyCatalogue=work.collections.value("catalogueAlbums").toList()+work.collections.value("albums").toList();
    work.history=work.keepHistory?VinylRecommendationHistory::project(work.historyInputs,historyCatalogue,now):QVariantMap();
    const auto derived=VinylRecommendationHistory::derive(work.scope,work.history,now);
    if(!derived.ok())return work;
    const auto manualSessions=work.history.value("qualifiedManualSessions").toInt();
    const double seedMultiplier=manualSessions<=30?1.0:manualSessions<=100?0.75:0.5;
    QSet<QString> savedSeedKeys;
    for(const auto &value:work.collections.value("artists").toList()) {
        const auto saved=artist(value.toMap());if(!saved.isEmpty())savedSeedKeys.insert(saved.value("key").toString());
    }
    RecommendationExposureLog exposure(*profile.value);QVariantList retainedExposure;
    if(work.keepHistory&&work.historyInputs.enabled) {
        const auto &stored=work.storedExposure;
        const bool valid=readExposure(work.scope,stored,now,work.activityReset,work.historyReset,exposure,retainedExposure);
        // Old-generation/expired private records are removed after their owner
        // bindings are known; valid post-reset records survive a fresh service.
        if(valid && retainedExposure.isEmpty() && !stored.isEmpty()) {
            const auto envelope=QJsonDocument::fromJson(stored.toUtf8()).object();
            work.clearExposure=work.historyStoreBound||envelope.value("historyResetGeneration").toInteger(-1)==0;
        }
    }
    // Exposure captured in this visit is durable for future visits. Applying it
    // immediately would move the cards being observed and expose replacements.
    RecommendationExposureLog priorVisitExposure(*profile.value);
    for(const auto &record:exposure.records()) {
        const auto shelf=record.evidence().first().sourceId().mid(5); // validated home:<shelf>
        const auto currentId="visible-"+digest(QVariantList{work.visitId,shelf,record.entity().stableKey()});
        if(record.exposureId().value()!=currentId)priorVisitExposure.append(record);
    }
    struct Node {QVariantMap card;QMap<QString,QVariantMap> artistContexts,seedContexts;QSet<QString> sections,sources;QVector<RecommendationCandidate> candidates;QVector<SeedAffinityEvidence> affinities;};
    QMap<QString,Node> nodes,quickNodes,deepNodes,playedNodes,similarNodes;QMap<QString,QSet<QString>> generators;QVariantList sourceSnapshot;
    auto seedIdentity=[&work](const QVariantMap &seed)->std::optional<RecommendationEntityRef>{
        const auto identity=work.identities.value(seed.value("key").toString());
        if(identity.value("status")=="verified"){
            const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(identity.value("ref").toMap()));
            if(ref.ok()&&ref.value->isCanonical()&&ref.value->kind()==CanonicalEntityKind::Artist)return *ref.value;
        }
        return typed(seed);
    };
    auto add=[&](const QVariantMap &raw,const QVariantMap &seed,const QString &generator,const QString &section,bool library,
                 std::optional<double> similarity=std::nullopt){
        const bool songSection=section=="quick-picks"||section=="deep-cuts";
        const auto converted=songSection?VinylRecommendationIdentity::fromProviderSong(raw)
            :VinylRecommendationIdentity::fromCatalogue(raw);if(!converted.value("ok").toBool())return;
        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(converted.value("ref").toMap()));const auto seedRef=seedIdentity(seed);
        if(!ref.ok()||!seedRef)return;const auto key=ref.value->stableKey();
        auto &target=section=="quick-picks"?quickNodes:section=="deep-cuts"?deepNodes:
            section=="because-you-played"?playedNodes:section=="artists-like-yours"?similarNodes:nodes;
        if(!target.contains(key)&&target.size()>=(section=="quick-picks"?120:300))return;
        if(!generators[generator].contains(key)&&generators[generator].size()>=60)return;
        const auto destination=converted.value("destination").toMap();if(destination.value("id").toString().isEmpty()||raw.value("title").toString().isEmpty())return;
        // A seed shelf is not an exact recording-match attestation. Prefer its
        // usable album/artist destinations until recording qualification exists.
        if(destination.value("kind")=="song"&&!destination.value("playable").toBool())return;
        generators[generator].insert(key);auto &node=target[key];
        auto card=raw;card["ref"]=converted.value("ref");card["key"]=key;card["typedKey"]=converted.value("ref");card["destination"]=destination;
        card["kind"]=destination.value("kind")=="artist"?"artists":destination.value("kind")=="song"?"songs":"albums";
        card["id"]=destination.value("id");card["albumId"]=destination.value("id");card["artistId"]=destination.value("id");card["round"]=destination.value("kind")=="artist";
        QStringList names,artistIds;for(const auto &entry:raw.value("artistRefs").toList()){const auto artist=entry.toMap();names.append(artist.value("name").toString());artistIds.append("ytm:artist:"+artist.value("providerId").toString());}
        card["subtitle"]=names.join(QStringLiteral(" · "));card["cover"]=raw.value("cover",raw.value("image"));
        if(destination.value("kind")=="song"&&!raw.value("record").toMap().isEmpty()) {
            card["record"]=raw.value("record");card["trackIndex"]=raw.value("trackIndex",0);
        }else if(destination.value("kind")=="song"){
            QVariantMap track{{"id",destination.value("id")},{"videoId",destination.value("providerId")},{"title",raw.value("title")},{"seconds",raw.value("seconds")},{"artists",names},{"artistIds",artistIds},{"available",destination.value("playable")}};
            card["record"]=QVariantMap{{"id",destination.value("id")},{"browseId",destination.value("id")},{"type","Song"},{"origin","youtube-music"},{"title",raw.value("title")},{"cover",card.value("cover")},{"artists",names},{"artistIds",artistIds},{"tracks",QVariantList{track}}};card["trackIndex"]=0;
        }
        if(node.card.isEmpty()||wire(card)<wire(node.card))node.card=card;
        auto credits=raw.value("artistRefs").toList();
        // An own-release shelf establishes its owner even when the provider
        // omits credits. A related artist's seed is only its discovery source.
        if(raw.value("relation")=="own-release")credits.append(raw.value("seedArtistRef",seed));
        for(const auto &value:credits)if(const auto artist=typed(value.toMap()))
            node.artistContexts.insert(artist->stableKey(),value.toMap());
        QVariantList mergedCredits;for(const auto &credit:node.artistContexts)mergedCredits.append(credit);
        node.card["artistRefs"]=mergedCredits;
        if(section=="quick-picks") {
            node.seedContexts.insert(seed.value("providerId").toString(),seed);
            for(const auto &source:raw.value("candidateSources").toStringList())node.sources.insert(source);
            QVariantList seedRefs;for(const auto &value:node.seedContexts)seedRefs.append(value);
            auto sources=node.sources.values();std::sort(sources.begin(),sources.end());
            node.card["seedArtistRefs"]=seedRefs;node.card["candidateSources"]=sources;
        }
        node.sections.insert(section);
        const auto today=QDateTime::fromMSecsSinceEpoch(now,Qt::UTC).date();
        if((section=="from-your-artists"||section=="new-from-your-artists")&&destination.value("kind")=="album"&&VinylSourcePool::isNewRelease(raw.value("releaseDate").toString(),today))node.sections.insert("new-from-your-artists");
        RecommendationCanonicalContext context;
        auto primary=raw.value("artistRefs").toList();auto artistRef=primary.isEmpty()?seedRef:typed(primary.first().toMap());
        if(!primary.isEmpty()&&primary.first().toMap().value("provider")==seed.value("provider")
            &&primary.first().toMap().value("providerId")==seed.value("providerId"))artistRef=seedRef;
        if(artistRef){if(artistRef->isCanonical())context.artistMbid=ArtistMbid::parse(artistRef->mbidString()).value;else context.providerArtist=*artistRef;}
        const auto group=ReleaseGroupMbid::parse(raw.value("releaseGroupMbid").toString());if(group.ok())context.releaseGroupMbid=*group.value;
        if(const auto release=typed(raw.value("albumRef").toMap());release&&!release->isCanonical()&&release->kind()==CanonicalEntityKind::Release)
            context.providerRelease=*release;
        const auto provenance=RecommendationCandidateProvenance::create(*RecommendationGeneratorId::parse(generator).value,
            similarity?RecommendationSourceFamily::Collaborative:RecommendationSourceFamily::Editorial,
            *ProviderId::parse(raw.value("provider").toString()).value,*ProviderItemId::parse(raw.value("providerId").toString()).value,{},
            *RecommendationSeed::create(*seedRef,work.saved.contains(seed)?"selected-artist":"saved-artist").value);
        if(!provenance.ok())return;
        const auto contribution=RecommendationContribution::create(context,*provenance.value,similarity,
            {*RecommendationReasonEvidence::create(similarity?RecommendationEvidenceKind::CollaborativePrior:RecommendationEvidenceKind::EditorialSelection,
                similarity?"Last.fm / ListenBrainz artist similarity":library?"Current library membership":"Provider artist shelf: "+raw.value("relation").toString()).value});
        if(!contribution.ok())return;
        const auto candidate=RecommendationCandidate::create(*ref.value,*contribution.value,destination.value("playable").toBool());
        if(candidate.ok()){node.candidates.append(*candidate.value);node.affinities.append({*ref.value,*seedRef,
            library || savedSeedKeys.contains(seed.value("key").toString())?1.0:seedMultiplier});}
    };
    // Round-robin admission prevents the first returned artist consuming a generator budget.
    for(int index=0;index<60;++index)for(const auto &value:work.seeds){const auto seed=value.toMap();const auto data=work.evidence.value(seed.value("key").toString());
        const auto items=data.value("items").toList();if(index>=items.size())continue;
        const auto key=seed.value("key").toString();
        const auto item=datedRelease(items[index].toMap(),work.identities.value(key),work.discography.value(key));const auto relation=item.value("relation").toString();
        add(item,seed,relation=="related-artist"?"youtube-related-artists":relation=="top-recording"?"youtube-top-recordings":"youtube-own-releases",relation=="related-artist"?"related-discoveries":"from-your-artists",false);
    }
    for(int index=0;index<8;++index)for(auto it=work.neighbors.cbegin();it!=work.neighbors.cend();++it){
        const auto items=work.evidence.value(it.key()).value("items").toList();if(index>=items.size())continue;
        const auto item=datedRelease(items[index].toMap(),work.identities.value(it.key()),work.discography.value(it.key()));
        if(item.value("kind")=="album"&&item.value("dateSource")=="musicbrainz"
            &&VinylSourcePool::isNewRelease(item.value("releaseDate").toString(),QDateTime::fromMSecsSinceEpoch(now,Qt::UTC).date()))
            add(item,work.parents.value(it.key()),"nearby-recent-releases","new-from-your-artists",false);
    }
    for(const auto &data:work.similarity){
        const auto mbid=data.value("seed").toMap().value("mbid").toString();QVariantMap seed;
        for(const auto &picked:work.seeds)if(work.identities.value(picked.toMap().value("key").toString()).value("mbid")==mbid){seed=picked.toMap();break;}
        if(seed.isEmpty())continue;
        for(const auto &entry:data.value("items").toList()){
            auto item=VinylSourcePool::catalogueCandidate(entry.toMap());item["relation"]="similar-artist";
            add(item,seed,"metadata-similar-artists","related-discoveries",false);
        }
    }
    // The newest qualified play is the only anchor, even if it has no usable artist.
    QVariantMap latest;
    for(const auto &value:work.history.value("sessions").toList()) {
        const auto session=value.toMap();
        if(session.value("qualifiedMs").toLongLong()>=10000
            &&session.value("atMs").toLongLong()>latest.value("atMs").toLongLong())latest=session;
    }
    QSet<QString> blockedKeys,blockedNames,blockedMbids,seedMbids,seedKeys,playedChannels;
    QMap<QString,QVariantMap> seedByMbid;
    auto rememberArtist=[&](const QVariantMap &seed) {
        if(const auto ref=typed(seed))blockedKeys.insert(ref->stableKey());
        const auto name=artistNameKey(seed.value("name").toString());if(!name.isEmpty())blockedNames.insert(name);
        if(const auto ref=seedIdentity(seed);ref&&ref->isCanonical())blockedMbids.insert(ref->mbidString());
        for(const auto &alias:VinylSourcePool::artistNames(work.identities.value(seed.value("key").toString())))
            blockedNames.insert(artistNameKey(alias));
    };
    for(const auto &value:work.seeds) {
        const auto seed=value.toMap();
        const auto key=seed.value("key").toString();seedKeys.insert(key);
        const auto mbid=work.identities.value(key).value("mbid").toString();
        if(!mbid.isEmpty()){seedMbids.insert(mbid);seedByMbid[mbid]=seed;}
    }
    for(const auto &value:work.saved)rememberArtist(value.toMap());
    for(const auto &aggregate:derived.value->aggregates()) {
        const auto &ref=aggregate.entity();
        if(ref.kind()!=CanonicalEntityKind::Artist||aggregate.playCount()<=0)continue;
        blockedKeys.insert(ref.stableKey());
        if(ref.isCanonical())blockedMbids.insert(ref.mbidString());
        if(const auto provider=ref.providerRef();provider&&provider->providerId().value()=="youtube-music") {
            const auto id=provider->itemId().value();playedChannels.insert(id);
            rememberArtist(artist({{"provider","youtube-music"},{"providerId",id}}));
        }
    }
    for(const auto &value:latest.value("entities").toList()) {
        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(value.toMap()));
        if(!ref.ok()||ref.value->kind()!=CanonicalEntityKind::Artist)continue;
        if(ref.value->isCanonical())work.anchor=seedByMbid.value(ref.value->mbidString());
        else if(const auto provider=ref.value->providerRef();provider&&provider->providerId().value()=="youtube-music")
            work.anchor=artist({{"provider","youtube-music"},{"providerId",provider->itemId().value()}});
        const auto id=work.anchor.value("providerId").toString();
        for(const auto &seed:work.seeds)if(seed.toMap().value("provider")=="youtube-music"&&seed.toMap().value("providerId")==id){work.anchor=seed.toMap();break;}
        break;
    }
    // Use existing catalogue/related artist names for provider-only activity.
    for(const auto &value:historyCatalogue) {
        const auto entry=value.toMap(), album=entry.value("album",entry).toMap();
        const auto ids=album.value("artistIds").toStringList(),names=album.value("artists").toStringList();
        for(int i=0;i<ids.size();++i)if(ids[i].startsWith("ytm:artist:")&&playedChannels.contains(ids[i].mid(11)))
            blockedNames.insert(artistNameKey(names.value(i)));
    }
    QMap<QString,QMap<QString,QVariantMap>> youtubeByName;
    for(const auto &key:seedKeys)for(const auto &entry:work.evidence.value(key).value("items").toList()) {
        const auto row=entry.toMap();if(row.value("relation")!="related-artist")continue;
        const auto name=artistNameKey(row.value("title").toString()),id=row.value("providerId").toString();
        if(playedChannels.contains(id))blockedNames.insert(name);
        youtubeByName[name][id]=row;
    }
    auto isBlocked=[&](const SimilarArtist &match) {
        return blockedMbids.contains(match.mbid)||blockedNames.intersects(match.names);
    };
    for(const auto &match:similarArtists(work.similarity,seedMbids)) {
        if(isBlocked(match))continue;
        QMap<QString,QVariantMap> youtube;
        for(const auto &name:match.names)youtube.insert(youtubeByName.value(name));
        if(youtube.size()>1)continue;
        auto item=youtube.isEmpty()?match.card:youtube.first();
        if(const auto ref=typed(item);ref&&blockedKeys.contains(ref->stableKey()))continue;
        item["relation"]="similar-artist";
        add(item,seedByMbid.value(match.bySeed.firstKey()),"home-artist-similarity","artists-like-yours",false,match.strength());
    }
    const auto anchorKey=work.anchor.value("key").toString();
    const auto anchorIdentity=work.identities.value(anchorKey);
    const auto anchorMbid=anchorIdentity.value("mbid").toString();
    const auto anchorName=VinylSourcePool::artistNames(anchorIdentity).value(0,work.anchor.value("name").toString());
    QVariantList anchorCatalogue;
    if(!anchorMbid.isEmpty()&&!anchorName.isEmpty()) {
        anchorCatalogue.append(work.evidence.value(anchorKey));
        const auto related=relatedRows(work.evidence.value(anchorKey));
        for(const auto &match:similarArtists(work.similarity,{anchorMbid})) {
            const auto row=matchingChannel(match,related);
            if(row.isEmpty())continue;
            const auto neighbor=artist({{"provider",row.value("provider")},{"providerId",row.value("providerId")},{"name",row.value("title")}});
            if(neighbor.isEmpty())continue;
            const auto catalogue=work.evidence.value(neighbor.value("key").toString());
            if(catalogue.isEmpty())continue;
            anchorCatalogue.append(catalogue);
            int admitted=0;
            for(const auto &entry:catalogue.value("items").toList()) {
                auto album=entry.toMap();
                if(album.value("kind")!="album"||album.value("relation")!="own-release")continue;
                auto credits=album.value("artistRefs").toList();if(credits.isEmpty())credits.append(row);
                if(credits.first().toMap().value("providerId")!=neighbor.value("providerId"))continue;
                const bool known=isBlocked(match)||std::any_of(credits.cbegin(),credits.cend(),[&](const QVariant &value){
                    const auto credit=value.toMap();const auto ref=typed(credit);
                    return (ref&&blockedKeys.contains(ref->stableKey()))||blockedNames.contains(artistNameKey(credit.value("name").toString()));
                });
                if(known)continue;
                album["artistRefs"]=credits;
                album["reason"]=tr("Listeners of %1 also play %2").arg(anchorName,neighbor.value("name").toString());
                album["anchorReason"]=tr("More like %1").arg(anchorName);
                album["similarArtistKey"]=match.mbid.isEmpty()?artistNameKey(neighbor.value("name").toString()):match.mbid;
                add(album,work.anchor,"home-album-similarity","because-you-played",false,match.strength());
                if(++admitted==6)break;
            }
        }
    }
    // Admit each song-seed stream round-robin before applying the shared ranker.
    const bool hasCurrentSongs=std::any_of(work.songEvidence.cbegin(),work.songEvidence.cend(),[](const QVariantMap &data){return !data.value("items").toList().isEmpty();});
    const auto &candidateSongEvidence=hasCurrentSongs?work.songEvidence:work.previousSongEvidence;
    const bool usingPreviousSongs=!hasCurrentSongs&&!candidateSongEvidence.isEmpty();
    for(int index=0;index<40;++index)for(const auto &data:candidateSongEvidence) {
        const auto items=data.value("items").toList();if(index>=items.size())continue;
        const auto row=items[index].toMap(),seed=data.value("seed").toMap();
        if(work.excludedArtistProviderIds.contains(seed.value("providerId").toString()))continue;
        for(const auto &source:row.value("candidateSources").toStringList())
            add(row,seed,"youtube-song-"+source,"quick-picks",false);
    }
    const auto &baselineQuickPicks=work.baselineQuickPicks;
    const bool hasSongCandidates=!quickNodes.isEmpty();
    QSet<QString> topRecordings;
    for(const auto &data:work.evidence)for(const auto &entry:data.value("items").toList())
        if(entry.toMap().value("relation")=="top-recording")topRecordings.insert(entry.toMap().value("providerId").toString());
    QMap<QString,QSet<QString>> hits;
    for(auto it=work.topTracks.cbegin();it!=work.topTracks.cend();++it) {
        const auto &data=it.value();
        if(!data.value("complete").toBool()||data.value("_stale").toBool()||!data.value("_cacheError").toString().isEmpty())continue;
        auto &titles=hits[it.key()];
        const auto name=artistNameKey(data.value("seed").toMap().value("name").toString());
        for(const auto &entry:data.value("items").toList()) {
            const auto track=entry.toMap();
            if(artistNameKey(track.value("artist").toString())==name)titles.insert(hitTitle(track.value("title").toString()));
        }
        if(titles.isEmpty()&&data.value("status")=="ready")hits.remove(it.key());
    }
    const int oldestAllowed=QDateTime::fromMSecsSinceEpoch(now,Qt::UTC).date().year()-6;
    for(const auto &value:baselineQuickPicks) {
        const auto card=value.toMap(),record=card.value("record").toMap();const int index=card.value("trackIndex").toInt();
        const auto track=record.value("tracks").toList().value(index).toMap();const auto video=track.value("videoId").toString();
        if(!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(video).hasMatch())continue;
        QVariantList credits;const auto ids=track.value("artistIds",record.value("artistIds")).toStringList();
        const auto names=track.value("artists",record.value("artists")).toStringList();
        for(int i=0;i<ids.size();++i)if(ids[i].startsWith("ytm:artist:"))credits.append(QVariantMap{
            {"kind","artist"},{"provider","youtube-music"},{"providerId",ids[i].mid(11)},{"name",names.value(i)}});
        if(credits.isEmpty()||work.seeds.isEmpty())continue;
        QVariantMap row{{"kind","song"},{"provider","youtube-music"},{"providerId",video},{"id","ytm:track:"+video},
            {"title",track.value("title")},{"seconds",track.value("seconds")},{"artistRefs",credits},
            {"cover",record.value("cover")},{"record",record},{"trackIndex",index},{"relation","mix-fallback"}};
        auto album=record.value("providerIds").toMap().value("youtube-music",record.value("browseId")).toString();
        if(album.startsWith("ytm:album:"))album=album.mid(10);
        if(!album.isEmpty()&&record.value("type")!="Song")row["albumRef"]=QVariantMap{{"kind","album"},{"provider","youtube-music"},{"providerId",album}};
        QVariantMap matchingSeed;
        for(const auto &picked:work.seeds)for(const auto &credit:credits)
            if(picked.toMap().value("providerId")==credit.toMap().value("providerId")){matchingSeed=picked.toMap();break;}
        if(matchingSeed.isEmpty())continue;
        if(!hasSongCandidates)add(row,matchingSeed,"legacy-album-mix","quick-picks",false);
        // Keep the existing quick-pick/Spin fallback independent of Deep cuts.
        if(!topRecordings.contains(video))add(row,matchingSeed,"album-deep-cuts","quick-picks",false);
        const int year=record.value("year").toInt();
        const auto primary=credits.first().toMap().value("providerId").toString();
        if(year<=0||year>oldestAllowed||album.isEmpty()||record.value("type")=="Song"||!hits.contains(primary))continue;
        const auto title=hitTitle(track.value("title").toString());
        if(title.isEmpty()||hits.value(primary).contains(title)||topRecordings.contains(video))continue;
        for(const auto &picked:work.seeds)if(picked.toMap().value("providerId").toString()==primary) {
            row["releaseGroupMbid"]=record.value("releaseGroupMbid");
            add(row,picked.toMap(),"album-deep-cuts","deep-cuts",false);
            break;
        }
    }
    for(const auto &seedValue:work.seeds){const auto seed=seedValue.toMap();sourceSnapshot.append(work.evidence.value(seed.value("key").toString()));
        for(const auto &value:work.collections.value("albums").toList()){const auto saved=value.toMap(),album=saved.value("album").toMap();const auto ids=album.value("artistIds").toStringList();
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
    QVariantList suppressedItems;QSet<QString> suppressedKeys;
    auto appendSuppressed=[&](const QVariantMap &card) {
        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(card.value("ref").toMap()));
        if(!ref.ok())return true;const auto own=taste.find(*ref.value);
        const bool hidden=own && own->suppression().hidden(),snoozed=own && own->suppression().isSnoozedAt(now);
        QVariantList excluded;
        if(own && own->suppression().doNotRecommend() && ref.value->kind()==CanonicalEntityKind::Artist)
            excluded.append(QVariantMap{{"ref",card.value("ref")},{"name",card.value("title")}});
        bool artistSuppressed=false;
        for(const auto &value:card.value("artistRefs").toList()) {
            const auto credit=value.toMap();if(const auto artistRef=typed(credit)) {
                if(const auto state=taste.find(*artistRef)) {
                    artistSuppressed|=state->suppression().doNotRecommend() || state->suppression().hidden() || state->suppression().isSnoozedAt(now);
                    if(state->suppression().doNotRecommend())excluded.append(QVariantMap{{"ref",artistRef->toJson().toVariantMap()},{"name",credit.value("name",credit.value("title"))}});
                }
            }
        }
        const bool suppressed=hidden || snoozed || artistSuppressed || !excluded.isEmpty();
        if(suppressed && !suppressedKeys.contains(ref.value->stableKey())) {
            suppressedKeys.insert(ref.value->stableKey());suppressedItems.append(QVariantMap{
                {"ref",card.value("ref")},{"title",card.value("title")},{"artistRefs",card.value("artistRefs")},
                {"seedArtistRef",card.value("seedArtistRef")},{"hidden",hidden},{"snoozed",snoozed},
                {"excluded",!excluded.isEmpty()},{"excludedArtistRefs",excluded}});
        }return suppressed;
    };
    work.retainedCarousel.clear();
    for(const auto &node:nodes) {
        if(appendSuppressed(node.card))continue;
        const auto key=node.card.value("key").toString();
        if(work.selectionKeys.contains(key)){
            auto card=node.card;
            card["spotlightKind"]=node.sections.contains("new-from-your-artists")?"new-from-your-artists"
                :node.sections.contains("related-discoveries")?"related-discoveries":"from-your-artists";
            work.retainedCarousel.insert(key,card);
        }
    }
    for(const auto &node:quickNodes)appendSuppressed(node.card);
    const bool loading=work.loading;QVariantList sections;QSet<QString> prior,distinctSeeds;
    for(const auto &seed:work.seeds)if(const auto ref=seedIdentity(seed.toMap()))distinctSeeds.insert(ref->stableKey());
    const QStringList sectionIds{"from-your-artists","related-discoveries","new-from-your-artists","quick-picks","deep-cuts","because-you-played","artists-like-yours"};
    const QStringList titles{"From your artists","Related discoveries","New from your artists","Quick picks","Deep cuts from your artists","",""};
    QVariantList crateAlbums,crateSongs;
    for(int sectionIndex=0;sectionIndex<sectionIds.size();++sectionIndex){const auto section=sectionIds[sectionIndex];QVector<RecommendationCandidate> pool;RankerFeatureContext features;
        const bool historyShelf=section=="because-you-played", discoveryShelf=section=="artists-like-yours";
        const auto &sectionNodes=historyShelf?playedNodes:discoveryShelf?similarNodes:
            section=="deep-cuts"?deepNodes:section=="quick-picks"?quickNodes:nodes;
        for(auto it=sectionNodes.cbegin();it!=sectionNodes.cend();++it) {
            if(!it->sections.contains(section))continue;
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
        if(section=="quick-picks") {
            config.version="vinyl-song-radio-related-v1";config.maxPerArtist=2;config.maxRecordingsPerReleaseGroup=1;
            // Consensus contributes support without an unbounded seed-affinity award.
            QMap<QString,QSet<QString>> support;
            for(const auto &entry:features.seedAffinities)support[entry.entity.stableKey()].insert(entry.seed.stableKey());
            for(auto &entry:features.seedAffinities)entry.multiplier/=std::max(1,int(support.value(entry.entity.stableKey()).size()));
        }
        if(section=="deep-cuts"){config.maxPerArtist=2;config.maxRecordingsPerReleaseGroup=1;}
        if(historyShelf||discoveryShelf) {
            config.maxPerArtist=0;
            config.seedAffinityWeight=0; // The calibrated sum already accounts for each seed.
            config.rawScoreCalibrations.append({*RecommendationGeneratorId::parse(
                historyShelf?"home-album-similarity":"home-artist-similarity").value,600,60000});
        }
        const auto ranked=rankDeterministically(RecommendationCandidateSet::normalize(pool),taste,
            *derived.value,priorVisitExposure,features,config);
        if(!ranked.ok())continue;QVariantList preferred,repeated;
        for(const auto &entry:ranked.value->ranked()){auto card=sectionNodes.value(entry.entity().stableKey()).card;QVariantList reasons;
            for(const auto &e:entry.explanation()){QVariantMap reason{{"kind",rankEvidenceKindName(e.kind)},{"sourceId",e.sourceId},{"scoreDelta",e.scoreDelta}};if(e.relatedEntity)reason["seed"]=e.relatedEntity->toJson().toVariantMap();reasons.append(reason);}
            card["score"]=entry.score();card["reasons"]=reasons;card["evidence"]=reasons;card["configVersion"]=config.version;
            if(!historyShelf&&!discoveryShelf&&prior.contains(entry.entity().stableKey()))repeated.append(card);else preferred.append(card);
        }
        preferred.append(repeated);
        if(historyShelf){work.playedAlbums=preferred;continue;}
        if(discoveryShelf){work.similarArtistCards=preferred;continue;}
        if(section=="from-your-artists")crateAlbums=preferred;else if(section=="quick-picks")crateSongs=preferred;
        if(section!="related-discoveries")work.rowCandidates[section]=preferred;
        if(preferred.size()>12)preferred=preferred.mid(0,12);if(preferred.isEmpty())continue;
        if(section=="quick-picks"){work.quickPicks=preferred;continue;}
        prior.clear();for(const auto &card:preferred)prior.insert(card.toMap().value("key").toString());
        bool stale=false;QString fetched;
        for(const auto &data:work.evidence){stale|=data.value("_stale").toBool();if(data.value("fetchedAt").toString()>fetched)fetched=data.value("fetchedAt").toString();}
        sections.append(QVariantMap{{"id",section},{"key",section},{"title",titles[sectionIndex]},{"kind","recommendations"},{"items",preferred},
            {"status",stale||!work.failed.isEmpty()?"stale":loading?"loading":"ready"},{"loading",loading},{"error",work.error},{"fetchedAt",fetched},
            {"canRetry",!loading},{"configVersion",config.version},{"evaluatedAtMs",now}});
    }
    for(const auto &node:nodes)if(!appendSuppressed(node.card)) {
        const auto kind=crateKind(node.card.value("kind").toString());
        if(kind=="albums")crateAlbums.append(node.card);else if(kind=="songs")crateSongs.append(node.card);
    }
    for(const auto &node:quickNodes)if(!appendSuppressed(node.card))crateSongs.append(node.card);
    // An actual failed/pending artist lane is actionable even without cached
    // cards. Unsupported related/new lanes never become empty placeholders.
    if(!work.failed.isEmpty() && work.error.isEmpty())work.error="The music catalogue is unavailable.";
    const bool hasOwn=std::any_of(sections.begin(),sections.end(),[](const auto &s){return s.toMap().value("id")=="from-your-artists";});
    const bool pendingArtists=loading;
    if(!hasOwn && (pendingArtists || !work.failed.isEmpty()))sections.prepend(QVariantMap{
        {"id","from-your-artists"},{"key","from-your-artists"},{"title","From your artists"},{"kind","recommendations"},
        {"items",QVariantList()},{"status",pendingArtists?"loading":"error"},{"loading",pendingArtists},
        {"error",work.error},{"canRetry",!loading},{"evaluatedAtMs",now}});
    struct RetainedItem {QVariantMap card;qint64 count,last;QString key;};
    QList<RetainedItem> retainedAlbums;QSet<QString> retainedKeys;qint64 highestCount=0;
    for(const auto &value:work.collections.value("albums").toList()) {
        const auto album=value.toMap().value("album").toMap();if(album.isEmpty())continue;
        const bool yt=album.value("origin")=="youtube-music";
        auto raw=album;raw["kind"]="album";raw["provider"]=yt?"youtube-music":"local";
        raw["providerId"]=yt?album.value("providerIds").toMap().value("youtube-music",album.value("browseId")):album.value("id");
        if(yt)raw["id"]="ytm:album:"+raw.value("providerId").toString();
        const auto converted=VinylRecommendationIdentity::fromCatalogue(raw);if(!converted.value("ok").toBool())continue;
        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(converted.value("ref").toMap()));
        if(!ref.ok() || retainedKeys.contains(ref.value->stableKey()))continue;
        qint64 count=0,last=0;
        for(const auto &aggregate:derived.value->aggregates())if(aggregate.entity()==*ref.value) {
            count=aggregate.playCount();last=aggregate.lastPlayedAtMs().value_or(0);break;
        }
        // Missing retained history is unknown, never evidence of zero lifetime plays.
        if(count<=0 || last<=0)continue;
        QVariantList credits;const auto ids=album.value("artistIds").toStringList(),names=album.value("artists").toStringList();
        for(int i=0;i<ids.size();++i) {
            const bool artistYt=ids[i].startsWith("ytm:artist:");
            if(yt && !artistYt)continue;
            credits.append(QVariantMap{{"kind","artist"},{"provider",artistYt?"youtube-music":"local"},
                {"providerId",artistYt?ids[i].mid(11):ids[i]},{"name",names.value(i)}});
        }
        QVariantMap card{{"id",converted.value("destination").toMap().value("id")},{"key",ref.value->stableKey()},
            {"typedKey",converted.value("ref")},{"ref",converted.value("ref")},{"kind","albums"},
            {"title",album.value("title")},{"subtitle",names.join(QStringLiteral(" · "))},{"cover",album.value("cover")},
            {"destination",converted.value("destination")},{"artistRefs",credits},{"configVersion","vinyl-home-v1"},
            {"reasons",QVariantList{QVariantMap{{"kind","retained_listening"},{"sourceId","profile-qualified-activity"},{"playCount",count},{"lastPlayedAtMs",last}}}}};
        if(card.value("title").toString().isEmpty() || appendSuppressed(card))continue;
        retainedKeys.insert(ref.value->stableKey());highestCount=std::max(highestCount,count);
        retainedAlbums.append({card,count,last,ref.value->stableKey()});
    }
    // Saved-song cards retain the personal library's already-resolved route.
    // A history reference is not a new remote playback-match attestation.
    for(const auto &value:work.collections.value("songs").toList()) {
        auto card=value.toMap();const auto row=card.value("row").toMap(),track=row.value("track").toMap();
        if(row.isEmpty() || card.value("unavailable").toBool())continue;
        const auto album=row.value("edition").toMap().value("sourceAlbum").toMap();
        const bool yt=album.value("origin")=="youtube-music";
        const auto converted=VinylRecommendationIdentity::fromCatalogue({{"kind","song"},{"provider",yt?"youtube-music":"local"},
            {"providerId",yt?track.value("videoId"):row.value("trackId",track.value("id"))}});
        if(!converted.value("ok").toBool())continue;
        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(converted.value("ref").toMap()));
        if(!ref.ok() || retainedKeys.contains(ref.value->stableKey()))continue;
        qint64 count=0,last=0;for(const auto &aggregate:derived.value->aggregates())if(aggregate.entity()==*ref.value) {
            count=aggregate.playCount();last=aggregate.lastPlayedAtMs().value_or(0);break;
        }
        if(count<=0 || last<=0 || card.value("id").toString().isEmpty() || card.value("title").toString().isEmpty())continue;
        QVariantList credits;auto ids=track.value("artistIds").toStringList(),names=track.value("artists").toStringList();
        if(ids.isEmpty()){ids=album.value("artistIds").toStringList();names=album.value("artists").toStringList();}
        for(int i=0;i<ids.size();++i) {
            const bool artistYt=ids[i].startsWith("ytm:artist:");if(yt && !artistYt)continue;
            credits.append(QVariantMap{{"kind","artist"},{"provider",artistYt?"youtube-music":"local"},
                {"providerId",artistYt?ids[i].mid(11):ids[i]},{"name",names.value(i)}});
        }
        card["kind"]="saved-song";card["ref"]=converted.value("ref");card["key"]=ref.value->stableKey();card["typedKey"]=converted.value("ref");
        card["destination"]=QVariantMap{{"kind","saved-song"},{"id",card.value("id")}};
        card["subtitle"]=card.value("artist");card["artistRefs"]=credits;card["configVersion"]="vinyl-home-v1";
        card["reasons"]=QVariantList{QVariantMap{{"kind","retained_listening"},{"sourceId","profile-qualified-activity"},{"playCount",count},{"lastPlayedAtMs",last}}};
        if(appendSuppressed(card))continue;retainedKeys.insert(ref.value->stableKey());highestCount=std::max(highestCount,count);
        retainedAlbums.append({card,count,last,ref.value->stableKey()});
    }
    std::sort(retainedAlbums.begin(),retainedAlbums.end(),[](const auto &a,const auto &b){return a.last==b.last?a.key<b.key:a.last<b.last;});
    auto less=retainedAlbums;
    less.erase(std::remove_if(less.begin(),less.end(),[highestCount](const auto &a){return a.count>=highestCount;}),less.end());
    std::sort(less.begin(),less.end(),[](const auto &a,const auto &b){return a.count==b.count?(a.last==b.last?a.key<b.key:a.last<b.last):a.count<b.count;});
    if(less.size()>12)less=less.mid(0,12);
    QSet<QString> lessKeys;for(const auto &item:less)lessKeys.insert(item.key);
    auto historySection=[&](const QString &id,const QString &title,const QList<RetainedItem> &values) {
        QVariantList items;for(const auto &item:values)if(items.size()<12)items.append(item.card);
        if(!items.isEmpty())sections.append(QVariantMap{{"id",id},{"key",id},{"title",title},{"kind","recommendations"},
            {"items",items},{"status","ready"},{"loading",false},{"canRetry",false},{"evaluatedAtMs",now},{"configVersion","vinyl-home-v1"}});
    };
    QList<RetainedItem> rediscover;for(const auto &item:retainedAlbums)if(!lessKeys.contains(item.key))rediscover.append(item);
    historySection("rediscover-library","Rediscover your library",rediscover);
    historySection("less-played","Less played",less);
    const bool hasCards=std::any_of(sections.begin(),sections.end(),[](const auto &s){return !s.toMap().value("items").toList().isEmpty();});
    work.status=loading?"loading":!hasCards?work.failed.isEmpty()?"empty":"error":work.failed.isEmpty()?"ready":"stale";
    QVariantMap mappings;for(auto it=work.identities.cbegin();it!=work.identities.cend();++it)mappings[it.key()]=it.value();
    QVariantMap songEvidence;for(auto it=candidateSongEvidence.cbegin();it!=candidateSongEvidence.cend();++it)songEvidence[it.key()]=it.value();
    QVariantMap metadata;
    for(const auto &source:{QStringLiteral("discography"),QStringLiteral("similarity"),QStringLiteral("neighbors")}){
        const auto &values=source=="discography"?work.discography:source=="similarity"?work.similarity:work.evidence;
        QVariantMap captured;for(auto it=values.cbegin();it!=values.cend();++it)
            if(source=="similarity") {
                const auto mbid=it.value().value("seed").toMap().value("mbid").toString();
                if(seedMbids.contains(mbid)||mbid==anchorMbid)captured[it.key()]=it.value();
            }else if(source!="neighbors"||work.neighbors.contains(it.key()))captured[it.key()]=it.value();
        metadata[source]=captured;
    }
    QVariantMap topTrackInputs;for(auto it=work.topTracks.cbegin();it!=work.topTracks.cend();++it)topTrackInputs[it.key()]=it.value();
    const QVariantMap inputs{{"catalogue",sourceSnapshot},{"anchorCatalogue",anchorCatalogue},{"topTracks",topTrackInputs},{"songCandidates",songEvidence},{"identities",mappings},{"metadata",metadata},{"library",work.collections.value("albums")},{"feedback",feedback},{"seeds",work.seeds},{"history",work.history}};
    auto configPolicy=policy();configPolicy["seedDecay"]=QVariantList{30,100,1.0,0.75,0.5};configPolicy["historyVersion"]=1;
    configPolicy["maxHomeCandidates"]=300;configPolicy["maxSongCandidates"]=120;configPolicy["maxCandidates"]=420;
    configPolicy["songRecommendations"]=QVariantMap{{"version","vinyl-song-radio-related-v1"},{"songsPerArtist",2},{"maxArtists",6},{"maxPerArtist",2},{"maxPerAlbum",1},{"deadlineMs",90000}};
    configPolicy["exposureApplication"]="prior-visits";
    configPolicy["historyShelves"]=QVariantMap{{"version",1},{"requireRetainedPlay",true},{"lessPlayed","positive-count-below-library-max"},{"maxShelf",12},{"rediscoverOrder","oldest-retained-play"}};
    const auto sourceHash=digest(inputs);
    const bool partialSongs=usingPreviousSongs||work.songFailed>0||std::any_of(work.songEvidence.cbegin(),work.songEvidence.cend(),[](const QVariantMap &data){return data.value("status")=="partial"||data.value("_stale").toBool();});
    const QVariantMap snapshot{{"configVersion","vinyl-home-v1"},{"evaluatedAtMs",now},{"sourceHash",sourceHash},{"configHash",digest(configPolicy)},{"seedHash",work.seedHash},{"candidateCount",nodes.size()+quickNodes.size()},{"historyComplete",false},{"optionalUnavailableCount",work.optionalUnavailable},
        {"qualifiedManualSessions",manualSessions},{"seedMultiplier",seedMultiplier},{"exposureSnapshotId",digest(QVariantList{work.visitId,sourceHash})},{"exposureCount",exposure.records().size()},{"suppressedItems",suppressedItems},
        {"historyEnabled",work.keepHistory && work.history.value("enabled").toBool()},
        {"activityResetGeneration",QVariant::fromValue(work.activityReset)},{"historyResetGeneration",work.historyReset},
        {"songRecommendations",QVariantMap{{"configVersion","vinyl-song-radio-related-v1"},
            {"seedSongCount",work.songAttempted},{"completedSeedSongCount",work.songEvidence.size()},
            {"failedSeedSongCount",work.songFailed},{"pendingSeedSongCount",work.songPending},
            {"seedSongs",work.songSeeds},{"usesProviderCandidates",hasSongCandidates},{"usingPreviousSelection",usingPreviousSongs},
            {"status",work.songPending>0?"loading":hasSongCandidates?partialSongs?"partial":"ready":work.songAttempted==0?"idle":"unavailable"}}},
        {"baselineQuickPicks",baselineQuickPicks}};
    for(const auto &node:nodes)work.rankedCrateCards.append(node.card);
    for(const auto &node:quickNodes)work.rankedCrateCards.append(node.card);
    work.sections=std::move(sections);work.snapshot=std::move(snapshot);
    work.crateAlbums=std::move(crateAlbums);work.crateSongs=std::move(crateSongs);
    QVariantList feeds;
    for(auto it=work.chartFeeds.cbegin();it!=work.chartFeeds.cend();++it){auto feed=it.value();const auto parts=it.key().split(':');
        feed["key"]=it.key();feed["source"]=feed.value("provenance").toMap().value("source");
        feed["kind"]=it.key()=="lastfm:tracks"?"songs":it.key()=="lastfm:artists"?"artists":parts.value(2);
        feed["storefront"]=parts.value(1);feeds.append(feed);
    }

    for(const QString &kind:{"albums","songs","artists"}){
        auto items=VinylSourcePool::mergeCharts(feeds,kind);
        for(auto &entry:items){auto item=entry.toMap();item["kind"]=kind;item["typedKey"]=item.value("ref");entry=item;}
        work.charts[kind]=items;
        QDate oldest;
        for(const auto &entry:feeds){const auto feed=entry.toMap();if(feed.value("kind")!=kind||feed.value("items").toList().isEmpty())continue;
            const auto date=QDateTime::fromString(feed.value("fetchedAt").toString(),Qt::ISODateWithMs).date();
            if(date.isValid()&&(!oldest.isValid()||date<oldest))oldest=date;
        }
        if(oldest.isValid()){int year=0;const auto week=oldest.weekNumber(&year);work.chartWeeks[kind]=QString("%1-W%2").arg(year).arg(week,2,10,QChar('0'));}
    }
    // Crate frequency is derived from the existing qualified activity facts.
    // Each explicit album/artist start counts once; automatic following tracks
    // contribute to songs without turning one album visit into twelve visits.
    auto countCrate=[&](const QVariantMap &raw,qint64 at){
        const auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(raw));if(!ref.ok())return;
        const auto key=ref.value->stableKey();++work.crateCounts[key];work.crateLast[key]=qMax(work.crateLast.value(key),at);
    };
    for(const auto &value:work.history.value("sessions").toList()){
        const auto session=value.toMap();const auto origin=session.value("origin").toString();
        if(session.value("qualifiedMs").toLongLong()<10000||origin=="unknown")continue;
        const auto at=session.value("atMs").toLongLong();countCrate(session.value("recording").toMap(),at);
        if(origin=="manual")for(const auto &entity:session.value("entities").toList())countCrate(entity.toMap(),at);
    }
    work.catalogueCrateCards=catalogueCrateCards(work.collections);
    work.valid=true;return work;
}

void VinylRecommendationService::publishHome(HomeComposition work){
    const bool startEnrichment=std::exchange(m_home->firstCorePublicationPending,false);
    if(startEnrichment)QTimer::singleShot(0,this,[this,epoch=m_home->epoch]{
        if(!m_active||m_home->epoch!=epoch)return;
        queueHomeEnrichment();
        // The cache callback pumps and republishes when there is new evidence.
        if(m_home->metadataReading.isEmpty())pumpHome();
    });
    if(work.feedbackError){m_home->quickPicks.clear();m_home->sections.clear();m_home->status="error";m_home->error="Saved feedback could not be read.";emit homeChanged();return;}
    if(!work.valid)return;
    if(work.clearExposure&&m_preferences&&m_preferences->property("vinylRecommendationExposureSnapshot").toString()==work.storedExposure)storeExposure({});
    const auto previousQuickPicks=m_home->quickPicks;
    m_home->quickPicks=std::move(work.quickPicks);
    m_home->history=std::move(work.history);m_home->excludedArtistProviderIds=std::move(work.excludedArtistProviderIds);
    m_home->retainedCarousel=std::move(work.retainedCarousel);m_home->status=std::move(work.status);
    if(m_home->error.isEmpty())m_home->error=std::move(work.error);
    const auto &sections=work.sections;
    if(sections!=m_home->sections||work.snapshot!=m_home->snapshot){m_home->sections=sections;m_home->snapshot=std::move(work.snapshot);++m_home->revision;}
    if(work.anchor!=m_home->anchor) {
        m_home->anchor=work.anchor;
        for(const auto &lane:m_home->retireAnchorJobs())cancelLane(m_source,lane);
        QTimer::singleShot(0,this,[this,epoch=m_home->epoch]{
            if(m_active&&m_home->visited&&m_home->epoch==epoch&&m_home->refreshed&&!m_home->seedCachePending){
                queueHomeEnrichment();pumpHome();
            }
        });
    }
    const auto &feedback=work.feedback;
    selectHomeCarousel();
    const bool selectionReady=!m_home->corePending();
    m_home->listening->setInputs(m_source,m_scope,m_generation,m_saved,selectionReady?m_home->sections:QVariantList(),
        m_home->selectionKey+QString::number(m_home->selectionSerial)+digest(feedback)+QString::number(m_keepHistory),
        m_home->excludedArtistProviderIds);
    queueSongCandidates();
    QVariantMap legacy;
    for(const auto &entry:sections) {
        const auto section=entry.toMap();legacy[section.value("id").toString()]=section.value("items");
    }
    const auto listeningCards=m_home->listening->cards();
    auto stations=listeningCards;
    for(const auto &entry:legacy.value("related-discoveries").toList()){
        auto item=entry.toMap();if(item.value("provider")!="youtube-music"||item.value("requiresResolution").toBool())continue;
        item["kind"]="radio";item["seedArtist"]=item;item["id"]="home-radio:"+item.value("providerId").toString();stations.append(item);
        if(stations.size()>=12)break;
    }
    // The claim order only affects Home rows, not the legacy, Spin or station inputs.
    QVariantMap homeBuckets{{"mixes-radio",stations},{"rediscover",legacy.value("rediscover-library")}};
    homeBuckets["because-you-played"]=work.playedAlbums;
    homeBuckets["artists-like-yours"]=work.similarArtistCards;
    homeBuckets["albums-for-you"]=work.rowCandidates.value("from-your-artists");
    homeBuckets["deep-cuts"]=work.rowCandidates.value("deep-cuts");
    homeBuckets["quick-picks"]=work.rowCandidates.value("quick-picks");
    auto recent=work.rowCandidates.value("new-from-your-artists").toList();
    recent.removeIf([](const QVariant &value){return value.toMap().value("dateSource")!="musicbrainz";});
    std::stable_sort(recent.begin(),recent.end(),[](const QVariant &a,const QVariant &b){return a.toMap().value("releaseDate").toString()>b.toMap().value("releaseDate").toString();});
    homeBuckets["new-from-your-artists"]=recent;
    const auto &charts=work.charts;
    const auto &crateCounts=work.crateCounts;const auto &crateLast=work.crateLast;
    m_home->crateCandidates.clear();
    auto rememberCrate=[&](QVariantMap card){
        if(card.value("id").toString().isEmpty()||card.value("title").toString().isEmpty())return;
        auto key=card.value("key").toString();if(key.isEmpty())key=card.value("id").toString();card["key"]=key;
        if(!m_home->crateCandidates.contains(key))m_home->crateCandidates[key]=card;
        m_home->crateCandidates[crateAlias(card.value("kind").toString(),card.value("id").toString())]=card;
    };
    for(const auto &card:work.rankedCrateCards)rememberCrate(card.toMap());
    for(const auto &value:stations)rememberCrate(value.toMap());
    for(const auto &value:charts)for(const auto &card:value.toList())rememberCrate(card.toMap());
    for(const auto &card:work.catalogueCrateCards)rememberCrate(card.toMap());
    QVariantList crate;QSet<QString> used;
    for(const auto &value:(m_preferences?m_preferences->property("vinylCratePins").toList():QVariantList())){
        const auto pin=value.toMap();auto card=m_home->crateCandidates.value(crateAlias(pin.value("kind").toString(),pin.value("id").toString()));
        if(card.isEmpty()&&pin.value("provider")=="youtube-music")card=m_home->crateCandidates.value(pin.value("key").toString());
        if(card.isEmpty()){
            card=pin;card["unavailable"]=true;
            if(pin.value("provider")!="local" && !pin.value("catalogueItem").toMap().isEmpty()){
                card["unavailable"]=false;card["requiresResolution"]=true;
            }
            if(pin.value("provider")=="youtube-music"&&pin.value("kind")=="artists")card["unavailable"]=false;
        }
        const auto key=pin.value("key").toString();if(used.contains(key))continue;used.insert(key);
        card["key"]=key;card["pinned"]=true;crate.append(card);
    }
    QMap<QString,QVariantList> frequent;
    for(const auto &card:m_home->crateCandidates){const auto key=card.value("key").toString();
        if(used.contains(key)||crateCounts.value(key)<=0||card.value("unavailable").toBool())continue;
        used.insert(key);auto ranked=card;ranked["playCount"]=crateCounts.value(key);ranked["lastPlayedAtMs"]=crateLast.value(key);
        frequent[crateKind(card.value("kind").toString())].append(ranked);
    }
    for(auto &items:frequent)std::sort(items.begin(),items.end(),[](const QVariant &a,const QVariant &b){
        const auto x=a.toMap(),y=b.toMap();if(x.value("playCount")!=y.value("playCount"))return x.value("playCount").toInt()>y.value("playCount").toInt();
        if(x.value("lastPlayedAtMs")!=y.value("lastPlayedAtMs"))return x.value("lastPlayedAtMs").toLongLong()>y.value("lastPlayedAtMs").toLongLong();
        return x.value("key").toString()<y.value("key").toString();
    });
    for(int index=0;index<256;++index){bool added=false;
        for(const QString &kind:{"albums","artists","songs","mixes","radio"}){
            const auto &items=frequent[kind];if(index<items.size()){crate.append(items[index]);added=true;}
        }if(!added)break;
    }
    crate=fillCrate(crate,m_source?listeningCards:QVariantList(),m_source?work.crateAlbums:QVariantList(),m_source?work.crateSongs:QVariantList(),m_saved);
    const QVariantMap pool{{"rows",VinylSourcePool::homeRows(homeBuckets,m_home->history.value("qualifiedManualSessions").toInt()>0)},{"charts",charts},{"chartWeeks",work.chartWeeks},{"crate",crate},
        {"forYou",m_home->carousel},{"songs",m_home->quickPicks},{"albums",legacy.value("from-your-artists")},{"artists",legacy.value("related-discoveries")},
        {"pendingCount",m_home->waiting.size()+m_home->active.size()+m_home->songWaiting.size()+m_home->songActive.size()+m_home->songCacheReads.size()}};
    if(pool!=m_home->pool){m_home->pool=pool;++m_home->revision;}
    if(previousQuickPicks!=m_home->quickPicks)emit listeningChanged();
    emit homeChanged();
}

void VinylRecommendationService::selectHomeCarousel(bool refresh) {
    const auto day=QDateTime::fromMSecsSinceEpoch(m_home->clock()).date().toString(Qt::ISODate);
    const auto selectionKey=day+"/"+m_home->seedHash+"/"+QString::number(m_home->activityReset)+"/"+QString::number(m_home->historyReset);
    const auto stored=m_preferences?QJsonDocument::fromJson(m_preferences->property("vinylHomeSelectionSnapshot").toString().toUtf8()).object():QJsonObject();
    if(m_home->selectionKey!=selectionKey){
        m_home->previousKeys=m_home->selectionKeys;
        m_home->selectionKeys.clear();m_home->selectionSerial=0;m_home->selectionKey=selectionKey;
        if(stored.value("version")==1&&stored.value("profileId").toString()==m_scope&&stored.value("seedHash").toString()==m_home->seedHash){
            QStringList oldKeys;for(const auto &key:stored.value("keys").toArray())oldKeys.append(key.toString());
            if(stored.value("day").toString()==day){
                m_home->selectionKeys=oldKeys;m_home->selectionSerial=stored.value("refreshSerial").toInteger();
                for(const auto &key:stored.value("previousKeys").toArray())m_home->previousKeys.append(key.toString());
            }else m_home->previousKeys=oldKeys;
        }
    }
    if(refresh){m_home->previousKeys=m_home->selectionKeys;m_home->selectionKeys.clear();++m_home->selectionSerial;}
    QMap<QString,QVariantMap> pool;
    QStringList fresh;
    for(const auto &value:m_home->sections){
        const auto section=value.toMap();const auto sectionId=section.value("id").toString();
        if(sectionId=="rediscover-library"||sectionId=="less-played")continue;
        for(const auto &item:section.value("items").toList()){
            auto card=item.toMap();const auto destination=card.value("destination").toMap();
            if(destination.isEmpty()||card.value("title").toString().isEmpty())continue;
            const auto key=card.value("key",card.value("id")).toString();if(key.isEmpty())continue;
            card["spotlightKind"]=sectionId;pool.insert(key,card);
            if(sectionId=="new-from-your-artists")fresh.append(key);
        }
    }
    if(!refresh)for(auto it=m_home->retainedCarousel.cbegin();it!=m_home->retainedCarousel.cend();++it)
        if(m_home->selectionKeys.contains(it.key()))pool.insert(it.key(),it.value());
    if(pool.isEmpty()){m_home->carousel.clear();return;}
    // Previously selected but unseen cards are rotated too; actual impressions
    // keep their separate, qualified exposure policy in the ranker.
    QStringList keys;
    for(const auto &key:m_home->selectionKeys)if(pool.contains(key)&&!keys.contains(key))keys.append(key);
    const bool restoringPending=!refresh&&!m_home->selectionKeys.isEmpty()
        &&m_home->corePending();
    QStringList candidates=pool.keys();
    const auto salt=selectionKey+"/"+QString::number(m_home->selectionSerial);
    std::stable_sort(candidates.begin(),candidates.end(),[&](const QString &a,const QString &b){
        const bool oldA=m_home->previousKeys.contains(a),oldB=m_home->previousKeys.contains(b);
        if(oldA!=oldB)return !oldA;
        return digest(QVariantList{salt,a})<digest(QVariantList{salt,b});
    });
    if(keys.isEmpty()&&!restoringPending){
        for(const auto &key:candidates)if(fresh.contains(key)&&!m_home->previousKeys.contains(key)){keys.append(key);break;}
    }
    QMap<QString,int> artistCounts;
    auto primaryArtist=[&](const QString &key){const auto refs=pool.value(key).value("artistRefs").toList();return refs.isEmpty()?QString():refs.first().toMap().value("providerId").toString();};
    for(const auto &key:keys)++artistCounts[primaryArtist(key)];
    for(int pass=0;!restoringPending&&pass<2&&keys.size()<5;++pass)for(const auto &key:candidates){
        if(keys.size()==5)break;if(keys.contains(key))continue;
        const auto artist=primaryArtist(key);if(pass==0&&!artist.isEmpty()&&artistCounts.value(artist)>=2)continue;
        keys.append(key);++artistCounts[artist];
    }
    m_home->previousKeys.removeDuplicates();
    if(!restoringPending&&(refresh||!m_home->corePending()))m_home->selectionKeys=keys;
    m_home->carousel.clear();
    for(const auto &key:keys)m_home->carousel.append(pool.value(key));
    if(!keys.isEmpty()&&!m_home->corePending()&&m_preferences
        &&m_preferences->metaObject()->indexOfMethod("setVinylHomeSelectionSnapshot(QString)")>=0){
        const auto snapshot=QString::fromUtf8(wire(QVariantMap{{"version",1},{"profileId",m_scope},{"day",day},
            {"seedHash",m_home->seedHash},{"refreshSerial",m_home->selectionSerial},{"keys",keys},{"previousKeys",m_home->previousKeys.mid(0,10)}}));
        if(snapshot!=m_preferences->property("vinylHomeSelectionSnapshot").toString()){
            bool saved=false;QScopedValueRollback<bool> guard(m_home->persisting,true);
            QMetaObject::invokeMethod(m_preferences,"setVinylHomeSelectionSnapshot",Qt::DirectConnection,Q_RETURN_ARG(bool,saved),Q_ARG(QString,snapshot));
        }
    }
}
