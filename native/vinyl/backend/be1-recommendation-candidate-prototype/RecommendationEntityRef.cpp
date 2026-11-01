#include "RecommendationEntityRef.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace vinyl {
QString RecommendationEntityRef::stableKey() const {
    if (isCanonical())
        return QStringLiteral("canonical:%1:%2").arg(static_cast<int>(kind())).arg(mbidString());
    return QString::fromUtf8(QJsonDocument(QJsonArray{"provider", canonicalEntityKindName(kind()),
        providerRef()->providerId().value(), providerRef()->itemId().value()}).toJson(QJsonDocument::Compact));
}
QJsonObject RecommendationEntityRef::toJson() const {
    QJsonObject out{{"kind",canonicalEntityKindName(kind())}};
    if (isCanonical()) { out["identity"]="canonical"; out["mbid"]=mbidString(); }
    else { out["identity"]="provider";out["providerId"]=providerRef()->providerId().value();out["itemId"]=providerRef()->itemId().value(); }
    return out;
}
DomainResult<RecommendationEntityRef> RecommendationEntityRef::fromJson(const QJsonObject &object) {
    const auto fail=[] { return DomainResult<RecommendationEntityRef>::failure(
        DomainErrorCode::InvalidProviderItemId,"identity","Malformed recommendation identity."); };
    const QString identity=object.value("identity").toString(),kind=object.value("kind").toString();
    const QSet<QString> allowed=identity=="canonical" ? QSet<QString>{"identity","kind","mbid"}
        : QSet<QString>{"identity","kind","providerId","itemId"};
    for(auto it=object.begin();it!=object.end();++it)if(!allowed.contains(it.key())||!it.value().isString())return fail();
    if(identity=="canonical") {
#define PARSE_KIND(name, Type) if(kind==name){auto parsed=Type::parse(object.value("mbid").toString());if(parsed.ok())return DomainResult<RecommendationEntityRef>::success(CanonicalEntityRef::from(*parsed.value));return fail();}
        PARSE_KIND("artist",ArtistMbid)
        PARSE_KIND("release_group",ReleaseGroupMbid)
        PARSE_KIND("release",ReleaseMbid)
        PARSE_KIND("release_track",ReleaseTrackMbid)
        PARSE_KIND("recording",RecordingMbid)
        PARSE_KIND("work",WorkMbid)
#undef PARSE_KIND
        return fail();
    }
    if(identity!="provider")return fail();
    auto provider=ProviderId::parse(object.value("providerId").toString());
    auto item=ProviderItemId::parse(object.value("itemId").toString());
    if(!provider.ok()||!item.ok())return fail();
    for(auto value:{CanonicalEntityKind::Artist,CanonicalEntityKind::ReleaseGroup,CanonicalEntityKind::Release,
            CanonicalEntityKind::ReleaseTrack,CanonicalEntityKind::Recording,CanonicalEntityKind::Work})
        if(kind==canonicalEntityKindName(value))return DomainResult<RecommendationEntityRef>::success(
            fromProvider(value,ProviderItemRef(*provider.value,*item.value)));
    return fail();
}
} // namespace vinyl
