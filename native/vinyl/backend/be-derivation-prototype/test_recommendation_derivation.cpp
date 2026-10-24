#include "RecommendationDerivation.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <optional>
#include <stdexcept>

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
T mustDerive(const vinyl::recommendation::DerivationResult<T> &result,
             const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "fixture Recording MBID should parse");
}

vinyl::ProfileScopeId profile(const char *value)
{
    return mustDomain(
        vinyl::ProfileScopeId::parse(QString::fromLatin1(value)),
        "fixture profile should parse");
}

vinyl::QueueOccurrenceId occurrenceId(const QString &value)
{
    return mustQueue(
        vinyl::QueueOccurrenceId::parse(value),
        "fixture occurrence ID should parse");
}

vinyl::EnqueueBatchId batchId(const QString &value)
{
    return mustQueue(
        vinyl::EnqueueBatchId::parse(value),
        "fixture enqueue batch should parse");
}

vinyl::ListenSessionId listenSession(const QString &value)
{
    return mustListen(
        vinyl::ListenSessionId::parse(value),
        "fixture listen session should parse");
}

vinyl::PlayInstanceId playInstance(const QString &value)
{
    return mustDomain(
        vinyl::PlayInstanceId::parse(value),
        "fixture play instance should parse");
}

vinyl::ListenContext makeContext(
    const vinyl::ProfileScopeId &profileId,
    const vinyl::RecordingMbid &recordingMbid,
    vinyl::EnqueueOriginKind originKind,
    const QString &suffix)
{
    using namespace vinyl;

    const EnqueueOrigin origin(
        originKind,
        batchId(QStringLiteral("batch-") + suffix));

    const auto occurrence = mustQueue(
        QueueOccurrence::create(
            occurrenceId(QStringLiteral("occ-") + suffix),
            recordingMbid,
            std::nullopt,
            origin),
        "queue occurrence should construct");

    PlaybackSession playback;
    const auto token = mustPlayback(
        playback.beginOccurrence(occurrence),
        "playback occurrence should begin");

    return mustListen(
        ListenContext::create(
            profileId,
            listenSession(QStringLiteral("listen-") + suffix),
            occurrence,
            token,
            playInstance(QStringLiteral("play-") + suffix)),
        "listen context should construct");
}

int countSignal(
    const QVector<vinyl::BehavioralSignalObservation> &observations,
    vinyl::BehavioralSignalKind kind)
{
    int count = 0;
    for (const auto &observation : observations) {
        if (observation.kind() == kind)
            ++count;
    }
    return count;
}

void requireStatus(const vinyl::ListenLedgerMutationStatus &status,
                   const char *message)
{
    require(status.ok, message);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;
    using namespace vinyl::recommendation;

    require(
        QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
        "derivation harness must execute against Qt 6.11.1");

    const auto profileA = profile("profile-a");
    const auto profileB = profile("profile-b");
    const auto recordingA =
        recording("11111111-1111-4111-8111-111111111111");
    const auto canonicalA = CanonicalEntityRef::from(recordingA);

    ListenEventLedger ledger(profileA);
    ListenEventRecorder recorder(ledger);

    const auto deliberateOne = makeContext(
        profileA,
        recordingA,
        EnqueueOriginKind::User,
        QStringLiteral("user-1"));
    requireStatus(
        recorder.start(
            deliberateOne,
            1000000,
            1000,
            0,
            1000),
        "first deliberate play should start");
    requireStatus(
        recorder.complete(
            deliberateOne.workToken(),
            1180000,
            181000,
            180000,
            1000),
        "first deliberate play should complete");

    const auto passive = makeContext(
        profileA,
        recordingA,
        EnqueueOriginKind::Radio,
        QStringLiteral("radio-1"));
    requireStatus(
        recorder.start(
            passive,
            2000000,
            200000,
            0,
            1000),
        "passive play should start");
    requireStatus(
        recorder.complete(
            passive.workToken(),
            2180000,
            380000,
            180000,
            1000),
        "passive play should complete");

    const auto earlySkip = makeContext(
        profileA,
        recordingA,
        EnqueueOriginKind::User,
        QStringLiteral("user-skip"));
    requireStatus(
        recorder.start(
            earlySkip,
            3000000,
            400000,
            0,
            1000),
        "early-skip play should start");
    requireStatus(
        recorder.skip(
            earlySkip.workToken(),
            3008000,
            408000,
            8000,
            1000),
        "early-skip play should skip");

    const auto deliberateReplay = makeContext(
        profileA,
        recordingA,
        EnqueueOriginKind::User,
        QStringLiteral("user-2"));
    requireStatus(
        recorder.start(
            deliberateReplay,
            4000000,
            500000,
            0,
            1000),
        "second deliberate play should start");
    requireStatus(
        recorder.complete(
            deliberateReplay.workToken(),
            4180000,
            680000,
            180000,
            1000),
        "second deliberate play should complete");

    RecommendationDerivationConfig config;
    config.nowMs = 5000000;
    config.recentWindowMs = 4500000;
    config.earlySkipPositionMs = 30000;

    const auto derived = mustDerive(
        deriveRecommendationInputs(ledger, config),
        "history derivation should succeed");

    require(
        derived.version() == kRecommendationDerivationVersion,
        "derivation version must be explicit");
    require(
        derived.profileId() == profileA,
        "derivation must preserve profile scope");

    const auto *aggregate = derived.findAggregate(canonicalA);
    require(aggregate != nullptr, "recording aggregate must exist");
    require(aggregate->playCount() == 4, "play count must use factual starts");
    require(
        aggregate->lastPlayedAtMs().has_value()
            && *aggregate->lastPlayedAtMs() == 4000000,
        "last played must use the last factual non-continuation start");
    require(
        aggregate->recentCompletionCount() == 3,
        "recent completion count must be deterministic");
    require(
        aggregate->recentSkipCount() == 1,
        "recent skip count must be deterministic");
    require(
        aggregate->naturalCompletionCount() == 2,
        "deliberate completion must remain natural-completion evidence");
    require(
        aggregate->passiveAutoplayCompletionCount() == 1,
        "radio completion must remain passive-autoplay evidence");
    require(
        aggregate->earlySkipCount() == 1,
        "early skip must remain explicit negative evidence");

    require(
        countSignal(
            derived.observations(),
            BehavioralSignalKind::DeliberateQueue) == 3,
        "user enqueue origin must derive deliberate_queue");
    require(
        countSignal(
            derived.observations(),
            BehavioralSignalKind::PassiveAutoplayCompletion) == 1,
        "radio origin must derive passive autoplay completion");
    require(
        countSignal(
            derived.observations(),
            BehavioralSignalKind::NaturalCompletion) == 2,
        "user completions must derive natural completion");
    require(
        countSignal(
            derived.observations(),
            BehavioralSignalKind::EarlySkip) == 1,
        "early skip must derive its own signal");
    require(
        countSignal(
            derived.observations(),
            BehavioralSignalKind::Replay) == 2,
        "repeat deliberate starts after the first must derive replay");

    require(
        countSignal(
            derived.observations(),
            BehavioralSignalKind::AlbumCompletion) == 0,
        "album completion must not be fabricated without album-completeness evidence");
    require(
        countSignal(
            derived.observations(),
            BehavioralSignalKind::QueueRemoval) == 0,
        "queue removal must not be fabricated from BA4 absence");
    require(
        countSignal(
            derived.observations(),
            BehavioralSignalKind::RecommendationAvoidance) == 0,
        "recommendation avoidance must not be fabricated from non-listening");

    ListenEventLedger localLedger(profileA);
    ListenEventRecorder localRecorder(localLedger);
    const QueueOccurrence unmappedLocalOccurrence = mustQueue(
        QueueOccurrence::createLocal(
            occurrenceId(QStringLiteral("occ-local-unmapped")),
            QStringLiteral("vault:recommendation-unmapped"),
            std::nullopt,
            std::nullopt,
            EnqueueOrigin(
                EnqueueOriginKind::User,
                batchId(QStringLiteral("batch-local-unmapped")))),
        "unmapped local recommendation occurrence should construct");
    PlaybackSession unmappedLocalPlayback;
    const auto unmappedLocalToken = mustPlayback(
        unmappedLocalPlayback.beginOccurrence(unmappedLocalOccurrence),
        "unmapped local recommendation playback should begin");
    const auto unmappedLocalContext = mustListen(
        ListenContext::create(
            profileA,
            listenSession(QStringLiteral("session-local-unmapped")),
            unmappedLocalOccurrence,
            unmappedLocalToken,
            playInstance(QStringLiteral("play-local-unmapped"))),
        "unmapped local recommendation context should construct");
    requireStatus(
        localRecorder.start(
            unmappedLocalContext, 4600000, 720000, 0, 1000),
        "unmapped local listen fact should remain in the transient ledger");

    const QueueOccurrence mappedLocalOccurrence = mustQueue(
        QueueOccurrence::createLocal(
            occurrenceId(QStringLiteral("occ-local-mapped")),
            QStringLiteral("vault:recommendation-mapped"),
            std::optional<RecordingMbid>{recordingA},
            std::nullopt,
            EnqueueOrigin(
                EnqueueOriginKind::User,
                batchId(QStringLiteral("batch-local-mapped")))),
        "verified mapped local recommendation occurrence should construct");
    PlaybackSession mappedLocalPlayback;
    const auto mappedLocalToken = mustPlayback(
        mappedLocalPlayback.beginOccurrence(mappedLocalOccurrence),
        "mapped local recommendation playback should begin");
    const auto mappedLocalContext = mustListen(
        ListenContext::create(
            profileA,
            listenSession(QStringLiteral("session-local-mapped")),
            mappedLocalOccurrence,
            mappedLocalToken,
            playInstance(QStringLiteral("play-local-mapped"))),
        "mapped local recommendation context should construct");
    requireStatus(
        localRecorder.start(
            mappedLocalContext, 4610000, 721000, 0, 1000),
        "mapped local listen fact should remain in the transient ledger");

    std::optional<DerivationResult<RecommendationDerivationOutput>> localDerived;
    bool localDerivationThrew = false;
    try {
        localDerived.emplace(deriveRecommendationInputs(localLedger, config));
    } catch (const std::bad_optional_access &) {
        localDerivationThrew = true;
    }
    require(!localDerivationThrew,
            "recommendation derivation must skip an unmapped local event instead of throwing");
    require(localDerived.has_value() && localDerived->ok(),
            "unmapped local history should not fail the rest of recommendation derivation");
    const RecommendationDerivationOutput &localOutput = *localDerived->value;
    require(localOutput.aggregates().size() == 1,
            "only the mapped local event should contribute canonical affinity");
    const auto *mappedLocalAggregate = localOutput.findAggregate(canonicalA);
    require(mappedLocalAggregate != nullptr
                && mappedLocalAggregate->playCount() == 1,
            "verified local-to-canonical mapping should contribute to its canonical aggregate");
    require(countSignal(
                localOutput.observations(),
                BehavioralSignalKind::DeliberateQueue) == 1,
            "unmapped local history must not fabricate canonical recommendation signals");

    ListenEventLedger failureLedger(profileA);
    ListenEventRecorder failureRecorder(failureLedger);
    const auto failedPlay = makeContext(
        profileA,
        recordingA,
        EnqueueOriginKind::User,
        QStringLiteral("user-failure"));
    requireStatus(
        failureRecorder.start(
            failedPlay,
            4500000,
            700000,
            0,
            1000),
        "failure fixture should start factually");
    requireStatus(
        failureRecorder.failure(
            failedPlay.workToken(),
            4510000,
            710000,
            10000,
            1000),
        "failure fixture should record a distinct terminal failure");

    const auto failureDerived = mustDerive(
        deriveRecommendationInputs(failureLedger, config),
        "failure-only factual history should derive without inventing taste");
    const auto *failureAggregate =
        failureDerived.findAggregate(canonicalA);
    require(failureAggregate != nullptr
                && failureAggregate->playCount() == 1,
            "failure history should preserve the factual start");
    require(failureAggregate->recentCompletionCount() == 0
                && failureAggregate->recentSkipCount() == 0
                && failureAggregate->naturalCompletionCount() == 0
                && failureAggregate->passiveAutoplayCompletionCount() == 0
                && failureAggregate->earlySkipCount() == 0,
            "failure must not be reclassified as completion or skip evidence");
    require(
        countSignal(
            failureDerived.observations(),
            BehavioralSignalKind::DeliberateQueue) == 1,
        "failure journey may retain deliberate queue origin from its factual start");
    require(
        countSignal(
            failureDerived.observations(),
            BehavioralSignalKind::NaturalCompletion) == 0
            && countSignal(
                failureDerived.observations(),
                BehavioralSignalKind::PassiveAutoplayCompletion) == 0
            && countSignal(
                failureDerived.observations(),
                BehavioralSignalKind::EarlySkip) == 0,
        "failure terminal fact must not fabricate recommendation outcome evidence");

    const auto evidence = mustDerive(
        ExposureEvidenceRef::create(
            ExposureEvidenceKind::CandidateGenerator,
            QStringLiteral("generator:history.v1"),
            canonicalA),
        "exposure evidence should construct");
    const auto exposureId = mustDerive(
        RecommendationExposureId::parse(QStringLiteral("exposure-1")),
        "exposure id should parse");

    const auto exposure = mustDerive(
        RecommendationExposureRecord::create(
            profileA,
            exposureId,
            canonicalA,
            4900000,
            1,
            QStringLiteral("ranker-v1"),
            QVector<ExposureEvidenceRef>{evidence}),
        "exposure record should construct");

    require(
        exposure.schemaVersion() == kRecommendationExposureSchemaVersion,
        "exposure schema version must be explicit");

    RecommendationExposureLog exposureLog(profileA);
    auto appendStatus = exposureLog.append(exposure);
    require(
        appendStatus.ok && appendStatus.changed,
        "first exposure append must mutate the log");
    appendStatus = exposureLog.append(exposure);
    require(
        appendStatus.ok && !appendStatus.changed,
        "exact exposure replay must be idempotent");

    const auto conflictingExposure = mustDerive(
        RecommendationExposureRecord::create(
            profileA,
            exposureId,
            canonicalA,
            4900001,
            2,
            QStringLiteral("ranker-v1"),
            QVector<ExposureEvidenceRef>{evidence}),
        "conflicting exposure fixture should construct");
    const auto conflict = exposureLog.append(conflictingExposure);
    require(
        !conflict.ok
            && conflict.error.code
                == DerivationErrorCode::ConflictingExposure,
        "same exposure id with different payload must fail closed");

    const auto otherProfileExposure = mustDerive(
        RecommendationExposureRecord::create(
            profileB,
            mustDerive(
                RecommendationExposureId::parse(
                    QStringLiteral("exposure-profile-b")),
                "other exposure id should parse"),
            canonicalA,
            4900002,
            1,
            QStringLiteral("ranker-v1"),
            QVector<ExposureEvidenceRef>{evidence}),
        "other profile exposure should construct");
    const auto wrongProfile = exposureLog.append(otherProfileExposure);
    require(
        !wrongProfile.ok
            && wrongProfile.error.code == DerivationErrorCode::ProfileMismatch,
        "exposure log must remain profile scoped");

    require(
        derived.observations().size()
            == countSignal(
                   derived.observations(),
                   BehavioralSignalKind::DeliberateQueue)
                + countSignal(
                    derived.observations(),
                    BehavioralSignalKind::NaturalCompletion)
                + countSignal(
                    derived.observations(),
                    BehavioralSignalKind::PassiveAutoplayCompletion)
                + countSignal(
                    derived.observations(),
                    BehavioralSignalKind::EarlySkip)
                + countSignal(
                    derived.observations(),
                    BehavioralSignalKind::Replay),
        "exposure must never create taste observations");

    qInfo().noquote()
        << "VINYL_BE_DERIVATION_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}
