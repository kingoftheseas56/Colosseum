#pragma once

#include "VinylDomainTypes.h"

#include <QFlags>
#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class SourceCandidateErrorCode {
    None,
    InvalidSourceProviderId,
    InvalidCandidateId,
    InvalidProviderDescriptor,
    ProviderMissingCapability,
    InvalidFactHint,
    DuplicateCandidateKey,
    RecordingMismatch,
};

struct SourceCandidateError final {
    SourceCandidateErrorCode code = SourceCandidateErrorCode::None;
    QString field;
    QString detail;
};

QString sourceCandidateErrorCodeName(SourceCandidateErrorCode code);

template <typename T>
struct SourceCandidateResult final {
    std::optional<T> value;
    SourceCandidateError error;

    bool ok() const noexcept { return value.has_value(); }

    static SourceCandidateResult success(T value)
    {
        SourceCandidateResult out;
        out.value = std::move(value);
        return out;
    }

    static SourceCandidateResult failure(SourceCandidateErrorCode code,
                                         QString field,
                                         QString detail = QString())
    {
        SourceCandidateResult out;
        out.error = SourceCandidateError{
            code,
            std::move(field),
            std::move(detail),
        };
        return out;
    }
};

class SourceProviderId final
{
public:
    static SourceCandidateResult<SourceProviderId> parse(const QString &text);

    const QString &value() const noexcept { return m_providerId.value(); }
    const ProviderId &providerId() const noexcept { return m_providerId; }

    bool operator==(const SourceProviderId &other) const noexcept
    {
        return m_providerId == other.m_providerId;
    }

    bool operator!=(const SourceProviderId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit SourceProviderId(ProviderId providerId)
        : m_providerId(std::move(providerId))
    {
    }

    ProviderId m_providerId;
};

class SourceCandidateId final
{
public:
    static SourceCandidateResult<SourceCandidateId> parse(const QString &text);

    const QString &value() const noexcept { return m_itemId.value(); }
    const ProviderItemId &providerItemId() const noexcept { return m_itemId; }

    bool operator==(const SourceCandidateId &other) const noexcept
    {
        return m_itemId == other.m_itemId;
    }

    bool operator!=(const SourceCandidateId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit SourceCandidateId(ProviderItemId itemId)
        : m_itemId(std::move(itemId))
    {
    }

    ProviderItemId m_itemId;
};

enum class SourceProviderCapability : quint32 {
    DiscoverCandidates = 1u << 0,
    ResolveExactSource = 1u << 1,
    LocalFileSources = 1u << 2,
    HttpSources = 1u << 3,
    PeerToPeerSources = 1u << 4,
    ExternalResolverSources = 1u << 5,
    ProviderMediatedSources = 1u << 6,
    AuthenticatedAccess = 1u << 7,
};
Q_DECLARE_FLAGS(SourceProviderCapabilities, SourceProviderCapability)

class SourceProviderDescriptor final
{
public:
    static SourceCandidateResult<SourceProviderDescriptor> create(
        SourceProviderId id,
        SourceProviderCapabilities capabilities);

    const SourceProviderId &id() const noexcept { return m_id; }
    SourceProviderCapabilities capabilities() const noexcept { return m_capabilities; }

    bool hasCapability(SourceProviderCapability capability) const noexcept
    {
        return m_capabilities.testFlag(capability);
    }

private:
    SourceProviderDescriptor(SourceProviderId id,
                             SourceProviderCapabilities capabilities)
        : m_id(std::move(id))
        , m_capabilities(capabilities)
    {
    }

    SourceProviderId m_id;
    SourceProviderCapabilities m_capabilities;
};

class SourceCandidateKey final
{
public:
    SourceCandidateKey(SourceProviderId providerId, SourceCandidateId candidateId)
        : m_providerId(std::move(providerId))
        , m_candidateId(std::move(candidateId))
    {
    }

    const SourceProviderId &providerId() const noexcept { return m_providerId; }
    const SourceCandidateId &candidateId() const noexcept { return m_candidateId; }

    QString stableKey() const;

    bool operator==(const SourceCandidateKey &other) const noexcept
    {
        return m_providerId == other.m_providerId
            && m_candidateId == other.m_candidateId;
    }

    bool operator!=(const SourceCandidateKey &other) const noexcept
    {
        return !(*this == other);
    }

private:
    SourceProviderId m_providerId;
    SourceCandidateId m_candidateId;
};

enum class SourceDiscoveryMethod {
    ProviderSearch,
    CanonicalMapping,
    LocalScan,
    ImportedIndex,
};

QString sourceDiscoveryMethodName(SourceDiscoveryMethod method);

enum class SourceKind {
    Unknown,
    LocalFile,
    Http,
    PeerToPeer,
    ExternalResolver,
    ProviderMediated,
};

QString sourceKindName(SourceKind kind);

enum class CandidateResolutionMode {
    StableReference,
    Deferred,
    Refreshable,
};

QString candidateResolutionModeName(CandidateResolutionMode mode);

struct SourceCandidateFacts final {
    std::optional<QString> title;
    std::optional<QString> artist;
    std::optional<QString> album;
    std::optional<qint64> durationMs;
    std::optional<qint64> contentLengthBytes;
    std::optional<int> bitrateKbps;
    std::optional<int> sampleRateHz;
    std::optional<int> bitDepth;
    std::optional<int> channels;
    std::optional<QString> codec;
    std::optional<QString> container;
    std::optional<QString> mimeType;
    std::optional<QString> qualityLabel;
    std::optional<bool> lossless;
};

enum class SourceCandidateFailureCode {
    TemporarilyUnavailable,
    Unavailable,
    ProviderFailure,
    ResolutionFailed,
};

QString sourceCandidateFailureCodeName(SourceCandidateFailureCode code);

struct SourceCandidateFailure final {
    SourceCandidateFailureCode code = SourceCandidateFailureCode::TemporarilyUnavailable;
    bool retryable = true;
    std::optional<QString> detail;
};

class SourceCandidateProvenance final
{
public:
    SourceCandidateProvenance(SourceCandidateKey key,
                              SourceDiscoveryMethod discoveryMethod)
        : m_key(std::move(key))
        , m_discoveryMethod(discoveryMethod)
    {
    }

    const SourceCandidateKey &key() const noexcept { return m_key; }
    SourceDiscoveryMethod discoveryMethod() const noexcept { return m_discoveryMethod; }

private:
    SourceCandidateKey m_key;
    SourceDiscoveryMethod m_discoveryMethod;
};

class NormalizedSourceCandidate final
{
public:
    static SourceCandidateResult<NormalizedSourceCandidate> create(
        RecordingMbid requestedRecordingMbid,
        SourceProviderDescriptor provider,
        SourceCandidateId candidateId,
        SourceDiscoveryMethod discoveryMethod,
        SourceKind sourceKind,
        CandidateResolutionMode resolutionMode,
        SourceCandidateFacts facts = {},
        std::optional<SourceCandidateFailure> failure = std::nullopt);

    const RecordingMbid &requestedRecordingMbid() const noexcept
    {
        return m_requestedRecordingMbid;
    }

    const SourceProviderDescriptor &provider() const noexcept { return m_provider; }
    const SourceCandidateProvenance &provenance() const noexcept { return m_provenance; }
    SourceKind sourceKind() const noexcept { return m_sourceKind; }
    CandidateResolutionMode resolutionMode() const noexcept { return m_resolutionMode; }
    const SourceCandidateFacts &facts() const noexcept { return m_facts; }

    const std::optional<SourceCandidateFailure> &failure() const noexcept
    {
        return m_failure;
    }

private:
    NormalizedSourceCandidate(RecordingMbid requestedRecordingMbid,
                              SourceProviderDescriptor provider,
                              SourceCandidateProvenance provenance,
                              SourceKind sourceKind,
                              CandidateResolutionMode resolutionMode,
                              SourceCandidateFacts facts,
                              std::optional<SourceCandidateFailure> failure)
        : m_requestedRecordingMbid(std::move(requestedRecordingMbid))
        , m_provider(std::move(provider))
        , m_provenance(std::move(provenance))
        , m_sourceKind(sourceKind)
        , m_resolutionMode(resolutionMode)
        , m_facts(std::move(facts))
        , m_failure(std::move(failure))
    {
    }

    RecordingMbid m_requestedRecordingMbid;
    SourceProviderDescriptor m_provider;
    SourceCandidateProvenance m_provenance;
    SourceKind m_sourceKind;
    CandidateResolutionMode m_resolutionMode;
    SourceCandidateFacts m_facts;
    std::optional<SourceCandidateFailure> m_failure;
};

class SourceCandidateSet final
{
public:
    static SourceCandidateResult<SourceCandidateSet> create(
        RecordingMbid requestedRecordingMbid,
        QVector<NormalizedSourceCandidate> candidates);

    const RecordingMbid &requestedRecordingMbid() const noexcept
    {
        return m_requestedRecordingMbid;
    }

    const QVector<NormalizedSourceCandidate> &candidates() const noexcept
    {
        return m_candidates;
    }

private:
    SourceCandidateSet(RecordingMbid requestedRecordingMbid,
                       QVector<NormalizedSourceCandidate> candidates)
        : m_requestedRecordingMbid(std::move(requestedRecordingMbid))
        , m_candidates(std::move(candidates))
    {
    }

    RecordingMbid m_requestedRecordingMbid;
    QVector<NormalizedSourceCandidate> m_candidates;
};

} // namespace vinyl

Q_DECLARE_OPERATORS_FOR_FLAGS(vinyl::SourceProviderCapabilities)
