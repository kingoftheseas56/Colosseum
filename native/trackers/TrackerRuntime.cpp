#include "TrackerRuntime.h"

#include <limits>

namespace {

int providerKey(TrackerProviderId providerId)
{
    return static_cast<int>(providerId);
}

bool sameAttempt(const TrackerConnectionAttempt &left,
                 const TrackerConnectionAttempt &right)
{
    return left.profileId == right.profileId
        && left.profileGeneration == right.profileGeneration
        && left.providerId == right.providerId
        && left.connectionGeneration == right.connectionGeneration;
}

} // namespace

TrackerProviderRegistry::TrackerProviderRegistry(
    const QList<TrackerIdentityProvider *> &providers)
{
    for (TrackerIdentityProvider *provider : providers) {
        if (!provider)
            continue;
        const TrackerProviderDescriptor descriptor = provider->descriptor();
        if (descriptor.providerId == TrackerProviderId::Simkl
            || descriptor.providerId == TrackerProviderId::Mal
            || descriptor.providerId == TrackerProviderId::Trakt
            || descriptor.providerId == TrackerProviderId::AniList) {
            m_providers.insert(providerKey(descriptor.providerId), provider);
        }
    }
}

TrackerProviderRegistry TrackerProviderRegistry::production()
{
    // No provider has a verified desktop authentication adapter in Slice 1.
    // Test doubles can only enter through an explicit registry construction.
    return TrackerProviderRegistry();
}

TrackerIdentityProvider *TrackerProviderRegistry::provider(
    TrackerProviderId providerId) const
{
    return m_providers.value(providerKey(providerId), nullptr);
}

TrackerConnectionRuntime::TrackerConnectionRuntime(
    TrackerProviderRegistry providers,
    TrackerCredentialVault *vault,
    TrackerClock *clock)
    : m_providers(std::move(providers)),
      m_vault(vault),
      m_clock(clock),
      m_callbackLifetime(std::make_shared<CallbackLifetime>())
{
    m_callbackLifetime->runtime = this;
    m_callbackLifetime->ownerThread = QThread::currentThread();
}

TrackerConnectionRuntime::~TrackerConnectionRuntime()
{
    const std::lock_guard<std::mutex> lock(m_callbackLifetime->mutex);
    m_callbackLifetime->runtime = nullptr;
}

bool TrackerConnectionRuntime::activateProfile(const ProfilePaths &profile)
{
    ++m_profileGeneration;
    m_profile = profile;
    m_pendingAttempts.clear();
    m_nextConnectionGenerations.clear();
    m_lastError.clear();
    m_store = std::make_unique<TrackerConnectionStore>(profile);
    if (!m_store->healthy(&m_lastError)) {
        m_store.reset();
        return false;
    }

    for (const TrackerConnection &connection : m_store->connections()) {
        if (connection.connectionGeneration == std::numeric_limits<quint64>::max()) {
            setError(QStringLiteral("Tracker connection generation is exhausted for this provider."));
            m_store.reset();
            return false;
        }
        m_nextConnectionGenerations.insert(
            providerKey(connection.providerId), connection.connectionGeneration + 1);
    }
    return true;
}

void TrackerConnectionRuntime::deactivateProfile()
{
    ++m_profileGeneration;
    m_pendingAttempts.clear();
    m_nextConnectionGenerations.clear();
    m_store.reset();
    m_profile = ProfilePaths::sealed();
    m_lastError.clear();
}

bool TrackerConnectionRuntime::beginConnection(TrackerProviderId providerId)
{
    if (!m_store || !m_vault || !m_clock) {
        setError(QStringLiteral("Tracker connections are unavailable for this profile."));
        return false;
    }
    const auto existingConnection = m_store->connection(providerId);
    if (existingConnection
        && existingConnection->state == TrackerConnectionState::TransferPending) {
        setError(QStringLiteral("A tracker connection move needs recovery before sign-in."));
        return false;
    }

    TrackerIdentityProvider *provider = m_providers.provider(providerId);
    if (!provider || !provider->descriptor().available) {
        setError(QStringLiteral("%1 is not available in this build.")
                     .arg(trackerProviderDisplayName(providerId)));
        return false;
    }
    if (provider->descriptor().providerId != providerId) {
        setError(QStringLiteral("Tracker provider identity is inconsistent."));
        return false;
    }

    const int key = providerKey(providerId);
    const quint64 generation = m_nextConnectionGenerations.value(
        key, m_store->nextConnectionGeneration(providerId));
    if (generation == 0 || generation == std::numeric_limits<quint64>::max()) {
        setError(QStringLiteral("Tracker connection generation is exhausted for this provider."));
        return false;
    }
    m_nextConnectionGenerations.insert(key, generation + 1);
    const TrackerConnectionAttempt attempt{
        m_profile.profileId(), m_profileGeneration, providerId, generation};
    m_pendingAttempts.insert(key, attempt);
    const std::weak_ptr<CallbackLifetime> lifetime = m_callbackLifetime;
    provider->requestStableAccountId(
        attempt,
        [lifetime, attempt](const TrackerStableAccountResult &result) {
            const std::shared_ptr<CallbackLifetime> guarded = lifetime.lock();
            if (!guarded)
                return;
            const std::lock_guard<std::mutex> lock(guarded->mutex);
            if (!guarded->runtime
                || guarded->ownerThread != QThread::currentThread()) {
                return;
            }
            guarded->runtime->completeConnection(attempt, result);
        });
    return true;
}

QString TrackerConnectionRuntime::lastError() const
{
    return m_lastError;
}

void TrackerConnectionRuntime::completeConnection(
    const TrackerConnectionAttempt &attempt,
    const TrackerStableAccountResult &result)
{
    if (!isCurrentAttempt(attempt))
        return;

    const int key = providerKey(attempt.providerId);
    m_pendingAttempts.remove(key);
    const QString remoteAccountId = result.remoteAccountId.trimmed();
    const QString providerName = trackerProviderDisplayName(attempt.providerId);
    if (remoteAccountId.isEmpty()) {
        setError(QStringLiteral("%1 did not confirm a stable account identity.").arg(providerName));
        return;
    }

    const TrackerCredentialSlot slot{
        attempt.profileId, attempt.providerId, remoteAccountId};
    if (!m_vault->hasReusableCredential(slot, m_clock->nowMs())) {
        setError(QStringLiteral("%1 credentials could not be verified for this profile.")
                     .arg(providerName));
        return;
    }

    QString ownershipError;
    const auto claim = m_store->externalAccountClaim(
        attempt.providerId, remoteAccountId, nullptr, &ownershipError);
    if (claim == TrackerConnectionStore::ExternalAccountClaim::Indeterminate) {
        setError(ownershipError);
        return;
    }
    if (claim == TrackerConnectionStore::ExternalAccountClaim::Claimed) {
        setError(QStringLiteral("This %1 account is already connected to another profile.")
                     .arg(providerName));
        return;
    }

    TrackerIdentityProvider *provider = m_providers.provider(attempt.providerId);
    if (!provider || provider->descriptor().providerId != attempt.providerId) {
        setError(QStringLiteral("Tracker provider identity is inconsistent."));
        return;
    }
    if (!trackerProviderCapabilitiesAreKnown(provider->descriptor().capabilities)) {
        setError(QStringLiteral("%1 advertised unsupported capabilities.").arg(providerName));
        return;
    }
    QString persistError;
    if (!m_store->upsert({
            attempt.providerId,
            remoteAccountId,
            attempt.connectionGeneration,
            m_clock->nowMs(),
            provider->descriptor().capabilities,
            TrackerConnectionState::Connected},
        &persistError)) {
        setError(persistError);
        return;
    }
    m_lastError.clear();
}

void TrackerConnectionRuntime::setError(const QString &error)
{
    m_lastError = error;
}

bool TrackerConnectionRuntime::isCurrentAttempt(
    const TrackerConnectionAttempt &attempt) const
{
    if (!m_store || attempt.profileGeneration != m_profileGeneration
        || attempt.profileId != m_profile.profileId()) {
        return false;
    }
    const auto pending = m_pendingAttempts.constFind(providerKey(attempt.providerId));
    return pending != m_pendingAttempts.cend() && sameAttempt(pending.value(), attempt);
}
