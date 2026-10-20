#include "VinylBoundaryValidation.h"
#include "VinylListenEventLedger.h"

#include <QChar>

#include <algorithm>
#include <limits>

namespace vinyl {

namespace {

constexpr qint64 kMaxSampleGapMs = 30000;

bool validOpaqueText(const QString &text, qsizetype maxLength)
{
    if (text.isEmpty() || text.size() > maxLength || text.trimmed() != text)
        return false;

    for (const QChar ch : text) {
        if (ch.category() == QChar::Other_Control)
            return false;
    }
    return true;
}

} // namespace

QString listenEventKindName(ListenEventKind kind)
{
    switch (kind) {
    case ListenEventKind::Start:
        return QStringLiteral("start");
    case ListenEventKind::Pause:
        return QStringLiteral("pause");
    case ListenEventKind::Resume:
        return QStringLiteral("resume");
    case ListenEventKind::Seek:
        return QStringLiteral("seek");
    case ListenEventKind::Skip:
        return QStringLiteral("skip");
    case ListenEventKind::Stop:
        return QStringLiteral("stop");
    case ListenEventKind::Failure:
        return QStringLiteral("failure");
    case ListenEventKind::Completion:
        return QStringLiteral("completion");
    case ListenEventKind::SourceChange:
        return QStringLiteral("source_change");
    case ListenEventKind::ListeningSpan:
        return QStringLiteral("listening_span");
    }
    return QStringLiteral("unknown");
}

QString listenLedgerErrorCodeName(ListenLedgerErrorCode code)
{
    switch (code) {
    case ListenLedgerErrorCode::None:
        return QStringLiteral("none");
    case ListenLedgerErrorCode::InvalidSessionId:
        return QStringLiteral("invalid_session_id");
    case ListenLedgerErrorCode::InvalidSourceTraceId:
        return QStringLiteral("invalid_source_trace_id");
    case ListenLedgerErrorCode::InvalidTimestamp:
        return QStringLiteral("invalid_timestamp");
    case ListenLedgerErrorCode::InvalidPosition:
        return QStringLiteral("invalid_position");
    case ListenLedgerErrorCode::InvalidRate:
        return QStringLiteral("invalid_rate");
    case ListenLedgerErrorCode::ContextMismatch:
        return QStringLiteral("context_mismatch");
    case ListenLedgerErrorCode::ProfileMismatch:
        return QStringLiteral("profile_mismatch");
    case ListenLedgerErrorCode::NoActiveContext:
        return QStringLiteral("no_active_context");
    case ListenLedgerErrorCode::StaleContext:
        return QStringLiteral("stale_context");
    case ListenLedgerErrorCode::InvalidTransition:
        return QStringLiteral("invalid_transition");
    case ListenLedgerErrorCode::CounterExhausted:
        return QStringLiteral("counter_exhausted");
    }
    return QStringLiteral("unknown");
}

ListenLedgerResult<ListenSessionId> ListenSessionId::parse(const QString &text)
{
    if (!validOpaqueText(text, 128)) {
        return ListenLedgerResult<ListenSessionId>::failure(
            ListenLedgerErrorCode::InvalidSessionId,
            QStringLiteral("sessionId"),
            QStringLiteral("Listen session ID must be non-empty, bounded, trimmed, and contain no control characters."));
    }
    return ListenLedgerResult<ListenSessionId>::success(ListenSessionId(text));
}

ListenLedgerResult<ListenSourceTraceId> ListenSourceTraceId::parse(
    const QString &text)
{
    if (!validOpaqueText(text, 256)) {
        return ListenLedgerResult<ListenSourceTraceId>::failure(
            ListenLedgerErrorCode::InvalidSourceTraceId,
            QStringLiteral("sourceTraceId"),
            QStringLiteral("Source trace ID must be non-empty, bounded, trimmed, and contain no control characters."));
    }
    return ListenLedgerResult<ListenSourceTraceId>::success(
        ListenSourceTraceId(text));
}

ListenLedgerResult<ListenContext> ListenContext::create(
    ProfileScopeId profileId,
    ListenSessionId sessionId,
    QueueOccurrence occurrence,
    PlaybackWorkToken workToken,
    PlayInstanceId playInstanceId,
    std::optional<PlayInstanceId> continuationOf,
    std::optional<catalogue::LastKnownDisplaySnapshot> displaySnapshot)
{
    if (displaySnapshot.has_value() && !boundary::display(displaySnapshot->primaryText, displaySnapshot->artistCredit))
        return ListenLedgerResult<ListenContext>::failure(ListenLedgerErrorCode::ContextMismatch, QStringLiteral("displaySnapshot"));
    if (workToken.generation() == 0) {
        return ListenLedgerResult<ListenContext>::failure(
            ListenLedgerErrorCode::ContextMismatch,
            QStringLiteral("workToken.generation"),
            QStringLiteral("A listen context requires a live BA2 playback generation."));
    }

    if (occurrence.occurrenceId() != workToken.occurrenceId()) {
        return ListenLedgerResult<ListenContext>::failure(
            ListenLedgerErrorCode::ContextMismatch,
            QStringLiteral("workToken.occurrenceId"),
            QStringLiteral("Queue occurrence and BA2 work token must identify the same exact occurrence."));
    }
    if (continuationOf.has_value() && *continuationOf == playInstanceId) {
        return ListenLedgerResult<ListenContext>::failure(
            ListenLedgerErrorCode::ContextMismatch,
            QStringLiteral("continuationOf"),
            QStringLiteral("A play instance cannot continue itself."));
    }

    return ListenLedgerResult<ListenContext>::success(
        ListenContext(std::move(profileId),
                      std::move(sessionId),
                      std::move(occurrence),
                      std::move(workToken),
                      std::move(playInstanceId),
                      std::move(continuationOf),
                      std::move(displaySnapshot)));
}

qint64 ListenEventLedger::totalListeningMs() const noexcept
{
    qint64 total = 0;
    for (const ListenEvent &event : m_events) {
        if (event.kind() != ListenEventKind::ListeningSpan
            || !event.activeMs().has_value()) {
            continue;
        }
        total += *event.activeMs();
    }
    return total;
}

ListenLedgerMutationStatus ListenEventLedger::append(
    ListenEventKind kind,
    const ListenContext &context,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    std::optional<qint64> fromPositionMs,
    std::optional<qint64> toPositionMs,
    std::optional<qint64> activeMs,
    std::optional<ListenSourceTraceId> sourceBefore,
    std::optional<ListenSourceTraceId> sourceAfter)
{
    if (context.profileId() != m_profileId) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::ProfileMismatch,
            QStringLiteral("profileId"),
            QStringLiteral("A profile-owned listen ledger cannot accept another profile's event."));
    }
    if (observedAtMs < 0) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTimestamp,
            QStringLiteral("observedAtMs"));
    }
    if (monotonicElapsedMs < 0) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTimestamp,
            QStringLiteral("monotonicElapsedMs"),
            QStringLiteral("Monotonic elapsed time must be non-negative."));
    }
    if (positionMs < 0
        || (fromPositionMs.has_value() && *fromPositionMs < 0)
        || (toPositionMs.has_value() && *toPositionMs < 0)) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidPosition,
            QStringLiteral("positionMs"));
    }
    if (activeMs.has_value() && *activeMs <= 0) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTimestamp,
            QStringLiteral("activeMs"),
            QStringLiteral("Qualified listening spans must be positive."));
    }
    if (m_nextSequence == 0) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::CounterExhausted,
            QStringLiteral("sequence"));
    }

    const quint64 sequence = m_nextSequence;
    ++m_nextSequence;
    m_events.append(ListenEvent(
        kind,
        context,
        sequence,
        observedAtMs,
        monotonicElapsedMs,
        positionMs,
        fromPositionMs,
        toPositionMs,
        activeMs,
        std::move(sourceBefore),
        std::move(sourceAfter)));
    return ListenLedgerMutationStatus::success();
}

ListenLedgerMutationStatus ListenEventLedger::appendOrExtendListeningSpan(
    const ListenContext &context,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 fromPositionMs,
    qint64 toPositionMs,
    qint64 activeMs)
{
    if (!m_events.isEmpty()) {
        ListenEvent &last = m_events.last();
        if (last.kind() == ListenEventKind::ListeningSpan
            && last.context().workToken() == context.workToken()
            && last.toPositionMs().has_value()
            && *last.toPositionMs() == fromPositionMs) {
            if (observedAtMs < 0 || monotonicElapsedMs < 0
                || toPositionMs < 0 || activeMs <= 0) {
                return ListenLedgerMutationStatus::failure(
                    ListenLedgerErrorCode::InvalidTimestamp,
                    QStringLiteral("listeningSpan"));
            }
            last.m_observedAtMs = observedAtMs;
            last.m_monotonicElapsedMs = monotonicElapsedMs;
            last.m_positionMs = toPositionMs;
            last.m_toPositionMs = toPositionMs;
            last.m_activeMs = last.m_activeMs.value_or(0) + activeMs;
            return ListenLedgerMutationStatus::success();
        }
    }

    return append(ListenEventKind::ListeningSpan,
                  context,
                  observedAtMs,
                  monotonicElapsedMs,
                  toPositionMs,
                  fromPositionMs,
                  toPositionMs,
                  activeMs);
}

ListenLedgerMutationStatus ListenEventRecorder::validateCurrent(
    const PlaybackWorkToken &token) const
{
    if (!m_context.has_value()) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::NoActiveContext,
            QStringLiteral("workToken"));
    }
    if (token != m_context->workToken()) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::StaleContext,
            QStringLiteral("workToken"),
            QStringLiteral("Observation does not belong to the active queue occurrence and playback generation."));
    }
    return ListenLedgerMutationStatus::success(false);
}

ListenLedgerMutationStatus ListenEventRecorder::validateObservation(
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli) const
{
    if (observedAtMs < 0) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTimestamp,
            QStringLiteral("observedAtMs"));
    }
    if (monotonicElapsedMs < 0) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTimestamp,
            QStringLiteral("monotonicElapsedMs"));
    }
    if (positionMs < 0) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidPosition,
            QStringLiteral("positionMs"));
    }
    if (rateMilli <= 0) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidRate,
            QStringLiteral("rateMilli"));
    }
    return ListenLedgerMutationStatus::success(false);
}

void ListenEventRecorder::resetBaseline(qint64 monotonicElapsedMs,
                                        qint64 positionMs,
                                        qint64 rateMilli)
{
    m_baseline = Baseline{monotonicElapsedMs, positionMs, rateMilli};
}

ListenLedgerMutationStatus ListenEventRecorder::accountTo(
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    if (!m_consuming)
        return ListenLedgerMutationStatus::success(false);

    if (!m_baseline.has_value()) {
        resetBaseline(monotonicElapsedMs, positionMs, rateMilli);
        return ListenLedgerMutationStatus::success(false);
    }

    const Baseline prior = *m_baseline;
    const qint64 elapsedMs = monotonicElapsedMs - prior.monotonicElapsedMs;
    const qint64 contentDeltaMs = positionMs - prior.positionMs;

    if (rateMilli != prior.rateMilli
        || elapsedMs <= 0
        || elapsedMs > kMaxSampleGapMs
        || contentDeltaMs <= 0) {
        resetBaseline(monotonicElapsedMs, positionMs, rateMilli);
        return ListenLedgerMutationStatus::success(false);
    }

    const long double equivalent =
        (static_cast<long double>(contentDeltaMs) * 1000.0L)
        / static_cast<long double>(rateMilli);
    const qint64 contentEquivalentWallMs =
        equivalent >= static_cast<long double>(
                          std::numeric_limits<qint64>::max())
        ? std::numeric_limits<qint64>::max()
        : static_cast<qint64>(equivalent);
    const qint64 qualifiedMs =
        std::min(elapsedMs, std::max<qint64>(0, contentEquivalentWallMs));

    if (qualifiedMs <= 0) {
        resetBaseline(monotonicElapsedMs, positionMs, rateMilli);
        return ListenLedgerMutationStatus::success(false);
    }

    const ListenLedgerMutationStatus appended =
        m_ledger.appendOrExtendListeningSpan(
            *m_context,
            observedAtMs,
            monotonicElapsedMs,
            prior.positionMs,
            positionMs,
            qualifiedMs);
    if (!appended.ok)
        return appended;

    resetBaseline(monotonicElapsedMs, positionMs, rateMilli);
    return appended;
}

ListenLedgerMutationStatus ListenEventRecorder::start(
    ListenContext context,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli,
    std::optional<ListenSourceTraceId> source)
{
    if (context.profileId() != m_ledger.profileId()) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::ProfileMismatch,
            QStringLiteral("profileId"));
    }

    const ListenLedgerMutationStatus valid =
        validateObservation(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
    if (!valid.ok)
        return valid;

    if (m_context.has_value()
        && context.workToken() == m_context->workToken()) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTransition,
            QStringLiteral("start"),
            QStringLiteral("The active playback generation cannot be started twice."));
    }

    if (context.continuationOf().has_value() && m_context.has_value()) {
        if (*context.continuationOf() != m_context->playInstanceId()
            || context.occurrenceId() != m_context->occurrenceId()) {
            return ListenLedgerMutationStatus::failure(
                ListenLedgerErrorCode::InvalidTransition,
                QStringLiteral("continuationOf"),
                QStringLiteral("A continuation must name the immediately replaced play instance for the same queue occurrence."));
        }
    }

    const ListenLedgerMutationStatus appended = m_ledger.append(
        ListenEventKind::Start,
        context,
        observedAtMs,
        monotonicElapsedMs,
        positionMs,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        source);
    if (!appended.ok)
        return appended;

    m_context = std::move(context);
    m_source = std::move(source);
    m_consuming = true;
    resetBaseline(monotonicElapsedMs, positionMs, rateMilli);
    return appended;
}

ListenLedgerMutationStatus ListenEventRecorder::sample(
    const PlaybackWorkToken &token,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    const ListenLedgerMutationStatus current = validateCurrent(token);
    if (!current.ok)
        return current;

    const ListenLedgerMutationStatus valid =
        validateObservation(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
    if (!valid.ok)
        return valid;

    if (!m_consuming)
        return ListenLedgerMutationStatus::success(false);

    return accountTo(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
}

ListenLedgerMutationStatus ListenEventRecorder::pause(
    const PlaybackWorkToken &token,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    const ListenLedgerMutationStatus current = validateCurrent(token);
    if (!current.ok)
        return current;
    if (!m_consuming) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTransition,
            QStringLiteral("pause"),
            QStringLiteral("Pause requires a currently consuming listen context."));
    }

    const ListenLedgerMutationStatus valid =
        validateObservation(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
    if (!valid.ok)
        return valid;

    const ListenLedgerMutationStatus accounted =
        accountTo(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
    if (!accounted.ok)
        return accounted;

    const ListenLedgerMutationStatus appended = m_ledger.append(
        ListenEventKind::Pause,
        *m_context,
        observedAtMs,
        monotonicElapsedMs,
        positionMs);
    if (!appended.ok)
        return appended;

    m_consuming = false;
    clearBaseline();
    return appended;
}

ListenLedgerMutationStatus ListenEventRecorder::resume(
    const PlaybackWorkToken &token,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    const ListenLedgerMutationStatus current = validateCurrent(token);
    if (!current.ok)
        return current;
    if (m_consuming) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTransition,
            QStringLiteral("resume"),
            QStringLiteral("Resume requires a paused listen context."));
    }

    const ListenLedgerMutationStatus valid =
        validateObservation(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
    if (!valid.ok)
        return valid;

    const ListenLedgerMutationStatus appended = m_ledger.append(
        ListenEventKind::Resume,
        *m_context,
        observedAtMs,
        monotonicElapsedMs,
        positionMs);
    if (!appended.ok)
        return appended;

    m_consuming = true;
    resetBaseline(monotonicElapsedMs, positionMs, rateMilli);
    return appended;
}

ListenLedgerMutationStatus ListenEventRecorder::seek(
    const PlaybackWorkToken &token,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 fromPositionMs,
    qint64 toPositionMs,
    qint64 rateMilli)
{
    const ListenLedgerMutationStatus current = validateCurrent(token);
    if (!current.ok)
        return current;

    const ListenLedgerMutationStatus valid =
        validateObservation(observedAtMs, monotonicElapsedMs, fromPositionMs, rateMilli);
    if (!valid.ok)
        return valid;
    if (toPositionMs < 0) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidPosition,
            QStringLiteral("toPositionMs"));
    }

    if (m_consuming) {
        const ListenLedgerMutationStatus accounted =
            accountTo(observedAtMs, monotonicElapsedMs, fromPositionMs, rateMilli);
        if (!accounted.ok)
            return accounted;
    }

    const ListenLedgerMutationStatus appended = m_ledger.append(
        ListenEventKind::Seek,
        *m_context,
        observedAtMs,
        monotonicElapsedMs,
        toPositionMs,
        fromPositionMs,
        toPositionMs);
    if (!appended.ok)
        return appended;

    if (m_consuming)
        resetBaseline(monotonicElapsedMs, toPositionMs, rateMilli);
    else
        clearBaseline();
    return appended;
}

ListenLedgerMutationStatus ListenEventRecorder::sourceChanged(
    const PlaybackWorkToken &token,
    ListenSourceTraceId newSource,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    const ListenLedgerMutationStatus current = validateCurrent(token);
    if (!current.ok)
        return current;

    const ListenLedgerMutationStatus valid =
        validateObservation(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
    if (!valid.ok)
        return valid;

    if (m_source.has_value() && *m_source == newSource)
        return ListenLedgerMutationStatus::success(false);

    if (m_consuming) {
        const ListenLedgerMutationStatus accounted =
            accountTo(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
        if (!accounted.ok)
            return accounted;
    }

    const ListenLedgerMutationStatus appended = m_ledger.append(
        ListenEventKind::SourceChange,
        *m_context,
        observedAtMs,
        monotonicElapsedMs,
        positionMs,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        m_source,
        newSource);
    if (!appended.ok)
        return appended;

    m_source = std::move(newSource);
    if (m_consuming)
        resetBaseline(monotonicElapsedMs, positionMs, rateMilli);
    else
        clearBaseline();
    return appended;
}

ListenLedgerMutationStatus ListenEventRecorder::fallbackSourceChangedAfterFailure(
    ListenSourceTraceId failedSource,
    ListenSourceTraceId replacementSource,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    if (m_context.has_value()) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTransition,
            QStringLiteral("fallbackSourceChangedAfterFailure"),
            QStringLiteral("Post-failure source change requires a closed BA4 context."));
    }
    if (m_ledger.events().isEmpty()
        || m_ledger.events().constLast().kind() != ListenEventKind::Failure) {
        return ListenLedgerMutationStatus::failure(
            ListenLedgerErrorCode::InvalidTransition,
            QStringLiteral("fallbackSourceChangedAfterFailure"),
            QStringLiteral("Immediate prior BA4 fact must be Failure."));
    }

    const ListenLedgerMutationStatus valid =
        validateObservation(
            observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
    if (!valid.ok)
        return valid;

    const ListenEvent &failure = m_ledger.events().constLast();
    return m_ledger.append(
        ListenEventKind::SourceChange,
        failure.context(),
        observedAtMs,
        monotonicElapsedMs,
        positionMs,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::move(failedSource),
        std::move(replacementSource));
}

ListenLedgerMutationStatus ListenEventRecorder::terminate(
    ListenEventKind kind,
    const PlaybackWorkToken &token,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    const ListenLedgerMutationStatus current = validateCurrent(token);
    if (!current.ok)
        return current;

    const ListenLedgerMutationStatus valid =
        validateObservation(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
    if (!valid.ok)
        return valid;

    if (m_consuming) {
        const ListenLedgerMutationStatus accounted =
            accountTo(observedAtMs, monotonicElapsedMs, positionMs, rateMilli);
        if (!accounted.ok)
            return accounted;
    }

    const ListenLedgerMutationStatus appended = m_ledger.append(
        kind,
        *m_context,
        observedAtMs,
        monotonicElapsedMs,
        positionMs);
    if (!appended.ok)
        return appended;

    clearContext();
    return appended;
}

void ListenEventRecorder::clearContext() noexcept
{
    m_context.reset();
    m_source.reset();
    m_baseline.reset();
    m_consuming = false;
}

ListenLedgerMutationStatus ListenEventRecorder::skip(
    const PlaybackWorkToken &token,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    return terminate(
        ListenEventKind::Skip,
        token,
        observedAtMs,
        monotonicElapsedMs,
        positionMs,
        rateMilli);
}

ListenLedgerMutationStatus ListenEventRecorder::stop(
    const PlaybackWorkToken &token,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    return terminate(
        ListenEventKind::Stop,
        token,
        observedAtMs,
        monotonicElapsedMs,
        positionMs,
        rateMilli);
}

ListenLedgerMutationStatus ListenEventRecorder::failure(
    const PlaybackWorkToken &token,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    return terminate(
        ListenEventKind::Failure,
        token,
        observedAtMs,
        monotonicElapsedMs,
        positionMs,
        rateMilli);
}

ListenLedgerMutationStatus ListenEventRecorder::complete(
    const PlaybackWorkToken &token,
    qint64 observedAtMs,
    qint64 monotonicElapsedMs,
    qint64 positionMs,
    qint64 rateMilli)
{
    return terminate(
        ListenEventKind::Completion,
        token,
        observedAtMs,
        monotonicElapsedMs,
        positionMs,
        rateMilli);
}

} // namespace vinyl
