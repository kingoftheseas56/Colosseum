#include "DeviceRecovery.h"

#include <QtCore/QStringLiteral>

#include <algorithm>
#include <chrono>
#include <thread>

namespace Colosseum::Player2 {
namespace {

int defaultBackoffMs(int attempt)
{
    // A short, capped backoff so a transient loss (driver reset) settles before the rebuild.
    return std::min(2'000, 100 * (1 << std::min(attempt, 4)));
}

} // namespace

QString deviceLostReasonName(DeviceLostReason reason)
{
    switch (reason) {
    case DeviceLostReason::None: return QStringLiteral("none");
    case DeviceLostReason::VideoDeviceRemoved: return QStringLiteral("video-device-removed");
    case DeviceLostReason::AudioEndpointLost: return QStringLiteral("audio-endpoint-lost");
    case DeviceLostReason::DisplayChanged: return QStringLiteral("display-changed");
    case DeviceLostReason::AdapterChanged: return QStringLiteral("adapter-changed");
    }
    return QStringLiteral("unknown");
}

DeviceRecoveryCoordinator::DeviceRecoveryCoordinator(RecoveryPolicy policy)
    : m_policy(std::move(policy))
{
    if (m_policy.maxAttempts < 1)
        m_policy.maxAttempts = 1;
    if (!m_policy.backoffMs)
        m_policy.backoffMs = &defaultBackoffMs;
}

int DeviceRecoveryCoordinator::backoffFor(int attempt) const
{
    return m_policy.backoffMs ? m_policy.backoffMs(attempt) : 0;
}

Player2ErrorCode DeviceRecoveryCoordinator::terminalCodeFor(DeviceLostReason reason)
{
    return reason == DeviceLostReason::AudioEndpointLost ? Player2ErrorCode::AudioDeviceLost
                                                         : Player2ErrorCode::DeviceLost;
}

RecoveryOutcome DeviceRecoveryCoordinator::recover(DeviceLostReason reason,
                                                   IRecoverableTarget &target,
                                                   const std::atomic_bool &cancelled)
{
    RecoveryOutcome outcome;
    for (int attempt = 1; attempt <= m_policy.maxAttempts; ++attempt) {
        if (cancelled.load(std::memory_order_acquire)) {
            outcome.terminalCode = Player2ErrorCode::Cancelled;
            outcome.message = QStringLiteral("shutdown during recovery");
            return outcome; // clean shutdown mid-recovery: return promptly, never hang
        }

        const int delayMs = backoffFor(attempt);
        for (int waited = 0; waited < delayMs; waited += 20) {
            if (cancelled.load(std::memory_order_acquire)) {
                outcome.terminalCode = Player2ErrorCode::Cancelled;
                outcome.message = QStringLiteral("shutdown during recovery");
                outcome.attempts = attempt - 1;
                return outcome;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(20, delayMs - waited)));
        }

        outcome.attempts = attempt;
        QString error;
        if (!target.rebuildDevice(reason, &error)) {
            outcome.message = error; // rebuild failed; retry until the bound is reached
            continue;
        }
        if (cancelled.load(std::memory_order_acquire)) {
            outcome.terminalCode = Player2ErrorCode::Cancelled;
            outcome.message = QStringLiteral("shutdown during recovery");
            return outcome;
        }
        if (!target.reopenAtSavedPosition(&error)) {
            outcome.message = error; // rebuilt but could not resume; retry
            continue;
        }
        outcome.recovered = true;
        return outcome;
    }

    outcome.terminalCode = terminalCodeFor(reason);
    if (outcome.message.isEmpty())
        outcome.message = QStringLiteral("device recovery exhausted after %1 attempts")
                              .arg(m_policy.maxAttempts);
    return outcome;
}

} // namespace Colosseum::Player2
