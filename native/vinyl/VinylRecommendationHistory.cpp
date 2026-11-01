#include "VinylRecommendationHistory.h"
#include "VinylRecommendationIdentity.h"
#include "account/ActivityStore.h"
#include "account/HistoryStore.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <algorithm>

using namespace vinyl;
using namespace vinyl::recommendation;
namespace {
std::optional<RecommendationEntityRef> readRef(const QVariant &raw) {
    return RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(raw.toMap())).value;
}
QVariantMap typedRow(const QVariantMap &raw) {
    return VinylRecommendationIdentity::fromCatalogue(raw).value("ref").toMap();
}
QVariantMap emptyProjection(bool ok,bool enabled) {
    return {{"ok",ok},{"enabled",enabled},{"historyComplete",false},{"qualifiedManualSessions",0},{"sessions",QVariantList()}};
}
QString catalogueKey(const QString &album,const QString &track) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{album,track}).toJson(QJsonDocument::Compact));
}
}
QVariantMap VinylRecommendationHistory::project(ActivityStore *activity,HistoryStore *history,
                                               const QVariantList &catalogueRows,qint64 nowMs) {
    if(!activity || !activity->retentionEnabled())return emptyProjection(true,false);
    if(nowMs<=0 || !activity->healthy() || (history && !history->healthy()))return emptyProjection(false,true);
    struct Session {
        QString id,item,origin="unknown",outcome;
        QVariantMap recording;
        QVariantList contexts;
        QList<QPair<qint64,qint64>> intervals;
        qint64 at=0,outcomeAt=0,position=0;
        bool conflict=false,started=false;
    };
    QMap<QString,QVariantMap> known;
    for(const auto &value:catalogueRows) {
        const auto entry=value.toMap();auto album=entry.value("album",entry).toMap();
        const auto payload=entry.value("payload").toMap();if(!payload.isEmpty())album=payload.value("album").toMap();
        const bool yt=album.value("origin")=="youtube-music";
        auto release=album;release["kind"]="album";release["provider"]=yt?"youtube-music":"local";
        release["providerId"]=yt?album.value("providerIds").toMap().value("youtube-music",album.value("browseId")):album.value("id");
        if(yt)release["id"]="ytm:album:"+release.value("providerId").toString();
        QVariantList contexts;const auto releaseRef=typedRow(release);if(!releaseRef.isEmpty())contexts.append(releaseRef);
        for(const auto &id:album.value("artistIds").toStringList()) {
            const bool artistYt=id.startsWith("ytm:artist:");
            const auto artistRef=typedRow({{"kind","artist"},{"provider",artistYt?"youtube-music":"local"},
                {"providerId",artistYt?id.mid(11):id}});
            if(!artistRef.isEmpty())contexts.append(artistRef);
        }
        for(const auto &trackValue:album.value("tracks").toList()) {
            const auto track=trackValue.toMap();auto recording=track;
            recording["kind"]="song";recording["provider"]=yt?"youtube-music":"local";
            recording["providerId"]=yt?track.value("videoId"):track.value("id");
            if(yt)recording["id"]="ytm:track:"+recording.value("providerId").toString();
            const auto entity=typedRow(recording);if(entity.isEmpty())continue;
            known[catalogueKey(album.value("id").toString(),track.value("id").toString())]={{"recording",entity},{"contexts",contexts}};
        }
    }
    QMap<QString,Session> sessions;
    for(const auto &fact:activity->historyProjectionFacts()) {
        if(fact.value("world")!="vinyl" || fact.value("kind")!="music" || fact.value("syncable").toBool()
            || fact.value("_trackerOrigin")!="native_local")continue;
        const auto type=fact.value("type").toString();if(type!="music_action" && type!="playback_delta")continue;
        const auto at=fact.value(type=="playback_delta"?"startAtMs":"atMs").toLongLong();
        const auto resetAt=std::max(activity->resetAtMs(),history?history->syncResetBarrierAtMs():qint64(0));
        if(at<=0 || at>nowMs || at<=resetAt)continue;
        const auto sessionId=fact.value("sessionId").toString(),item=fact.value("itemKey").toString();
        const auto key=QString::fromUtf8(QJsonDocument(QJsonArray{sessionId,item}).toJson(QJsonDocument::Compact));
        auto &session=sessions[key];session.id=key;session.item=item;
        if(!session.at || at<session.at)session.at=at;
        if(type=="playback_delta") {
            const auto end=fact.value("endAtMs").toLongLong(),active=fact.value("activeMs").toLongLong();
            if(end>nowMs || active<=0)continue;
            // Count overlapping persisted samples once, even if they have distinct IDs.
            session.intervals.append({at,std::min(end,at+active)});continue;
        }
        const auto ref=readRef(fact.value("entityRef"));if(!ref || ref->kind()!=CanonicalEntityKind::Recording){session.conflict=true;continue;}
        const auto identity=ref->toJson().toVariantMap();
        if(!session.recording.isEmpty() && session.recording!=identity)session.conflict=true;
        session.recording=identity;
        const auto origin=fact.value("origin").toString();
        if(session.origin!="unknown" && session.origin!=origin)session.conflict=true;
        session.origin=origin;
        auto addContext=[&](const QVariant &raw,CanonicalEntityKind kind) {
            const auto context=readRef(raw);
            if(context && context->kind()==kind && !session.contexts.contains(context->toJson().toVariantMap()))
                session.contexts.append(context->toJson().toVariantMap());
        };
        addContext(fact.value("releaseRef"),CanonicalEntityKind::Release);
        for(const auto &artist:fact.value("artistRefs").toList())addContext(artist,CanonicalEntityKind::Artist);
        const auto action=fact.value("action").toString();
        if(action=="start")session.started=true;
        else if(session.outcome.isEmpty() || at<session.outcomeAt) {
            session.outcome=action;session.outcomeAt=at;session.position=fact.value("positionMs").toLongLong();
        } else if(at==session.outcomeAt && action!=session.outcome)session.conflict=true;
    }
    QVariantList output;int manual=0;
    for(auto &session:sessions) {
        if(session.conflict)continue;
        if(session.recording.isEmpty()) {
            const auto item=QJsonDocument::fromJson(session.item.toUtf8()).array();
            if(item.size()!=5)continue;
            const auto found=known.value(catalogueKey(item[0].toString(),item[4].toString()));
            session.recording=found.value("recording").toMap();session.contexts=found.value("contexts").toList();
            if(session.recording.isEmpty())continue;
        }
        std::sort(session.intervals.begin(),session.intervals.end());
        qint64 qualified=0,last=0;
        for(const auto &interval:session.intervals) {
            const auto start=std::max(interval.first,last);
            if(interval.second>start)qualified+=interval.second-start;
            last=std::max(last,interval.second);
        }
        if(qualified>=10000 && session.started && session.origin=="manual")++manual;
        output.append(QVariantMap{{"sessionId",session.id},{"recording",session.recording},{"entities",session.contexts},
            {"atMs",session.at},{"qualifiedMs",qualified},{"origin",session.started?session.origin:"unknown"},
            {"outcome",session.started?session.outcome:QString()},{"outcomeAtMs",session.outcomeAt},{"positionMs",session.position}});
    }
    auto result=emptyProjection(true,true);result["sessions"]=output;result["qualifiedManualSessions"]=manual;
    result["resetGeneration"]=QVariant::fromValue(activity->resetGeneration());
    // Chronology is factual, but does not manufacture outcome or play-count evidence.
    if(history) {
        QVariantList chronology;for(const auto &entry:history->records())
            if(entry.toMap().value("kind")=="music")chronology.append(entry);
        result["chronology"]=chronology;
    }
    return result;
}
DerivationResult<RecommendationDerivationOutput> VinylRecommendationHistory::derive(
    const QString &scope,const QVariantMap &snapshot,qint64 nowMs) {
    const auto profile=ProfileScopeId::parse(scope);
    if(!profile.ok())return DerivationResult<RecommendationDerivationOutput>::failure(DerivationErrorCode::ProfileMismatch,"profile");
    QVector<QualifiedListeningSession> sessions;
    if(snapshot.value("ok").toBool() && snapshot.value("enabled").toBool())for(const auto &value:snapshot.value("sessions").toList()) {
        const auto item=value.toMap();const auto recording=readRef(item.value("recording"));if(!recording)continue;
        QualifiedListeningSession session{item.value("sessionId").toString(),*recording};
        for(const auto &entity:item.value("entities").toList())if(const auto ref=readRef(entity))session.contexts.append(*ref);
        session.atMs=item.value("atMs").toLongLong();session.qualifiedMs=item.value("qualifiedMs").toLongLong();
        session.origin=item.value("origin").toString();session.outcome=item.value("outcome").toString();
        session.outcomeAtMs=item.value("outcomeAtMs").toLongLong();session.positionMs=item.value("positionMs").toLongLong();sessions.append(session);
    }
    RecommendationDerivationConfig config;config.nowMs=nowMs;config.historyComplete=false;
    return deriveQualifiedRecommendationInputs(*profile.value,sessions,config);
}
