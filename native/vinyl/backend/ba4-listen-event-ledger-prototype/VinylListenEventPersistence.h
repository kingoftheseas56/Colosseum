#pragma once

#include "VinylListenEventLedger.h"

#include <QByteArray>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class ListenPersistenceErrorCode {
    None,
    EmptySnapshot,
    MalformedSnapshot,
    UnsupportedSchema,
    UnsupportedVersion,
    ProfileMismatch,
    InvalidState,
};

struct ListenPersistenceError final {
    ListenPersistenceErrorCode code = ListenPersistenceErrorCode::None;
    QString field;
    QString detail;
};
QString listenPersistenceErrorCodeName(ListenPersistenceErrorCode code);

template <typename T>
struct ListenPersistenceResult final {
    std::optional<T> value;
    ListenPersistenceError error;

    bool ok() const noexcept { return value.has_value(); }

    static ListenPersistenceResult success(T value)
    {
        ListenPersistenceResult out;
        out.value = std::move(value);
        return out;
    }

    static ListenPersistenceResult failure(
        ListenPersistenceErrorCode code,
        QString field,
        QString detail = QString())
    {
        ListenPersistenceResult out;
        out.error = ListenPersistenceError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};
struct DurableListenEvent final {
    ListenEventKind kind;
    ListenSessionId sessionId;
    QueueOccurrence occurrence;
    PlayInstanceId playInstanceId;
    std::optional<PlayInstanceId> continuationOf;
    std::optional<catalogue::LastKnownDisplaySnapshot> displaySnapshot;
    quint64 sequence = 0;
    qint64 observedAtMs = 0;
    qint64 monotonicElapsedMs = 0;
    qint64 positionMs = 0;
    std::optional<qint64> fromPositionMs;
    std::optional<qint64> toPositionMs;
    std::optional<qint64> activeMs;
    std::optional<ListenSourceTraceId> sourceBefore;
    std::optional<ListenSourceTraceId> sourceAfter;
};

struct RestoredListenLedger final {
    ProfileScopeId profileId;
    QVector<DurableListenEvent> events;

    qint64 totalListeningMs() const noexcept;
};
class VinylListenEventPersistence final
{
public:
    static constexpr int MinimumSupportedVersion = 1;
    static constexpr int SchemaVersion = 3;

    static QString schemaName();

    static ListenPersistenceResult<QByteArray> capture(
        const ListenEventLedger &ledger);

    static ListenPersistenceResult<RestoredListenLedger> restore(
        const ProfileScopeId &expectedProfileId,
        const QByteArray &snapshot);
};

} // namespace vinyl
