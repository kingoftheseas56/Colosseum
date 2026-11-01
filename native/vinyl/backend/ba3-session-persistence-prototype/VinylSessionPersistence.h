#pragma once

#include "VinylPlaybackSession.h"
#include "VinylCatalogueTypes.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class SessionPersistenceErrorCode {
    None,
    InvalidProfileScopeId,
    EmptySnapshot,
    MalformedSnapshot,
    UnsupportedSchema,
    UnsupportedVersion,
    ProfileMismatch,
    InvalidQueueState,
    InvalidResumeState,
    InvalidCoordinatorState,
};

struct SessionPersistenceError final {
    SessionPersistenceErrorCode code = SessionPersistenceErrorCode::None;
    QString field;
    QString detail;
};

QString sessionPersistenceErrorCodeName(SessionPersistenceErrorCode code);

template <typename T>
struct SessionPersistenceResult final {
    std::optional<T> value;
    SessionPersistenceError error;

    bool ok() const noexcept { return value.has_value(); }

    static SessionPersistenceResult success(T value)
    {
        SessionPersistenceResult out;
        out.value = std::move(value);
        return out;
    }

    static SessionPersistenceResult failure(SessionPersistenceErrorCode code,
                                            QString field,
                                            QString detail = QString())
    {
        SessionPersistenceResult out;
        out.error = SessionPersistenceError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};

class SessionResumeIntent final
{
public:
    SessionResumeIntent(QueueOccurrenceId occurrenceId,
                        bool playWhenReady,
                        std::optional<qint64> positionMs)
        : m_occurrenceId(std::move(occurrenceId))
        , m_playWhenReady(playWhenReady)
        , m_positionMs(positionMs)
    {
    }

    const QueueOccurrenceId &occurrenceId() const noexcept
    {
        return m_occurrenceId;
    }

    bool playWhenReady() const noexcept { return m_playWhenReady; }

    const std::optional<qint64> &positionMs() const noexcept
    {
        return m_positionMs;
    }

private:
    QueueOccurrenceId m_occurrenceId;
    bool m_playWhenReady = false;
    std::optional<qint64> m_positionMs;
};

enum class DurableRepeatMode {
    Off,
    Queue,
};

QString durableRepeatModeName(DurableRepeatMode mode);

struct SessionPendingInputIntent final {
    QueueOccurrenceId occurrenceId;
    bool playWhenReady = false;
};

struct SessionCoordinatorIntent final {
    DurableRepeatMode repeatMode = DurableRepeatMode::Off;
    std::optional<SessionPendingInputIntent> pendingInput;
};

struct QueueDisplaySnapshot final {
    QueueOccurrenceId occurrenceId;
    catalogue::LastKnownDisplaySnapshot display;
};

struct RestoredSessionState final {
    VinylQueue queue;
    std::optional<SessionResumeIntent> resumeIntent;
    SessionCoordinatorIntent coordinatorIntent;
    QVector<QueueDisplaySnapshot> displaySnapshots;
};

class VinylSessionPersistence final
{
public:
    static constexpr int MinimumSupportedVersion = 1;
    // Canonical-only captures stay at v2; host-local identities require v3.
    static constexpr int SchemaVersion = 2;
    static constexpr int MaximumSupportedVersion = 3;

    static QString schemaName();

    static SessionPersistenceResult<QByteArray> capture(
        const ProfileScopeId &profileId,
        const VinylQueue &queue,
        const PlaybackSession &session,
        SessionCoordinatorIntent coordinatorIntent = {},
        QVector<QueueDisplaySnapshot> displaySnapshots = {});

    static SessionPersistenceResult<RestoredSessionState> restore(
        const ProfileScopeId &expectedProfileId,
        const QByteArray &snapshot);
};

} // namespace vinyl
