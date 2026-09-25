#pragma once

#include <QFlags>
#include <QList>
#include <QString>

#include <optional>

// Arc 35 tracker-plane vocabulary. It deliberately contains no ratings, review,
// Progress, History, Activity, account-sync, or provider-payload types.
enum class TrackerProviderId : quint8 {
    Simkl,
    Mal,
    Trakt,
    AniList
};

enum class TrackerProviderCapability : quint32 {
    None = 0,
    ReadHistory = 1U << 0,
    ReadProgress = 1U << 1,
    WriteProgress = 1U << 2,
    WriteCompletion = 1U << 3,
    Scrobble = 1U << 4
};
Q_DECLARE_FLAGS(TrackerProviderCapabilities, TrackerProviderCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(TrackerProviderCapabilities)

struct TrackerProviderDescriptor {
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString displayName;
    TrackerProviderCapabilities capabilities;
    bool available = false;
};

enum class TrackerConnectionState : quint8 {
    Connected,
    Disconnected,
    TransferPending
};

struct TrackerConnection {
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
    quint64 connectionGeneration = 0;
    qint64 verifiedAtMs = 0;
    TrackerProviderCapabilities capabilities;
    TrackerConnectionState state = TrackerConnectionState::Connected;
};

QString trackerProviderKey(TrackerProviderId providerId);
std::optional<TrackerProviderId> trackerProviderIdFromKey(const QString &key);
QString trackerProviderDisplayName(TrackerProviderId providerId);
bool trackerProviderCapabilitiesAreKnown(TrackerProviderCapabilities capabilities);
QList<TrackerProviderDescriptor> trackerBuiltInProviderCatalog();
