#pragma once

#include "VinylDomainTypes.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <optional>
#include <utility>

namespace vinyl {

enum class QueueErrorCode {
    None,
    InvalidOccurrenceId,
    DuplicateOccurrenceId,
    OccurrenceNotFound,
    IndexOutOfRange,
    RecordingContextMismatch,
    NoActiveOccurrence,
    InvalidEnqueueOrigin,
    InvalidShuffleState,
};

struct QueueError final {
    QueueErrorCode code = QueueErrorCode::None;
    QString field;
    QString detail;
};

QString queueErrorCodeName(QueueErrorCode code);

struct QueueMutationStatus final {
    bool ok = true;
    bool changed = false;
    QueueError error;

    static QueueMutationStatus success(bool changed = true)
    {
        QueueMutationStatus status;
        status.changed = changed;
        return status;
    }

    static QueueMutationStatus failure(QueueErrorCode code,
                                       QString field,
                                       QString detail = QString())
    {
        QueueMutationStatus status;
        status.ok = false;
        status.error = QueueError{code, std::move(field), std::move(detail)};
        return status;
    }
};

template <typename T>
struct QueueResult final {
    std::optional<T> value;
    QueueError error;

    bool ok() const noexcept { return value.has_value(); }

    static QueueResult success(T value)
    {
        QueueResult out;
        out.value = std::move(value);
        return out;
    }

    static QueueResult failure(QueueErrorCode code,
                               QString field,
                               QString detail = QString())
    {
        QueueResult out;
        out.error = QueueError{code, std::move(field), std::move(detail)};
        return out;
    }
};

class QueueOccurrenceId final
{
public:
    static QueueResult<QueueOccurrenceId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const QueueOccurrenceId &other) const noexcept
    {
        return m_value == other.m_value;
    }

    bool operator!=(const QueueOccurrenceId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit QueueOccurrenceId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

enum class EnqueueOriginKind {
    User,
    Album,
    Playlist,
    Radio,
    Restore,
};

QString enqueueOriginKindName(EnqueueOriginKind kind);

class EnqueueBatchId final
{
public:
    static QueueResult<EnqueueBatchId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const EnqueueBatchId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit EnqueueBatchId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

enum class EnqueueContainerKind {
    CanonicalEntity,
    Playlist,
};

QString enqueueContainerKindName(EnqueueContainerKind kind);

class EnqueueContainerRef final
{
public:
    static EnqueueContainerRef canonical(CanonicalEntityRef entity);
    static QueueResult<EnqueueContainerRef> playlist(const QString &playlistId);

    EnqueueContainerKind kind() const noexcept { return m_kind; }
    const std::optional<CanonicalEntityRef> &canonicalEntity() const noexcept
    {
        return m_canonicalEntity;
    }
    const QString &playlistId() const noexcept { return m_playlistId; }

    bool operator==(const EnqueueContainerRef &other) const noexcept
    {
        return m_kind == other.m_kind
            && m_canonicalEntity == other.m_canonicalEntity
            && m_playlistId == other.m_playlistId;
    }

private:
    EnqueueContainerRef(EnqueueContainerKind kind,
                        std::optional<CanonicalEntityRef> canonicalEntity,
                        QString playlistId)
        : m_kind(kind)
        , m_canonicalEntity(std::move(canonicalEntity))
        , m_playlistId(std::move(playlistId))
    {
    }

    EnqueueContainerKind m_kind;
    std::optional<CanonicalEntityRef> m_canonicalEntity;
    QString m_playlistId;
};

class EnqueueOrigin final
{
public:
    EnqueueOrigin(EnqueueOriginKind kind,
                  EnqueueBatchId batchId,
                  std::optional<EnqueueContainerRef> container = std::nullopt)
        : m_kind(kind)
        , m_batchId(std::move(batchId))
        , m_container(std::move(container))
    {
    }

    EnqueueOriginKind kind() const noexcept { return m_kind; }
    const EnqueueBatchId &batchId() const noexcept { return m_batchId; }
    const std::optional<EnqueueContainerRef> &container() const noexcept
    {
        return m_container;
    }

    bool operator==(const EnqueueOrigin &other) const noexcept
    {
        return m_kind == other.m_kind
            && m_batchId == other.m_batchId
            && m_container == other.m_container;
    }

private:
    EnqueueOriginKind m_kind;
    EnqueueBatchId m_batchId;
    std::optional<EnqueueContainerRef> m_container;
};

class QueueOccurrence final
{
public:
    static QueueResult<QueueOccurrence> create(
        QueueOccurrenceId occurrenceId,
        RecordingMbid recording,
        std::optional<ReleaseTrackCoordinate> releaseContext = std::nullopt,
        std::optional<EnqueueOrigin> enqueueOrigin = std::nullopt);

    const QueueOccurrenceId &occurrenceId() const noexcept { return m_occurrenceId; }
    const RecordingMbid &recordingMbid() const noexcept { return m_recording; }
    const std::optional<ReleaseTrackCoordinate> &releaseContext() const noexcept
    {
        return m_releaseContext;
    }
    const std::optional<EnqueueOrigin> &enqueueOrigin() const noexcept
    {
        return m_enqueueOrigin;
    }

private:
    QueueOccurrence(QueueOccurrenceId occurrenceId,
                    RecordingMbid recording,
                    std::optional<ReleaseTrackCoordinate> releaseContext,
                    std::optional<EnqueueOrigin> enqueueOrigin)
        : m_occurrenceId(std::move(occurrenceId))
        , m_recording(std::move(recording))
        , m_releaseContext(std::move(releaseContext))
        , m_enqueueOrigin(std::move(enqueueOrigin))
    {
    }

    QueueOccurrenceId m_occurrenceId;
    RecordingMbid m_recording;
    std::optional<ReleaseTrackCoordinate> m_releaseContext;
    std::optional<EnqueueOrigin> m_enqueueOrigin;
};

class VinylQueue final
{
public:
    int size() const noexcept { return m_items.size(); }
    bool empty() const noexcept { return m_items.isEmpty(); }

    const QVector<QueueOccurrence> &items() const noexcept { return m_items; }

    std::optional<QueueOccurrenceId> activeOccurrenceId() const
    {
        return m_activeId;
    }

    int activeIndex() const;
    const QueueOccurrence *activeOccurrence() const;
    const QueueOccurrence *find(const QueueOccurrenceId &id) const;

    QueueMutationStatus append(QueueOccurrence occurrence);
    QueueMutationStatus insert(int index, QueueOccurrence occurrence);
    QueueMutationStatus playNow(QueueOccurrence occurrence);
    QueueMutationStatus playNext(QueueOccurrence occurrence);
    QueueMutationStatus remove(const QueueOccurrenceId &id);
    QueueMutationStatus move(const QueueOccurrenceId &id, int destinationIndex);
    QueueMutationStatus clear();
    QueueMutationStatus clearAfterCurrent();
    QueueMutationStatus activate(const QueueOccurrenceId &id);
    QueueMutationStatus deactivate();
    QueueMutationStatus advance();

    QueueMutationStatus shuffle(quint64 seed);
    QueueMutationStatus unshuffle();
    QueueMutationStatus restoreShuffleOriginalOrder(
        QVector<QueueOccurrenceId> originalOrder);
    bool isShuffled() const noexcept { return m_originalOrder.has_value(); }
    const std::optional<QVector<QueueOccurrenceId>> &shuffleOriginalOrder() const noexcept
    {
        return m_originalOrder;
    }

private:
    int indexOf(const QueueOccurrenceId &id) const;
    int originalIndexOf(const QueueOccurrenceId &id) const;
    bool containsId(const QueueOccurrenceId &id) const;
    QueueMutationStatus validateNewOccurrence(const QueueOccurrence &occurrence) const;
    QueueMutationStatus validateOriginalOrder(
        const QVector<QueueOccurrenceId> &originalOrder) const;
    void insertIntoOriginalOrder(int index, const QueueOccurrenceId &id);
    void removeFromOriginalOrder(const QueueOccurrenceId &id);

    QVector<QueueOccurrence> m_items;
    std::optional<QueueOccurrenceId> m_activeId;
    std::optional<QVector<QueueOccurrenceId>> m_originalOrder;
};

} // namespace vinyl
