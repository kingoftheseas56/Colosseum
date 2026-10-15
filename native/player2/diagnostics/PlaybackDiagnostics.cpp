#include "PlaybackDiagnostics.h"

namespace Colosseum::Player2 {

QStringList PlaybackDiagnostics::schemaKeys()
{
    // The fixed, ordered schema. Adding a field means adding it here AND in toJson(); the contract
    // test asserts the two stay in lockstep so the schema never drifts silently.
    return {
        QStringLiteral("state"),
        QStringLiteral("position"),
        QStringLiteral("duration"),
        QStringLiteral("videoCodec"),
        QStringLiteral("hardwareFormat"),
        QStringLiteral("inputFormat"),
        QStringLiteral("colorConversion"),
        QStringLiteral("adapter"),
        QStringLiteral("adapterMatch"),
        QStringLiteral("decoded"),
        QStringLiteral("submitted"),
        QStringLiteral("presented"),
        QStringLiteral("dropped"),
        QStringLiteral("scheduledLateDrops"),
        QStringLiteral("cpuTransfers"),
        QStringLiteral("deviceErrors"),
        QStringLiteral("avErrorMs"),
        QStringLiteral("avP95Ms"),
        QStringLiteral("audioDevice"),
        QStringLiteral("audioFormat"),
        QStringLiteral("audioQueueMs"),
        QStringLiteral("audioUnderruns"),
        QStringLiteral("normalizationLatencyMs"),
        QStringLiteral("videoDeviceLost"),
        QStringLiteral("audioDeviceLost"),
        QStringLiteral("deviceLostReason"),
        QStringLiteral("recoveryAttempts"),
    };
}

QJsonObject PlaybackDiagnostics::toJson() const
{
    return QJsonObject{
        {QStringLiteral("state"), state},
        {QStringLiteral("position"), position},
        {QStringLiteral("duration"), duration},
        {QStringLiteral("videoCodec"), videoCodec},
        {QStringLiteral("hardwareFormat"), hardwareFormat},
        {QStringLiteral("inputFormat"), inputFormat},
        {QStringLiteral("colorConversion"), colorConversion},
        {QStringLiteral("adapter"), adapter},
        {QStringLiteral("adapterMatch"), adapterMatch},
        {QStringLiteral("decoded"), static_cast<qint64>(decoded)},
        {QStringLiteral("submitted"), static_cast<qint64>(submitted)},
        {QStringLiteral("presented"), static_cast<qint64>(presented)},
        {QStringLiteral("dropped"), static_cast<qint64>(dropped)},
        {QStringLiteral("scheduledLateDrops"), static_cast<qint64>(scheduledLateDrops)},
        {QStringLiteral("cpuTransfers"), static_cast<qint64>(cpuTransfers)},
        {QStringLiteral("deviceErrors"), static_cast<qint64>(deviceErrors)},
        {QStringLiteral("avErrorMs"), avErrorMs},
        {QStringLiteral("avP95Ms"), avP95Ms},
        {QStringLiteral("audioDevice"), audioDevice},
        {QStringLiteral("audioFormat"), audioFormat},
        {QStringLiteral("audioQueueMs"), audioQueueMs},
        {QStringLiteral("audioUnderruns"), static_cast<qint64>(audioUnderruns)},
        {QStringLiteral("normalizationLatencyMs"), normalizationLatencyMs},
        {QStringLiteral("videoDeviceLost"), videoDeviceLost},
        {QStringLiteral("audioDeviceLost"), audioDeviceLost},
        {QStringLiteral("deviceLostReason"), deviceLostReason},
        {QStringLiteral("recoveryAttempts"), recoveryAttempts},
    };
}

} // namespace Colosseum::Player2
