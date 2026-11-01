#include "RecommendationCandidateContract.h"
#include "FeedbackTastePersistence.h"
#include "DeterministicRanker.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include "vinyl/VinylRecommendationIdentity.h"

using namespace vinyl;
namespace {
CanonicalEntityRef artistRef() {
    return CanonicalEntityRef::from(*ArtistMbid::parse("11111111-1111-4111-8111-111111111111").value);
}
RecommendationContribution contribution(RecommendationCanonicalContext context = {}) {
    return *RecommendationContribution::create(context,
        *RecommendationCandidateProvenance::create(*RecommendationGeneratorId::parse("fixture").value,
            RecommendationSourceFamily::Editorial).value, std::nullopt,
        {*RecommendationReasonEvidence::create(RecommendationEvidenceKind::EditorialSelection, "fixture").value}).value;
}
QVariantMap catalogueArtist(QString id = "UCPoQYATXIYvN5WB0c4f6jfQ") {
    return {{"kind","artist"},{"provider","youtube-music"},{"providerId",id},
        {"id","ytm:artist:"+id},{"name","Nas"}};
}
}
class RecommendationContracts : public QObject {
    Q_OBJECT
private slots:
    void seedAffinityIsDistinctFromFeedbackAndDeduplicatesRelations() {
        using namespace vinyl::recommendation;
        auto provider=*ProviderId::parse("youtube-music").value;
        auto ref=[&](CanonicalEntityKind kind,const QString &id){return RecommendationEntityRef::fromProvider(kind,ProviderItemRef(provider,*ProviderItemId::parse(id).value));};
        const auto item=ref(CanonicalEntityKind::Release,"release"),seed=ref(CanonicalEntityKind::Artist,"seed"),other=ref(CanonicalEntityKind::Artist,"other");
        auto candidate=*RecommendationCandidate::create(item,contribution()).value;
        auto profile=*ProfileScopeId::parse("home-test").value;TasteProfileState taste(profile);
        RecommendationDerivationOutput history(profile,kRecommendationDerivationVersion,{},{});
        DeterministicRankerConfig config;config.version="vinyl-home-v1";config.nowMs=123456;config.seedAffinityWeight=300;
        RankerFeatureContext features;features.seedAffinities={{item,seed,1.0},{item,seed,1.0},{item,other,1.0}};
        auto ranked=rankDeterministically(RecommendationCandidateSet::normalize({candidate,candidate}),taste,history,RecommendationExposureLog(profile),features,config);
        QVERIFY(ranked.ok());QCOMPARE(ranked.value->ranked().first().score(),qint64(620));
        int count=0;for(const auto &reason:ranked.value->ranked().first().explanation()){
            QVERIFY(reason.kind!=RankEvidenceKind::ExplicitPositive);
            if(reason.kind==RankEvidenceKind::SeedAffinity)++count;
        }
        QCOMPARE(count,2);QCOMPARE(taste.size(),0);
        features.seedAffinities={{item,seed,0.5},{item,seed,0.5}};
        ranked=rankDeterministically(RecommendationCandidateSet::normalize({candidate}),taste,history,RecommendationExposureLog(profile),features,config);
        QVERIFY(ranked.ok());QCOMPARE(ranked.value->ranked().first().score(),qint64(170));
    }
    void homeDiversityCapsReturnFewerItemsAndKeepDistinctVersions() {
        using namespace vinyl::recommendation;
        auto provider=*ProviderId::parse("youtube-music").value;
        auto ref=[&](CanonicalEntityKind kind,const QString &id){return RecommendationEntityRef::fromProvider(kind,ProviderItemRef(provider,*ProviderItemId::parse(id).value));};
        RecommendationCanonicalContext context;context.providerArtist=ref(CanonicalEntityKind::Artist,"artist");
        QVector<RecommendationCandidate> input;
        for(int i=0;i<5;++i)input.append(*RecommendationCandidate::create(ref(CanonicalEntityKind::Release,"version-"+QString::number(i)),contribution(context)).value);
        auto profile=*ProfileScopeId::parse("home-test").value;TasteProfileState taste(profile);
        RecommendationDerivationOutput history(profile,kRecommendationDerivationVersion,{},{});
        DeterministicRankerConfig config;config.version="vinyl-home-v1";config.nowMs=123456;config.maxPerArtist=3;
        auto ranked=rankDeterministically(RecommendationCandidateSet::normalize(input),taste,history,RecommendationExposureLog(profile),{},config);
        QVERIFY(ranked.ok());QCOMPARE(ranked.value->ranked().size(),3);
        config.maxPerArtist=0;
        ranked=rankDeterministically(RecommendationCandidateSet::normalize(input),taste,history,RecommendationExposureLog(profile),{},config);
        QVERIFY(ranked.ok());QCOMPARE(ranked.value->ranked().size(),5);
        context.releaseGroupMbid=*ReleaseGroupMbid::parse("11111111-1111-4111-8111-111111111111").value;input.clear();
        for(int i=0;i<4;++i)input.append(*RecommendationCandidate::create(ref(CanonicalEntityKind::Recording,"track-"+QString::number(i)),contribution(context)).value);
        config.maxRecordingsPerReleaseGroup=2;
        ranked=rankDeterministically(RecommendationCandidateSet::normalize(input),taste,history,RecommendationExposureLog(profile),{},config);
        QVERIFY(ranked.ok());QCOMPARE(ranked.value->ranked().size(),2);
        RankerFeatureContext features;
        for(int i=0;i<2;++i) {
            const auto release=ref(CanonicalEntityKind::Release,"album-"+QString::number(i));
            input.append(*RecommendationCandidate::create(release,contribution(context)).value);
            features.seedAffinities.append({release,ref(CanonicalEntityKind::Artist,"seed"),1.0});
        }
        config.seedAffinityWeight=1000;
        ranked=rankDeterministically(RecommendationCandidateSet::normalize(input),taste,history,RecommendationExposureLog(profile),features,config);
        QVERIFY(ranked.ok());QCOMPARE(ranked.value->ranked().size(),4);
    }
    void providerAndCanonicalNeverAlias() {
        auto id = *ProviderItemId::parse(artistRef().mbidString()).value;
        auto provider = *ProviderId::parse("youtube-music").value;
        auto p = RecommendationEntityRef::fromProvider(CanonicalEntityKind::Artist, ProviderItemRef(provider,id));
        RecommendationEntityRef canonical(artistRef());
        QVERIFY(p != canonical);
        QVERIFY(p.stableKey() != canonical.stableKey());
        auto other = RecommendationEntityRef::fromProvider(CanonicalEntityKind::Recording, ProviderItemRef(provider,id));
        QVERIFY(p != other);
        auto anotherProvider = RecommendationEntityRef::fromProvider(CanonicalEntityKind::Artist,
            ProviderItemRef(*ProviderId::parse("another-provider").value,id));
        QVERIFY(p != anotherProvider);
        QVERIFY(p.mbidString().isEmpty());
        QCOMPARE(*RecommendationEntityRef::fromJson(p.toJson()).value,p);

    }
    void generatedUuidIsNotCanonical() {
        auto item=catalogueArtist();item["uuid"]=artistRef().mbidString();
        item["canonicalMbid"]=artistRef().mbidString();item["mappingStatus"]="verified";
        auto result=VinylRecommendationIdentity::fromCatalogue(item);
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(result.value("ref").toMap().value("identity").toString(),QString("provider"));
        item["mapping"]=QVariantMap{{"status","verified"},{"kind","artist"},
            {"mbid",artistRef().mbidString()},{"provider","youtube-music"},
            {"providerId",item["providerId"]},{"method","external-id"},{"fetchedAtMs",1234}};
        result=VinylRecommendationIdentity::fromCatalogue(item);
        QCOMPARE(result.value("mappingStatus").toString(),QString("verified"));
        QCOMPARE(result.value("ref").toMap().value("identity").toString(),QString("canonical"));
        const auto verifiedKey=result["key"];
        auto equivalent=item;equivalent["providerId"]="UCwzzSiogpjsYBMoyVcwDXwQ";equivalent["id"]="ytm:artist:UCwzzSiogpjsYBMoyVcwDXwQ";
        auto equivalentMapping=equivalent["mapping"].toMap();equivalentMapping["providerId"]=equivalent["providerId"];equivalent["mapping"]=equivalentMapping;
        auto verifiedEquivalent=VinylRecommendationIdentity::fromCatalogue(equivalent);
        QCOMPARE(verifiedEquivalent["key"],verifiedKey);
        QVERIFY(verifiedEquivalent["destination"]!=result["destination"]);
        auto mapping=item["mapping"].toMap();mapping["providerId"]="wrong-artist";item["mapping"]=mapping;
        result=VinylRecommendationIdentity::fromCatalogue(item);
        QVERIFY(result.value("mappingStatus")!="verified");
        QCOMPARE(result.value("ref").toMap().value("identity").toString(),QString("provider"));

    }
    void collaboratorsRemainSeparate() {
        auto a=catalogueArtist();auto b=catalogueArtist("UCwzzSiogpjsYBMoyVcwDXwQ");b["name"]="Ilaiyaraaja";
        QVariantMap song{{"kind","song"},{"provider","youtube-music"},{"providerId","abcdefghijk"},
            {"id","ytm:track:abcdefghijk"},{"artists",QVariantList{a,b}},{"title","Collaboration"}};
        auto result=VinylRecommendationIdentity::fromCatalogue(song);
        auto artists=result["artists"].toList();QCOMPARE(artists.size(),2);
        QVERIFY(artists[0].toMap()["ref"]!=artists[1].toMap()["ref"]);
        QCOMPARE(artists[0].toMap()["destination"].toMap()["id"],a["id"]);
        QCOMPARE(artists[1].toMap()["destination"].toMap()["id"],b["id"]);
        song["artists"]=QStringList{"Slick Rick · Nas"};
        QCOMPARE(VinylRecommendationIdentity::fromCatalogue(song)["artists"].toList().size(),0);

    }
    void ambiguousVersionCannotPlay() {
        QVariantMap song{{"kind","song"},{"provider","youtube-music"},{"providerId","abcdefghijk"},
            {"id","ytm:track:abcdefghijk"},{"mapping",QVariantMap{{"status","ambiguous"}}}};
        auto result=VinylRecommendationIdentity::fromCatalogue(song);
        QVERIFY(result.value("ok").toBool());
        QVERIFY(!result["destination"].toMap()["playable"].toBool());
        QCOMPARE(result["mappingStatus"].toString(),QString("ambiguous"));

    }
    void canonicalSnapshotsMigrateLosslessly() {
        auto profile=*ProfileScopeId::parse("identity-test").value;
        TasteProfileState state(profile);QVERIFY(state.applyExplicitFeedback(artistRef(),ExplicitTasteFeedback::Positive,2,1234).ok);
        QVERIFY(state.setHidden(artistRef(),true,3,1235).ok);
        const auto bytes=*FeedbackTastePersistence::capture(state).value;
        for(int version:{1,2}) {
            auto old=QJsonDocument::fromJson(bytes).object();old["version"]=version;
            // Prior snapshots have canonical kind/mbid at the entity root.
            auto rows=old["entities"].toArray();auto row=rows[0].toObject();row.remove("identity");rows[0]=row;old["entities"]=rows;
            auto restored=FeedbackTastePersistence::restore(profile,QJsonDocument(old).toJson());
            QVERIFY(restored.ok());auto found=restored.value->find(artistRef());QVERIFY(found);
            QCOMPARE(found->explicitFeedback().revision(),quint64(2));QVERIFY(found->suppression().hidden());
            auto upgraded=*FeedbackTastePersistence::capture(*restored.value).value;
            QVERIFY(QJsonDocument::fromJson(upgraded).object()["version"].toInt()>=3);
        }
    }
    void unknownSnapshotVersionPreservesInput() {
        auto profile=*ProfileScopeId::parse("identity-test").value;
        auto object=QJsonDocument::fromJson(*FeedbackTastePersistence::capture(TasteProfileState(profile)).value).object();
        object["version"]=999;QByteArray stored=QJsonDocument(object).toJson(),before=stored;
        auto restored=FeedbackTastePersistence::restore(profile,stored);
        QVERIFY(!restored.ok());QCOMPARE(restored.error.code,TastePersistenceErrorCode::UnsupportedVersion);
        QCOMPARE(stored,before);QVERIFY(!restored.value);
    }
    void duplicateContributionsDoNotMultiplySupport() {
        auto c=*RecommendationCandidate::create(artistRef(),contribution()).value;
        auto set=RecommendationCandidateSet::normalize({c,c,c});
        QCOMPARE(set.candidates().size(),1);
        QCOMPARE(set.candidates()[0].contributions().size(),1);
    }
    void onboardingRefsAndSameNameArtistsStayDistinct() {
        auto a=catalogueArtist();a["id"]=QStringLiteral("[\"youtube-music\",\"artist\",\"UCPoQYATXIYvN5WB0c4f6jfQ\"]");
        auto first=VinylRecommendationIdentity::fromCatalogue(a);
        QVERIFY(first["ok"].toBool());
        QCOMPARE(first["destination"].toMap()["id"].toString(),QString("ytm:artist:UCPoQYATXIYvN5WB0c4f6jfQ"));
        auto b=catalogueArtist("UCwzzSiogpjsYBMoyVcwDXwQ");
        auto second=VinylRecommendationIdentity::fromCatalogue(b);
        QVERIFY(first["key"]!=second["key"]); // Same display name is never identity evidence.
        a["id"]="ytm:artist:UCwzzSiogpjsYBMoyVcwDXwQ";
        QVERIFY(!VinylRecommendationIdentity::fromCatalogue(a)["ok"].toBool());
    }
    void providerFeedbackRoundTripCannotTouchCanonicalTwin() {
        auto profile=*ProfileScopeId::parse("provider-test").value;
        auto p=RecommendationEntityRef::fromProvider(CanonicalEntityKind::Artist,
            ProviderItemRef(*ProviderId::parse("youtube-music").value,*ProviderItemId::parse(artistRef().mbidString()).value));
        TasteProfileState taste(profile);
        QVERIFY(taste.applyExplicitFeedback(p,ExplicitTasteFeedback::Negative,1,1234).ok);
        QVERIFY(taste.applyExplicitFeedback(artistRef(),ExplicitTasteFeedback::Positive,1,1234).ok);
        QVERIFY(taste.setDoNotRecommend(p,true,2,1235).ok);
        auto observation=BehavioralSignalObservation::create(*BehaviorObservationId::parse("provider-skip").value,p,
            BehavioralSignalKind::EarlySkip,1236);QVERIFY(observation.ok());
        QVERIFY(taste.recordBehavioralSignal(*observation.value).ok);
        auto snapshot=FeedbackTastePersistence::capture(taste);QVERIFY(snapshot.ok());
        auto restored=FeedbackTastePersistence::restore(profile,*snapshot.value);QVERIFY(restored.ok());
        QCOMPARE(restored.value->size(),2);
        QVERIFY(restored.value->find(p)->suppression().doNotRecommend());
        QCOMPARE(restored.value->find(p)->behavioralSignals().size(),1);
        QCOMPARE(restored.value->find(p)->behavioralSignals()[0].canonical(),p);
        QVERIFY(!restored.value->find(artistRef())->suppression().doNotRecommend());
        QCOMPARE(restored.value->find(artistRef())->explicitFeedback().feedback(),ExplicitTasteFeedback::Positive);
        QCOMPARE(*FeedbackTastePersistence::capture(*restored.value).value,*snapshot.value);
    }
    void providerContextAppliesDiversityWithoutCanonicalGraphEvidence() {
        using namespace vinyl::recommendation;
        auto provider=*ProviderId::parse("youtube-music").value;
        auto ref=[&](CanonicalEntityKind kind,const QString &id){return RecommendationEntityRef::fromProvider(kind,
            ProviderItemRef(provider,*ProviderItemId::parse(id).value));};
        RecommendationCanonicalContext context;
        context.providerArtist=ref(CanonicalEntityKind::Artist,artistRef().mbidString());
        context.providerReleaseGroup=ref(CanonicalEntityKind::ReleaseGroup,"provider-album");
        auto first=*RecommendationCandidate::create(ref(CanonicalEntityKind::Recording,"recording-a"),contribution(context)).value;
        auto second=*RecommendationCandidate::create(ref(CanonicalEntityKind::Recording,"recording-b"),contribution(context)).value;
        QVERIFY(!first.isDirectlyPlayable());
        auto profile=*ProfileScopeId::parse("rank-test").value;
        TasteProfileState taste(profile);
        RecommendationDerivationOutput history(profile,kRecommendationDerivationVersion,{},{});
        DeterministicRankerConfig config;config.version="identity-test";config.nowMs=123456;
        auto ranked=rankDeterministically(RecommendationCandidateSet::normalize({first,second}),taste,history,
            RecommendationExposureLog(profile),{},config);QVERIFY(ranked.ok());
        QCOMPARE(ranked.value->ranked().size(),2);
        QCOMPARE(ranked.value->ranked()[0].score()-ranked.value->ranked()[1].score(),
            -config.repeatedArtistPenalty-config.repeatedReleaseGroupPenalty);
        // A verified canonical artist groups editions from different provider contexts.
        context.artistMbid=*ArtistMbid::parse(artistRef().mbidString()).value;
        auto mappedFirst=*RecommendationCandidate::create(ref(CanonicalEntityKind::Recording,"mapped-a"),contribution(context)).value;
        context.providerArtist=ref(CanonicalEntityKind::Artist,"another-provider-artist");
        auto mappedSecond=*RecommendationCandidate::create(ref(CanonicalEntityKind::Recording,"mapped-b"),contribution(context)).value;
        auto mappedRank=rankDeterministically(RecommendationCandidateSet::normalize({mappedFirst,mappedSecond}),taste,history,
            RecommendationExposureLog(profile),{},config);QVERIFY(mappedRank.ok());
        QCOMPARE(mappedRank.value->ranked()[0].score()-mappedRank.value->ranked()[1].score(),
            -config.repeatedArtistPenalty-config.repeatedReleaseGroupPenalty);
        context.providerArtist=RecommendationEntityRef(artistRef());
        QVERIFY(!RecommendationContribution::create(context,
            *RecommendationCandidateProvenance::create(*RecommendationGeneratorId::parse("fixture").value,RecommendationSourceFamily::Graph).value,
            {},{*RecommendationReasonEvidence::create(RecommendationEvidenceKind::EditorialSelection,"context").value}).ok());
    }
    void recordingPlaybackRequiresExactEvidence() {
        QVariantMap song{{"kind","song"},{"provider","youtube-music"},{"providerId","abcdefghijk"}};
        QVariantMap match{{"status","verified"},{"provider","youtube-music"},{"providerId","abcdefghijk"},
            {"artistMatched",true},{"versionMatched",true},{"durationMatched",true},{"method","exact-metadata"},{"fetchedAtMs",1234}};
        song["playbackMatch"]=match;
        QVERIFY(VinylRecommendationIdentity::fromCatalogue(song)["destination"].toMap()["playable"].toBool());
        for(auto flag: {"artistMatched","versionMatched","durationMatched"}) {
            auto bad=match;bad[flag]="true";song["playbackMatch"]=bad;
            QVERIFY(!VinylRecommendationIdentity::fromCatalogue(song)["destination"].toMap()["playable"].toBool());
        }
        song["playbackMatch"]=match;song["mapping"]=QVariantMap{{"status","ambiguous"}};
        QVERIFY(!VinylRecommendationIdentity::fromCatalogue(song)["destination"].toMap()["playable"].toBool());
    }
    void typedCanonicalRecordingHonorsPlaybackGate() {
        auto canonical=CanonicalEntityRef::from(*RecordingMbid::parse(artistRef().mbidString()).value);
        RecommendationEntityRef typed(canonical);
        QVERIFY(RecommendationCandidate::create(canonical,contribution()).value->isDirectlyPlayable());
        QVERIFY(!RecommendationCandidate::create(typed,contribution()).value->isDirectlyPlayable());
        QVERIFY(!RecommendationCandidate::create(typed,contribution(),false).value->isDirectlyPlayable());
        QVERIFY(RecommendationCandidate::create(typed,contribution(),true).value->isDirectlyPlayable());
    }
    void mappedAlbumsRemainCandidatesAndCanRecordExposure() {
        using namespace vinyl::recommendation;
        QVariantMap album{{"kind","album"},{"provider","youtube-music"},{"providerId","MPRE_album"}};
        auto providerResult=VinylRecommendationIdentity::fromCatalogue(album);
        auto providerRef=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(providerResult["ref"].toMap()));
        QVERIFY(providerRef.ok());
        auto profile=*ProfileScopeId::parse("album-test").value;
        auto exposure=RecommendationExposureRecord::create(profile,*RecommendationExposureId::parse("provider-album").value,
            *providerRef.value,1234,1,"identity-test",{*ExposureEvidenceRef::create(ExposureEvidenceKind::CatalogueContext,"fixture").value});
        QVERIFY(exposure.ok());
        RecommendationExposureLog log(profile);QVERIFY(log.append(*exposure.value).ok);
        QCOMPARE(log.records()[0].entity(),*providerRef.value);
    }
    void verifiedAlbumRemainsCandidate() {
        QVariantMap album{{"kind","album"},{"provider","youtube-music"},{"providerId","MPRE_album"}};
        album["mapping"]=QVariantMap{{"status","verified"},{"provider","youtube-music"},{"providerId","MPRE_album"},
            {"kind","release"},{"mbid",artistRef().mbidString()},{"method","external-id"},{"fetchedAtMs",1234}};
        auto result=VinylRecommendationIdentity::fromCatalogue(album);
        QCOMPARE(result["mappingStatus"].toString(),QString("verified"));
        auto typed=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(result["ref"].toMap()));QVERIFY(typed.ok());
        QVERIFY(RecommendationCandidate::create(*typed.value,contribution()).ok());
        auto legacy=CanonicalEntityRef::from(*ReleaseMbid::parse(artistRef().mbidString()).value);
        QVERIFY(!RecommendationCandidate::create(legacy,contribution()).ok());
    }
};
QTEST_GUILESS_MAIN(RecommendationContracts)
#include "vinyl_recommendation_contracts.moc"
