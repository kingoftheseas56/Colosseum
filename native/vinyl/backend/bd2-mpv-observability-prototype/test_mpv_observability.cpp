#include "VinylMpvObservability.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdio>
#include <cstdlib>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;

    qCritical().noquote() << "FAIL:" << message;
    std::exit(1);
}

template <typename T>
T mustParse(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::FactProvenance confirmedSource(const QString &name)
{
    return vinyl::FactProvenance{
        vinyl::ProvenanceKind::SourceProvider,
        name,
        vinyl::EvidenceStrength::Confirmed,
        std::nullopt};
}

int countKind(const QVector<vinyl::TransformationStage> &stages,
              vinyl::TransformationKind kind)
{
    int count = 0;
    for (const auto &stage : stages) {
        if (stage.kind == kind)
            ++count;
    }
    return count;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    const auto recording = mustParse(
        RecordingMbid::parse(QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "recording MBID should parse");

    SourceAudioFacts runtimeSource(recording);
    RequestedProcessing requested;
    requested.replayGainMode = ReplayGainMode::Track;
    requested.normalizationMode = NormalizationMode::ReplayGain;
    requested.softwareVolume = SoftwareVolumeIntent::Adjusted;
    requested.softwareGainDb = -3.0;
    requested.exclusiveModeRequested = true;

    MpvRuntimeObservations mpv;
    mpv.fileFormat = QStringLiteral("flac");
    mpv.selectedTrackCodec = QStringLiteral("flac");
    mpv.demuxSampleRateHz = 44100;
    mpv.demuxChannelCount = 6;
    mpv.demuxBitrateBps = 3000000;
    mpv.decodedSampleFormat = QStringLiteral("s32");
    mpv.decodedSampleRateHz = 96000;
    mpv.decodedChannelCount = 2;
    mpv.outputSampleFormat = QStringLiteral("float");
    mpv.outputSampleRateHz = 48000;
    mpv.outputChannelCount = 2;
    mpv.currentAudioBackend = QStringLiteral("wasapi");
    mpv.configuredAudioDevice = QStringLiteral("wasapi/{requested-endpoint}");
    mpv.audioExclusiveOption = true;

    const auto mapped = mapMpvObservations(std::move(runtimeSource), requested, mpv);

    require(mapped.signalPath.source.codec.isKnown()
                && *mapped.signalPath.source.codec.value() == QStringLiteral("flac"),
            "selected-track codec should populate source codec");
    require(mapped.signalPath.source.container.isKnown()
                && *mapped.signalPath.source.container.value() == QStringLiteral("flac"),
            "file-format should populate source container");
    require(mapped.signalPath.source.sampleRateHz.isKnown()
                && *mapped.signalPath.source.sampleRateHz.value() == 96000,
            "decoder-observed sample rate should outrank demux hint");
    require(mapped.signalPath.source.sampleRateHz.provenance().strength
                == EvidenceStrength::Observed,
            "decoder source sample rate should be observed evidence");
    require(mapped.signalPath.source.channels.isKnown()
                && *mapped.signalPath.source.channels.value() == 2,
            "decoder-observed channels should outrank demux hint");
    require(mapped.demuxSampleRateHint.isKnown()
                && *mapped.demuxSampleRateHint.value() == 44100,
            "demux sample-rate hint should remain visible as reported evidence");
    require(mapped.demuxSampleRateHint.provenance().strength
                == EvidenceStrength::Reported,
            "demux sample-rate hint must remain reported");
    require(mapped.demuxChannelCountHint.isKnown()
                && *mapped.demuxChannelCountHint.value() == 6,
            "demux channel hint should not masquerade as decoded truth");
    require(!mapped.signalPath.source.bitDepth.isKnown(),
            "sample-format names must not manufacture source bit depth");
    require(mapped.signalPath.source.bitrateKbps.isKnown()
                && *mapped.signalPath.source.bitrateKbps.value() == 3000,
            "demux bitrate may populate reported source bitrate");

    require(mapped.decodedSampleFormat.isKnown()
                && *mapped.decodedSampleFormat.value() == QStringLiteral("s32"),
            "audio-params format should remain decoded runtime evidence");
    require(mapped.mpvOutputSampleFormat.isKnown()
                && *mapped.mpvOutputSampleFormat.value() == QStringLiteral("float"),
            "audio-out-params format should be observed at the mpv audio API boundary");
    require(mapped.signalPath.observedOutput.sampleRateHz.isKnown()
                && *mapped.signalPath.observedOutput.sampleRateHz.value() == 48000,
            "mpv audio-out sample rate should populate observed output when device evidence is absent");
    require(mapped.signalPath.observedOutput.sampleRateHz.provenance().kind
                == ProvenanceKind::OutputBackend,
            "mpv output rate must be scoped to the output backend, not device runtime");
    require(mapped.signalPath.observedOutput.sampleFormat.isKnown()
                && *mapped.signalPath.observedOutput.sampleFormat.value()
                    == QStringLiteral("float"),
            "mpv output sample format should populate observed output");
    require(!mapped.signalPath.observedOutput.bitDepth.isKnown(),
            "mpv sample-format spelling must not be converted into invented output bit depth");

    require(mapped.signalPath.selectedOutput.backendId.isKnown()
                && *mapped.signalPath.selectedOutput.backendId.value()
                    == QStringLiteral("wasapi"),
            "current-ao should populate observed backend identity");
    require(mapped.signalPath.selectedOutput.backendId.provenance().strength
                == EvidenceStrength::Observed,
            "current-ao is observed backend evidence");
    require(mapped.configuredDeviceSelector.isKnown()
                && *mapped.configuredDeviceSelector.value()
                    == QStringLiteral("wasapi/{requested-endpoint}"),
            "audio-device should remain visible as configured selector");
    require(mapped.configuredDeviceSelector.provenance().strength
                == EvidenceStrength::Requested,
            "audio-device selector must remain request-strength evidence");
    require(!mapped.signalPath.selectedOutput.deviceId.isKnown(),
            "mpv audio-device must not masquerade as actual selected device");
    require(mapped.mpvExclusiveOption.isKnown() && *mapped.mpvExclusiveOption.value(),
            "mpv exclusive option should be captured as request evidence");
    require(mapped.mpvExclusiveOption.provenance().strength
                == EvidenceStrength::Requested,
            "mpv exclusive option must never be promoted above requested");
    require(!mapped.signalPath.selectedOutput.exclusiveConfirmed.isKnown(),
            "mpv exclusive request cannot confirm exclusive mode");
    require(mapped.signalPath.requested.exclusiveModeRequested,
            "caller-owned requested policy should survive mapping");

    require(countKind(mapped.signalPath.transformations,
                      TransformationKind::Resampling) == 1,
            "96 kHz decoder to 48 kHz audio API should record resampling");
    require(countKind(mapped.signalPath.transformations,
                      TransformationKind::SampleFormatConversion) == 1,
            "s32 decoder to float audio API should record sample-format conversion");
    require(countKind(mapped.signalPath.transformations,
                      TransformationKind::ChannelMix) == 0,
            "matching decoded/output channel count should not invent channel mixing");
    require(mapped.signalPath.verdict().conversion == ConversionVerdict::Transformed,
            "observed runtime conversion should produce transformed verdict");
    require(mapped.signalPath.verdict().exclusive
                == ExclusiveVerdict::ExclusiveRequested,
            "requested exclusive without device confirmation must stay requested");
    require(mapped.signalPath.verdict().bitPerfect == BitPerfectVerdict::NotProven,
            "observed conversion path can never claim bit-perfect");

    SourceAudioFacts withDeviceSource(recording);
    withDeviceSource.bitDepth = EvidenceFact<int>::known(
        24, confirmedSource(QStringLiteral("fixture:provider-bit-depth")));

    MpvRuntimeObservations equalMpv;
    equalMpv.decodedSampleFormat = QStringLiteral("s24");
    equalMpv.decodedSampleRateHz = 96000;
    equalMpv.decodedChannelCount = 2;
    equalMpv.outputSampleFormat = QStringLiteral("s24");
    equalMpv.outputSampleRateHz = 96000;
    equalMpv.outputChannelCount = 2;
    equalMpv.currentAudioBackend = QStringLiteral("wasapi");
    equalMpv.configuredAudioDevice = QStringLiteral("auto");
    equalMpv.audioExclusiveOption = true;

    DeviceRuntimeObservations device;
    device.actualDeviceId = QStringLiteral("{actual-endpoint}");
    device.outputSampleFormat = QStringLiteral("s24");
    device.outputSampleRateHz = 96000;
    device.outputChannelCount = 2;
    device.outputBitDepth = 24;
    device.exclusiveConfirmed = true;

    RequestedProcessing equalRequested;
    equalRequested.exclusiveModeRequested = true;
    const auto deviceMapped =
        mapMpvObservations(std::move(withDeviceSource), equalRequested, equalMpv, device);

    require(deviceMapped.signalPath.selectedOutput.deviceId.isKnown()
                && *deviceMapped.signalPath.selectedOutput.deviceId.value()
                    == QStringLiteral("{actual-endpoint}"),
            "device adapter evidence may populate actual device identity");
    require(deviceMapped.signalPath.selectedOutput.deviceId.provenance().kind
                == ProvenanceKind::DeviceRuntime,
            "actual device identity must carry device-runtime provenance");
    require(deviceMapped.signalPath.observedOutput.sampleRateHz.provenance().kind
                == ProvenanceKind::DeviceRuntime,
            "device output rate should outrank mpv audio-API boundary evidence");
    require(deviceMapped.signalPath.observedOutput.bitDepth.isKnown()
                && *deviceMapped.signalPath.observedOutput.bitDepth.value() == 24,
            "bit depth may be populated only when a device adapter actually reports it");
    require(deviceMapped.signalPath.selectedOutput.exclusiveConfirmed.isKnown()
                && *deviceMapped.signalPath.selectedOutput.exclusiveConfirmed.value(),
            "device adapter confirmation may populate exclusive confirmation");
    require(deviceMapped.signalPath.selectedOutput.exclusiveConfirmed.provenance().strength
                == EvidenceStrength::Confirmed,
            "exclusive confirmation requires confirmed-strength device evidence");
    require(deviceMapped.signalPath.verdict().exclusive
                == ExclusiveVerdict::ExclusiveConfirmed,
            "confirmed device evidence may produce exclusive_confirmed");
    require(deviceMapped.signalPath.transformations.isEmpty(),
            "matching decoder, audio API and device facts should add no transformation stage");
    require(deviceMapped.signalPath.verdict().conversion
                == ConversionVerdict::NoObservedConversion,
            "complete matching observed format may say no observed conversion");
    require(deviceMapped.signalPath.verdict().bitPerfect == BitPerfectVerdict::NotProven,
            "no observed conversion plus exclusive confirmation is still not bit-perfect proof");

    SourceAudioFacts policySource(recording);
    MpvRuntimeObservations requestOnly;
    requestOnly.audioExclusiveOption = true;
    requestOnly.configuredAudioDevice = QStringLiteral("wasapi/{requested-only}");
    const auto requestOnlyMapped =
        mapMpvObservations(std::move(policySource), RequestedProcessing{}, requestOnly);

    require(!requestOnlyMapped.signalPath.requested.exclusiveModeRequested,
            "runtime option echo must not rewrite caller-owned requested policy");
    require(requestOnlyMapped.signalPath.verdict().exclusive == ExclusiveVerdict::Unknown,
            "mpv option echo alone must not become an exclusive result");
    require(!requestOnlyMapped.signalPath.selectedOutput.deviceId.isKnown(),
            "configured selector without device evidence must leave device identity unknown");
    require(!requestOnlyMapped.signalPath.observedOutput.sampleRateHz.isKnown(),
            "missing output runtime facts must stay unknown");

    SourceAudioFacts strongSource(recording);
    strongSource.codec = EvidenceFact<QString>::known(
        QStringLiteral("flac"),
        confirmedSource(QStringLiteral("fixture:confirmed-codec")));
    strongSource.sampleRateHz = EvidenceFact<int>::known(
        192000,
        confirmedSource(QStringLiteral("fixture:confirmed-rate")));

    MpvRuntimeObservations weakerRuntime;
    weakerRuntime.selectedTrackCodec = QStringLiteral("aac");
    weakerRuntime.decodedSampleRateHz = 96000;
    const auto strongMapped =
        mapMpvObservations(std::move(strongSource), RequestedProcessing{}, weakerRuntime);

    require(*strongMapped.signalPath.source.codec.value() == QStringLiteral("flac"),
            "reported runtime codec must not overwrite confirmed source evidence");
    require(*strongMapped.signalPath.source.sampleRateHz.value() == 192000,
            "observed decoder rate must not overwrite confirmed source evidence");

    MpvRuntimeObservations invalidRuntime;
    invalidRuntime.decodedSampleRateHz = 0;
    invalidRuntime.decodedChannelCount = -1;
    invalidRuntime.outputSampleRateHz = -48000;
    invalidRuntime.outputChannelCount = 0;
    invalidRuntime.currentAudioBackend = QStringLiteral("   ");

    SourceAudioFacts invalidSource(recording);
    const auto invalidMapped =
        mapMpvObservations(std::move(invalidSource), RequestedProcessing{}, invalidRuntime);

    require(!invalidMapped.signalPath.source.sampleRateHz.isKnown(),
            "non-positive decoder sample rate must remain unknown");
    require(!invalidMapped.signalPath.source.channels.isKnown(),
            "non-positive decoder channels must remain unknown");
    require(!invalidMapped.signalPath.observedOutput.sampleRateHz.isKnown(),
            "non-positive output sample rate must remain unknown");
    require(!invalidMapped.signalPath.observedOutput.channels.isKnown(),
            "non-positive output channels must remain unknown");
    require(!invalidMapped.signalPath.selectedOutput.backendId.isKnown(),
            "blank backend identity must remain unknown");

    std::printf("VINYL_BD2_MPV_OBSERVABILITY_OK checks=%d\n", g_checks);
    return 0;
}
