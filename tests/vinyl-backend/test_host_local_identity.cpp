#include "VinylSessionPersistence.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>

#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

template <typename T, typename = void>
struct HasHostLocalIdentityApi : std::false_type
{
};

template <typename T>
struct HasHostLocalIdentityApi<T, std::void_t<
    decltype(T::createLocal(
        std::declval<vinyl::QueueOccurrenceId>(),
        std::declval<const QString &>(),
        std::declval<std::optional<vinyl::RecordingMbid>>(),
        std::declval<std::optional<vinyl::ReleaseTrackCoordinate>>(),
        std::declval<std::optional<vinyl::EnqueueOrigin>>())),
    decltype(std::declval<const T &>().localIdentity()),
    decltype(std::declval<const T &>().canonicalRecording()),
    decltype(std::declval<const T &>().sameMusicIdentity(
        std::declval<const T &>()))>> : std::true_type
{
};

namespace {

using namespace vinyl;

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
T mustDomain(const DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustQueue(const QueueResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPersistence(const SessionPersistenceResult<T> &result,
                  const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

ProfileScopeId profile(const char *value)
{
    return mustDomain(ProfileScopeId::parse(QString::fromLatin1(value)),
                      "profile fixture should parse");
}

QueueOccurrenceId occurrenceId(const char *value)
{
    return mustQueue(QueueOccurrenceId::parse(QString::fromLatin1(value)),
                     "occurrence fixture should parse");
}

RecordingMbid recording(const char *value)
{
    return mustDomain(RecordingMbid::parse(QString::fromLatin1(value)),
                      "recording fixture should parse");
}

ReleaseTrackCoordinate releaseContext(const RecordingMbid &recordingMbid)
{
    const auto releaseGroup = mustDomain(
        ReleaseGroupMbid::parse(
            QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "release group fixture should parse");
    const auto release = mustDomain(
        ReleaseMbid::parse(
            QStringLiteral("44444444-4444-4444-8444-444444444444")),
        "release fixture should parse");
    const auto releaseTrack = mustDomain(
        ReleaseTrackMbid::parse(
            QStringLiteral("55555555-5555-4555-8555-555555555555")),
        "release track fixture should parse");
    return mustDomain(
        ReleaseTrackCoordinate::create(
            releaseGroup, release, releaseTrack, recordingMbid, 2, 7),
        "release context fixture should construct");
}

QJsonObject parseObject(const QByteArray &bytes)
{
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    require(document.isObject(), "snapshot should be a JSON object");
    return document.object();
}

QByteArray compact(QJsonObject object)
{
    return QJsonDocument(std::move(object)).toJson(QJsonDocument::Compact);
}

template <typename Occurrence>
int runHostLocalIdentityChecks()
{
    static_assert(
        std::is_same<decltype(std::declval<const Occurrence &>().recordingMbid()),
                     const RecordingMbid &>::value,
        "legacy recordingMbid() must remain a canonical-only accessor");

    const RecordingMbid mappedRecording =
        recording("22222222-2222-4222-8222-222222222222");
    const RecordingMbid differentRecording =
        recording("66666666-6666-4666-8666-666666666666");
    const ReleaseTrackCoordinate mappedContext = releaseContext(mappedRecording);

    const Occurrence localA = mustQueue(
        Occurrence::createLocal(occurrenceId("local-a"),
                                QStringLiteral("vault-entry:audio-42")),
        "unmapped local occurrence should construct without inventing an MBID");
    const Occurrence localB = mustQueue(
        Occurrence::createLocal(occurrenceId("local-b"),
                                QStringLiteral("vault-entry:audio-42")),
        "second occurrence of one local track should construct");
    const Occurrence otherLocal = mustQueue(
        Occurrence::createLocal(occurrenceId("local-c"),
                                QStringLiteral("vault-entry:audio-43")),
        "different local track should construct");

    VinylQueue localQueue;
    require(localQueue.append(localA).ok && localQueue.append(localB).ok,
            "duplicate local tracks should remain distinct queue occurrences");
    require(localA.localIdentity() == QStringLiteral("vault-entry:audio-42"),
            "local identity should retain the opaque host reference");
    require(!localA.canonicalRecording().has_value(),
            "unmapped local tracks should not acquire a fake canonical MBID");
    bool missingCanonicalThrows = false;
    try {
        (void)localA.recordingMbid();
    } catch (const std::bad_optional_access &) {
        missingCanonicalThrows = true;
    }
    require(missingCanonicalThrows,
            "legacy canonical accessor should expose misuse through optional::value");
    require(localA.sameMusicIdentity(localB),
            "two occurrences of one local identity should share music identity");
    require(!localA.sameMusicIdentity(otherLocal),
            "different local identities should remain distinct");

    const auto missingContextMapping = Occurrence::createLocal(
        occurrenceId("local-bad-context"),
        QStringLiteral("vault-entry:audio-42"),
        std::nullopt,
        mappedContext);
    require(!missingContextMapping.ok()
                && missingContextMapping.error.code
                    == QueueErrorCode::RecordingContextMismatch,
            "release context must not silently supply a missing verified mapping");
    require(!Occurrence::createLocal(occurrenceId("local-empty"), QString()).ok(),
            "empty local identities should be rejected");
    require(!Occurrence::createLocal(
                 occurrenceId("local-too-long"), QString(2049, QLatin1Char('x')))
                 .ok(),
            "local identities must have a bounded length");

    const Occurrence mappedLocal = mustQueue(
        Occurrence::createLocal(
            occurrenceId("local-mapped"),
            QStringLiteral("vault-entry:audio-42"),
            mappedRecording,
            mappedContext,
            EnqueueOrigin(EnqueueOriginKind::User,
                          mustQueue(EnqueueBatchId::parse(
                                        QStringLiteral("local-batch")),
                                    "enqueue batch fixture should parse"))),
        "verified canonical mapping with matching context should construct");
    const Occurrence canonical = mustQueue(
        Occurrence::create(occurrenceId("canonical"), mappedRecording),
        "canonical occurrence fixture should construct");
    const auto mismatchedContext = Occurrence::createLocal(
        occurrenceId("local-bad-mapping"),
        QStringLiteral("vault-entry:audio-42"),
        differentRecording,
        mappedContext);
    require(!mismatchedContext.ok()
                && mismatchedContext.error.code
                    == QueueErrorCode::RecordingContextMismatch,
            "release context must match an explicit verified mapping");
    require(!mappedLocal.sameMusicIdentity(canonical),
            "canonical mapping must not erase the local file reference during session replacement");
    require(!mappedLocal.sameMusicIdentity(localA),
            "a verified mapping change must be explicit during session replacement");
    const Occurrence sameMappedLocal = mustQueue(
        Occurrence::createLocal(occurrenceId("same-mapped-local"),
                                QStringLiteral("vault-entry:audio-42"), mappedRecording),
        "matching mapped local occurrence should construct");
    require(mappedLocal.sameMusicIdentity(sameMappedLocal),
            "the same local reference and verified mapping should share music identity");

    VinylQueue persistedQueue;
    require(persistedQueue.append(localA).ok
                && persistedQueue.append(mappedLocal).ok,
            "local queue fixture should append both unmapped and mapped tracks");
    PlaybackSession idleSession;
    const ProfileScopeId localProfile = profile("local-profile");
    const QByteArray localSnapshot = mustPersistence(
        VinylSessionPersistence::capture(localProfile, persistedQueue, idleSession),
        "BA3 should capture local queue identities");
    const QJsonObject localRoot = parseObject(localSnapshot);
    require(localRoot.value(QStringLiteral("version")).toInt() == 3,
            "BA3 should emit schema v3 when a local identity requires it");

    const auto restored = mustPersistence(
        VinylSessionPersistence::restore(localProfile, localSnapshot),
        "BA3 should restore local identity snapshot for its bound profile");
    const Occurrence *restoredUnmapped =
        restored.queue.find(occurrenceId("local-a"));
    const Occurrence *restoredMapped =
        restored.queue.find(occurrenceId("local-mapped"));
    require(restoredUnmapped && restoredMapped,
            "restored queue should retain each local occurrence ID");
    require(restoredUnmapped->localIdentity()
                == QStringLiteral("vault-entry:audio-42")
                && !restoredUnmapped->canonicalRecording().has_value(),
            "BA3 should round-trip an unmapped local identity without an MBID");
    require(restoredMapped->localIdentity()
                == QStringLiteral("vault-entry:audio-42")
                && restoredMapped->canonicalRecording().has_value()
                && *restoredMapped->canonicalRecording() == mappedRecording,
            "BA3 should round-trip the optional verified mapping");
    require(restoredMapped->releaseContext().has_value()
                && restoredMapped->releaseContext()->trackPosition() == 7
                && restoredMapped->enqueueOrigin().has_value(),
            "BA3 should retain local release context and enqueue origin");
    const auto wrongProfileRestore = VinylSessionPersistence::restore(
        profile("other-profile"), localSnapshot);
    require(!wrongProfileRestore.ok()
                && wrongProfileRestore.error.code
                    == SessionPersistenceErrorCode::ProfileMismatch,
            "BA3 local snapshot must remain bound to its originating profile");

    QJsonObject malformedRoot = localRoot;
    QJsonObject malformedQueue = malformedRoot.value(QStringLiteral("queue")).toObject();
    QJsonArray malformedItems = malformedQueue.value(QStringLiteral("items")).toArray();
    QJsonObject malformedLocal = malformedItems.at(0).toObject();
    const QJsonObject mappedWireItem = malformedItems.at(1).toObject();
    malformedLocal.insert(QStringLiteral("releaseContext"),
                          mappedWireItem.value(QStringLiteral("releaseContext")));
    malformedItems.replace(0, malformedLocal);
    malformedQueue.insert(QStringLiteral("items"), malformedItems);
    malformedRoot.insert(QStringLiteral("queue"), malformedQueue);
    const auto malformedContextRestore =
        VinylSessionPersistence::restore(localProfile, compact(malformedRoot));
    require(!malformedContextRestore.ok()
                && malformedContextRestore.error.code
                    == SessionPersistenceErrorCode::InvalidQueueState,
            "local release context without a verified recording mapping must be rejected");

    QJsonObject mixedRoot = localRoot;
    QJsonObject mixedQueue = mixedRoot.value(QStringLiteral("queue")).toObject();
    QJsonArray mixedItems = mixedQueue.value(QStringLiteral("items")).toArray();
    QJsonObject mixedLocal = mixedItems.at(0).toObject();
    mixedLocal.insert(QStringLiteral("identityKind"), QStringLiteral("canonical"));
    mixedItems.replace(0, mixedLocal);
    mixedQueue.insert(QStringLiteral("items"), mixedItems);
    mixedRoot.insert(QStringLiteral("queue"), mixedQueue);
    const auto mixedIdentityRestore =
        VinylSessionPersistence::restore(localProfile, compact(mixedRoot));
    require(!mixedIdentityRestore.ok()
                && mixedIdentityRestore.error.code
                    == SessionPersistenceErrorCode::MalformedSnapshot,
            "snapshot must reject a local ID declared with canonical identity kind");

    QJsonObject localWithCanonicalMbidRoot = localRoot;
    QJsonObject localWithCanonicalMbidQueue =
        localWithCanonicalMbidRoot.value(QStringLiteral("queue")).toObject();
    QJsonArray localWithCanonicalMbidItems = localWithCanonicalMbidQueue
        .value(QStringLiteral("items")).toArray();
    QJsonObject localWithCanonicalMbid = localWithCanonicalMbidItems.at(0).toObject();
    localWithCanonicalMbid.insert(QStringLiteral("recordingMbid"),
                                  mappedRecording.toString());
    localWithCanonicalMbidItems.replace(0, localWithCanonicalMbid);
    localWithCanonicalMbidQueue.insert(QStringLiteral("items"),
                                       localWithCanonicalMbidItems);
    localWithCanonicalMbidRoot.insert(QStringLiteral("queue"),
                                      localWithCanonicalMbidQueue);
    const auto localWithCanonicalMbidRestore = VinylSessionPersistence::restore(
        localProfile, compact(localWithCanonicalMbidRoot));
    require(!localWithCanonicalMbidRestore.ok()
                && localWithCanonicalMbidRestore.error.code
                    == SessionPersistenceErrorCode::MalformedSnapshot,
            "local variant must reject a simultaneous canonical identity field");

    QJsonObject oldVersionWithLocal = localRoot;
    oldVersionWithLocal.insert(QStringLiteral("version"), 2);
    const auto oldVersionRestore = VinylSessionPersistence::restore(
        localProfile, compact(oldVersionWithLocal));
    require(!oldVersionRestore.ok()
                && oldVersionRestore.error.code
                    == SessionPersistenceErrorCode::MalformedSnapshot,
            "schema v1/v2 must not silently accept local identity fields");

    VinylQueue canonicalQueue;
    require(canonicalQueue.append(canonical).ok,
            "canonical compatibility fixture should append");
    const QByteArray canonicalV2 = mustPersistence(
        VinylSessionPersistence::capture(localProfile, canonicalQueue, idleSession),
        "BA3 should capture canonical-only queues");
    const QJsonObject canonicalRoot = parseObject(canonicalV2);
    require(canonicalRoot.value(QStringLiteral("version")).toInt() == 2,
            "canonical-only capture should preserve schema v2");
    const auto restoredCanonical = mustPersistence(
        VinylSessionPersistence::restore(localProfile, canonicalV2),
        "BA3 should continue reading schema v2 canonical snapshots");
    require(restoredCanonical.queue.items().first().recordingMbid()
                == mappedRecording,
            "schema v2 canonical identity should remain intact");

    QJsonObject legacyItem;
    legacyItem.insert(QStringLiteral("occurrenceId"), QStringLiteral("legacy-v1"));
    legacyItem.insert(QStringLiteral("recordingMbid"), mappedRecording.toString());
    legacyItem.insert(QStringLiteral("releaseContext"), QJsonValue::Null);
    QJsonObject legacyQueue;
    legacyQueue.insert(QStringLiteral("items"), QJsonArray{legacyItem});
    legacyQueue.insert(QStringLiteral("activeOccurrenceId"), QJsonValue::Null);
    QJsonObject legacyRoot;
    legacyRoot.insert(QStringLiteral("schema"), VinylSessionPersistence::schemaName());
    legacyRoot.insert(QStringLiteral("version"), 1);
    legacyRoot.insert(QStringLiteral("profileScopeId"), localProfile.value());
    legacyRoot.insert(QStringLiteral("queue"), legacyQueue);
    legacyRoot.insert(QStringLiteral("resume"), QJsonValue::Null);
    const auto restoredV1 = mustPersistence(
        VinylSessionPersistence::restore(localProfile, compact(legacyRoot)),
        "BA3 should continue reading old schema v1 canonical snapshots");
    require(restoredV1.queue.items().first().recordingMbid() == mappedRecording,
            "schema v1 canonical identity should remain intact");

    qInfo().noquote() << "PASS: host local identity checks" << g_checks;
    return 0;
}

template <typename Occurrence>
int runWhenSupported()
{
    if constexpr (!HasHostLocalIdentityApi<Occurrence>::value) {
        qCritical().noquote()
            << "FAIL: QueueOccurrence host local identity API is not implemented";
        return 1;
    } else {
        return runHostLocalIdentityChecks<Occurrence>();
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    return runWhenSupported<vinyl::QueueOccurrence>();
}
