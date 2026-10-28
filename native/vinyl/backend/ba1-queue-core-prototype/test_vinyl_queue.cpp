#include "VinylQueue.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSet>

#include <cstdlib>
#include <optional>

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
T must(const vinyl::QueueResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::QueueOccurrenceId occurrenceId(const char *value)
{
    return must(
        vinyl::QueueOccurrenceId::parse(QString::fromLatin1(value)),
        "fixture occurrence ID should parse");
}

vinyl::RecordingMbid recording(const char *value)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(value)),
        "fixture Recording MBID should parse");
}

vinyl::QueueOccurrence occurrence(const char *id,
                                  const vinyl::RecordingMbid &recordingMbid,
                                  std::optional<vinyl::EnqueueOrigin> origin = std::nullopt)
{
    return must(
        vinyl::QueueOccurrence::create(
            occurrenceId(id), recordingMbid, std::nullopt, std::move(origin)),
        "fixture queue occurrence should construct");
}

vinyl::EnqueueBatchId batchId(const char *value)
{
    return must(
        vinyl::EnqueueBatchId::parse(QString::fromLatin1(value)),
        "fixture enqueue batch ID should parse");
}

QVector<QString> queueIds(const vinyl::VinylQueue &queue)
{
    QVector<QString> ids;
    for (const vinyl::QueueOccurrence &item : queue.items())
        ids.append(item.occurrenceId().value());
    return ids;
}

void requireActive(const vinyl::VinylQueue &queue, const char *id)
{
    require(queue.activeOccurrenceId().has_value(),
            "queue should have an active occurrence");
    require(queue.activeOccurrenceId()->value() == QString::fromLatin1(id),
            "active occurrence should match expected identity");
}

void requireInvariant(const vinyl::VinylQueue &queue)
{
    QSet<QString> occurrenceIds;
    for (const vinyl::QueueOccurrence &item : queue.items()) {
        require(!occurrenceIds.contains(item.occurrenceId().value()),
                "queue invariant: occurrence IDs must be unique");
        occurrenceIds.insert(item.occurrenceId().value());

        if (item.releaseContext().has_value()) {
            require(item.releaseContext()->recordingMbid() == item.recordingMbid(),
                    "queue invariant: release context must match occurrence Recording MBID");
        }
    }

    if (queue.activeOccurrenceId().has_value()) {
        require(queue.find(*queue.activeOccurrenceId()) != nullptr,
                "queue invariant: active occurrence must exist in queue");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    const RecordingMbid a = recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid b = recording("22222222-2222-4222-8222-222222222222");
    const RecordingMbid c = recording("33333333-3333-4333-8333-333333333333");
    const RecordingMbid d = recording("44444444-4444-4444-8444-444444444444");

    const auto playlistContainer = must(
        EnqueueContainerRef::playlist(QStringLiteral("playlist-1")),
        "playlist origin container should parse");
    const EnqueueOrigin playlistOrigin(
        EnqueueOriginKind::Playlist,
        batchId("batch-1"),
        playlistContainer);
    const QueueOccurrence originated = occurrence(
        "origin-a", a, playlistOrigin);
    require(originated.enqueueOrigin().has_value(),
            "queue occurrence should preserve optional enqueue origin");
    require(originated.enqueueOrigin()->kind() == EnqueueOriginKind::Playlist,
            "enqueue origin kind should remain descriptive metadata");
    require(originated.enqueueOrigin()->batchId().value()
                == QStringLiteral("batch-1"),
            "enqueue batch identity should be preserved");
    require(originated.enqueueOrigin()->container().has_value()
                && originated.enqueueOrigin()->container()->kind()
                    == EnqueueContainerKind::Playlist
                && originated.enqueueOrigin()->container()->playlistId()
                    == QStringLiteral("playlist-1"),
            "playlist container context should round-trip in the occurrence");
    require(!EnqueueBatchId::parse(QStringLiteral(" bad ")).ok(),
            "unsafe enqueue batch ID should fail closed");
    require(enqueueOriginKindName(EnqueueOriginKind::Radio)
                == QStringLiteral("radio"),
            "enqueue origin kinds should expose stable names");

    const auto invalidId = QueueOccurrenceId::parse(QStringLiteral(" bad "));
    require(!invalidId.ok(), "occurrence ID with surrounding whitespace should fail");
    require(invalidId.error.code == QueueErrorCode::InvalidOccurrenceId,
            "invalid occurrence ID should use typed error");

    VinylQueue duplicateQueue;
    require(duplicateQueue.append(occurrence("a-1", a)).ok,
            "first occurrence should append");
    require(duplicateQueue.append(occurrence("a-2", a)).ok,
            "same Recording MBID should append under another occurrence ID");
    require(duplicateQueue.size() == 2,
            "duplicate Recording MBIDs should remain two queue occurrences");
    require(duplicateQueue.items().at(0).recordingMbid() == a
                && duplicateQueue.items().at(1).recordingMbid() == a,
            "duplicate occurrences should preserve the same canonical Recording MBID");

    const QueueMutationStatus duplicateId =
        duplicateQueue.append(occurrence("a-1", b));
    require(!duplicateId.ok,
            "duplicate occurrence ID should fail even for another Recording MBID");
    require(duplicateId.error.code == QueueErrorCode::DuplicateOccurrenceId,
            "duplicate occurrence ID should use typed error");

    require(duplicateQueue.remove(occurrenceId("a-1")).ok,
            "removing one duplicate occurrence should succeed");
    require(duplicateQueue.size() == 1,
            "removing one duplicate should leave the other");
    require(duplicateQueue.items().constFirst().occurrenceId().value()
                == QStringLiteral("a-2"),
            "the other duplicate occurrence must remain untouched");
    requireInvariant(duplicateQueue);

    VinylQueue moveQueue;
    require(moveQueue.append(occurrence("a", a)).ok, "append a");
    require(moveQueue.append(occurrence("b", b)).ok, "append b");
    require(moveQueue.append(occurrence("a-again", a)).ok, "append duplicate a");
    require(moveQueue.activate(occurrenceId("b")).ok, "activate b");
    require(moveQueue.move(occurrenceId("a-again"), 0).ok,
            "moving one duplicate should succeed");
    require(moveQueue.items().at(0).occurrenceId().value()
                == QStringLiteral("a-again"),
            "only the requested duplicate should move");
    require(moveQueue.items().at(1).occurrenceId().value()
                == QStringLiteral("a"),
            "other duplicate should keep its relative identity");
    requireActive(moveQueue, "b");
    require(moveQueue.activeIndex() == 2,
            "active occurrence identity should survive index changes");

    const QueueMutationStatus sameMove =
        moveQueue.move(occurrenceId("b"), moveQueue.activeIndex());
    require(sameMove.ok && !sameMove.changed,
            "moving an occurrence to its current index should be a successful no-op");
    requireInvariant(moveQueue);

    VinylQueue nextQueue;
    require(nextQueue.append(occurrence("a", a)).ok, "append a");
    require(nextQueue.append(occurrence("b", b)).ok, "append b");
    require(nextQueue.activate(occurrenceId("a")).ok, "activate a");
    require(nextQueue.playNext(occurrence("c", c)).ok, "playNext c");
    require(nextQueue.items().at(1).occurrenceId().value() == QStringLiteral("c"),
            "Play Next should insert directly after active");
    requireActive(nextQueue, "a");

    require(nextQueue.playNow(occurrence("d", d)).ok, "playNow d");
    require(nextQueue.items().at(1).occurrenceId().value() == QStringLiteral("d"),
            "Play Now should insert directly after old active");
    requireActive(nextQueue, "d");
    require(nextQueue.items().at(0).occurrenceId().value() == QStringLiteral("a"),
            "Play Now should not delete the previous active occurrence");
    requireInvariant(nextQueue);

    VinylQueue noActiveQueue;
    require(noActiveQueue.append(occurrence("a", a)).ok, "append a");
    require(noActiveQueue.playNext(occurrence("b", b)).ok,
            "Play Next without active should insert at head");
    require(noActiveQueue.items().constFirst().occurrenceId().value()
                == QStringLiteral("b"),
            "Play Next without active should become first pending occurrence");
    require(!noActiveQueue.activeOccurrenceId().has_value(),
            "Play Next must not create active state");
    require(noActiveQueue.playNow(occurrence("c", c)).ok,
            "Play Now without active should insert at head");
    requireActive(noActiveQueue, "c");
    require(noActiveQueue.items().constFirst().occurrenceId().value()
                == QStringLiteral("c"),
            "Play Now without active should become head and active");
    requireInvariant(noActiveQueue);

    VinylQueue clearQueue;
    require(clearQueue.append(occurrence("a", a)).ok, "append a");
    require(clearQueue.append(occurrence("b", b)).ok, "append b");
    require(clearQueue.append(occurrence("c", c)).ok, "append c");
    require(clearQueue.append(occurrence("d", d)).ok, "append d");
    require(clearQueue.activate(occurrenceId("b")).ok, "activate b");
    require(clearQueue.clearAfterCurrent().ok,
            "clearAfterCurrent should succeed with active item");
    require(clearQueue.size() == 2,
            "clearAfterCurrent should remove only following occurrences");
    require(clearQueue.items().at(0).occurrenceId().value() == QStringLiteral("a")
                && clearQueue.items().at(1).occurrenceId().value()
                    == QStringLiteral("b"),
            "clearAfterCurrent should preserve items through active occurrence");
    requireActive(clearQueue, "b");
    const QueueMutationStatus clearNoop = clearQueue.clearAfterCurrent();
    require(clearNoop.ok && !clearNoop.changed,
            "clearAfterCurrent at tail should be a successful no-op");
    requireInvariant(clearQueue);

    VinylQueue noAnchor;
    require(noAnchor.append(occurrence("a", a)).ok, "append a");
    const QueueMutationStatus noAnchorStatus = noAnchor.clearAfterCurrent();
    require(!noAnchorStatus.ok,
            "clearAfterCurrent without active occurrence should fail");
    require(noAnchorStatus.error.code == QueueErrorCode::NoActiveOccurrence,
            "clearAfterCurrent without active should use typed error");

    VinylQueue removeActive;
    require(removeActive.append(occurrence("a", a)).ok, "append a");
    require(removeActive.append(occurrence("b", b)).ok, "append b");
    require(removeActive.append(occurrence("c", c)).ok, "append c");
    require(removeActive.activate(occurrenceId("b")).ok, "activate b");
    require(removeActive.remove(occurrenceId("b")).ok, "remove active b");
    requireActive(removeActive, "c");
    require(removeActive.activeIndex() == 1,
            "active removal should select successor at same index");

    require(removeActive.remove(occurrenceId("c")).ok, "remove active tail c");
    requireActive(removeActive, "a");
    require(removeActive.activeIndex() == 0,
            "active tail removal should select predecessor");

    require(removeActive.remove(occurrenceId("a")).ok, "remove final active a");
    require(removeActive.empty(), "queue should become empty");
    require(!removeActive.activeOccurrenceId().has_value(),
            "removing final active occurrence should clear active state");
    requireInvariant(removeActive);

    const QueueMutationStatus missingRemove =
        removeActive.remove(occurrenceId("missing"));
    require(!missingRemove.ok,
            "unknown occurrence removal should not be a silent no-op");
    require(missingRemove.error.code == QueueErrorCode::OccurrenceNotFound,
            "unknown occurrence removal should use typed error");

    VinylQueue rangeQueue;
    require(rangeQueue.append(occurrence("a", a)).ok, "append a");
    const QueueMutationStatus badInsert =
        rangeQueue.insert(3, occurrence("b", b));
    require(!badInsert.ok && badInsert.error.code == QueueErrorCode::IndexOutOfRange,
            "insert outside 0..size should fail");
    const QueueMutationStatus badMove =
        rangeQueue.move(occurrenceId("a"), 1);
    require(!badMove.ok && badMove.error.code == QueueErrorCode::IndexOutOfRange,
            "move destination outside final queue indices should fail");

    const ReleaseGroupMbid rg = mustDomain(
        ReleaseGroupMbid::parse(QStringLiteral("55555555-5555-4555-8555-555555555555")),
        "release-group fixture should parse");
    const ReleaseMbid release = mustDomain(
        ReleaseMbid::parse(QStringLiteral("66666666-6666-4666-8666-666666666666")),
        "release fixture should parse");
    const ReleaseTrackMbid rt = mustDomain(
        ReleaseTrackMbid::parse(QStringLiteral("77777777-7777-4777-8777-777777777777")),
        "release-track fixture should parse");
    const auto coordinate = mustDomain(
        ReleaseTrackCoordinate::create(
            rg,
            release,
            std::optional<ReleaseTrackMbid>{rt},
            a,
            1,
            4),
        "release context fixture should construct");

    const QueueOccurrence contextual = must(
        QueueOccurrence::create(
            occurrenceId("context-a"),
            a,
            std::optional<ReleaseTrackCoordinate>{coordinate}),
        "matching release context should construct");
    require(contextual.releaseContext().has_value(),
            "release context should remain attached to occurrence");
    require(contextual.releaseContext()->trackPosition() == 4,
            "release context track position should be preserved");

    const auto mismatched = QueueOccurrence::create(
        occurrenceId("context-bad"),
        b,
        std::optional<ReleaseTrackCoordinate>{coordinate});
    require(!mismatched.ok(),
            "release context for another Recording MBID should fail");
    require(mismatched.error.code == QueueErrorCode::RecordingContextMismatch,
            "recording/context mismatch should use typed error");

    VinylQueue contextualQueue;
    require(contextualQueue.append(contextual).ok,
            "contextual occurrence should append");
    requireInvariant(contextualQueue);

    VinylQueue advanceQueue;
    require(advanceQueue.append(occurrence("a", a)).ok, "append a");
    require(advanceQueue.append(occurrence("b", b)).ok, "append b");
    const QueueMutationStatus inactiveAdvance = advanceQueue.advance();
    require(!inactiveAdvance.ok,
            "advance without an active occurrence should fail rather than auto-start");
    require(inactiveAdvance.error.code == QueueErrorCode::NoActiveOccurrence,
            "inactive advance should use typed no-active error");
    require(advanceQueue.activate(occurrenceId("a")).ok,
            "session owner may explicitly activate first occurrence");
    require(advanceQueue.advance().ok,
            "advance should select successor");
    requireActive(advanceQueue, "b");
    require(advanceQueue.advance().ok,
            "advance at tail should terminate active cursor");
    require(!advanceQueue.activeOccurrenceId().has_value(),
            "advance at tail should leave queue inactive");
    const QueueMutationStatus noWrapAdvance = advanceQueue.advance();
    require(!noWrapAdvance.ok,
            "advance after queue exhaustion must not wrap to the first occurrence");
    require(noWrapAdvance.error.code == QueueErrorCode::NoActiveOccurrence,
            "post-exhaustion advance should remain explicitly inactive");

    VinylQueue emptyAdvance;
    const QueueMutationStatus emptyAdvanceStatus = emptyAdvance.advance();
    require(!emptyAdvanceStatus.ok,
            "advance on empty queue should fail");
    require(emptyAdvanceStatus.error.code == QueueErrorCode::NoActiveOccurrence,
            "empty advance should use typed no-active error");
    requireInvariant(advanceQueue);

    VinylQueue stableActive;
    require(stableActive.append(occurrence("a", a)).ok, "append a");
    require(stableActive.append(occurrence("b", b)).ok, "append b");
    require(stableActive.activate(occurrenceId("b")).ok, "activate b");
    require(stableActive.insert(0, occurrence("c", c)).ok,
            "insert before active should succeed");
    requireActive(stableActive, "b");
    require(stableActive.activeIndex() == 2,
            "insert before active should not retarget active occurrence");
    require(stableActive.remove(occurrenceId("c")).ok,
            "remove unrelated occurrence should succeed");
    requireActive(stableActive, "b");

    VinylQueue shuffleA;
    VinylQueue shuffleB;
    for (VinylQueue *target : {&shuffleA, &shuffleB}) {
        require(target->append(occurrence("s-a", a)).ok, "shuffle append a");
        require(target->append(occurrence("s-b", b)).ok, "shuffle append b");
        require(target->append(occurrence("s-c", c)).ok, "shuffle append c");
        require(target->append(occurrence("s-d", d)).ok, "shuffle append d");
        require(target->append(occurrence("s-e", a)).ok, "shuffle append e");
        require(target->activate(occurrenceId("s-b")).ok,
                "shuffle fixture should activate b");
    }

    const QVector<QString> originalShuffleOrder = queueIds(shuffleA);
    require(shuffleA.shuffle(0x12345678ULL).ok,
            "shuffle should materialize with host-supplied seed");
    require(shuffleB.shuffle(0x12345678ULL).ok,
            "same seed should materialize second queue");
    require(queueIds(shuffleA) == queueIds(shuffleB),
            "same queue plus same seed should produce deterministic order");
    require(shuffleA.isShuffled()
                && shuffleA.shuffleOriginalOrder().has_value(),
            "shuffle should remember original order for unshuffle");
    require(shuffleA.items().at(0).occurrenceId().value()
                == QStringLiteral("s-a")
                && shuffleA.items().at(1).occurrenceId().value()
                    == QStringLiteral("s-b"),
            "shuffle must leave active occurrence and prior visible history in place");
    requireActive(shuffleA, "s-b");

    require(shuffleA.append(occurrence("s-f", b)).ok,
            "append while shuffled should keep reversible original-order state");
    require(shuffleA.unshuffle().ok,
            "unshuffle should restore remembered original order");
    QVector<QString> expectedUnshuffle = originalShuffleOrder;
    expectedUnshuffle.append(QStringLiteral("s-f"));
    require(queueIds(shuffleA) == expectedUnshuffle,
            "unshuffle should restore pre-shuffle order plus later append");
    requireActive(shuffleA, "s-b");
    require(!shuffleA.isShuffled(),
            "unshuffle should clear remembered shuffle state");

    QVector<QueueOccurrenceId> invalidOriginal;
    invalidOriginal.append(occurrenceId("s-a"));
    const QueueMutationStatus invalidShuffleRestore =
        shuffleA.restoreShuffleOriginalOrder(invalidOriginal);
    require(!invalidShuffleRestore.ok
                && invalidShuffleRestore.error.code
                    == QueueErrorCode::InvalidShuffleState,
            "persisted shuffle original order must be an exact permutation");

    const QueueMutationStatus clearStatus = stableActive.clear();
    require(clearStatus.ok && clearStatus.changed, "clear populated queue should mutate");
    require(stableActive.empty(), "clear should remove every occurrence");
    require(!stableActive.activeOccurrenceId().has_value(),
            "clear should remove active state");
    const QueueMutationStatus clearAgain = stableActive.clear();
    require(clearAgain.ok && !clearAgain.changed,
            "clear empty queue should be a successful no-op");
    requireInvariant(stableActive);

    require(queueErrorCodeName(QueueErrorCode::DuplicateOccurrenceId)
                == QStringLiteral("duplicate_occurrence_id"),
            "queue errors should expose stable snake_case names");

    qInfo().noquote() << "VINYL_BA1_QUEUE_OK checks=" << g_checks;
    return 0;
}
