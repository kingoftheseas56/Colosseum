#include "VinylSignalPathTypes.h"

namespace vinyl {
namespace {

bool hasObservedOrConfirmedEvidence(EvidenceStrength strength)
{
    return strength == EvidenceStrength::Observed
        || strength == EvidenceStrength::Confirmed;
}

template <typename T>
bool observedMismatch(const EvidenceFact<T> &source, const EvidenceFact<T> &observed)
{
    return source.isKnown()
        && observed.isKnown()
        && hasObservedOrConfirmedEvidence(observed.provenance().strength)
        && *source.value() != *observed.value();
}

template <typename T>
bool observedEqual(const EvidenceFact<T> &source, const EvidenceFact<T> &observed)
{
    return source.isKnown()
        && observed.isKnown()
        && hasObservedOrConfirmedEvidence(observed.provenance().strength)
        && *source.value() == *observed.value();
}

bool hasObservedTransformation(const QVector<TransformationStage> &stages)
{
    for (const auto &stage : stages) {
        if (hasObservedOrConfirmedEvidence(stage.provenance.strength))
            return true;
    }
    return false;
}

} // namespace

QString evidenceStrengthName(EvidenceStrength strength)
{
    switch (strength) {
    case EvidenceStrength::Unknown:
        return QStringLiteral("unknown");
    case EvidenceStrength::Reported:
        return QStringLiteral("reported");
    case EvidenceStrength::Requested:
        return QStringLiteral("requested");
    case EvidenceStrength::Inferred:
        return QStringLiteral("inferred");
    case EvidenceStrength::Observed:
        return QStringLiteral("observed");
    case EvidenceStrength::Confirmed:
        return QStringLiteral("confirmed");
    }
    return QStringLiteral("unknown");
}

QString provenanceKindName(ProvenanceKind kind)
{
    switch (kind) {
    case ProvenanceKind::Unknown:
        return QStringLiteral("unknown");
    case ProvenanceKind::CanonicalMetadata:
        return QStringLiteral("canonical_metadata");
    case ProvenanceKind::SourceProvider:
        return QStringLiteral("source_provider");
    case ProvenanceKind::UserPolicy:
        return QStringLiteral("user_policy");
    case ProvenanceKind::PlayerRuntime:
        return QStringLiteral("player_runtime");
    case ProvenanceKind::OutputBackend:
        return QStringLiteral("output_backend");
    case ProvenanceKind::DeviceRuntime:
        return QStringLiteral("device_runtime");
    }
    return QStringLiteral("unknown");
}

QString conversionVerdictName(ConversionVerdict verdict)
{
    switch (verdict) {
    case ConversionVerdict::Unknown:
        return QStringLiteral("unknown");
    case ConversionVerdict::Transformed:
        return QStringLiteral("transformed");
    case ConversionVerdict::NoObservedConversion:
        return QStringLiteral("no_observed_conversion");
    }
    return QStringLiteral("unknown");
}

QString exclusiveVerdictName(ExclusiveVerdict verdict)
{
    switch (verdict) {
    case ExclusiveVerdict::Unknown:
        return QStringLiteral("unknown");
    case ExclusiveVerdict::ExclusiveRequested:
        return QStringLiteral("exclusive_requested");
    case ExclusiveVerdict::ExclusiveConfirmed:
        return QStringLiteral("exclusive_confirmed");
    }
    return QStringLiteral("unknown");
}

QString bitPerfectVerdictName(BitPerfectVerdict verdict)
{
    switch (verdict) {
    case BitPerfectVerdict::NotProven:
        return QStringLiteral("bit_perfect_not_proven");
    }
    return QStringLiteral("bit_perfect_not_proven");
}

SignalPathVerdict SignalPathSnapshot::verdict() const
{
    SignalPathVerdict out;

    if (hasObservedTransformation(transformations)
        || observedMismatch(source.sampleRateHz, observedOutput.sampleRateHz)
        || observedMismatch(source.channels, observedOutput.channels)
        || observedMismatch(source.bitDepth, observedOutput.bitDepth)) {
        out.conversion = ConversionVerdict::Transformed;
    } else if (observedEqual(source.sampleRateHz, observedOutput.sampleRateHz)
               && observedEqual(source.channels, observedOutput.channels)
               && observedEqual(source.bitDepth, observedOutput.bitDepth)) {
        out.conversion = ConversionVerdict::NoObservedConversion;
    }

    const bool exclusiveIsConfirmed =
        selectedOutput.exclusiveConfirmed.isKnown()
        && *selectedOutput.exclusiveConfirmed.value()
        && selectedOutput.exclusiveConfirmed.provenance().strength
            == EvidenceStrength::Confirmed;

    if (exclusiveIsConfirmed) {
        out.exclusive = ExclusiveVerdict::ExclusiveConfirmed;
    } else if (requested.exclusiveModeRequested) {
        out.exclusive = ExclusiveVerdict::ExclusiveRequested;
    }

    // BD1 deliberately has no positive bit-perfect state. Matching formats,
    // exclusive output, and absent observed conversion remain insufficient proof.
    out.bitPerfect = BitPerfectVerdict::NotProven;
    return out;
}

} // namespace vinyl
