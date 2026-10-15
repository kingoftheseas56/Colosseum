#include "watchparty/WatchPartyPlayerSync.h"

#include <QtGlobal>

#include <cmath>
#include <limits>

namespace Colosseum::WatchParty {

PlayerSyncController::PlayerSyncController(QObject* parent)
    : QObject(parent)
{
}

bool PlayerSyncController::canControlTimeline() const
{
    if (!m_active || m_localParticipantId.isEmpty() || m_hostParticipantId.isEmpty())
        return false;

    return m_localParticipantId == m_hostParticipantId
        || m_controlMode == ControlMode::SharedControl;
}

QString PlayerSyncController::syncStatus() const
{
    if (!m_active)
        return QStringLiteral("inactive");
    return syncStatusName(m_syncStatus);
}

double PlayerSyncController::driftSeconds() const
{
    return m_active ? msToSeconds(m_driftMs) : 0.0;
}

double PlayerSyncController::authoritativePositionSeconds() const
{
    return m_haveAuthoritative
        ? msToSeconds(m_lastAuthoritativePositionMs)
        : 0.0;
}

bool PlayerSyncController::activate(const QString& localParticipantId,
                                    const QString& hostParticipantId,
                                    const QString& controlMode)
{
    ControlMode parsedMode = ControlMode::HostControl;
    const QString localId = localParticipantId.trimmed();
    const QString hostId = hostParticipantId.trimmed();

    if (localId.isEmpty() || hostId.isEmpty()
        || !controlModeFromName(controlMode.trimmed(), &parsedMode)) {
        return false;
    }

    deactivate();

    m_active = true;
    m_localParticipantId = localId;
    m_hostParticipantId = hostId;
    m_controlMode = parsedMode;
    emitChanged();
    Q_EMIT playerObservationRequested();
    return true;
}

void PlayerSyncController::deactivate()
{
    const bool wasActive = m_active;

    m_active = false;
    m_localParticipantId.clear();
    m_hostParticipantId.clear();
    m_controlMode = ControlMode::HostControl;

    m_haveAuthoritative = false;
    m_authoritative = TimelineState();
    m_authoritativeObservedAtMs = -1;
    m_lastAuthoritativePositionMs = 0;

    m_playerReady = false;
    m_playerPaused = true;
    m_playerBuffering = false;
    m_playerSeeking = false;
    m_playerPositionMs = 0;

    m_syncStatus = SyncStatus::Unknown;
    m_driftMs = 0;
    m_catchUpAvailable = false;
    clearCorrectionState();

    if (wasActive)
        emitChanged();
}

bool PlayerSyncController::updateAuthority(const QString& hostParticipantId,
                                           const QString& controlMode)
{
    if (!m_active)
        return false;

    ControlMode parsedMode = ControlMode::HostControl;
    const QString hostId = hostParticipantId.trimmed();
    if (hostId.isEmpty()
        || !controlModeFromName(controlMode.trimmed(), &parsedMode)) {
        return false;
    }

    const bool changed = m_hostParticipantId != hostId
        || m_controlMode != parsedMode;
    m_hostParticipantId = hostId;
    m_controlMode = parsedMode;

    if (changed)
        emitChanged();
    return true;
}

bool PlayerSyncController::requestLocalPlayback(bool playing,
                                                double positionSeconds)
{
    if (!m_active) {
        Q_EMIT controlRejected(QStringLiteral("watch_party_inactive"));
        return false;
    }
    if (!finiteNonNegativeSeconds(positionSeconds)) {
        Q_EMIT controlRejected(QStringLiteral("invalid_position"));
        return false;
    }
    if (!canControlTimeline()) {
        Q_EMIT controlRejected(QStringLiteral("timeline_control_not_authorized"));
        return false;
    }

    Q_EMIT timelineCommandRequested(
        playing ? QStringLiteral("play") : QStringLiteral("pause"),
        true,
        msToSeconds(secondsToMs(positionSeconds)));
    return true;
}

bool PlayerSyncController::requestLocalSeek(double positionSeconds)
{
    if (!m_active) {
        Q_EMIT controlRejected(QStringLiteral("watch_party_inactive"));
        return false;
    }
    if (!finiteNonNegativeSeconds(positionSeconds)) {
        Q_EMIT controlRejected(QStringLiteral("invalid_position"));
        return false;
    }
    if (!canControlTimeline()) {
        Q_EMIT controlRejected(QStringLiteral("timeline_control_not_authorized"));
        return false;
    }

    Q_EMIT timelineCommandRequested(
        QStringLiteral("seek"),
        true,
        msToSeconds(secondsToMs(positionSeconds)));
    return true;
}

bool PlayerSyncController::applyAuthoritativeTimeline(bool playing,
                                                      double positionSeconds,
                                                      qulonglong revision,
                                                      qint64 nowMs)
{
    if (!m_active || nowMs < 0 || !finiteNonNegativeSeconds(positionSeconds))
        return false;
    if (m_haveAuthoritative && revision < m_authoritative.revision)
        return false;

    const bool newerRevision =
        !m_haveAuthoritative || revision > m_authoritative.revision;

    m_authoritative.playing = playing;
    m_authoritative.positionMs = secondsToMs(positionSeconds);
    m_authoritative.revision = revision;
    m_authoritativeObservedAtMs = nowMs;
    m_lastAuthoritativePositionMs = m_authoritative.positionMs;
    m_haveAuthoritative = true;

    if (newerRevision)
        clearCorrectionState();

    reconcile(nowMs, false);
    emitChanged();
    Q_EMIT playerObservationRequested();
    return true;
}

void PlayerSyncController::observePlayer(double positionSeconds,
                                         bool paused,
                                         bool ready,
                                         bool buffering,
                                         bool seeking,
                                         qint64 nowMs)
{
    if (!m_active || nowMs < 0 || !finiteNonNegativeSeconds(positionSeconds))
        return;

    const bool wasBuffering = m_playerBuffering;

    m_playerPositionMs = secondsToMs(positionSeconds);
    m_playerPaused = paused;
    m_playerReady = ready;
    m_playerBuffering = buffering;
    m_playerSeeking = seeking;

    if (m_seekCorrectionPending
        && qAbs(m_playerPositionMs - m_seekCorrectionTargetMs)
            <= kCorrectionSettleMs) {
        m_seekCorrectionPending = false;
        m_seekCorrectionRequestedAtMs = -1;
    }

    if (m_pauseCorrectionPending
        && m_playerPaused == m_pauseCorrectionTarget) {
        m_pauseCorrectionPending = false;
        m_pauseCorrectionRequestedAtMs = -1;
    }

    if (!m_haveAuthoritative) {
        setSyncStatus(SyncStatus::Unknown);
        setCatchUpAvailable(false);
        m_driftMs = 0;
        emitChanged();
        return;
    }

    if (!m_playerReady) {
        setSyncStatus(SyncStatus::Unknown);
        setCatchUpAvailable(false);
        emitChanged();
        return;
    }

    if (m_playerBuffering) {
        m_lastAuthoritativePositionMs = authoritativePositionMsAt(nowMs);
        m_driftMs = m_playerPositionMs - m_lastAuthoritativePositionMs;
        setSyncStatus(SyncStatus::Buffering);
        setCatchUpAvailable(false);
        emitChanged();
        return;
    }

    reconcile(nowMs, wasBuffering && !m_playerBuffering);
    emitChanged();
}

bool PlayerSyncController::catchUp(qint64 nowMs)
{
    if (!m_active || !m_haveAuthoritative || !m_playerReady
        || m_playerBuffering || m_playerSeeking || nowMs < 0) {
        return false;
    }

    reconcile(nowMs, true);
    emitChanged();
    return true;
}

bool PlayerSyncController::finiteNonNegativeSeconds(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return false;

    const double maxSeconds =
        static_cast<double>(std::numeric_limits<qint64>::max()) / 1000.0;
    return seconds <= maxSeconds;
}

qint64 PlayerSyncController::secondsToMs(double seconds)
{
    return qRound64(seconds * 1000.0);
}

double PlayerSyncController::msToSeconds(qint64 milliseconds)
{
    return static_cast<double>(milliseconds) / 1000.0;
}

qint64 PlayerSyncController::authoritativePositionMsAt(qint64 nowMs) const
{
    if (!m_haveAuthoritative)
        return 0;
    if (!m_authoritative.playing || m_authoritativeObservedAtMs < 0)
        return m_authoritative.positionMs;

    const qint64 elapsedMs = qMax<qint64>(
        0, nowMs - m_authoritativeObservedAtMs);
    if (elapsedMs > std::numeric_limits<qint64>::max()
        - m_authoritative.positionMs) {
        return std::numeric_limits<qint64>::max();
    }
    return m_authoritative.positionMs + elapsedMs;
}

void PlayerSyncController::reconcile(qint64 nowMs, bool forceCatchUp)
{
    if (!m_active || !m_haveAuthoritative || !m_playerReady)
        return;

    const qint64 authoritativePositionMs = authoritativePositionMsAt(nowMs);
    m_lastAuthoritativePositionMs = authoritativePositionMs;
    m_driftMs = m_playerPositionMs - authoritativePositionMs;

    if (m_playerBuffering) {
        setSyncStatus(SyncStatus::Buffering);
        setCatchUpAvailable(false);
        return;
    }

    const bool desiredPaused = !m_authoritative.playing;
    const bool pauseMismatch = m_playerPaused != desiredPaused;
    const qint64 absoluteDriftMs = qAbs(m_driftMs);

    const bool pauseRetryDue =
        !m_pauseCorrectionPending
        || m_pauseCorrectionRequestedAtMs < 0
        || nowMs - m_pauseCorrectionRequestedAtMs >= kCorrectionCooldownMs;
    if (pauseMismatch && (forceCatchUp || pauseRetryDue)) {
        m_pauseCorrectionPending = true;
        m_pauseCorrectionTarget = desiredPaused;
        m_pauseCorrectionRequestedAtMs = nowMs;
        Q_EMIT pauseRequested(desiredPaused);
    }

    const qint64 seekThresholdMs =
        forceCatchUp ? kCorrectionSettleMs : kDriftToleranceMs;
    const bool seekRetryDue =
        !m_seekCorrectionPending
        || m_seekCorrectionRequestedAtMs < 0
        || nowMs - m_seekCorrectionRequestedAtMs >= kCorrectionCooldownMs;

    if (!m_playerSeeking
        && absoluteDriftMs > seekThresholdMs
        && (forceCatchUp || seekRetryDue)) {
        m_seekCorrectionPending = true;
        m_seekCorrectionTargetMs = authoritativePositionMs;
        m_seekCorrectionRequestedAtMs = nowMs;
        Q_EMIT seekRequested(msToSeconds(authoritativePositionMs));
    }

    const bool outOfSync =
        pauseMismatch
        || absoluteDriftMs > kDriftToleranceMs
        || m_pauseCorrectionPending
        || m_seekCorrectionPending;

    setSyncStatus(outOfSync ? SyncStatus::Desynced : SyncStatus::InSync);
    setCatchUpAvailable(outOfSync && !m_playerSeeking);
}

void PlayerSyncController::setSyncStatus(SyncStatus status)
{
    m_syncStatus = status;
}

void PlayerSyncController::setCatchUpAvailable(bool available)
{
    m_catchUpAvailable = available;
}

void PlayerSyncController::clearCorrectionState()
{
    m_seekCorrectionPending = false;
    m_seekCorrectionTargetMs = 0;
    m_seekCorrectionRequestedAtMs = -1;

    m_pauseCorrectionPending = false;
    m_pauseCorrectionTarget = true;
    m_pauseCorrectionRequestedAtMs = -1;
}

void PlayerSyncController::emitChanged()
{
    Q_EMIT changed();
}

} // namespace Colosseum::WatchParty
