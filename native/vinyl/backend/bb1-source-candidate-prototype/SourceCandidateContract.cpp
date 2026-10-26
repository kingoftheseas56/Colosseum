#include "SourceCandidateContract.h"

#include <QByteArray>

#include <algorithm>

namespace vinyl {

namespace {

SourceCandidateResult<NormalizedSourceCandidate> candidateFailure(
    SourceCandidateErrorCode code,
    const QString &field,
    const QString &detail)
{
    return SourceCandidateResult<NormalizedSourceCandidate>::failure(
        code,
        field,
        detail);
}

bool hasBlankOptionalText(const std::optional<QString> &value)
{
    return value.has_value() && value->trimmed().isEmpty();
}

std::optional<SourceProviderCapability> requiredCapability(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Unknown:
        return std::nullopt;
    case SourceKind::LocalFile:
        return SourceProviderCapability::LocalFileSources;
    case SourceKind::Http:
        return SourceProviderCapability::HttpSources;
    case SourceKind::PeerToPeer:
        return SourceProviderCapability::PeerToPeerSources;
    case SourceKind::ExternalResolver:
        return SourceProviderCapability::ExternalResolverSources;
    case SourceKind::ProviderMediated:
        return SourceProviderCapability::ProviderMediatedSources;
    }
    return std::nullopt;
}

std::optional<SourceCandidateError> validateFacts(
    const SourceCandidateFacts &facts)
{
    if (hasBlankOptionalText(facts.title)
        || hasBlankOptionalText(facts.artist)
        || hasBlankOptionalText(facts.album)
        || hasBlankOptionalText(facts.codec)
        || hasBlankOptionalText(facts.container)
        || hasBlankOptionalText(facts.mimeType)
        || hasBlankOptionalText(facts.qualityLabel)) {
        return SourceCandidateError{
            SourceCandidateErrorCode::InvalidFactHint,
            QStringLiteral("facts"),
            QStringLiteral("Known textual facts must not be blank; use unknown instead."),
        };
    }

    if ((facts.durationMs && *facts.durationMs <= 0)
        || (facts.contentLengthBytes && *facts.contentLengthBytes <= 0)
        || (facts.bitrateKbps && *facts.bitrateKbps <= 0)
        || (facts.sampleRateHz && *facts.sampleRateHz <= 0)
        || (facts.bitDepth && *facts.bitDepth <= 0)
        || (facts.channels && *facts.channels <= 0)) {
        return SourceCandidateError{
            SourceCandidateErrorCode::InvalidFactHint,
            QStringLiteral("facts"),
            QStringLiteral("Known numeric source facts must be positive."),
        };
    }

    return std::nullopt;
}

} // namespace

QString sourceCandidateErrorCodeName(SourceCandidateErrorCode code)
{
    switch (code) {
    case SourceCandidateErrorCode::None:
        return QStringLiteral("none");
    case SourceCandidateErrorCode::InvalidSourceProviderId:
        return QStringLiteral("invalid_source_provider_id");
    case SourceCandidateErrorCode::InvalidCandidateId:
        return QStringLiteral("invalid_candidate_id");
    case SourceCandidateErrorCode::InvalidProviderDescriptor:
        return QStringLiteral("invalid_provider_descriptor");
    case SourceCandidateErrorCode::ProviderMissingCapability:
        return QStringLiteral("provider_missing_capability");
    case SourceCandidateErrorCode::InvalidFactHint:
        return QStringLiteral("invalid_fact_hint");
    case SourceCandidateErrorCode::DuplicateCandidateKey:
        return QStringLiteral("duplicate_candidate_key");
    case SourceCandidateErrorCode::RecordingMismatch:
        return QStringLiteral("recording_mismatch");
    }
    return QStringLiteral("unknown");
}

SourceCandidateResult<SourceProviderId> SourceProviderId::parse(const QString &text)
{
    const auto parsed = ProviderId::parse(text);
    if (!parsed.ok()) {
        return SourceCandidateResult<SourceProviderId>::failure(
            SourceCandidateErrorCode::InvalidSourceProviderId,
            QStringLiteral("sourceProviderId"),
            parsed.error.detail);
    }

    return SourceCandidateResult<SourceProviderId>::success(
        SourceProviderId(*parsed.value));
}

SourceCandidateResult<SourceCandidateId> SourceCandidateId::parse(const QString &text)
{
    const auto parsed = ProviderItemId::parse(text);
    if (!parsed.ok()) {
        return SourceCandidateResult<SourceCandidateId>::failure(
            SourceCandidateErrorCode::InvalidCandidateId,
            QStringLiteral("candidateId"),
            parsed.error.detail);
    }

    return SourceCandidateResult<SourceCandidateId>::success(
        SourceCandidateId(*parsed.value));
}

SourceCandidateResult<SourceProviderDescriptor> SourceProviderDescriptor::create(
    SourceProviderId id,
    SourceProviderCapabilities capabilities)
{
    const bool discovers = capabilities.testFlag(
        SourceProviderCapability::DiscoverCandidates);
    const bool resolves = capabilities.testFlag(
        SourceProviderCapability::ResolveExactSource);

    if (!discovers && !resolves) {
        return SourceCandidateResult<SourceProviderDescriptor>::failure(
            SourceCandidateErrorCode::InvalidProviderDescriptor,
            QStringLiteral("capabilities"),
            QStringLiteral("A source provider must discover candidates, resolve sources, or both."));
    }

    return SourceCandidateResult<SourceProviderDescriptor>::success(
        SourceProviderDescriptor(std::move(id), capabilities));
}

QString SourceCandidateKey::stableKey() const
{
    const QByteArray encodedCandidate = m_candidateId.value().toUtf8().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    return QStringLiteral("sc1|%1|%2")
        .arg(m_providerId.value(),
             QString::fromLatin1(encodedCandidate));
}

QString sourceDiscoveryMethodName(SourceDiscoveryMethod method)
{
    switch (method) {
    case SourceDiscoveryMethod::ProviderSearch:
        return QStringLiteral("provider_search");
    case SourceDiscoveryMethod::CanonicalMapping:
        return QStringLiteral("canonical_mapping");
    case SourceDiscoveryMethod::LocalScan:
        return QStringLiteral("local_scan");
    case SourceDiscoveryMethod::ImportedIndex:
        return QStringLiteral("imported_index");
    }
    return QStringLiteral("unknown");
}

QString sourceKindName(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Unknown:
        return QStringLiteral("unknown");
    case SourceKind::LocalFile:
        return QStringLiteral("local_file");
    case SourceKind::Http:
        return QStringLiteral("http");
    case SourceKind::PeerToPeer:
        return QStringLiteral("peer_to_peer");
    case SourceKind::ExternalResolver:
        return QStringLiteral("external_resolver");
    case SourceKind::ProviderMediated:
        return QStringLiteral("provider_mediated");
    }
    return QStringLiteral("unknown");
}

QString candidateResolutionModeName(CandidateResolutionMode mode)
{
    switch (mode) {
    case CandidateResolutionMode::StableReference:
        return QStringLiteral("stable_reference");
    case CandidateResolutionMode::Deferred:
        return QStringLiteral("deferred");
    case CandidateResolutionMode::Refreshable:
        return QStringLiteral("refreshable");
    }
    return QStringLiteral("unknown");
}

QString sourceCandidateFailureCodeName(SourceCandidateFailureCode code)
{
    switch (code) {
    case SourceCandidateFailureCode::TemporarilyUnavailable:
        return QStringLiteral("temporarily_unavailable");
    case SourceCandidateFailureCode::Unavailable:
        return QStringLiteral("unavailable");
    case SourceCandidateFailureCode::ProviderFailure:
        return QStringLiteral("provider_failure");
    case SourceCandidateFailureCode::ResolutionFailed:
        return QStringLiteral("resolution_failed");
    }
    return QStringLiteral("unknown");
}

SourceCandidateResult<NormalizedSourceCandidate> NormalizedSourceCandidate::create(
    RecordingMbid requestedRecordingMbid,
    SourceProviderDescriptor provider,
    SourceCandidateId candidateId,
    SourceDiscoveryMethod discoveryMethod,
    SourceKind sourceKind,
    CandidateResolutionMode resolutionMode,
    SourceCandidateFacts facts,
    std::optional<SourceCandidateFailure> failure)
{
    if (!provider.hasCapability(SourceProviderCapability::DiscoverCandidates)) {
        return candidateFailure(
            SourceCandidateErrorCode::ProviderMissingCapability,
            QStringLiteral("provider.capabilities"),
            QStringLiteral("Only a discovery-capable source provider may emit a candidate."));
    }

    if (const auto required = requiredCapability(sourceKind);
        required && !provider.hasCapability(*required)) {
        return candidateFailure(
            SourceCandidateErrorCode::ProviderMissingCapability,
            QStringLiteral("sourceKind"),
            QStringLiteral("Candidate kind is not declared by the source provider."));
    }

    if ((resolutionMode == CandidateResolutionMode::Deferred
         || resolutionMode == CandidateResolutionMode::Refreshable)
        && !provider.hasCapability(SourceProviderCapability::ResolveExactSource)) {
        return candidateFailure(
            SourceCandidateErrorCode::ProviderMissingCapability,
            QStringLiteral("resolutionMode"),
            QStringLiteral("Deferred or refreshable candidates require exact-source resolution capability."));
    }

    if (const auto factError = validateFacts(facts)) {
        return candidateFailure(
            factError->code,
            factError->field,
            factError->detail);
    }

    SourceCandidateKey key(provider.id(), std::move(candidateId));
    SourceCandidateProvenance provenance(std::move(key), discoveryMethod);

    return SourceCandidateResult<NormalizedSourceCandidate>::success(
        NormalizedSourceCandidate(
            std::move(requestedRecordingMbid),
            std::move(provider),
            std::move(provenance),
            sourceKind,
            resolutionMode,
            std::move(facts),
            std::move(failure)));
}

SourceCandidateResult<SourceCandidateSet> SourceCandidateSet::create(
    RecordingMbid requestedRecordingMbid,
    QVector<NormalizedSourceCandidate> candidates)
{
    for (const auto &candidate : candidates) {
        if (candidate.requestedRecordingMbid() != requestedRecordingMbid) {
            return SourceCandidateResult<SourceCandidateSet>::failure(
                SourceCandidateErrorCode::RecordingMismatch,
                QStringLiteral("requestedRecordingMbid"),
                QStringLiteral("All candidates in a set must retain the requested Recording MBID."));
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const NormalizedSourceCandidate &left,
           const NormalizedSourceCandidate &right) {
            return left.provenance().key().stableKey()
                < right.provenance().key().stableKey();
        });

    for (qsizetype i = 1; i < candidates.size(); ++i) {
        if (candidates.at(i - 1).provenance().key()
            == candidates.at(i).provenance().key()) {
            return SourceCandidateResult<SourceCandidateSet>::failure(
                SourceCandidateErrorCode::DuplicateCandidateKey,
                QStringLiteral("candidates"),
                QStringLiteral("A provider-namespaced candidate key may appear only once."));
        }
    }

    return SourceCandidateResult<SourceCandidateSet>::success(
        SourceCandidateSet(
            std::move(requestedRecordingMbid),
            std::move(candidates)));
}

} // namespace vinyl
