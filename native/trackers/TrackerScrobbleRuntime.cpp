#include "TrackerScrobbleRuntime.h"

#include "TrackerSyncSettingsStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QPointer>

#include <atomic>
#include <cmath>

namespace {

std::atomic<quint64> g_playbackScopeGeneration{0};

bool safeKey(const QString &value, int maximum = 512)
{
    return !value.isEmpty() && value == value.trimmed() && value.size() <= maximum
        && !value.contains(QLatin1Char('\0')) && !value.contains(QLatin1String(".."));
}

bool positiveSimklId(const QString &value)
{
    if (!safeKey(value, 32))
        return false;
    bool ok = false;
    const qulonglong numeric = value.toULongLong(&ok);
    return ok && numeric > 0 && QString::number(numeric) == value;
}

quint64 asUnsigned(const QVariant &value, bool *ok)
{
    bool converted = false;
    const quint64 result = value.toULongLong(&converted);
    if (ok)
        *ok = converted;
    return result;
}

QString actionForIntent(const QString &eventAction,
                        bool completedLocally,
                        int progressHundredths)
{
    if (eventAction == QLatin1String("start") || eventAction == QLatin1String("resume"))
        return QStringLiteral("start");
    if (eventAction == QLatin1String("pause"))
        return QStringLiteral("pause");
    if (eventAction == QLatin1String("close"))
        return completedLocally && progressHundredths >= 9000
            ? QStringLiteral("stop") : QStringLiteral("pause");
    return {};
}

TrackerScrobbleAction actionFromIntent(const QString &action)
{
    if (action == QLatin1String("pause"))
        return TrackerScrobbleAction::Pause;
    if (action == QLatin1String("stop"))
        return TrackerScrobbleAction::Stop;
    return TrackerScrobbleAction::Start;
}

} // namespace

TrackerScrobbleRuntime::TrackerScrobbleRuntime(const ProfilePaths &profile,
                                               TrackerConnectionStore *connections,
                                               TrackerMappingStore *mappings,
                                               SimklScrobbleTransport *transport,
                                               QObject *parent)
    : QObject(parent),
      m_profile(profile),
      m_connections(connections),
      m_mappings(mappings),
      m_transport(transport),
      m_store(profile),
      m_playbackScopeGeneration(g_playbackScopeGeneration.fetch_add(
          1, std::memory_order_relaxed) + 1)
{
    if (m_playbackScopeGeneration == 0)
        m_playbackScopeGeneration = g_playbackScopeGeneration.fetch_add(
            1, std::memory_order_relaxed) + 1;
}

bool TrackerScrobbleRuntime::start(QString *out)
{
    if (out)
        out->clear();
    if (m_started)
        return true;
    QString detail;
    if (!m_store.healthy(&detail) || !m_connections || !m_mappings
        || !m_connections->healthy(&detail) || !m_mappings->healthy(&detail)) {
        setError(detail.isEmpty()
                     ? QStringLiteral("Tracker playback tracking is unavailable for this profile.")
                     : detail);
        if (out)
            *out = m_error;
        return false;
    }
    if (!m_store.recoverUnattemptedIntents(&detail)) {
        setError(detail);
        if (out)
            *out = m_error;
        return false;
    }
    m_started = true;
    m_error.clear();
    return true;
}

QString TrackerScrobbleRuntime::profileId() const
{
    return m_profile.profileId();
}

quint64 TrackerScrobbleRuntime::playbackScopeGeneration() const
{
    return m_playbackScopeGeneration;
}

QString TrackerScrobbleRuntime::lastError() const
{
    return m_error;
}

TrackerScrobbleStore *TrackerScrobbleRuntime::store()
{
    return &m_store;
}

void TrackerScrobbleRuntime::setSyncSettingsStore(TrackerSyncSettingsStore *settings)
{
    m_syncSettings = settings;
}

void TrackerScrobbleRuntime::resumeAfterGlobalSyncEnabled()
{
    if (!m_started || !globalSyncEnabled() || !m_connections)
        return;
    for (const TrackerConnection &connection : m_connections->connections()) {
        if (connection.state == TrackerConnectionState::Connected
            && connection.capabilities.testFlag(TrackerProviderCapability::Scrobble)) {
            dispatchNextPending(connection.remoteAccountId);
        }
    }
}

bool TrackerScrobbleRuntime::prepareForProfileDeactivation()
{
    if (!m_started)
        return true;

    const QStringList remainingSources = m_activePlaybacks.keys();
    for (const QString &source : remainingSources) {
        const auto active = m_activePlaybacks.constFind(source);
        if (active == m_activePlaybacks.cend())
            continue;
        if (!closeActivePlayback(source, active.value())) {
            setError(QStringLiteral("The live SIMKL session could not be safely finalized before profile change."));
            return false;
        }
    }
    return true;
}

bool TrackerScrobbleRuntime::snapshotPlaybackPosition(
    const QString &source,
    const QString &sessionId,
    quint64 scopeGeneration,
    qint64 positionMs,
    qint64 durationMs,
    bool completedLocally)
{
    if (!m_started || scopeGeneration != m_playbackScopeGeneration
        || !safeKey(source, 64) || !safeKey(sessionId, 128)
        || positionMs < 0 || durationMs <= 0) {
        return false;
    }
    auto active = m_activePlaybacks.find(source);
    if (active == m_activePlaybacks.end() || active->sessionId != sessionId)
        return false;

    active->durationMs = durationMs;
    active->positionMs = qBound<qint64>(0, positionMs, durationMs);
    active->completedLocally = active->completedLocally || completedLocally;
    return true;
}

bool TrackerScrobbleRuntime::setLivePlaybackTrackingEnabled(const QString &providerKey,
                                                             bool enabledValue)
{
    QString error;
    const auto providerId = trackerProviderIdFromKey(providerKey);
    if (!m_started || !providerId || !m_connections) {
        setError(QStringLiteral("Tracker playback tracking is unavailable."));
        return false;
    }
    const auto connection = m_connections->connection(*providerId);
    if (!connection || connection->state != TrackerConnectionState::Connected
        || !connection->capabilities.testFlag(TrackerProviderCapability::Scrobble)) {
        setError(QStringLiteral("This tracker does not have an active playback-tracking connection."));
        return false;
    }
    if (!enabledValue && m_store.enabled(*providerId, connection->remoteAccountId)) {
        QStringList activeSources;
        for (auto it = m_activePlaybacks.cbegin(); it != m_activePlaybacks.cend(); ++it) {
            if (it->remoteAccountId == connection->remoteAccountId)
                activeSources.append(it.key());
        }
        for (const QString &source : activeSources) {
            const auto active = m_activePlaybacks.value(source);
            if (!closeActivePlayback(source, active)) {
                setError(QStringLiteral("The live SIMKL session could not be safely closed; tracking remains enabled."));
                return false;
            }
        }
    }
    if (!m_store.setEnabled(*providerId, connection->remoteAccountId, enabledValue, &error)) {
        setError(error);
        return false;
    }
    m_error.clear();
    emit lastErrorChanged();
    return true;
}

bool TrackerScrobbleRuntime::livePlaybackTrackingEnabled(const QString &providerKey) const
{
    const auto providerId = trackerProviderIdFromKey(providerKey);
    if (!providerId || !m_connections)
        return false;
    const auto connection = m_connections->connection(*providerId);
    return connection && connection->state == TrackerConnectionState::Connected
        && connection->capabilities.testFlag(TrackerProviderCapability::Scrobble)
        && m_store.enabled(*providerId, connection->remoteAccountId);
}

std::optional<TrackerTitleMapping> TrackerScrobbleRuntime::exactMovieMapping(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    const QString &itemKey) const
{
    if (!m_mappings || !safeKey(itemKey))
        return std::nullopt;
    const QString canonicalId = QStringLiteral("movie:") + itemKey;
    std::optional<TrackerTitleMapping> exact;
    for (const TrackerTitleMapping &mapping : m_mappings->mappings()) {
        if (mapping.remote.providerId == providerId
            && mapping.remote.remoteAccountId == remoteAccountId
            && mapping.canonical.canonicalMediaId == canonicalId
            && mapping.canonical.historyKind == QLatin1String("movie")
            && mapping.canonical.historyId == itemKey) {
            if (exact || !positiveSimklId(mapping.remote.remoteMediaId))
                return std::nullopt;
            exact = mapping;
        }
    }
    return exact;
}

int TrackerScrobbleRuntime::progressHundredths(qint64 positionMs, qint64 durationMs)
{
    if (durationMs <= 0)
        return -1;
    const long double bounded = static_cast<long double>(qBound<qint64>(0, positionMs, durationMs));
    const long double percentageHundredths = (bounded * 10000.0L) / durationMs;
    return qBound(0, static_cast<int>(std::llround(percentageHundredths)), 10000);
}

void TrackerScrobbleRuntime::observePlaybackLifecycle(const QVariantMap &event)
{
    if (!m_started)
        return;

    bool scopeOk = false;
    bool generationOk = false;
    bool sequenceOk = false;
    const quint64 scope = asUnsigned(event.value(QStringLiteral("scopeGeneration")), &scopeOk);
    const quint64 generation = asUnsigned(
        event.value(QStringLiteral("playbackGeneration")), &generationOk);
    const quint64 sequence = asUnsigned(
        event.value(QStringLiteral("transitionSequence")), &sequenceOk);
    if (!scopeOk || scope != m_playbackScopeGeneration || !generationOk || generation == 0
        || !sequenceOk || sequence == 0) {
        setError(QStringLiteral("A stale or incomplete playback event was ignored."));
        return;
    }

    const QVariantMap identity = event.value(QStringLiteral("identity")).toMap();
    const QString source = identity.value(QStringLiteral("source")).toString();
    const QString world = identity.value(QStringLiteral("world")).toString();
    const QString kind = identity.value(QStringLiteral("kind")).toString();
    const QString itemKey = identity.value(QStringLiteral("itemKey")).toString();
    const QString sessionId = event.value(QStringLiteral("sessionId")).toString();
    const QString eventAction = event.value(QStringLiteral("action")).toString();
    const bool completedLocally = event.value(QStringLiteral("completedLocally")).toBool();
    const bool isStart = eventAction == QLatin1String("start");
    const bool isResume = eventAction == QLatin1String("resume");
    const bool isPause = eventAction == QLatin1String("pause");
    const bool isClose = eventAction == QLatin1String("close");
    if (world != QLatin1String("theatre") || !safeKey(source, 64)
        || !safeKey(sessionId, 128) || (!isStart && !isResume && !isPause && !isClose)) {
        setError(QStringLiteral("A playback lifecycle event was malformed and ignored."));
        return;
    }

    const bool hasActivePlayback = m_activePlaybacks.contains(source);
    const ActivePlayback active = m_activePlaybacks.value(source);
    if (isStart) {
        if (generation <= m_playbackGenerationFloorBySource.value(source, 0)
            || hasActivePlayback) {
            setError(QStringLiteral("A stale playback generation was ignored."));
            return;
        }
    } else if (!hasActivePlayback || active.generation != generation || active.sessionId != sessionId
               || sequence <= active.lastSequence) {
        setError(QStringLiteral("A stale playback transition was ignored."));
        return;
    }

    const auto connection = m_connections->connection(TrackerProviderId::Simkl);
    if (!connection || connection->state != TrackerConnectionState::Connected
        || !connection->capabilities.testFlag(TrackerProviderCapability::Scrobble)
        || !m_store.enabled(TrackerProviderId::Simkl, connection->remoteAccountId)) {
        if (isClose)
            m_activePlaybacks.remove(source);
        return; // default-off and disconnected playback remain entirely local
    }

    if (kind != QLatin1String("movie")) {
        setError(QStringLiteral("SIMKL playback tracking is not enabled for this mapped media shape."));
        if (isClose)
            m_activePlaybacks.remove(source);
        return;
    }

    const qint64 positionMs = event.value(QStringLiteral("positionMs")).toLongLong();
    const qint64 durationMs = event.value(QStringLiteral("durationMs")).toLongLong();
    const int progress = progressHundredths(positionMs, durationMs);
    if (progress < 0) {
        setError(QStringLiteral("Playback tracking needs a confirmed media duration."));
        return;
    }
    const QString action = actionForIntent(eventAction, completedLocally, progress);
    if (action.isEmpty()) {
        setError(QStringLiteral("A playback lifecycle event was malformed and ignored."));
        return;
    }

    if (isStart || isResume) {
        const bool anotherSessionMayBeOpen = m_store.hasOpenPlayback(
            TrackerProviderId::Simkl, connection->remoteAccountId, sessionId);
        if (anotherSessionMayBeOpen) {
            bool protectedByQueuedClose = false;
            for (const TrackerScrobbleIntent &pending : m_store.intents()) {
                if (pending.providerId != TrackerProviderId::Simkl
                    || pending.remoteAccountId != connection->remoteAccountId
                    || pending.playbackSessionId == sessionId || !pending.closesSession
                    || pending.state == TrackerScrobbleState::Succeeded
                    || pending.state == TrackerScrobbleState::Superseded
                    || !m_store.hasOpenPlaybackForSession(
                        TrackerProviderId::Simkl, connection->remoteAccountId,
                        pending.playbackSessionId)) {
                    continue;
                }
                protectedByQueuedClose = true;
                break;
            }
            if (!protectedByQueuedClose) {
                setError(QStringLiteral("Another SIMKL playback session must close before this one starts."));
                return;
            }
        }
    }

    QString remoteMediaId;
    QString canonicalMediaId;
    quint64 mappingRevision = 0;
    if (isStart) {
        const auto mapping = exactMovieMapping(TrackerProviderId::Simkl,
                                                connection->remoteAccountId, itemKey);
        if (!mapping) {
            setError(QStringLiteral("This movie needs one exact SIMKL match before playback tracking."));
            return;
        }
        remoteMediaId = mapping->remote.remoteMediaId;
        canonicalMediaId = mapping->canonical.canonicalMediaId;
        mappingRevision = mapping->revision;
    } else {
        if (active.remoteAccountId != connection->remoteAccountId
            || active.connectionGeneration != connection->connectionGeneration
            || active.canonicalMediaId != QStringLiteral("movie:") + itemKey) {
            setError(QStringLiteral("The SIMKL account or playback identity changed during this session."));
            if (isClose)
                m_activePlaybacks.remove(source);
            return;
        }
        remoteMediaId = active.remoteMediaId;
        canonicalMediaId = active.canonicalMediaId;
        mappingRevision = active.mappingRevision;
    }

    ActivePlayback nextActive = active;
    if (isStart) {
        nextActive = {sessionId, source, connection->remoteAccountId,
                      canonicalMediaId, remoteMediaId,
                      identity, generation, sequence, connection->connectionGeneration,
                      mappingRevision, positionMs, durationMs, completedLocally, {}, -1};
    } else {
        nextActive.lastSequence = sequence;
        nextActive.positionMs = positionMs;
        nextActive.durationMs = durationMs;
        nextActive.completedLocally = nextActive.completedLocally || completedLocally;
    }

    const auto previousIntent = m_store.intent(active.lastOperationId);
    if (isClose && action == QStringLiteral("pause")
        && active.lastAction == QLatin1String("pause")
        && active.lastProgressHundredths == progress && previousIntent
        && previousIntent->state != TrackerScrobbleState::Waiting
        && previousIntent->state != TrackerScrobbleState::Superseded) {
        QString error;
        if (!m_store.markSessionClosed(active.lastOperationId, &error)) {
            setError(error);
            return;
        }
        m_activePlaybacks.remove(source);
        m_error.clear();
        emit lastErrorChanged();
        return;
    }

    if (!supersedeUnsentBeforeCurrentEvent(connection->remoteAccountId, sessionId))
        return;

    const QString material = trackerProviderKey(TrackerProviderId::Simkl) + QChar(0x1f)
        + connection->remoteAccountId + QChar(0x1f)
        + QString::number(connection->connectionGeneration) + QChar(0x1f)
        + canonicalMediaId + QChar(0x1f)
        + remoteMediaId + QChar(0x1f) + sessionId + QChar(0x1f)
        + QString::number(generation) + QChar(0x1f) + QString::number(sequence) + QChar(0x1f)
        + action + QChar(0x1f) + QString::number(progress);
    const QString operationId = QStringLiteral("scrobble-") + QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex());

    TrackerScrobbleIntent intent;
    intent.operationId = operationId;
    intent.providerId = TrackerProviderId::Simkl;
    intent.remoteAccountId = connection->remoteAccountId;
    intent.connectionGeneration = connection->connectionGeneration;
    intent.mappingRevision = mappingRevision;
    intent.canonicalMediaId = canonicalMediaId;
    intent.remoteMediaId = remoteMediaId;
    intent.playbackSessionId = sessionId;
    intent.closesSession = isClose;
    intent.playbackGeneration = generation;
    intent.transitionSequence = sequence;
    intent.action = actionFromIntent(action);
    intent.progressHundredths = progress;
    intent.completedLocally = nextActive.completedLocally;
    intent.createdAtMs = QDateTime::currentMSecsSinceEpoch();

    QString error;
    if (!m_store.recordIntent(intent, &error)) {
        setError(error);
        return;
    }
    if (isStart) {
        // Keep the source's high-water mark after its active session closes.
        // Otherwise a delayed start from an older playback can look new after
        // m_activePlaybacks has removed the only generation record.
        m_playbackGenerationFloorBySource.insert(source, generation);
    }
    nextActive.lastAction = action;
    nextActive.lastProgressHundredths = progress;
    nextActive.lastOperationId = operationId;
    m_activePlaybacks.insert(source, nextActive);
    m_error.clear();
    emit lastErrorChanged();
    if (globalSyncEnabled())
        emit intentReadyForDispatch(operationId);
    if (!retryProtectiveClosesForLaterSessionEvent(connection->remoteAccountId, sessionId))
        return;
    dispatchNextPending(connection->remoteAccountId);
    if (isClose)
        m_activePlaybacks.remove(source);
}

void TrackerScrobbleRuntime::dispatchNextPending(const QString &remoteAccountId)
{
    if (!m_started || !globalSyncEnabled() || m_inFlightAccounts.contains(remoteAccountId))
        return;
    for (const TrackerScrobbleIntent &intent : m_store.intents()) {
        if (intent.providerId != TrackerProviderId::Simkl
            || intent.remoteAccountId != remoteAccountId)
            continue;
        if (intent.state == TrackerScrobbleState::Succeeded
            || intent.state == TrackerScrobbleState::Superseded)
            continue;
        const auto connection = m_connections
            ? m_connections->connection(TrackerProviderId::Simkl) : std::nullopt;
        if (!connection || connection->state != TrackerConnectionState::Connected
            || connection->remoteAccountId != intent.remoteAccountId
            || connection->connectionGeneration != intent.connectionGeneration
            || !connection->capabilities.testFlag(TrackerProviderCapability::Scrobble)) {
            if (intent.state == TrackerScrobbleState::Pending
                || intent.state == TrackerScrobbleState::Waiting) {
                QString error;
                if (!m_store.markNeedsAttention(intent.operationId,
                                                TrackerScrobbleReason::StalePlayback, &error))
                    setError(error);
            }
            return;
        }
        const bool enabledForAccount = m_store.enabled(TrackerProviderId::Simkl,
                                                        intent.remoteAccountId);
        if (!enabledForAccount && !intent.closesSession) {
            if (intent.state == TrackerScrobbleState::Pending
                || intent.state == TrackerScrobbleState::Waiting) {
                QString error;
                if (!m_store.supersedeIfNeverApplied(intent.operationId, &error))
                    setError(error);
                continue;
            }
            return;
        }
        if (intent.state == TrackerScrobbleState::UnknownOutcome) {
            if (!m_transport)
                return;
            m_inFlightAccounts.insert(intent.remoteAccountId);
            const QPointer<TrackerScrobbleRuntime> guard(this);
            m_transport->readback(intent, [guard, operationId = intent.operationId](
                                              SimklScrobbleReadbackResult result) {
                if (guard)
                    guard->completeReadback(operationId, result);
            });
            return;
        }
        if (intent.state == TrackerScrobbleState::Pending) {
            if (!isCurrentMapping(intent)) {
                QString error;
                if (!m_store.markNeedsAttention(intent.operationId,
                                                TrackerScrobbleReason::MappingChanged, &error))
                    setError(error);
                else
                    setError(QStringLiteral("The exact SIMKL match changed before playback tracking could be sent."));
                return;
            }
            dispatchIntent(intent);
        }
        return; // an older unresolved edge must never be overtaken
    }
}

bool TrackerScrobbleRuntime::isCurrentMapping(const TrackerScrobbleIntent &intent) const
{
    constexpr auto prefix = "movie:";
    if (!intent.canonicalMediaId.startsWith(QLatin1String(prefix)))
        return false;
    const QString itemKey = intent.canonicalMediaId.mid(int(sizeof("movie:") - 1));
    const auto mapping = exactMovieMapping(intent.providerId, intent.remoteAccountId, itemKey);
    return mapping && mapping->remote.remoteMediaId == intent.remoteMediaId
        && mapping->canonical.canonicalMediaId == intent.canonicalMediaId
        && mapping->revision == intent.mappingRevision;
}

bool TrackerScrobbleRuntime::supersedeUnsentBeforeCurrentEvent(
    const QString &remoteAccountId,
    const QString &currentSessionId)
{
    for (const TrackerScrobbleIntent &intent : m_store.intents()) {
        if (intent.providerId != TrackerProviderId::Simkl
            || intent.remoteAccountId != remoteAccountId)
            continue;
        if (intent.state != TrackerScrobbleState::Pending
            && intent.state != TrackerScrobbleState::Waiting)
            continue;
        if (intent.playbackSessionId != currentSessionId && intent.closesSession
            && m_store.hasOpenPlaybackForSession(intent.providerId, remoteAccountId,
                                                  intent.playbackSessionId)) {
            continue;
        }
        QString error;
        if (!m_store.supersedeIfNeverApplied(intent.operationId, &error)) {
            setError(error);
            return false;
        }
    }
    return true;
}

bool TrackerScrobbleRuntime::retryProtectiveClosesForLaterSessionEvent(
    const QString &remoteAccountId,
    const QString &currentSessionId)
{
    // Preserve the wait state; without a transport, requeueing an already
    // attempted close cannot be deferred without consuming or corrupting its retry budget.
    if (!m_transport)
        return true;

    for (const TrackerScrobbleIntent &intent : m_store.intents()) {
        if (intent.providerId != TrackerProviderId::Simkl
            || intent.remoteAccountId != remoteAccountId
            || intent.playbackSessionId == currentSessionId || !intent.closesSession
            || intent.state != TrackerScrobbleState::Waiting
            || intent.attemptCount >= 2
            || !m_store.hasOpenPlaybackForSession(intent.providerId, remoteAccountId,
                                                   intent.playbackSessionId)) {
            continue;
        }
        if (intent.reason != TrackerScrobbleReason::ProviderUnavailable
            && intent.reason != TrackerScrobbleReason::RetryableKnownNotApplied) {
            continue;
        }
        QString error;
        if (!m_store.retryWaitingOnNextEvent(intent.operationId, &error)) {
            setError(error);
            return false;
        }
    }
    return true;
}

void TrackerScrobbleRuntime::dispatchIntent(const TrackerScrobbleIntent &intent)
{
    if (!globalSyncEnabled())
        return;
    if (!m_transport) {
        QString error;
        if (!m_store.deferWithoutAttempt(intent.operationId,
                                         TrackerScrobbleReason::ProviderUnavailable,
                                         &error)) {
            setError(error);
        }
        return;
    }

    QString error;
    if (!m_store.markDelivering(intent.operationId, &error)) {
        setError(error);
        return;
    }
    m_inFlightAccounts.insert(intent.remoteAccountId);
    const QPointer<TrackerScrobbleRuntime> guard(this);
    m_transport->send(intent, [guard, operationId = intent.operationId](
                                  SimklScrobbleSendResult result) {
        if (guard)
            guard->completeSend(operationId, result);
    });
}

bool TrackerScrobbleRuntime::globalSyncEnabled() const
{
    if (!m_syncSettings)
        return true;
    return m_syncSettings->healthy()
        && m_syncSettings->globalSettings().trackerSyncEnabled;
}

void TrackerScrobbleRuntime::completeSend(const QString &operationId,
                                           SimklScrobbleSendResult result)
{
    const auto intent = m_store.intent(operationId);
    if (!intent || intent->state != TrackerScrobbleState::Delivering)
        return;
    TrackerScrobbleAttemptResult storedResult = TrackerScrobbleAttemptResult::NeedsAttention;
    TrackerScrobbleReason reason = TrackerScrobbleReason::None;
    switch (result) {
    case SimklScrobbleSendResult::Succeeded:
        storedResult = TrackerScrobbleAttemptResult::Succeeded;
        break;
    case SimklScrobbleSendResult::KnownNotApplied:
        storedResult = TrackerScrobbleAttemptResult::KnownNotApplied;
        reason = TrackerScrobbleReason::RetryableKnownNotApplied;
        break;
    case SimklScrobbleSendResult::UnknownOutcome:
        storedResult = TrackerScrobbleAttemptResult::UnknownOutcome;
        reason = TrackerScrobbleReason::AcknowledgementLost;
        break;
    case SimklScrobbleSendResult::NeedsAttention:
        storedResult = TrackerScrobbleAttemptResult::NeedsAttention;
        reason = TrackerScrobbleReason::UnsupportedAction;
        break;
    }

    QString error;
    if (!m_store.recordAttemptResult(operationId, storedResult, reason, &error)) {
        setError(error);
        m_inFlightAccounts.remove(intent->remoteAccountId);
        return;
    }
    if (result == SimklScrobbleSendResult::UnknownOutcome) {
        if (!globalSyncEnabled()) {
            // The send may have reached SIMKL, so preserve the uncertainty,
            // but do not start a new provider request while global Sync is
            // paused. Explicit resume routes this journaled state through
            // dispatchNextPending(), which performs the required readback.
            m_inFlightAccounts.remove(intent->remoteAccountId);
            return;
        }
        const QPointer<TrackerScrobbleRuntime> guard(this);
        m_transport->readback(*intent, [guard, operationId](SimklScrobbleReadbackResult readback) {
            if (guard)
                guard->completeReadback(operationId, readback);
        });
        return;
    }
    m_inFlightAccounts.remove(intent->remoteAccountId);
    if (result == SimklScrobbleSendResult::Succeeded)
        dispatchNextPending(intent->remoteAccountId);
    else if (result == SimklScrobbleSendResult::KnownNotApplied) {
        const QList<TrackerScrobbleIntent> intents = m_store.intents();
        bool afterFailedIntent = false;
        bool hasLaterPending = false;
        bool hasLaterDifferentSessionEvent = false;
        for (const TrackerScrobbleIntent &candidate : intents) {
            if (candidate.operationId == operationId) {
                afterFailedIntent = true;
                continue;
            }
            if (afterFailedIntent && candidate.providerId == TrackerProviderId::Simkl
                && candidate.remoteAccountId == intent->remoteAccountId
                && candidate.state != TrackerScrobbleState::Succeeded
                && candidate.state != TrackerScrobbleState::Superseded) {
                hasLaterPending = hasLaterPending
                    || candidate.state == TrackerScrobbleState::Pending;
                hasLaterDifferentSessionEvent = hasLaterDifferentSessionEvent
                    || candidate.playbackSessionId != intent->playbackSessionId;
            }
        }
        const auto failedIntent = m_store.intent(operationId);
        const bool protectiveClose = intent->closesSession
            && m_store.hasOpenPlaybackForSession(intent->providerId, intent->remoteAccountId,
                                                  intent->playbackSessionId);
        if (hasLaterDifferentSessionEvent && protectiveClose && failedIntent
            && failedIntent->state == TrackerScrobbleState::Waiting
            && failedIntent->attemptCount < 2) {
            if (!m_store.retryWaitingOnNextEvent(operationId, &error)) {
                setError(error);
                return;
            }
            dispatchNextPending(intent->remoteAccountId);
        } else if (hasLaterDifferentSessionEvent && protectiveClose) {
            setError(QStringLiteral("The previous SIMKL playback must be resolved before another can start."));
        } else if (hasLaterPending && failedIntent
                   && failedIntent->state == TrackerScrobbleState::Waiting) {
            if (!m_store.supersedeIfNeverApplied(operationId, &error)) {
                setError(error);
                return;
            }
            dispatchNextPending(intent->remoteAccountId);
        } else {
            setError(QStringLiteral("SIMKL did not confirm playback tracking; the event is held for recovery."));
        }
    }
    else if (!m_store.enabled(TrackerProviderId::Simkl, intent->remoteAccountId))
        dispatchNextPending(intent->remoteAccountId);
    else
        setError(QStringLiteral("SIMKL did not confirm playback tracking; the event is held for recovery."));
}

void TrackerScrobbleRuntime::completeReadback(const QString &operationId,
                                               SimklScrobbleReadbackResult result)
{
    const auto intent = m_store.intent(operationId);
    if (!intent || intent->state != TrackerScrobbleState::UnknownOutcome)
        return;
    TrackerScrobbleReadback storeResult = TrackerScrobbleReadback::Indeterminate;
    switch (result) {
    case SimklScrobbleReadbackResult::ExactPresent:
        storeResult = TrackerScrobbleReadback::ExactPresent;
        break;
    case SimklScrobbleReadbackResult::Absent:
        storeResult = TrackerScrobbleReadback::Absent;
        break;
    case SimklScrobbleReadbackResult::Indeterminate:
        storeResult = TrackerScrobbleReadback::Indeterminate;
        break;
    }
    QString error;
    if (!m_store.reconcileUnknown(operationId, storeResult, &error)) {
        setError(error);
        m_inFlightAccounts.remove(intent->remoteAccountId);
        return;
    }
    m_inFlightAccounts.remove(intent->remoteAccountId);
    const auto resolved = m_store.intent(operationId);
    if (resolved && (resolved->state == TrackerScrobbleState::Succeeded
                     || resolved->state == TrackerScrobbleState::Superseded))
        dispatchNextPending(intent->remoteAccountId);
    else
        setError(QStringLiteral("SIMKL playback delivery needs attention; it will not be resent automatically."));
}

bool TrackerScrobbleRuntime::closeActivePlayback(const QString &source,
                                                 const ActivePlayback &active)
{
    QVariantMap identity = active.identity;
    identity.insert(QStringLiteral("source"), source);
    QVariantMap event{{QStringLiteral("action"), QStringLiteral("close")},
                      {QStringLiteral("identity"), identity},
                      {QStringLiteral("sessionId"), active.sessionId},
                      {QStringLiteral("scopeGeneration"), m_playbackScopeGeneration},
                      {QStringLiteral("playbackGeneration"), QVariant::fromValue(active.generation)},
                      {QStringLiteral("transitionSequence"), QVariant::fromValue(active.lastSequence + 1)},
                      {QStringLiteral("positionMs"), active.positionMs},
                      {QStringLiteral("durationMs"), active.durationMs},
                      {QStringLiteral("completedLocally"), active.completedLocally}};
    observePlaybackLifecycle(event);
    return !m_activePlaybacks.contains(source);
}

void TrackerScrobbleRuntime::setError(const QString &error)
{
    const QString value = error.isEmpty()
        ? QStringLiteral("SIMKL playback tracking could not save this event.")
        : error;
    if (m_error == value)
        return;
    m_error = value;
    emit lastErrorChanged();
}
