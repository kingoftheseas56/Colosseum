// Hermetic tests for Task 12's device-recovery coordinator and colour/HDR policy. Both are pure of
// any GPU/audio API, so a scripted fake target exercises every recovery path deterministically and
// the colour decision table is verified without a decoder.

#include "player2/diagnostics/PlaybackDiagnostics.h"
#include "player2/platform/windows/DeviceRecovery.h"
#include "player2/video/ColorHdrPolicy.h"

#include <QtCore/QJsonObject>

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Colosseum::Player2;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

RecoveryPolicy fastPolicy(int maxAttempts)
{
    RecoveryPolicy policy;
    policy.maxAttempts = maxAttempts;
    policy.backoffMs = [](int) { return 0; }; // no real delay in tests
    return policy;
}

class FakeTarget final : public IRecoverableTarget
{
public:
    int rebuildFailuresBeforeSuccess = 0; // fail this many rebuilds, then succeed
    bool rebuildAlwaysFails = false;
    bool reopenAlwaysFails = false;
    std::atomic_bool *cancelAfterRebuild = nullptr; // flip the cancel flag once a rebuild succeeds

    int rebuildCalls = 0;
    int reopenCalls = 0;

    bool rebuildDevice(DeviceLostReason, QString *error) override
    {
        ++rebuildCalls;
        if (rebuildAlwaysFails || rebuildCalls <= rebuildFailuresBeforeSuccess) {
            if (error)
                *error = QStringLiteral("rebuild failed");
            return false;
        }
        if (cancelAfterRebuild)
            cancelAfterRebuild->store(true, std::memory_order_release);
        return true;
    }

    bool reopenAtSavedPosition(QString *error) override
    {
        ++reopenCalls;
        if (reopenAlwaysFails) {
            if (error)
                *error = QStringLiteral("reopen failed");
            return false;
        }
        return true;
    }
};

// ---- recovery coordinator -----------------------------------------------------------------------

void testRecoversOnFirstAttempt()
{
    DeviceRecoveryCoordinator coordinator(fastPolicy(3));
    FakeTarget target;
    std::atomic_bool cancelled{false};
    const RecoveryOutcome outcome =
        coordinator.recover(DeviceLostReason::VideoDeviceRemoved, target, cancelled);
    require(outcome.recovered, "a healthy rebuild must recover");
    require(outcome.attempts == 1, "first-attempt recovery uses one attempt");
    require(target.rebuildCalls == 1 && target.reopenCalls == 1, "one rebuild + one reopen");
    require(outcome.terminalCode == Player2ErrorCode::None, "recovery has no terminal error");
}

void testRecoversAfterTransientFailures()
{
    DeviceRecoveryCoordinator coordinator(fastPolicy(5));
    FakeTarget target;
    target.rebuildFailuresBeforeSuccess = 2; // first two rebuilds fail, third succeeds
    std::atomic_bool cancelled{false};
    const RecoveryOutcome outcome =
        coordinator.recover(DeviceLostReason::VideoDeviceRemoved, target, cancelled);
    require(outcome.recovered, "recovery should succeed once the device settles");
    require(outcome.attempts == 3, "it should take exactly three attempts");
    require(target.reopenCalls == 1, "reopen only after the successful rebuild");
}

void testExhaustionReturnsTypedDeviceLost()
{
    DeviceRecoveryCoordinator coordinator(fastPolicy(3));
    FakeTarget target;
    target.rebuildAlwaysFails = true;
    std::atomic_bool cancelled{false};
    const RecoveryOutcome outcome =
        coordinator.recover(DeviceLostReason::VideoDeviceRemoved, target, cancelled);
    require(!outcome.recovered, "an unrecoverable device must not report recovery");
    require(outcome.terminalCode == Player2ErrorCode::DeviceLost, "video loss maps to DeviceLost");
    require(outcome.attempts == 3, "recovery is bounded to the policy attempts");
    require(!outcome.message.isEmpty(), "a terminal outcome carries a message");
}

void testReopenFailureExhausts()
{
    DeviceRecoveryCoordinator coordinator(fastPolicy(2));
    FakeTarget target;
    target.reopenAlwaysFails = true; // device rebuilds but media never resumes
    std::atomic_bool cancelled{false};
    const RecoveryOutcome outcome =
        coordinator.recover(DeviceLostReason::VideoDeviceRemoved, target, cancelled);
    require(!outcome.recovered, "a rebuild without resume is not a recovery");
    require(outcome.terminalCode == Player2ErrorCode::DeviceLost, "still a device-lost terminal");
    require(target.reopenCalls == 2, "reopen attempted once per rebuild");
}

void testAudioReasonMapsToAudioDeviceLost()
{
    DeviceRecoveryCoordinator coordinator(fastPolicy(1));
    FakeTarget target;
    target.rebuildAlwaysFails = true;
    std::atomic_bool cancelled{false};
    const RecoveryOutcome outcome =
        coordinator.recover(DeviceLostReason::AudioEndpointLost, target, cancelled);
    require(outcome.terminalCode == Player2ErrorCode::AudioDeviceLost,
            "audio loss maps to AudioDeviceLost");
    require(DeviceRecoveryCoordinator::terminalCodeFor(DeviceLostReason::AudioEndpointLost) ==
                Player2ErrorCode::AudioDeviceLost,
            "terminalCodeFor is consistent for audio");
}

void testCancelBeforeRecoveryReturnsPromptly()
{
    DeviceRecoveryCoordinator coordinator(fastPolicy(3));
    FakeTarget target;
    std::atomic_bool cancelled{true}; // already shutting down
    const RecoveryOutcome outcome =
        coordinator.recover(DeviceLostReason::VideoDeviceRemoved, target, cancelled);
    require(!outcome.recovered, "a cancelled recovery does not resume");
    require(outcome.terminalCode == Player2ErrorCode::Cancelled, "shutdown yields Cancelled");
    require(target.rebuildCalls == 0, "no rebuild is attempted once cancelled");
}

void testCancelDuringRecoveryDoesNotResume()
{
    DeviceRecoveryCoordinator coordinator(fastPolicy(3));
    FakeTarget target;
    std::atomic_bool cancelled{false};
    target.cancelAfterRebuild = &cancelled; // a shutdown races in right after the rebuild succeeds
    const RecoveryOutcome outcome =
        coordinator.recover(DeviceLostReason::VideoDeviceRemoved, target, cancelled);
    require(!outcome.recovered, "a shutdown mid-recovery must not resume playback");
    require(outcome.terminalCode == Player2ErrorCode::Cancelled, "it yields Cancelled");
    require(target.reopenCalls == 0, "reopen must not run after a mid-recovery cancel");
}

// ---- colour / HDR policy ------------------------------------------------------------------------

void testColorPolicyDecisionTable()
{
    // FFmpeg AVCOL_* values used below: SPC BT709=1, UNSPECIFIED=2, SMPTE170M=6, BT2020_NCL=9;
    // RANGE MPEG(studio)=1, JPEG(full)=2; TRC PQ=16, HLG=18; PRI BT2020=9.
    const ColorConversion sd = resolveColorConversion(6, 1, 480, 0, 0, 8);
    require(sd.matrix == ColorMatrix::Bt601, "SD content is BT.601");
    require(sd.range == ColorRange::Studio, "SD content is studio range");
    require(!sd.hdrSource, "SD content is not HDR");
    require(sd.inputYCbCrMatrix() == 0, "BT.601 legacy matrix value is 0");

    const ColorConversion untaggedHd = resolveColorConversion(2, 1, 1080, 0, 0, 8);
    require(untaggedHd.matrix == ColorMatrix::Bt709, "untagged HD falls back to BT.709");
    require(untaggedHd.untaggedHdFallback, "the fallback is recorded");
    require(untaggedHd.inputYCbCrMatrix() == 1, "BT.709 legacy matrix value is 1");

    const ColorConversion taggedHd = resolveColorConversion(1, 1, 720, 0, 0, 8);
    require(taggedHd.matrix == ColorMatrix::Bt709, "tagged BT.709 stays BT.709");
    require(!taggedHd.untaggedHdFallback, "tagged content is not a fallback");

    const ColorConversion full = resolveColorConversion(1, 2, 1080, 0, 0, 8);
    require(full.range == ColorRange::Full, "JPEG range is full");
    require(full.inputNominalRange() == 2, "full range legacy value is 2");

    const ColorConversion pq = resolveColorConversion(9, 1, 2160, 16, 9, 10);
    require(pq.hdrSource, "PQ transfer marks an HDR source");
    require(pq.handling == HdrHandling::TonemapToSdr, "HDR is tone-mapped to SDR, never passthrough");
    require(pq.matrix == ColorMatrix::Bt2020, "BT.2020 colourspace is detected");
    require(pq.inputYCbCrMatrix() == 1, "BT.2020 is approximated as BT.709 in the legacy struct");

    const ColorConversion hlg = resolveColorConversion(1, 1, 1080, 18, 1, 10);
    require(hlg.hdrSource, "HLG transfer marks an HDR source");
    require(hlg.handling == HdrHandling::TonemapToSdr, "HLG is tone-mapped to SDR");
}

void testDiagnosticsSchemaIsStableAndComplete()
{
    // The emitted JSON must contain exactly the declared schema keys — no drift, no extras, no
    // missing fields. This is what the diagnostics contract relies on.
    PlaybackDiagnostics diagnostics;
    diagnostics.state = QStringLiteral("Playing");
    diagnostics.videoCodec = QStringLiteral("h264");
    diagnostics.deviceLostReason = QStringLiteral("none");
    const QJsonObject json = diagnostics.toJson();
    const QStringList keys = PlaybackDiagnostics::schemaKeys();

    require(json.size() == keys.size(), "toJson must emit exactly the schema key count");
    for (const QString &key : keys)
        require(json.contains(key), "schema key missing from toJson: " + key.toStdString());
    for (auto it = json.constBegin(); it != json.constEnd(); ++it)
        require(keys.contains(it.key()), "toJson emitted an unschemaed key: " + it.key().toStdString());
    require(json.value(QStringLiteral("videoCodec")).toString() == QStringLiteral("h264"),
            "typed fields round-trip through the schema");
}

} // namespace

int main()
{
    try {
        testDiagnosticsSchemaIsStableAndComplete();
        testRecoversOnFirstAttempt();
        testRecoversAfterTransientFailures();
        testExhaustionReturnsTypedDeviceLost();
        testReopenFailureExhausts();
        testAudioReasonMapsToAudioDeviceLost();
        testCancelBeforeRecoveryReturnsPromptly();
        testCancelDuringRecoveryDoesNotResume();
        testColorPolicyDecisionTable();
    } catch (const std::exception &error) {
        std::cerr << "player2_device_recovery_test: FAIL " << error.what() << '\n';
        return 1;
    }
    std::cout << "player2_device_recovery_test: PASS\n";
    return 0;
}
