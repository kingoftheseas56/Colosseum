#include "VinylQueue.h"

#include <QChar>
#include <QSet>

namespace vinyl {

QString queueErrorCodeName(QueueErrorCode code)
{
    switch (code) {
    case QueueErrorCode::None:
        return QStringLiteral("none");
    case QueueErrorCode::InvalidOccurrenceId:
        return QStringLiteral("invalid_occurrence_id");
    case QueueErrorCode::InvalidLocalIdentity:
        return QStringLiteral("invalid_local_identity");
    case QueueErrorCode::DuplicateOccurrenceId:
        return QStringLiteral("duplicate_occurrence_id");
    case QueueErrorCode::OccurrenceNotFound:
        return QStringLiteral("occurrence_not_found");
    case QueueErrorCode::IndexOutOfRange:
        return QStringLiteral("index_out_of_range");
    case QueueErrorCode::RecordingContextMismatch:
        return QStringLiteral("recording_context_mismatch");
    case QueueErrorCode::NoActiveOccurrence:
        return QStringLiteral("no_active_occurrence");
    case QueueErrorCode::InvalidEnqueueOrigin:
        return QStringLiteral("invalid_enqueue_origin");
    case QueueErrorCode::InvalidShuffleState:
        return QStringLiteral("invalid_shuffle_state");
    }
    return QStringLiteral("unknown");
}

QueueResult<QueueOccurrenceId> QueueOccurrenceId::parse(const QString &text)
{
    if (text.isEmpty() || text.size() > 128 || text.trimmed() != text) {
        return QueueResult<QueueOccurrenceId>::failure(
            QueueErrorCode::InvalidOccurrenceId,
            QStringLiteral("occurrenceId"),
            QStringLiteral("Occurrence ID must be non-empty, bounded, and have no surrounding whitespace."));
    }

    for (const QChar ch : text) {
        if (ch.isNull() || ch.category() == QChar::Other_Control) {
            return QueueResult<QueueOccurrenceId>::failure(
                QueueErrorCode::InvalidOccurrenceId,
                QStringLiteral("occurrenceId"),
                QStringLiteral("Occurrence ID must not contain control characters."));
        }
    }

    return QueueResult<QueueOccurrenceId>::success(QueueOccurrenceId(text));
}

QString enqueueOriginKindName(EnqueueOriginKind kind)
{
    switch (kind) {
    case EnqueueOriginKind::User:
        return QStringLiteral("user");
    case EnqueueOriginKind::Album:
        return QStringLiteral("album");
    case EnqueueOriginKind::Playlist:
        return QStringLiteral("playlist");
    case EnqueueOriginKind::Radio:
        return QStringLiteral("radio");
    case EnqueueOriginKind::Restore:
        return QStringLiteral("restore");
    }
    return QStringLiteral("unknown");
}

QueueResult<EnqueueBatchId> EnqueueBatchId::parse(const QString &text)
{
    if (text.isEmpty() || text.size() > 128 || text.trimmed() != text) {
        return QueueResult<EnqueueBatchId>::failure(
            QueueErrorCode::InvalidEnqueueOrigin,
            QStringLiteral("enqueueOrigin.batchId"),
            QStringLiteral("Enqueue batch ID must be a non-empty bounded opaque token."));
    }
    for (const QChar ch : text) {
        if (ch.isNull() || ch.category() == QChar::Other_Control) {
            return QueueResult<EnqueueBatchId>::failure(
                QueueErrorCode::InvalidEnqueueOrigin,
                QStringLiteral("enqueueOrigin.batchId"),
                QStringLiteral("Enqueue batch ID must not contain control characters."));
        }
    }
    return QueueResult<EnqueueBatchId>::success(EnqueueBatchId(text));
}

QString enqueueContainerKindName(EnqueueContainerKind kind)
{
    switch (kind) {
    case EnqueueContainerKind::CanonicalEntity:
        return QStringLiteral("canonical");
    case EnqueueContainerKind::Playlist:
        return QStringLiteral("playlist");
    }
    return QStringLiteral("unknown");
}

EnqueueContainerRef EnqueueContainerRef::canonical(CanonicalEntityRef entity)
{
    return EnqueueContainerRef(
        EnqueueContainerKind::CanonicalEntity, std::move(entity), QString());
}

QueueResult<EnqueueContainerRef> EnqueueContainerRef::playlist(
    const QString &playlistId)
{
    if (playlistId.isEmpty() || playlistId.size() > 128
        || playlistId.trimmed() != playlistId) {
        return QueueResult<EnqueueContainerRef>::failure(
            QueueErrorCode::InvalidEnqueueOrigin,
            QStringLiteral("enqueueOrigin.container.playlistId"),
            QStringLiteral("Playlist container ID must be a non-empty bounded opaque token."));
    }
    for (const QChar ch : playlistId) {
        if (ch.isNull() || ch.category() == QChar::Other_Control) {
            return QueueResult<EnqueueContainerRef>::failure(
                QueueErrorCode::InvalidEnqueueOrigin,
                QStringLiteral("enqueueOrigin.container.playlistId"),
                QStringLiteral("Playlist container ID must not contain control characters."));
        }
    }
    return QueueResult<EnqueueContainerRef>::success(
        EnqueueContainerRef(
            EnqueueContainerKind::Playlist, std::nullopt, playlistId));
}

QueueResult<QueueOccurrence> QueueOccurrence::create(
    QueueOccurrenceId occurrenceId,
    RecordingMbid recording,
    std::optional<ReleaseTrackCoordinate> releaseContext,
    std::optional<EnqueueOrigin> enqueueOrigin)
{
    if (releaseContext.has_value()
        && releaseContext->recordingMbid() != recording) {
        return QueueResult<QueueOccurrence>::failure(
            QueueErrorCode::RecordingContextMismatch,
            QStringLiteral("releaseContext.recordingMbid"),
            QStringLiteral("Release context must refer to the same Recording MBID as the queue occurrence."));
    }

    return QueueResult<QueueOccurrence>::success(
        QueueOccurrence(std::move(occurrenceId),
                        QString(),
                        std::optional<RecordingMbid>(std::move(recording)),
                        std::move(releaseContext),
                        std::move(enqueueOrigin)));
}

QueueResult<QueueOccurrence> QueueOccurrence::createLocal(
    QueueOccurrenceId occurrenceId,
    const QString &localIdentity,
    std::optional<RecordingMbid> verifiedRecording,
    std::optional<ReleaseTrackCoordinate> releaseContext,
    std::optional<EnqueueOrigin> enqueueOrigin)
{
    if (localIdentity.isEmpty() || localIdentity.size() > 2048
        || localIdentity.trimmed() != localIdentity) {
        return QueueResult<QueueOccurrence>::failure(
            QueueErrorCode::InvalidLocalIdentity,
            QStringLiteral("localIdentity"),
            QStringLiteral("Local identity must be non-empty, bounded, and have no surrounding whitespace."));
    }

    for (const QChar ch : localIdentity) {
        if (ch.isNull() || ch.category() == QChar::Other_Control) {
            return QueueResult<QueueOccurrence>::failure(
                QueueErrorCode::InvalidLocalIdentity,
                QStringLiteral("localIdentity"),
                QStringLiteral("Local identity must not contain control characters."));
        }
    }

    if (releaseContext.has_value()
        && (!verifiedRecording.has_value()
            || releaseContext->recordingMbid() != *verifiedRecording)) {
        return QueueResult<QueueOccurrence>::failure(
            QueueErrorCode::RecordingContextMismatch,
            QStringLiteral("releaseContext.recordingMbid"),
            QStringLiteral("Release context requires and must match a verified Recording MBID mapping."));
    }

    return QueueResult<QueueOccurrence>::success(
        QueueOccurrence(std::move(occurrenceId),
                        localIdentity,
                        std::move(verifiedRecording),
                        std::move(releaseContext),
                        std::move(enqueueOrigin)));
}

bool QueueOccurrence::sameMusicIdentity(
    const QueueOccurrence &other) const noexcept
{
    if (!m_localIdentity.isEmpty() || !other.m_localIdentity.isEmpty())
        return m_localIdentity == other.m_localIdentity
            && m_canonicalRecording == other.m_canonicalRecording;

    if (m_canonicalRecording.has_value()
        && other.m_canonicalRecording.has_value()) {
        return *m_canonicalRecording == *other.m_canonicalRecording;
    }

    return false;
}

int VinylQueue::indexOf(const QueueOccurrenceId &id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).occurrenceId() == id)
            return i;
    }
    return -1;
}

int VinylQueue::originalIndexOf(const QueueOccurrenceId &id) const
{
    if (!m_originalOrder.has_value())
        return -1;
    for (int i = 0; i < m_originalOrder->size(); ++i) {
        if (m_originalOrder->at(i) == id)
            return i;
    }
    return -1;
}

bool VinylQueue::containsId(const QueueOccurrenceId &id) const
{
    return indexOf(id) >= 0;
}

int VinylQueue::activeIndex() const
{
    return m_activeId.has_value() ? indexOf(*m_activeId) : -1;
}

const QueueOccurrence *VinylQueue::activeOccurrence() const
{
    const int index = activeIndex();
    return index >= 0 ? &m_items.at(index) : nullptr;
}

const QueueOccurrence *VinylQueue::find(const QueueOccurrenceId &id) const
{
    const int index = indexOf(id);
    return index >= 0 ? &m_items.at(index) : nullptr;
}

QueueMutationStatus VinylQueue::validateNewOccurrence(
    const QueueOccurrence &occurrence) const
{
    if (containsId(occurrence.occurrenceId())) {
        return QueueMutationStatus::failure(
            QueueErrorCode::DuplicateOccurrenceId,
            QStringLiteral("occurrenceId"),
            QStringLiteral("Queue occurrence IDs must be unique inside one queue."));
    }
    return QueueMutationStatus::success(false);
}

QueueMutationStatus VinylQueue::validateOriginalOrder(
    const QVector<QueueOccurrenceId> &originalOrder) const
{
    if (originalOrder.size() != m_items.size()) {
        return QueueMutationStatus::failure(
            QueueErrorCode::InvalidShuffleState,
            QStringLiteral("shuffle.originalOrder"),
            QStringLiteral("Original order must contain exactly the current queue occurrences."));
    }

    QSet<QString> ids;
    for (const QueueOccurrenceId &id : originalOrder) {
        if (ids.contains(id.value()) || !containsId(id)) {
            return QueueMutationStatus::failure(
                QueueErrorCode::InvalidShuffleState,
                QStringLiteral("shuffle.originalOrder"),
                QStringLiteral("Original order must be a duplicate-free permutation of the current queue."));
        }
        ids.insert(id.value());
    }
    return QueueMutationStatus::success(false);
}

void VinylQueue::insertIntoOriginalOrder(
    int index, const QueueOccurrenceId &id)
{
    if (!m_originalOrder.has_value())
        return;
    const int bounded = qBound(0, index, m_originalOrder->size());
    m_originalOrder->insert(bounded, id);
}

void VinylQueue::removeFromOriginalOrder(const QueueOccurrenceId &id)
{
    const int index = originalIndexOf(id);
    if (index >= 0)
        m_originalOrder->removeAt(index);
}

QueueMutationStatus VinylQueue::append(QueueOccurrence occurrence)
{
    const QueueMutationStatus valid = validateNewOccurrence(occurrence);
    if (!valid.ok)
        return valid;

    const QueueOccurrenceId newId = occurrence.occurrenceId();
    m_items.append(std::move(occurrence));
    if (m_originalOrder.has_value())
        m_originalOrder->append(newId);
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::insert(int index, QueueOccurrence occurrence)
{
    if (index < 0 || index > m_items.size()) {
        return QueueMutationStatus::failure(
            QueueErrorCode::IndexOutOfRange,
            QStringLiteral("index"),
            QStringLiteral("Insert index must be between zero and queue size inclusive."));
    }

    const QueueMutationStatus valid = validateNewOccurrence(occurrence);
    if (!valid.ok)
        return valid;

    const QueueOccurrenceId newId = occurrence.occurrenceId();
    m_items.insert(index, std::move(occurrence));
    insertIntoOriginalOrder(index, newId);
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::playNow(QueueOccurrence occurrence)
{
    const QueueMutationStatus valid = validateNewOccurrence(occurrence);
    if (!valid.ok)
        return valid;

    const QueueOccurrenceId newId = occurrence.occurrenceId();
    const int currentIndex = activeIndex();
    const int insertionIndex = currentIndex >= 0 ? currentIndex + 1 : 0;
    const int originalInsertionIndex =
        currentIndex >= 0 && m_originalOrder.has_value()
        ? originalIndexOf(*m_activeId) + 1
        : insertionIndex;

    m_items.insert(insertionIndex, std::move(occurrence));
    insertIntoOriginalOrder(originalInsertionIndex, newId);
    m_activeId = newId;
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::playNext(QueueOccurrence occurrence)
{
    const QueueMutationStatus valid = validateNewOccurrence(occurrence);
    if (!valid.ok)
        return valid;

    const QueueOccurrenceId newId = occurrence.occurrenceId();
    const int currentIndex = activeIndex();
    const int insertionIndex = currentIndex >= 0 ? currentIndex + 1 : 0;
    const int originalInsertionIndex =
        currentIndex >= 0 && m_originalOrder.has_value()
        ? originalIndexOf(*m_activeId) + 1
        : insertionIndex;
    m_items.insert(insertionIndex, std::move(occurrence));
    insertIntoOriginalOrder(originalInsertionIndex, newId);
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::remove(const QueueOccurrenceId &id)
{
    const int index = indexOf(id);
    if (index < 0) {
        return QueueMutationStatus::failure(
            QueueErrorCode::OccurrenceNotFound,
            QStringLiteral("occurrenceId"),
            QStringLiteral("Queue occurrence does not exist."));
    }

    const bool removedActive = m_activeId.has_value() && *m_activeId == id;
    m_items.removeAt(index);
    removeFromOriginalOrder(id);

    if (removedActive) {
        if (m_items.isEmpty()) {
            m_activeId.reset();
        } else if (index < m_items.size()) {
            m_activeId = m_items.at(index).occurrenceId();
        } else {
            m_activeId = m_items.constLast().occurrenceId();
        }
    }

    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::move(const QueueOccurrenceId &id,
                                     int destinationIndex)
{
    const int sourceIndex = indexOf(id);
    if (sourceIndex < 0) {
        return QueueMutationStatus::failure(
            QueueErrorCode::OccurrenceNotFound,
            QStringLiteral("occurrenceId"),
            QStringLiteral("Queue occurrence does not exist."));
    }

    if (destinationIndex < 0 || destinationIndex >= m_items.size()) {
        return QueueMutationStatus::failure(
            QueueErrorCode::IndexOutOfRange,
            QStringLiteral("destinationIndex"),
            QStringLiteral("Move destination must name a final queue index."));
    }

    if (sourceIndex == destinationIndex)
        return QueueMutationStatus::success(false);

    QueueOccurrence occurrence = m_items.takeAt(sourceIndex);
    m_items.insert(destinationIndex, std::move(occurrence));
    if (m_originalOrder.has_value()) {
        removeFromOriginalOrder(id);
        insertIntoOriginalOrder(destinationIndex, id);
    }
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::clear()
{
    if (m_items.isEmpty() && !m_activeId.has_value())
        return QueueMutationStatus::success(false);

    m_items.clear();
    m_activeId.reset();
    m_originalOrder.reset();
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::clearAfterCurrent()
{
    const int currentIndex = activeIndex();
    if (currentIndex < 0) {
        return QueueMutationStatus::failure(
            QueueErrorCode::NoActiveOccurrence,
            QStringLiteral("activeOccurrenceId"),
            QStringLiteral("Clear-after-current requires an active queue occurrence."));
    }

    if (currentIndex == m_items.size() - 1)
        return QueueMutationStatus::success(false);

    QVector<QueueOccurrenceId> removedIds;
    for (int i = currentIndex + 1; i < m_items.size(); ++i)
        removedIds.append(m_items.at(i).occurrenceId());

    m_items.erase(m_items.begin() + currentIndex + 1, m_items.end());
    for (const QueueOccurrenceId &id : removedIds)
        removeFromOriginalOrder(id);
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::activate(const QueueOccurrenceId &id)
{
    if (!containsId(id)) {
        return QueueMutationStatus::failure(
            QueueErrorCode::OccurrenceNotFound,
            QStringLiteral("occurrenceId"),
            QStringLiteral("Cannot activate an occurrence that is not in the queue."));
    }

    if (m_activeId.has_value() && *m_activeId == id)
        return QueueMutationStatus::success(false);

    m_activeId = id;
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::deactivate()
{
    if (!m_activeId.has_value())
        return QueueMutationStatus::success(false);

    m_activeId.reset();
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::advance()
{
    const int currentIndex = activeIndex();
    if (currentIndex < 0) {
        return QueueMutationStatus::failure(
            QueueErrorCode::NoActiveOccurrence,
            QStringLiteral("activeOccurrenceId"),
            QStringLiteral("Advance requires an active queue occurrence."));
    }

    if (currentIndex + 1 < m_items.size()) {
        m_activeId = m_items.at(currentIndex + 1).occurrenceId();
        return QueueMutationStatus::success();
    }

    m_activeId.reset();
    return QueueMutationStatus::success();
}

QueueMutationStatus VinylQueue::shuffle(quint64 seed)
{
    const int firstShuffleIndex = m_activeId.has_value()
        ? activeIndex() + 1
        : 0;
    if (firstShuffleIndex < 0 || m_items.size() - firstShuffleIndex < 2)
        return QueueMutationStatus::success(false);

    const bool newlyShuffled = !m_originalOrder.has_value();
    if (newlyShuffled) {
        QVector<QueueOccurrenceId> original;
        original.reserve(m_items.size());
        for (const QueueOccurrence &item : m_items)
            original.append(item.occurrenceId());
        m_originalOrder = std::move(original);
    }

    QVector<QueueOccurrenceId> before;
    before.reserve(m_items.size());
    for (const QueueOccurrence &item : m_items)
        before.append(item.occurrenceId());

    quint64 state = seed;
    const auto nextRandom = [&state]() {
        state += Q_UINT64_C(0x9E3779B97F4A7C15);
        quint64 z = state;
        z = (z ^ (z >> 30)) * Q_UINT64_C(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)) * Q_UINT64_C(0x94D049BB133111EB);
        return z ^ (z >> 31);
    };

    for (int i = m_items.size() - 1; i > firstShuffleIndex; --i) {
        const quint64 width =
            static_cast<quint64>(i - firstShuffleIndex + 1);
        const int j = firstShuffleIndex
            + static_cast<int>(nextRandom() % width);
        if (i != j)
            m_items.swapItemsAt(i, j);
    }

    bool orderChanged = false;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).occurrenceId() != before.at(i)) {
            orderChanged = true;
            break;
        }
    }
    return QueueMutationStatus::success(newlyShuffled || orderChanged);
}

QueueMutationStatus VinylQueue::unshuffle()
{
    if (!m_originalOrder.has_value())
        return QueueMutationStatus::success(false);

    const QueueMutationStatus valid =
        validateOriginalOrder(*m_originalOrder);
    if (!valid.ok)
        return valid;

    QVector<QueueOccurrence> restored;
    restored.reserve(m_items.size());
    for (const QueueOccurrenceId &id : *m_originalOrder) {
        const QueueOccurrence *item = find(id);
        if (!item) {
            return QueueMutationStatus::failure(
                QueueErrorCode::InvalidShuffleState,
                QStringLiteral("shuffle.originalOrder"),
                QStringLiteral("Original order references a missing queue occurrence."));
        }
        restored.append(*item);
    }

    m_items = std::move(restored);
    m_originalOrder.reset();
    return QueueMutationStatus::success(true);
}

QueueMutationStatus VinylQueue::restoreShuffleOriginalOrder(
    QVector<QueueOccurrenceId> originalOrder)
{
    const QueueMutationStatus valid = validateOriginalOrder(originalOrder);
    if (!valid.ok)
        return valid;

    if (m_originalOrder.has_value()
        && *m_originalOrder == originalOrder) {
        return QueueMutationStatus::success(false);
    }

    m_originalOrder = std::move(originalOrder);
    return QueueMutationStatus::success(true);
}

} // namespace vinyl
