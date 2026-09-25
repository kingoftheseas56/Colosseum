#pragma once

#include "TrackerTypes.h"

#include "account/ProfilePaths.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

enum class TrackerScrobbleAction : quint8 {
    Start,
    Pause,
    Stop
};

enum class TrackerScrobbleState : quint8 {
    Pending,
    Delivering,
    Succeeded,
    Waiting,
    UnknownOutcome,
    NeedsAttention,
    Superseded
};

enum class TrackerScrobbleReason : quint8 {
    None,
    ProviderUnavailable,
    RetryableKnownNotApplied,
    RetryLimitReached,
    AcknowledgementLost,
    UnsupportedAction,
    MappingChanged,
    StalePlayback,
    ReadbackPresent,
    ReadbackAbsent,
    ReadbackUncertain,
    UserDiscarded
};

enum class TrackerScrobbleAttemptResult : quint8 {
    Succeeded,
    KnownNotApplied,
    UnknownOutcome,
    NeedsAttention
};

enum class TrackerScrobbleReadback : quint8 {
    ExactPresent,
    Absent,
    Indeterminate
};

struct TrackerScrobbleIntent {
    QString operationId;
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
    quint64 connectionGeneration = 0;
    quint64 mappingRevision = 0;
    QString canonicalMediaId;
    QString remoteMediaId;
    QString playbackSessionId;
    bool closesSession = false;
    quint64 playbackGeneration = 0;
    quint64 transitionSequence = 0;
    TrackerScrobbleAction action = TrackerScrobbleAction::Start;
    int progressHundredths = 0;
    bool completedLocally = false;
    qint64 createdAtMs = 0;
    TrackerScrobbleState state = TrackerScrobbleState::Pending;
    TrackerScrobbleReason reason = TrackerScrobbleReason::None;
    int attemptCount = 0;
};

// Profile-private scrobble preference and lifecycle-intent journal. It stores
// no credentials, provider response bodies, Activity, History, or statistics.
class TrackerScrobbleStore final
{
public:
    explicit TrackerScrobbleStore(const ProfilePaths &profile);

    static QString storagePath(const ProfilePaths &profile);
    bool healthy(QString *error = nullptr) const;
    bool enabled(TrackerProviderId providerId, const QString &remoteAccountId) const;
    bool setEnabled(TrackerProviderId providerId,
                    const QString &remoteAccountId,
                    bool enabled,
                    QString *error = nullptr);
    bool removePreferenceFor(TrackerProviderId providerId,
                             const QString &remoteAccountId,
                             QString *error = nullptr);
    QList<TrackerScrobbleIntent> intents() const;
    bool adoptPrivateStateFrom(const TrackerScrobbleStore &source,
                               QString *error = nullptr);
    QList<TrackerScrobbleIntent> unresolvedIntents(TrackerProviderId providerId,
                                                    const QString &remoteAccountId) const;
    std::optional<TrackerScrobbleIntent> intent(const QString &operationId) const;
    bool recordIntent(const TrackerScrobbleIntent &intent, QString *error = nullptr);
    bool deferWithoutAttempt(const QString &operationId,
                             TrackerScrobbleReason reason,
                             QString *error = nullptr);
    bool retryWaitingOnNextEvent(const QString &operationId, QString *error = nullptr);
    bool supersedeIfNeverApplied(const QString &operationId, QString *error = nullptr);
    bool markNeedsAttention(const QString &operationId,
                            TrackerScrobbleReason reason,
                            QString *error = nullptr);
    bool markDelivering(const QString &operationId, QString *error = nullptr);
    bool markSessionClosed(const QString &operationId, QString *error = nullptr);
    bool recordAttemptResult(const QString &operationId,
                             TrackerScrobbleAttemptResult result,
                             TrackerScrobbleReason reason = TrackerScrobbleReason::None,
                             QString *error = nullptr);
    bool reconcileUnknown(const QString &operationId,
                          TrackerScrobbleReadback result,
                          QString *error = nullptr);
    bool recoverUnattemptedIntents(QString *error = nullptr);
    bool hasUnknownOutcome(TrackerProviderId providerId,
                           const QString &remoteAccountId) const;
    bool hasBlockingOutcome(TrackerProviderId providerId,
                            const QString &remoteAccountId) const;
    bool resumeKnownUnsentAfterReconnect(TrackerProviderId providerId,
                                         const QString &remoteAccountId,
                                         quint64 connectionGeneration,
                                         QString *error = nullptr);
    int discardKnownUnsent(TrackerProviderId providerId,
                           const QString &remoteAccountId,
                           QString *error = nullptr);
    bool hasOpenPlayback(TrackerProviderId providerId,
                        const QString &remoteAccountId,
                        const QString &exceptSessionId = {}) const;
    bool hasOpenPlaybackForSession(TrackerProviderId providerId,
                                   const QString &remoteAccountId,
                                   const QString &sessionId) const;

private:
    struct Preference {
        TrackerProviderId providerId = TrackerProviderId::Simkl;
        QString remoteAccountId;
        bool enabled = false;
    };

    bool load();
    bool persist(const QList<Preference> &preferences,
                 const QList<TrackerScrobbleIntent> &intents,
                 QString *error) const;

    ProfilePaths m_profile;
    QString m_path;
    QList<Preference> m_preferences;
    QList<TrackerScrobbleIntent> m_intents;
    bool m_healthy = true;
    QString m_error;
};
