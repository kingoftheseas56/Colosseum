#pragma once

#include "watchparty/WatchPartyTypes.h"

#include <QObject>
#include <QString>

namespace Colosseum::WatchParty {

// Narrow Player 1 timeline coordinator.
//
// Room/network/account owners remain outside this class. They feed authoritative
// room state in; this object decides whether the local shipped player needs a
// pause/seek correction and exposes explicit user-intent requests back out.
// Ordinary mpv property changes never become room commands by observation alone.
class PlayerSyncController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool canControlTimeline READ canControlTimeline NOTIFY changed)
    Q_PROPERTY(QString syncStatus READ syncStatus NOTIFY changed)
    Q_PROPERTY(double driftSeconds READ driftSeconds NOTIFY changed)
    Q_PROPERTY(bool catchUpAvailable READ catchUpAvailable NOTIFY changed)
    Q_PROPERTY(bool authoritativePlaying READ authoritativePlaying NOTIFY changed)
    Q_PROPERTY(double authoritativePositionSeconds READ authoritativePositionSeconds NOTIFY changed)
    Q_PROPERTY(qulonglong authoritativeRevision READ authoritativeRevision NOTIFY changed)

public:
    explicit PlayerSyncController(QObject* parent = nullptr);

    bool active() const { return m_active; }
    bool canControlTimeline() const;
    QString syncStatus() const;
    double driftSeconds() const;
    bool catchUpAvailable() const { return m_catchUpAvailable; }
    bool authoritativePlaying() const { return m_authoritative.playing; }
    double authoritativePositionSeconds() const;
    qulonglong authoritativeRevision() const { return m_authoritative.revision; }

    Q_INVOKABLE bool activate(const QString& localParticipantId,
                              const QString& hostParticipantId,
                              const QString& controlMode);
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE bool updateAuthority(const QString& hostParticipantId,
                                     const QString& controlMode);

    // Explicit local user intent only. These methods authorize and emit a room
    // command request; they never mutate Player 1 directly.
    Q_INVOKABLE bool requestLocalPlayback(bool playing, double positionSeconds);
    Q_INVOKABLE bool requestLocalSeek(double positionSeconds);

    // Authoritative room state enters here. nowMs is supplied by the caller so
    // policy tests stay deterministic; elapsed playing time is extrapolated from
    // this receipt point until the next authoritative update.
    Q_INVOKABLE bool applyAuthoritativeTimeline(bool playing,
                                                double positionSeconds,
                                                qulonglong revision,
                                                qint64 nowMs);

    // Player 1 observation only. This never emits a room command. ready=false
    // covers local lifecycle interruptions (startup/minimize/resume overlay);
    // buffering=true suppresses correction until playback is viable again.
    Q_INVOKABLE void observePlayer(double positionSeconds,
                                   bool paused,
                                   bool ready,
                                   bool buffering,
                                   bool seeking,
                                   qint64 nowMs);

    Q_INVOKABLE bool catchUp(qint64 nowMs);

Q_SIGNALS:
    void changed();
    // Requests one fresh Player 1 observation after activation or authoritative
    // room input. This is deliberately separate from changed() so observation
    // cannot recurse through its own property-notification signal.
    void playerObservationRequested();

    // Slice 4+ room transport consumes this. type is "play", "pause", or "seek";
    // positionSeconds is present for every emitted command in this adapter.
    void timelineCommandRequested(QString type,
                                  bool hasPosition,
                                  double positionSeconds);

    // PlayerPage consumes only these two inbound application requests.
    void seekRequested(double positionSeconds);
    void pauseRequested(bool paused);

    void controlRejected(QString reason);

private:
    static constexpr qint64 kDriftToleranceMs = 1'000;
    static constexpr qint64 kCorrectionSettleMs = 250;
    static constexpr qint64 kCorrectionCooldownMs = 2'000;

    static bool finiteNonNegativeSeconds(double seconds);
    static qint64 secondsToMs(double seconds);
    static double msToSeconds(qint64 milliseconds);

    qint64 authoritativePositionMsAt(qint64 nowMs) const;
    void reconcile(qint64 nowMs, bool forceCatchUp);
    void setSyncStatus(SyncStatus status);
    void setCatchUpAvailable(bool available);
    void clearCorrectionState();
    void emitChanged();

    bool m_active = false;
    QString m_localParticipantId;
    QString m_hostParticipantId;
    ControlMode m_controlMode = ControlMode::HostControl;

    bool m_haveAuthoritative = false;
    TimelineState m_authoritative;
    qint64 m_authoritativeObservedAtMs = -1;
    qint64 m_lastAuthoritativePositionMs = 0;

    bool m_playerReady = false;
    bool m_playerPaused = true;
    bool m_playerBuffering = false;
    bool m_playerSeeking = false;
    qint64 m_playerPositionMs = 0;

    SyncStatus m_syncStatus = SyncStatus::Unknown;
    qint64 m_driftMs = 0;
    bool m_catchUpAvailable = false;

    bool m_seekCorrectionPending = false;
    qint64 m_seekCorrectionTargetMs = 0;
    qint64 m_seekCorrectionRequestedAtMs = -1;

    bool m_pauseCorrectionPending = false;
    bool m_pauseCorrectionTarget = true;
    qint64 m_pauseCorrectionRequestedAtMs = -1;
};

} // namespace Colosseum::WatchParty
