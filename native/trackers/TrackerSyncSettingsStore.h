#pragma once

#include "TrackerTypes.h"

#include "account/ProfilePaths.h"

#include <QList>
#include <QString>

struct TrackerGlobalSyncSettings {
    bool trackerSyncEnabled = true;
    bool checkOnLaunch = true;
    bool backgroundDelivery = true;
    bool completionMessages = false;
};

// Device-private, profile-scoped Tracker Sync Center settings. Provider keys
// are bound to stable remote account IDs inside native storage; the view model
// never exposes those IDs to QML.
class TrackerSyncSettingsStore final
{
public:
    explicit TrackerSyncSettingsStore(const ProfilePaths &profile);

    static QString storagePath(const ProfilePaths &profile);

    bool healthy(QString *error = nullptr) const;
    QString persistenceError() const;
    TrackerGlobalSyncSettings globalSettings() const;
    bool setGlobalSetting(const QString &key, bool enabled,
                          QString *error = nullptr);
    bool pullAutomatically(TrackerProviderId providerId,
                           const QString &remoteAccountId,
                           bool defaultEnabled) const;
    bool setPullAutomatically(TrackerProviderId providerId,
                              const QString &remoteAccountId,
                              bool enabled,
                              QString *error = nullptr);
    qint64 lastSuccessfulSyncAtMs(TrackerProviderId providerId,
                                  const QString &remoteAccountId) const;
    bool recordSuccessfulSync(TrackerProviderId providerId,
                              const QString &remoteAccountId,
                              qint64 syncedAtMs,
                              QString *error = nullptr);

    // LocalOnly -> Account adoption keeps explicit destination settings; when
    // none exist, pull/reconciliation settings follow the connection while
    // outbound master/background controls retain destination defaults. No
    // credentials or provider payloads are included.
    bool adoptPrivateStateFrom(const TrackerSyncSettingsStore &source,
                               QString *error = nullptr);

private:
    struct ProviderSettings {
        TrackerProviderId providerId = TrackerProviderId::Simkl;
        QString remoteAccountId;
        bool pullAutomatically = false;
        bool pullAutomaticallyConfigured = false;
        qint64 lastSuccessfulSyncAtMs = 0;
    };

    bool load();
    bool persist(const TrackerGlobalSyncSettings &global,
                 const QList<ProviderSettings> &providers,
                 QString *error) const;
    QString m_path;
    TrackerGlobalSyncSettings m_global;
    QList<ProviderSettings> m_providers;
    bool m_healthy = true;
    bool m_fileExists = false;
    QString m_error;
};
