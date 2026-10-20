#include "FeedbackTasteState.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>

#include <cstdlib>
#include <cstdio>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (!condition) {
        std::fprintf(stderr, "BE2_CHECK_FAILED: %s\n", message);
        std::exit(1);
    }
}

template <typename T>
T mustDomain(vinyl::DomainResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *std::move(result.value);
}

template <typename T>
T mustTaste(vinyl::TasteResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *std::move(result.value);
}

template <typename T, typename = void>
struct HasFinalScore : std::false_type {
};

template <typename T>
struct HasFinalScore<
    T,
    std::void_t<decltype(std::declval<const T &>().finalScore())>>
    : std::true_type {
};

template <typename T, typename = void>
struct HasRank : std::false_type {
};

template <typename T>
struct HasRank<T, std::void_t<decltype(std::declval<const T &>().rank())>>
    : std::true_type {
};

template <typename T, typename = void>
struct HasRadioSequence : std::false_type {
};

template <typename T>
struct HasRadioSequence<
    T,
    std::void_t<decltype(std::declval<const T &>().radioSequence())>>
    : std::true_type {
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    static_assert(
        !std::is_same<ProfileScopeId, ProviderId>::value,
        "taste profile scope must be host-owned, not provider identity");
    static_assert(
        !std::is_same<BehaviorObservationId, ProviderItemId>::value,
        "behavior observation identity must remain host-owned");
    static_assert(
        !HasFinalScore<EntityTasteState>::value,
        "BE2 must not expose a final Vinyl score");
    static_assert(
        !HasRank<EntityTasteState>::value,
        "BE2 must not expose final recommendation rank");
    static_assert(
        !HasRadioSequence<TasteProfileState>::value,
        "BE2 must not manufacture radio/autoplay sequencing");

    require(
        QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
        "BE2 harness must execute against Qt 6.11.1");

    const auto badProfile = ProfileScopeId::parse(QStringLiteral(" "));
    require(
        !badProfile.ok()
            && badProfile.error.code == DomainErrorCode::InvalidProfileScopeId,
        "blank profile scope must fail shared BA0 typed validation");

    auto profileAId = mustDomain(
        ProfileScopeId::parse(QStringLiteral("profile-A")),
        "profile A should parse");
    auto profileBId = mustDomain(
        ProfileScopeId::parse(QStringLiteral("profile-B")),
        "profile B should parse");

    const auto recordingA = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "recording A should parse");
    const auto recordingB = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "recording B should parse");
    const auto recordingC = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("44444444-4444-4444-8444-444444444444")),
        "recording C should parse");
    const auto artist = mustDomain(
        ArtistMbid::parse(
            QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "artist should parse");

    const CanonicalEntityRef canonicalA = CanonicalEntityRef::from(recordingA);
    const CanonicalEntityRef canonicalB = CanonicalEntityRef::from(recordingB);
    const CanonicalEntityRef canonicalC = CanonicalEntityRef::from(recordingC);
    const CanonicalEntityRef canonicalArtist = CanonicalEntityRef::from(artist);

    const auto providerId = mustDomain(
        ProviderId::parse(QStringLiteral("ListenBrainz")),
        "provider id should parse");
    const auto providerFeedbackId = mustDomain(
        ProviderItemId::parse(QStringLiteral("feedback-record-77")),
        "provider feedback id should parse");
    const ProviderItemRef providerFeedbackRef(providerId, providerFeedbackId);

    TasteProfileState profileA(profileAId);

    auto positive = profileA.applyExplicitFeedback(
        canonicalA,
        ExplicitTasteFeedback::Positive,
        100,
        100,
        providerFeedbackRef);
    require(positive.ok && positive.changed, "positive feedback should apply");

    const auto *stateA = profileA.find(canonicalA);
    require(stateA != nullptr, "explicit feedback should create canonical state");
    require(
        stateA->canonical() == canonicalA,
        "taste state identity must remain the canonical entity");
    require(
        stateA->explicitFeedback().feedback() == ExplicitTasteFeedback::Positive,
        "explicit positive feedback must be stored explicitly");
    require(
        stateA->explicitFeedback().providerFeedbackRef().has_value()
            && *stateA->explicitFeedback().providerFeedbackRef()
                == providerFeedbackRef,
        "provider feedback id may survive only as provenance");

    auto negative = profileA.applyExplicitFeedback(
        canonicalA,
        ExplicitTasteFeedback::Negative,
        200,
        200);
    require(negative.ok && negative.changed, "newer negative should replace positive");
    stateA = profileA.find(canonicalA);
    require(
        stateA->explicitFeedback().feedback() == ExplicitTasteFeedback::Negative,
        "newer explicit polarity must be authoritative");
    require(
        !stateA->explicitFeedback().providerFeedbackRef().has_value(),
        "new explicit mutation may clear old provider provenance");

    const auto staleExplicit = profileA.applyExplicitFeedback(
        canonicalA,
        ExplicitTasteFeedback::Positive,
        150,
        150);
    require(
        !staleExplicit.ok
            && staleExplicit.error.code == TasteErrorCode::StaleMutation,
        "older explicit feedback must fail closed");

    const auto duplicateExplicit = profileA.applyExplicitFeedback(
        canonicalA,
        ExplicitTasteFeedback::Negative,
        200,
        200);
    require(
        duplicateExplicit.ok && !duplicateExplicit.changed,
        "same explicit value at same timestamp must be idempotent");

    const auto conflictExplicit = profileA.applyExplicitFeedback(
        canonicalA,
        ExplicitTasteFeedback::Positive,
        200,
        200);
    require(
        !conflictExplicit.ok
            && conflictExplicit.error.code == TasteErrorCode::ConflictingMutation,
        "equal-timestamp explicit disagreement must fail closed");

    const auto neutral = profileA.applyExplicitFeedback(
        canonicalA,
        ExplicitTasteFeedback::Neutral,
        300,
        300);
    require(neutral.ok && neutral.changed, "neutral tombstone should apply");
    require(
        profileA.find(canonicalA)->explicitFeedback().hasExplicitMutation()
            && profileA.find(canonicalA)->explicitFeedback().feedback()
                == ExplicitTasteFeedback::Neutral,
        "neutral must remain an explicit tombstone, not erase ordering state");

    const auto staleAfterNeutral = profileA.applyExplicitFeedback(
        canonicalA,
        ExplicitTasteFeedback::Negative,
        250,
        250);
    require(
        !staleAfterNeutral.ok
            && staleAfterNeutral.error.code == TasteErrorCode::StaleMutation,
        "neutral tombstone must prevent stale feedback resurrection");

    const auto sameProviderOtherCanonical = profileA.applyExplicitFeedback(
        canonicalB,
        ExplicitTasteFeedback::Positive,
        110,
        110,
        providerFeedbackRef);
    require(
        sameProviderOtherCanonical.ok && sameProviderOtherCanonical.changed,
        "provider feedback provenance must not deduplicate canonical entities");
    require(
        profileA.find(canonicalA) != nullptr
            && profileA.find(canonicalB) != nullptr
            && profileA.find(canonicalA)->canonical()
                != profileA.find(canonicalB)->canonical(),
        "provider feedback id must never become canonical identity");

    const auto artistFeedback = profileA.applyExplicitFeedback(
        canonicalArtist,
        ExplicitTasteFeedback::Positive,
        120,
        120);
    require(
        artistFeedback.ok && artistFeedback.changed,
        "feedback state must accept canonical entity kinds beyond recordings");

    const auto invalidHidden = profileA.setHidden(canonicalA, true, 1, 0);
    require(
        !invalidHidden.ok
            && invalidHidden.error.code == TasteErrorCode::InvalidTimestamp,
        "non-positive descriptive wall timestamp must fail");
    const auto invalidRevision = profileA.setHidden(canonicalA, true, 0, 1);
    require(
        !invalidRevision.ok
            && invalidRevision.error.code == TasteErrorCode::InvalidRevision,
        "zero host revision must fail");

    const auto hide = profileA.setHidden(canonicalA, true, 600, 600);
    const auto snooze = profileA.setSnoozedUntil(canonicalA, 1000, 610, 610);
    const auto dnr = profileA.setDoNotRecommend(canonicalA, true, 620, 620);
    require(hide.ok && hide.changed, "hide should apply");
    require(snooze.ok && snooze.changed, "snooze should apply");
    require(dnr.ok && dnr.changed, "do-not-recommend should apply");

    stateA = profileA.find(canonicalA);
    require(
        stateA->suppression().hidden(),
        "hide must remain an explicit independent suppression dimension");
    require(
        stateA->suppression().snoozedUntilMs() == std::optional<qint64>(1000),
        "snooze expiry must remain explicit");
    require(
        stateA->suppression().doNotRecommend(),
        "do-not-recommend must remain explicit and distinct");
    require(
        stateA->suppression().isSnoozedAt(999)
            && !stateA->suppression().isSnoozedAt(1000),
        "snooze query must expire deterministically at the boundary");

    const auto invalidSnooze = profileA.setSnoozedUntil(canonicalA, 800, 800, 800);
    require(
        !invalidSnooze.ok
            && invalidSnooze.error.code == TasteErrorCode::InvalidSnoozeUntil,
        "snooze expiry must be later than mutation timestamp");

    const auto clearHide = profileA.setHidden(canonicalA, false, 700, 700);
    const auto clearSnooze = profileA.setSnoozedUntil(canonicalA, std::nullopt, 710, 710);
    const auto clearDnr = profileA.setDoNotRecommend(canonicalA, false, 720, 720);
    require(clearHide.ok && clearHide.changed, "hide should clear independently");
    require(clearSnooze.ok && clearSnooze.changed, "snooze should clear independently");
    require(clearDnr.ok && clearDnr.changed, "do-not-recommend should clear independently");

    stateA = profileA.find(canonicalA);
    require(
        !stateA->suppression().hidden()
            && !stateA->suppression().snoozedUntilMs().has_value()
            && !stateA->suppression().doNotRecommend(),
        "clearing suppression dimensions must be deterministic");
    require(
        stateA->explicitFeedback().feedback() == ExplicitTasteFeedback::Neutral,
        "suppression changes must not mutate explicit taste feedback");

    const auto staleHide = profileA.setHidden(canonicalA, true, 650, 650);
    require(
        !staleHide.ok && staleHide.error.code == TasteErrorCode::StaleMutation,
        "older hide mutation must fail closed");
    const auto equalHideConflict = profileA.setHidden(canonicalA, true, 700, 700);
    require(
        !equalHideConflict.ok
            && equalHideConflict.error.code == TasteErrorCode::ConflictingMutation,
        "equal-timestamp hide disagreement must fail closed");
    const auto equalHideNoop = profileA.setHidden(canonicalA, false, 700, 700);
    require(
        equalHideNoop.ok && !equalHideNoop.changed,
        "equal-timestamp identical hide mutation must be idempotent");

    const auto staleSnooze = profileA.setSnoozedUntil(canonicalA, 1200, 705, 705);
    require(
        !staleSnooze.ok && staleSnooze.error.code == TasteErrorCode::StaleMutation,
        "older snooze mutation must fail closed");
    const auto equalSnoozeConflict =
        profileA.setSnoozedUntil(canonicalA, 1200, 710, 710);
    require(
        !equalSnoozeConflict.ok
            && equalSnoozeConflict.error.code
                == TasteErrorCode::ConflictingMutation,
        "equal-timestamp snooze disagreement must fail closed");
    const auto equalSnoozeNoop =
        profileA.setSnoozedUntil(canonicalA, std::nullopt, 710, 710);
    require(
        equalSnoozeNoop.ok && !equalSnoozeNoop.changed,
        "equal-timestamp identical snooze mutation must be idempotent");

    const auto staleDnr = profileA.setDoNotRecommend(canonicalA, true, 719, 719);
    require(
        !staleDnr.ok && staleDnr.error.code == TasteErrorCode::StaleMutation,
        "older do-not-recommend mutation must fail closed");
    const auto equalDnrConflict =
        profileA.setDoNotRecommend(canonicalA, true, 720, 720);
    require(
        !equalDnrConflict.ok
            && equalDnrConflict.error.code
                == TasteErrorCode::ConflictingMutation,
        "equal-timestamp do-not-recommend disagreement must fail closed");
    const auto equalDnrNoop =
        profileA.setDoNotRecommend(canonicalA, false, 720, 720);
    require(
        equalDnrNoop.ok && !equalDnrNoop.changed,
        "equal-revision identical do-not-recommend mutation must be idempotent");

    const auto backwardWallNewer =
        profileA.setDoNotRecommend(canonicalA, true, 721, 500);
    require(
        backwardWallNewer.ok && backwardWallNewer.changed,
        "higher revision must apply even when descriptive wall time moves backward");
    stateA = profileA.find(canonicalA);
    require(
        stateA->suppression().doNotRecommendRevision() == 721
            && stateA->suppression().doNotRecommendChangedAtMs() == 500,
        "revision owns mutation order while backward wall time is retained descriptively");

    const auto observationId = mustTaste(
        BehaviorObservationId::parse(QStringLiteral("listen-event-001")),
        "behavior observation id should parse");
    const auto replayObservation = mustTaste(
        BehavioralSignalObservation::create(
            observationId,
            canonicalA,
            BehavioralSignalKind::Replay,
            800),
        "replay observation should be valid");

    const auto behaviorAdded = profileA.recordBehavioralSignal(replayObservation);
    require(
        behaviorAdded.ok && behaviorAdded.changed,
        "first behavioral observation should append");
    const auto behaviorDuplicate = profileA.recordBehavioralSignal(replayObservation);
    require(
        behaviorDuplicate.ok && !behaviorDuplicate.changed,
        "same behavioral observation must be idempotent");

    const auto conflictingObservation = mustTaste(
        BehavioralSignalObservation::create(
            observationId,
            canonicalA,
            BehavioralSignalKind::EarlySkip,
            800),
        "conflicting fixture observation should still be structurally valid");
    const auto behaviorConflict =
        profileA.recordBehavioralSignal(conflictingObservation);
    require(
        !behaviorConflict.ok
            && behaviorConflict.error.code
                == TasteErrorCode::ConflictingBehaviorObservation,
        "reused observation id with different evidence must fail closed");

    const auto secondObservationId = mustTaste(
        BehaviorObservationId::parse(QStringLiteral("listen-event-002")),
        "second observation id should parse");
    const auto earlySkipObservation = mustTaste(
        BehavioralSignalObservation::create(
            secondObservationId,
            canonicalA,
            BehavioralSignalKind::EarlySkip,
            900),
        "early skip observation should be valid");
    require(
        profileA.recordBehavioralSignal(earlySkipObservation).ok,
        "independent behavioral evidence should append");

    const auto behaviorOnlyId = mustTaste(
        BehaviorObservationId::parse(QStringLiteral("listen-event-003")),
        "behavior-only observation id should parse");
    const auto behaviorOnlyObservation = mustTaste(
        BehavioralSignalObservation::create(
            behaviorOnlyId,
            canonicalC,
            BehavioralSignalKind::NaturalCompletion,
            950),
        "behavior-only observation should be valid");
    require(
        profileA.recordBehavioralSignal(behaviorOnlyObservation).ok,
        "behavior-only evidence should create entity taste state");
    const auto *behaviorOnlyState = profileA.find(canonicalC);
    require(
        behaviorOnlyState != nullptr
            && !behaviorOnlyState->explicitFeedback().hasExplicitMutation(),
        "behavior-only state must not manufacture an explicit user vote");

    stateA = profileA.find(canonicalA);
    require(
        stateA->behavioralSignals().size() == 2,
        "positive and negative behavioral evidence must coexist as observations");
    require(
        stateA->behavioralSignals()[0].kind() == BehavioralSignalKind::Replay
            && stateA->behavioralSignals()[1].kind()
                == BehavioralSignalKind::EarlySkip,
        "behavioral evidence kinds must remain distinguishable");
    require(
        stateA->explicitFeedback().feedback() == ExplicitTasteFeedback::Neutral,
        "behavioral observations must never silently become explicit feedback");

    TasteProfileState profileB(profileBId);
    const auto profileBPositive = profileB.applyExplicitFeedback(
        canonicalA,
        ExplicitTasteFeedback::Positive,
        50,
        50);
    require(profileBPositive.ok && profileBPositive.changed, "profile B feedback should apply");
    require(
        profileB.find(canonicalA)->explicitFeedback().feedback()
            == ExplicitTasteFeedback::Positive,
        "profile B must own its own taste state");
    require(
        profileA.find(canonicalA)->explicitFeedback().feedback()
            == ExplicitTasteFeedback::Neutral,
        "mutating profile B must not change profile A");

    require(
        explicitTasteFeedbackName(ExplicitTasteFeedback::Negative)
            == QStringLiteral("negative"),
        "explicit feedback names must be stable");
    require(
        behavioralSignalKindName(BehavioralSignalKind::RecommendationAvoidance)
            == QStringLiteral("recommendation_avoidance"),
        "behavioral signal names must be stable");
    require(
        tasteErrorCodeName(TasteErrorCode::ConflictingMutation)
            == QStringLiteral("conflicting_mutation"),
        "taste error names must be stable snake_case");

    TasteProfileState rejectedProfile(profileAId);
    require(!rejectedProfile.setHidden(canonicalA, true, 0, 1000).ok
                && rejectedProfile.size() == 0,
            "M0R rejected hidden mutation must not create entity state");
    require(!rejectedProfile.setDoNotRecommend(canonicalB, true, 1, 0).ok
                && rejectedProfile.size() == 0,
            "M0R rejected DNR mutation must not create entity state");
    QTextStream(stdout)
        << "VINYL_BE2_FEEDBACK_TASTE_STATE_OK checks=" << g_checks
        << " qt=" << qVersion() << Qt::endl;
    return 0;
}
