#include "VinylSignalPathTypes.h"

#include <QCoreApplication>
#include <QDebug>

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

vinyl::FactProvenance provenance(vinyl::ProvenanceKind kind,
                                 vinyl::EvidenceStrength strength,
                                 const QString &source)
{
    return vinyl::FactProvenance{kind, source, strength, std::nullopt};
}

vinyl::SourceAudioFacts source2496(const vinyl::RecordingMbid &recording)
{
    using namespace vinyl;

    SourceAudioFacts source(recording);
    const auto metadata = provenance(ProvenanceKind::SourceProvider,
                                     EvidenceStrength::Reported,
                                     QStringLiteral("fixture.source-metadata"));
    source.codec = EvidenceFact<QString>::known(QStringLiteral("flac"), metadata);
    source.container = EvidenceFact<QString>::known(QStringLiteral("flac"), metadata);
    source.channels = EvidenceFact<int>::known(2, metadata);
    source.sampleRateHz = EvidenceFact<int>::known(96000, metadata);
    source.bitDepth = EvidenceFact<int>::known(24, metadata);
    return source;
}

vinyl::ObservedOutputFacts observed2496()
{
    using namespace vinyl;

    ObservedOutputFacts output;
    const auto runtime = provenance(ProvenanceKind::DeviceRuntime,
                                    EvidenceStrength::Observed,
                                    QStringLiteral("fixture.device"));
    output.channels = EvidenceFact<int>::known(2, runtime);
    output.sampleRateHz = EvidenceFact<int>::known(96000, runtime);
    output.bitDepth = EvidenceFact<int>::known(24, runtime);
    output.sampleFormat = EvidenceFact<QString>::known(QStringLiteral("s24"), runtime);
    return output;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;

    const auto recording = mustParse(
        RecordingMbid::parse(QStringLiteral("33333333-3333-4333-8333-333333333333")),
        "recording MBID should parse");

    // 1. Source facts never manufacture actual-output facts.
    SignalPathSnapshot unknownOutput(source2496(recording));
    require(unknownOutput.verdict().conversion == ConversionVerdict::Unknown,
            "24/96 source with unknown output must remain unknown");
    require(unknownOutput.verdict().bitPerfect == BitPerfectVerdict::NotProven,
            "source quality cannot prove bit-perfect output");

    // 2. A 96 kHz source observed at 48 kHz records transformation.
    SignalPathSnapshot resampled(source2496(recording));
    resampled.observedOutput = observed2496();
    resampled.observedOutput.sampleRateHz = EvidenceFact<int>::known(
        48000,
        provenance(ProvenanceKind::DeviceRuntime,
                   EvidenceStrength::Observed,
                   QStringLiteral("fixture.device")));
    require(resampled.verdict().conversion == ConversionVerdict::Transformed,
            "observed 96 -> 48 kHz must be transformed");

    SignalPathSnapshot reportedOnly(source2496(recording));
    reportedOnly.observedOutput = observed2496();
    reportedOnly.observedOutput.sampleRateHz = EvidenceFact<int>::known(
        48000,
        provenance(ProvenanceKind::DeviceRuntime,
                   EvidenceStrength::Reported,
                   QStringLiteral("fixture.unverified-report")));
    require(reportedOnly.verdict().conversion == ConversionVerdict::Unknown,
            "reported output mismatch must not masquerade as observed conversion");

    // 3. Requested exclusivity is not confirmed exclusivity.
    SignalPathSnapshot exclusiveRequested(source2496(recording));
    exclusiveRequested.requested.exclusiveModeRequested = true;
    require(exclusiveRequested.verdict().exclusive
                == ExclusiveVerdict::ExclusiveRequested,
            "exclusive request must remain only a request");
    exclusiveRequested.selectedOutput.exclusiveConfirmed =
        EvidenceFact<bool>::known(
            true,
            provenance(ProvenanceKind::OutputBackend,
                       EvidenceStrength::Observed,
                       QStringLiteral("fixture.backend")));
    require(exclusiveRequested.verdict().exclusive
                == ExclusiveVerdict::ExclusiveRequested,
            "observed hint must not be promoted to exclusive confirmation");
    exclusiveRequested.selectedOutput.exclusiveConfirmed =
        EvidenceFact<bool>::known(
            true,
            provenance(ProvenanceKind::DeviceRuntime,
                       EvidenceStrength::Confirmed,
                       QStringLiteral("fixture.platform-proof")));
    require(exclusiveRequested.verdict().exclusive
                == ExclusiveVerdict::ExclusiveConfirmed,
            "confirmed platform evidence may confirm exclusivity");

    // 4. Matching observed format is still not bit-perfect proof.
    SignalPathSnapshot matching(source2496(recording));
    matching.observedOutput = observed2496();
    require(matching.verdict().conversion
                == ConversionVerdict::NoObservedConversion,
            "matching complete observed format should report no observed conversion");
    require(matching.verdict().bitPerfect == BitPerfectVerdict::NotProven,
            "no observed conversion must not imply bit-perfect");

    // 5. Unknown source bit depth remains unknown.
    auto unknownDepthSource = source2496(recording);
    unknownDepthSource.bitDepth = EvidenceFact<int>::unknown(
        provenance(ProvenanceKind::SourceProvider,
                   EvidenceStrength::Reported,
                   QStringLiteral("fixture.provider")));
    SignalPathSnapshot unknownDepth(std::move(unknownDepthSource));
    unknownDepth.observedOutput = observed2496();
    require(!unknownDepth.source.bitDepth.isKnown(),
            "unknown source bit depth must stay unknown");
    require(unknownDepth.verdict().conversion == ConversionVerdict::Unknown,
            "incomplete format evidence must not claim no conversion");

    // 6. Requested ReplayGain policy is not a source fact or observed transform.
    SignalPathSnapshot replayGain(source2496(recording));
    replayGain.requested.replayGainMode = ReplayGainMode::Track;
    replayGain.requested.normalizationMode = NormalizationMode::ReplayGain;
    replayGain.requested.softwareVolume = SoftwareVolumeIntent::Adjusted;
    replayGain.requested.softwareGainDb = -3.0;
    require(replayGain.source.sampleRateHz.isKnown()
                && *replayGain.source.sampleRateHz.value() == 96000,
            "requested processing must not rewrite source facts");
    require(replayGain.transformations.isEmpty(),
            "a requested processing policy is not evidence that processing occurred");
    require(replayGain.verdict().conversion == ConversionVerdict::Unknown,
            "requested ReplayGain alone must not manufacture transformation evidence");

    // 7. Canonical and provider provenance survive independently.
    const auto providerId = mustParse(
        ProviderId::parse(QStringLiteral("Tankorent-Music")),
        "provider ID should parse");
    const auto itemId = mustParse(
        ProviderItemId::parse(QStringLiteral("candidate:ABC-123")),
        "provider item ID should parse");
    const ProviderItemRef providerItem(providerId, itemId);

    auto providerSource = source2496(recording);
    providerSource.sourceProviderItem = providerItem;
    FactProvenance providerProvenance{
        ProvenanceKind::SourceProvider,
        QStringLiteral("fixture.tankorent"),
        EvidenceStrength::Reported,
        providerItem};
    providerSource.codec = EvidenceFact<QString>::known(
        QStringLiteral("flac"), providerProvenance);
    SignalPathSnapshot provenanceSnapshot(std::move(providerSource));
    require(provenanceSnapshot.source.recordingMbid == recording,
            "canonical Recording MBID must survive source provenance");
    require(provenanceSnapshot.source.sourceProviderItem.has_value(),
            "provider source identity should survive");
    require(provenanceSnapshot.source.sourceProviderItem->providerId() == providerId,
            "provider ID must remain distinct from canonical identity");
    require(provenanceSnapshot.source.codec.provenance().providerItem.has_value(),
            "fact-level provider provenance should survive");
    require(provenanceSnapshot.source.codec.provenance().providerItem->itemId() == itemId,
            "opaque provider item provenance should remain intact");

    // 8. This native model is usable with Qt Core only and no QML object layer.
    SignalPathSnapshot coreOnly(source2496(recording));
    coreOnly.selectedOutput.backendId = EvidenceFact<QString>::known(
        QStringLiteral("opaque.backend"),
        provenance(ProvenanceKind::OutputBackend,
                   EvidenceStrength::Observed,
                   QStringLiteral("fixture.runtime")));
    coreOnly.selectedOutput.deviceId = EvidenceFact<QString>::known(
        QStringLiteral("{opaque-device-id}"),
        provenance(ProvenanceKind::DeviceRuntime,
                   EvidenceStrength::Observed,
                   QStringLiteral("fixture.runtime")));
    require(coreOnly.selectedOutput.backendId.isKnown(),
            "Qt Core value model should hold opaque backend identity");
    require(coreOnly.selectedOutput.deviceId.isKnown(),
            "Qt Core value model should hold opaque device identity");

    require(conversionVerdictName(ConversionVerdict::NoObservedConversion)
                == QStringLiteral("no_observed_conversion"),
            "conversion verdict should expose stable truthful vocabulary");
    require(exclusiveVerdictName(ExclusiveVerdict::ExclusiveRequested)
                == QStringLiteral("exclusive_requested"),
            "exclusive request should expose stable truthful vocabulary");
    require(bitPerfectVerdictName(BitPerfectVerdict::NotProven)
                == QStringLiteral("bit_perfect_not_proven"),
            "bit-perfect status should never overclaim");

    qInfo().noquote() << "VINYL_BD1_SIGNAL_PATH_OK checks=" << g_checks;
    return 0;
}
