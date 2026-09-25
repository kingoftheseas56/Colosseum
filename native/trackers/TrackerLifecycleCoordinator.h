#pragma once

#include "TrackerTypes.h"

#include "account/ProfilePaths.h"

#include <QString>

#include <functional>

class TrackerCredentialVault;
class TrackerConnectionStore;
class TrackerDeliveryStore;
class TrackerMappingStore;
class TrackerScrobbleStore;
class TrackerLifecycleTest;

enum class TrackerDisconnectChoice : quint8 {
    Cancel,
    KeepPaused,
    DiscardKnownUnsent
};

struct TrackerReconnectActions {
    std::function<bool(const TrackerConnection &, QString *)> pull;
    std::function<bool(const TrackerConnection &, QString *)> reconcileUnknown;
    std::function<bool(const TrackerConnection &, QString *)> resumeSafePending;
};

// Keeps tracker lifecycle ordering beside its existing private-state owners.
// This coordinator never calls a provider transport or mutates canonical
// History, Activity, Progress, statistics, or Arc 49 ratings/reviews.
class TrackerLifecycleCoordinator final
{
public:
    static bool hasPrivateState(const ProfilePaths &profile,
                                bool *present,
                                QString *error = nullptr);

    // Same-device LocalOnly -> Account adoption is source-preserving and
    // idempotent. It never copies secrets into tracker files or mutates
    // Colosseum-owned History, Activity, Progress, or Arc 49 state.
    static bool adoptPrivateState(const ProfilePaths &source,
                                  const ProfilePaths &destination,
                                  TrackerCredentialVault &vault,
                                  qint64 nowMs,
                                  QString *error = nullptr);

    static bool reconnect(const ProfilePaths &profile,
                          TrackerProviderId providerId,
                          const QString &verifiedRemoteAccountId,
                          const TrackerReconnectActions &actions,
                          QString *error = nullptr);

    static bool moveConnection(const ProfilePaths &source,
                               const ProfilePaths &destination,
                               TrackerProviderId providerId,
                               TrackerCredentialVault &vault,
                               qint64 nowMs,
                               QString *error = nullptr);

    // Cancel returns false without changing state. KeepPaused retains both
    // known-unsent and unknown operations under their old account/generation.
    static bool disconnect(const ProfilePaths &profile,
                           TrackerProviderId providerId,
                           TrackerDisconnectChoice choice,
                           TrackerCredentialVault &vault,
                           QString *error = nullptr);

    // Live-owner overload for the profile runtime. It avoids constructing a
    // second set of cached stores while changing the connection and queues.
    static bool disconnect(const ProfilePaths &profile,
                           TrackerProviderId providerId,
                           TrackerDisconnectChoice choice,
                           TrackerCredentialVault &vault,
                           TrackerConnectionStore &connections,
                           TrackerMappingStore &mappings,
                           TrackerDeliveryStore &delivery,
                           TrackerScrobbleStore &scrobble,
                           QString *error = nullptr);

    static int removeImportedHistory(const ProfilePaths &profile,
                                    TrackerProviderId providerId,
                                    const QString &remoteAccountId,
                                    QString *error = nullptr);

private:
    friend class TrackerLifecycleTest;

    // Reserved for an explicit, confirmed permanent profile deletion, after
    // confirmation and immediately before the profile lifecycle removes its
    // managed tree. Never call from sign-out, deactivation, profile switching,
    // or adoption cleanup. No current production profile-deletion flow exists.
    // This removes only tracker-private files and local credentials; it never
    // mutates provider state or Colosseum-owned History/Activity/Progress or
    // Arc 49 state.
    static bool removeProfilePrivateStateForPermanentDeletion(
        const ProfilePaths &profile,
        TrackerCredentialVault &vault,
        QString *error = nullptr);
};
