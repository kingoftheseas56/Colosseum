#include "VinylRecommendationIdentity.h"
#include "RecommendationEntityRef.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {
using namespace vinyl;
bool positiveInteger(const QVariant &v) {
    const int type=v.metaType().id();
    if(type!=QMetaType::Int && type!=QMetaType::UInt && type!=QMetaType::LongLong
        && type!=QMetaType::ULongLong && type!=QMetaType::Double)return false;
    const double n=v.toDouble();return std::isfinite(n) && n>0 && std::floor(n)==n && n<=9007199254740991.0;
}
bool flag(const QVariantMap &map,const char *key) {
    const auto value=map.value(QLatin1String(key));return value.metaType().id()==QMetaType::Bool && value.toBool();
}
QString text(const QVariantMap &m,const char *key) {
    const auto v=m.value(QLatin1String(key));return v.metaType().id()==QMetaType::QString?v.toString():QString();
}
QVariantMap failure(const QString &message) {
    return {{"ok",false},{"ref",QVariantMap()},{"destination",QVariantMap()},
        {"artists",QVariantList()},{"mappingStatus","unavailable"},{"error",message}};
}
QVariantMap convert(const QVariantMap &item,bool includeArtists) {
    if(QJsonDocument::fromVariant(item).toJson(QJsonDocument::Compact).size()>256*1024)
        return failure("Catalogue identity is oversized.");
    QString kind=text(item,"kind"),provider=text(item,"provider"),id=text(item,"providerId");
    const QString original=text(item,"id");
    if(provider.isEmpty() && original.startsWith("ytm:"))provider="youtube-music";
    if(kind.isEmpty())kind=original.startsWith("ytm:artist:")?"artist":original.startsWith("ytm:album:")?"album":original.startsWith("ytm:track:")?"song":"";
    const CanonicalEntityKind entityKind=kind=="artist"?CanonicalEntityKind::Artist
        :kind=="album"||kind=="release"?CanonicalEntityKind::Release
        :kind=="release_group"?CanonicalEntityKind::ReleaseGroup:CanonicalEntityKind::Recording;
    if(kind!="artist" && kind!="album" && kind!="release" && kind!="release_group" && kind!="song" && kind!="recording")
        return failure("Unsupported recommendation destination kind.");
    const QString destinationKind=entityKind==CanonicalEntityKind::Artist?"artist"
        :entityKind==CanonicalEntityKind::Recording?"song":"album";
    const QString prefix=destinationKind=="artist"?"ytm:artist:":destinationKind=="song"?"ytm:track:":"ytm:album:";
    if(id.isEmpty() && provider=="youtube-music" && original.startsWith(prefix))id=original.mid(prefix.size());
    if(provider=="youtube-music" && original.startsWith("ytm:") && original!=prefix+id)
        return failure("Catalogue destination does not match its provider identity.");
    auto p=ProviderId::parse(provider);auto i=ProviderItemId::parse(id);
    if(!p.ok() || !i.ok() || p.value->value()!=provider)return failure("Missing or malformed provider identity.");
    if(provider=="youtube-music") {
        const QString pattern=destinationKind=="artist"?"^UC[A-Za-z0-9_-]{22}$":destinationKind=="song"?"^[A-Za-z0-9_-]{11}$":"^[A-Za-z0-9_-]{1,256}$";
        if(!QRegularExpression(pattern).match(id).hasMatch())return failure("Malformed YouTube Music entity ID.");
    }
    RecommendationEntityRef ref=RecommendationEntityRef::fromProvider(entityKind,ProviderItemRef(*p.value,*i.value));
    const auto mapping=item.value("mapping").toMap();QString status=text(mapping,"status");
    if(status.isEmpty())status="unresolved";
    if(!QSet<QString>{"verified","unresolved","ambiguous","unavailable"}.contains(status))status="unresolved";
    if(status=="verified") {
        const QString method=text(mapping,"method");
        const bool bound=text(mapping,"provider")==provider && text(mapping,"providerId")==id
            && text(mapping,"kind")==canonicalEntityKindName(entityKind)
            && QSet<QString>{"external-id","corroborated-metadata"}.contains(method) && positiveInteger(mapping.value("fetchedAtMs"));
        const bool recordingMatch=entityKind!=CanonicalEntityKind::Recording
            || (flag(mapping,"artistMatched") && flag(mapping,"versionMatched")
                && flag(mapping,"durationMatched"));
        auto canonical=RecommendationEntityRef::fromJson(QJsonObject{{"identity","canonical"},
            {"kind",canonicalEntityKindName(entityKind)},{"mbid",text(mapping,"mbid")}});
        if(bound && recordingMatch && canonical.ok())ref=*canonical.value;
        else status="unresolved";
    }
    const auto match=item.value("playbackMatch").toMap();
    const bool playable=entityKind==CanonicalEntityKind::Recording && status!="ambiguous" && status!="unavailable"
        && text(match,"status")=="verified" && text(match,"provider")==provider && text(match,"providerId")==id
        && flag(match,"artistMatched") && flag(match,"versionMatched")
        && flag(match,"durationMatched") && !text(match,"method").trimmed().isEmpty()
        && positiveInteger(match.value("fetchedAtMs"));
    QVariantMap destination{{"kind",destinationKind},{"provider",provider},{"providerId",id},
        {"id",provider=="youtube-music"?prefix+id:(original.isEmpty()?id:original)},{"playable",playable}};
    QVariantList artists;QSet<QString> seen;
    if(includeArtists) {
        const auto raw=item.value("artistRefs",item.value("artists"));
        if(raw.metaType().id()==QMetaType::QVariantList) {
            const auto values=raw.toList();if(values.size()>64)return failure("Too many artist identities.");
            for(const auto &value:values) {
                if(value.metaType().id()!=QMetaType::QVariantMap)continue;
                auto artist=value.toMap();artist["kind"]="artist";
                auto converted=convert(artist,false);if(!converted.value("ok").toBool())continue;
                const auto key=converted.value("key").toString();if(seen.contains(key))continue;
                seen.insert(key);converted["name"]=artist.value("name",artist.value("title"));artists.append(converted);
            }
        }
    }
    return {{"ok",true},{"ref",ref.toJson().toVariantMap()},{"key",ref.stableKey()},
        {"destination",destination},{"artists",artists},{"mappingStatus",status},{"error",""}};
}
}
QVariantMap VinylRecommendationIdentity::fromCatalogue(const QVariantMap &item) {
    return convert(item,true);
}
