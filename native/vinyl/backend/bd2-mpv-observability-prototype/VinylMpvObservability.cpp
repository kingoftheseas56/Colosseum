#include "VinylMpvObservability.h"

#include <QString>

namespace vinyl {
namespace {

FactProvenance provenance(ProvenanceKind kind,
                          EvidenceStrength strength,
                          const QString &source)
{
    return FactProvenance{kind, source, strength, std::nullopt};
}

bool usableString(const std::optional<QString> &value)
{
    return value.has_value() && !value->trimmed().isEmpty();
}

bool usablePositive(const std::optional<int> &value)
{
    return value.has_value() && *value > 0;
}

template <typename T>
bool mayReplaceWithObserved(const EvidenceFact<T> &current)
{
    if (!current.isKnown())
        return true;

    const auto strength = current.provenance().strength;
    return strength != EvidenceStrength::Observed
        && strength != EvidenceStrength::Confirmed;
}

template <typename T>
bool mayFillWithReported(const EvidenceFact<T> &current)
{
    return !current.isKnown();
}

void appendRateTransformation(MpvObservationMapping &mapping,
                              int input,
                              int output,
                              ProvenanceKind inputKind,
                              const QString &inputSource,
                              ProvenanceKind outputKind,
                              const QString &outputSource)
{
    if (input == output)
        return;

    TransformationStage stage;
    stage.kind = TransformationKind::Resampling;
    stage.provenance = provenance(outputKind, EvidenceStrength::Observed, outputSource);
    stage.detail = QStringLiteral("%1 Hz -> %2 Hz").arg(input).arg(output);
    stage.inputSampleRateHz = EvidenceFact<int>::known(
        input,
        provenance(inputKind, EvidenceStrength::Observed, inputSource));
    stage.outputSampleRateHz = EvidenceFact<int>::known(
        output,
        provenance(outputKind, EvidenceStrength::Observed, outputSource));
    mapping.signalPath.transformations.push_back(std::move(stage));
}

void appendFormatTransformation(MpvObservationMapping &mapping,
                                const QString &input,
                                const QString &output,
                                ProvenanceKind kind,
                                const QString &source)
{
    if (input == output)
        return;

    TransformationStage stage;
    stage.kind = TransformationKind::SampleFormatConversion;
    stage.provenance = provenance(kind, EvidenceStrength::Observed, source);
    stage.detail = QStringLiteral("%1 -> %2").arg(input, output);
    mapping.signalPath.transformations.push_back(std::move(stage));
}

void appendChannelTransformation(MpvObservationMapping &mapping,
                                 int input,
                                 int output,
                                 ProvenanceKind kind,
                                 const QString &source)
{
    if (input == output)
        return;

    TransformationStage stage;
    stage.kind = TransformationKind::ChannelMix;
    stage.provenance = provenance(kind, EvidenceStrength::Observed, source);
    stage.detail = QStringLiteral("%1 channels -> %2 channels").arg(input).arg(output);
    mapping.signalPath.transformations.push_back(std::move(stage));
}

} // namespace

MpvObservationMapping mapMpvObservations(SourceAudioFacts source,
                                         RequestedProcessing requested,
                                         const MpvRuntimeObservations &mpv,
                                         const DeviceRuntimeObservations &device)
{
    MpvObservationMapping mapping(std::move(source));
    mapping.signalPath.requested = std::move(requested);

    if (usableString(mpv.selectedTrackCodec)
        && mayFillWithReported(mapping.signalPath.source.codec)) {
        mapping.signalPath.source.codec = EvidenceFact<QString>::known(
            mpv.selectedTrackCodec->trimmed(),
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Reported,
                       QStringLiteral("mpv:current-tracks/audio/codec")));
    }

    if (usableString(mpv.fileFormat)
        && mayFillWithReported(mapping.signalPath.source.container)) {
        mapping.signalPath.source.container = EvidenceFact<QString>::known(
            mpv.fileFormat->trimmed(),
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Reported,
                       QStringLiteral("mpv:file-format")));
    }

    if (usablePositive(mpv.demuxSampleRateHz)) {
        mapping.demuxSampleRateHint = EvidenceFact<int>::known(
            *mpv.demuxSampleRateHz,
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Reported,
                       QStringLiteral("mpv:current-tracks/audio/demux-samplerate")));
    }

    if (usablePositive(mpv.demuxChannelCount)) {
        mapping.demuxChannelCountHint = EvidenceFact<int>::known(
            *mpv.demuxChannelCount,
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Reported,
                       QStringLiteral("mpv:current-tracks/audio/demux-channel-count")));
    }

    if (usablePositive(mpv.demuxBitrateBps)
        && mayFillWithReported(mapping.signalPath.source.bitrateKbps)) {
        const int kbps = (*mpv.demuxBitrateBps + 500) / 1000;
        mapping.signalPath.source.bitrateKbps = EvidenceFact<int>::known(
            kbps,
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Reported,
                       QStringLiteral("mpv:current-tracks/audio/demux-bitrate")));
    }

    if (usableString(mpv.decodedSampleFormat)) {
        mapping.decodedSampleFormat = EvidenceFact<QString>::known(
            mpv.decodedSampleFormat->trimmed(),
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Observed,
                       QStringLiteral("mpv:audio-params/format")));
    }

    if (usablePositive(mpv.decodedSampleRateHz)) {
        mapping.decodedSampleRateHz = EvidenceFact<int>::known(
            *mpv.decodedSampleRateHz,
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Observed,
                       QStringLiteral("mpv:audio-params/samplerate")));
        if (mayReplaceWithObserved(mapping.signalPath.source.sampleRateHz))
            mapping.signalPath.source.sampleRateHz = mapping.decodedSampleRateHz;
    }

    if (usablePositive(mpv.decodedChannelCount)) {
        mapping.decodedChannels = EvidenceFact<int>::known(
            *mpv.decodedChannelCount,
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Observed,
                       QStringLiteral("mpv:audio-params/channel-count")));
        if (mayReplaceWithObserved(mapping.signalPath.source.channels))
            mapping.signalPath.source.channels = mapping.decodedChannels;
    }

    if (usableString(mpv.outputSampleFormat)) {
        mapping.mpvOutputSampleFormat = EvidenceFact<QString>::known(
            mpv.outputSampleFormat->trimmed(),
            provenance(ProvenanceKind::OutputBackend,
                       EvidenceStrength::Observed,
                       QStringLiteral("mpv:audio-out-params/format")));
    }

    if (usablePositive(mpv.outputSampleRateHz)) {
        mapping.mpvOutputSampleRateHz = EvidenceFact<int>::known(
            *mpv.outputSampleRateHz,
            provenance(ProvenanceKind::OutputBackend,
                       EvidenceStrength::Observed,
                       QStringLiteral("mpv:audio-out-params/samplerate")));
    }

    if (usablePositive(mpv.outputChannelCount)) {
        mapping.mpvOutputChannels = EvidenceFact<int>::known(
            *mpv.outputChannelCount,
            provenance(ProvenanceKind::OutputBackend,
                       EvidenceStrength::Observed,
                       QStringLiteral("mpv:audio-out-params/channel-count")));
    }

    if (usableString(mpv.currentAudioBackend)) {
        mapping.signalPath.selectedOutput.backendId = EvidenceFact<QString>::known(
            mpv.currentAudioBackend->trimmed(),
            provenance(ProvenanceKind::OutputBackend,
                       EvidenceStrength::Observed,
                       QStringLiteral("mpv:current-ao")));
    }

    if (usableString(mpv.configuredAudioDevice)) {
        mapping.configuredDeviceSelector = EvidenceFact<QString>::known(
            mpv.configuredAudioDevice->trimmed(),
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Requested,
                       QStringLiteral("mpv:audio-device")));
    }

    if (mpv.audioExclusiveOption.has_value()) {
        mapping.mpvExclusiveOption = EvidenceFact<bool>::known(
            *mpv.audioExclusiveOption,
            provenance(ProvenanceKind::PlayerRuntime,
                       EvidenceStrength::Requested,
                       QStringLiteral("mpv:options/audio-exclusive")));
    }

    if (usableString(device.actualDeviceId)) {
        mapping.signalPath.selectedOutput.deviceId = EvidenceFact<QString>::known(
            device.actualDeviceId->trimmed(),
            provenance(ProvenanceKind::DeviceRuntime,
                       EvidenceStrength::Observed,
                       QStringLiteral("device-runtime:actual-device")));
    }

    if (device.exclusiveConfirmed.has_value()) {
        mapping.signalPath.selectedOutput.exclusiveConfirmed = EvidenceFact<bool>::known(
            *device.exclusiveConfirmed,
            provenance(ProvenanceKind::DeviceRuntime,
                       EvidenceStrength::Confirmed,
                       QStringLiteral("device-runtime:exclusive-confirmation")));
    }

    if (usableString(device.outputSampleFormat)) {
        mapping.signalPath.observedOutput.sampleFormat = EvidenceFact<QString>::known(
            device.outputSampleFormat->trimmed(),
            provenance(ProvenanceKind::DeviceRuntime,
                       EvidenceStrength::Observed,
                       QStringLiteral("device-runtime:sample-format")));
    } else if (mapping.mpvOutputSampleFormat.isKnown()) {
        mapping.signalPath.observedOutput.sampleFormat = mapping.mpvOutputSampleFormat;
    }

    if (usablePositive(device.outputSampleRateHz)) {
        mapping.signalPath.observedOutput.sampleRateHz = EvidenceFact<int>::known(
            *device.outputSampleRateHz,
            provenance(ProvenanceKind::DeviceRuntime,
                       EvidenceStrength::Observed,
                       QStringLiteral("device-runtime:sample-rate")));
    } else if (mapping.mpvOutputSampleRateHz.isKnown()) {
        mapping.signalPath.observedOutput.sampleRateHz = mapping.mpvOutputSampleRateHz;
    }

    if (usablePositive(device.outputChannelCount)) {
        mapping.signalPath.observedOutput.channels = EvidenceFact<int>::known(
            *device.outputChannelCount,
            provenance(ProvenanceKind::DeviceRuntime,
                       EvidenceStrength::Observed,
                       QStringLiteral("device-runtime:channel-count")));
    } else if (mapping.mpvOutputChannels.isKnown()) {
        mapping.signalPath.observedOutput.channels = mapping.mpvOutputChannels;
    }

    if (usablePositive(device.outputBitDepth)) {
        mapping.signalPath.observedOutput.bitDepth = EvidenceFact<int>::known(
            *device.outputBitDepth,
            provenance(ProvenanceKind::DeviceRuntime,
                       EvidenceStrength::Observed,
                       QStringLiteral("device-runtime:bit-depth")));
    }

    if (mapping.decodedSampleRateHz.isKnown()
        && mapping.mpvOutputSampleRateHz.isKnown()) {
        appendRateTransformation(mapping,
                                 *mapping.decodedSampleRateHz.value(),
                                 *mapping.mpvOutputSampleRateHz.value(),
                                 ProvenanceKind::PlayerRuntime,
                                 QStringLiteral("mpv:audio-params/samplerate"),
                                 ProvenanceKind::OutputBackend,
                                 QStringLiteral("mpv:audio-out-params/samplerate"));
    }

    if (mapping.decodedSampleFormat.isKnown()
        && mapping.mpvOutputSampleFormat.isKnown()) {
        appendFormatTransformation(mapping,
                                   *mapping.decodedSampleFormat.value(),
                                   *mapping.mpvOutputSampleFormat.value(),
                                   ProvenanceKind::OutputBackend,
                                   QStringLiteral("mpv:decoder-to-audio-api"));
    }

    if (mapping.decodedChannels.isKnown() && mapping.mpvOutputChannels.isKnown()) {
        appendChannelTransformation(mapping,
                                    *mapping.decodedChannels.value(),
                                    *mapping.mpvOutputChannels.value(),
                                    ProvenanceKind::OutputBackend,
                                    QStringLiteral("mpv:decoder-to-audio-api"));
    }

    if (mapping.mpvOutputSampleRateHz.isKnown()
        && usablePositive(device.outputSampleRateHz)) {
        appendRateTransformation(mapping,
                                 *mapping.mpvOutputSampleRateHz.value(),
                                 *device.outputSampleRateHz,
                                 ProvenanceKind::OutputBackend,
                                 QStringLiteral("mpv:audio-out-params/samplerate"),
                                 ProvenanceKind::DeviceRuntime,
                                 QStringLiteral("device-runtime:sample-rate"));
    }

    if (mapping.mpvOutputSampleFormat.isKnown()
        && usableString(device.outputSampleFormat)) {
        appendFormatTransformation(mapping,
                                   *mapping.mpvOutputSampleFormat.value(),
                                   device.outputSampleFormat->trimmed(),
                                   ProvenanceKind::DeviceRuntime,
                                   QStringLiteral("device-runtime:audio-api-to-device"));
    }

    if (mapping.mpvOutputChannels.isKnown()
        && usablePositive(device.outputChannelCount)) {
        appendChannelTransformation(mapping,
                                    *mapping.mpvOutputChannels.value(),
                                    *device.outputChannelCount,
                                    ProvenanceKind::DeviceRuntime,
                                    QStringLiteral("device-runtime:audio-api-to-device"));
    }

    return mapping;
}

} // namespace vinyl
