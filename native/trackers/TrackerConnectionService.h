#pragma once

#include "TrackerConnectionStore.h"
#include "TrackerDeliveryStore.h"
#include "TrackerMappingStore.h"

#include <QList>
#include <QString>

#include <optional>

enum class TrackerNormalizedTransportAction : quint8 {
    ReadHistory,
    ReadProgress,
    WriteProgress,
    WriteCompletion
};

enum class TrackerNormalizedTransportOutcome : quint8 {
    Completed,
    Unavailable,
    Unsupported,
    AuthenticationRequired,
    Retryable,
    RateLimited,
    Rejected,
    UnknownOutcome
};

// Provider-neutral request vocabulary. It contains canonical facts and an
// opaque mapped item key, never provider payloads or ratings/reviews intents.
struct TrackerNormalizedTransportRequest {
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    quint64 connectionGeneration = 0;
    TrackerNormalizedTransportAction action = TrackerNormalizedTransportAction::ReadProgress;
    QString canonicalMediaId;
    QString historyKind;
    QString historyId;
    QString remoteMediaId;
    TrackerMediaDomain mediaDomain = TrackerMediaDomain::Unknown;
    int progress = 0;
    bool completed = false;
};

struct TrackerNormalizedTransportResult {
    TrackerNormalizedTransportOutcome outcome = TrackerNormalizedTransportOutcome::Unavailable;
};

class TrackerNormalizedTransport
{
public:
    virtual ~TrackerNormalizedTransport() = default;
    virtual TrackerNormalizedTransportResult execute(
        const TrackerNormalizedTransportRequest &request) const = 0;
};

enum class TrackerReceiptCurrentness : quint8 {
    Current,
    StaleSource,
    StaleConnection,
    StaleMapping,
    Unsupported
};

struct TrackerProviderReceiptStatus {
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    TrackerDeliveryFactKind factKind = TrackerDeliveryFactKind::Progress;
    TrackerDeliveryState state = TrackerDeliveryState::Pending;
    TrackerDeliveryReason reason = TrackerDeliveryReason::None;
    TrackerReceiptCurrentness currentness = TrackerReceiptCurrentness::Unsupported;
    bool contributesToCoverage = false;
};

struct TrackerCoverageSnapshot {
    bool available = false;
    int providerCount = 0;
    bool localOnly = false;
    QList<TrackerProviderReceiptStatus> receiptStatuses;
};

// Native-only read seam for profile-scoped connection truth. Do not expose its
// account identities or raw TrackerConnection values to QML.
class TrackerConnectionService final
{
public:
    TrackerConnectionService(const TrackerConnectionStore *connections,
                             const TrackerMappingStore *mappings,
                             const TrackerDeliveryStore *delivery,
                             const TrackerDeliverySource *source,
                             const QList<TrackerProviderDescriptor> &providers,
                             const TrackerNormalizedTransport *transport = nullptr);

    bool healthy(QString *error = nullptr) const;
    std::optional<TrackerConnection> connection(TrackerProviderId providerId) const;
    QList<TrackerConnection> connections() const;
    TrackerCoverageSnapshot coverageForTitle(const QString &canonicalMediaId) const;
    const TrackerNormalizedTransport &normalizedTransport() const;

private:
    const TrackerConnectionStore *m_connections = nullptr;
    const TrackerMappingStore *m_mappings = nullptr;
    const TrackerDeliveryStore *m_delivery = nullptr;
    const TrackerDeliverySource *m_source = nullptr;
    QList<TrackerProviderDescriptor> m_providers;
    const TrackerNormalizedTransport *m_transport = nullptr;
};
