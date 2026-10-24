#include "DeterministicRanker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include <cstdlib>
#include <cstdio>
#include <limits>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustQueue(const vinyl::QueueResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPlayback(const vinyl::PlaybackSessionResult<T> &result,
               const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustListen(const vinyl::ListenLedgerResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustCandidate(const vinyl::RecommendationCandidateResult<T> &result,
                const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustDerive(const vinyl::recommendation::DerivationResult<T> &result,
             const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustDiscovery(const vinyl::discovery::DiscoveryResult<T> &result,
                const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "recording MBID should parse");
}

vinyl::ArtistMbid artist(const char *value)
{
    return mustDomain(
        vinyl::ArtistMbid::parse(QString::fromLatin1(value)),
        "artist MBID should parse");
}

vinyl::ReleaseGroupMbid releaseGroup(const char *value)
{
    return mustDomain(
        vinyl::ReleaseGroupMbid::parse(QString::fromLatin1(value)),
        "release-group MBID should parse");
}

vinyl::ProfileScopeId profile(const char *value)
{
    return mustDomain(
        vinyl::ProfileScopeId::parse(QString::fromLatin1(value)),
        "profile should parse");
}

vinyl::RecommendationGeneratorId generator(const char *value)
{
    return mustCandidate(
        vinyl::RecommendationGeneratorId::parse(QString::fromLatin1(value)),
        "generator should parse");
}

vinyl::RecommendationCandidate makeCandidate(
    vinyl::CanonicalEntityRef entity,
    const char *generatorName,
    std::optional<double> rawScore = std::nullopt,
    vinyl::RecommendationCanonicalContext canonicalContext = {})
{
    using namespace vinyl;

    const auto reason = mustCandidate(
        RecommendationReasonEvidence::create(
            RecommendationEvidenceKind::HistorySignal,
            QStringLiteral("fixture evidence")),
        "candidate evidence should construct");

    const auto provenance = mustCandidate(
        RecommendationCandidateProvenance::create(
            generator(generatorName),
            RecommendationSourceFamily::History),
        "candidate provenance should construct");

    const auto contribution = mustCandidate(
        RecommendationContribution::create(
            std::move(canonicalContext),
            provenance,
            rawScore,
            QVector<RecommendationReasonEvidence>{reason}),
        "candidate contribution should construct");

    return mustCandidate(
        RecommendationCandidate::create(
            std::move(entity),
            contribution),
        "candidate should construct");
}

vinyl::ListenContext makeListenContext(
    const vinyl::ProfileScopeId &profileId,
    const vinyl::RecordingMbid &recordingMbid,
    const QString &suffix)
{
    using namespace vinyl;

    const auto batch = mustQueue(
        EnqueueBatchId::parse(QStringLiteral("batch-") + suffix),
        "batch ID should parse");
    const EnqueueOrigin origin(EnqueueOriginKind::User, batch);

    const auto occurrence = mustQueue(
        QueueOccurrence::create(
            mustQueue(
                QueueOccurrenceId::parse(QStringLiteral("occ-") + suffix),
                "occurrence ID should parse"),
            recordingMbid,
            std::nullopt,
            origin),
        "occurrence should construct");

    PlaybackSession playback;
    const auto token = mustPlayback(
        playback.beginOccurrence(occurrence),
        "playback should begin");

    return mustListen(
        ListenContext::create(
            profileId,
            mustListen(
                ListenSessionId::parse(QStringLiteral("listen-") + suffix),
                "listen session should parse"),
            occurrence,
            token,
            mustDomain(
                PlayInstanceId::parse(QStringLiteral("play-") + suffix),
                "play instance should parse")),
        "listen context should construct");
}

vinyl::recommendation::RecommendationDerivationOutput makeHistory(
    const vinyl::ProfileScopeId &profileId,
    const vinyl::RecordingMbid &skippedRecording)
{
    using namespace vinyl;
    using namespace vinyl::recommendation;

    ListenEventLedger ledger(profileId);
    ListenEventRecorder recorder(ledger);
    const auto context = makeListenContext(
        profileId,
        skippedRecording,
        QStringLiteral("early-skip"));

    require(
        recorder.start(context, 1000000, 1000, 0, 1000).ok,
        "history start should succeed");
    require(
        recorder.skip(
            context.workToken(),
            1008000,
            9000,
            8000,
            1000).ok,
        "history early skip should succeed");

    RecommendationDerivationConfig config;
    config.nowMs = 5000000;
    config.recentWindowMs = 5000000;
    config.earlySkipPositionMs = 30000;
    config.historyComplete = true; // This fixture supplies the complete profile ledger.

    return mustDerive(
        deriveRecommendationInputs(ledger, config),
        "history derivation should succeed");
}

bool hasEvidence(
    const vinyl::recommendation::RankedRecommendation &ranked,
    vinyl::recommendation::RankEvidenceKind kind)
{
    for (const auto &evidence : ranked.explanation()) {
        if (evidence.kind == kind)
            return true;
    }
    return false;
}

const vinyl::recommendation::RankedRecommendation *findRanked(
    const vinyl::recommendation::RankerOutput &output,
    const vinyl::CanonicalEntityRef &entity)
{
    for (const auto &ranked : output.ranked()) {
        if (ranked.entity() == entity)
            return &ranked;
    }
    return nullptr;
}

bool containsEntity(
    const QVector<vinyl::RecommendationEntityRef> &entities,
    const vinyl::CanonicalEntityRef &entity)
{
    for (const auto &candidate : entities) {
        if (candidate == entity)
            return true;
    }
    return false;
}

bool sameOutput(
    const vinyl::recommendation::RankerOutput &left,
    const vinyl::recommendation::RankerOutput &right)
{
    if (left.configVersion() != right.configVersion()
        || left.suppressed() != right.suppressed()
        || left.ranked().size() != right.ranked().size()) {
        return false;
    }

    for (int i = 0; i < left.ranked().size(); ++i) {
        const auto &a = left.ranked()[i];
        const auto &b = right.ranked()[i];
        if (a.entity() != b.entity()
            || a.directlyPlayable() != b.directlyPlayable()
            || a.score() != b.score()
            || a.explanation() != b.explanation()) {
            return false;
        }
    }
    return true;
}

vinyl::recommendation::DeterministicRankerConfig baseConfig()
{
    using namespace vinyl::recommendation;

    DeterministicRankerConfig config;
    config.version = QStringLiteral("ranker-v1-fixture");
    config.nowMs = 5000000;
    config.maxResults = 20;
    config.rawScoreCalibrations = QVector<RawScoreCalibration>{
        RawScoreCalibration{
            generator("calibrated.gen"),
            100.0,
            200,
        },
    };
    return config;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;
    using namespace vinyl::recommendation;

    require(
        QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
        "ranker harness must execute against Qt 6.11.1");

    const auto profileA = profile("profile-a");

    const auto recA = recording("11111111-1111-4111-8111-111111111111");
    const auto recB = recording("22222222-2222-4222-8222-222222222222");
    const auto recC = recording("33333333-3333-4333-8333-333333333333");
    const auto recD = recording("44444444-4444-4444-8444-444444444444");
    const auto recG = recording("77777777-7777-4777-8777-777777777777");
    const auto recH = recording("88888888-8888-4888-8888-888888888888");
    const auto recI = recording("99999999-9999-4999-8999-999999999991");
    const auto recJ = recording("99999999-9999-4999-8999-999999999992");
    const auto recK = recording("99999999-9999-4999-8999-999999999993");
    const auto artistE = artist("55555555-5555-4555-8555-555555555555");
    const auto artistPositive =
        artist("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const auto artistShared =
        artist("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    const auto artistOther =
        artist("cccccccc-cccc-4ccc-8ccc-cccccccccccc");
    const auto releaseF =
        releaseGroup("66666666-6666-4666-8666-666666666666");

    const auto aRef = CanonicalEntityRef::from(recA);
    const auto bRef = CanonicalEntityRef::from(recB);
    const auto cRef = CanonicalEntityRef::from(recC);
    const auto dRef = CanonicalEntityRef::from(recD);
    const auto eRef = CanonicalEntityRef::from(artistE);
    const auto fRef = CanonicalEntityRef::from(releaseF);
    const auto gRef = CanonicalEntityRef::from(recG);
    const auto hRef = CanonicalEntityRef::from(recH);
    const auto iRef = CanonicalEntityRef::from(recI);
    const auto jRef = CanonicalEntityRef::from(recJ);
    const auto kRef = CanonicalEntityRef::from(recK);

    RecommendationCanonicalContext aContext;
    aContext.artistMbid = artistPositive;
    aContext.releaseGroupMbid = releaseF;

    const auto aCalibrated = makeCandidate(
        aRef,
        "calibrated.gen",
        0.9,
        aContext);
    const auto aUncalibrated = makeCandidate(
        aRef,
        "uncalibrated.gen",
        9999.0,
        aContext);
    const auto bCandidate = makeCandidate(
        bRef,
        "uncalibrated.gen",
        9999.0);
    const auto cCandidate = makeCandidate(cRef, "plain.gen");
    const auto dCandidate = makeCandidate(dRef, "plain.gen");
    const auto eCandidate = makeCandidate(eRef, "plain.gen");
    const auto fCandidate = makeCandidate(fRef, "plain.gen");

    const auto normalized = RecommendationCandidateSet::normalize(
        QVector<RecommendationCandidate>{
            fCandidate,
            aUncalibrated,
            dCandidate,
            bCandidate,
            eCandidate,
            aCalibrated,
            cCandidate,
        });

    TasteProfileState taste(profileA);
    require(
        taste.applyExplicitFeedback(
                 aRef,
                 ExplicitTasteFeedback::Positive,
                 1,
                 1000).ok,
        "explicit positive should apply");
    require(
        taste.applyExplicitFeedback(
                 bRef,
                 ExplicitTasteFeedback::Negative,
                 1,
                 1000).ok,
        "explicit negative should apply");
    require(
        taste.applyExplicitFeedback(
                 CanonicalEntityRef::from(artistPositive),
                 ExplicitTasteFeedback::Positive,
                 1,
                 1000).ok,
        "related artist positive should apply");
    require(
        taste.setHidden(cRef, true, 1, 1000).ok,
        "hide should apply");
    require(
        taste.setSnoozedUntil(dRef, 6000000, 1, 1000).ok,
        "snooze should apply");
    require(
        taste.setDoNotRecommend(eRef, true, 1, 1000).ok,
        "do-not-recommend should apply");

    const auto history = makeHistory(profileA, recB);

    RecommendationExposureLog emptyExposure(profileA);

    context::ContextGraph graph;
    graph.addNode(aRef);
    graph.addNode(CanonicalEntityRef::from(artistPositive));
    context::ContextRelationship relationship{
        aRef,
        CanonicalEntityRef::from(artistPositive),
        context::RelationshipKind::ArtistCredit,
        context::RelationshipDirectionality::Directed,
        {},
        QString(),
        {},
    };
    relationship.evidence.push_back(
        context::RelationshipEvidence{
            QStringLiteral("musicbrainz"),
            QStringLiteral("rel-1"),
            QStringLiteral("artist"),
            QStringLiteral("assert-1"),
        });
    graph.addRelationship(relationship);
    graph.normalize();

    const auto discoveryProvider = mustDomain(
        ProviderId::parse(QStringLiteral("discovery.fixture")),
        "discovery provider should parse");
    discovery::DiscoveryCollectionSpec discoverySpec{
        discoveryProvider,
        discovery::DiscoveryKind::Popularity,
        discovery::DiscoverySnapshot{
            QStringLiteral("snapshot-1"),
            QDateTime::fromMSecsSinceEpoch(4900000, Qt::UTC),
            discovery::DiscoveryFreshness::Fresh,
        },
        std::nullopt,
        std::nullopt,
        {},
    };
    discoverySpec.items.push_back(
        discovery::DiscoveryItem{
            aRef,
            1,
            12345.0,
            discovery::DiscoveryProvenance{
                discoveryProvider,
                QStringLiteral("pop-a"),
                QStringLiteral("fixture"),
            },
        });

    RankerFeatureContext features;
    features.contextGraph = graph;
    features.discoveryCollections.push_back(
        mustDiscovery(
            discovery::DiscoveryCollection::create(discoverySpec),
            "discovery collection should construct"));

    auto config = baseConfig();

    const auto first = rankDeterministically(
        normalized,
        taste,
        history,
        emptyExposure,
        features,
        config);
    require(first.ok(), "first deterministic ranking should succeed");
    const auto output = *first.value;

    require(
        containsEntity(output.suppressed(), cRef)
            && containsEntity(output.suppressed(), dRef)
            && containsEntity(output.suppressed(), eRef),
        "hide, snooze, and do-not-recommend must all suppress deterministically");
    require(
        findRanked(output, cRef) == nullptr
            && findRanked(output, dRef) == nullptr
            && findRanked(output, eRef) == nullptr,
        "suppressed entities must not leak into ranked results");

    const auto *rankedA = findRanked(output, aRef);
    const auto *rankedB = findRanked(output, bRef);
    const auto *rankedF = findRanked(output, fRef);
    require(
        rankedA != nullptr && rankedB != nullptr && rankedF != nullptr,
        "unsuppressed entities must remain rankable");
    require(
        rankedA->score() > rankedB->score(),
        "explicit positive must outrank explicit negative under the same policy");
    require(
        rankedA->directlyPlayable() && !rankedF->directlyPlayable(),
        "Recording playability must stay distinct from Release Group navigation");
    require(
        hasEvidence(*rankedA, RankEvidenceKind::ExplicitPositive)
            && hasEvidence(*rankedA, RankEvidenceKind::CalibratedGeneratorScore)
            && hasEvidence(*rankedA, RankEvidenceKind::CanonicalRelationship)
            && hasEvidence(*rankedA, RankEvidenceKind::DiscoveryPresence),
        "positive result explanation must trace actual taste/calibration/graph/discovery evidence");
    require(
        hasEvidence(*rankedB, RankEvidenceKind::ExplicitNegative)
            && hasEvidence(*rankedB, RankEvidenceKind::EarlySkip),
        "negative result explanation must trace explicit negative and early-skip evidence");
    require(
        !hasEvidence(*rankedB, RankEvidenceKind::CalibratedGeneratorScore),
        "uncalibrated raw scores must remain incomparable evidence");
    require(
        !hasEvidence(*rankedF, RankEvidenceKind::CalibratedGeneratorScore),
        "missing raw scores must not become fabricated neutral calibration evidence");

    const auto second = rankDeterministically(
        normalized,
        taste,
        history,
        emptyExposure,
        features,
        config);
    require(
        second.ok() && sameOutput(output, *second.value),
        "two complete ranking runs must be byte-for-byte structurally identical");

    const auto permuted = RecommendationCandidateSet::normalize(
        QVector<RecommendationCandidate>{
            cCandidate,
            aCalibrated,
            eCandidate,
            bCandidate,
            dCandidate,
            aUncalibrated,
            fCandidate,
        });
    const auto permutationRun = rankDeterministically(
        permuted,
        taste,
        history,
        emptyExposure,
        features,
        config);
    require(
        permutationRun.ok() && sameOutput(output, *permutationRun.value),
        "provider/candidate arrival-order permutation must not change final output");

    TasteProfileState neutralTaste(profileA);
    ListenEventLedger emptyLedger(profileA);
    RecommendationDerivationConfig emptyHistoryConfig;
    emptyHistoryConfig.nowMs = config.nowMs;
    const auto emptyHistory = mustDerive(
        deriveRecommendationInputs(emptyLedger, emptyHistoryConfig),
        "empty history should derive");

    const auto tieSet = RecommendationCandidateSet::normalize(
        QVector<RecommendationCandidate>{
            makeCandidate(hRef, "plain.gen"),
            makeCandidate(gRef, "plain.gen"),
        });
    const auto tieRun = rankDeterministically(
        tieSet,
        neutralTaste,
        emptyHistory,
        emptyExposure,
        RankerFeatureContext{},
        config);
    require(tieRun.ok(), "tie run should succeed");
    require(
        tieRun.value->ranked().size() == 2
            && tieRun.value->ranked()[0].entity() == gRef
            && tieRun.value->ranked()[1].entity() == hRef,
        "deterministic ties must resolve by canonical identity, not input order");

    const auto exposureEvidence = mustDerive(
        ExposureEvidenceRef::create(
            ExposureEvidenceKind::CandidateGenerator,
            QStringLiteral("generator:plain.gen")),
        "exposure evidence should construct");
    RecommendationExposureLog fatiguedExposure(profileA);
    const auto recentExposure = mustDerive(
        RecommendationExposureRecord::create(
            profileA,
            mustDerive(
                RecommendationExposureId::parse(
                    QStringLiteral("recent-g")),
                "recent exposure id should parse"),
            gRef,
            4900000,
            1,
            config.version,
            QVector<ExposureEvidenceRef>{exposureEvidence}),
        "recent exposure should construct");
    require(
        fatiguedExposure.append(recentExposure).ok,
        "recent exposure should append");

    const auto fatigueRun = rankDeterministically(
        tieSet,
        neutralTaste,
        emptyHistory,
        fatiguedExposure,
        RankerFeatureContext{},
        config);
    require(fatigueRun.ok(), "fatigue run should succeed");
    require(
        fatigueRun.value->ranked()[0].entity() == hRef,
        "recent exposure must create deterministic fatigue rather than positive taste");
    const auto *fatiguedG = findRanked(*fatigueRun.value, gRef);
    require(
        fatiguedG != nullptr
            && hasEvidence(*fatiguedG, RankEvidenceKind::ExposureFatigue),
        "fatigue explanation must cite exposure evidence");

    RankerFeatureContext diversityFeatures;
    diversityFeatures.recordings = QVector<catalogue::Recording>{
        catalogue::Recording{
            recI,
            QStringLiteral("I"),
            QVector<catalogue::ArtistCredit>{
                catalogue::ArtistCredit{artistShared, QStringLiteral("Shared")},
            },
            100000,
            {},
        },
        catalogue::Recording{
            recJ,
            QStringLiteral("J"),
            QVector<catalogue::ArtistCredit>{
                catalogue::ArtistCredit{artistShared, QStringLiteral("Shared")},
            },
            100000,
            {},
        },
        catalogue::Recording{
            recK,
            QStringLiteral("K"),
            QVector<catalogue::ArtistCredit>{
                catalogue::ArtistCredit{artistOther, QStringLiteral("Other")},
            },
            100000,
            {},
        },
    };

    const auto diversitySet = RecommendationCandidateSet::normalize(
        QVector<RecommendationCandidate>{
            makeCandidate(jRef, "plain.gen"),
            makeCandidate(kRef, "plain.gen"),
            makeCandidate(iRef, "plain.gen"),
        });
    const auto diversityRun = rankDeterministically(
        diversitySet,
        neutralTaste,
        emptyHistory,
        emptyExposure,
        diversityFeatures,
        config);
    require(diversityRun.ok(), "diversity run should succeed");
    require(
        diversityRun.value->ranked().size() == 3
            && diversityRun.value->ranked()[0].entity() == iRef
            && diversityRun.value->ranked()[1].entity() == kRef,
        "bounded artist diversity must prefer a different artist after the first tied pick");
    const auto *diverseK = findRanked(*diversityRun.value, kRef);
    require(
        diverseK != nullptr
            && !hasEvidence(
                *diverseK,
                RankEvidenceKind::DiversityArtistAdjustment),
        "diverse pick should not receive a fabricated penalty");
    const auto *penalizedJ = findRanked(*diversityRun.value, jRef);
    require(
        penalizedJ != nullptr
            && hasEvidence(
                *penalizedJ,
                RankEvidenceKind::DiversityArtistAdjustment),
        "repeated-artist pick must expose its diversity adjustment");

    auto explorationConfig = config;
    explorationConfig.playCountWeight = 0;
    explorationConfig.recentCompletionWeight = 0;
    explorationConfig.earlySkipWeight = 0;
    explorationConfig.explicitPositiveWeight = 0;
    explorationConfig.explicitNegativeWeight = 0;
    explorationConfig.relationshipWeight = 0;
    explorationConfig.discoveryWeight = 0;
    explorationConfig.exposurePenalty = 0;

    const auto explorationSet = RecommendationCandidateSet::normalize(
        QVector<RecommendationCandidate>{
            makeCandidate(bRef, "plain.gen"),
            makeCandidate(gRef, "plain.gen"),
        });
    const auto explorationRun = rankDeterministically(
        explorationSet,
        neutralTaste,
        history,
        emptyExposure,
        RankerFeatureContext{},
        explorationConfig);
    require(explorationRun.ok(), "exploration run should succeed");
    require(
        explorationRun.value->ranked()[0].entity() == gRef
            && hasEvidence(
                explorationRun.value->ranked()[0],
                RankEvidenceKind::ExplorationBonus),
        "bounded exploration bonus must apply only where unplayed evidence is actually known");

    for (const auto &ranked : output.ranked()) {
        require(
            !ranked.explanation().isEmpty(),
            "every ranked result must expose structured explanation evidence");
        for (const auto &evidence : ranked.explanation()) {
            require(
                !evidence.sourceId.trimmed().isEmpty(),
                "explanation evidence must have a traceable source id");
        }
    }

    const QString regression = app.arguments().value(1);
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-calibration")) {
        auto boundedConfig = baseConfig();
        boundedConfig.generatorSupportWeight = 0;
        boundedConfig.explorationBonus = 0;
        const auto hugeSet = RecommendationCandidateSet::normalize({makeCandidate(gRef, "calibrated.gen", 1e100)});
        const auto bounded = rankDeterministically(hugeSet, neutralTaste, emptyHistory, emptyExposure, {}, boundedConfig);
        require(bounded.ok() && bounded.value->ranked().first().score() == 200,
                "M0R positive finite extreme provider score must reach positive calibration cap");
        const auto negativeSet = RecommendationCandidateSet::normalize({makeCandidate(gRef, "calibrated.gen", -1e100)});
        const auto negative = rankDeterministically(negativeSet, neutralTaste, emptyHistory, emptyExposure, {}, boundedConfig);
        require(negative.ok() && negative.value->ranked().first().score() == -200,
                "negative finite extreme provider score must reach negative calibration cap");
        boundedConfig.rawScoreCalibrations[0].multiplier = std::numeric_limits<double>::max();
        const auto multiplied = rankDeterministically(hugeSet, neutralTaste, emptyHistory, emptyExposure, {}, boundedConfig);
        require(multiplied.ok() && multiplied.value->ranked().first().score() == 200,
                "M0R floating multiplication overflow must remain safely capped");
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-accumulation")) {
        auto overflowConfig = baseConfig();
        overflowConfig.generatorSupportWeight = std::numeric_limits<qint64>::max();
        overflowConfig.explicitPositiveWeight = std::numeric_limits<qint64>::max();
        overflowConfig.explorationBonus = 0;
        const auto overflowSet = RecommendationCandidateSet::normalize({makeCandidate(aRef, "plain.gen")});
        const auto bounded = rankDeterministically(overflowSet, taste, emptyHistory, emptyExposure, {}, overflowConfig);
        require(bounded.ok() && bounded.value->ranked().first().score() == std::numeric_limits<qint64>::max(),
                "M0R score accumulation must saturate instead of wrapping positive support negative");
        overflowConfig.generatorSupportWeight = std::numeric_limits<qint64>::min();
        overflowConfig.explicitNegativeWeight = std::numeric_limits<qint64>::min();
        const auto negativeSet = RecommendationCandidateSet::normalize({makeCandidate(bRef, "plain.gen")});
        const auto negative = rankDeterministically(negativeSet, taste, emptyHistory, emptyExposure, {}, overflowConfig);
        require(negative.ok() && negative.value->ranked().first().score() == std::numeric_limits<qint64>::min(),
                "negative score accumulation must saturate instead of wrapping");
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-products")) {
        ListenEventLedger productLedger(profileA);
        ListenEventRecorder productRecorder(productLedger);
        for (int i = 0; i < 2; ++i) {
            const auto context = makeListenContext(profileA, recG, QStringLiteral("product-%1").arg(i));
            require(productRecorder.start(context, 1000000 + i * 2000, 1000 + i * 2000, 0, 1000).ok,
                    "product fixture start must be factual");
            require(productRecorder.complete(context.workToken(), 1001000 + i * 2000, 2000 + i * 2000, 1000, 1000).ok,
                    "product fixture completion must be factual");
        }
        RecommendationDerivationConfig productDerivation; productDerivation.nowMs = config.nowMs;
        const auto productHistory = mustDerive(deriveRecommendationInputs(productLedger, productDerivation),
                                               "product fixture history must derive");
        auto productConfig = baseConfig(); productConfig.generatorSupportWeight = 0;
        productConfig.playCountWeight = std::numeric_limits<qint64>::max();
        productConfig.recentCompletionWeight = 0; productConfig.explorationBonus = 0;
        const auto set = RecommendationCandidateSet::normalize({makeCandidate(gRef, "plain.gen")});
        const auto bounded = rankDeterministically(set, neutralTaste, productHistory, emptyExposure, {}, productConfig);
        require(bounded.ok() && bounded.value->ranked().first().score() == std::numeric_limits<qint64>::max(),
                "M0R score component products must saturate instead of overflowing");
        productConfig.playCountWeight = std::numeric_limits<qint64>::min();
        const auto negative = rankDeterministically(set, neutralTaste, productHistory, emptyExposure, {}, productConfig);
        require(negative.ok() && negative.value->ranked().first().score() == std::numeric_limits<qint64>::min(),
                "negative weighted counts must saturate instead of overflowing");
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-graph")) {
        for (const bool unsupported : {true, false}) {
            context::ContextGraph untrusted;
            untrusted.addRelationship({aRef, CanonicalEntityRef::from(artistPositive),
                unsupported ? context::RelationshipKind::Unsupported : context::RelationshipKind::ArtistCredit,
                context::RelationshipDirectionality::Directed, {}, QStringLiteral("opaque"),
                unsupported ? relationship.evidence : QVector<context::RelationshipEvidence>{}});
            RankerFeatureContext weakFeatures; weakFeatures.contextGraph = untrusted;
            const auto set = RecommendationCandidateSet::normalize({makeCandidate(aRef, "plain.gen")});
            const auto ranked = rankDeterministically(set, taste, emptyHistory, emptyExposure, weakFeatures, config);
            require(ranked.ok() && !hasEvidence(ranked.value->ranked().first(), RankEvidenceKind::CanonicalRelationship),
                    "M0R unsupported or unevidenced graph edge must not create affinity");
        }
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-graph-identity")) {
        TasteProfileState identityTaste = taste;
        require(identityTaste.applyExplicitFeedback(CanonicalEntityRef::from(artistShared),
                    ExplicitTasteFeedback::Positive, 1, 1000).ok,
                "second related artist preference must be factual");
        context::ContextGraph identityGraph;
        identityGraph.addRelationship({aRef, CanonicalEntityRef::from(artistPositive),
            context::RelationshipKind::ArtistCredit, context::RelationshipDirectionality::Directed, {}, {},
            {{QStringLiteral("a:b"), QStringLiteral("item-a"), QStringLiteral("artist"), QStringLiteral("c")}}});
        identityGraph.addRelationship({aRef, CanonicalEntityRef::from(artistShared),
            context::RelationshipKind::ArtistCredit, context::RelationshipDirectionality::Directed, {}, {},
            {{QStringLiteral("a"), QStringLiteral("item-b"), QStringLiteral("artist"), QStringLiteral("b:c")}}});
        identityGraph.normalize();
        RankerFeatureContext identityFeatures; identityFeatures.contextGraph = identityGraph;
        const auto set = RecommendationCandidateSet::normalize({makeCandidate(aRef, "plain.gen")});
        const auto ranked = rankDeterministically(set, identityTaste, emptyHistory, emptyExposure, identityFeatures, config);
        require(ranked.ok(), "identity regression ranking must succeed");
        int relationshipReasons = 0;
        for (const auto &reason : ranked.value->ranked().first().explanation())
            if (reason.kind == RankEvidenceKind::CanonicalRelationship) ++relationshipReasons;
        require(relationshipReasons == 2,
                "M0R distinct graph provenance tuples must not collapse into one affinity explanation");
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-duplicates")) {
        const auto single = RecommendationCandidateSet::normalize({aCalibrated});
        const auto duplicate = RecommendationCandidateSet::normalize({aCalibrated, aCalibrated});
        const auto once = rankDeterministically(single, neutralTaste, emptyHistory, emptyExposure, {}, config);
        const auto twice = rankDeterministically(duplicate, neutralTaste, emptyHistory, emptyExposure, {}, config);
        require(once.ok() && twice.ok() && sameOutput(*once.value, *twice.value),
                "M0R duplicate generator contribution must not multiply score or explanation");
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-unknown-history")) {
        const RecommendationDerivationOutput unknownHistory(profileA, kRecommendationDerivationVersion, {}, {});
        const auto set = RecommendationCandidateSet::normalize({makeCandidate(gRef, "plain.gen")});
        const auto ranked = rankDeterministically(set, neutralTaste, unknownHistory, emptyExposure, {}, config);
        require(ranked.ok() && !hasEvidence(ranked.value->ranked().first(), RankEvidenceKind::ExplorationBonus),
                "M0R absent history coverage must not assert never-played exploration evidence");
        if (!regression.isEmpty()) return 0;
    }
    qInfo().noquote()
        << "VINYL_BE3_DETERMINISTIC_RANKER_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}
