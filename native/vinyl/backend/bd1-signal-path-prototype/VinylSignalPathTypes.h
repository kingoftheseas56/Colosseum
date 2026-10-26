#pragma once

#include "../ba0-domain-prototype/VinylDomainTypes.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class FactKnowledge {
    Unknown,
    Known,
};

enum class EvidenceStrength {
    Unknown,
    Reported,
    Requested,
    Inferred,
    Observed,
    Confirmed,
};

enum class ProvenanceKind {
    Unknown,
    CanonicalMetadata,
    SourceProvider,
    UserPolicy,
    PlayerRuntime,
    OutputBackend,
    DeviceRuntime,
};

QString evidenceStrengthName(EvidenceStrength strength);
QString provenanceKindName(ProvenanceKind kind);

struct FactProvenance final {
    ProvenanceKind kind = ProvenanceKind::Unknown;
    QString source;
    EvidenceStrength strength = EvidenceStrength::Unknown;
    std::optional<ProviderItemRef> providerItem;
};

template <typename T>
class EvidenceFact final
{
public:
    static EvidenceFact unknown(FactProvenance provenance = {})
    {
        return EvidenceFact(std::nullopt, std::move(provenance));
    }

    static EvidenceFact known(T value, FactProvenance provenance)
    {
        return EvidenceFact(std::optional<T>{std::move(value)}, std::move(provenance));
    }

    FactKnowledge knowledge() const noexcept
    {
        return m_value.has_value() ? FactKnowledge::Known : FactKnowledge::Unknown;
    }

    bool isKnown() const noexcept { return m_value.has_value(); }
    const std::optional<T> &value() const noexcept { return m_value; }
    const FactProvenance &provenance() const noexcept { return m_provenance; }

private:
    EvidenceFact(std::optional<T> value, FactProvenance provenance)
        : m_value(std::move(value))
        , m_provenance(std::move(provenance))
    {
    }

    std::optional<T> m_value;
    FactProvenance m_provenance;
};

struct SourceAudioFacts final {
    explicit SourceAudioFacts(RecordingMbid recording)
        : recordingMbid(std::move(recording))
    {
    }

    RecordingMbid recordingMbid;
    std::optional<ProviderItemRef> sourceProviderItem;
    EvidenceFact<QString> codec = EvidenceFact<QString>::unknown();
    EvidenceFact<QString> container = EvidenceFact<QString>::unknown();
    EvidenceFact<int> channels = EvidenceFact<int>::unknown();
    EvidenceFact<int> sampleRateHz = EvidenceFact<int>::unknown();
    EvidenceFact<int> bitDepth = EvidenceFact<int>::unknown();
    EvidenceFact<int> bitrateKbps = EvidenceFact<int>::unknown();
};

enum class ReplayGainMode {
    Off,
    Track,
    Album,
};

enum class NormalizationMode {
    Off,
    ReplayGain,
    Loudness,
};

enum class RequestedSampleRateBehavior {
    DeviceDefault,
    FollowSource,
    FixedRate,
};

enum class SoftwareVolumeIntent {
    Unspecified,
    Unity,
    Adjusted,
};

struct RequestedProcessing final {
    ReplayGainMode replayGainMode = ReplayGainMode::Off;
    NormalizationMode normalizationMode = NormalizationMode::Off;
    RequestedSampleRateBehavior sampleRateBehavior = RequestedSampleRateBehavior::DeviceDefault;
    std::optional<int> fixedSampleRateHz;
    SoftwareVolumeIntent softwareVolume = SoftwareVolumeIntent::Unspecified;
    std::optional<double> softwareGainDb;
    bool exclusiveModeRequested = false;
};

enum class TransformationKind {
    ReplayGain,
    LoudnessNormalization,
    SoftwareVolume,
    Resampling,
    SampleFormatConversion,
    ChannelMix,
    Other,
};

struct TransformationStage final {
    TransformationKind kind = TransformationKind::Other;
    FactProvenance provenance;
    QString detail;
    EvidenceFact<int> inputSampleRateHz = EvidenceFact<int>::unknown();
    EvidenceFact<int> outputSampleRateHz = EvidenceFact<int>::unknown();
};

struct SelectedOutput final {
    EvidenceFact<QString> backendId = EvidenceFact<QString>::unknown();
    EvidenceFact<QString> deviceId = EvidenceFact<QString>::unknown();
    EvidenceFact<bool> exclusiveConfirmed = EvidenceFact<bool>::unknown();
};

struct ObservedOutputFacts final {
    EvidenceFact<int> channels = EvidenceFact<int>::unknown();
    EvidenceFact<int> sampleRateHz = EvidenceFact<int>::unknown();
    EvidenceFact<int> bitDepth = EvidenceFact<int>::unknown();
    EvidenceFact<QString> sampleFormat = EvidenceFact<QString>::unknown();
};

enum class ConversionVerdict {
    Unknown,
    Transformed,
    NoObservedConversion,
};

enum class ExclusiveVerdict {
    Unknown,
    ExclusiveRequested,
    ExclusiveConfirmed,
};

enum class BitPerfectVerdict {
    NotProven,
};

QString conversionVerdictName(ConversionVerdict verdict);
QString exclusiveVerdictName(ExclusiveVerdict verdict);
QString bitPerfectVerdictName(BitPerfectVerdict verdict);

struct SignalPathVerdict final {
    ConversionVerdict conversion = ConversionVerdict::Unknown;
    ExclusiveVerdict exclusive = ExclusiveVerdict::Unknown;
    BitPerfectVerdict bitPerfect = BitPerfectVerdict::NotProven;
};

class SignalPathSnapshot final
{
public:
    explicit SignalPathSnapshot(SourceAudioFacts sourceFacts)
        : source(std::move(sourceFacts))
    {
    }

    SignalPathVerdict verdict() const;

    SourceAudioFacts source;
    RequestedProcessing requested;
    QVector<TransformationStage> transformations;
    SelectedOutput selectedOutput;
    ObservedOutputFacts observedOutput;
};

} // namespace vinyl
