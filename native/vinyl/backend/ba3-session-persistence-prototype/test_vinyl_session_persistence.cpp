#include "VinylSessionPersistence.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>

#include <cstdlib>
#include <limits>
#include <optional>
#include <type_traits>

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
T mustSession(const vinyl::PlaybackSessionResult<T> &result,
              const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPersistence(const vinyl::SessionPersistenceResult<T> &result,
                  const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::ProfileScopeId profile(const char *value)
{
    return mustDomain(
        vinyl::ProfileScopeId::parse(QString::fromLatin1(value)),
        "fixture profile should parse");
}

vinyl::QueueOccurrenceId occurrenceId(const char *value)
{
    return mustQueue(
        vinyl::QueueOccurrenceId::parse(QString::fromLatin1(value)),
        "fixture occurrence ID should parse");
}

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "fixture Recording MBID should parse");
}

vinyl::QueueOccurrence occurrence(
    const char *id,
    const vinyl::RecordingMbid &recordingMbid,
    std::optional<vinyl::ReleaseTrackCoordinate> context = std::nullopt,
    std::optional<vinyl::EnqueueOrigin> origin = std::nullopt)
{
    return mustQueue(
        vinyl::QueueOccurrence::create(
            occurrenceId(id),
            recordingMbid,
            std::move(context),
            std::move(origin)),
        "fixture occurrence should construct");
}

vinyl::EnqueueBatchId batchId(const char *value)
{
    return mustQueue(
        vinyl::EnqueueBatchId::parse(QString::fromLatin1(value)),
        "fixture enqueue batch ID should parse");
}

vinyl::ReleaseTrackCoordinate releaseContext(
    const vinyl::RecordingMbid &recordingMbid)
{
    const auto releaseGroup = mustDomain(
        vinyl::ReleaseGroupMbid::parse(
            QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "release group fixture");
    const auto release = mustDomain(
        vinyl::ReleaseMbid::parse(
            QStringLiteral("44444444-4444-4444-8444-444444444444")),
        "release fixture");
    const auto releaseTrack = mustDomain(
        vinyl::ReleaseTrackMbid::parse(
            QStringLiteral("55555555-5555-4555-8555-555555555555")),
        "release track fixture");
    return mustDomain(
        vinyl::ReleaseTrackCoordinate::create(
            releaseGroup, release, releaseTrack, recordingMbid, 2, 7),
        "release context fixture");
}

QJsonObject parseObject(const QByteArray &bytes)
{
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    require(document.isObject(), "fixture snapshot should be a JSON object");
    return document.object();
}

QByteArray compact(QJsonObject object)
{
    return QJsonDocument(std::move(object)).toJson(QJsonDocument::Compact);
}

void requireError(
    const vinyl::SessionPersistenceResult<vinyl::RestoredSessionState> &result,
    vinyl::SessionPersistenceErrorCode expected,
    const char *message)
{
    require(!result.ok(), message);
    require(result.error.code == expected,
            "negative control should return expected typed BA3 error");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    static_assert(
        !std::is_same<ProfileScopeId, QueueOccurrenceId>::value,
        "profile scope and queue occurrence identity must stay distinct");
    static_assert(
        !std::is_same<ProfileScopeId, PlayInstanceId>::value,
        "profile scope and durable play identity must stay distinct");

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "BA3 harness must execute against Qt 6.11.1");

    const ProfileScopeId local = profile("local");
    const ProfileScopeId other = profile("other-profile");
    const RecordingMbid duplicate =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid contextual =
        recording("22222222-2222-4222-8222-222222222222");

    const EnqueueContainerRef playlistContainer = mustQueue(
        EnqueueContainerRef::playlist(QStringLiteral("playlist-42")),
        "playlist enqueue container should parse");
    const EnqueueOrigin playlistOrigin(
        EnqueueOriginKind::Playlist,
        batchId("batch-42"),
        playlistContainer);

    VinylQueue queue;
    require(queue.append(
                occurrence(
                    "dup-a",
                    duplicate,
                    std::nullopt,
                    playlistOrigin))
                .ok,
            "append first duplicate occurrence with descriptive origin");
    require(queue.append(occurrence("dup-b", duplicate)).ok,
            "append second duplicate occurrence");
    require(queue.append(
                occurrence("context-c", contextual,
                           releaseContext(contextual)))
                .ok,
            "append release-context occurrence");
    require(queue.activate(occurrenceId("dup-b")).ok,
            "activate second duplicate by occurrence identity");

    PlaybackSession originalSession;
    const PlaybackWorkToken firstGeneration = mustSession(
        originalSession.beginOccurrence(*queue.activeOccurrence(), true),
        "first BA2 lifecycle should begin");
    require(originalSession.markLoaded(firstGeneration).ok,
            "first BA2 generation should load");
    require(originalSession.observePosition(firstGeneration, 12.25).ok,
            "first generation should observe position");

    const PlaybackWorkToken oldToken = mustSession(
        originalSession.beginOccurrence(*queue.activeOccurrence(), true),
        "same occurrence restart should create second generation");
    require(oldToken.generation() == 2,
            "fixture should contain a nontrivial old playback generation");
    require(originalSession.markLoaded(oldToken).ok,
            "second generation should load");
    require(originalSession.observePosition(oldToken, 21.0).ok,
            "second generation should observe position");
    const PlaybackSeekRequest oldSeek1 = mustSession(
        originalSession.requestSeek(35.0),
        "first old-generation seek should be created");
    const PlaybackSeekRequest oldSeek2 = mustSession(
        originalSession.requestSeek(42.5),
        "second old-generation seek should supersede first");
    require(oldSeek1.sequence() == 1 && oldSeek2.sequence() == 2,
            "fixture should contain nontrivial seek sequence");

    SessionCoordinatorIntent durableCoordinator;
    durableCoordinator.repeatMode = DurableRepeatMode::Queue;
    QVector<QueueDisplaySnapshot> displaySnapshots;
    displaySnapshots.append(
        QueueDisplaySnapshot{
            occurrenceId("dup-b"),
            catalogue::LastKnownDisplaySnapshot{
                QStringLiteral("Last Known Duplicate"),
                QStringList{QStringLiteral("Known Artist")}}});

    const QByteArray snapshot = mustPersistence(
        VinylSessionPersistence::capture(
            local,
            queue,
            originalSession,
            durableCoordinator,
            displaySnapshots),
        "resumable queue/session should capture");
    require(!snapshot.isEmpty(), "capture should produce durable bytes");
    require(parseObject(snapshot).value(QStringLiteral("version")).toInt()
                == VinylSessionPersistence::SchemaVersion,
            "new capture should emit schema v2");

    const QByteArray lower = snapshot.toLower();
    require(!lower.contains("\"generation\""),
            "snapshot must not persist playback generation");
    require(!lower.contains("\"seeksequence\""),
            "snapshot must not persist seek sequence");
    require(!lower.contains("\"worktoken\""),
            "snapshot must not persist work token");
    require(!lower.contains("\"failure\""),
            "snapshot must not persist terminal failure envelope");
    require(!lower.contains("\"durationseconds\""),
            "snapshot must not persist BA2 duration observation");
    require(lower.contains("\"positionms\""),
            "schema v2 should persist resume bookmark in integer milliseconds");
    require(!lower.contains("\"positionseconds\""),
            "schema v2 must not emit legacy floating-second bookmark");
    require(!lower.contains("\"provider"),
            "snapshot must not persist provider state or credentials");
    require(!lower.contains("\"mpv"),
            "snapshot must not persist raw mpv state");

    RestoredSessionState restored = mustPersistence(
        VinylSessionPersistence::restore(local, snapshot),
        "captured snapshot should restore");
    require(restored.queue.size() == 3,
            "round trip should preserve queue size");
    require(restored.queue.items().at(0).occurrenceId().value()
                == QStringLiteral("dup-a")
            && restored.queue.items().at(1).occurrenceId().value()
                == QStringLiteral("dup-b")
            && restored.queue.items().at(2).occurrenceId().value()
                == QStringLiteral("context-c"),
            "round trip should preserve deterministic queue order");
    require(restored.queue.items().at(0).recordingMbid() == duplicate
            && restored.queue.items().at(1).recordingMbid() == duplicate,
            "duplicate Recording MBID should survive as two queue occurrences");
    require(restored.queue.items().at(0).occurrenceId()
                != restored.queue.items().at(1).occurrenceId(),
            "duplicate recordings must retain distinct QueueOccurrenceIds");
    require(restored.queue.items().at(0).enqueueOrigin().has_value()
                && restored.queue.items().at(0).enqueueOrigin()->kind()
                    == EnqueueOriginKind::Playlist
                && restored.queue.items().at(0).enqueueOrigin()->batchId().value()
                    == QStringLiteral("batch-42"),
            "descriptive enqueue origin should round trip without changing occurrence identity");
    require(restored.queue.items().at(0).enqueueOrigin()->container().has_value()
                && restored.queue.items().at(0).enqueueOrigin()->container()->playlistId()
                    == QStringLiteral("playlist-42"),
            "enqueue container context should round trip");
    require(restored.queue.activeOccurrenceId().has_value()
            && restored.queue.activeOccurrenceId()->value()
                == QStringLiteral("dup-b"),
            "active occurrence should restore by exact occurrence identity");

    const QueueOccurrence &contextualRestored = restored.queue.items().at(2);
    require(contextualRestored.releaseContext().has_value(),
            "release context should round trip");
    require(contextualRestored.releaseContext()->mediumPosition() == 2
            && contextualRestored.releaseContext()->trackPosition() == 7,
            "release coordinate positions should round trip");
    require(contextualRestored.releaseContext()->recordingMbid()
                == contextual,
            "release context Recording MBID should round trip");

    require(restored.resumeIntent.has_value(),
            "playing lifecycle should restore inert resume intent");
    require(restored.resumeIntent->occurrenceId().value()
                == QStringLiteral("dup-b"),
            "resume intent should name exact active occurrence");
    require(restored.resumeIntent->playWhenReady(),
            "resume intent should preserve play-when-ready");
    require(restored.resumeIntent->positionMs().has_value()
            && *restored.resumeIntent->positionMs() == 42500,
            "latest pending seek target should become inert integer-ms resume bookmark");
    require(restored.coordinatorIntent.repeatMode == DurableRepeatMode::Queue,
            "coordinator-owned repeat queue intent should round trip");
    require(!restored.coordinatorIntent.pendingInput.has_value(),
            "active resumable lifecycle should not manufacture pending input");
    require(restored.displaySnapshots.size() == 1
                && restored.displaySnapshots.constFirst().occurrenceId.value()
                    == QStringLiteral("dup-b")
                && restored.displaySnapshots.constFirst().display.primaryText
                    == QStringLiteral("Last Known Duplicate")
                && restored.displaySnapshots.constFirst().display.artistCredit
                    == QStringList{QStringLiteral("Known Artist")},
            "last-known display snapshot should round trip as non-authoritative metadata");

    PlaybackSession freshSession;
    require(freshSession.generation() == 0,
            "restore must not manufacture a live BA2 generation");
    const PlaybackWorkToken freshToken = mustSession(
        freshSession.beginOccurrence(
            *restored.queue.activeOccurrence(),
            restored.resumeIntent->playWhenReady()),
        "post-restore BA2 lifecycle should begin freshly");
    require(freshToken.generation() == 1,
            "first post-restore lifecycle should mint generation one");
    const PlaybackMutationStatus staleOldLoad =
        freshSession.markLoaded(oldToken);
    require(!staleOldLoad.ok
            && staleOldLoad.error.code
                == PlaybackSessionErrorCode::StaleAsyncWork,
            "pre-restart work token must be stale against fresh lifecycle");
    require(freshSession.markLoaded(freshToken).ok,
            "fresh work token should complete fresh load");
    const PlaybackSeekRequest freshResumeSeek = mustSession(
        freshSession.requestSeek(
            static_cast<double>(*restored.resumeIntent->positionMs()) / 1000.0),
        "restored position should become a fresh BA2 seek request");
    require(freshResumeSeek.sequence() == 1,
            "post-restore seek sequence must restart freshly at one");
    require(freshResumeSeek.workToken() == freshToken,
            "post-restore seek must carry fresh work token");

    QJsonObject reordered = parseObject(snapshot);
    QJsonObject reorderedQueue =
        reordered.value(QStringLiteral("queue")).toObject();
    QJsonArray reorderedItems =
        reorderedQueue.value(QStringLiteral("items")).toArray();
    const QJsonValue firstItem = reorderedItems.at(0);
    reorderedItems[0] = reorderedItems.at(1);
    reorderedItems[1] = firstItem;
    reorderedQueue.insert(QStringLiteral("items"), reorderedItems);
    reordered.insert(QStringLiteral("queue"), reorderedQueue);
    RestoredSessionState reorderedState = mustPersistence(
        VinylSessionPersistence::restore(local, compact(reordered)),
        "reordered valid queue should restore");
    require(reorderedState.queue.items().at(0).occurrenceId().value()
                == QStringLiteral("dup-b"),
            "persisted array order should define restored queue order");
    require(reorderedState.queue.activeIndex() == 0
            && reorderedState.queue.activeOccurrenceId()->value()
                == QStringLiteral("dup-b"),
            "active occurrence should follow identity instead of old index");

    requireError(
        VinylSessionPersistence::restore(
            local, QByteArrayLiteral("{not-json")),
        SessionPersistenceErrorCode::MalformedSnapshot,
        "malformed JSON should fail closed");
    requireError(
        VinylSessionPersistence::restore(local, QByteArray()),
        SessionPersistenceErrorCode::EmptySnapshot,
        "empty snapshot should fail typed");

    QJsonObject legacyV1 = parseObject(snapshot);
    legacyV1.insert(QStringLiteral("version"), 1);
    legacyV1.remove(QStringLiteral("coordinator"));
    QJsonObject legacyQueue =
        legacyV1.value(QStringLiteral("queue")).toObject();
    legacyQueue.remove(QStringLiteral("shuffle"));
    QJsonArray legacyItems =
        legacyQueue.value(QStringLiteral("items")).toArray();
    for (int i = 0; i < legacyItems.size(); ++i) {
        QJsonObject item = legacyItems.at(i).toObject();
        item.remove(QStringLiteral("enqueueOrigin"));
        item.remove(QStringLiteral("displaySnapshot"));
        legacyItems[i] = item;
    }
    legacyQueue.insert(QStringLiteral("items"), legacyItems);
    legacyV1.insert(QStringLiteral("queue"), legacyQueue);
    QJsonObject legacyResume =
        legacyV1.value(QStringLiteral("resume")).toObject();
    legacyResume.remove(QStringLiteral("positionMs"));
    legacyResume.insert(QStringLiteral("positionSeconds"), 42.5);
    legacyV1.insert(QStringLiteral("resume"), legacyResume);

    const QByteArray legacyV1Bytes = compact(legacyV1);
    const RestoredSessionState upgradedV1 = mustPersistence(
        VinylSessionPersistence::restore(local, legacyV1Bytes),
        "supported schema v1 fixture should upgrade in memory");
    require(upgradedV1.queue.size() == 3
                && upgradedV1.queue.activeOccurrenceId().has_value()
                && upgradedV1.queue.activeOccurrenceId()->value()
                    == QStringLiteral("dup-b"),
            "v1 upgrade should preserve ordered queue and active occurrence");
    require(upgradedV1.queue.items().at(0).recordingMbid() == duplicate
                && upgradedV1.queue.items().at(1).recordingMbid() == duplicate
                && upgradedV1.queue.items().at(0).occurrenceId()
                    != upgradedV1.queue.items().at(1).occurrenceId(),
            "v1 upgrade should preserve duplicate recordings as distinct occurrences");
    require(upgradedV1.resumeIntent.has_value()
                && upgradedV1.resumeIntent->positionMs().has_value()
                && *upgradedV1.resumeIntent->positionMs() == 42500,
            "v1 floating-second bookmark should normalize to integer milliseconds");
    require(upgradedV1.coordinatorIntent.repeatMode == DurableRepeatMode::Off
                && !upgradedV1.coordinatorIntent.pendingInput.has_value(),
            "v1 upgrade should receive explicit safe coordinator defaults");
    require(upgradedV1.displaySnapshots.isEmpty()
                && !upgradedV1.queue.isShuffled()
                && !upgradedV1.queue.items().at(0).enqueueOrigin().has_value(),
            "v1 upgrade should not invent origin, display, or shuffle facts");

    const QString checkedInV1Path =
        QFileInfo(QString::fromLocal8Bit(__FILE__)).absolutePath()
        + QStringLiteral("/fixtures/ba3-session-v1.json");
    QFile checkedInV1File(checkedInV1Path);
    require(checkedInV1File.open(QIODevice::ReadOnly),
            "checked-in Wave 3 BA3 v1 fixture file should open");
    const RestoredSessionState checkedInV1 = mustPersistence(
        VinylSessionPersistence::restore(local, checkedInV1File.readAll()),
        "checked-in Wave 3 BA3 v1 snapshot should restore");
    require(checkedInV1.queue.size() == 2
                && checkedInV1.queue.activeOccurrenceId().has_value()
                && checkedInV1.queue.activeOccurrenceId()->value()
                    == QStringLiteral("wave3-v1-b"),
            "checked-in v1 snapshot must preserve ordered queue and active occurrence");
    require(checkedInV1.resumeIntent.has_value()
                && checkedInV1.resumeIntent->positionMs().has_value()
                && *checkedInV1.resumeIntent->positionMs() == 42500,
            "checked-in v1 snapshot must migrate floating seconds to integer milliseconds");
    require(checkedInV1.displaySnapshots.isEmpty()
                && !checkedInV1.queue.isShuffled()
                && !checkedInV1.coordinatorIntent.pendingInput.has_value(),
            "checked-in v1 snapshot must receive only safe current-reader defaults");

    QJsonObject legacyUnknown = legacyV1;
    legacyUnknown.insert(QStringLiteral("coordinator"), QJsonObject{});
    requireError(
        VinylSessionPersistence::restore(local, compact(legacyUnknown)),
        SessionPersistenceErrorCode::MalformedSnapshot,
        "v1 reader should reject fields outside declared v1 schema");

    QJsonObject wrongSchema = parseObject(snapshot);
    wrongSchema.insert(QStringLiteral("schema"),
                       QStringLiteral("other.schema"));
    requireError(
        VinylSessionPersistence::restore(local, compact(wrongSchema)),
        SessionPersistenceErrorCode::UnsupportedSchema,
        "unknown schema should fail typed");

    QJsonObject wrongVersion = parseObject(snapshot);
    wrongVersion.insert(QStringLiteral("version"), 99);
    requireError(
        VinylSessionPersistence::restore(local, compact(wrongVersion)),
        SessionPersistenceErrorCode::UnsupportedVersion,
        "unknown version should fail typed");

    requireError(
        VinylSessionPersistence::restore(other, snapshot),
        SessionPersistenceErrorCode::ProfileMismatch,
        "cross-profile restore should fail closed");

    QJsonObject duplicateIdRoot = parseObject(snapshot);
    QJsonObject duplicateIdQueue =
        duplicateIdRoot.value(QStringLiteral("queue")).toObject();
    QJsonArray duplicateIdItems =
        duplicateIdQueue.value(QStringLiteral("items")).toArray();
    QJsonObject duplicateSecond = duplicateIdItems.at(1).toObject();
    duplicateSecond.insert(QStringLiteral("occurrenceId"),
                           QStringLiteral("dup-a"));
    duplicateIdItems[1] = duplicateSecond;
    duplicateIdQueue.insert(QStringLiteral("items"), duplicateIdItems);
    duplicateIdRoot.insert(QStringLiteral("queue"), duplicateIdQueue);
    requireError(
        VinylSessionPersistence::restore(
            local, compact(duplicateIdRoot)),
        SessionPersistenceErrorCode::InvalidQueueState,
        "duplicate QueueOccurrenceId should fail closed");

    QJsonObject missingActiveRoot = parseObject(snapshot);
    QJsonObject missingActiveQueue =
        missingActiveRoot.value(QStringLiteral("queue")).toObject();
    missingActiveQueue.insert(QStringLiteral("activeOccurrenceId"),
                              QStringLiteral("missing-id"));
    missingActiveRoot.insert(QStringLiteral("queue"), missingActiveQueue);
    requireError(
        VinylSessionPersistence::restore(
            local, compact(missingActiveRoot)),
        SessionPersistenceErrorCode::InvalidQueueState,
        "unknown active occurrence should fail closed");

    QJsonObject mismatchResumeRoot = parseObject(snapshot);
    QJsonObject mismatchResume =
        mismatchResumeRoot.value(QStringLiteral("resume")).toObject();
    mismatchResume.insert(QStringLiteral("occurrenceId"),
                          QStringLiteral("dup-a"));
    mismatchResumeRoot.insert(QStringLiteral("resume"), mismatchResume);
    requireError(
        VinylSessionPersistence::restore(
            local, compact(mismatchResumeRoot)),
        SessionPersistenceErrorCode::InvalidResumeState,
        "resume intent for non-active occurrence should fail closed");

    QJsonObject injectedGenerationRoot = parseObject(snapshot);
    QJsonObject injectedResume =
        injectedGenerationRoot.value(QStringLiteral("resume")).toObject();
    injectedResume.insert(QStringLiteral("generation"), 2);
    injectedGenerationRoot.insert(
        QStringLiteral("resume"), injectedResume);
    requireError(
        VinylSessionPersistence::restore(
            local, compact(injectedGenerationRoot)),
        SessionPersistenceErrorCode::MalformedSnapshot,
        "injected live generation should be rejected as unknown v2 field");

    QJsonObject injectedProviderRoot = parseObject(snapshot);
    injectedProviderRoot.insert(QStringLiteral("providerCredentials"),
                                QStringLiteral("must-not-load"));
    requireError(
        VinylSessionPersistence::restore(
            local, compact(injectedProviderRoot)),
        SessionPersistenceErrorCode::MalformedSnapshot,
        "injected provider credential field should fail closed");

    PlaybackSession idleSession;
    const QByteArray idleSnapshot = mustPersistence(
        VinylSessionPersistence::capture(local, queue, idleSession),
        "idle BA2 state should still snapshot BA1 queue");
    const RestoredSessionState idleRestored = mustPersistence(
        VinylSessionPersistence::restore(local, idleSnapshot),
        "idle queue snapshot should restore");
    require(!idleRestored.resumeIntent.has_value(),
            "idle BA2 state must not manufacture resume work");
    require(idleRestored.queue.activeOccurrenceId().has_value()
            && idleRestored.queue.activeOccurrenceId()->value()
                == QStringLiteral("dup-b"),
            "BA1 active cursor should persist independently of BA2 idle state");

    SessionCoordinatorIntent pendingCoordinator;
    pendingCoordinator.repeatMode = DurableRepeatMode::Queue;
    pendingCoordinator.pendingInput = SessionPendingInputIntent{
        occurrenceId("dup-b"), true};
    const QByteArray pendingSnapshot = mustPersistence(
        VinylSessionPersistence::capture(
            local, queue, idleSession, pendingCoordinator),
        "idle active queue should persist coordinator awaiting-input intent");
    const RestoredSessionState pendingRestored = mustPersistence(
        VinylSessionPersistence::restore(local, pendingSnapshot),
        "pending-input snapshot should restore");
    require(pendingRestored.coordinatorIntent.repeatMode
                == DurableRepeatMode::Queue
            && pendingRestored.coordinatorIntent.pendingInput.has_value()
            && pendingRestored.coordinatorIntent.pendingInput->occurrenceId.value()
                == QStringLiteral("dup-b")
            && pendingRestored.coordinatorIntent.pendingInput->playWhenReady,
            "pending-input occurrence plus play intent should round trip");

    PlaybackSession endedSession;
    const PlaybackWorkToken endedToken = mustSession(
        endedSession.beginOccurrence(*queue.activeOccurrence(), true),
        "ended fixture should begin");
    require(endedSession.markLoaded(endedToken).ok,
            "ended fixture should load");
    require(endedSession.markEnded(endedToken).ok,
            "ended fixture should reach terminal Ended");
    const QByteArray endedSnapshot = mustPersistence(
        VinylSessionPersistence::capture(local, queue, endedSession),
        "terminal Ended session should snapshot queue without resume");
    const RestoredSessionState endedRestored = mustPersistence(
        VinylSessionPersistence::restore(local, endedSnapshot),
        "terminal queue snapshot should restore");
    require(!endedRestored.resumeIntent.has_value(),
            "Ended BA2 state must not become live resume work");

    VinylQueue shuffledQueue;
    require(shuffledQueue.append(occurrence("s-a", duplicate)).ok,
            "shuffle persistence append a");
    require(shuffledQueue.append(occurrence("s-b", contextual)).ok,
            "shuffle persistence append b");
    require(shuffledQueue.append(occurrence("s-c", duplicate)).ok,
            "shuffle persistence append c");
    require(shuffledQueue.append(occurrence("s-d", contextual)).ok,
            "shuffle persistence append d");
    require(shuffledQueue.append(occurrence("s-e", duplicate)).ok,
            "shuffle persistence append e");
    require(shuffledQueue.activate(occurrenceId("s-b")).ok,
            "shuffle persistence activate b");
    const QVector<QString> originalOrder{
        QStringLiteral("s-a"),
        QStringLiteral("s-b"),
        QStringLiteral("s-c"),
        QStringLiteral("s-d"),
        QStringLiteral("s-e")};
    require(shuffledQueue.shuffle(Q_UINT64_C(0x12345678)).ok,
            "shuffle persistence should materialize deterministic tail order");
    QVector<QString> shuffledVisible;
    for (const QueueOccurrence &item : shuffledQueue.items())
        shuffledVisible.append(item.occurrenceId().value());
    require(shuffledQueue.isShuffled()
                && shuffledQueue.shuffleOriginalOrder().has_value(),
            "shuffle persistence fixture should retain reversible shuffle state");
    PlaybackSession shuffledIdleSession;
    const QByteArray shuffledSnapshot = mustPersistence(
        VinylSessionPersistence::capture(
            local, shuffledQueue, shuffledIdleSession),
        "shuffled queue should capture original-order state");
    RestoredSessionState shuffledRestored = mustPersistence(
        VinylSessionPersistence::restore(local, shuffledSnapshot),
        "shuffled queue snapshot should restore");
    QVector<QString> restoredVisible;
    for (const QueueOccurrence &item : shuffledRestored.queue.items())
        restoredVisible.append(item.occurrenceId().value());
    require(restoredVisible == shuffledVisible
                && shuffledRestored.queue.isShuffled()
                && shuffledRestored.queue.shuffleOriginalOrder().has_value(),
            "restore should preserve visible shuffled order and remembered original order");
    require(shuffledRestored.queue.unshuffle().ok,
            "restored shuffled queue should remain reversibly unshufflable");
    QVector<QString> restoredOriginal;
    for (const QueueOccurrence &item : shuffledRestored.queue.items())
        restoredOriginal.append(item.occurrenceId().value());
    require(restoredOriginal == originalOrder,
            "unshuffle after restore should recover exact pre-shuffle order");

    VinylQueue mismatchedQueue = queue;
    require(mismatchedQueue.activate(occurrenceId("dup-a")).ok,
            "mismatch fixture should change BA1 active occurrence");
    const auto incoherentCapture = VinylSessionPersistence::capture(
        local, mismatchedQueue, originalSession);
    require(!incoherentCapture.ok()
            && incoherentCapture.error.code
                == SessionPersistenceErrorCode::InvalidResumeState,
            "incoherent active BA1/BA2 owners should fail capture");

    require(sessionPersistenceErrorCodeName(
                SessionPersistenceErrorCode::MalformedSnapshot)
                == QStringLiteral("malformed_snapshot"),
            "BA3 errors should expose stable snake_case names");
    require(VinylSessionPersistence::schemaName()
                == QStringLiteral("colosseum.vinyl.session-persistence"),
            "BA3 schema name should remain stable");


    const QString repairCase = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (repairCase.isEmpty() || repairCase == "pending-resume") {
        auto captured = VinylSessionPersistence::capture(local, queue, originalSession, pendingCoordinator);
        require(captured.ok(), "M0R pending replacement must coexist with resumable transport");
        auto both = VinylSessionPersistence::restore(local, *captured.value);
        require(both.ok() && both.value->resumeIntent.has_value()
            && both.value->coordinatorIntent.pendingInput.has_value(), "M0R both intents must round-trip");
    }
    if (repairCase.isEmpty() || repairCase == "display-budget") {
        displaySnapshots[0].display.primaryText = QString(4097, QLatin1Char('x'));
        require(!VinylSessionPersistence::capture(local, queue, originalSession, {}, displaySnapshots).ok(),
            "M0R oversized queue display must fail capture");
    }
    if (repairCase.isEmpty() || repairCase == "container-budget") {
        require(!VinylSessionPersistence::restore(local, snapshot + QByteArray(8 * 1024 * 1024, ' ')).ok(),
            "M0R oversized session container must fail before parse");
    }
    if (repairCase.isEmpty() || repairCase == "integer-boundary") {
        auto root = parseObject(snapshot); auto resume = root.value("resume").toObject();
        resume.insert("positionMs", QJsonValue(std::numeric_limits<qint64>::max())); root.insert("resume", resume);
        auto maxValue = VinylSessionPersistence::restore(local, compact(root));
        require(maxValue.ok() && maxValue.value->resumeIntent->positionMs() == std::numeric_limits<qint64>::max(),
            "M0R exact qint64 JSON boundary must not round through double");
        resume.insert("positionMs", QJsonValue(9223372036854775808.0)); root.insert("resume", resume);
        require(!VinylSessionPersistence::restore(local, compact(root)).ok(), "M0R out-of-range signed integer must fail");
        root.insert("version", QJsonValue(4294967298.0));
        require(!VinylSessionPersistence::restore(local, compact(root)).ok(), "M0R version must not wrap to supported value");
    }

    qInfo().noquote()
        << "VINYL_BA3_SESSION_PERSISTENCE_OK checks=" << g_checks;
    return 0;
}
