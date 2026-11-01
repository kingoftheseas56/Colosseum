#include "FeedbackTastePersistence.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <cstdio>
#include <limits>
#include <QJsonArray>

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
T mustDomain(vinyl::DomainResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}
template <typename T>
T mustTaste(vinyl::TasteResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPersist(vinyl::TastePersistenceResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::BehaviorObservationId observationId(const QString &value)
{
    return mustTaste(
        vinyl::BehaviorObservationId::parse(value),
        "behavior observation ID should parse");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;
    const ProfileScopeId profile = mustDomain(
        ProfileScopeId::parse(QStringLiteral("profile-a")),
        "profile should parse");
    const RecordingMbid recordingA = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "recording A should parse");
    const RecordingMbid recordingB = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("22222222-2222-4222-8222-222222222222")),
        "recording B should parse");
    const CanonicalEntityRef canonicalA =
        CanonicalEntityRef::from(recordingA);
    const CanonicalEntityRef canonicalB =
        CanonicalEntityRef::from(recordingB);

    TasteProfileState state(profile);
    require(state.applyExplicitFeedback(
                canonicalB,
                ExplicitTasteFeedback::Negative,
                1,
                1000).ok,
            "recording B feedback should apply");
    require(state.applyExplicitFeedback(
                canonicalA,
                ExplicitTasteFeedback::Positive,
                2,
                900).ok,
            "higher revision should accept backward descriptive wall time");
    require(state.setHidden(canonicalA, true, 3, 800).ok,
            "hidden state should apply");
    require(state.setSnoozedUntil(
                canonicalA,
                std::optional<qint64>{5000},
                4,
                700).ok,
            "snooze state should apply");
    require(state.setDoNotRecommend(canonicalA, true, 5, 600).ok,
            "do-not-recommend should apply");

    const auto behavior = mustTaste(
        BehavioralSignalObservation::create(
            observationId(QStringLiteral("obs-2")),
            canonicalA,
            BehavioralSignalKind::EarlySkip,
            1200),
        "behavior should construct");
    require(state.recordBehavioralSignal(behavior).ok,
            "behavior should append");
    const auto behaviorEarlier = mustTaste(
        BehavioralSignalObservation::create(
            observationId(QStringLiteral("obs-1")),
            canonicalA,
            BehavioralSignalKind::Replay,
            1100),
        "second behavior should construct");
    require(state.recordBehavioralSignal(behaviorEarlier).ok,
            "second behavior should append");

    const QByteArray first = mustPersist(
        FeedbackTastePersistence::capture(state),
        "taste capture should succeed");
    const QByteArray second = mustPersist(
        FeedbackTastePersistence::capture(state),
        "second taste capture should succeed");
    require(first == second,
            "identical taste state must serialize to identical canonical bytes");

    const QJsonObject emitted = QJsonDocument::fromJson(first).object();
    require(emitted.value(QStringLiteral("version")).toInt()
                == FeedbackTastePersistence::SchemaVersion,
            "writer must emit the current BE2 schema version");
    const TasteProfileState restored = mustPersist(
        FeedbackTastePersistence::restore(profile, first),
        "taste state should round-trip");
    const EntityTasteState *restoredA = restored.find(canonicalA);
    require(restoredA != nullptr,
            "recording A taste state must round-trip");
    require(restoredA->explicitFeedback().feedback()
                == ExplicitTasteFeedback::Positive,
            "explicit feedback must round-trip");
    require(restoredA->explicitFeedback().revision() == 2,
            "explicit feedback revision must round-trip");
    require(restoredA->suppression().hidden()
                && restoredA->suppression().hiddenRevision() == 3,
            "hidden state and revision must round-trip");
    require(restoredA->suppression().snoozedUntilMs()
                == std::optional<qint64>{5000},
            "snooze expiry must round-trip");
    require(restoredA->suppression().doNotRecommend()
                && restoredA->suppression().doNotRecommendRevision() == 5,
            "do-not-recommend state must round-trip");
    require(restoredA->behavioralSignals().size() == 2,
            "behavioral evidence must round-trip");

    TasteProfileState equivalent(profile);
    require(equivalent.applyExplicitFeedback(
                canonicalA,
                ExplicitTasteFeedback::Positive,
                2,
                900).ok,
            "equivalent A feedback should apply");
    require(equivalent.setHidden(canonicalA, true, 3, 800).ok,
            "equivalent hidden state should apply");
    require(equivalent.setSnoozedUntil(
                canonicalA,
                std::optional<qint64>{5000},
                4,
                700).ok,
            "equivalent snooze should apply");
    require(equivalent.setDoNotRecommend(
                canonicalA, true, 5, 600).ok,
            "equivalent dnr should apply");
    require(equivalent.recordBehavioralSignal(behaviorEarlier).ok,
            "equivalent first behavior should append");
    require(equivalent.recordBehavioralSignal(behavior).ok,
            "equivalent second behavior should append");
    require(equivalent.applyExplicitFeedback(
                canonicalB,
                ExplicitTasteFeedback::Negative,
                1,
                1000).ok,
            "equivalent B feedback should apply");

    const QByteArray equivalentBytes = mustPersist(
        FeedbackTastePersistence::capture(equivalent),
        "equivalent taste capture should succeed");
    require(first == equivalentBytes,
            "canonical BE2 serialization must ignore insertion order");
    QJsonObject migration = QJsonDocument::fromJson(first).object();
    migration.insert(QStringLiteral("version"), 1);
    auto legacyRows = migration.value("entities").toArray();
    for (int index=0; index<legacyRows.size(); ++index) {
        auto row=legacyRows[index].toObject(); row.remove("identity"); legacyRows[index]=row;
    }
    migration.insert("entities", legacyRows);
    const QByteArray v1Fixture =
        QJsonDocument(migration).toJson(QJsonDocument::Compact);
    const TasteProfileState migrated = mustPersist(
        FeedbackTastePersistence::restore(profile, v1Fixture),
        "supported BE2 v1 fixture should restore");
    const QByteArray migratedBytes = mustPersist(
        FeedbackTastePersistence::capture(migrated),
        "migrated BE2 state should serialize");
    require(QJsonDocument::fromJson(migratedBytes)
                .object().value(QStringLiteral("version")).toInt()
                == FeedbackTastePersistence::SchemaVersion,
            "BE2 v1 reader must upgrade on next write");

    const auto malformed = FeedbackTastePersistence::restore(
        profile, QByteArrayLiteral("{broken-json"));
    require(!malformed.ok()
                && malformed.error.code
                    == TastePersistenceErrorCode::MalformedSnapshot,
            "malformed BE2 JSON must return typed corruption error");
    QJsonObject unknown = QJsonDocument::fromJson(first).object();
    unknown.insert(QStringLiteral("futureField"), true);
    const auto unknownResult = FeedbackTastePersistence::restore(
        profile,
        QJsonDocument(unknown).toJson(QJsonDocument::Compact));
    require(!unknownResult.ok()
                && unknownResult.error.code
                    == TastePersistenceErrorCode::MalformedSnapshot,
            "unknown fields must fail closed inside a declared BE2 version");

    QJsonObject future = QJsonDocument::fromJson(first).object();
    future.insert(
        QStringLiteral("version"),
        FeedbackTastePersistence::SchemaVersion + 1);
    const auto futureResult = FeedbackTastePersistence::restore(
        profile,
        QJsonDocument(future).toJson(QJsonDocument::Compact));
    require(!futureResult.ok()
                && futureResult.error.code
                    == TastePersistenceErrorCode::UnsupportedVersion,
            "future BE2 versions must fail closed");

    const QString regression = app.arguments().value(1);
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-exact")) {
        TasteProfileState exactState(profile);
        const qint64 largeTimestamp = 9007199254740993LL;
        require(exactState.setHidden(canonicalA, true, 1, largeTimestamp).ok,
                "large exact timestamp must be valid in-memory state");
        const auto exactBytes = mustPersist(FeedbackTastePersistence::capture(exactState), "exact capture must succeed");
        const auto exactRestored = mustPersist(FeedbackTastePersistence::restore(profile, exactBytes), "exact restore must succeed");
        require(exactRestored.find(canonicalA)->suppression().hiddenChangedAtMs() == largeTimestamp,
                "M0R persistence must preserve integer milliseconds above double exact range");
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-max")) {
        TasteProfileState maximumState(profile);
        require(maximumState.setHidden(canonicalA, true, 1, std::numeric_limits<qint64>::max()).ok,
                "maximum timestamp must be valid in memory");
        const auto bytes = mustPersist(FeedbackTastePersistence::capture(maximumState), "maximum capture must succeed");
        const auto restoredMaximum = mustPersist(FeedbackTastePersistence::restore(profile, bytes),
                                                  "M0R maximum qint64 timestamp must restore exactly");
        require(restoredMaximum.find(canonicalA)->suppression().hiddenChangedAtMs() == std::numeric_limits<qint64>::max(),
                "M0R maximum timestamp must survive persistence");
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-overflow")) {
        QJsonObject bad = QJsonDocument::fromJson(first).object();
        auto entities = bad.value(QStringLiteral("entities")).toArray();
        auto entity = entities.first().toObject();
        auto feedback = entity.value(QStringLiteral("explicit")).toObject();
        feedback.insert(QStringLiteral("changedAtMs"), 9223372036854775808.0);
        entity.insert(QStringLiteral("explicit"), feedback);
        entities[0] = entity; bad.insert(QStringLiteral("entities"), entities);
        require(!FeedbackTastePersistence::restore(profile, QJsonDocument(bad).toJson()).ok(),
                "M0R out-of-range millisecond value must fail before integer conversion");
        bad.insert(QStringLiteral("version"), 1e100);
        require(!FeedbackTastePersistence::restore(profile, QJsonDocument(bad).toJson()).ok(),
                "M0R oversized schema version must fail before integer conversion");
        if (!regression.isEmpty()) return 0;
    }
    qInfo().noquote()
        << "VINYL_BE2_PERSISTENCE_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}
