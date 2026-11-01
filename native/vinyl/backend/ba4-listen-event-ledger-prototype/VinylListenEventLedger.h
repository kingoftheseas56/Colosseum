#pragma once

#include "VinylPlaybackSession.h"
#include "VinylCatalogueTypes.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class ListenEventKind {
    Start,
    Pause,
    Resume,
    Seek,
    Skip,
    Stop,
    Failure,
    Completion,
    SourceChange,
    ListeningSpan,
};

QString listenEventKindName(ListenEventKind kind);

enum class ListenLedgerErrorCode {
    None,
    InvalidSessionId,
    InvalidSourceTraceId,
    InvalidTimestamp,
    InvalidPosition,
    InvalidRate,
    ContextMismatch,
    ProfileMismatch,
    NoActiveContext,
    StaleContext,
    InvalidTransition,
    CounterExhausted,
};

struct ListenLedgerError final {
    ListenLedgerErrorCode code = ListenLedgerErrorCode::None;
    QString field;
    QString detail;
};

QString listenLedgerErrorCodeName(ListenLedgerErrorCode code);

struct ListenLedgerMutationStatus final {
    bool ok = true;
    bool changed = false;
    ListenLedgerError error;

    static ListenLedgerMutationStatus success(bool changed = true)
    {
        ListenLedgerMutationStatus status;
        status.changed = changed;
        return status;
    }

    static ListenLedgerMutationStatus failure(
        ListenLedgerErrorCode code,
        QString field,
        QString detail = QString())
    {
        ListenLedgerMutationStatus status;
        status.ok = false;
        status.error = ListenLedgerError{
            code, std::move(field), std::move(detail)};
        return status;
    }
};

template <typename T>
struct ListenLedgerResult final {
    std::optional<T> value;
    ListenLedgerError error;

    bool ok() const noexcept { return value.has_value(); }

    static ListenLedgerResult success(T value)
    {
        ListenLedgerResult out;
        out.value = std::move(value);
        return out;
    }

    static ListenLedgerResult failure(
        ListenLedgerErrorCode code,
        QString field,
        QString detail = QString())
    {
        ListenLedgerResult out;
        out.error = ListenLedgerError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};

class ListenSessionId final
{
public:
    static ListenLedgerResult<ListenSessionId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const ListenSessionId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit ListenSessionId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class ListenSourceTraceId final
{
public:
    static ListenLedgerResult<ListenSourceTraceId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const ListenSourceTraceId &other) const noexcept
    {
        return m_value == other.m_value;
    }

    bool operator!=(const ListenSourceTraceId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit ListenSourceTraceId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class ListenContext final
{
public:
    static ListenLedgerResult<ListenContext> create(
        ProfileScopeId profileId,
        ListenSessionId sessionId,
        QueueOccurrence occurrence,
        PlaybackWorkToken workToken,
        PlayInstanceId playInstanceId,
        std::optional<PlayInstanceId> continuationOf = std::nullopt,
        std::optional<catalogue::LastKnownDisplaySnapshot> displaySnapshot = std::nullopt);

    const ProfileScopeId &profileId() const noexcept { return m_profileId; }
    const ListenSessionId &sessionId() const noexcept { return m_sessionId; }
    const QueueOccurrence &occurrence() const noexcept { return m_occurrence; }
    const RecordingMbid &recordingMbid() const noexcept
    {
        return m_occurrence.recordingMbid();
    }
    const QueueOccurrenceId &occurrenceId() const noexcept
    {
        return m_workToken.occurrenceId();
    }
    const PlaybackWorkToken &workToken() const noexcept { return m_workToken; }
    const PlayInstanceId &playInstanceId() const noexcept { return m_playInstanceId; }
    const std::optional<PlayInstanceId> &continuationOf() const noexcept
    {
        return m_continuationOf;
    }
    const std::optional<catalogue::LastKnownDisplaySnapshot> &displaySnapshot() const noexcept
    {
        return m_displaySnapshot;
    }

private:
    ListenContext(ProfileScopeId profileId,
                  ListenSessionId sessionId,
                  QueueOccurrence occurrence,
                  PlaybackWorkToken workToken,
                  PlayInstanceId playInstanceId,
                  std::optional<PlayInstanceId> continuationOf,
                  std::optional<catalogue::LastKnownDisplaySnapshot> displaySnapshot)
        : m_profileId(std::move(profileId))
        , m_sessionId(std::move(sessionId))
        , m_occurrence(std::move(occurrence))
        , m_workToken(std::move(workToken))
        , m_playInstanceId(std::move(playInstanceId))
        , m_continuationOf(std::move(continuationOf))
        , m_displaySnapshot(std::move(displaySnapshot))
    {
    }

    ProfileScopeId m_profileId;
    ListenSessionId m_sessionId;
    QueueOccurrence m_occurrence;
    PlaybackWorkToken m_workToken;
    PlayInstanceId m_playInstanceId;
    std::optional<PlayInstanceId> m_continuationOf;
    std::optional<catalogue::LastKnownDisplaySnapshot> m_displaySnapshot;
};

class ListenEvent final
{
public:
    ListenEventKind kind() const noexcept { return m_kind; }
    const ListenContext &context() const noexcept { return m_context; }
    quint64 sequence() const noexcept { return m_sequence; }
    qint64 observedAtMs() const noexcept { return m_observedAtMs; }
    qint64 monotonicElapsedMs() const noexcept { return m_monotonicElapsedMs; }
    qint64 positionMs() const noexcept { return m_positionMs; }
    const std::optional<qint64> &fromPositionMs() const noexcept
    {
        return m_fromPositionMs;
    }
    const std::optional<qint64> &toPositionMs() const noexcept
    {
        return m_toPositionMs;
    }
    const std::optional<qint64> &activeMs() const noexcept { return m_activeMs; }
    const std::optional<ListenSourceTraceId> &sourceBefore() const noexcept
    {
        return m_sourceBefore;
    }
    const std::optional<ListenSourceTraceId> &sourceAfter() const noexcept
    {
        return m_sourceAfter;
    }

private:
    friend class ListenEventLedger;

    ListenEvent(ListenEventKind kind,
                ListenContext context,
                quint64 sequence,
                qint64 observedAtMs,
                qint64 monotonicElapsedMs,
                qint64 positionMs,
                std::optional<qint64> fromPositionMs,
                std::optional<qint64> toPositionMs,
                std::optional<qint64> activeMs,
                std::optional<ListenSourceTraceId> sourceBefore,
                std::optional<ListenSourceTraceId> sourceAfter)
        : m_kind(kind)
        , m_context(std::move(context))
        , m_sequence(sequence)
        , m_observedAtMs(observedAtMs)
        , m_monotonicElapsedMs(monotonicElapsedMs)
        , m_positionMs(positionMs)
        , m_fromPositionMs(fromPositionMs)
        , m_toPositionMs(toPositionMs)
        , m_activeMs(activeMs)
        , m_sourceBefore(std::move(sourceBefore))
        , m_sourceAfter(std::move(sourceAfter))
    {
    }

    ListenEventKind m_kind;
    ListenContext m_context;
    quint64 m_sequence = 0;
    qint64 m_observedAtMs = 0;
    qint64 m_monotonicElapsedMs = 0;
    qint64 m_positionMs = 0;
    std::optional<qint64> m_fromPositionMs;
    std::optional<qint64> m_toPositionMs;
    std::optional<qint64> m_activeMs;
    std::optional<ListenSourceTraceId> m_sourceBefore;
    std::optional<ListenSourceTraceId> m_sourceAfter;
};

class ListenEventLedger final
{
public:
    explicit ListenEventLedger(ProfileScopeId profileId)
        : m_profileId(std::move(profileId))
    {
    }

    const ProfileScopeId &profileId() const noexcept { return m_profileId; }
    const QVector<ListenEvent> &events() const noexcept { return m_events; }
    int size() const noexcept { return m_events.size(); }
    bool empty() const noexcept { return m_events.isEmpty(); }
    qint64 totalListeningMs() const noexcept;

private:
    friend class ListenEventRecorder;

    ListenLedgerMutationStatus append(
        ListenEventKind kind,
        const ListenContext &context,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        std::optional<qint64> fromPositionMs = std::nullopt,
        std::optional<qint64> toPositionMs = std::nullopt,
        std::optional<qint64> activeMs = std::nullopt,
        std::optional<ListenSourceTraceId> sourceBefore = std::nullopt,
        std::optional<ListenSourceTraceId> sourceAfter = std::nullopt);
    ListenLedgerMutationStatus appendOrExtendListeningSpan(
        const ListenContext &context,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 fromPositionMs,
        qint64 toPositionMs,
        qint64 activeMs);

    ProfileScopeId m_profileId;
    QVector<ListenEvent> m_events;
    quint64 m_nextSequence = 1;
};

class ListenEventRecorder final
{
public:
    explicit ListenEventRecorder(ListenEventLedger &ledger)
        : m_ledger(ledger)
    {
    }

    bool active() const noexcept { return m_context.has_value(); }
    bool consuming() const noexcept { return m_consuming; }
    const std::optional<ListenContext> &activeContext() const noexcept
    {
        return m_context;
    }

    ListenLedgerMutationStatus start(
        ListenContext context,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli,
        std::optional<ListenSourceTraceId> source = std::nullopt);

    ListenLedgerMutationStatus sample(
        const PlaybackWorkToken &token,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);

    ListenLedgerMutationStatus pause(
        const PlaybackWorkToken &token,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);

    ListenLedgerMutationStatus resume(
        const PlaybackWorkToken &token,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);

    ListenLedgerMutationStatus seek(
        const PlaybackWorkToken &token,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 fromPositionMs,
        qint64 toPositionMs,
        qint64 rateMilli);

    ListenLedgerMutationStatus sourceChanged(
        const PlaybackWorkToken &token,
        ListenSourceTraceId newSource,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);

    ListenLedgerMutationStatus fallbackSourceChangedAfterFailure(
        ListenSourceTraceId failedSource,
        ListenSourceTraceId replacementSource,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);

    ListenLedgerMutationStatus skip(
        const PlaybackWorkToken &token,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);

    ListenLedgerMutationStatus stop(
        const PlaybackWorkToken &token,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);

    ListenLedgerMutationStatus failure(
        const PlaybackWorkToken &token,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);

    ListenLedgerMutationStatus complete(
        const PlaybackWorkToken &token,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);

private:
    struct Baseline final {
        qint64 monotonicElapsedMs = 0;
        qint64 positionMs = 0;
        qint64 rateMilli = 0;
    };

    ListenLedgerMutationStatus validateCurrent(
        const PlaybackWorkToken &token) const;
    ListenLedgerMutationStatus validateObservation(
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli) const;
    ListenLedgerMutationStatus accountTo(
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);
    ListenLedgerMutationStatus terminate(
        ListenEventKind kind,
        const PlaybackWorkToken &token,
        qint64 observedAtMs,
        qint64 monotonicElapsedMs,
        qint64 positionMs,
        qint64 rateMilli);
    void resetBaseline(qint64 monotonicElapsedMs,
                       qint64 positionMs,
                       qint64 rateMilli);
    void clearBaseline() noexcept { m_baseline.reset(); }
    void clearContext() noexcept;

    ListenEventLedger &m_ledger;
    std::optional<ListenContext> m_context;
    std::optional<ListenSourceTraceId> m_source;
    std::optional<Baseline> m_baseline;
    bool m_consuming = false;
};

} // namespace vinyl
