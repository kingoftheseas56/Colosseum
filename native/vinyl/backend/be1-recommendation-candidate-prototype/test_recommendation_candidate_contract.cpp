#include "RecommendationCandidateContract.h"

#include <QCoreApplication>
#include <QDebug>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;

    qCritical().noquote() << "FAIL:" << message;
    std::exit(1);
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustRecommendation(
    const vinyl::RecommendationCandidateResult<T> &result,
    const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T, typename = void>
struct HasFinalVinylScore : std::false_type {
};

template <typename T>
struct HasFinalVinylScore<
    T,
    std::void_t<decltype(std::declval<const T &>().finalVinylScore())>>
    : std::true_type {
};

template <typename T, typename = void>
struct HasFinalRank : std::false_type {
};

template <typename T>
struct HasFinalRank<
    T,
    std::void_t<decltype(std::declval<const T &>().finalRank())>>
    : std::true_type {
};

vinyl::RecommendationCandidateProvenance makeProvenance(
    const QString &generatorId,
    vinyl::RecommendationSourceFamily family,
    std::optional<vinyl::ProviderId> providerId = std::nullopt,
    std::optional<vinyl::ProviderItemId> providerCandidateId = std::nullopt,
    std::optional<QString> providerModelId = std::nullopt,
    std::optional<vinyl::RecommendationSeed> seed = std::nullopt)
{
    using namespace vinyl;

    return mustRecommendation(
        RecommendationCandidateProvenance::create(
            mustRecommendation(
                RecommendationGeneratorId::parse(generatorId),
                "generator id should parse"),
            family,
            std::move(providerId),
            std::move(providerCandidateId),
            std::move(providerModelId),
            std::move(seed)),
        "provenance should be valid");
}

vinyl::RecommendationContribution makeContribution(
    vinyl::RecommendationCandidateProvenance provenance,
    std::optional<double> score,
    vinyl::RecommendationReasonEvidence evidence,
    vinyl::RecommendationCanonicalContext context = {})
{
    using namespace vinyl;

    return mustRecommendation(
        RecommendationContribution::create(
            std::move(context),
            std::move(provenance),
            score,
            QVector<RecommendationReasonEvidence>{std::move(evidence)}),
        "contribution should be valid");
}

vinyl::RecommendationCandidate makeCandidate(
    vinyl::CanonicalEntityRef canonical,
    vinyl::RecommendationContribution contribution)
{
    return mustRecommendation(
        vinyl::RecommendationCandidate::create(
            std::move(canonical),
            std::move(contribution)),
        "candidate should be valid");
}

vinyl::RecommendationCandidate makeCandidate(
    const vinyl::RecordingMbid &recording,
    vinyl::RecommendationContribution contribution)
{
    return makeCandidate(
        vinyl::CanonicalEntityRef::from(recording),
        std::move(contribution));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    static_assert(
        !std::is_same<RecommendationGeneratorId, ProviderId>::value,
        "recommendation generator identity must remain lane-specific");
    static_assert(
        !std::is_convertible<ProviderId, RecommendationGeneratorId>::value,
        "provider identity must not silently become generator identity");
    static_assert(
        !HasFinalVinylScore<RecommendationCandidate>::value,
        "BE1 must not expose a final Vinyl score");
    static_assert(
        !HasFinalRank<RecommendationCandidate>::value,
        "BE1 must not expose final Vinyl rank");

    require(
        QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
        "BE1 harness must execute against Qt 6.11.1");

    const auto targetRecording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "target Recording MBID should parse");
    const auto secondRecording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "second Recording MBID should parse");
    const auto seedRecording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "seed Recording MBID should parse");
    const auto artist = mustDomain(
        ArtistMbid::parse(
            QStringLiteral("44444444-4444-4444-8444-444444444444")),
        "Artist MBID should parse");
    const auto releaseGroup = mustDomain(
        ReleaseGroupMbid::parse(
            QStringLiteral("55555555-5555-4555-8555-555555555555")),
        "Release Group MBID should parse");
    const auto concreteRelease = mustDomain(
        ReleaseMbid::parse(
            QStringLiteral("66666666-6666-4666-8666-666666666666")),
        "Release MBID should parse");
    const auto work = mustDomain(
        WorkMbid::parse(
            QStringLiteral("77777777-7777-4777-8777-777777777777")),
        "Work MBID should parse");
    const auto sameUuidArtist = mustDomain(
        ArtistMbid::parse(targetRecording.toString()),
        "same UUID under Artist kind should parse");

    const auto listenBrainzProvider = mustDomain(
        ProviderId::parse(QStringLiteral("ListenBrainz")),
        "provider id should parse");
    const auto collaborativeCandidateId = mustDomain(
        ProviderItemId::parse(QStringLiteral("lb-candidate-42")),
        "provider candidate id should parse");

    const auto recordingSeed = mustRecommendation(
        RecommendationSeed::create(
            CanonicalEntityRef::from(seedRecording),
            QStringLiteral("similar_recording")),
        "recording seed should be valid");

    const auto collaborativeEvidence = mustRecommendation(
        RecommendationReasonEvidence::create(
            RecommendationEvidenceKind::CollaborativePrior,
            QStringLiteral("Population similarity to seed recording"),
            CanonicalEntityRef::from(seedRecording)),
        "collaborative evidence should be valid");

    RecommendationCanonicalContext collaborativeContext;
    collaborativeContext.artistMbid = artist;
    collaborativeContext.releaseGroupMbid = releaseGroup;

    auto collaborativeContribution = makeContribution(
        makeProvenance(
            QStringLiteral("ListenBrainz.CF"),
            RecommendationSourceFamily::Collaborative,
            listenBrainzProvider,
            collaborativeCandidateId,
            QStringLiteral("als-2026-09"),
            recordingSeed),
        0.91,
        collaborativeEvidence,
        collaborativeContext);

    const auto graphEvidence = mustRecommendation(
        RecommendationReasonEvidence::create(
            RecommendationEvidenceKind::CanonicalRelationship,
            QStringLiteral("Produced by canonical related artist"),
            CanonicalEntityRef::from(artist)),
        "graph evidence should be valid");

    const auto artistSeed = mustRecommendation(
        RecommendationSeed::create(
            CanonicalEntityRef::from(artist),
            QStringLiteral("artist_graph")),
        "artist seed should be valid");

    auto graphContribution = makeContribution(
        makeProvenance(
            QStringLiteral("Vinyl.Graph"),
            RecommendationSourceFamily::Graph,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            artistSeed),
        std::nullopt,
        graphEvidence);

    const auto collaborativeCandidate = makeCandidate(
        targetRecording,
        collaborativeContribution);
    const auto graphCandidate = makeCandidate(
        targetRecording,
        graphContribution);

    const auto releaseGroupCandidate = makeCandidate(
        CanonicalEntityRef::from(releaseGroup),
        graphContribution);
    const auto artistCandidate = makeCandidate(
        CanonicalEntityRef::from(artist),
        graphContribution);
    require(
        collaborativeCandidate.canonicalEntity().kind()
                == CanonicalEntityKind::Recording
            && collaborativeCandidate.isDirectlyPlayable(),
        "Recording candidate must remain the only directly playable kind");
    require(
        releaseGroupCandidate.canonicalEntity().kind()
                == CanonicalEntityKind::ReleaseGroup
            && !releaseGroupCandidate.isDirectlyPlayable(),
        "Release Group candidate must be navigational, not directly playable");
    require(
        artistCandidate.canonicalEntity().kind() == CanonicalEntityKind::Artist
            && !artistCandidate.isDirectlyPlayable(),
        "Artist candidate must be navigational, not directly playable");

    const auto invalidReleaseCandidate = RecommendationCandidate::create(
        CanonicalEntityRef::from(concreteRelease),
        graphContribution);
    const auto invalidWorkCandidate = RecommendationCandidate::create(
        CanonicalEntityRef::from(work),
        graphContribution);
    require(
        !invalidReleaseCandidate.ok()
            && invalidReleaseCandidate.error.code
                == RecommendationCandidateErrorCode::InvalidCanonicalKind,
        "Release recommendation candidate must fail closed");
    require(
        !invalidWorkCandidate.ok()
            && invalidWorkCandidate.error.code
                == RecommendationCandidateErrorCode::InvalidCanonicalKind,
        "Work recommendation candidate must fail closed");

    const auto crossKindCandidate = makeCandidate(
        CanonicalEntityRef::from(sameUuidArtist),
        graphContribution);
    const auto crossKindSet = RecommendationCandidateSet::normalize(
        QVector<RecommendationCandidate>{
            collaborativeCandidate,
            crossKindCandidate,
        });
    require(
        crossKindSet.candidates().size() == 2,
        "same UUID under different canonical kinds must not deduplicate");

    const auto deduped = RecommendationCandidateSet::normalize(
        QVector<RecommendationCandidate>{
            collaborativeCandidate,
            graphCandidate,
        });

    require(
        deduped.candidates().size() == 1,
        "same Recording from multiple generators must deduplicate canonically");
    require(
        deduped.candidates().front().contributions().size() == 2,
        "canonical dedupe must preserve both generator contributions");

    const auto &mergedContributions =
        deduped.candidates().front().contributions();

    require(
        mergedContributions[0].provenance().sourceFamily()
            == RecommendationSourceFamily::Collaborative,
        "collaborative provenance must survive dedupe");
    require(
        mergedContributions[1].provenance().sourceFamily()
            == RecommendationSourceFamily::Graph,
        "graph provenance must survive dedupe");

    require(
        mergedContributions[0].evidence().front().detail()
            == QStringLiteral("Population similarity to seed recording"),
        "explanation evidence must survive normalization");
    require(
        mergedContributions[0]
                .evidence()
                .front()
                .relatedCanonicalEntity()
                .has_value(),
        "related canonical evidence must survive normalization");
    require(
        mergedContributions[0]
                .evidence()
                .front()
                .relatedCanonicalEntity()
                ->mbidString()
            == seedRecording.toString(),
        "evidence relation must retain the seed Recording MBID");

    require(
        !mergedContributions[1].rawProviderScore().has_value(),
        "missing optional score must stay unknown");
    require(
        mergedContributions[0].rawProviderScore().has_value()
            && *mergedContributions[0].rawProviderScore() == 0.91,
        "provider score must survive only as raw evidence");

    require(
        mergedContributions[0].provenance().seed().has_value(),
        "seed provenance must remain explicit");
    require(
        mergedContributions[0]
                .provenance()
                .seed()
                ->entity()
                .mbidString()
            == seedRecording.toString(),
        "seed provenance must preserve canonical seed identity");
    require(
        mergedContributions[0].provenance().seed()->context()
            == QStringLiteral("similar_recording"),
        "seed provenance must preserve seed context");

    const auto lowScoreEvidence = mustRecommendation(
        RecommendationReasonEvidence::create(
            RecommendationEvidenceKind::EditorialSelection,
            QStringLiteral("same observed title: Shared Title")),
        "low-score evidence should be valid");
    const auto highScoreEvidence = mustRecommendation(
        RecommendationReasonEvidence::create(
            RecommendationEvidenceKind::EditorialSelection,
            QStringLiteral("same observed title: Shared Title")),
        "high-score evidence should be valid");

    const auto lowScoreCandidate = makeCandidate(
        targetRecording,
        makeContribution(
            makeProvenance(
                QStringLiteral("editorial.one"),
                RecommendationSourceFamily::Editorial),
            0.01,
            lowScoreEvidence));

    const auto highScoreCandidate = makeCandidate(
        secondRecording,
        makeContribution(
            makeProvenance(
                QStringLiteral("editorial.two"),
                RecommendationSourceFamily::Editorial),
            9999.0,
            highScoreEvidence));

    const auto scoreOrder = RecommendationCandidateSet::normalize(
        QVector<RecommendationCandidate>{
            lowScoreCandidate,
            highScoreCandidate,
        });

    require(
        scoreOrder.candidates().size() == 2,
        "different Recording MBIDs must remain different candidates");
    require(
        scoreOrder.candidates()[0].canonicalEntity()
                == CanonicalEntityRef::from(targetRecording)
            && scoreOrder.candidates()[1].canonicalEntity()
                == CanonicalEntityRef::from(secondRecording),
        "raw provider scores must not reorder candidates into a Vinyl rank");
    require(
        scoreOrder.candidates()[0]
                .contributions()
                .front()
                .evidence()
                .front()
                .detail()
            == scoreOrder.candidates()[1]
                   .contributions()
                   .front()
                   .evidence()
                   .front()
                   .detail(),
        "fixture must actually use matching noncanonical title evidence");
    require(
        scoreOrder.candidates()[0].canonicalEntity()
            != scoreOrder.candidates()[1].canonicalEntity(),
        "matching title evidence must never merge different Recording MBIDs");

    const auto sharedProviderCandidateId = mustDomain(
        ProviderItemId::parse(QStringLiteral("shared-provider-candidate")),
        "shared provider candidate id should parse");

    const auto sharedProviderEvidence = mustRecommendation(
        RecommendationReasonEvidence::create(
            RecommendationEvidenceKind::CollaborativePrior,
            QStringLiteral("shared provider candidate fixture")),
        "shared-provider evidence should be valid");

    const auto providerIdentityA = makeCandidate(
        targetRecording,
        makeContribution(
            makeProvenance(
                QStringLiteral("provider.generator.a"),
                RecommendationSourceFamily::Collaborative,
                listenBrainzProvider,
                sharedProviderCandidateId),
            0.5,
            sharedProviderEvidence));

    const auto providerIdentityB = makeCandidate(
        secondRecording,
        makeContribution(
            makeProvenance(
                QStringLiteral("provider.generator.b"),
                RecommendationSourceFamily::Collaborative,
                listenBrainzProvider,
                sharedProviderCandidateId),
            0.5,
            sharedProviderEvidence));

    const auto providerIdentitySet = RecommendationCandidateSet::normalize(
        QVector<RecommendationCandidate>{
            providerIdentityA,
            providerIdentityB,
        });

    require(
        providerIdentitySet.candidates().size() == 2,
        "provider candidate id must never replace Recording MBID");
    require(
        providerIdentitySet.candidates()[0]
                .contributions()
                .front()
                .provenance()
                .providerCandidateId()
                .has_value()
            && providerIdentitySet.candidates()[1]
                   .contributions()
                   .front()
                   .provenance()
                   .providerCandidateId()
                   .has_value(),
        "provider candidate identity should remain provenance");

    const auto badGenerator =
        RecommendationGeneratorId::parse(QStringLiteral("bad generator!"));
    require(
        !badGenerator.ok()
            && badGenerator.error.code
                == RecommendationCandidateErrorCode::InvalidGeneratorId,
        "invalid generator identity should be a typed BE1 failure");

    const auto generator = mustRecommendation(
        RecommendationGeneratorId::parse(QStringLiteral("valid.generator")),
        "valid generator should parse");

    const auto missingProvider = RecommendationCandidateProvenance::create(
        generator,
        RecommendationSourceFamily::Collaborative,
        std::nullopt,
        sharedProviderCandidateId);
    require(
        !missingProvider.ok()
            && missingProvider.error.code
                == RecommendationCandidateErrorCode::
                    InvalidProviderProvenance,
        "provider candidate id without provider must fail typed validation");

    const auto blankEvidence = RecommendationReasonEvidence::create(
        RecommendationEvidenceKind::EditorialSelection,
        QStringLiteral("   "));
    require(
        !blankEvidence.ok()
            && blankEvidence.error.code
                == RecommendationCandidateErrorCode::InvalidEvidence,
        "blank evidence must fail typed validation");

    const auto blankSeed = RecommendationSeed::create(
        CanonicalEntityRef::from(seedRecording),
        QStringLiteral("  "));
    require(
        !blankSeed.ok()
            && blankSeed.error.code
                == RecommendationCandidateErrorCode::InvalidSeedContext,
        "blank seed context must fail typed validation");

    const auto validEvidence = mustRecommendation(
        RecommendationReasonEvidence::create(
            RecommendationEvidenceKind::HistorySignal,
            QStringLiteral("fixture history signal")),
        "history evidence should be valid");

    const auto validProvenance = makeProvenance(
        QStringLiteral("history.generator"),
        RecommendationSourceFamily::History);

    const auto noEvidence = RecommendationContribution::create(
        {},
        validProvenance,
        std::nullopt,
        {});
    require(
        !noEvidence.ok()
            && noEvidence.error.code
                == RecommendationCandidateErrorCode::MissingEvidence,
        "candidate contribution without evidence must fail typed validation");

    const auto nanScore = RecommendationContribution::create(
        {},
        validProvenance,
        std::numeric_limits<double>::quiet_NaN(),
        QVector<RecommendationReasonEvidence>{validEvidence});
    require(
        !nanScore.ok()
            && nanScore.error.code
                == RecommendationCandidateErrorCode::InvalidProviderScore,
        "non-finite provider score must fail typed validation");

    require(
        recommendationCandidateErrorCodeName(
            RecommendationCandidateErrorCode::InvalidProviderScore)
            == QStringLiteral("invalid_provider_score"),
        "BE1 errors should expose stable snake_case names");
    require(
        recommendationSourceFamilyName(
            RecommendationSourceFamily::Collaborative)
            == QStringLiteral("collaborative"),
        "source families should expose stable names");
    require(
        recommendationEvidenceKindName(
            RecommendationEvidenceKind::CanonicalRelationship)
            == QStringLiteral("canonical_relationship"),
        "evidence kinds should expose stable names");

    qInfo().noquote()
        << "VINYL_BE1_RECOMMENDATION_CANDIDATE_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}
