#include "QueueSessionCoordinator.h"

namespace vinyl {

QString queueSessionErrorCodeName(QueueSessionErrorCode code)
{
    switch (code) {
    case QueueSessionErrorCode::None:
        return QStringLiteral("none");
    case QueueSessionErrorCode::EmptyQueue:
        return QStringLiteral("empty_queue");
    case QueueSessionErrorCode::MissingPlaybackInput:
        return QStringLiteral("missing_playback_input");
    case QueueSessionErrorCode::QueueMutationFailed:
        return QStringLiteral("queue_mutation_failed");
    case QueueSessionErrorCode::PlaybackMutationFailed:
        return QStringLiteral("playback_mutation_failed");
    case QueueSessionErrorCode::ActiveOccurrenceMismatch:
        return QStringLiteral("active_occurrence_mismatch");
    case QueueSessionErrorCode::ReplacementNotArmed:
        return QStringLiteral("replacement_not_armed");
    }
    return QStringLiteral("unknown");
}

QString occurrenceEndCauseName(OccurrenceEndCause cause)
{
    switch (cause) {
    case OccurrenceEndCause::Eof:
        return QStringLiteral("eof");
    case OccurrenceEndCause::UserSkip:
        return QStringLiteral("user_skip");
    case OccurrenceEndCause::Removed:
        return QStringLiteral("removed");
    case OccurrenceEndCause::Stopped:
        return QStringLiteral("stopped");
    case OccurrenceEndCause::Failed:
        return QStringLiteral("failed");
    case OccurrenceEndCause::Replaced:
        return QStringLiteral("replaced");
    }
    return QStringLiteral("unknown");
}

QString coordinatorRepeatModeName(CoordinatorRepeatMode mode)
{
    switch (mode) {
    case CoordinatorRepeatMode::Off:
        return QStringLiteral("off");
    case CoordinatorRepeatMode::Queue:
        return QStringLiteral("queue");
    }
    return QStringLiteral("unknown");
}

bool QueueSessionCoordinator::sameOptionalId(
    const std::optional<QueueOccurrenceId> &left,
    const std::optional<QueueOccurrenceId> &right)
{
    if (left.has_value() != right.has_value())
        return false;
    if (!left.has_value())
        return true;
    return *left == *right;
}

void QueueSessionCoordinator::appendFacts(
    QVector<CoordinatorTransitionFact> &destination,
    const QVector<CoordinatorTransitionFact> &source)
{
    for (const CoordinatorTransitionFact &fact : source)
        destination.append(fact);
}

QueueSessionStatus QueueSessionCoordinator::fromQueueStatus(
    const QueueMutationStatus &status) const
{
    if (status.ok)
        return QueueSessionStatus::success(status.changed, false);

    return QueueSessionStatus::failure(
        QueueSessionErrorCode::QueueMutationFailed,
        status.error.field,
        queueErrorCodeName(status.error.code)
            + QStringLiteral(": ")
            + status.error.detail);
}

QueueSessionStatus QueueSessionCoordinator::fromPlaybackStatus(
    const PlaybackMutationStatus &status) const
{
    if (status.ok)
        return QueueSessionStatus::success(false, status.changed);

    return QueueSessionStatus::failure(
        QueueSessionErrorCode::PlaybackMutationFailed,
        status.error.field,
        playbackSessionErrorCodeName(status.error.code)
            + QStringLiteral(": ")
            + status.error.detail);
}

bool QueueSessionCoordinator::sessionBoundTo(
    const QueueOccurrenceId &id) const
{
    return m_session.activeOccurrence().has_value()
        && m_session.activeOccurrence()->occurrenceId() == id;
}

bool QueueSessionCoordinator::pendingFor(
    const QueueOccurrenceId &id) const
{
    return m_pendingInput.has_value()
        && m_pendingInput->occurrenceId == id;
}

bool QueueSessionCoordinator::currentPlayWhenReadyIntent() const
{
    const std::optional<QueueOccurrenceId> activeId =
        m_queue.activeOccurrenceId();
    if (activeId.has_value() && pendingFor(*activeId))
        return m_pendingInput->playWhenReady;
    return m_session.playWhenReady();
}

QueueSessionStatus QueueSessionCoordinator::expectedActiveStatus(
    const QueueOccurrenceId &expectedOccurrenceId) const
{
    const std::optional<QueueOccurrenceId> activeId =
        m_queue.activeOccurrenceId();
    if (!activeId.has_value()) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("expectedOccurrenceId"),
            QStringLiteral("BA1 has no active occurrence."));
    }

    if (*activeId != expectedOccurrenceId) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("expectedOccurrenceId"),
            QStringLiteral("Expected occurrence is not BA1's active occurrence."));
    }

    const QueueOccurrence *active = m_queue.activeOccurrence();
    if (!active || active->occurrenceId() != *activeId) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("activeOccurrence"),
            QStringLiteral("BA1 active occurrence lookup disagrees with its active occurrence ID."));
    }

    if (m_session.activeOccurrence().has_value()
        && m_session.activeOccurrence()->occurrenceId() != *activeId) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("playbackOccurrence"),
            QStringLiteral("BA2 is bound to a QueueOccurrenceId different from BA1 active."));
    }

    if (m_pendingInput.has_value()
        && m_pendingInput->occurrenceId != *activeId) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("pendingInput"),
            QStringLiteral("Pending input belongs to a QueueOccurrenceId different from BA1 active."));
    }

    return QueueSessionStatus::success();
}

QueueSessionStatus QueueSessionCoordinator::beginOccurrenceWithInput(
    const QueueOccurrence &occurrence,
    const PlaybackLoadInput &input,
    bool playWhenReady,
    bool queueChanged,
    QVector<CoordinatorTransitionFact> prefixFacts)
{
    QueueSessionStatus active =
        expectedActiveStatus(occurrence.occurrenceId());
    if (!active.ok) {
        active.queueChanged = queueChanged;
        active.transitionFacts = std::move(prefixFacts);
        return active;
    }

    if (m_session.activeOccurrence().has_value()) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::PlaybackMutationFailed,
            QStringLiteral("playbackOccurrence"),
            QStringLiteral("BA2 binding must be cleared before beginning a new transport."),
            queueChanged,
            false,
            std::move(prefixFacts));
    }

    auto started =
        m_adapter.beginOccurrence(occurrence, input, playWhenReady);
    if (!started.ok()) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::PlaybackMutationFailed,
            started.error.field,
            playbackSessionErrorCodeName(started.error.code)
                + QStringLiteral(": ")
                + started.error.detail,
            queueChanged,
            false,
            std::move(prefixFacts));
    }

    if (pendingFor(occurrence.occurrenceId()))
        m_pendingInput.reset();

    prefixFacts.append(OccurrenceStarted{
        occurrence.occurrenceId(),
        *started.value,
        playWhenReady});
    return QueueSessionStatus::success(
        queueChanged,
        true,
        std::move(prefixFacts));
}

QueueSessionStatus QueueSessionCoordinator::beginOccurrenceFromProvider(
    const QueueOccurrence &occurrence,
    bool playWhenReady,
    bool queueChanged,
    QVector<CoordinatorTransitionFact> prefixFacts)
{
    QueueSessionStatus active =
        expectedActiveStatus(occurrence.occurrenceId());
    if (!active.ok) {
        active.queueChanged = queueChanged;
        active.transitionFacts = std::move(prefixFacts);
        return active;
    }

    const std::optional<PlaybackLoadInput> input =
        m_inputProvider.inputFor(occurrence);
    if (!input.has_value()) {
        const CoordinatorPendingInput pending{
            occurrence.occurrenceId(),
            playWhenReady};
        const bool changed =
            !m_pendingInput.has_value() || !(*m_pendingInput == pending);
        m_pendingInput = pending;
        if (changed) {
            prefixFacts.append(AwaitingInput{
                occurrence.occurrenceId(),
                playWhenReady});
        }
        return QueueSessionStatus::success(
            queueChanged,
            false,
            std::move(prefixFacts));
    }

    return beginOccurrenceWithInput(
        occurrence,
        *input,
        playWhenReady,
        queueChanged,
        std::move(prefixFacts));
}

QueueSessionStatus QueueSessionCoordinator::transitionToActive(
    const std::optional<QueueOccurrenceId> &beforeActive,
    bool nextPlayWhenReady,
    std::optional<OccurrenceEndCause> endCause,
    bool forceRestart)
{
    const std::optional<QueueOccurrenceId> afterActive =
        m_queue.activeOccurrenceId();

    if (sameOptionalId(beforeActive, afterActive) && !forceRestart)
        return QueueSessionStatus::success(false, false);

    QVector<CoordinatorTransitionFact> facts;
    bool playbackChanged = false;
    bool hadOldContext = false;

    if (beforeActive.has_value()) {
        if (m_session.activeOccurrence().has_value()) {
            if (m_session.activeOccurrence()->occurrenceId() != *beforeActive) {
                return QueueSessionStatus::failure(
                    QueueSessionErrorCode::ActiveOccurrenceMismatch,
                    QStringLiteral("playbackOccurrence"),
                    QStringLiteral("BA2 binding no longer matches the coordinator's previous BA1 active occurrence."));
            }
            hadOldContext = true;
        }

        if (m_pendingInput.has_value()) {
            if (m_pendingInput->occurrenceId != *beforeActive) {
                return QueueSessionStatus::failure(
                    QueueSessionErrorCode::ActiveOccurrenceMismatch,
                    QStringLiteral("pendingInput"),
                    QStringLiteral("Pending input no longer matches the coordinator's previous BA1 active occurrence."));
            }
            hadOldContext = true;
        }
    } else if (m_session.activeOccurrence().has_value()
               || m_pendingInput.has_value()) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("activeOccurrence"),
            QStringLiteral("Coordinator has BA2/pending state while BA1 had no previous active occurrence."));
    }

    if (m_session.activeOccurrence().has_value()) {
        const PlaybackMutationStatus stopped = m_adapter.stop();
        if (!stopped.ok) {
            return QueueSessionStatus::failure(
                QueueSessionErrorCode::PlaybackMutationFailed,
                stopped.error.field,
                playbackSessionErrorCodeName(stopped.error.code)
                    + QStringLiteral(": ")
                    + stopped.error.detail,
                false,
                playbackChanged);
        }
        playbackChanged = playbackChanged || stopped.changed;
    }

    if (beforeActive.has_value() && pendingFor(*beforeActive))
        m_pendingInput.reset();

    if (hadOldContext && beforeActive.has_value() && endCause.has_value()) {
        facts.append(OccurrenceEnded{
            *beforeActive,
            *endCause});
    }

    if (!afterActive.has_value()) {
        m_pendingInput.reset();
        return QueueSessionStatus::success(
            false,
            playbackChanged,
            std::move(facts));
    }

    const QueueOccurrence *active = m_queue.activeOccurrence();
    if (!active || active->occurrenceId() != *afterActive) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("activeOccurrence"),
            QStringLiteral("BA1 active occurrence lookup disagrees with its active occurrence ID."),
            false,
            playbackChanged,
            std::move(facts));
    }

    QueueSessionStatus begun =
        beginOccurrenceFromProvider(
            *active,
            nextPlayWhenReady,
            false,
            std::move(facts));
    begun.playbackChanged = playbackChanged || begun.playbackChanged;
    return begun;
}

QueueSessionStatus QueueSessionCoordinator::reconcileAfterMutation(
    const std::optional<QueueOccurrenceId> &beforeActive,
    const QueueMutationStatus &mutation,
    bool nextPlayWhenReady,
    std::optional<OccurrenceEndCause> endCause)
{
    if (!mutation.ok)
        return fromQueueStatus(mutation);

    QueueSessionStatus coordinated =
        transitionToActive(
            beforeActive,
            nextPlayWhenReady,
            endCause);
    coordinated.queueChanged = mutation.changed;
    return coordinated;
}

QueueSessionStatus QueueSessionCoordinator::start(bool playWhenReady)
{
    const std::optional<QueueOccurrenceId> activeId =
        m_queue.activeOccurrenceId();

    if (!activeId.has_value()) {
        if (m_queue.empty()) {
            return QueueSessionStatus::failure(
                QueueSessionErrorCode::EmptyQueue,
                QStringLiteral("queue"),
                QStringLiteral("Cannot start playback from an empty queue."));
        }

        if (m_session.activeOccurrence().has_value()
            || m_pendingInput.has_value()) {
            return QueueSessionStatus::failure(
                QueueSessionErrorCode::ActiveOccurrenceMismatch,
                QStringLiteral("activeOccurrence"),
                QStringLiteral("BA2/pending state exists while BA1 is inactive."));
        }

        const QueueOccurrence first = m_queue.items().first();
        const QueueMutationStatus activated =
            m_queue.activate(first.occurrenceId());
        if (!activated.ok)
            return fromQueueStatus(activated);

        return beginOccurrenceFromProvider(
            first,
            playWhenReady,
            activated.changed);
    }

    QueueSessionStatus active = expectedActiveStatus(*activeId);
    if (!active.ok)
        return active;

    if (sessionBoundTo(*activeId)
        && (m_session.state() == PlaybackState::Loading
            || m_session.state() == PlaybackState::Playing
            || m_session.state() == PlaybackState::Paused)) {
        return QueueSessionStatus::success(false, false);
    }

    bool stoppedTerminal = false;
    if (sessionBoundTo(*activeId)) {
        const PlaybackMutationStatus stopped = m_adapter.stop();
        if (!stopped.ok)
            return fromPlaybackStatus(stopped);
        stoppedTerminal = stopped.changed;
    }

    const QueueOccurrence *activeOccurrence = m_queue.activeOccurrence();
    if (!activeOccurrence) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("activeOccurrence"),
            QStringLiteral("BA1 active occurrence disappeared during start."));
    }

    QueueSessionStatus begun =
        beginOccurrenceFromProvider(
            *activeOccurrence,
            playWhenReady,
            false);
    begun.playbackChanged = stoppedTerminal || begun.playbackChanged;
    return begun;
}

QueueSessionStatus QueueSessionCoordinator::stop()
{
    const std::optional<QueueOccurrenceId> activeId =
        m_queue.activeOccurrenceId();

    if (m_session.activeOccurrence().has_value()
        && (!activeId.has_value()
            || m_session.activeOccurrence()->occurrenceId() != *activeId)) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("playbackOccurrence"),
            QStringLiteral("BA2 binding does not match BA1 active occurrence."));
    }

    if (m_pendingInput.has_value()
        && (!activeId.has_value()
            || m_pendingInput->occurrenceId != *activeId)) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("pendingInput"),
            QStringLiteral("Pending input does not match BA1 active occurrence."));
    }

    const bool openPlayback =
        m_session.activeOccurrence().has_value()
        && (m_session.state() == PlaybackState::Loading
            || m_session.state() == PlaybackState::Playing
            || m_session.state() == PlaybackState::Paused);
    const bool openPending = m_pendingInput.has_value();

    const PlaybackMutationStatus stopped = m_adapter.stop();
    if (!stopped.ok)
        return fromPlaybackStatus(stopped);

    m_pendingInput.reset();

    QVector<CoordinatorTransitionFact> facts;
    if (activeId.has_value() && (openPlayback || openPending)) {
        facts.append(OccurrenceEnded{
            *activeId,
            OccurrenceEndCause::Stopped});
    }

    return QueueSessionStatus::success(
        false,
        stopped.changed,
        std::move(facts));
}

QueueSessionStatus QueueSessionCoordinator::skip()
{
    const std::optional<QueueOccurrenceId> before =
        m_queue.activeOccurrenceId();
    if (!before.has_value()) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::QueueMutationFailed,
            QStringLiteral("activeOccurrenceId"),
            QStringLiteral("user_skip requires an active queue occurrence."));
    }

    const bool playWhenReady = currentPlayWhenReadyIntent();
    const QueueMutationStatus advanced = m_queue.advance();
    if (!advanced.ok)
        return fromQueueStatus(advanced);

    QueueSessionStatus coordinated =
        transitionToActive(
            before,
            playWhenReady,
            OccurrenceEndCause::UserSkip);
    coordinated.queueChanged = advanced.changed;

    if (coordinated.ok && !m_queue.activeOccurrenceId().has_value())
        coordinated.transitionFacts.append(QueueExhausted{});

    return coordinated;
}

QueueSessionStatus QueueSessionCoordinator::awaitInput(
    const QueueOccurrenceId &expectedOccurrenceId,
    bool playWhenReady)
{
    QueueSessionStatus active =
        expectedActiveStatus(expectedOccurrenceId);
    if (!active.ok)
        return active;

    const CoordinatorPendingInput pending{
        expectedOccurrenceId,
        playWhenReady};
    if (m_pendingInput.has_value() && *m_pendingInput == pending)
        return QueueSessionStatus::success(false, false);

    m_pendingInput = pending;
    QVector<CoordinatorTransitionFact> facts;
    facts.append(AwaitingInput{
        expectedOccurrenceId,
        playWhenReady});
    return QueueSessionStatus::success(
        false,
        false,
        std::move(facts));
}

QueueSessionStatus QueueSessionCoordinator::restartActive(
    const QueueOccurrenceId &expectedOccurrenceId,
    const PlaybackLoadInput &input)
{
    QueueSessionStatus active =
        expectedActiveStatus(expectedOccurrenceId);
    if (!active.ok)
        return active;

    const bool hasBinding = sessionBoundTo(expectedOccurrenceId);
    const bool hasPending = pendingFor(expectedOccurrenceId);
    if (!hasBinding && !hasPending) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ReplacementNotArmed,
            QStringLiteral("expectedOccurrenceId"),
            QStringLiteral("Active occurrence has neither a live/terminal BA2 binding nor pending replacement intent."));
    }

    const bool playWhenReady = hasPending
        ? m_pendingInput->playWhenReady
        : m_session.playWhenReady();

    const bool replacingLive =
        hasBinding
        && (m_session.state() == PlaybackState::Loading
            || m_session.state() == PlaybackState::Playing
            || m_session.state() == PlaybackState::Paused);

    QVector<CoordinatorTransitionFact> facts;
    bool playbackChanged = false;
    if (hasBinding) {
        const PlaybackMutationStatus stopped = m_adapter.stop();
        if (!stopped.ok)
            return fromPlaybackStatus(stopped);
        playbackChanged = stopped.changed;
        if (replacingLive) {
            facts.append(OccurrenceEnded{
                expectedOccurrenceId,
                OccurrenceEndCause::Replaced});
        }
    }

    m_pendingInput.reset();

    const QueueOccurrence *occurrence = m_queue.activeOccurrence();
    if (!occurrence
        || occurrence->occurrenceId() != expectedOccurrenceId) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("activeOccurrence"),
            QStringLiteral("BA1 active occurrence changed before replacement begin."),
            false,
            playbackChanged,
            std::move(facts));
    }

    QueueSessionStatus begun =
        beginOccurrenceWithInput(
            *occurrence,
            input,
            playWhenReady,
            false,
            std::move(facts));
    begun.playbackChanged = playbackChanged || begun.playbackChanged;
    return begun;
}

QueueSessionStatus QueueSessionCoordinator::append(QueueOccurrence occurrence)
{
    const auto before = m_queue.activeOccurrenceId();
    const bool playWhenReady = currentPlayWhenReadyIntent();
    return reconcileAfterMutation(
        before,
        m_queue.append(std::move(occurrence)),
        playWhenReady);
}

QueueSessionStatus QueueSessionCoordinator::insert(
    int index,
    QueueOccurrence occurrence)
{
    const auto before = m_queue.activeOccurrenceId();
    const bool playWhenReady = currentPlayWhenReadyIntent();
    return reconcileAfterMutation(
        before,
        m_queue.insert(index, std::move(occurrence)),
        playWhenReady);
}

QueueSessionStatus QueueSessionCoordinator::playNext(
    QueueOccurrence occurrence)
{
    const auto before = m_queue.activeOccurrenceId();
    const bool playWhenReady = currentPlayWhenReadyIntent();
    return reconcileAfterMutation(
        before,
        m_queue.playNext(std::move(occurrence)),
        playWhenReady);
}

QueueSessionStatus QueueSessionCoordinator::playNow(
    QueueOccurrence occurrence)
{
    const auto before = m_queue.activeOccurrenceId();
    return reconcileAfterMutation(
        before,
        m_queue.playNow(std::move(occurrence)),
        true,
        OccurrenceEndCause::Replaced);
}

QueueSessionStatus QueueSessionCoordinator::remove(
    const QueueOccurrenceId &id)
{
    const auto before = m_queue.activeOccurrenceId();
    const bool playWhenReady = currentPlayWhenReadyIntent();
    return reconcileAfterMutation(
        before,
        m_queue.remove(id),
        playWhenReady,
        OccurrenceEndCause::Removed);
}

QueueSessionStatus QueueSessionCoordinator::move(
    const QueueOccurrenceId &id,
    int destinationIndex)
{
    const auto before = m_queue.activeOccurrenceId();
    const bool playWhenReady = currentPlayWhenReadyIntent();
    return reconcileAfterMutation(
        before,
        m_queue.move(id, destinationIndex),
        playWhenReady);
}

QueueSessionStatus QueueSessionCoordinator::clear()
{
    const auto before = m_queue.activeOccurrenceId();
    const bool playWhenReady = currentPlayWhenReadyIntent();
    return reconcileAfterMutation(
        before,
        m_queue.clear(),
        playWhenReady,
        OccurrenceEndCause::Removed);
}

QueueSessionStatus QueueSessionCoordinator::clearAfterCurrent()
{
    const auto before = m_queue.activeOccurrenceId();
    const bool playWhenReady = currentPlayWhenReadyIntent();
    return reconcileAfterMutation(
        before,
        m_queue.clearAfterCurrent(),
        playWhenReady);
}

QueueSessionStatus QueueSessionCoordinator::activate(
    const QueueOccurrenceId &id,
    bool playWhenReady)
{
    const auto before = m_queue.activeOccurrenceId();
    const QueueMutationStatus mutation = m_queue.activate(id);
    if (!mutation.ok)
        return fromQueueStatus(mutation);

    const auto after = m_queue.activeOccurrenceId();
    if (sameOptionalId(before, after)) {
        QueueSessionStatus active = expectedActiveStatus(id);
        if (!active.ok) {
            active.queueChanged = mutation.changed;
            return active;
        }

        if (sessionBoundTo(id))
            return QueueSessionStatus::success(mutation.changed, false);

        const QueueOccurrence *occurrence = m_queue.activeOccurrence();
        if (!occurrence) {
            return QueueSessionStatus::failure(
                QueueSessionErrorCode::ActiveOccurrenceMismatch,
                QStringLiteral("activeOccurrence"),
                QStringLiteral("BA1 active occurrence is unavailable after activation."),
                mutation.changed,
                false);
        }

        return beginOccurrenceFromProvider(
            *occurrence,
            playWhenReady,
            mutation.changed);
    }

    return reconcileAfterMutation(
        before,
        mutation,
        playWhenReady,
        OccurrenceEndCause::Replaced);
}

QueueSessionStatus QueueSessionCoordinator::deactivate()
{
    const auto before = m_queue.activeOccurrenceId();
    return reconcileAfterMutation(
        before,
        m_queue.deactivate(),
        false,
        OccurrenceEndCause::Stopped);
}

QueueSessionStatus QueueSessionCoordinator::engineLoaded(
    const PlaybackWorkToken &token)
{
    return fromPlaybackStatus(m_adapter.engineLoaded(token));
}

QueueSessionStatus QueueSessionCoordinator::enginePauseObserved(
    const PlaybackWorkToken &token,
    bool paused)
{
    return fromPlaybackStatus(
        m_adapter.enginePauseObserved(token, paused));
}

QueueSessionStatus QueueSessionCoordinator::enginePositionObserved(
    const PlaybackWorkToken &token,
    double seconds)
{
    return fromPlaybackStatus(
        m_adapter.enginePositionObserved(token, seconds));
}

QueueSessionStatus QueueSessionCoordinator::engineDurationObserved(
    const PlaybackWorkToken &token,
    double seconds)
{
    return fromPlaybackStatus(
        m_adapter.engineDurationObserved(token, seconds));
}

QueueSessionStatus QueueSessionCoordinator::engineSeekApplied(
    const PlaybackSeekRequest &request,
    double observedPositionSeconds)
{
    return fromPlaybackStatus(
        m_adapter.engineSeekApplied(
            request,
            observedPositionSeconds));
}

QueueSessionStatus QueueSessionCoordinator::engineEnded(
    const PlaybackWorkToken &token)
{
    const bool nextPlayWhenReady = currentPlayWhenReadyIntent();
    const std::optional<QueueOccurrenceId> before =
        m_queue.activeOccurrenceId();

    if (!before.has_value() || *before != token.occurrenceId()) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("activeOccurrence"),
            QStringLiteral("EOF token does not belong to BA1's active occurrence."));
    }

    const PlaybackMutationStatus ended = m_adapter.engineEnded(token);
    if (!ended.ok)
        return fromPlaybackStatus(ended);

    if (!ended.changed)
        return QueueSessionStatus::success(false, false);

    const QueueMutationStatus advanced = m_queue.advance();
    if (!advanced.ok) {
        QueueSessionStatus failed = fromQueueStatus(advanced);
        failed.playbackChanged = true;
        return failed;
    }

    bool queueChanged = advanced.changed;
    bool forceRestart = false;

    if (!m_queue.activeOccurrenceId().has_value()
        && m_repeatMode == CoordinatorRepeatMode::Queue
        && !m_queue.empty()) {
        const QueueOccurrenceId firstId =
            m_queue.items().first().occurrenceId();
        const QueueMutationStatus wrapped = m_queue.activate(firstId);
        if (!wrapped.ok) {
            QueueSessionStatus failed = fromQueueStatus(wrapped);
            failed.queueChanged = queueChanged || wrapped.changed;
            failed.playbackChanged = true;
            return failed;
        }
        queueChanged = queueChanged || wrapped.changed;
        forceRestart = firstId == *before;
    }

    QueueSessionStatus coordinated =
        transitionToActive(
            before,
            nextPlayWhenReady,
            OccurrenceEndCause::Eof,
            forceRestart);
    coordinated.queueChanged = queueChanged;
    coordinated.playbackChanged = true || coordinated.playbackChanged;

    if (coordinated.ok
        && !m_queue.activeOccurrenceId().has_value()) {
        coordinated.transitionFacts.append(QueueExhausted{});
    }

    return coordinated;
}

QueueSessionStatus QueueSessionCoordinator::engineFailed(
    const PlaybackWorkToken &token,
    PlaybackFailure failure)
{
    const std::optional<QueueOccurrenceId> activeId =
        m_queue.activeOccurrenceId();
    if (!activeId.has_value() || *activeId != token.occurrenceId()) {
        return QueueSessionStatus::failure(
            QueueSessionErrorCode::ActiveOccurrenceMismatch,
            QStringLiteral("activeOccurrence"),
            QStringLiteral("Failure token does not belong to BA1's active occurrence."));
    }

    const PlaybackMutationStatus failed =
        m_adapter.engineFailed(token, std::move(failure));
    if (!failed.ok)
        return fromPlaybackStatus(failed);

    if (!failed.changed)
        return QueueSessionStatus::success(false, false);

    QVector<CoordinatorTransitionFact> facts;
    facts.append(OccurrenceEnded{
        *activeId,
        OccurrenceEndCause::Failed});
    return QueueSessionStatus::success(
        false,
        true,
        std::move(facts));
}

} // namespace vinyl
