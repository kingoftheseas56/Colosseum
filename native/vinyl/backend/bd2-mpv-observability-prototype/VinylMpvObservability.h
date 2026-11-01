#pragma once

#include "../bd1-signal-path-prototype/VinylSignalPathTypes.h"

#include <QString>

#include <optional>
#include <utility>

namespace vinyl {

struct MpvRuntimeObservations final {
    std::optional<QString> fileFormat;
    std::optional<QString> selectedTrackCodec;
    std::optional<int> demuxSampleRateHz;
    std::optional<int> demuxChannelCount;
    std::optional<int> demuxBitrateBps;

    std::optional<QString> decodedSampleFormat;
    std::optional<int> decodedSampleRateHz;
    std::optional<int> decodedChannelCount;

    std::optional<QString> outputSampleFormat;
    std::optional<int> outputSampleRateHz;
    std::optional<int> outputChannelCount;

    std::optional<QString> currentAudioBackend;
    std::optional<QString> configuredAudioDevice;
    std::optional<bool> audioExclusiveOption;
};

struct DeviceRuntimeObservations final {
    std::optional<QString> actualDeviceId;
    std::optional<QString> outputSampleFormat;
    std::optional<int> outputSampleRateHz;
    std::optional<int> outputChannelCount;
    std::optional<int> outputBitDepth;
    std::optional<bool> exclusiveConfirmed;
};

struct MpvObservationMapping final {
    explicit MpvObservationMapping(SourceAudioFacts source)
        : signalPath(std::move(source))
    {
    }

    SignalPathSnapshot signalPath;

    EvidenceFact<int> demuxSampleRateHint = EvidenceFact<int>::unknown();
    EvidenceFact<int> demuxChannelCountHint = EvidenceFact<int>::unknown();

    EvidenceFact<QString> decodedSampleFormat = EvidenceFact<QString>::unknown();
    EvidenceFact<int> decodedSampleRateHz = EvidenceFact<int>::unknown();
    EvidenceFact<int> decodedChannels = EvidenceFact<int>::unknown();

    EvidenceFact<QString> mpvOutputSampleFormat = EvidenceFact<QString>::unknown();
    EvidenceFact<int> mpvOutputSampleRateHz = EvidenceFact<int>::unknown();
    EvidenceFact<int> mpvOutputChannels = EvidenceFact<int>::unknown();

    EvidenceFact<QString> configuredDeviceSelector = EvidenceFact<QString>::unknown();
    EvidenceFact<bool> mpvExclusiveOption = EvidenceFact<bool>::unknown();
};

MpvObservationMapping mapMpvObservations(
    SourceAudioFacts source,
    RequestedProcessing requested,
    const MpvRuntimeObservations &mpv,
    const DeviceRuntimeObservations &device = DeviceRuntimeObservations{});

} // namespace vinyl
