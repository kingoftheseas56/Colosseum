#pragma once

#include "player2/core/Player2Types.h"

#include <QtCore/QString>

#include <atomic>
#include <functional>

namespace Colosseum::Player2 {

// Why a device was lost. Typed so recovery and diagnostics never guess from a string.
enum class DeviceLostReason
{
    None,
    VideoDeviceRemoved, // DXGI_ERROR_DEVICE_REMOVED / DEVICE_RESET (GetDeviceRemovedReason)
    AudioEndpointLost,  // AUDCLNT_E_DEVICE_INVALIDATED
    DisplayChanged,     // display topology change
    AdapterChanged,     // the presenting adapter changed under us
};

QString deviceLostReasonName(DeviceLostReason reason);

// The recoverable target the coordinator drives. The real implementation (owned by Player2Session)
// tears down and rebuilds the D3D11 / WASAPI device and reopens the current media at the saved
// position; the test fake scripts outcomes. The coordinator NEVER touches a device directly, and a
// pipeline NEVER reopens media itself — that policy lives only here.
class IRecoverableTarget
{
public:
    virtual ~IRecoverableTarget() = default;
    // Rebuild the lost device / endpoint. Returns true on success. Must not reopen media.
    virtual bool rebuildDevice(DeviceLostReason reason, QString *error) = 0;
    // Reopen the current media at the saved position after a successful rebuild. Returns true when
    // playback has been re-armed.
    virtual bool reopenAtSavedPosition(QString *error) = 0;
};

struct RecoveryOutcome
{
    bool recovered = false;                                  // playback resumed
    Player2ErrorCode terminalCode = Player2ErrorCode::None;  // set only when recovered == false
    QString message;
    int attempts = 0;                                        // rebuild attempts actually made
};

struct RecoveryPolicy
{
    int maxAttempts = 3;
    std::function<int(int)> backoffMs; // delay before attempt N; nullptr => a small fixed backoff
};

// Bounded, deterministic device-recovery policy. It is pure of any GPU/audio API so it can be
// tested with a fake target: every call either resumes playback or returns a typed terminal error,
// and a cancel observed between steps returns promptly — it must never hang.
class DeviceRecoveryCoordinator
{
public:
    explicit DeviceRecoveryCoordinator(RecoveryPolicy policy = {});

    RecoveryOutcome recover(DeviceLostReason reason, IRecoverableTarget &target,
                            const std::atomic_bool &cancelled);

    static Player2ErrorCode terminalCodeFor(DeviceLostReason reason);

private:
    int backoffFor(int attempt) const;
    RecoveryPolicy m_policy;
};

} // namespace Colosseum::Player2
