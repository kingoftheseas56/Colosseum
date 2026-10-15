#pragma once

#include "TrackerScrobbleStore.h"
#include "TrackerConnectionStore.h"
#include "TrackerMappingStore.h"

#include <QObject>
#include <QHash>
#include <QSet>
#include <QVariantMap>

#include <functional>

class TrackerSyncSettingsStore;

enum class SimklScrobbleSendResult : quint8 {
    Succeeded,
    KnownNotApplied,
    UnknownOutcome,
    NeedsAttention
};

enum class SimklScrobbleReadbackResult : quint8 {
    ExactPresent,
    Absent,
    Indeterminate
};

// Narrow native seam for the SIMKL lifecycle API. Production composition uses
// the registered AUTH V2 client; tests can inject a deterministic fake and
// never contact SIMKL.
class SimklScrobbleTransport
{
public:
    using SendCompletion = std::function<void(SimklScrobbleSendResult)>;
    using ReadbackCompletion = std::function<void(SimklScrobbleReadbackResult)>;

    virtual ~SimklScrobbleTransport() = default;
    virtual void send(const TrackerScrobbleIntent &intent, SendCompletion completion) = 0;
    virtual void readback(const TrackerScrobbleIntent &intent,
                          ReadbackCompletion completion) = 0;
};

// Converts native player lifecycle facts into provider-qualified, profile-scoped
// intents. It never owns canonical playback/history.
class TrackerScrobbleRuntime final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString profileId READ profileId CONSTANT)
    Q_PROPERTY(quint64 playbackScopeGeneration READ playbackScopeGeneration CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool suppressLegacyTrackerPlaybackRelay READ suppressLegacyTrackerPlaybackRelay
               NOTIFY suppressLegacyTrackerPlaybackRelayChanged)

public:
    TrackerScrobbleRuntime(const ProfilePaths &profile,
                           TrackerConnectionStore *connections,
                           TrackerMappingStore *mappings,
                           SimklScrobbleTransport *transport = nullptr,
                           QObject *parent = nullptr);

    bool start(QString *error = nullptr);
    QString profileId() const;
    quint64 playbackScopeGeneration() const;
    QString lastError() const;
    TrackerScrobbleStore *store();

    Q_INVOKABLE bool setLivePlaybackTrackingEnabled(const QString &providerKey, bool enabled);
    Q_INVOKABLE bool livePlaybackTrackingEnabled(const QString &providerKey) const;
    Q_INVOKABLE void observePlaybackLifecycle(const QVariantMap &event);
    Q_INVOKABLE bool snapshotPlaybackPosition(const QString &source,
                                              const QString &sessionId,
                                              quint64 scopeGeneration,
                                              qint64 positionMs,
                                              qint64 durationMs,
                                              bool completedLocally);
    bool prepareForProfileDeactivation();
    void setSyncSettingsStore(TrackerSyncSettingsStore *settings);
    void resumeAfterGlobalSyncEnabled();
    void setProviderTransport(TrackerProviderId providerId, SimklScrobbleTransport *transport);
    bool suppressLegacyTrackerPlaybackRelay() const;
    void refreshLegacyPlaybackRelaySuppression();

signals:
    void suppressLegacyTrackerPlaybackRelayChanged();
    void lastErrorChanged();
    // A native adapter may consume the persisted intent. The operation ID is
    // the only signal payload; no provider request or credential enters QML.
    void intentReadyForDispatch(const QString &operationId);

private:
    struct ActivePlayback {
        QString sessionId;
        QString source;
        QString remoteAccountId;
        QString canonicalMediaId;
        QString remoteMediaId;
        QVariantMap identity;
        quint64 generation = 0;
        quint64 lastSequence = 0;
        quint64 connectionGeneration = 0;
        quint64 mappingRevision = 0;
        qint64 positionMs = 0;
        qint64 durationMs = 0;
        bool completedLocally = false;
        QString lastAction;
        int lastProgressHundredths = -1;
        QString lastOperationId;
        TrackerProviderId providerId = TrackerProviderId::Simkl;
    };

    void setError(const QString &error);
    bool globalSyncEnabled() const;
    void observePlaybackLifecycleForProvider(const QVariantMap &event, TrackerProviderId providerId);
    void dispatchNextPending(TrackerProviderId providerId, const QString &remoteAccountId);
    void dispatchIntent(const TrackerScrobbleIntent &intent);
    bool isCurrentMapping(const TrackerScrobbleIntent &intent) const;
    bool supersedeUnsentBeforeCurrentEvent(TrackerProviderId providerId, const QString &remoteAccountId,
                                           const QString &currentSessionId);
    bool retryProtectiveClosesForLaterSessionEvent(TrackerProviderId providerId, const QString &remoteAccountId,
                                                   const QString &currentSessionId);
    void completeSend(const QString &operationId, SimklScrobbleSendResult result);
    void completeReadback(const QString &operationId, SimklScrobbleReadbackResult result);
    bool closeActivePlayback(const QString &source, const ActivePlayback &active);
    std::optional<TrackerTitleMapping> exactMapping(
        TrackerProviderId providerId,
        const QString &remoteAccountId,
        const QString &kind,
        const QString &itemKey) const;
    static int progressHundredths(qint64 positionMs, qint64 durationMs);

    ProfilePaths m_profile;
    TrackerConnectionStore *m_connections = nullptr;
    TrackerMappingStore *m_mappings = nullptr;
    QHash<QString, SimklScrobbleTransport *> m_transports;
    TrackerSyncSettingsStore *m_syncSettings = nullptr;
    TrackerScrobbleStore m_store;
    QHash<QString, ActivePlayback> m_activePlaybacks;
    QHash<QString, quint64> m_playbackGenerationFloorBySource;
    QSet<QString> m_inFlightAccounts;
    quint64 m_playbackScopeGeneration = 0;
    QString m_error;
    bool m_started = false;
    bool m_lastRelaySuppression = false;
};
