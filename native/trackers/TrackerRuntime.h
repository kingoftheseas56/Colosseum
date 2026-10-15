#pragma once

#include "TrackerConnectionStore.h"
#include "TrackerCredentialVault.h"

#include <QHash>
#include <QThread>

#include <functional>
#include <memory>
#include <mutex>

class TrackerClock
{
public:
    virtual ~TrackerClock() = default;
    virtual qint64 nowMs() const = 0;
};

struct TrackerConnectionAttempt {
    QString profileId;
    quint64 profileGeneration = 0;
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    quint64 connectionGeneration = 0;
};

struct TrackerStableAccountResult {
    QString remoteAccountId;
};

using StableAccountCompletion = std::function<void(const TrackerStableAccountResult &)>;

// Narrow Slice 1 provider seam. It has no OAuth, HTTP, payload, mapping, or
// delivery API; those later capabilities cannot leak into this foundation.
class TrackerIdentityProvider
{
public:
    virtual ~TrackerIdentityProvider() = default;
    virtual TrackerProviderDescriptor descriptor() const = 0;
    virtual void requestStableAccountId(
        const TrackerConnectionAttempt &attempt,
        StableAccountCompletion completion) = 0;
};

class TrackerProviderRegistry final
{
public:
    explicit TrackerProviderRegistry(
        const QList<TrackerIdentityProvider *> &providers = {});

    static TrackerProviderRegistry production();
    TrackerIdentityProvider *provider(TrackerProviderId providerId) const;

private:
    QHash<int, TrackerIdentityProvider *> m_providers;
};

class TrackerConnectionRuntime final
{
public:
    TrackerConnectionRuntime(TrackerProviderRegistry providers,
                             TrackerCredentialVault *vault,
                             TrackerClock *clock);
    ~TrackerConnectionRuntime();

    TrackerConnectionRuntime(const TrackerConnectionRuntime &) = delete;
    TrackerConnectionRuntime &operator=(const TrackerConnectionRuntime &) = delete;
    TrackerConnectionRuntime(TrackerConnectionRuntime &&) = delete;
    TrackerConnectionRuntime &operator=(TrackerConnectionRuntime &&) = delete;

    bool activateProfile(const ProfilePaths &profile);
    void deactivateProfile();
    bool beginConnection(TrackerProviderId providerId);
    QString lastError() const;

private:
    struct CallbackLifetime {
        std::mutex mutex;
        TrackerConnectionRuntime *runtime = nullptr;
        QThread *ownerThread = nullptr;
    };

    void completeConnection(const TrackerConnectionAttempt &attempt,
                            const TrackerStableAccountResult &result);
    void setError(const QString &error);
    bool isCurrentAttempt(const TrackerConnectionAttempt &attempt) const;

    TrackerProviderRegistry m_providers;
    TrackerCredentialVault *m_vault = nullptr;
    TrackerClock *m_clock = nullptr;
    ProfilePaths m_profile = ProfilePaths::sealed();
    std::unique_ptr<TrackerConnectionStore> m_store;
    QHash<int, quint64> m_nextConnectionGenerations;
    QHash<int, TrackerConnectionAttempt> m_pendingAttempts;
    std::shared_ptr<CallbackLifetime> m_callbackLifetime;
    quint64 m_profileGeneration = 0;
    QString m_lastError;
};
