#include "TrackerConnectionService.h"

#include <QSet>

#include <algorithm>

namespace {

bool sameFact(const TrackerDeliveryFact &left, const TrackerDeliveryFact &right)
{
    return left.canonicalMediaId == right.canonicalMediaId
        && left.historyKind == right.historyKind
        && left.historyId == right.historyId
        && left.kind == right.kind
        && left.sourceRevision == right.sourceRevision
        && left.sourceEventId == right.sourceEventId
        && left.progress == right.progress
        && left.contentFingerprint == right.contentFingerprint
        && left.origin == right.origin
        && left.mediaDomain == right.mediaDomain;
}

bool sameMapping(const TrackerTitleMapping &left, const TrackerTitleMapping &right)
{
    return left.remote.providerId == right.remote.providerId
        && left.remote.remoteAccountId == right.remote.remoteAccountId
        && left.remote.remoteMediaId == right.remote.remoteMediaId
        && left.canonical.canonicalMediaId == right.canonical.canonicalMediaId
        && left.canonical.historyKind == right.canonical.historyKind
        && left.canonical.historyId == right.canonical.historyId
        && left.canonical.displayName == right.canonical.displayName
        && left.provenance == right.provenance
        && left.revision == right.revision;
}

const TrackerProviderDescriptor *descriptorFor(
    const QList<TrackerProviderDescriptor> &providers,
    TrackerProviderId providerId)
{
    const auto found = std::find_if(providers.cbegin(), providers.cend(),
        [providerId](const TrackerProviderDescriptor &provider) {
            return provider.providerId == providerId;
        });
    return found == providers.cend() ? nullptr : &*found;
}

TrackerProviderCapability requiredCapability(TrackerDeliveryFactKind kind)
{
    return kind == TrackerDeliveryFactKind::Progress
        ? TrackerProviderCapability::WriteProgress
        : TrackerProviderCapability::WriteCompletion;
}

bool sameRemoteBinding(const TrackerConnection &connection,
                       const TrackerDeliveryOperation &operation)
{
    return connection.state == TrackerConnectionState::Connected
        && connection.providerId == operation.providerId
        && connection.remoteAccountId == operation.remoteAccountId
        && connection.connectionGeneration == operation.connectionGeneration;
}

class UnavailableNormalizedTransport final : public TrackerNormalizedTransport
{
public:
    TrackerNormalizedTransportResult execute(
        const TrackerNormalizedTransportRequest &) const override
    {
        return {TrackerNormalizedTransportOutcome::Unavailable};
    }
};

const TrackerNormalizedTransport &unavailableNormalizedTransport()
{
    static const UnavailableNormalizedTransport transport;
    return transport;
}

} // namespace

TrackerConnectionService::TrackerConnectionService(
    const TrackerConnectionStore *connections,
    const TrackerMappingStore *mappings,
    const TrackerDeliveryStore *delivery,
    const TrackerDeliverySource *source,
    const QList<TrackerProviderDescriptor> &providers,
    const TrackerNormalizedTransport *transport)
    : m_connections(connections),
      m_mappings(mappings),
      m_delivery(delivery),
      m_source(source),
      m_providers(providers),
      m_transport(transport)
{}

bool TrackerConnectionService::healthy(QString *error) const
{
    if (m_connections && m_connections->healthy()
        && m_mappings && m_mappings->healthy()
        && m_delivery && m_delivery->healthy()
        && m_source && m_source->isReady()) {
        return true;
    }
    if (error)
        *error = QStringLiteral("Tracker connection service is unavailable.");
    return false;
}

std::optional<TrackerConnection> TrackerConnectionService::connection(
    TrackerProviderId providerId) const
{
    if (!m_connections || !m_connections->healthy()
        || trackerProviderKey(providerId).isEmpty()) {
        return std::nullopt;
    }
    return m_connections->connection(providerId);
}

QList<TrackerConnection> TrackerConnectionService::connections() const
{
    if (!m_connections || !m_connections->healthy())
        return {};
    return m_connections->connections();
}

const TrackerNormalizedTransport &TrackerConnectionService::normalizedTransport() const
{
    return m_transport ? *m_transport : unavailableNormalizedTransport();
}

TrackerCoverageSnapshot TrackerConnectionService::coverageForTitle(
    const QString &canonicalMediaId) const
{
    TrackerCoverageSnapshot result;
    if (canonicalMediaId.trimmed().isEmpty() || !healthy())
        return result;

    result.available = true;
    const QList<TrackerDeliveryFact> currentFacts = m_source->currentCommittedFacts();
    QSet<QString> currentProviders;
    for (const TrackerDeliveryOperation &operation : m_delivery->operations()) {
        if (operation.adoptedReceipt
            || operation.fact.origin != TrackerDeliveryOrigin::NativeLocal
            || operation.fact.canonicalMediaId != canonicalMediaId
            || operation.connectionGeneration == 0
            || operation.remoteAccountId.trimmed().isEmpty()
            || trackerProviderKey(operation.providerId).isEmpty()) {
            continue;
        }

        TrackerProviderReceiptStatus receipt;
        receipt.providerId = operation.providerId;
        receipt.factKind = operation.fact.kind;
        receipt.state = operation.state;
        receipt.reason = operation.reason;

        const TrackerProviderDescriptor *provider = descriptorFor(m_providers,
                                                                   operation.providerId);
        if (!provider || !provider->available
            || !provider->capabilities.testFlag(requiredCapability(operation.fact.kind))
            || !TrackerDeliveryStore::supportsProviderDelivery(
                operation.providerId, operation.fact.mediaDomain)
            || operation.mapping.remote.providerId != operation.providerId
            || operation.mapping.remote.remoteAccountId != operation.remoteAccountId
            || operation.mapping.remote.remoteMediaId.trimmed().isEmpty()
            || operation.mapping.canonical.canonicalMediaId != operation.fact.canonicalMediaId
            || operation.mapping.canonical.historyKind != operation.fact.historyKind
            || operation.mapping.canonical.historyId != operation.fact.historyId) {
            receipt.currentness = TrackerReceiptCurrentness::Unsupported;
            result.receiptStatuses.append(receipt);
            continue;
        }

        const auto currentConnection = m_connections->connection(operation.providerId);
        if (!currentConnection || !sameRemoteBinding(*currentConnection, operation)
            || !currentConnection->capabilities.testFlag(
                requiredCapability(operation.fact.kind))) {
            receipt.currentness = TrackerReceiptCurrentness::StaleConnection;
            result.receiptStatuses.append(receipt);
            continue;
        }

        const auto currentMapping = m_mappings->mapping(operation.mapping.remote);
        if (!currentMapping || !sameMapping(*currentMapping, operation.mapping)) {
            receipt.currentness = TrackerReceiptCurrentness::StaleMapping;
            result.receiptStatuses.append(receipt);
            continue;
        }

        const bool listedAsCurrent = std::any_of(currentFacts.cbegin(), currentFacts.cend(),
            [&operation](const TrackerDeliveryFact &fact) {
                return sameFact(fact, operation.fact);
            });
        if (!listedAsCurrent || !m_source->isDurablyCurrent(operation.fact)) {
            receipt.currentness = TrackerReceiptCurrentness::StaleSource;
            result.receiptStatuses.append(receipt);
            continue;
        }

        receipt.currentness = TrackerReceiptCurrentness::Current;
        receipt.contributesToCoverage = operation.state == TrackerDeliveryState::Succeeded
            && (operation.reason == TrackerDeliveryReason::None
                || operation.reason == TrackerDeliveryReason::ReadbackPresent);
        result.receiptStatuses.append(receipt);
        if (receipt.contributesToCoverage)
            currentProviders.insert(trackerProviderKey(operation.providerId));
    }

    result.providerCount = currentProviders.size();
    result.localOnly = result.providerCount == 0;
    return result;
}
