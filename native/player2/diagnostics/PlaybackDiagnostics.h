#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace Colosseum::Player2 {

// A typed, stable diagnostics snapshot for Player 2. Every field is a named, typed value — there is
// deliberately NO generic string-keyed property bag (the mpv-property anti-pattern), so tools, the
// harness report and the promotion gates can rely on a fixed JSON schema. `schemaKeys()` is the
// single source of truth for that schema and `toJson()` emits exactly those keys.
struct PlaybackDiagnostics
{
    // Playback
    QString state;
    double position = 0.0;
    double duration = 0.0;

    // Video / codec / colour
    QString videoCodec;
    QString hardwareFormat;
    QString inputFormat;
    QString colorConversion;
    QString adapter;
    bool adapterMatch = false;

    // Frame counters
    quint64 decoded = 0;
    quint64 submitted = 0;
    quint64 presented = 0;
    quint64 dropped = 0;
    quint64 scheduledLateDrops = 0;
    quint64 cpuTransfers = 0;
    quint64 deviceErrors = 0;
    double avErrorMs = 0.0;
    double avP95Ms = 0.0;

    // Audio
    QString audioDevice;
    QString audioFormat;
    double audioQueueMs = 0.0;
    quint64 audioUnderruns = 0;
    double normalizationLatencyMs = 0.0;

    // Device / recovery
    bool videoDeviceLost = false;
    bool audioDeviceLost = false;
    QString deviceLostReason; // typed reason name; "none" when healthy
    int recoveryAttempts = 0;

    QJsonObject toJson() const;
    static QStringList schemaKeys();
};

} // namespace Colosseum::Player2
