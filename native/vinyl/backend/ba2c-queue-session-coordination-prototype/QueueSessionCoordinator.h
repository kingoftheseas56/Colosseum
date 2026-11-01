#pragma once

#include "PlaybackEngineAdapter.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>
#include <variant>

namespace vinyl {

enum class QueueSessionErrorCode {
    None,
    EmptyQueue,
    MissingPlaybackInput,
    QueueMutationFailed,
    PlaybackMutationFailed,
    ActiveOccurrenceMismatch,
    ReplacementNotArmed,
};

struct QueueSessionError final {
    QueueSessionErrorCode code = QueueSessionErrorCode::None;
    QString field;
    QString detail;
};

QString queueSessionErrorCodeName(QueueSessionErrorCode code);

enum class OccurrenceEndCause {
    Eof,
    UserSkip,
    Removed,
    Stopped,
    Failed,
    Replaced,
};

QString occurrenceEndCauseName(OccurrenceEndCause cause);

enum class CoordinatorRepeatMode {
    Off,
    Queue,
};

QString coordinatorRepeatModeName(CoordinatorRepeatMode mode);

struct CoordinatorPendingInput final {
    QueueOccurrenceId occurrenceId;
    bool playWhenReady = false;

    bool operator==(const CoordinatorPendingInput &other) const noexcept
    {
        return occurrenceId == other.occurrenceId
            && playWhenReady == other.playWhenReady;
    }
};

struct OccurrenceStarted final {
    QueueOccurrenceId occurrenceId;
    PlaybackWorkToken workToken;
    bool playWhenReady = false;
};

struct OccurrenceEnded final {
    QueueOccurrenceId occurrenceId;
    OccurrenceEndCause cause = OccurrenceEndCause::Stopped;
};

struct QueueExhausted final {
};

struct AwaitingInput final {
    QueueOccurrenceId occurrenceId;
    bool playWhenReady = false;
};

using CoordinatorTransitionFact = std::variant<
    OccurrenceStarted,
    OccurrenceEnded,
    QueueExhausted,
    AwaitingInput>;

struct QueueSessionStatus final {
    bool ok = true;
    bool queueChanged = false;
    bool playbackChanged = false;
    QueueSessionError error;
    QVector<CoordinatorTransitionFact> transitionFacts;

    static QueueSessionStatus success(
        bool queueChanged = false,
        bool playbackChanged = false,
        QVector<CoordinatorTransitionFact> transitionFacts = {})
    {
        QueueSessionStatus status;
        status.queueChanged = queueChanged;
        status.playbackChanged = playbackChanged;
        status.transitionFacts = std::move(transitionFacts);
        return status;
    }

    static QueueSessionStatus failure(
        QueueSessionErrorCode code,
        QString field,
        QString detail = QString(),
        bool queueChanged = false,
        bool playbackChanged = false,
        QVector<CoordinatorTransitionFact> transitionFacts = {})
    {
        QueueSessionStatus status;
        status.ok = false;
        status.queueChanged = queueChanged;
        status.playbackChanged = playbackChanged;
        status.error = QueueSessionError{
            code, std::move(field), std::move(detail)};
        status.transitionFacts = std::move(transitionFacts);
        return status;
    }
};

class PlaybackLoadInputProvider
{
public:
    virtual ~PlaybackLoadInputProvider() = default;

    virtual std::optional<PlaybackLoadInput> inputFor(
        const QueueOccurrence &occurrence) = 0;
};

class QueueSessionCoordinator final
{
public:
    QueueSessionCoordinator(VinylQueue &queue,
                            PlaybackSession &session,
                            PlaybackEngineAdapter &adapter,
                            PlaybackLoadInputProvider &inputProvider)
        : m_queue(queue)
        , m_session(session)
        , m_adapter(adapter)
        , m_inputProvider(inputProvider)
    {
    }

    QueueSessionStatus start(bool playWhenReady = true);
    QueueSessionStatus stop();
    QueueSessionStatus skip();

    QueueSessionStatus awaitInput(
        const QueueOccurrenceId &expectedOccurrenceId,
        bool playWhenReady);
    QueueSessionStatus restartActive(
        const QueueOccurrenceId &expectedOccurrenceId,
        const PlaybackLoadInput &input);

    CoordinatorRepeatMode repeatMode() const noexcept { return m_repeatMode; }
    void setRepeatMode(CoordinatorRepeatMode mode) noexcept { m_repeatMode = mode; }

    const std::optional<CoordinatorPendingInput> &pendingInput() const noexcept
    {
        return m_pendingInput;
    }

    QueueSessionStatus append(QueueOccurrence occurrence);
    QueueSessionStatus insert(int index, QueueOccurrence occurrence);
    QueueSessionStatus playNext(QueueOccurrence occurrence);
    QueueSessionStatus playNow(QueueOccurrence occurrence);
    QueueSessionStatus remove(const QueueOccurrenceId &id);
    QueueSessionStatus move(const QueueOccurrenceId &id,
                            int destinationIndex);
    QueueSessionStatus clear();
    QueueSessionStatus clearAfterCurrent();
    QueueSessionStatus activate(const QueueOccurrenceId &id,
                                bool playWhenReady = true);
    QueueSessionStatus deactivate();

    QueueSessionStatus engineLoaded(const PlaybackWorkToken &token);
    QueueSessionStatus enginePauseObserved(const PlaybackWorkToken &token,
                                           bool paused);
    QueueSessionStatus enginePositionObserved(const PlaybackWorkToken &token,
                                              double seconds);
    QueueSessionStatus engineDurationObserved(const PlaybackWorkToken &token,
                                              double seconds);
    QueueSessionStatus engineSeekApplied(
        const PlaybackSeekRequest &request,
        double observedPositionSeconds);
    QueueSessionStatus engineEnded(const PlaybackWorkToken &token);
    QueueSessionStatus engineFailed(const PlaybackWorkToken &token,
                                    PlaybackFailure failure);

private:
    static bool sameOptionalId(
        const std::optional<QueueOccurrenceId> &left,
        const std::optional<QueueOccurrenceId> &right);
    static void appendFacts(
        QVector<CoordinatorTransitionFact> &destination,
        const QVector<CoordinatorTransitionFact> &source);

    QueueSessionStatus fromQueueStatus(
        const QueueMutationStatus &status) const;
    QueueSessionStatus fromPlaybackStatus(
        const PlaybackMutationStatus &status) const;

    bool currentPlayWhenReadyIntent() const;
    bool sessionBoundTo(const QueueOccurrenceId &id) const;
    bool pendingFor(const QueueOccurrenceId &id) const;

    QueueSessionStatus beginOccurrenceFromProvider(
        const QueueOccurrence &occurrence,
        bool playWhenReady,
        bool queueChanged,
        QVector<CoordinatorTransitionFact> prefixFacts = {});

    QueueSessionStatus beginOccurrenceWithInput(
        const QueueOccurrence &occurrence,
        const PlaybackLoadInput &input,
        bool playWhenReady,
        bool queueChanged,
        QVector<CoordinatorTransitionFact> prefixFacts = {});

    QueueSessionStatus transitionToActive(
        const std::optional<QueueOccurrenceId> &beforeActive,
        bool nextPlayWhenReady,
        std::optional<OccurrenceEndCause> endCause = std::nullopt,
        bool forceRestart = false);

    QueueSessionStatus reconcileAfterMutation(
        const std::optional<QueueOccurrenceId> &beforeActive,
        const QueueMutationStatus &mutation,
        bool nextPlayWhenReady,
        std::optional<OccurrenceEndCause> endCause = std::nullopt);

    QueueSessionStatus expectedActiveStatus(
        const QueueOccurrenceId &expectedOccurrenceId) const;

    VinylQueue &m_queue;
    PlaybackSession &m_session;
    PlaybackEngineAdapter &m_adapter;
    PlaybackLoadInputProvider &m_inputProvider;
    std::optional<CoordinatorPendingInput> m_pendingInput;
    CoordinatorRepeatMode m_repeatMode = CoordinatorRepeatMode::Off;
};

} // namespace vinyl
