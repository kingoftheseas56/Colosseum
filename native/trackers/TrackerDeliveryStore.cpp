#include "TrackerDeliveryStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int kSchemaVersion = 2;
constexpr int kMaximumAttempts = 5;
constexpr qint64 kMaximumBackoffMs = 60 * 60 * 1000;
constexpr auto kFileName = "tracker-delivery.json";

bool setError(QString *out, const QString &message)
{
    if (out)
        *out = message;
    return false;
}

bool safeText(const QString &value, int maximum = 512)
{
    if (value.isEmpty() || value != value.trimmed() || value.size() > maximum
        || QDir::isAbsolutePath(value) || value.contains(QLatin1String(".."))) {
        return false;
    }
    return std::none_of(value.cbegin(), value.cend(), [](QChar c) {
        return c.category() == QChar::Other_Control;
    });
}

QString providerAccountKey(TrackerProviderId providerId, const QString &remoteAccountId)
{
    return trackerProviderKey(providerId) + QChar(0x1f) + remoteAccountId;
}

QString sourceFactKey(const TrackerDeliveryFact &fact)
{
    return fact.canonicalMediaId + QChar(0x1f) + fact.historyKind + QChar(0x1f)
        + fact.historyId + QChar(0x1f) + QString::number(static_cast<int>(fact.kind))
        + QChar(0x1f) + fact.sourceEventId + QChar(0x1f)
        + QString::number(static_cast<int>(fact.mediaDomain));
}

bool validFact(const TrackerDeliveryFact &fact)
{
    if (!safeText(fact.canonicalMediaId) || !safeText(fact.historyKind, 64)
        || !safeText(fact.historyId) || fact.sourceRevision == 0 || fact.progress < 0
        || !safeText(fact.contentFingerprint, 128)
        || (fact.origin != TrackerDeliveryOrigin::NativeLocal
            && fact.origin != TrackerDeliveryOrigin::TrackerImport
            && fact.origin != TrackerDeliveryOrigin::AccountSync
            && fact.origin != TrackerDeliveryOrigin::Unknown)) {
        return false;
    }
    if (fact.mediaDomain != TrackerMediaDomain::Unknown
        && fact.mediaDomain != TrackerMediaDomain::Anime
        && fact.mediaDomain != TrackerMediaDomain::Manga
        && fact.mediaDomain != TrackerMediaDomain::Movie
        && fact.mediaDomain != TrackerMediaDomain::Television) {
        return false;
    }
    if (fact.kind == TrackerDeliveryFactKind::Progress)
        return fact.sourceEventId.isEmpty();
    if (fact.kind == TrackerDeliveryFactKind::Completion)
        return safeText(fact.sourceEventId, 256);
    return false;
}

bool validMapping(const TrackerTitleMapping &mapping)
{
    return !trackerProviderKey(mapping.remote.providerId).isEmpty()
        && safeText(mapping.remote.remoteAccountId, 128)
        && safeText(mapping.remote.remoteMediaId)
        && safeText(mapping.canonical.canonicalMediaId)
        && safeText(mapping.canonical.historyKind, 64)
        && safeText(mapping.canonical.historyId)
        && safeText(mapping.canonical.displayName)
        && mapping.revision > 0;
}

bool sameRemote(const TrackerRemoteMediaKey &left, const TrackerRemoteMediaKey &right)
{
    return left.providerId == right.providerId
        && left.remoteAccountId == right.remoteAccountId
        && left.remoteMediaId == right.remoteMediaId;
}

bool sameMapping(const TrackerTitleMapping &left, const TrackerTitleMapping &right)
{
    return sameRemote(left.remote, right.remote)
        && left.canonical.canonicalMediaId == right.canonical.canonicalMediaId
        && left.canonical.historyKind == right.canonical.historyKind
        && left.canonical.historyId == right.canonical.historyId
        && left.canonical.displayName == right.canonical.displayName
        && left.provenance == right.provenance && left.revision == right.revision;
}

bool sameFact(const TrackerDeliveryFact &left, const TrackerDeliveryFact &right)
{
    return left.canonicalMediaId == right.canonicalMediaId
        && left.historyKind == right.historyKind && left.historyId == right.historyId
        && left.kind == right.kind && left.sourceRevision == right.sourceRevision
        && left.sourceEventId == right.sourceEventId && left.progress == right.progress
        && left.contentFingerprint == right.contentFingerprint && left.origin == right.origin
        && left.mediaDomain == right.mediaDomain;
}

QString factBindingKey(const TrackerDeliveryFact &fact)
{
    const QString material = sourceFactKey(fact) + QChar(0x1f)
        + QString::number(fact.sourceRevision) + QChar(0x1f)
        + QString::number(fact.progress) + QChar(0x1f)
        + fact.contentFingerprint + QChar(0x1f)
        + QString::number(static_cast<int>(fact.origin)) + QChar(0x1f)
        + QString::number(static_cast<int>(fact.mediaDomain));
    return QString::fromLatin1(QCryptographicHash::hash(material.toUtf8(),
                                                         QCryptographicHash::Sha256).toHex());
}

QString factKindKey(TrackerDeliveryFactKind kind)
{
    return kind == TrackerDeliveryFactKind::Progress ? QStringLiteral("progress")
                                                      : QStringLiteral("completion");
}

std::optional<TrackerDeliveryFactKind> factKindFromKey(const QString &key)
{
    if (key == QLatin1String("progress"))
        return TrackerDeliveryFactKind::Progress;
    if (key == QLatin1String("completion"))
        return TrackerDeliveryFactKind::Completion;
    return std::nullopt;
}

QString originKey(TrackerDeliveryOrigin origin)
{
    switch (origin) {
    case TrackerDeliveryOrigin::Unknown: return QStringLiteral("unknown");
    case TrackerDeliveryOrigin::NativeLocal: return QStringLiteral("native_local");
    case TrackerDeliveryOrigin::TrackerImport: return QStringLiteral("tracker_import");
    case TrackerDeliveryOrigin::AccountSync: return QStringLiteral("account_sync");
    }
    return {};
}

std::optional<TrackerDeliveryOrigin> originFromKey(const QString &key)
{
    if (key == QLatin1String("unknown"))
        return TrackerDeliveryOrigin::Unknown;
    if (key == QLatin1String("native_local"))
        return TrackerDeliveryOrigin::NativeLocal;
    if (key == QLatin1String("tracker_import"))
        return TrackerDeliveryOrigin::TrackerImport;
    if (key == QLatin1String("account_sync"))
        return TrackerDeliveryOrigin::AccountSync;
    return std::nullopt;
}

QString mediaDomainKey(TrackerMediaDomain domain)
{
    switch (domain) {
    case TrackerMediaDomain::Unknown: return QStringLiteral("unknown");
    case TrackerMediaDomain::Anime: return QStringLiteral("anime");
    case TrackerMediaDomain::Manga: return QStringLiteral("manga");
    case TrackerMediaDomain::Movie: return QStringLiteral("movie");
    case TrackerMediaDomain::Television: return QStringLiteral("television");
    }
    return {};
}

std::optional<TrackerMediaDomain> mediaDomainFromKey(const QString &key)
{
    if (key == QLatin1String("unknown")) return TrackerMediaDomain::Unknown;
    if (key == QLatin1String("anime")) return TrackerMediaDomain::Anime;
    if (key == QLatin1String("manga")) return TrackerMediaDomain::Manga;
    if (key == QLatin1String("movie")) return TrackerMediaDomain::Movie;
    if (key == QLatin1String("television")) return TrackerMediaDomain::Television;
    return std::nullopt;
}

bool providerSupportsDelivery(TrackerProviderId providerId, TrackerMediaDomain domain)
{
    // Arc 35's current provider gate enables only SIMKL's documented TV, movie,
    // and anime sync domains. Other providers remain unavailable until their
    // security/terms gates are explicitly cleared.
    return providerId == TrackerProviderId::Simkl
        && (domain == TrackerMediaDomain::Anime
            || domain == TrackerMediaDomain::Movie
            || domain == TrackerMediaDomain::Television);
}

QString remoteSnapshotBinding(const TrackerRemoteDeliverySnapshot &snapshot)
{
    QStringList rows;
    rows.reserve(snapshot.items.size());
    for (const TrackerRemoteDeliveryState &item : snapshot.items) {
        rows.push_back(item.remoteMediaId + QChar(0x1f)
            + QString::number(static_cast<int>(item.factKind)) + QChar(0x1f)
            + item.sourceEventId + QChar(0x1f)
            + (item.present ? QStringLiteral("present") : QStringLiteral("absent"))
            + QChar(0x1f) + (item.exactlyMatchesIntendedState
                                  ? QStringLiteral("exact") : QStringLiteral("different"))
            + QChar(0x1f) + item.stateFingerprint + QChar(0x1f) + item.safeSummary);
    }
    std::sort(rows.begin(), rows.end());
    const QString material = trackerProviderKey(snapshot.providerId) + QChar(0x1f)
        + snapshot.remoteAccountId + QChar(0x1f)
        + QString::number(snapshot.connectionGeneration) + QChar(0x1f)
        + snapshot.snapshotId + QChar(0x1f) + QString::number(snapshot.observedAtMs)
        + QChar(0x1f) + (snapshot.completeForMappedItems ? QStringLiteral("complete")
                                                         : QStringLiteral("partial"))
        + QChar(0x1f) + rows.join(QChar(0x1e));
    return QString::fromLatin1(QCryptographicHash::hash(
        material.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool validRemoteSnapshot(const TrackerRemoteDeliverySnapshot &snapshot)
{
    if (!trackerProviderIdFromKey(trackerProviderKey(snapshot.providerId))
        || !safeText(snapshot.remoteAccountId, 128)
        || snapshot.connectionGeneration == 0 || !safeText(snapshot.snapshotId, 256)
        || snapshot.observedAtMs <= 0 || !snapshot.completeForMappedItems) {
        return false;
    }
    QSet<QString> seen;
    for (const TrackerRemoteDeliveryState &item : snapshot.items) {
        const bool validKind = item.factKind == TrackerDeliveryFactKind::Progress
            || item.factKind == TrackerDeliveryFactKind::Completion;
        const bool validFactIdentity = validKind
            && (item.factKind == TrackerDeliveryFactKind::Progress
            ? item.sourceEventId.isEmpty()
            : safeText(item.sourceEventId, 128));
        const QString identity = item.remoteMediaId + QChar(0x1f)
            + QString::number(static_cast<int>(item.factKind)) + QChar(0x1f)
            + item.sourceEventId;
        if (!safeText(item.remoteMediaId) || !validFactIdentity || seen.contains(identity)
            || (item.present && (!safeText(item.stateFingerprint, 128)
                                 || !safeText(item.safeSummary, 256)))
            || (!item.present && (item.exactlyMatchesIntendedState
                                  || item.stateFingerprint != QLatin1String("absent")))) {
            return false;
        }
        seen.insert(identity);
    }
    return true;
}

bool sameRemoteSnapshot(const TrackerRemoteDeliverySnapshot &left,
                        const TrackerRemoteDeliverySnapshot &right)
{
    return validRemoteSnapshot(left) && validRemoteSnapshot(right)
        && remoteSnapshotBinding(left) == remoteSnapshotBinding(right);
}

QString stateKey(TrackerDeliveryState state)
{
    switch (state) {
    case TrackerDeliveryState::Pending: return QStringLiteral("pending");
    case TrackerDeliveryState::Delivering: return QStringLiteral("delivering");
    case TrackerDeliveryState::Retrying: return QStringLiteral("retrying");
    case TrackerDeliveryState::Succeeded: return QStringLiteral("succeeded");
    case TrackerDeliveryState::NeedsAttention: return QStringLiteral("needs_attention");
    case TrackerDeliveryState::FailedTerminal: return QStringLiteral("failed_terminal");
    case TrackerDeliveryState::UnknownOutcome: return QStringLiteral("unknown_outcome");
    }
    return {};
}

std::optional<TrackerDeliveryState> stateFromKey(const QString &key)
{
    if (key == QLatin1String("pending")) return TrackerDeliveryState::Pending;
    if (key == QLatin1String("delivering")) return TrackerDeliveryState::Delivering;
    if (key == QLatin1String("retrying")) return TrackerDeliveryState::Retrying;
    if (key == QLatin1String("succeeded")) return TrackerDeliveryState::Succeeded;
    if (key == QLatin1String("needs_attention")) return TrackerDeliveryState::NeedsAttention;
    if (key == QLatin1String("failed_terminal")) return TrackerDeliveryState::FailedTerminal;
    if (key == QLatin1String("unknown_outcome")) return TrackerDeliveryState::UnknownOutcome;
    return std::nullopt;
}

QString reasonKey(TrackerDeliveryReason reason)
{
    switch (reason) {
    case TrackerDeliveryReason::None: return QStringLiteral("none");
    case TrackerDeliveryReason::ProviderRetryable: return QStringLiteral("provider_retryable");
    case TrackerDeliveryReason::ProviderRateLimited: return QStringLiteral("provider_rate_limited");
    case TrackerDeliveryReason::AuthenticationRequired: return QStringLiteral("authentication_required");
    case TrackerDeliveryReason::UnsupportedAction: return QStringLiteral("unsupported_action");
    case TrackerDeliveryReason::TerminalProviderRefusal: return QStringLiteral("terminal_provider_refusal");
    case TrackerDeliveryReason::AcknowledgementLost: return QStringLiteral("acknowledgement_lost");
    case TrackerDeliveryReason::ReadbackPresent: return QStringLiteral("readback_present");
    case TrackerDeliveryReason::ReadbackAbsent: return QStringLiteral("readback_absent");
    case TrackerDeliveryReason::ReadbackDifferent: return QStringLiteral("readback_different");
    case TrackerDeliveryReason::ReadbackUncertain: return QStringLiteral("readback_uncertain");
    case TrackerDeliveryReason::StaleLocalFact: return QStringLiteral("stale_local_fact");
    case TrackerDeliveryReason::MappingChanged: return QStringLiteral("mapping_changed");
    case TrackerDeliveryReason::RetryLimitReached: return QStringLiteral("retry_limit_reached");
    case TrackerDeliveryReason::DestinationReviewRequired:
        return QStringLiteral("destination_review_required");
    }
    return {};
}

std::optional<TrackerDeliveryReason> reasonFromKey(const QString &key)
{
    if (key == QLatin1String("none")) return TrackerDeliveryReason::None;
    if (key == QLatin1String("provider_retryable")) return TrackerDeliveryReason::ProviderRetryable;
    if (key == QLatin1String("provider_rate_limited")) return TrackerDeliveryReason::ProviderRateLimited;
    if (key == QLatin1String("authentication_required")) return TrackerDeliveryReason::AuthenticationRequired;
    if (key == QLatin1String("unsupported_action")) return TrackerDeliveryReason::UnsupportedAction;
    if (key == QLatin1String("terminal_provider_refusal")) return TrackerDeliveryReason::TerminalProviderRefusal;
    if (key == QLatin1String("acknowledgement_lost")) return TrackerDeliveryReason::AcknowledgementLost;
    if (key == QLatin1String("readback_present")) return TrackerDeliveryReason::ReadbackPresent;
    if (key == QLatin1String("readback_absent")) return TrackerDeliveryReason::ReadbackAbsent;
    if (key == QLatin1String("readback_different")) return TrackerDeliveryReason::ReadbackDifferent;
    if (key == QLatin1String("readback_uncertain")) return TrackerDeliveryReason::ReadbackUncertain;
    if (key == QLatin1String("stale_local_fact")) return TrackerDeliveryReason::StaleLocalFact;
    if (key == QLatin1String("mapping_changed")) return TrackerDeliveryReason::MappingChanged;
    if (key == QLatin1String("retry_limit_reached")) return TrackerDeliveryReason::RetryLimitReached;
    if (key == QLatin1String("destination_review_required"))
        return TrackerDeliveryReason::DestinationReviewRequired;
    return std::nullopt;
}

QString ineligibleReasonKey(TrackerExportIneligibleReason reason)
{
    switch (reason) {
    case TrackerExportIneligibleReason::None: return QStringLiteral("none");
    case TrackerExportIneligibleReason::SourceChanged: return QStringLiteral("source_changed");
    case TrackerExportIneligibleReason::NonNativeOrigin: return QStringLiteral("non_native_origin");
    case TrackerExportIneligibleReason::MissingMapping: return QStringLiteral("missing_mapping");
    case TrackerExportIneligibleReason::AmbiguousMapping: return QStringLiteral("ambiguous_mapping");
    case TrackerExportIneligibleReason::Unsupported: return QStringLiteral("unsupported");
    case TrackerExportIneligibleReason::RemoteAlreadyCurrent: return QStringLiteral("remote_already_current");
    }
    return {};
}

QString operationIdFor(TrackerProviderId providerId,
                       const QString &remoteAccountId,
                       quint64 generation,
                       const TrackerTitleMapping &mapping,
                       const TrackerDeliveryFact &fact)
{
    const QString material = providerAccountKey(providerId, remoteAccountId) + QChar(0x1f)
        + QString::number(generation) + QChar(0x1f) + mapping.remote.remoteMediaId
        + QChar(0x1f) + QString::number(mapping.revision) + QChar(0x1f)
        + fact.canonicalMediaId + QChar(0x1f) + fact.historyKind + QChar(0x1f)
        + fact.historyId + QChar(0x1f) + factKindKey(fact.kind) + QChar(0x1f)
        + QString::number(fact.sourceRevision) + QChar(0x1f) + fact.sourceEventId
        + QChar(0x1f) + QString::number(fact.progress) + QChar(0x1f)
        + fact.contentFingerprint + QChar(0x1f)
        + QString::number(static_cast<int>(fact.mediaDomain));
    return QStringLiteral("delivery-") + QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString exportItemId(TrackerProviderId providerId,
                     const QString &remoteAccountId,
                     quint64 generation,
                     const TrackerDeliveryFact &fact,
                     const std::optional<TrackerTitleMapping> &mapping,
                     TrackerExportIneligibleReason reason,
                     const QString &remoteSnapshotId,
                     const QString &remoteStateFingerprint)
{
    const QString material = providerAccountKey(providerId, remoteAccountId) + QChar(0x1f)
        + QString::number(generation) + QChar(0x1f) + sourceFactKey(fact)
        + QChar(0x1f) + QString::number(fact.sourceRevision) + QChar(0x1f)
        + QString::number(fact.progress) + QChar(0x1f) + fact.contentFingerprint
        + QChar(0x1f) + ineligibleReasonKey(reason) + QChar(0x1f)
        + (mapping ? mapping->remote.remoteMediaId : QString()) + QChar(0x1f)
        + (mapping ? QString::number(mapping->revision) : QStringLiteral("no_mapping"))
        + QChar(0x1f) + remoteSnapshotId + QChar(0x1f) + remoteStateFingerprint;
    return QStringLiteral("export-item-") + QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString exportPreviewId(TrackerProviderId providerId,
                        const QString &remoteAccountId,
                        quint64 generation,
                        const QList<TrackerExportPreviewItem> &items,
                        const QString &remoteSnapshotId,
                        const QString &remoteSnapshotFingerprint)
{
    QStringList ids;
    ids.reserve(items.size());
    for (const TrackerExportPreviewItem &item : items)
        ids.push_back(item.itemId);
    std::sort(ids.begin(), ids.end());
    const QString material = providerAccountKey(providerId, remoteAccountId) + QChar(0x1f)
        + QString::number(generation) + QChar(0x1f) + ids.join(QChar(0x1f))
        + QChar(0x1f) + remoteSnapshotId + QChar(0x1f) + remoteSnapshotFingerprint;
    return QStringLiteral("export-preview-") + QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool currentConnection(const TrackerConnectionStore *connections,
                       TrackerProviderId providerId,
                       const QString &remoteAccountId,
                       quint64 generation,
                       TrackerProviderCapability capability,
                       TrackerMediaDomain domain)
{
    if (!connections || !connections->healthy() || !providerSupportsDelivery(providerId, domain))
        return false;
    const auto connection = connections->connection(providerId);
    return connection && connection->state == TrackerConnectionState::Connected
        && connection->remoteAccountId == remoteAccountId
        && connection->connectionGeneration == generation
        && connection->capabilities.testFlag(capability);
}

TrackerProviderCapability requiredCapability(TrackerDeliveryFactKind kind)
{
    return kind == TrackerDeliveryFactKind::Progress
        ? TrackerProviderCapability::WriteProgress
        : TrackerProviderCapability::WriteCompletion;
}

std::optional<TrackerTitleMapping> uniqueMapping(const TrackerMappingStore *mappings,
                                                 TrackerProviderId providerId,
                                                 const QString &remoteAccountId,
                                                 const TrackerDeliveryFact &fact,
                                                 bool *ambiguous = nullptr)
{
    if (ambiguous)
        *ambiguous = false;
    if (!mappings || !mappings->healthy())
        return std::nullopt;
    std::optional<TrackerTitleMapping> found;
    for (const TrackerTitleMapping &mapping : mappings->mappings()) {
        if (mapping.remote.providerId != providerId
            || mapping.remote.remoteAccountId != remoteAccountId
            || mapping.canonical.canonicalMediaId != fact.canonicalMediaId
            || mapping.canonical.historyKind != fact.historyKind
            || mapping.canonical.historyId != fact.historyId) {
            continue;
        }
        if (found) {
            if (ambiguous)
                *ambiguous = true;
            return std::nullopt;
        }
        found = mapping;
    }
    return found;
}

QJsonObject factToJson(const TrackerDeliveryFact &fact)
{
    return {{QStringLiteral("canonicalMediaId"), fact.canonicalMediaId},
            {QStringLiteral("historyKind"), fact.historyKind},
            {QStringLiteral("historyId"), fact.historyId},
            {QStringLiteral("kind"), factKindKey(fact.kind)},
            {QStringLiteral("sourceRevision"), QString::number(fact.sourceRevision)},
            {QStringLiteral("sourceEventId"), fact.sourceEventId},
            {QStringLiteral("progress"), fact.progress},
            {QStringLiteral("contentFingerprint"), fact.contentFingerprint},
            {QStringLiteral("origin"), originKey(fact.origin)},
            {QStringLiteral("mediaDomain"), mediaDomainKey(fact.mediaDomain)}};
}

std::optional<TrackerDeliveryFact> factFromJson(const QJsonObject &object)
{
    const auto kind = factKindFromKey(object.value(QStringLiteral("kind")).toString());
    const auto origin = originFromKey(object.value(QStringLiteral("origin")).toString());
    const auto mediaDomain = mediaDomainFromKey(object.value(QStringLiteral("mediaDomain")).toString());
    bool revisionOk = false;
    const quint64 revision = object.value(QStringLiteral("sourceRevision")).toString()
                                 .toULongLong(&revisionOk);
    const QJsonValue progressValue = object.value(QStringLiteral("progress"));
    const double rawProgress = progressValue.toDouble();
    if (!kind || !origin || !mediaDomain || !revisionOk || !progressValue.isDouble()
        || !std::isfinite(rawProgress) || std::floor(rawProgress) != rawProgress
        || rawProgress < 0 || rawProgress > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    TrackerDeliveryFact fact{object.value(QStringLiteral("canonicalMediaId")).toString(),
                             object.value(QStringLiteral("historyKind")).toString(),
                             object.value(QStringLiteral("historyId")).toString(),
                             *kind,
                             revision,
                             object.value(QStringLiteral("sourceEventId")).toString(),
                             static_cast<int>(rawProgress),
                             object.value(QStringLiteral("contentFingerprint")).toString(),
                             *origin,
                             *mediaDomain};
    return validFact(fact) ? std::optional<TrackerDeliveryFact>(fact) : std::nullopt;
}

QJsonObject mappingToJson(const TrackerTitleMapping &mapping)
{
    return {{QStringLiteral("providerId"), trackerProviderKey(mapping.remote.providerId)},
            {QStringLiteral("remoteAccountId"), mapping.remote.remoteAccountId},
            {QStringLiteral("remoteMediaId"), mapping.remote.remoteMediaId},
            {QStringLiteral("canonicalMediaId"), mapping.canonical.canonicalMediaId},
            {QStringLiteral("historyKind"), mapping.canonical.historyKind},
            {QStringLiteral("historyId"), mapping.canonical.historyId},
            {QStringLiteral("displayName"), mapping.canonical.displayName},
            {QStringLiteral("provenance"), mapping.provenance == TrackerMappingProvenance::ExactProviderIdentity
                 ? QStringLiteral("exact_provider_identity") : QStringLiteral("user_confirmed")},
            {QStringLiteral("revision"), QString::number(mapping.revision)}};
}

std::optional<TrackerTitleMapping> mappingFromJson(const QJsonObject &object)
{
    const auto provider = trackerProviderIdFromKey(object.value(QStringLiteral("providerId")).toString());
    const QString provenanceKeyValue = object.value(QStringLiteral("provenance")).toString();
    std::optional<TrackerMappingProvenance> provenance;
    if (provenanceKeyValue == QLatin1String("exact_provider_identity"))
        provenance = TrackerMappingProvenance::ExactProviderIdentity;
    else if (provenanceKeyValue == QLatin1String("user_confirmed"))
        provenance = TrackerMappingProvenance::UserConfirmed;
    bool revisionOk = false;
    const quint64 revision = object.value(QStringLiteral("revision")).toString().toULongLong(&revisionOk);
    if (!provider || !provenance || !revisionOk)
        return std::nullopt;
    TrackerTitleMapping mapping{{*provider,
                                 object.value(QStringLiteral("remoteAccountId")).toString(),
                                 object.value(QStringLiteral("remoteMediaId")).toString()},
                                {object.value(QStringLiteral("canonicalMediaId")).toString(),
                                 object.value(QStringLiteral("historyKind")).toString(),
                                 object.value(QStringLiteral("historyId")).toString(),
                                 object.value(QStringLiteral("displayName")).toString()},
                                *provenance,
                                revision};
    return validMapping(mapping) ? std::optional<TrackerTitleMapping>(mapping) : std::nullopt;
}

QJsonObject operationToJson(const TrackerDeliveryOperation &operation)
{
    return {{QStringLiteral("operationId"), operation.operationId},
            {QStringLiteral("providerId"), trackerProviderKey(operation.providerId)},
            {QStringLiteral("remoteAccountId"), operation.remoteAccountId},
            {QStringLiteral("connectionGeneration"), QString::number(operation.connectionGeneration)},
            {QStringLiteral("mapping"), mappingToJson(operation.mapping)},
            {QStringLiteral("fact"), factToJson(operation.fact)},
            {QStringLiteral("state"), stateKey(operation.state)},
            {QStringLiteral("attemptCount"), operation.attemptCount},
            {QStringLiteral("nextAttemptAtMs"), QString::number(operation.nextAttemptAtMs)},
            {QStringLiteral("reason"), reasonKey(operation.reason)},
            {QStringLiteral("reviewedRemoteSnapshotId"), operation.reviewedRemoteSnapshotId},
            {QStringLiteral("reviewedRemoteStateFingerprint"), operation.reviewedRemoteStateFingerprint},
            {QStringLiteral("reviewedRemoteStatePresent"), operation.reviewedRemoteStatePresent},
            {QStringLiteral("adoptedReceipt"), operation.adoptedReceipt}};
}

std::optional<TrackerDeliveryOperation> operationFromJson(const QJsonObject &object)
{
    const auto provider = trackerProviderIdFromKey(object.value(QStringLiteral("providerId")).toString());
    const auto state = stateFromKey(object.value(QStringLiteral("state")).toString());
    const auto reason = reasonFromKey(object.value(QStringLiteral("reason")).toString());
    const auto mapping = mappingFromJson(object.value(QStringLiteral("mapping")).toObject());
    const auto fact = factFromJson(object.value(QStringLiteral("fact")).toObject());
    bool generationOk = false;
    const quint64 generation = object.value(QStringLiteral("connectionGeneration")).toString()
                                  .toULongLong(&generationOk);
    bool nextAtOk = false;
    const qint64 nextAt = object.value(QStringLiteral("nextAttemptAtMs")).toString()
                              .toLongLong(&nextAtOk);
    const QJsonValue attemptsValue = object.value(QStringLiteral("attemptCount"));
    const QString reviewedSnapshotId = object.value(QStringLiteral("reviewedRemoteSnapshotId")).toString();
    const QString reviewedStateFingerprint = object.value(
        QStringLiteral("reviewedRemoteStateFingerprint")).toString();
    const QJsonValue reviewedStatePresentValue = object.value(QStringLiteral("reviewedRemoteStatePresent"));
    const QJsonValue adoptedReceiptValue = object.value(QStringLiteral("adoptedReceipt"));
    const double rawAttempts = attemptsValue.toDouble();
    if (!provider || !state || !reason || !mapping || !fact || !generationOk || generation == 0
        || !nextAtOk || !attemptsValue.isDouble() || !reviewedStatePresentValue.isBool()
        || (!adoptedReceiptValue.isUndefined() && !adoptedReceiptValue.isBool())
        || !std::isfinite(rawAttempts)
        || std::floor(rawAttempts) != rawAttempts || rawAttempts < 0
        || rawAttempts > kMaximumAttempts || mapping->remote.providerId != *provider
        || mapping->remote.remoteAccountId != object.value(QStringLiteral("remoteAccountId")).toString()
        || mapping->canonical.canonicalMediaId != fact->canonicalMediaId
        || mapping->canonical.historyKind != fact->historyKind
        || mapping->canonical.historyId != fact->historyId
        || fact->origin != TrackerDeliveryOrigin::NativeLocal
        || (!reviewedSnapshotId.isEmpty() && (!safeText(reviewedSnapshotId, 256)
            || !safeText(reviewedStateFingerprint, 128)))
        || (reviewedSnapshotId.isEmpty() && !reviewedStateFingerprint.isEmpty())) {
        return std::nullopt;
    }
    TrackerDeliveryOperation operation{object.value(QStringLiteral("operationId")).toString(),
                                       *provider,
                                       object.value(QStringLiteral("remoteAccountId")).toString(),
                                       generation,
                                       *mapping,
                                       *fact,
                                       *state,
                                       static_cast<int>(rawAttempts),
                                       nextAt,
                                       *reason,
                                       reviewedSnapshotId,
                                       reviewedStateFingerprint,
                                       reviewedStatePresentValue.toBool()};
    operation.adoptedReceipt = adoptedReceiptValue.toBool();
    if (!safeText(operation.remoteAccountId, 128) || !safeText(operation.operationId, 128)
        || operation.attemptCount > kMaximumAttempts
        || operation.operationId != operationIdFor(*provider, operation.remoteAccountId,
                                                    generation, *mapping, *fact)) {
        return std::nullopt;
    }
    return operation;
}

QJsonObject preferenceToJson(const TrackerDeliveryProviderPreference &preference)
{
    QJsonArray reviewedFacts;
    for (auto it = preference.reviewedFactBindings.cbegin();
         it != preference.reviewedFactBindings.cend(); ++it) {
        reviewedFacts.push_back(QJsonObject{{QStringLiteral("identity"),
                                            QString::fromLatin1(it.key().toUtf8().toBase64(
                                                QByteArray::Base64UrlEncoding
                                                | QByteArray::OmitTrailingEquals))},
                                            {QStringLiteral("binding"), it.value()}});
    }
    return {{QStringLiteral("providerId"), trackerProviderKey(preference.providerId)},
            {QStringLiteral("remoteAccountId"), preference.remoteAccountId},
            {QStringLiteral("firstExportConfirmedAtMs"), QString::number(preference.firstExportConfirmedAtMs)},
            {QStringLiteral("sendEnabled"), preference.sendEnabled},
            {QStringLiteral("reviewedFacts"), reviewedFacts}};
}

std::optional<TrackerDeliveryProviderPreference> preferenceFromJson(const QJsonObject &object)
{
    const auto provider = trackerProviderIdFromKey(object.value(QStringLiteral("providerId")).toString());
    bool confirmedAtOk = false;
    const qint64 confirmedAt = object.value(QStringLiteral("firstExportConfirmedAtMs")).toString()
                                  .toLongLong(&confirmedAtOk);
    const QJsonValue enabledValue = object.value(QStringLiteral("sendEnabled"));
    const QJsonValue reviewedFactsValue = object.value(QStringLiteral("reviewedFacts"));
    const QString remoteAccountId = object.value(QStringLiteral("remoteAccountId")).toString();
    if (!provider || !confirmedAtOk || confirmedAt < 0 || !enabledValue.isBool()
        || !safeText(remoteAccountId, 128) || !reviewedFactsValue.isArray()
        || (confirmedAt == 0
            && (enabledValue.toBool() || !reviewedFactsValue.toArray().isEmpty()))) {
        return std::nullopt;
    }
    TrackerDeliveryProviderPreference preference{*provider, remoteAccountId, confirmedAt,
                                                 enabledValue.toBool(), {}};
    for (const QJsonValue &value : reviewedFactsValue.toArray()) {
        if (!value.isObject())
            return std::nullopt;
        const QJsonObject reviewed = value.toObject();
        const QByteArray encodedIdentity = reviewed.value(QStringLiteral("identity")).toString().toLatin1();
        const QByteArray identityBytes = QByteArray::fromBase64(
            encodedIdentity, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
        const QString identity = QString::fromUtf8(identityBytes);
        const QString binding = reviewed.value(QStringLiteral("binding")).toString();
        const QByteArray canonicalIdentity = identity.toUtf8().toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        if (identity.isEmpty() || identityBytes.isEmpty() || encodedIdentity != canonicalIdentity
            || binding.size() != 64
            || !std::all_of(binding.cbegin(), binding.cend(), [](QChar c) { return c.isDigit()
                    || (c >= QLatin1Char('a') && c <= QLatin1Char('f')); })
            || preference.reviewedFactBindings.contains(identity)) {
            return std::nullopt;
        }
        preference.reviewedFactBindings.insert(identity, binding);
    }
    return preference;
}

bool connectionMatches(const TrackerConnectionStore *connections,
                       const TrackerDeliveryOperation &operation)
{
    if (!connections || !connections->healthy()
        || !providerSupportsDelivery(operation.providerId, operation.fact.mediaDomain))
        return false;
    const auto connection = connections->connection(operation.providerId);
    return connection && connection->state == TrackerConnectionState::Connected
        && connection->remoteAccountId == operation.remoteAccountId
        && connection->connectionGeneration == operation.connectionGeneration
        && connection->capabilities.testFlag(requiredCapability(operation.fact.kind));
}

qint64 retryDelayMs(const QString &operationId, int attemptCount)
{
    const int shift = std::clamp(attemptCount - 1, 0, 20);
    const qint64 base = std::min<qint64>(kMaximumBackoffMs, 1000LL << shift);
    const QByteArray digest = QCryptographicHash::hash(
        (operationId + QChar(0x1f) + QString::number(attemptCount)).toUtf8(),
        QCryptographicHash::Sha256);
    const auto byte = static_cast<unsigned char>(digest.at(0));
    const qint64 jitter = (base / 5) * byte / 255;
    return std::min(kMaximumBackoffMs, base + jitter);
}

} // namespace

bool TrackerDeliveryStore::supportsProviderDelivery(TrackerProviderId providerId,
                                                    TrackerMediaDomain domain)
{
    return providerSupportsDelivery(providerId, domain);
}

TrackerDeliveryStore::TrackerDeliveryStore(const ProfilePaths &profile,
                                           const TrackerMappingStore *mappings,
                                           const TrackerConnectionStore *connections)
    : m_profile(profile), m_mappings(mappings), m_connections(connections), m_path(storagePath(profile))
{
    if (!m_mappings || !m_mappings->healthy() || !m_connections || !m_connections->healthy()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker delivery needs healthy profile mapping and connection stores.");
    } else if (m_path.isEmpty()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker delivery is unavailable for this profile.");
    } else {
        load();
    }
}

QString TrackerDeliveryStore::storagePath(const ProfilePaths &profile)
{
    if (profile.kind() == ProfilePaths::Kind::Sealed
        || profile.kind() == ProfilePaths::Kind::LegacyLocal || profile.profileRoot().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(profile.profileRoot() + QLatin1Char('/') + QLatin1String(kFileName));
}

bool TrackerDeliveryStore::healthy(QString *out) const
{
    if (!m_healthy && out)
        *out = m_error;
    return m_healthy;
}

std::optional<TrackerExportPreview> TrackerDeliveryStore::createExportPreview(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    quint64 connectionGeneration,
    const QList<TrackerDeliveryFact> &facts,
    const TrackerRemoteDeliverySnapshot &remoteSnapshot,
    const TrackerDeliverySource *source,
    QString *out)
{
    if (!healthy(out) || !source || !source->isReady()
        || !safeText(remoteAccountId, 128) || connectionGeneration == 0
        || !trackerProviderIdFromKey(trackerProviderKey(providerId))
        || !m_mappings->healthy()
        || !m_connections->connection(providerId)
        || m_connections->connection(providerId)->remoteAccountId != remoteAccountId
        || m_connections->connection(providerId)->connectionGeneration != connectionGeneration) {
        setError(out, QStringLiteral("Tracker export preview is invalid or belongs to a stale connection."));
        return std::nullopt;
    }
    if (!validRemoteSnapshot(remoteSnapshot)
        || remoteSnapshot.providerId != providerId
        || remoteSnapshot.remoteAccountId != remoteAccountId
        || remoteSnapshot.connectionGeneration != connectionGeneration) {
        setError(out, QStringLiteral("Tracker export needs a complete current remote-state snapshot."));
        return std::nullopt;
    }

    TrackerExportPreview preview;
    preview.providerId = providerId;
    preview.remoteAccountId = remoteAccountId;
    preview.connectionGeneration = connectionGeneration;
    preview.remoteSnapshot = remoteSnapshot;
    const QString snapshotBinding = remoteSnapshotBinding(remoteSnapshot);
    QHash<QString, TrackerRemoteDeliveryState> remoteStates;
    for (const TrackerRemoteDeliveryState &remoteState : remoteSnapshot.items)
        remoteStates.insert(remoteState.remoteMediaId + QChar(0x1f)
                                 + QString::number(static_cast<int>(remoteState.factKind))
                                 + QChar(0x1f) + remoteState.sourceEventId,
                             remoteState);
    QSet<QString> expectedRemoteFactKeys;
    QHash<QString, TrackerDeliveryFact> currentFacts;
    for (const TrackerDeliveryFact &current : source->currentCommittedFacts()) {
        const QString identity = sourceFactKey(current);
        if (!validFact(current) || currentFacts.contains(identity)) {
            setError(out, QStringLiteral("Canonical local export snapshot is invalid."));
            return std::nullopt;
        }
        currentFacts.insert(identity, current);
    }
    if (currentFacts.size() != facts.size()) {
        setError(out, QStringLiteral("Export preview does not cover the complete current local snapshot."));
        return std::nullopt;
    }
    QSet<QString> seenFacts;
    for (const TrackerDeliveryFact &fact : facts) {
        if (!validFact(fact)) {
            setError(out, QStringLiteral("Tracker export preview contains an invalid local fact."));
            return std::nullopt;
        }
        const QString identity = sourceFactKey(fact);
        if (seenFacts.contains(identity)) {
            setError(out, QStringLiteral("Tracker export preview contains a duplicate local fact."));
            return std::nullopt;
        }
        const auto current = currentFacts.constFind(identity);
        if (current == currentFacts.cend() || !sameFact(*current, fact)) {
            setError(out, QStringLiteral("Export preview does not match the complete current local snapshot."));
            return std::nullopt;
        }
        seenFacts.insert(identity);

        TrackerExportPreviewItem item;
        item.fact = fact;
        if (fact.origin != TrackerDeliveryOrigin::NativeLocal) {
            item.reason = TrackerExportIneligibleReason::NonNativeOrigin;
        } else if (!source->isDurablyCurrent(fact)) {
            item.reason = TrackerExportIneligibleReason::SourceChanged;
        } else {
            bool ambiguous = false;
            item.mapping = uniqueMapping(m_mappings, providerId, remoteAccountId, fact, &ambiguous);
            if (ambiguous) {
                item.reason = TrackerExportIneligibleReason::AmbiguousMapping;
                item.mapping.reset();
            } else if (!item.mapping) {
                item.reason = TrackerExportIneligibleReason::MissingMapping;
            } else {
                const QString remoteMediaId = item.mapping->remote.remoteMediaId;
                const QString remoteFactKey = remoteMediaId + QChar(0x1f)
                    + QString::number(static_cast<int>(fact.kind)) + QChar(0x1f)
                    + fact.sourceEventId;
                const auto remoteState = remoteStates.constFind(remoteFactKey);
                if (remoteState == remoteStates.cend()) {
                    setError(out, QStringLiteral("Remote-state snapshot omitted a mapped item."));
                    return std::nullopt;
                }
                expectedRemoteFactKeys.insert(remoteFactKey);
                item.remoteStateKnown = true;
                item.remotePresent = remoteState->present;
                item.remoteExactlyMatches = remoteState->exactlyMatchesIntendedState;
                item.willChangeRemote = !remoteState->exactlyMatchesIntendedState;
                item.remoteStateFingerprint = remoteState->stateFingerprint;
                item.remoteStateSummary = remoteState->safeSummary;

                if (!providerSupportsDelivery(providerId, fact.mediaDomain)
                    || !m_connections->connection(providerId)->capabilities.testFlag(
                        requiredCapability(fact.kind))) {
                    item.reason = TrackerExportIneligibleReason::Unsupported;
                } else if (remoteState->exactlyMatchesIntendedState) {
                    item.reason = TrackerExportIneligibleReason::RemoteAlreadyCurrent;
                } else {
                    item.eligible = true;
                    item.reason = TrackerExportIneligibleReason::None;
                }
            }
        }
        item.itemId = exportItemId(providerId, remoteAccountId, connectionGeneration,
                                   fact, item.mapping, item.reason, remoteSnapshot.snapshotId,
                                   item.remoteStateFingerprint);
        preview.items.push_back(std::move(item));
    }
    QSet<QString> snapshotRemoteFactKeys;
    for (const TrackerRemoteDeliveryState &remoteState : remoteSnapshot.items)
        snapshotRemoteFactKeys.insert(remoteState.remoteMediaId + QChar(0x1f)
            + QString::number(static_cast<int>(remoteState.factKind)) + QChar(0x1f)
            + remoteState.sourceEventId);
    if (snapshotRemoteFactKeys != expectedRemoteFactKeys) {
        setError(out, QStringLiteral("Remote-state snapshot does not exactly cover mapped export items."));
        return std::nullopt;
    }
    preview.previewId = exportPreviewId(providerId, remoteAccountId, connectionGeneration,
                                        preview.items, remoteSnapshot.snapshotId, snapshotBinding);
    m_previews.insert(preview.previewId, preview);
    return preview;
}

bool TrackerDeliveryStore::confirmExport(const QString &previewId,
                                         const QStringList &selectedItemIds,
                                         const TrackerRemoteDeliverySnapshot &currentRemoteSnapshot,
                                         const TrackerDeliverySource *source,
                                         qint64 confirmedAtMs,
                                         QString *out)
{
    if (!healthy(out) || !source || !source->isReady()
        || !safeText(previewId, 128) || selectedItemIds.isEmpty()
        || confirmedAtMs <= 0) {
        return setError(out, QStringLiteral("Tracker export confirmation is invalid."));
    }
    const auto previewIt = m_previews.constFind(previewId);
    if (previewIt == m_previews.cend())
        return setError(out, QStringLiteral("Tracker export preview is no longer available."));
    const TrackerExportPreview preview = *previewIt;
    if (!sameRemoteSnapshot(preview.remoteSnapshot, currentRemoteSnapshot))
        return setError(out, QStringLiteral("Provider state changed after export review; review it again."));
    QHash<QString, TrackerDeliveryFact> currentFacts;
    for (const TrackerDeliveryFact &fact : source->currentCommittedFacts()) {
        const QString identity = sourceFactKey(fact);
        if (!validFact(fact) || currentFacts.contains(identity))
            return setError(out, QStringLiteral("Canonical local export snapshot changed after review."));
        currentFacts.insert(identity, fact);
    }
    if (currentFacts.size() != preview.items.size())
        return setError(out, QStringLiteral("Local facts changed after export preview; review them again."));
    for (const TrackerExportPreviewItem &item : preview.items) {
        const auto current = currentFacts.constFind(sourceFactKey(item.fact));
        if (current == currentFacts.cend() || !sameFact(*current, item.fact)
            || !source->isDurablyCurrent(item.fact))
            return setError(out, QStringLiteral("Local facts changed after export preview; review them again."));
    }
    const auto active = m_connections->connection(preview.providerId);
    if (!active || active->state != TrackerConnectionState::Connected
        || active->remoteAccountId != preview.remoteAccountId
        || active->connectionGeneration != preview.connectionGeneration) {
        return setError(out, QStringLiteral("Tracker connection changed after export preview."));
    }

    QSet<QString> selected;
    for (const QString &id : selectedItemIds) {
        if (!safeText(id, 128) || selected.contains(id))
            return setError(out, QStringLiteral("Tracker export selection is invalid."));
        selected.insert(id);
    }
    QList<TrackerDeliveryOperation> candidateOperations = m_operations;
    QList<TrackerDeliveryProviderPreference> candidatePreferences = m_preferences;
    auto preference = std::find_if(candidatePreferences.begin(), candidatePreferences.end(),
        [&preview](const TrackerDeliveryProviderPreference &entry) {
            return entry.providerId == preview.providerId
                && entry.remoteAccountId == preview.remoteAccountId;
        });
    if (preference == candidatePreferences.end()) {
        candidatePreferences.push_back({preview.providerId, preview.remoteAccountId, confirmedAtMs, true});
    }
    preference = std::find_if(candidatePreferences.begin(), candidatePreferences.end(),
        [&preview](const TrackerDeliveryProviderPreference &entry) {
            return entry.providerId == preview.providerId
                && entry.remoteAccountId == preview.remoteAccountId;
        });
    if (preference == candidatePreferences.end())
        return setError(out, QStringLiteral("Tracker export consent could not be recorded."));
    if (preference->firstExportConfirmedAtMs <= 0) {
        preference->firstExportConfirmedAtMs = confirmedAtMs;
        preference->sendEnabled = true;
    }
    for (const TrackerExportPreviewItem &item : preview.items)
        preference->reviewedFactBindings.insert(sourceFactKey(item.fact), factBindingKey(item.fact));

    QSet<QString> consumed;
    for (const TrackerExportPreviewItem &item : preview.items) {
        if (!selected.contains(item.itemId))
            continue;
        consumed.insert(item.itemId);
        if (!item.eligible || !item.mapping || !validMapping(*item.mapping)
            || item.fact.origin != TrackerDeliveryOrigin::NativeLocal
            || !item.remoteStateKnown || item.remoteExactlyMatches
            || !source->isDurablyCurrent(item.fact)) {
            return setError(out, QStringLiteral("A selected export item is no longer eligible."));
        }
        bool ambiguous = false;
        const auto currentMapping = uniqueMapping(m_mappings, preview.providerId,
                                                 preview.remoteAccountId, item.fact, &ambiguous);
        if (ambiguous || !currentMapping || !sameMapping(*currentMapping, *item.mapping)
            || !currentConnection(m_connections, preview.providerId, preview.remoteAccountId,
                                  preview.connectionGeneration, requiredCapability(item.fact.kind),
                                  item.fact.mediaDomain)) {
            return setError(out, QStringLiteral("A selected export mapping or provider capability changed."));
        }
        const QString id = operationIdFor(preview.providerId, preview.remoteAccountId,
                                          preview.connectionGeneration, *currentMapping, item.fact);
        const auto existing = std::find_if(candidateOperations.cbegin(), candidateOperations.cend(),
            [&id](const TrackerDeliveryOperation &entry) { return entry.operationId == id; });
        if (existing != candidateOperations.cend())
            continue;

        if (item.fact.kind == TrackerDeliveryFactKind::Progress) {
            candidateOperations.erase(std::remove_if(candidateOperations.begin(), candidateOperations.end(),
                [&preview, &item, &currentMapping](const TrackerDeliveryOperation &entry) {
                    return entry.providerId == preview.providerId
                        && entry.remoteAccountId == preview.remoteAccountId
                        && entry.fact.kind == TrackerDeliveryFactKind::Progress
                        && entry.fact.canonicalMediaId == item.fact.canonicalMediaId
                        && entry.state == TrackerDeliveryState::Pending && entry.attemptCount == 0
                        && sameRemote(entry.mapping.remote, currentMapping->remote)
                        && entry.mapping.revision == currentMapping->revision;
                }), candidateOperations.end());
        }
        candidateOperations.push_back({id, preview.providerId, preview.remoteAccountId,
                                       preview.connectionGeneration, *currentMapping, item.fact,
                                       TrackerDeliveryState::Pending, 0, confirmedAtMs,
                                       TrackerDeliveryReason::None,
                                       preview.remoteSnapshot.snapshotId,
                                       item.remoteStateFingerprint,
                                       item.remotePresent});
    }
    if (consumed != selected)
        return setError(out, QStringLiteral("Tracker export selection contains an item outside this preview."));
    return commit(candidatePreferences, candidateOperations, m_cooldowns, out);
}

bool TrackerDeliveryStore::hasFirstExportConsent(TrackerProviderId providerId,
                                                 const QString &remoteAccountId) const
{
    return std::any_of(m_preferences.cbegin(), m_preferences.cend(),
        [providerId, &remoteAccountId](const TrackerDeliveryProviderPreference &preference) {
            return preference.providerId == providerId && preference.remoteAccountId == remoteAccountId
                && preference.firstExportConfirmedAtMs > 0;
        });
}

bool TrackerDeliveryStore::providerSendEnabled(TrackerProviderId providerId,
                                               const QString &remoteAccountId) const
{
    const auto preference = std::find_if(m_preferences.cbegin(), m_preferences.cend(),
        [providerId, &remoteAccountId](const TrackerDeliveryProviderPreference &entry) {
            return entry.providerId == providerId && entry.remoteAccountId == remoteAccountId;
        });
    return preference != m_preferences.cend() && preference->sendEnabled;
}

bool TrackerDeliveryStore::setProviderSendEnabled(TrackerProviderId providerId,
                                                  const QString &remoteAccountId,
                                                  bool enabled,
                                                  QString *out)
{
    if (!healthy(out) || !trackerProviderIdFromKey(trackerProviderKey(providerId))
        || !safeText(remoteAccountId, 128))
        return setError(out, QStringLiteral("Tracker export preference is invalid."));
    QList<TrackerDeliveryProviderPreference> candidate = m_preferences;
    auto preference = std::find_if(candidate.begin(), candidate.end(),
        [providerId, &remoteAccountId](const TrackerDeliveryProviderPreference &entry) {
            return entry.providerId == providerId && entry.remoteAccountId == remoteAccountId;
        });
    if (preference == candidate.end() || (enabled && preference->firstExportConfirmedAtMs <= 0))
        return setError(out, QStringLiteral("First local-export review is required before enabling sends."));
    if (preference->sendEnabled == enabled)
        return true;
    preference->sendEnabled = enabled;
    return commit(candidate, m_operations, m_cooldowns, out);
}

bool TrackerDeliveryStore::removeProviderSettingsFor(TrackerProviderId providerId,
                                                      const QString &remoteAccountId,
                                                      QString *out)
{
    if (!healthy(out) || !trackerProviderIdFromKey(trackerProviderKey(providerId))
        || !safeText(remoteAccountId, 128)) {
        return setError(out, QStringLiteral("Tracker export settings are invalid."));
    }
    auto sourcePreferences = m_preferences;
    const auto preference = std::find_if(sourcePreferences.cbegin(), sourcePreferences.cend(),
        [providerId, &remoteAccountId](const TrackerDeliveryProviderPreference &entry) {
            return entry.providerId == providerId && entry.remoteAccountId == remoteAccountId;
        });
    if (preference == sourcePreferences.cend())
        return true;

    const bool hasProfilePrivateOperations = std::any_of(
        m_operations.cbegin(), m_operations.cend(),
        [providerId, &remoteAccountId](const TrackerDeliveryOperation &entry) {
            return entry.providerId == providerId && entry.remoteAccountId == remoteAccountId;
        });
    if (hasProfilePrivateOperations) {
        // Retained source-profile outbox records remain bound to this consent
        // journal. The disconnected source binding prevents delivery; keeping
        // the local receipt also lets interrupted moves reopen safely.
        return true;
    }

    sourcePreferences.erase(std::remove_if(sourcePreferences.begin(), sourcePreferences.end(),
        [providerId, &remoteAccountId](const TrackerDeliveryProviderPreference &entry) {
            return entry.providerId == providerId && entry.remoteAccountId == remoteAccountId;
        }), sourcePreferences.end());
    return commit(sourcePreferences, m_operations, m_cooldowns, out);
}

bool TrackerDeliveryStore::observeCommittedFact(const TrackerDeliveryFact &fact,
                                                const TrackerDeliverySource *source,
                                                QString *operationId,
                                                QString *out)
{
    if (out)
        out->clear();
    if (operationId)
        operationId->clear();
    if (!healthy(out) || !source || !source->isReady() || !validFact(fact))
        return setError(out, QStringLiteral("Committed tracker delivery fact is invalid."));
    if (fact.origin != TrackerDeliveryOrigin::NativeLocal)
        return false;
    if (!source->isDurablyCurrent(fact))
        return setError(out, QStringLiteral("Local fact has no current durable source receipt."));

    bool queued = false;
    for (const TrackerDeliveryProviderPreference &preference : m_preferences) {
        if (!preference.sendEnabled)
            continue;
        const QString identity = sourceFactKey(fact);
        if (preference.reviewedFactBindings.value(identity) == factBindingKey(fact)) {
            const bool alreadyQueued = std::any_of(m_operations.cbegin(), m_operations.cend(),
                [&preference, &fact](const TrackerDeliveryOperation &entry) {
                    return entry.providerId == preference.providerId
                        && entry.remoteAccountId == preference.remoteAccountId
                        && sameFact(entry.fact, fact);
                });
            if (!alreadyQueued)
                continue;
        }
        const auto connection = m_connections->connection(preference.providerId);
        if (!connection || connection->remoteAccountId != preference.remoteAccountId
            || connection->state != TrackerConnectionState::Connected
            || !providerSupportsDelivery(preference.providerId, fact.mediaDomain)
            || !connection->capabilities.testFlag(requiredCapability(fact.kind))) {
            continue;
        }
        bool ambiguous = false;
        const auto mapping = uniqueMapping(m_mappings, preference.providerId,
                                          preference.remoteAccountId, fact, &ambiguous);
        if (!mapping || ambiguous)
            continue;
        QString id;
        if (!enqueueFact(fact, preference.providerId, preference.remoteAccountId,
                         connection->connectionGeneration, *mapping, &id, out)) {
            return false;
        }
        if (operationId && operationId->isEmpty())
            *operationId = id;
        queued = queued || !id.isEmpty();
    }
    return queued;
}

bool TrackerDeliveryStore::recoverSourceGap(const TrackerDeliverySource *source,
                                            int *enqueuedCount,
                                            QString *out)
{
    if (!healthy(out) || !source || !source->isReady())
        return setError(out, QStringLiteral("Tracker source recovery is unavailable."));
    int count = 0;
    for (const TrackerDeliveryFact &fact : source->currentCommittedFacts()) {
        if (!validFact(fact))
            return setError(out, QStringLiteral("Canonical local source returned an invalid fact."));
        if (fact.origin != TrackerDeliveryOrigin::NativeLocal)
            continue;
        if (!source->isDurablyCurrent(fact))
            return setError(out, QStringLiteral("Canonical local source changed during recovery."));
        QSet<QString> beforeIds;
        for (const TrackerDeliveryOperation &operation : m_operations)
            beforeIds.insert(operation.operationId);
        QString observeError;
        if (observeCommittedFact(fact, source, nullptr, &observeError)) {
            for (const TrackerDeliveryOperation &operation : m_operations) {
                if (!beforeIds.contains(operation.operationId))
                    ++count;
            }
        } else if (!observeError.isEmpty()) {
            return setError(out, observeError);
        }
    }
    if (enqueuedCount)
        *enqueuedCount = count;
    return true;
}

QList<TrackerDeliveryOperation> TrackerDeliveryStore::operations() const
{
    return m_operations;
}

std::optional<TrackerDeliveryOperation> TrackerDeliveryStore::operation(const QString &operationId) const
{
    const auto found = std::find_if(m_operations.cbegin(), m_operations.cend(),
        [&operationId](const TrackerDeliveryOperation &entry) { return entry.operationId == operationId; });
    return found == m_operations.cend() ? std::nullopt
                                        : std::optional<TrackerDeliveryOperation>(*found);
}

bool TrackerDeliveryStore::adoptPrivateStateFrom(const TrackerDeliveryStore &source,
                                                  QString *out)
{
    if (!healthy(out) || !source.healthy(out) || this == &source
        || m_profile.profileId() == source.m_profile.profileId()) {
        return setError(out, QStringLiteral("Tracker delivery state cannot be adopted between these profiles."));
    }

    auto preferences = m_preferences;
    auto operations = m_operations;
    auto cooldowns = m_cooldowns;
    bool changed = false;

    // Export consent and fact-review bindings are profile-local. Source
    // receipts remain visible for recovery, but adopted known-unsent work is
    // held for a new destination-owned export review and never rebound itself.

    for (const TrackerDeliveryOperation &stored : source.m_operations) {
        const bool hasDestinationPreference = std::any_of(
            preferences.cbegin(), preferences.cend(), [&stored](const auto &entry) {
                return entry.providerId == stored.providerId
                    && entry.remoteAccountId == stored.remoteAccountId;
            });
        if (!hasDestinationPreference) {
            preferences.push_back({stored.providerId, stored.remoteAccountId, 0, false, {}});
            changed = true;
        }

        TrackerDeliveryOperation adopted = stored;
        adopted.adoptedReceipt = true;
        if (adopted.state == TrackerDeliveryState::Delivering) {
            adopted.state = TrackerDeliveryState::UnknownOutcome;
            adopted.reason = TrackerDeliveryReason::AcknowledgementLost;
        } else if (adopted.state == TrackerDeliveryState::Pending
                   || adopted.state == TrackerDeliveryState::Retrying) {
            adopted.state = TrackerDeliveryState::NeedsAttention;
            adopted.reason = TrackerDeliveryReason::DestinationReviewRequired;
            adopted.nextAttemptAtMs = 0;
        }
        const auto duplicate = std::find_if(operations.cbegin(), operations.cend(),
            [&adopted](const TrackerDeliveryOperation &entry) {
                return entry.operationId == adopted.operationId;
            });
        if (duplicate != operations.cend()) {
            if (operationToJson(*duplicate) != operationToJson(adopted)) {
                return setError(out, QStringLiteral(
                    "The destination has a different outcome for a tracker delivery."));
            }
            continue;
        }
        operations.append(adopted);
        changed = true;
    }

    for (auto it = source.m_cooldowns.cbegin(); it != source.m_cooldowns.cend(); ++it) {
        const qint64 existing = cooldowns.value(it.key(), 0);
        if (it.value() <= existing)
            continue;
        cooldowns.insert(it.key(), it.value());
        changed = true;
    }

    if (!changed)
        return true;
    return commit(preferences, operations, cooldowns, out);
}

QList<TrackerDeliveryOperation> TrackerDeliveryStore::readyOperations(qint64 nowMs) const
{
    QList<TrackerDeliveryOperation> ready;
    if (!healthy())
        return ready;
    for (const TrackerDeliveryOperation &operation : m_operations) {
        if ((operation.state != TrackerDeliveryState::Pending
             && operation.state != TrackerDeliveryState::Retrying)
            || operation.adoptedReceipt
            || !providerSendEnabled(operation.providerId, operation.remoteAccountId)
            || operation.nextAttemptAtMs > nowMs || !connectionMatches(m_connections, operation)) {
            continue;
        }
        const qint64 cooldown = m_cooldowns.value(
            providerAccountKey(operation.providerId, operation.remoteAccountId), 0);
        if (cooldown <= nowMs)
            ready.push_back(operation);
    }
    return ready;
}

bool TrackerDeliveryStore::resumeKnownUnsentAfterReconnect(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    quint64 connectionGeneration,
    QString *out)
{
    if (!healthy(out) || !safeText(remoteAccountId, 128) || connectionGeneration == 0
        || !m_connections || !m_connections->healthy()) {
        return setError(out, QStringLiteral("Tracker pending updates could not be resumed."));
    }
    const auto binding = m_connections->connection(providerId);
    if (!binding || binding->state != TrackerConnectionState::Connected
        || binding->remoteAccountId != remoteAccountId
        || binding->connectionGeneration != connectionGeneration) {
        return setError(out, QStringLiteral("Tracker connection changed before queued updates resumed."));
    }

    const auto isKnownUnsentForReconnect = [&](const TrackerDeliveryOperation &operation) {
        return operation.providerId == providerId
            && operation.remoteAccountId == remoteAccountId
            && operation.connectionGeneration < connectionGeneration
            && (operation.state == TrackerDeliveryState::Pending
                || operation.state == TrackerDeliveryState::Retrying);
    };

    QSet<QString> occupiedIds;
    for (const TrackerDeliveryOperation &operation : m_operations) {
        if (!isKnownUnsentForReconnect(operation))
            occupiedIds.insert(operation.operationId);
    }

    QList<TrackerDeliveryOperation> candidate;
    bool changed = false;
    for (const TrackerDeliveryOperation &stored : m_operations) {
        if (!isKnownUnsentForReconnect(stored)) {
            candidate.append(stored);
            continue;
        }

        if (stored.adoptedReceipt) {
            candidate.append(stored);
            continue;
        }

        TrackerDeliveryOperation operation = stored;
        operation.connectionGeneration = connectionGeneration;
        operation.operationId = operationIdFor(providerId, remoteAccountId,
            connectionGeneration, operation.mapping, operation.fact);
        // A row already bound to this generation represents the same
        // deterministic operation. Keep it instead of creating a duplicate.
        if (!occupiedIds.contains(operation.operationId)) {
            occupiedIds.insert(operation.operationId);
            candidate.append(operation);
        }
        changed = true;
    }
    if (!changed)
        return true;
    if (!commit(m_preferences, candidate, m_cooldowns, out))
        return false;
    return true;
}

int TrackerDeliveryStore::discardKnownUnsent(TrackerProviderId providerId,
                                             const QString &remoteAccountId,
                                             QString *out)
{
    if (!healthy(out) || !trackerProviderIdFromKey(trackerProviderKey(providerId))
        || !safeText(remoteAccountId, 128)) {
        setError(out, QStringLiteral("Tracker pending updates could not be discarded."));
        return -1;
    }
    QList<TrackerDeliveryOperation> retained;
    int discarded = 0;
    for (const TrackerDeliveryOperation &operation : m_operations) {
        const bool knownUnsent = operation.state == TrackerDeliveryState::Pending
            || operation.state == TrackerDeliveryState::Retrying
            || (operation.state == TrackerDeliveryState::NeedsAttention
                && (operation.reason == TrackerDeliveryReason::RetryLimitReached
                    || operation.reason == TrackerDeliveryReason::DestinationReviewRequired));
        if (operation.providerId == providerId
            && operation.remoteAccountId == remoteAccountId && knownUnsent) {
            ++discarded;
        } else {
            retained.append(operation);
        }
    }
    if (discarded == 0)
        return 0;
    if (!commit(m_preferences, retained, m_cooldowns, out))
        return -1;
    return discarded;
}

bool TrackerDeliveryStore::markDelivering(const QString &operationId,
                                          const TrackerDeliverySource *source,
                                          qint64 nowMs,
                                          QString *out)
{
    if (!healthy(out) || !source || !source->isReady())
        return setError(out, QStringLiteral("Tracker delivery cannot start without a healthy local source."));
    auto candidate = m_operations;
    auto found = std::find_if(candidate.begin(), candidate.end(),
        [&operationId](const TrackerDeliveryOperation &entry) { return entry.operationId == operationId; });
    if (found == candidate.end()
        || (found->state != TrackerDeliveryState::Pending && found->state != TrackerDeliveryState::Retrying)
        || found->attemptCount >= kMaximumAttempts || found->nextAttemptAtMs > nowMs
        || found->adoptedReceipt
        || !providerSendEnabled(found->providerId, found->remoteAccountId)
        || !connectionMatches(m_connections, *found)) {
        return setError(out, QStringLiteral("Tracker delivery operation is not currently sendable."));
    }
    if (m_cooldowns.value(providerAccountKey(found->providerId, found->remoteAccountId), 0) > nowMs)
        return setError(out, QStringLiteral("Tracker provider is still rate limited."));
    const auto current = uniqueMapping(m_mappings, found->providerId, found->remoteAccountId,
                                       found->fact);
    if (!current || !sameMapping(*current, found->mapping)) {
        found->state = TrackerDeliveryState::NeedsAttention;
        found->reason = TrackerDeliveryReason::MappingChanged;
        if (!commit(m_preferences, candidate, m_cooldowns, out))
            return false;
        return setError(out, QStringLiteral("Tracker title mapping changed before delivery."));
    }
    if (!source->isDurablyCurrent(found->fact)) {
        found->state = TrackerDeliveryState::NeedsAttention;
        found->reason = TrackerDeliveryReason::StaleLocalFact;
        if (!commit(m_preferences, candidate, m_cooldowns, out))
            return false;
        return setError(out, QStringLiteral("Local fact changed before tracker delivery."));
    }
    ++found->attemptCount;
    found->state = TrackerDeliveryState::Delivering;
    found->reason = TrackerDeliveryReason::None;
    found->nextAttemptAtMs = 0;
    return commit(m_preferences, candidate, m_cooldowns, out);
}

bool TrackerDeliveryStore::recordAttemptResult(const QString &operationId,
                                               TrackerDeliveryAttemptResult result,
                                               qint64 nowMs,
                                               qint64 retryAfterAtMs,
                                               TrackerDeliveryReason reason,
                                               QString *out)
{
    const bool validResult = result == TrackerDeliveryAttemptResult::Succeeded
        || result == TrackerDeliveryAttemptResult::RetryableKnownNotApplied
        || result == TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied
        || result == TrackerDeliveryAttemptResult::NeedsAttention
        || result == TrackerDeliveryAttemptResult::FailedTerminal
        || result == TrackerDeliveryAttemptResult::UnknownOutcome;
    if (!healthy(out) || !validResult || reasonKey(reason).isEmpty() || nowMs <= 0
        || retryAfterAtMs < 0)
        return false;
    auto candidate = m_operations;
    auto found = std::find_if(candidate.begin(), candidate.end(),
        [&operationId](const TrackerDeliveryOperation &entry) { return entry.operationId == operationId; });
    if (found == candidate.end() || found->state != TrackerDeliveryState::Delivering)
        return setError(out, QStringLiteral("Tracker result does not match an active delivery attempt."));
    auto cooldowns = m_cooldowns;
    switch (result) {
    case TrackerDeliveryAttemptResult::Succeeded:
        found->state = TrackerDeliveryState::Succeeded;
        found->reason = TrackerDeliveryReason::None;
        found->nextAttemptAtMs = 0;
        break;
    case TrackerDeliveryAttemptResult::RetryableKnownNotApplied:
    case TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied: {
        const qint64 delay = retryDelayMs(found->operationId, found->attemptCount);
        const qint64 backoffAt = nowMs > std::numeric_limits<qint64>::max() - delay
            ? std::numeric_limits<qint64>::max() : nowMs + delay;
        const qint64 retryAt = std::max(backoffAt, retryAfterAtMs > nowMs ? retryAfterAtMs : 0LL);
        const bool rateLimited = result == TrackerDeliveryAttemptResult::RateLimitedKnownNotApplied;
        if (rateLimited) {
            const QString key = providerAccountKey(found->providerId, found->remoteAccountId);
            cooldowns.insert(key, std::max(cooldowns.value(key, 0), retryAt));
        }
        if (found->attemptCount >= kMaximumAttempts) {
            found->state = TrackerDeliveryState::NeedsAttention;
            found->reason = TrackerDeliveryReason::RetryLimitReached;
            found->nextAttemptAtMs = 0;
            break;
        }
        found->nextAttemptAtMs = retryAt;
        found->state = TrackerDeliveryState::Retrying;
        found->reason = reason == TrackerDeliveryReason::None
            ? (rateLimited
                   ? TrackerDeliveryReason::ProviderRateLimited
                   : TrackerDeliveryReason::ProviderRetryable)
            : reason;
        break;
    }
    case TrackerDeliveryAttemptResult::NeedsAttention:
        found->state = TrackerDeliveryState::NeedsAttention;
        found->reason = reason == TrackerDeliveryReason::None
            ? TrackerDeliveryReason::AuthenticationRequired : reason;
        found->nextAttemptAtMs = 0;
        break;
    case TrackerDeliveryAttemptResult::FailedTerminal:
        found->state = TrackerDeliveryState::FailedTerminal;
        found->reason = reason == TrackerDeliveryReason::None
            ? TrackerDeliveryReason::TerminalProviderRefusal : reason;
        found->nextAttemptAtMs = 0;
        break;
    case TrackerDeliveryAttemptResult::UnknownOutcome:
        found->state = TrackerDeliveryState::UnknownOutcome;
        found->reason = TrackerDeliveryReason::AcknowledgementLost;
        found->nextAttemptAtMs = 0;
        break;
    }
    return commit(m_preferences, candidate, cooldowns, out);
}

bool TrackerDeliveryStore::reconcileUnknown(const QString &operationId,
                                            TrackerDeliveryReadback result,
                                            qint64 nowMs,
                                            QString *out)
{
    if (!healthy(out) || (result != TrackerDeliveryReadback::ExactPresent
                          && result != TrackerDeliveryReadback::Absent
                          && result != TrackerDeliveryReadback::PresentDifferent
                          && result != TrackerDeliveryReadback::Indeterminate))
        return false;
    auto candidate = m_operations;
    auto found = std::find_if(candidate.begin(), candidate.end(),
        [&operationId](const TrackerDeliveryOperation &entry) { return entry.operationId == operationId; });
    if (found == candidate.end() || found->state != TrackerDeliveryState::UnknownOutcome)
        return setError(out, QStringLiteral("Readback is permitted only for an unknown delivery outcome."));
    switch (result) {
    case TrackerDeliveryReadback::ExactPresent:
        found->state = TrackerDeliveryState::Succeeded;
        found->reason = TrackerDeliveryReason::ReadbackPresent;
        found->nextAttemptAtMs = 0;
        break;
    case TrackerDeliveryReadback::Absent:
        if (found->adoptedReceipt) {
            found->state = TrackerDeliveryState::NeedsAttention;
            found->reason = TrackerDeliveryReason::DestinationReviewRequired;
            found->nextAttemptAtMs = 0;
        } else if (found->attemptCount >= kMaximumAttempts) {
            found->state = TrackerDeliveryState::NeedsAttention;
            found->reason = TrackerDeliveryReason::RetryLimitReached;
            found->nextAttemptAtMs = 0;
        } else {
            found->state = TrackerDeliveryState::Pending;
            found->reason = TrackerDeliveryReason::ReadbackAbsent;
            found->nextAttemptAtMs = nowMs;
        }
        break;
    case TrackerDeliveryReadback::PresentDifferent:
        found->state = TrackerDeliveryState::NeedsAttention;
        found->reason = TrackerDeliveryReason::ReadbackDifferent;
        found->nextAttemptAtMs = 0;
        break;
    case TrackerDeliveryReadback::Indeterminate:
        found->reason = TrackerDeliveryReason::ReadbackUncertain;
        break;
    }
    return commit(m_preferences, candidate, m_cooldowns, out);
}

bool TrackerDeliveryStore::enqueueFact(const TrackerDeliveryFact &fact,
                                       TrackerProviderId providerId,
                                       const QString &remoteAccountId,
                                       quint64 connectionGeneration,
                                       const TrackerTitleMapping &mapping,
                                       QString *operationId,
                                       QString *out)
{
    if (fact.origin != TrackerDeliveryOrigin::NativeLocal || !validFact(fact)
        || !providerSupportsDelivery(providerId, fact.mediaDomain)
        || !validMapping(mapping) || mapping.remote.providerId != providerId
        || mapping.remote.remoteAccountId != remoteAccountId
        || mapping.canonical.canonicalMediaId != fact.canonicalMediaId
        || mapping.canonical.historyKind != fact.historyKind
        || mapping.canonical.historyId != fact.historyId) {
        return setError(out, QStringLiteral("Tracker delivery binding is invalid."));
    }
    const auto connection = m_connections->connection(providerId);
    if (!connection || connection->remoteAccountId != remoteAccountId
        || connection->connectionGeneration != connectionGeneration
        || !connection->capabilities.testFlag(requiredCapability(fact.kind))) {
        return false;
    }
    const QString id = operationIdFor(providerId, remoteAccountId, connectionGeneration, mapping, fact);
    const auto duplicate = std::find_if(m_operations.cbegin(), m_operations.cend(),
        [&id](const TrackerDeliveryOperation &entry) { return entry.operationId == id; });
    if (duplicate != m_operations.cend()) {
        if (operationId)
            *operationId = id;
        return true;
    }

    auto candidate = m_operations;
    if (fact.kind == TrackerDeliveryFactKind::Progress) {
        candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
            [providerId, &remoteAccountId, &fact, &mapping](const TrackerDeliveryOperation &entry) {
                return entry.providerId == providerId && entry.remoteAccountId == remoteAccountId
                    && entry.fact.kind == TrackerDeliveryFactKind::Progress
                    && entry.fact.canonicalMediaId == fact.canonicalMediaId
                    && entry.state == TrackerDeliveryState::Pending && entry.attemptCount == 0
                    && sameRemote(entry.mapping.remote, mapping.remote)
                    && entry.mapping.revision == mapping.revision;
            }), candidate.end());
    }
    candidate.push_back({id, providerId, remoteAccountId, connectionGeneration, mapping, fact,
                         TrackerDeliveryState::Pending, 0, 0, TrackerDeliveryReason::None});
    if (!commit(m_preferences, candidate, m_cooldowns, out))
        return false;
    if (operationId)
        *operationId = id;
    return true;
}

bool TrackerDeliveryStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker delivery journal cannot be read.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker delivery journal is malformed; delivery is paused.");
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("schemaVersion"));
    const QJsonValue profileId = root.value(QStringLiteral("profileId"));
    if (!version.isDouble() || version.toInt(-1) != kSchemaVersion || !profileId.isString()
        || profileId.toString() != m_profile.profileId()
        || !root.value(QStringLiteral("preferences")).isArray()
        || !root.value(QStringLiteral("operations")).isArray()
        || !root.value(QStringLiteral("cooldowns")).isArray()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker delivery journal belongs to another profile or schema.");
        return false;
    }

    QList<TrackerDeliveryProviderPreference> preferences;
    QSet<QString> preferenceKeys;
    for (const QJsonValue &value : root.value(QStringLiteral("preferences")).toArray()) {
        if (!value.isObject()) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker delivery preference is malformed.");
            return false;
        }
        const auto preference = preferenceFromJson(value.toObject());
        if (!preference) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker delivery preference is malformed.");
            return false;
        }
        const QString key = providerAccountKey(preference->providerId, preference->remoteAccountId);
        if (preferenceKeys.contains(key)) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker delivery journal contains duplicate preferences.");
            return false;
        }
        preferenceKeys.insert(key);
        preferences.push_back(*preference);
    }

    QList<TrackerDeliveryOperation> operations;
    QSet<QString> operationIds;
    for (const QJsonValue &value : root.value(QStringLiteral("operations")).toArray()) {
        if (!value.isObject()) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker delivery operation is malformed.");
            return false;
        }
        auto operation = operationFromJson(value.toObject());
        if (!operation || operationIds.contains(operation->operationId)
            || !preferenceKeys.contains(providerAccountKey(operation->providerId,
                                                            operation->remoteAccountId))) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker delivery operation is malformed or unconsented.");
            return false;
        }
        operationIds.insert(operation->operationId);
        operations.push_back(*operation);
    }

    QHash<QString, qint64> cooldowns;
    for (const QJsonValue &value : root.value(QStringLiteral("cooldowns")).toArray()) {
        if (!value.isObject()) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker delivery cooldown is malformed.");
            return false;
        }
        const QJsonObject object = value.toObject();
        const auto provider = trackerProviderIdFromKey(object.value(QStringLiteral("providerId")).toString());
        const QString account = object.value(QStringLiteral("remoteAccountId")).toString();
        bool timeOk = false;
        const qint64 until = object.value(QStringLiteral("notBeforeAtMs")).toString().toLongLong(&timeOk);
        const QString key = provider ? providerAccountKey(*provider, account) : QString();
        if (!provider || !safeText(account, 128) || !timeOk || until <= 0 || cooldowns.contains(key)) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker delivery cooldown is malformed.");
            return false;
        }
        cooldowns.insert(key, until);
    }

    bool recovered = false;
    for (TrackerDeliveryOperation &operation : operations) {
        if (operation.state != TrackerDeliveryState::Delivering)
            continue;
        operation.state = TrackerDeliveryState::UnknownOutcome;
        operation.reason = TrackerDeliveryReason::AcknowledgementLost;
        operation.nextAttemptAtMs = 0;
        recovered = true;
    }
    m_preferences = preferences;
    m_operations = operations;
    m_cooldowns = cooldowns;
    if (recovered && !persist(m_preferences, m_operations, m_cooldowns, &m_error)) {
        m_healthy = false;
        if (m_error.isEmpty())
            m_error = QStringLiteral("Interrupted tracker delivery could not be recovered safely.");
        return false;
    }
    return true;
}

bool TrackerDeliveryStore::persist(const QList<TrackerDeliveryProviderPreference> &preferences,
                                   const QList<TrackerDeliveryOperation> &operations,
                                   const QHash<QString, qint64> &cooldowns,
                                   QString *out) const
{
#ifdef COLOSSEUM_TRACKER_DELIVERY_TESTING
    if (m_forcePersistenceFailure)
        return setError(out, QStringLiteral("Tracker delivery journal write was refused."));
#endif
    QJsonArray preferenceArray;
    for (const TrackerDeliveryProviderPreference &preference : preferences)
        preferenceArray.push_back(preferenceToJson(preference));
    QJsonArray operationArray;
    for (const TrackerDeliveryOperation &operation : operations)
        operationArray.push_back(operationToJson(operation));
    QJsonArray cooldownArray;
    for (auto it = cooldowns.cbegin(); it != cooldowns.cend(); ++it) {
        const int separator = it.key().indexOf(QChar(0x1f));
        if (separator <= 0)
            return setError(out, QStringLiteral("Tracker delivery cooldown key is invalid."));
        const auto provider = trackerProviderIdFromKey(it.key().left(separator));
        const QString account = it.key().mid(separator + 1);
        if (!provider || !safeText(account, 128) || it.value() <= 0)
            return setError(out, QStringLiteral("Tracker delivery cooldown is invalid."));
        cooldownArray.push_back(QJsonObject{
            {QStringLiteral("providerId"), trackerProviderKey(*provider)},
            {QStringLiteral("remoteAccountId"), account},
            {QStringLiteral("notBeforeAtMs"), QString::number(it.value())}});
    }
    const QJsonObject root{{QStringLiteral("schemaVersion"), kSchemaVersion},
                           {QStringLiteral("profileId"), m_profile.profileId()},
                           {QStringLiteral("preferences"), preferenceArray},
                           {QStringLiteral("operations"), operationArray},
                           {QStringLiteral("cooldowns"), cooldownArray}};
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.absolutePath()))
        return setError(out, QStringLiteral("Tracker delivery journal directory is unavailable."));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(out, QStringLiteral("Tracker delivery journal cannot be written."));
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(payload) != payload.size() || !file.commit())
        return setError(out, QStringLiteral("Tracker delivery journal could not be committed atomically."));
    return true;
}

bool TrackerDeliveryStore::commit(const QList<TrackerDeliveryProviderPreference> &preferences,
                                  const QList<TrackerDeliveryOperation> &operations,
                                  const QHash<QString, qint64> &cooldowns,
                                  QString *out)
{
    if (!persist(preferences, operations, cooldowns, out))
        return false;
    m_preferences = preferences;
    m_operations = operations;
    m_cooldowns = cooldowns;
    return true;
}
