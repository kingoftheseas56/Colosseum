#include "TrackerSyncCenterModel.h"

#include "TrackerConnectionStore.h"
#include "TrackerDeliveryStore.h"
#include "TrackerHistoryEvidenceStore.h"
#include "TrackerImportStore.h"
#include "TrackerMappingStore.h"
#include "TrackerProgressImportOwner.h"
#include "TrackerScrobbleStore.h"
#include "TrackerSyncSettingsStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

QString importClassificationKey(TrackerImportClassification value)
{
    switch (value) {
    case TrackerImportClassification::NewProgress: return QStringLiteral("new_progress");
    case TrackerImportClassification::ExactMatch: return QStringLiteral("exact_match");
    case TrackerImportClassification::RemoteAdvance: return QStringLiteral("remote_advance");
    case TrackerImportClassification::Disagreement: return QStringLiteral("disagreement");
    case TrackerImportClassification::NeedsMatching: return QStringLiteral("needs_matching");
    case TrackerImportClassification::Unsupported: return QStringLiteral("unsupported");
    case TrackerImportClassification::Duplicate: return QStringLiteral("duplicate");
    }
    return QStringLiteral("unknown");
}

QString importStateKey(TrackerImportItemState value)
{
    switch (value) {
    case TrackerImportItemState::ReviewRequired: return QStringLiteral("review_required");
    case TrackerImportItemState::AwaitingApply: return QStringLiteral("waiting_to_apply");
    case TrackerImportItemState::Applying: return QStringLiteral("applying");
    case TrackerImportItemState::Applied: return QStringLiteral("applied");
    case TrackerImportItemState::KeptLocal: return QStringLiteral("kept_colosseum");
    case TrackerImportItemState::Unresolved: return QStringLiteral("unresolved");
    case TrackerImportItemState::NonMutating: return QStringLiteral("no_change");
    case TrackerImportItemState::Superseded: return QStringLiteral("superseded");
    }
    return QStringLiteral("unknown");
}

QString importResolutionKey(TrackerImportResolution value)
{
    switch (value) {
    case TrackerImportResolution::None: return QStringLiteral("none");
    case TrackerImportResolution::UseProviderProgress:
        return QStringLiteral("use_provider_progress");
    case TrackerImportResolution::KeepColosseum: return QStringLiteral("keep_colosseum");
    case TrackerImportResolution::LeaveUnresolved: return QStringLiteral("leave_unresolved");
    }
    return QStringLiteral("none");
}

QString deliveryStateKey(TrackerDeliveryState value)
{
    switch (value) {
    case TrackerDeliveryState::Pending: return QStringLiteral("waiting");
    case TrackerDeliveryState::Delivering: return QStringLiteral("syncing");
    case TrackerDeliveryState::Retrying: return QStringLiteral("retrying");
    case TrackerDeliveryState::Succeeded: return QStringLiteral("confirmed");
    case TrackerDeliveryState::NeedsAttention: return QStringLiteral("needs_attention");
    case TrackerDeliveryState::FailedTerminal: return QStringLiteral("failed");
    case TrackerDeliveryState::UnknownOutcome: return QStringLiteral("checking_delivery");
    }
    return QStringLiteral("unknown");
}

QString deliveryReasonKey(TrackerDeliveryReason value)
{
    switch (value) {
    case TrackerDeliveryReason::None: return QStringLiteral("none");
    case TrackerDeliveryReason::ProviderRetryable: return QStringLiteral("provider_retry");
    case TrackerDeliveryReason::ProviderRateLimited: return QStringLiteral("rate_limited");
    case TrackerDeliveryReason::AuthenticationRequired: return QStringLiteral("authentication_required");
    case TrackerDeliveryReason::UnsupportedAction: return QStringLiteral("unsupported_action");
    case TrackerDeliveryReason::TerminalProviderRefusal: return QStringLiteral("provider_rejected");
    case TrackerDeliveryReason::AcknowledgementLost: return QStringLiteral("acknowledgement_lost");
    case TrackerDeliveryReason::ReadbackPresent: return QStringLiteral("confirmed_by_readback");
    case TrackerDeliveryReason::ReadbackAbsent: return QStringLiteral("not_found_on_provider");
    case TrackerDeliveryReason::ReadbackDifferent: return QStringLiteral("provider_state_differs");
    case TrackerDeliveryReason::ReadbackUncertain: return QStringLiteral("delivery_uncertain");
    case TrackerDeliveryReason::StaleLocalFact: return QStringLiteral("local_state_changed");
    case TrackerDeliveryReason::MappingChanged: return QStringLiteral("title_match_changed");
    case TrackerDeliveryReason::RetryLimitReached: return QStringLiteral("retry_limit_reached");
    case TrackerDeliveryReason::DestinationReviewRequired:
        return QStringLiteral("destination_review_required");
    }
    return QStringLiteral("unknown");
}

QString titleMatchMediaType(const QString &historyKind)
{
    if (historyKind == QLatin1String("video"))
        return QStringLiteral("Video");
    if (historyKind == QLatin1String("book"))
        return QStringLiteral("Book");
    if (historyKind == QLatin1String("manga"))
        return QStringLiteral("Manga");
    return QStringLiteral("Colosseum Progress");
}

QString scrobbleStateKey(TrackerScrobbleState value)
{
    switch (value) {
    case TrackerScrobbleState::Pending: return QStringLiteral("waiting");
    case TrackerScrobbleState::Delivering: return QStringLiteral("syncing");
    case TrackerScrobbleState::Succeeded: return QStringLiteral("confirmed");
    case TrackerScrobbleState::Waiting: return QStringLiteral("waiting");
    case TrackerScrobbleState::UnknownOutcome: return QStringLiteral("checking_delivery");
    case TrackerScrobbleState::NeedsAttention: return QStringLiteral("needs_attention");
    case TrackerScrobbleState::Superseded: return QStringLiteral("superseded");
    }
    return QStringLiteral("unknown");
}

QString scrobbleReasonKey(TrackerScrobbleReason value)
{
    switch (value) {
    case TrackerScrobbleReason::None: return QStringLiteral("none");
    case TrackerScrobbleReason::ProviderUnavailable: return QStringLiteral("provider_unavailable");
    case TrackerScrobbleReason::RetryableKnownNotApplied: return QStringLiteral("provider_retry");
    case TrackerScrobbleReason::RetryLimitReached: return QStringLiteral("retry_limit_reached");
    case TrackerScrobbleReason::AcknowledgementLost: return QStringLiteral("acknowledgement_lost");
    case TrackerScrobbleReason::UnsupportedAction: return QStringLiteral("unsupported_action");
    case TrackerScrobbleReason::MappingChanged: return QStringLiteral("title_match_changed");
    case TrackerScrobbleReason::StalePlayback: return QStringLiteral("playback_session_ended");
    case TrackerScrobbleReason::ReadbackPresent: return QStringLiteral("confirmed_by_readback");
    case TrackerScrobbleReason::ReadbackAbsent: return QStringLiteral("provider_did_not_confirm");
    case TrackerScrobbleReason::ReadbackUncertain: return QStringLiteral("delivery_uncertain");
    case TrackerScrobbleReason::UserDiscarded: return QStringLiteral("discarded_by_user");
    }
    return QStringLiteral("unknown");
}

QString connectionStateKey(TrackerConnectionState value)
{
    switch (value) {
    case TrackerConnectionState::Connected: return QStringLiteral("connected");
    case TrackerConnectionState::Disconnected: return QStringLiteral("disconnected");
    case TrackerConnectionState::TransferPending: return QStringLiteral("transfer_pending");
    }
    return QStringLiteral("unknown");
}

QString safePreviewTitle(QString value)
{
    QString safe;
    safe.reserve(qMin(value.size(), 256));
    bool previousWasSpace = true;
    for (QChar character : value) {
        if (character.isSpace() || character.isNull() || character.category() == QChar::Other_Control) {
            if (!previousWasSpace && safe.size() < 256) {
                safe.append(QLatin1Char(' '));
                previousWasSpace = true;
            }
            continue;
        }
        if (safe.size() >= 256)
            break;
        safe.append(character);
        previousWasSpace = false;
    }
    safe = safe.trimmed();
    return safe.isEmpty() ? QStringLiteral("Untitled media") : safe;
}

QString safeRemoteExportSummary(const QString &value)
{
    // The adapter supplies display text, never its opaque state fingerprint or ID.
    // Keep the public review bounded even if an adapter returns malformed text.
    QString safe;
    safe.reserve(qMin(value.size(), 96));
    bool previousWasSpace = true;
    for (QChar character : value) {
        if (character.isSpace()) {
            if (!previousWasSpace && safe.size() < 96) {
                safe.append(QLatin1Char(' '));
                previousWasSpace = true;
            }
        } else if ((character.isLetterOrNumber() || QStringLiteral(".,:;+-()%").contains(character))
                   && safe.size() < 96) {
            safe.append(character);
            previousWasSpace = false;
        } else {
            return QStringLiteral("Existing tracker state");
        }
        if (safe.size() >= 96)
            break;
    }
    safe = safe.trimmed();
    return safe.isEmpty() ? QStringLiteral("Existing tracker state") : safe;
}

QString safeRemovalConsequence(const QVariantMap &source)
{
    const QString consequence = source.value(QStringLiteral("consequence")).toString();
    if (consequence == QLatin1String(
            "Other tracker data remains; local Progress stays preserved.")
        || consequence == QLatin1String("Your local Progress will be restored.")
        || consequence == QLatin1String("This tracker-imported Progress will be removed.")) {
        return consequence;
    }
    return QStringLiteral(
        "Only this account's tracker-imported Progress is affected; local Progress and other tracker data are preserved.");
}

QStringList capabilityKeys(TrackerProviderCapabilities capabilities)
{
    QStringList values;
    if (capabilities.testFlag(TrackerProviderCapability::ReadHistory))
        values.append(QStringLiteral("read_history"));
    if (capabilities.testFlag(TrackerProviderCapability::ReadProgress))
        values.append(QStringLiteral("read_progress"));
    if (capabilities.testFlag(TrackerProviderCapability::WriteProgress))
        values.append(QStringLiteral("write_progress"));
    if (capabilities.testFlag(TrackerProviderCapability::WriteCompletion))
        values.append(QStringLiteral("write_completion"));
    if (capabilities.testFlag(TrackerProviderCapability::Scrobble))
        values.append(QStringLiteral("scrobble"));
    return values;
}

QStringList allowedImportChoices(const TrackerImportItem &item,
                                 bool findMatchAvailable)
{
    if (item.state != TrackerImportItemState::ReviewRequired)
        return {};
    switch (item.classification) {
    case TrackerImportClassification::NeedsMatching: {
        QStringList choices{QStringLiteral("leave_unmatched")};
        if (findMatchAvailable)
            choices.prepend(QStringLiteral("find_match"));
        return choices;
    }
    case TrackerImportClassification::Disagreement:
        return item.remote.contradictsNativeHistory
            ? QStringList{QStringLiteral("keep_colosseum")}
            : QStringList{QStringLiteral("keep_colosseum"),
                          QStringLiteral("use_provider_progress")};
    case TrackerImportClassification::NewProgress:
    case TrackerImportClassification::RemoteAdvance:
        return {QStringLiteral("use_provider_progress"),
                QStringLiteral("leave_unresolved")};
    case TrackerImportClassification::ExactMatch:
    case TrackerImportClassification::Unsupported:
    case TrackerImportClassification::Duplicate:
        return {};
    }
    return {};
}

QJsonObject progressFingerprint(const std::optional<TrackerImportedProgressValue> &value)
{
    if (!value)
        return {{QStringLiteral("present"), false}};
    QJsonObject target{{QStringLiteral("present"), value->exactProgressTarget.has_value()}};
    if (value->exactProgressTarget) {
        target.insert(QStringLiteral("canonicalMediaId"),
                      value->exactProgressTarget->canonicalMediaId);
        target.insert(QStringLiteral("kind"), value->exactProgressTarget->kind);
        target.insert(QStringLiteral("id"), value->exactProgressTarget->id);
        target.insert(QStringLiteral("fraction"), value->exactProgressTarget->fraction);
        target.insert(QStringLiteral("completed"), value->exactProgressTarget->completed);
    }
    return {{QStringLiteral("present"), true},
            {QStringLiteral("canonicalMediaId"), value->canonicalMediaId},
            {QStringLiteral("historyKind"), value->historyKind},
            {QStringLiteral("historyId"), value->historyId},
            {QStringLiteral("progress"), value->progress},
            {QStringLiteral("completed"), value->completed},
            {QStringLiteral("revision"), QString::number(value->revision)},
            {QStringLiteral("nativeWitnessedHistory"), value->nativeWitnessedHistory},
            {QStringLiteral("ownerRevisionToken"), value->ownerRevisionToken},
            {QStringLiteral("target"), target}};
}

QJsonObject mappingFingerprint(const std::optional<TrackerTitleMapping> &value)
{
    if (!value)
        return {{QStringLiteral("present"), false}};
    return {{QStringLiteral("present"), true},
            {QStringLiteral("provider"), trackerProviderKey(value->remote.providerId)},
            {QStringLiteral("account"), value->remote.remoteAccountId},
            {QStringLiteral("remoteMedia"), value->remote.remoteMediaId},
            {QStringLiteral("canonicalMedia"), value->canonical.canonicalMediaId},
            {QStringLiteral("historyKind"), value->canonical.historyKind},
            {QStringLiteral("historyId"), value->canonical.historyId},
            {QStringLiteral("displayName"), value->canonical.displayName},
            {QStringLiteral("provenance"),
             value->provenance == TrackerMappingProvenance::ExactProviderIdentity
                 ? QStringLiteral("exact") : QStringLiteral("user")},
            {QStringLiteral("revision"), QString::number(value->revision)}};
}

QJsonObject targetFingerprint(
    const std::optional<TrackerImportProgressTarget> &value)
{
    if (!value)
        return {{QStringLiteral("present"), false}};
    return {{QStringLiteral("present"), true},
            {QStringLiteral("canonicalMediaId"), value->canonicalMediaId},
            {QStringLiteral("kind"), value->kind},
            {QStringLiteral("id"), value->id},
            {QStringLiteral("fraction"), value->fraction},
            {QStringLiteral("completed"), value->completed}};
}

} // namespace

TrackerSyncCenterModel::TrackerSyncCenterModel(QObject *parent)
    : TrackerSyncCenterModel(nullptr, nullptr, nullptr, nullptr, nullptr,
                             trackerBuiltInProviderCatalog(), {}, {}, {}, nullptr, {}, parent)
{}

TrackerSyncCenterModel::TrackerSyncCenterModel(
    TrackerConnectionStore *connections,
    TrackerImportStore *imports,
    TrackerDeliveryStore *delivery,
    TrackerScrobbleStore *scrobble,
    TrackerSyncSettingsStore *settings,
    const QList<TrackerProviderDescriptor> &catalogueValue,
    LivePlaybackPreferenceAction livePlaybackAction,
    ResumeAfterSyncAction resumeAfterSync,
    DisconnectAction disconnectAction,
    TrackerHistoryEvidenceStore *historyEvidence,
    RefreshDeliveryFactsAction refreshDeliveryFacts,
    QObject *parent)
    : QObject(parent),
      m_connections(connections),
      m_imports(imports),
      m_historyEvidence(historyEvidence),
      m_delivery(delivery),
      m_scrobble(scrobble),
      m_settings(settings),
      m_catalogue(catalogueValue),
      m_livePlaybackAction(std::move(livePlaybackAction)),
      m_resumeAfterSync(std::move(resumeAfterSync)),
      m_disconnectAction(std::move(disconnectAction)),
      m_refreshDeliveryFacts(std::move(refreshDeliveryFacts)),
      m_profileAvailable(connections && imports && delivery && scrobble && settings)
{
    m_lastFingerprint = snapshotFingerprint();
}

quint64 TrackerSyncCenterModel::revision()
{
    refreshRevision();
    return m_revision;
}

QVariantList TrackerSyncCenterModel::connectedTrackers()
{
    refreshRevision();
    return buildConnectedTrackers();
}

QVariantList TrackerSyncCenterModel::catalogue()
{
    refreshRevision();
    return buildCatalogue();
}

QVariantList TrackerSyncCenterModel::importReviews()
{
    refreshRevision();
    return buildImportReviews();
}

QVariantMap TrackerSyncCenterModel::globalSettings()
{
    refreshRevision();
    return buildGlobalSettings();
}

QVariantMap TrackerSyncCenterModel::aggregateState()
{
    refreshRevision();
    return buildAggregateState();
}

QVariantMap TrackerSyncCenterModel::lastActionResult() const
{
    return m_lastActionResult;
}

QVariantMap TrackerSyncCenterModel::providerDossier(const QString &providerKey)
{
    refreshRevision();
    const auto providerId = trackerProviderIdFromKey(providerKey);
    if (!providerId)
        return {{QStringLiteral("found"), false},
                {QStringLiteral("revision"), QVariant::fromValue(m_revision)}};

    const TrackerProviderDescriptor *provider = descriptor(*providerId);
    if (!provider)
        return {{QStringLiteral("found"), false},
                {QStringLiteral("revision"), QVariant::fromValue(m_revision)}};

    const auto current = m_connections ? m_connections->connection(*providerId) : std::nullopt;
    const bool isConnected = current && current->state != TrackerConnectionState::Disconnected;
    const TrackerProviderCapabilities capabilities = current
        ? effectiveCapabilities(*current) : provider->capabilities;
    const bool hasPull = capabilities.testFlag(TrackerProviderCapability::ReadHistory)
        || capabilities.testFlag(TrackerProviderCapability::ReadProgress);
    const bool hasSend = capabilities.testFlag(TrackerProviderCapability::WriteProgress)
        || capabilities.testFlag(TrackerProviderCapability::WriteCompletion);
    const bool importReviewed = current && firstImportReviewed(*current);
    const bool exportReviewed = current && m_delivery
        && m_delivery->hasFirstExportConsent(*providerId, current->remoteAccountId);
    const auto global = m_settings ? m_settings->globalSettings() : TrackerGlobalSyncSettings{};
    const bool pullAutomatically = current && m_settings
        && m_settings->pullAutomatically(*providerId, current->remoteAccountId, importReviewed);
    const bool sendEnabled = current && m_delivery
        && m_delivery->providerSendEnabled(*providerId, current->remoteAccountId);
    const bool liveEnabled = current && m_scrobble
        && m_scrobble->enabled(*providerId, current->remoteAccountId);
    const ProviderCounts counts = current ? providerCounts(*current) : ProviderCounts{};
    const int historyCount = current ? importedHistoryCount(*current) : 0;
    const int progressCount = current ? importedProgressCount(*current) : 0;
    const QVariantList removalPreview = current
        ? importedDataRemovalPreview(*current) : QVariantList{};

    return {{QStringLiteral("found"), true},
            {QStringLiteral("revision"), QVariant::fromValue(m_revision)},
            {QStringLiteral("providerKey"), trackerProviderKey(*providerId)},
            {QStringLiteral("providerName"), provider->displayName},
            {QStringLiteral("available"), provider->available},
            {QStringLiteral("status"), current
                    ? providerState(*current, counts)
                    : (provider->available ? QStringLiteral("Not connected")
                                           : QStringLiteral("Unavailable in this build"))},
            {QStringLiteral("connected"), isConnected},
            {QStringLiteral("accountLabel"), isConnected
                    ? QStringLiteral("Connected account") : QString()},
            {QStringLiteral("capabilities"), capabilityKeys(capabilities)},
            {QStringLiteral("lastSuccessfulSyncAtMs"), current && m_settings
                    ? m_settings->lastSuccessfulSyncAtMs(*providerId, current->remoteAccountId)
                    : 0},
            {QStringLiteral("waitingCount"), counts.waiting},
            {QStringLiteral("pendingCount"), counts.pending},
            {QStringLiteral("unresolvedCount"), counts.unresolved},
            {QStringLiteral("knownUnsentCount"), counts.knownUnsent},
            {QStringLiteral("unknownOutcomeCount"), counts.unknownOutcome},
            {QStringLiteral("importedHistoryCount"), historyCount},
            {QStringLiteral("importedProgressCount"), progressCount},
            {QStringLiteral("importedDataRemovalPreview"), removalPreview},
            {QStringLiteral("pullAutomatically"), pullAutomatically},
            {QStringLiteral("pullSettingEnabled"), isConnected && provider->available
                    && importReviewed && hasPull},
            {QStringLiteral("sendProgressEnabled"), sendEnabled},
            {QStringLiteral("sendSettingEnabled"), isConnected && provider->available
                    && exportReviewed && hasSend},
            {QStringLiteral("exportReviewed"), exportReviewed},
            {QStringLiteral("exportReviewEnabled"), isConnected && provider->available
                    && hasSend && !exportReviewed && ownerHealthy()
                    && m_exportSource && m_exportSource->isReady()
                    && static_cast<bool>(m_readExportSnapshot)},
            {QStringLiteral("livePlaybackTrackingEnabled"), liveEnabled},
            {QStringLiteral("livePlaybackSettingEnabled"), isConnected && provider->available
                    && global.trackerSyncEnabled
                    && capabilities.testFlag(TrackerProviderCapability::Scrobble)},
            // No verified native authentication action is composed in the
            // production runtime yet. Descriptor availability alone must not
            // turn the visible Connect control into a dead enabled button.
            {QStringLiteral("connectEnabled"), false},
            {QStringLiteral("pendingWork"), current
                    && current->state == TrackerConnectionState::Disconnected
                    && (counts.pending > 0 || counts.unresolved > 0)},
            {QStringLiteral("syncEnabled"), isConnected && provider->available
                    && global.trackerSyncEnabled && ownerHealthy()
                    && syncAllHandlerAvailable()},
            {QStringLiteral("disconnectEnabled"), current
                    && current->state == TrackerConnectionState::Connected
                    && counts.syncing == 0 && ownerHealthy()
                    && static_cast<bool>(m_disconnectAction)},
            {QStringLiteral("cleanupEnabled"), current
                    && current->state == TrackerConnectionState::Disconnected
                    && counts.knownUnsent > 0 && counts.syncing == 0
                    && ownerHealthy() && static_cast<bool>(m_disconnectAction)},
            {QStringLiteral("removeImportedEnabled"), current
                    && (historyCount > 0 || progressCount > 0)
                    && removalPreview.size() == historyCount + progressCount
                    && !m_importedDataRemovalPending && profileStoresHealthy()
                    && (!m_historyEvidence || m_historyEvidence->healthy())
                    && m_importOwner},
            {QStringLiteral("removeImportedPending"), m_importedDataRemovalPending},
            {QStringLiteral("disconnectInFlight"), counts.syncing > 0}};
}

QVariantMap TrackerSyncCenterModel::diagnoseRoute(const QString &providerKey)
{
    refreshRevision();
    const auto rejected = [](const QString &code) {
        return QVariantMap{{QStringLiteral("accepted"), false},
                           {QStringLiteral("code"), code}};
    };
    const auto providerId = trackerProviderIdFromKey(providerKey);
    if (!providerId || trackerProviderKey(*providerId) != providerKey)
        return rejected(QStringLiteral("provider_unavailable"));
    if (!ownerHealthy() || !m_delivery || !m_scrobble)
        return rejected(QStringLiteral("owner_unavailable"));

    TrackerConnection connection;
    const TrackerProviderDescriptor *provider = descriptor(*providerId);
    if (!provider || !provider->available
        || !currentConnection(*providerId, &connection)) {
        return rejected(QStringLiteral("connection_unavailable"));
    }

    const auto route = [this, &providerKey](const QString &state,
                                            const QString &reason,
                                            int focusIndex) {
        return QVariantMap{{QStringLiteral("accepted"), true},
                           {QStringLiteral("code"), QStringLiteral("current_issue")},
                           {QStringLiteral("revision"), QVariant::fromValue(m_revision)},
                           {QStringLiteral("providerKey"), providerKey},
                           {QStringLiteral("destination"), QStringLiteral("provider_dossier")},
                           {QStringLiteral("focusTarget"), QStringLiteral("delivery_status")},
                           {QStringLiteral("state"), state},
                           {QStringLiteral("reason"), reason},
                           {QStringLiteral("focusIndex"), focusIndex}};
    };

    int focusIndex = 0;
    for (const TrackerDeliveryOperation &operation : m_delivery->operations()) {
        if (operation.providerId != *providerId
            || operation.remoteAccountId != connection.remoteAccountId
            || operation.state == TrackerDeliveryState::Succeeded) {
            continue;
        }
        const bool actionable = operation.state == TrackerDeliveryState::UnknownOutcome
            || operation.state == TrackerDeliveryState::NeedsAttention
            || operation.state == TrackerDeliveryState::FailedTerminal
            || (operation.state == TrackerDeliveryState::Retrying
                && operation.reason == TrackerDeliveryReason::ProviderRetryable);
        if (operation.connectionGeneration == connection.connectionGeneration && actionable) {
            return route(deliveryStateKey(operation.state),
                         deliveryReasonKey(operation.reason), focusIndex);
        }
        ++focusIndex;
    }

    for (const TrackerScrobbleIntent &intent : m_scrobble->intents()) {
        if (intent.providerId != *providerId
            || intent.remoteAccountId != connection.remoteAccountId
            || intent.state == TrackerScrobbleState::Succeeded
            || intent.state == TrackerScrobbleState::Superseded) {
            continue;
        }
        const bool actionable = intent.state == TrackerScrobbleState::UnknownOutcome
            || intent.state == TrackerScrobbleState::NeedsAttention
            || (intent.state == TrackerScrobbleState::Waiting
                && (intent.reason == TrackerScrobbleReason::ProviderUnavailable
                    || intent.reason == TrackerScrobbleReason::RetryableKnownNotApplied));
        if (intent.connectionGeneration == connection.connectionGeneration && actionable) {
            return route(scrobbleStateKey(intent.state),
                         scrobbleReasonKey(intent.reason), focusIndex);
        }
        ++focusIndex;
    }
    return rejected(QStringLiteral("no_current_issue"));
}

QVariantMap TrackerSyncCenterModel::importReviewSnapshot(const QString &batchId,
                                                        quint64 expectedRevision)
{
    refreshRevision();
    if (expectedRevision != m_revision)
        return {{QStringLiteral("accepted"), false},
                {QStringLiteral("code"), QStringLiteral("stale_intent")},
                {QStringLiteral("revision"), QVariant::fromValue(m_revision)},
                {QStringLiteral("items"), QVariantList{}}};
    if (!ownerHealthy() || !m_imports || batchId.isEmpty())
        return {{QStringLiteral("accepted"), false},
                {QStringLiteral("code"), QStringLiteral("owner_unavailable")},
                {QStringLiteral("revision"), QVariant::fromValue(m_revision)},
                {QStringLiteral("items"), QVariantList{}}};
    const auto batchValue = m_imports->batch(batchId);
    if (!batchValue)
        return {{QStringLiteral("accepted"), false},
                {QStringLiteral("code"), QStringLiteral("review_not_found")},
                {QStringLiteral("revision"), QVariant::fromValue(m_revision)},
                {QStringLiteral("items"), QVariantList{}}};
    const auto current = m_connections->connection(batchValue->providerId);
    if (!current || current->state != TrackerConnectionState::Connected
        || current->remoteAccountId != batchValue->remoteAccountId
        || current->connectionGeneration != batchValue->connectionGeneration) {
        return {{QStringLiteral("accepted"), false},
                {QStringLiteral("code"), QStringLiteral("connection_changed")},
                {QStringLiteral("revision"), QVariant::fromValue(m_revision)},
                {QStringLiteral("items"), QVariantList{}}};
    }
    return {{QStringLiteral("accepted"), true},
            {QStringLiteral("code"), QStringLiteral("ready")},
            {QStringLiteral("revision"), QVariant::fromValue(m_revision)},
            {QStringLiteral("batchId"), batchValue->batchId},
            {QStringLiteral("providerKey"), trackerProviderKey(batchValue->providerId)},
            {QStringLiteral("providerName"), trackerProviderDisplayName(batchValue->providerId)},
            {QStringLiteral("initialImport"), batchValue->initialImport},
            {QStringLiteral("pageComplete"), batchValue->pageComplete},
            {QStringLiteral("confirmed"), batchValue->confirmed},
            {QStringLiteral("items"), buildImportRows(*batchValue)}};
}

void TrackerSyncCenterModel::setExportReview(
    TrackerDeliverySource *source, ReadExportSnapshotAction readRemoteSnapshot)
{
    m_exportSource = source;
    m_readExportSnapshot = std::move(readRemoteSnapshot);
    m_exportReview.reset();
    emit modelChanged();
}

QVariantMap TrackerSyncCenterModel::beginExportReview(
    const QString &providerKey, quint64 expectedRevision)
{
    refreshRevision();
    auto rejected = [this](const QString &code) {
        return QVariantMap{{QStringLiteral("accepted"), false},
                           {QStringLiteral("code"), code},
                           {QStringLiteral("revision"), QVariant::fromValue(m_revision)},
                           {QStringLiteral("items"), QVariantList{}}};
    };
    if (expectedRevision != m_revision)
        return rejected(QStringLiteral("stale_intent"));
    const auto providerId = trackerProviderIdFromKey(providerKey);
    const auto *provider = providerId ? descriptor(*providerId) : nullptr;
    if (!provider || !provider->available || !ownerHealthy() || !m_exportSource
        || !m_exportSource->isReady() || !m_readExportSnapshot)
        return rejected(QStringLiteral("export_review_unavailable"));
    const auto connection = m_connections->connection(*providerId);
    if (!connection || connection->state != TrackerConnectionState::Connected)
        return rejected(QStringLiteral("connection_changed"));
    const auto capabilities = effectiveCapabilities(*connection);
    if (!capabilities.testFlag(TrackerProviderCapability::WriteProgress)
        && !capabilities.testFlag(TrackerProviderCapability::WriteCompletion))
        return rejected(QStringLiteral("provider_unavailable"));
    if (m_delivery->hasFirstExportConsent(*providerId, connection->remoteAccountId))
        return rejected(QStringLiteral("already_reviewed"));

    const QList<TrackerDeliveryFact> facts = m_exportSource->currentCommittedFacts();
    QString error;
    const auto remote = m_readExportSnapshot(*connection, facts, &error);
    if (!remote)
        return rejected(QStringLiteral("remote_snapshot_unavailable"));
    const auto preview = m_delivery->createExportPreview(*providerId,
        connection->remoteAccountId, connection->connectionGeneration,
        facts, *remote, m_exportSource, &error);
    if (!preview)
        return rejected(QStringLiteral("export_preview_changed"));

    ExportReviewHandle handle;
    handle.publicId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    handle.privatePreviewId = preview->previewId;
    handle.providerId = *providerId;
    handle.remoteAccountId = connection->remoteAccountId;
    handle.generation = connection->connectionGeneration;
    handle.revision = m_revision;
    QVariantList rows;
    int eligibleCount = 0;
    for (const TrackerExportPreviewItem &item : preview->items) {
        const QString publicItemId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        handle.publicToPrivateItemId.insert(publicItemId, item.itemId);
        if (item.eligible)
            ++eligibleCount;
        QString reason;
        switch (item.reason) {
        case TrackerExportIneligibleReason::None: reason = QStringLiteral("Ready to send"); break;
        case TrackerExportIneligibleReason::SourceChanged: reason = QStringLiteral("Local progress changed"); break;
        case TrackerExportIneligibleReason::NonNativeOrigin: reason = QStringLiteral("Imported progress stays in Colosseum"); break;
        case TrackerExportIneligibleReason::MissingMapping: reason = QStringLiteral("No exact title match"); break;
        case TrackerExportIneligibleReason::AmbiguousMapping: reason = QStringLiteral("Title match needs attention"); break;
        case TrackerExportIneligibleReason::Unsupported: reason = QStringLiteral("Provider does not support this item"); break;
        case TrackerExportIneligibleReason::RemoteAlreadyCurrent: reason = QStringLiteral("Already current on tracker"); break;
        }
        rows.append(QVariantMap{
            {QStringLiteral("itemId"), publicItemId},
            {QStringLiteral("title"), item.mapping
                    ? safePreviewTitle(item.mapping->canonical.displayName)
                    : QStringLiteral("Colosseum item")},
            {QStringLiteral("kind"), item.fact.kind == TrackerDeliveryFactKind::Progress
                    ? QStringLiteral("Progress") : QStringLiteral("Completion")},
            {QStringLiteral("progress"), item.fact.progress},
            {QStringLiteral("eligible"), item.eligible},
            {QStringLiteral("remotePresent"), item.remotePresent},
            {QStringLiteral("willChangeRemote"), item.willChangeRemote},
            {QStringLiteral("remoteBefore"), item.remotePresent
                    ? safeRemoteExportSummary(item.remoteStateSummary)
                    : QStringLiteral("No tracker state")},
            {QStringLiteral("remoteAfter"), item.fact.kind == TrackerDeliveryFactKind::Progress
                    ? QStringLiteral("Progress: %1").arg(item.fact.progress)
                    : QStringLiteral("Completed")},
            {QStringLiteral("reason"), reason}});
    }
    m_exportReview = std::move(handle);
    return {{QStringLiteral("accepted"), true},
            {QStringLiteral("code"), QStringLiteral("ready")},
            {QStringLiteral("revision"), QVariant::fromValue(m_revision)},
            {QStringLiteral("reviewId"), m_exportReview->publicId},
            {QStringLiteral("providerName"), provider->displayName},
            {QStringLiteral("eligibleCount"), eligibleCount},
            {QStringLiteral("items"), rows}};
}

bool TrackerSyncCenterModel::confirmExportReview(
    const QString &reviewId, const QStringList &selectedItemIds,
    quint64 expectedRevision)
{
    const QString action = QStringLiteral("confirm_export_review");
    if (!acceptIntent(expectedRevision, action)) {
        m_exportReview.reset();
        return false;
    }
    if (!m_exportReview || m_exportReview->publicId != reviewId
        || m_exportReview->revision != m_revision)
        return finishIntent(false, action, QStringLiteral("review_changed"));
    if (!ownerHealthy() || !m_exportSource || !m_exportSource->isReady()
        || !m_readExportSnapshot) {
        m_exportReview.reset();
        return finishIntent(false, action, QStringLiteral("owner_unavailable"));
    }
    const ExportReviewHandle handle = *m_exportReview;
    m_exportReview.reset(); // A confirmation attempt consumes this review.
    const auto connection = m_connections->connection(handle.providerId);
    const auto *provider = descriptor(handle.providerId);
    if (!provider || !provider->available || !connection
        || connection->state != TrackerConnectionState::Connected
        || connection->remoteAccountId != handle.remoteAccountId
        || connection->connectionGeneration != handle.generation)
        return finishIntent(false, action, QStringLiteral("connection_changed"));
    QStringList privateIds;
    QSet<QString> seen;
    for (const QString &publicId : selectedItemIds) {
        if (seen.contains(publicId) || !handle.publicToPrivateItemId.contains(publicId))
            return finishIntent(false, action, QStringLiteral("selection_changed"));
        seen.insert(publicId);
        privateIds.append(handle.publicToPrivateItemId.value(publicId));
    }
    if (privateIds.isEmpty())
        return finishIntent(false, action, QStringLiteral("selection_required"));
    const QList<TrackerDeliveryFact> currentFacts = m_exportSource->currentCommittedFacts();
    QString error;
    const auto currentRemote = m_readExportSnapshot(*connection, currentFacts, &error);
    if (!currentRemote)
        return finishIntent(false, action, QStringLiteral("remote_snapshot_unavailable"));
    if (!m_delivery->confirmExport(handle.privatePreviewId, privateIds,
                                   *currentRemote, m_exportSource,
                                   QDateTime::currentMSecsSinceEpoch(), &error))
        return finishIntent(false, action, QStringLiteral("export_review_changed"));
    return finishIntent(true, action, QStringLiteral("confirmed"));
}

void TrackerSyncCenterModel::setTitleMatching(
    TrackerMappingStore *store, const TrackerCanonicalTitleIndex *index)
{
    m_titleMatchStore = store;
    m_titleIndex = index;
    m_titleMatchCandidateHandles.clear();
    m_exportReview.reset();
    refreshRevision();
    emit modelChanged();
}

QVariantList TrackerSyncCenterModel::titleMatchCandidates(
    const QString &batchId,
    const QString &reviewItemId,
    const QString &searchText,
    quint64 expectedRevision)
{
    refreshRevision();
    if (expectedRevision != m_revision || !titleMatchAvailable()
        || !m_imports || !m_connections || batchId.isEmpty() || reviewItemId.isEmpty()) {
        return {};
    }

    const auto batchValue = m_imports->batch(batchId);
    if (!batchValue)
        return {};
    const auto connection = m_connections->connection(batchValue->providerId);
    if (!connection || connection->state != TrackerConnectionState::Connected
        || connection->remoteAccountId != batchValue->remoteAccountId
        || connection->connectionGeneration != batchValue->connectionGeneration) {
        return {};
    }

    const auto publicItem = m_publicToPrivateImportItemId.constFind(reviewItemId);
    if (publicItem == m_publicToPrivateImportItemId.cend()
        || publicItem->first != batchId) {
        return {};
    }
    const auto item = std::find_if(batchValue->items.cbegin(), batchValue->items.cend(),
        [&publicItem](const TrackerImportItem &candidate) {
            return candidate.itemId == publicItem->second;
        });
    if (item == batchValue->items.cend()
        || item->classification != TrackerImportClassification::NeedsMatching
        || item->state != TrackerImportItemState::ReviewRequired
        || item->remote.mapping) {
        return {};
    }

    QString query;
    query.reserve(qMin(searchText.size(), 120));
    for (const QChar character : searchText) {
        if (character.isNull() || character.category() == QChar::Other_Control)
            continue;
        if (query.size() >= 120)
            break;
        query.append(character);
    }
    query = query.trimmed();

    m_titleMatchCandidateHandles.clear();
    QVariantList result;
    const QList<TrackerCanonicalTitleCandidate> candidates =
        TrackerExactMappingService(m_titleMatchStore, m_titleIndex).findMatchCandidates();
    result.reserve(qMin(50, candidates.size()));
    for (const TrackerCanonicalTitleCandidate &candidate : candidates) {
        if (!query.isEmpty()
            && !candidate.displayName.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        const QString publicCandidateId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_titleMatchCandidateHandles.insert(publicCandidateId,
            TitleMatchHandle{batchId, publicItem->second, item->remote.remote.remoteMediaId,
                             candidate.canonicalMediaId, m_revision});
        result.append(QVariantMap{
            {QStringLiteral("candidateId"), publicCandidateId},
            {QStringLiteral("displayName"), candidate.displayName},
            {QStringLiteral("mediaType"), titleMatchMediaType(candidate.historyKind)},
            {QStringLiteral("displayContext"), candidate.displayContext}});
        if (result.size() >= 50)
            break;
    }
    return result;
}

bool TrackerSyncCenterModel::confirmTitleMatch(
    const QString &batchId,
    const QString &reviewItemId,
    const QString &candidateId,
    quint64 expectedRevision)
{
    const QString action = QStringLiteral("confirm_title_match");
    if (!acceptIntent(expectedRevision, action))
        return false;
    if (!titleMatchAvailable() || !m_imports || !m_connections)
        return finishIntent(false, action, QStringLiteral("owner_unavailable"));

    const auto handle = m_titleMatchCandidateHandles.constFind(candidateId);
    if (handle == m_titleMatchCandidateHandles.cend()
        || handle->revision != m_revision || handle->batchId != batchId) {
        return finishIntent(false, action, QStringLiteral("candidate_unavailable"));
    }
    const auto batchValue = m_imports->batch(batchId);
    if (!batchValue)
        return finishIntent(false, action, QStringLiteral("review_not_found"));
    const auto connection = m_connections->connection(batchValue->providerId);
    if (!connection || connection->state != TrackerConnectionState::Connected
        || connection->remoteAccountId != batchValue->remoteAccountId
        || connection->connectionGeneration != batchValue->connectionGeneration) {
        return finishIntent(false, action, QStringLiteral("connection_changed"));
    }
    const auto publicItem = m_publicToPrivateImportItemId.constFind(reviewItemId);
    if (publicItem == m_publicToPrivateImportItemId.cend()
        || publicItem->first != batchId || publicItem->second != handle->itemId) {
        return finishIntent(false, action, QStringLiteral("review_item_not_found"));
    }
    const auto item = std::find_if(batchValue->items.cbegin(), batchValue->items.cend(),
        [&handle](const TrackerImportItem &candidate) {
            return candidate.itemId == handle->itemId;
        });
    if (item == batchValue->items.cend()
        || item->classification != TrackerImportClassification::NeedsMatching
        || item->state != TrackerImportItemState::ReviewRequired
        || item->remote.mapping
        || item->remote.remote.remoteMediaId != handle->remoteMediaId) {
        return finishIntent(false, action, QStringLiteral("review_changed"));
    }

    const auto priorMapping = m_titleMatchStore->mapping(item->remote.remote);
    if (priorMapping
        && priorMapping->canonical.canonicalMediaId != handle->canonicalMediaId) {
        return finishIntent(false, action, QStringLiteral("mapping_changed"));
    }
    auto confirmedMapping = priorMapping;
    if (!confirmedMapping) {
        QString mappingError;
        TrackerExactMappingService mappingService(m_titleMatchStore, m_titleIndex);
        if (!mappingService.confirmCandidate(item->remote.remote,
                                             handle->canonicalMediaId, &mappingError)) {
            return finishIntent(false, action, QStringLiteral("candidate_unavailable"));
        }
        confirmedMapping = m_titleMatchStore->mapping(item->remote.remote);
    }
    if (!confirmedMapping
        || (confirmedMapping->provenance != TrackerMappingProvenance::UserConfirmed
            && !priorMapping)) {
        return finishIntent(false, action, QStringLiteral("mapping_unavailable"));
    }

    TrackerImportBatchDraft draft;
    draft.providerId = batchValue->providerId;
    draft.remoteAccountId = batchValue->remoteAccountId;
    draft.connectionGeneration = batchValue->connectionGeneration;
    draft.snapshotId = batchValue->snapshotId;
    draft.proposedCursor = batchValue->proposedCursor;
    draft.initialImport = batchValue->initialImport;
    draft.pageComplete = batchValue->pageComplete;
    draft.baseCursor = batchValue->baseCursor;
    draft.items.reserve(batchValue->items.size());
    for (const TrackerImportItem &existingItem : batchValue->items) {
        TrackerImportRemoteItem remote = existingItem.remote;
        if (existingItem.itemId == handle->itemId)
            remote.mapping = confirmedMapping;
        draft.items.append(std::move(remote));
    }
    QString previewError;
    if (!m_imports->createPreview(draft, &previewError)) {
        if (!priorMapping) {
            QString rollbackError;
            if (m_titleMatchStore->remove(item->remote.remote, &rollbackError))
                return finishIntent(false, action,
                                    QStringLiteral("preview_refresh_failed_mapping_rolled_back"));
        }
        const auto retained = m_titleMatchStore->mapping(item->remote.remote);
        const bool sameConfirmedMapping = retained
            && retained->canonical.canonicalMediaId == handle->canonicalMediaId
            && retained->provenance == TrackerMappingProvenance::UserConfirmed;
        return finishIntent(false, action,
            sameConfirmedMapping || priorMapping
                ? QStringLiteral("preview_refresh_failed_mapping_retained")
                : QStringLiteral("preview_refresh_failed"));
    }
    m_titleMatchCandidateHandles.clear();
    return finishIntent(true, action, QStringLiteral("saved"));
}

QVariantList TrackerSyncCenterModel::deliveryRows(const QString &providerKey)
{
    refreshRevision();
    QVariantList rows;
    const auto providerId = trackerProviderIdFromKey(providerKey);
    if (!ownerHealthy() || !providerId || !m_connections || !m_delivery)
        return rows;
    const auto current = m_connections->connection(*providerId);
    if (!current)
        return rows;
    for (const TrackerDeliveryOperation &operation : m_delivery->operations()) {
        if (operation.providerId != *providerId
            || operation.remoteAccountId != current->remoteAccountId
            || operation.state == TrackerDeliveryState::Succeeded) {
            continue;
        }
        rows.append(QVariantMap{
            {QStringLiteral("operationId"), operation.operationId},
            {QStringLiteral("title"), operation.mapping.canonical.displayName},
            {QStringLiteral("state"), deliveryStateKey(operation.state)},
            {QStringLiteral("reason"), deliveryReasonKey(operation.reason)},
            {QStringLiteral("progress"), operation.fact.progress},
            {QStringLiteral("attemptCount"), operation.attemptCount},
            {QStringLiteral("nextAttemptAtMs"), operation.nextAttemptAtMs}});
    }
    for (const TrackerScrobbleIntent &intent : m_scrobble->intents()) {
        if (intent.providerId != *providerId || intent.remoteAccountId != current->remoteAccountId
            || intent.state == TrackerScrobbleState::Succeeded
            || intent.state == TrackerScrobbleState::Superseded) {
            continue;
        }
        rows.append(QVariantMap{
            {QStringLiteral("operationId"), intent.operationId},
            {QStringLiteral("title"), QString()},
            {QStringLiteral("state"), scrobbleStateKey(intent.state)},
            {QStringLiteral("reason"), scrobbleReasonKey(intent.reason)},
            {QStringLiteral("progress"), intent.progressHundredths},
            {QStringLiteral("attemptCount"), intent.attemptCount},
            {QStringLiteral("nextAttemptAtMs"), 0}});
    }
    return rows;
}

void TrackerSyncCenterModel::refresh()
{
    refreshRevision();
}

bool TrackerSyncCenterModel::setGlobalSetting(const QString &key,
                                              bool enabled,
                                              quint64 expectedRevision)
{
    if (!acceptIntent(expectedRevision, QStringLiteral("set_global_setting")))
        return false;
    if (!ownerHealthy() || !m_settings)
        return finishIntent(false, QStringLiteral("set_global_setting"),
                           QStringLiteral("owner_unavailable"));
    if (key != QLatin1String("trackerSyncEnabled")
        && key != QLatin1String("checkOnLaunch")
        && key != QLatin1String("backgroundDelivery")
        && key != QLatin1String("completionMessages")) {
        return finishIntent(false, QStringLiteral("set_global_setting"),
                            QStringLiteral("unsupported_setting"));
    }
    const bool wasEnabled = m_settings->globalSettings().trackerSyncEnabled;
    if (!m_settings->setGlobalSetting(key, enabled))
        return finishIntent(false, QStringLiteral("set_global_setting"),
                            QStringLiteral("persistence_failed"));
    if (key == QLatin1String("trackerSyncEnabled") && enabled && !wasEnabled
        && m_resumeAfterSync) {
        // The runtime checks/reconciles uncertain outcomes before allowing a
        // later pending event to advance. Persisted queues are never cleared by
        // this setting transition.
        m_resumeAfterSync();
    }
    return finishIntent(true, QStringLiteral("set_global_setting"), QStringLiteral("saved"));
}

bool TrackerSyncCenterModel::setProviderPullAutomatically(
    const QString &providerKey,
    bool enabled,
    quint64 expectedRevision)
{
    const QString action = QStringLiteral("set_pull_automatically");
    if (!acceptIntent(expectedRevision, action))
        return false;
    const auto providerId = trackerProviderIdFromKey(providerKey);
    TrackerConnection connection;
    const TrackerProviderDescriptor *provider = providerId ? descriptor(*providerId) : nullptr;
    if (!ownerHealthy() || !providerId || !provider || !m_settings
        || !currentConnection(*providerId, &connection)) {
        return finishIntent(false, action, QStringLiteral("provider_unavailable"));
    }
    const bool reviewed = firstImportReviewed(connection);
    const TrackerProviderCapabilities capabilities = effectiveCapabilities(connection);
    const bool canRead = capabilities.testFlag(TrackerProviderCapability::ReadHistory)
        || capabilities.testFlag(TrackerProviderCapability::ReadProgress);
    if (enabled && (!provider->available || !reviewed || !canRead))
        return finishIntent(false, action, QStringLiteral("import_review_required"));
    if (!m_settings->setPullAutomatically(*providerId, connection.remoteAccountId, enabled))
        return finishIntent(false, action, QStringLiteral("persistence_failed"));
    return finishIntent(true, action, QStringLiteral("saved"));
}

bool TrackerSyncCenterModel::setProviderSendEnabled(const QString &providerKey,
                                                    bool enabled,
                                                    quint64 expectedRevision)
{
    const QString action = QStringLiteral("set_provider_send");
    if (!acceptIntent(expectedRevision, action))
        return false;
    const auto providerId = trackerProviderIdFromKey(providerKey);
    TrackerConnection connection;
    const TrackerProviderDescriptor *provider = providerId ? descriptor(*providerId) : nullptr;
    if (!ownerHealthy() || !providerId || !provider || !m_delivery
        || !currentConnection(*providerId, &connection)) {
        return finishIntent(false, action, QStringLiteral("provider_unavailable"));
    }
    const TrackerProviderCapabilities capabilities = effectiveCapabilities(connection);
    const bool canWrite = capabilities.testFlag(TrackerProviderCapability::WriteProgress)
        || capabilities.testFlag(TrackerProviderCapability::WriteCompletion);
    if (enabled && (!provider->available || !canWrite
                    || !m_delivery->hasFirstExportConsent(*providerId,
                                                          connection.remoteAccountId))) {
        return finishIntent(false, action, QStringLiteral("export_review_required"));
    }
    if (!enabled && !m_delivery->hasFirstExportConsent(*providerId, connection.remoteAccountId)
        && !m_delivery->providerSendEnabled(*providerId, connection.remoteAccountId)) {
        return finishIntent(true, action, QStringLiteral("already_off"));
    }
    const bool wasEnabled = m_delivery->providerSendEnabled(
        *providerId, connection.remoteAccountId);
    if (enabled && !wasEnabled && !m_refreshDeliveryFacts)
        return finishIntent(false, action, QStringLiteral("delivery_refresh_unavailable"));
    if (!m_delivery->setProviderSendEnabled(*providerId, connection.remoteAccountId, enabled))
        return finishIntent(false, action, QStringLiteral("persistence_failed"));
    if (enabled && !wasEnabled) {
        QString refreshError;
        if (!m_refreshDeliveryFacts(&refreshError)) {
            QString rollbackError;
            m_delivery->setProviderSendEnabled(*providerId, connection.remoteAccountId,
                                               false, &rollbackError);
            return finishIntent(false, action, QStringLiteral("delivery_refresh_failed"));
        }
    }
    return finishIntent(true, action, QStringLiteral("saved"));
}

bool TrackerSyncCenterModel::setLivePlaybackTrackingEnabled(
    const QString &providerKey,
    bool enabled,
    quint64 expectedRevision)
{
    const QString action = QStringLiteral("set_live_playback_tracking");
    if (!acceptIntent(expectedRevision, action))
        return false;
    const auto providerId = trackerProviderIdFromKey(providerKey);
    TrackerConnection connection;
    const TrackerProviderDescriptor *provider = providerId ? descriptor(*providerId) : nullptr;
    if (!ownerHealthy() || !providerId || !provider || !m_scrobble
        || !currentConnection(*providerId, &connection)) {
        return finishIntent(false, action, QStringLiteral("provider_unavailable"));
    }
    const TrackerProviderCapabilities capabilities = effectiveCapabilities(connection);
    if (!capabilities.testFlag(TrackerProviderCapability::Scrobble)
        || (enabled && (!provider->available
                        || !m_settings->globalSettings().trackerSyncEnabled))) {
        return finishIntent(false, action, QStringLiteral("unsupported_action"));
    }
    const bool currentValue = m_scrobble->enabled(*providerId, connection.remoteAccountId);
    if (currentValue == enabled)
        return finishIntent(true, action, QStringLiteral("already_set"));
    if (!m_livePlaybackAction
        || !m_livePlaybackAction(providerKey, enabled)) {
        // In particular, a failed remote-session close must leave the durable
        // preference enabled; the runtime is the authority for that decision.
        return finishIntent(false, action, QStringLiteral("safe_transition_unavailable"));
    }
    return finishIntent(true, action, QStringLiteral("saved"));
}

bool TrackerSyncCenterModel::resolveImportItem(const QString &batchId,
                                               const QString &itemId,
                                               const QString &choice,
                                               quint64 expectedRevision)
{
    const QString action = QStringLiteral("resolve_import_item");
    if (!acceptIntent(expectedRevision, action))
        return false;
    if (!ownerHealthy() || !m_imports || !m_connections)
        return finishIntent(false, action, QStringLiteral("owner_unavailable"));
    const auto batchValue = m_imports->batch(batchId);
    if (!batchValue)
        return finishIntent(false, action, QStringLiteral("review_not_found"));
    const auto current = m_connections->connection(batchValue->providerId);
    if (!current || current->state != TrackerConnectionState::Connected
        || current->remoteAccountId != batchValue->remoteAccountId
        || current->connectionGeneration != batchValue->connectionGeneration) {
        return finishIntent(false, action, QStringLiteral("connection_changed"));
    }
    const auto privateItem = m_publicToPrivateImportItemId.constFind(itemId);
    if (privateItem == m_publicToPrivateImportItemId.cend()
        || privateItem->first != batchId) {
        return finishIntent(false, action, QStringLiteral("review_item_not_found"));
    }
    const QString internalItemId = privateItem->second;
    const auto item = std::find_if(batchValue->items.cbegin(), batchValue->items.cend(),
        [&internalItemId](const TrackerImportItem &value) {
            return value.itemId == internalItemId;
        });
    if (item == batchValue->items.cend())
        return finishIntent(false, action, QStringLiteral("review_item_not_found"));
    const bool findMatchAvailable = titleMatchAvailable()
        && isSignalConnected(QMetaMethod::fromSignal(
            &TrackerSyncCenterModel::findMatchRequested));
    const QStringList choices = allowedImportChoices(*item, findMatchAvailable);
    if (!choices.contains(choice))
        return finishIntent(false, action, QStringLiteral("choice_not_allowed"));
    if (choice == QLatin1String("find_match")) {
        emit findMatchRequested(batchId, itemId, m_revision);
        return finishIntent(true, QStringLiteral("find_match"), QStringLiteral("accepted"));
    }
    TrackerImportResolution resolution = TrackerImportResolution::None;
    if (choice == QLatin1String("use_provider_progress"))
        resolution = TrackerImportResolution::UseProviderProgress;
    else if (choice == QLatin1String("keep_colosseum"))
        resolution = TrackerImportResolution::KeepColosseum;
    else if (choice == QLatin1String("leave_unmatched")
             || choice == QLatin1String("leave_unresolved"))
        resolution = TrackerImportResolution::LeaveUnresolved;
    else
        return finishIntent(false, action, QStringLiteral("action_unavailable"));
    if (!m_imports->resolve(batchId, internalItemId, resolution))
        return finishIntent(false, action, QStringLiteral("review_changed"));
    return finishIntent(true, action, QStringLiteral("saved"));
}

bool TrackerSyncCenterModel::resolveImportItems(const QString &batchId,
                                                const QStringList &itemIds,
                                                const QString &choice,
                                                quint64 expectedRevision)
{
    const QString action = QStringLiteral("resolve_import_items");
    if (!acceptIntent(expectedRevision, action))
        return false;
    if (!ownerHealthy() || !m_imports || !m_connections || batchId.isEmpty()
        || itemIds.isEmpty()) {
        return finishIntent(false, action, QStringLiteral("owner_unavailable"));
    }
    const auto batchValue = m_imports->batch(batchId);
    if (!batchValue)
        return finishIntent(false, action, QStringLiteral("review_not_found"));
    const auto current = m_connections->connection(batchValue->providerId);
    if (!current || current->state != TrackerConnectionState::Connected
        || current->remoteAccountId != batchValue->remoteAccountId
        || current->connectionGeneration != batchValue->connectionGeneration) {
        return finishIntent(false, action, QStringLiteral("connection_changed"));
    }

    QSet<QString> uniquePublicIds;
    QStringList privateItemIds;
    privateItemIds.reserve(itemIds.size());
    const bool findMatchAvailable = titleMatchAvailable()
        && isSignalConnected(QMetaMethod::fromSignal(
            &TrackerSyncCenterModel::findMatchRequested));
    for (const QString &publicItemId : itemIds) {
        if (publicItemId.isEmpty() || uniquePublicIds.contains(publicItemId))
            return finishIntent(false, action, QStringLiteral("choice_not_allowed"));
        uniquePublicIds.insert(publicItemId);
        const auto privateItem = m_publicToPrivateImportItemId.constFind(publicItemId);
        if (privateItem == m_publicToPrivateImportItemId.cend()
            || privateItem->first != batchId) {
            return finishIntent(false, action, QStringLiteral("review_item_not_found"));
        }
        const auto item = std::find_if(batchValue->items.cbegin(), batchValue->items.cend(),
            [&privateItem](const TrackerImportItem &value) {
                return value.itemId == privateItem->second;
            });
        if (item == batchValue->items.cend()
            || !allowedImportChoices(*item, findMatchAvailable).contains(choice)) {
            return finishIntent(false, action, QStringLiteral("choice_not_allowed"));
        }
        privateItemIds.append(privateItem->second);
    }

    TrackerImportResolution resolution = TrackerImportResolution::None;
    if (choice == QLatin1String("use_provider_progress"))
        resolution = TrackerImportResolution::UseProviderProgress;
    else if (choice == QLatin1String("keep_colosseum"))
        resolution = TrackerImportResolution::KeepColosseum;
    else if (choice == QLatin1String("leave_unmatched")
             || choice == QLatin1String("leave_unresolved"))
        resolution = TrackerImportResolution::LeaveUnresolved;
    if (resolution == TrackerImportResolution::None)
        return finishIntent(false, action, QStringLiteral("choice_not_allowed"));
    if (!m_imports->resolveSelected(batchId, privateItemIds, resolution))
        return finishIntent(false, action, QStringLiteral("review_changed"));
    return finishIntent(true, action, QStringLiteral("saved"));
}

bool TrackerSyncCenterModel::confirmImport(const QString &batchId,
                                           quint64 expectedRevision)
{
    const QString action = QStringLiteral("confirm_import");
    if (!acceptIntent(expectedRevision, action))
        return false;
    if (!ownerHealthy() || !m_imports || !m_connections)
        return finishIntent(false, action, QStringLiteral("owner_unavailable"));
    const auto batchValue = m_imports->batch(batchId);
    if (!batchValue)
        return finishIntent(false, action, QStringLiteral("review_not_found"));
    const auto current = m_connections->connection(batchValue->providerId);
    if (!current || current->state != TrackerConnectionState::Connected
        || current->remoteAccountId != batchValue->remoteAccountId
        || current->connectionGeneration != batchValue->connectionGeneration) {
        return finishIntent(false, action, QStringLiteral("connection_changed"));
    }
    if (!m_imports->confirm(batchId))
        return finishIntent(false, action, QStringLiteral("review_incomplete"));
    if (!m_importOwner)
        return finishIntent(true, action, QStringLiteral("confirmed"));

    QPointer<TrackerSyncCenterModel> self(this);
    QTimer::singleShot(0, this, [self, batchId, action] {
        if (!self || !self->m_imports || !self->m_importOwner)
            return;
        self->m_imports->applyConfirmedAsync(batchId, self->m_importOwner,
            [self, action](bool applied, const QString &) {
                if (!self)
                    return;
                self->finishIntent(applied, action,
                    applied ? QStringLiteral("applied")
                            : QStringLiteral("apply_needs_attention"));
            });
    });
    return finishIntent(true, action, QStringLiteral("pending"));
}

bool TrackerSyncCenterModel::disconnectTracker(const QString &providerKey,
                                               const QString &choice,
                                               quint64 expectedRevision)
{
    const QString action = QStringLiteral("disconnect_tracker");
    if (!acceptIntent(expectedRevision, action))
        return false;
    if (!ownerHealthy() || !m_disconnectAction)
        return finishIntent(false, action, QStringLiteral("owner_unavailable"));

    const auto providerId = trackerProviderIdFromKey(providerKey);
    const TrackerProviderDescriptor *provider = providerId ? descriptor(*providerId) : nullptr;
    TrackerConnection connection;
    const auto savedConnection = providerId && m_connections
        ? m_connections->connection(*providerId) : std::nullopt;
    if (!providerId || !provider || !savedConnection)
        return finishIntent(false, action, QStringLiteral("provider_unavailable"));
    connection = *savedConnection;
    const bool connected = connection.state == TrackerConnectionState::Connected;
    const bool cleanupRetry = connection.state == TrackerConnectionState::Disconnected
        && choice == QLatin1String("discard_known_unsent")
        && providerCounts(connection).knownUnsent > 0;
    if (!connected && !cleanupRetry)
        return finishIntent(false, action, QStringLiteral("provider_unavailable"));
    if (providerCounts(connection).syncing > 0)
        return finishIntent(false, action, QStringLiteral("delivery_in_flight"));

    if (choice != QLatin1String("keep_paused")
        && choice != QLatin1String("discard_known_unsent")) {
        return finishIntent(false, action, QStringLiteral("choice_not_allowed"));
    }

    QString error;
    if (!m_disconnectAction(providerKey, choice, &error)) {
        const auto after = m_connections->connection(*providerId);
        if (choice == QLatin1String("discard_known_unsent") && after
            && after->state == TrackerConnectionState::Disconnected
            && providerCounts(*after).knownUnsent > 0) {
            return finishIntent(false, action, QStringLiteral("disconnect_cleanup_pending"));
        }
        return finishIntent(false, action, QStringLiteral("disconnect_failed"));
    }
    return finishIntent(true, action,
                        choice == QLatin1String("keep_paused")
                            ? QStringLiteral("disconnected_paused")
                            : QStringLiteral("disconnected_known_unsent_discarded"));
}

bool TrackerSyncCenterModel::removeImportedData(const QString &providerKey,
                                                quint64 expectedRevision)
{
    const QString action = QStringLiteral("remove_imported_tracker_data");
    if (!acceptIntent(expectedRevision, action))
        return false;
    if (!profileStoresHealthy() || !m_connections)
        return finishIntent(false, action, QStringLiteral("owner_unavailable"));
    if (m_historyEvidence && !m_historyEvidence->healthy())
        return finishIntent(false, action, QStringLiteral("history_owner_unavailable"));
    if (m_importedDataRemovalPending)
        return finishIntent(false, action, QStringLiteral("removal_in_progress"));

    const auto providerId = trackerProviderIdFromKey(providerKey);
    const auto connection = providerId ? m_connections->connection(*providerId) : std::nullopt;
    if (!providerId || !connection)
        return finishIntent(false, action, QStringLiteral("provider_unavailable"));
    const int historyCount = importedHistoryCount(*connection);
    const int progressCount = importedProgressCount(*connection);
    if (historyCount == 0 && progressCount == 0)
        return finishIntent(false, action, QStringLiteral("no_imported_data"));
    if (historyCount > 0 && (!m_historyEvidence || !m_historyEvidence->healthy()))
        return finishIntent(false, action, QStringLiteral("history_owner_unavailable"));
    const QVariantList removalPreview = importedDataRemovalPreview(*connection);
    if (removalPreview.size() != historyCount + progressCount)
        return finishIntent(false, action, QStringLiteral("removal_preview_unavailable"));
    if (!m_importOwner)
        return finishIntent(false, action, QStringLiteral("progress_owner_unavailable"));

    m_importedDataRemovalPending = true;
    QPointer<TrackerSyncCenterModel> model(this);
    const TrackerProviderId removalProvider = *providerId;
    const QString remoteAccountId = connection->remoteAccountId;
    m_importOwner->removeImportedProgressAsync(
        removalProvider, remoteAccountId,
        [model, removalProvider, remoteAccountId, action](
            bool removed, int, const QString &) {
            if (!model)
                return;
            if (!removed) {
                model->m_importedDataRemovalPending = false;
                model->finishIntent(false, action,
                                    QStringLiteral("progress_removal_failed"));
                return;
            }
            if (model->m_historyEvidence
                && model->m_historyEvidence->removeSource(
                    removalProvider, remoteAccountId) < 0) {
                model->m_importedDataRemovalPending = false;
                model->finishIntent(false, action,
                    QStringLiteral("progress_removed_history_removal_failed"));
                return;
            }
            model->m_importedDataRemovalPending = false;
            model->finishIntent(true, action, QStringLiteral("removed"));
        });
    if (m_importedDataRemovalPending)
        finishIntent(true, action, QStringLiteral("removal_pending"));
    return true;
}

bool TrackerSyncCenterModel::requestSyncAll(quint64 expectedRevision)
{
    const QString action = QStringLiteral("sync_all");
    if (!acceptIntent(expectedRevision, action))
        return false;
    if (!ownerHealthy())
        return finishIntent(false, action, QStringLiteral("owner_unavailable"));
    if (!m_settings->globalSettings().trackerSyncEnabled)
        return finishIntent(false, action, QStringLiteral("sync_paused"));
    const QStringList providers = eligibleProviderKeys();
    if (providers.isEmpty())
        return finishIntent(false, action, QStringLiteral("no_eligible_provider"));
    if (!isSignalConnected(QMetaMethod::fromSignal(
            &TrackerSyncCenterModel::syncAllRequested))) {
        return finishIntent(false, action, QStringLiteral("sync_handler_unavailable"));
    }
    emit syncAllRequested(providers, m_revision);
    return finishIntent(true, action, QStringLiteral("accepted"));
}

const TrackerProviderDescriptor *TrackerSyncCenterModel::descriptor(
    TrackerProviderId providerId) const
{
    const auto found = std::find_if(m_catalogue.cbegin(), m_catalogue.cend(),
        [providerId](const TrackerProviderDescriptor &entry) {
            return entry.providerId == providerId;
        });
    return found == m_catalogue.cend() ? nullptr : &*found;
}

bool TrackerSyncCenterModel::profileStoresHealthy() const
{
    return m_profileAvailable && m_connections && m_imports && m_delivery && m_scrobble
        && m_settings && m_connections->healthy() && m_imports->healthy()
        && m_delivery->healthy() && m_scrobble->healthy() && m_settings->healthy();
}

bool TrackerSyncCenterModel::ownerHealthy() const
{
    return profileStoresHealthy()
        && (!m_historyEvidence || m_historyEvidence->healthy());
}

bool TrackerSyncCenterModel::titleMatchAvailable() const
{
    if (!m_titleMatchStore || !m_titleIndex || !m_importOwner
        || !m_titleMatchStore->healthy()) {
        return false;
    }
    // Keep Find match available when a healthy Progress index has no
    // distinguishable candidates. The chooser owns the honest empty state.
    return m_titleIndex->candidateSearchAvailable();
}

int TrackerSyncCenterModel::importedHistoryCount(const TrackerConnection &connection) const
{
    if (!m_historyEvidence || !m_historyEvidence->healthy())
        return 0;
    const QList<TrackerImportedHistoryEvidence> contributions =
        m_historyEvidence->contributions();
    return static_cast<int>(std::count_if(
        contributions.cbegin(), contributions.cend(),
        [&connection](const TrackerImportedHistoryEvidence &entry) {
            return entry.mapping.remote.providerId == connection.providerId
                && entry.mapping.remote.remoteAccountId == connection.remoteAccountId;
        }));
}

int TrackerSyncCenterModel::importedProgressCount(const TrackerConnection &connection) const
{
    return m_importOwner
        ? m_importOwner->importedProgressCount(connection.providerId,
                                              connection.remoteAccountId)
        : 0;
}

QVariantList TrackerSyncCenterModel::importedDataRemovalPreview(
    const TrackerConnection &connection) const
{
    QVariantList preview;
    const int expectedHistoryCount = importedHistoryCount(connection);
    if (expectedHistoryCount > 0) {
        if (!m_historyEvidence || !m_historyEvidence->healthy())
            return {};
        const QList<TrackerImportedHistoryEvidence> contributions =
            m_historyEvidence->contributions();
        for (const TrackerImportedHistoryEvidence &entry : contributions) {
            if (entry.mapping.remote.providerId != connection.providerId
                || entry.mapping.remote.remoteAccountId != connection.remoteAccountId) {
                continue;
            }
            preview.append(QVariantMap{
                {QStringLiteral("title"),
                 safePreviewTitle(entry.mapping.canonical.displayName)},
                {QStringLiteral("dataKind"), entry.eventKind == TrackerImportedEventKind::Completion
                    ? QStringLiteral("Completion evidence") : QStringLiteral("Activity evidence")},
                {QStringLiteral("consequence"), QStringLiteral(
                    "Only this tracker's History evidence is removed; native History and other tracker evidence remain.")}});
        }
        if (preview.size() != expectedHistoryCount)
            return {};
    }

    const int expectedProgressCount = importedProgressCount(connection);
    if (expectedProgressCount > 0) {
        if (!m_importOwner)
            return {};
        const QVariantList progressPreview = m_importOwner->importedProgressRemovalPreview(
            connection.providerId, connection.remoteAccountId);
        if (progressPreview.size() != expectedProgressCount)
            return {};
        for (const QVariant &value : progressPreview) {
            const QVariantMap entry = value.toMap();
            preview.append(QVariantMap{
                {QStringLiteral("title"),
                 safePreviewTitle(entry.value(QStringLiteral("title")).toString())},
                {QStringLiteral("dataKind"), QStringLiteral("Progress")},
                {QStringLiteral("consequence"), safeRemovalConsequence(entry)}});
        }
    }
    return preview;
}

bool TrackerSyncCenterModel::currentConnection(TrackerProviderId providerId,
                                               TrackerConnection *connection) const
{
    if (!m_connections || !m_connections->healthy())
        return false;
    const auto current = m_connections->connection(providerId);
    if (!current || current->state != TrackerConnectionState::Connected)
        return false;
    if (connection)
        *connection = *current;
    return true;
}

bool TrackerSyncCenterModel::firstImportReviewed(const TrackerConnection &connection) const
{
    if (!m_imports || !m_imports->healthy())
        return false;
    for (const TrackerImportBatch &batchValue : m_imports->batches()) {
        if (batchValue.providerId == connection.providerId
            && batchValue.remoteAccountId == connection.remoteAccountId
            && batchValue.connectionGeneration == connection.connectionGeneration
            && batchValue.initialImport && batchValue.confirmed && batchValue.cursorCommitted) {
            return true;
        }
    }
    return false;
}

TrackerSyncCenterModel::ProviderCounts TrackerSyncCenterModel::providerCounts(
    const TrackerConnection &connection) const
{
    ProviderCounts counts;
    if (m_imports) {
        for (const TrackerImportBatch &batchValue : m_imports->batches()) {
            if (batchValue.providerId != connection.providerId
                || batchValue.remoteAccountId != connection.remoteAccountId) {
                continue;
            }
            for (const TrackerImportItem &item : batchValue.items) {
                if (item.state == TrackerImportItemState::ReviewRequired
                    || item.state == TrackerImportItemState::Unresolved) {
                    ++counts.unresolved;
                    ++counts.attention;
                    ++counts.pending;
                } else if (item.state == TrackerImportItemState::Applying) {
                    ++counts.syncing;
                    ++counts.pending;
                } else if (item.state == TrackerImportItemState::AwaitingApply) {
                    ++counts.waiting;
                    ++counts.pending;
                }
            }
        }
    }
    if (m_delivery) {
        for (const TrackerDeliveryOperation &operation : m_delivery->operations()) {
            if (operation.providerId != connection.providerId
                || operation.remoteAccountId != connection.remoteAccountId) {
                continue;
            }
            switch (operation.state) {
            case TrackerDeliveryState::Pending:
                ++counts.waiting;
                ++counts.pending;
                ++counts.knownUnsent;
                break;
            case TrackerDeliveryState::Retrying:
                ++counts.waiting;
                ++counts.pending;
                ++counts.knownUnsent;
                if (operation.reason == TrackerDeliveryReason::ProviderRetryable)
                    ++counts.attention;
                break;
            case TrackerDeliveryState::Delivering:
                ++counts.syncing;
                ++counts.pending;
                break;
            case TrackerDeliveryState::NeedsAttention:
            case TrackerDeliveryState::FailedTerminal:
            case TrackerDeliveryState::UnknownOutcome:
                ++counts.attention;
                ++counts.pending;
                if (operation.state == TrackerDeliveryState::UnknownOutcome)
                    ++counts.unknownOutcome;
                if (operation.state == TrackerDeliveryState::NeedsAttention
                    && (operation.reason == TrackerDeliveryReason::RetryLimitReached
                        || operation.reason == TrackerDeliveryReason::DestinationReviewRequired)) {
                    ++counts.knownUnsent;
                }
                break;
            case TrackerDeliveryState::Succeeded:
                break;
            }
        }
    }
    if (m_scrobble) {
        for (const TrackerScrobbleIntent &intent : m_scrobble->intents()) {
            if (intent.providerId != connection.providerId
                || intent.remoteAccountId != connection.remoteAccountId) {
                continue;
            }
            switch (intent.state) {
            case TrackerScrobbleState::Pending:
                ++counts.waiting;
                ++counts.pending;
                ++counts.knownUnsent;
                break;
            case TrackerScrobbleState::Waiting:
                ++counts.waiting;
                ++counts.pending;
                if (intent.reason != TrackerScrobbleReason::AcknowledgementLost)
                    ++counts.knownUnsent;
                if (intent.reason == TrackerScrobbleReason::ProviderUnavailable
                    || intent.reason == TrackerScrobbleReason::RetryableKnownNotApplied) {
                    ++counts.attention;
                }
                break;
            case TrackerScrobbleState::Delivering:
                ++counts.syncing;
                ++counts.pending;
                break;
            case TrackerScrobbleState::UnknownOutcome:
            case TrackerScrobbleState::NeedsAttention:
                ++counts.attention;
                ++counts.pending;
                if (intent.state == TrackerScrobbleState::UnknownOutcome)
                    ++counts.unknownOutcome;
                if (intent.state == TrackerScrobbleState::NeedsAttention
                    && intent.reason == TrackerScrobbleReason::RetryLimitReached) {
                    ++counts.knownUnsent;
                }
                break;
            case TrackerScrobbleState::Succeeded:
            case TrackerScrobbleState::Superseded:
                break;
            }
        }
    }
    return counts;
}

QString TrackerSyncCenterModel::providerState(const TrackerConnection &connection,
                                              const ProviderCounts &counts) const
{
    const TrackerProviderDescriptor *provider = descriptor(connection.providerId);
    if (!ownerHealthy())
        return QStringLiteral("Attention");
    if (connection.state == TrackerConnectionState::Disconnected)
        return QStringLiteral("Disconnected");
    if (connection.state == TrackerConnectionState::TransferPending
        || !provider || !provider->available || counts.attention > 0) {
        return QStringLiteral("Attention");
    }
    if (counts.syncing > 0)
        return QStringLiteral("Syncing");
    if (!m_settings->globalSettings().trackerSyncEnabled)
        return QStringLiteral("Paused");
    if (counts.waiting > 0)
        return QStringLiteral("Waiting");
    return QStringLiteral("Connected");
}

TrackerProviderCapabilities TrackerSyncCenterModel::effectiveCapabilities(
    const TrackerConnection &connection) const
{
    const TrackerProviderDescriptor *provider = descriptor(connection.providerId);
    if (!provider || !provider->available
        || !trackerProviderCapabilitiesAreKnown(connection.capabilities)
        || !trackerProviderCapabilitiesAreKnown(provider->capabilities)) {
        return {};
    }
    return connection.capabilities & provider->capabilities;
}

QStringList TrackerSyncCenterModel::eligibleProviderKeys() const
{
    QStringList result;
    if (!ownerHealthy() || !m_settings
        || !m_settings->globalSettings().trackerSyncEnabled || !m_connections) {
        return result;
    }
    for (const TrackerConnection &connection : m_connections->connections()) {
        if (connection.state != TrackerConnectionState::Connected)
            continue;
        const TrackerProviderDescriptor *provider = descriptor(connection.providerId);
        if (!provider || !provider->available
            || effectiveCapabilities(connection) == TrackerProviderCapabilities{})
            continue;
        result.append(trackerProviderKey(connection.providerId));
    }
    return result;
}

bool TrackerSyncCenterModel::syncAllHandlerAvailable() const
{
    return isSignalConnected(QMetaMethod::fromSignal(
        &TrackerSyncCenterModel::syncAllRequested));
}

QVariantMap TrackerSyncCenterModel::connectionCard(const TrackerConnection &connection) const
{
    const TrackerProviderDescriptor *provider = descriptor(connection.providerId);
    const ProviderCounts counts = providerCounts(connection);
    const TrackerProviderCapabilities capabilities = effectiveCapabilities(connection);
    const bool isConnected = connection.state != TrackerConnectionState::Disconnected;
    return {{QStringLiteral("providerKey"), trackerProviderKey(connection.providerId)},
            {QStringLiteral("providerName"), provider ? provider->displayName
                                                      : trackerProviderDisplayName(connection.providerId)},
            {QStringLiteral("accountLabel"), isConnected
                    ? QStringLiteral("Connected account") : QString()},
            {QStringLiteral("status"), providerState(connection, counts)},
            {QStringLiteral("connectionState"), connectionStateKey(connection.state)},
            {QStringLiteral("lastSuccessfulSyncAtMs"), m_settings
                    ? m_settings->lastSuccessfulSyncAtMs(connection.providerId,
                                                         connection.remoteAccountId)
                    : 0},
            {QStringLiteral("pendingCount"), counts.pending},
            {QStringLiteral("waitingCount"), counts.waiting},
            {QStringLiteral("unresolvedCount"), counts.unresolved},
            {QStringLiteral("capabilities"), capabilityKeys(capabilities)},
            {QStringLiteral("available"), provider && provider->available},
            {QStringLiteral("connectEnabled"), false},
            {QStringLiteral("syncEnabled"), isConnected && provider && provider->available
                    && m_settings && m_settings->globalSettings().trackerSyncEnabled
                    && ownerHealthy() && syncAllHandlerAvailable()},
            {QStringLiteral("disconnectEnabled"), isConnected
                    && connection.state == TrackerConnectionState::Connected
                    && counts.syncing == 0 && ownerHealthy()
                    && static_cast<bool>(m_disconnectAction)}};
}

QVariantMap TrackerSyncCenterModel::providerCatalogueCard(
    const TrackerProviderDescriptor &provider) const
{
    const auto connection = m_connections
        ? m_connections->connection(provider.providerId) : std::nullopt;
    const bool isConnected = connection
        && connection->state != TrackerConnectionState::Disconnected;
    const ProviderCounts counts = connection ? providerCounts(*connection) : ProviderCounts{};
    const bool pausedWork = connection
        && connection->state == TrackerConnectionState::Disconnected
        && (counts.pending > 0 || counts.unresolved > 0);
    return {{QStringLiteral("providerKey"), trackerProviderKey(provider.providerId)},
            {QStringLiteral("providerName"), provider.displayName},
            {QStringLiteral("available"), provider.available},
            {QStringLiteral("status"), provider.available
                    ? (isConnected ? providerState(*connection, providerCounts(*connection))
                                   : (pausedWork ? QStringLiteral("Attention")
                                                 : QStringLiteral("Not connected")))
                    : QStringLiteral("Unavailable in this build")},
            {QStringLiteral("capabilities"), capabilityKeys(provider.capabilities)},
            {QStringLiteral("connected"), isConnected},
            {QStringLiteral("pendingWork"), pausedWork},
            {QStringLiteral("waitingCount"), counts.waiting},
            {QStringLiteral("unresolvedCount"), counts.unresolved},
            // Keep this fail-closed until a native auth action is composed.
            {QStringLiteral("connectEnabled"), false}};
}

QVariantList TrackerSyncCenterModel::buildConnectedTrackers() const
{
    QVariantList result;
    if (!m_connections || !m_connections->healthy())
        return result;
    for (const TrackerProviderDescriptor &provider : m_catalogue) {
        const auto connection = m_connections->connection(provider.providerId);
        if (connection && connection->state != TrackerConnectionState::Disconnected)
            result.append(connectionCard(*connection));
    }
    return result;
}

QVariantList TrackerSyncCenterModel::buildCatalogue() const
{
    QVariantList result;
    for (const TrackerProviderDescriptor &provider : m_catalogue) {
        const auto connection = m_connections
            ? m_connections->connection(provider.providerId) : std::nullopt;
        if (connection && connection->state != TrackerConnectionState::Disconnected)
            continue;
        result.append(providerCatalogueCard(provider));
    }
    return result;
}

QVariantList TrackerSyncCenterModel::buildImportReviews() const
{
    QVariantList result;
    if (!m_imports)
        return result;
    for (const TrackerImportBatch &batchValue : m_imports->batches()) {
        int reviewCount = 0;
        int decisionCount = 0;
        int awaitingApplyCount = 0;
        int unresolvedCount = 0;
        for (const TrackerImportItem &item : batchValue.items) {
            switch (item.state) {
            case TrackerImportItemState::ReviewRequired:
                ++decisionCount;
                ++reviewCount;
                break;
            case TrackerImportItemState::Unresolved:
                ++unresolvedCount;
                ++reviewCount;
                break;
            case TrackerImportItemState::Applying:
            case TrackerImportItemState::AwaitingApply:
                ++awaitingApplyCount;
                ++reviewCount;
                break;
            case TrackerImportItemState::Applied:
            case TrackerImportItemState::KeptLocal:
            case TrackerImportItemState::NonMutating:
            case TrackerImportItemState::Superseded:
                break;
            }
        }
        // Keep an unconfirmed batch discoverable after every item has a
        // decision; the user must still be able to reopen and confirm it.
        if (reviewCount == 0 && batchValue.confirmed)
            continue;
        result.append(QVariantMap{
            {QStringLiteral("batchId"), batchValue.batchId},
            {QStringLiteral("providerKey"), trackerProviderKey(batchValue.providerId)},
            {QStringLiteral("providerName"), trackerProviderDisplayName(batchValue.providerId)},
            {QStringLiteral("initialImport"), batchValue.initialImport},
            {QStringLiteral("pageComplete"), batchValue.pageComplete},
            {QStringLiteral("reviewCount"), reviewCount},
            {QStringLiteral("decisionCount"), decisionCount},
            {QStringLiteral("awaitingApplyCount"), awaitingApplyCount},
            {QStringLiteral("unresolvedCount"), unresolvedCount},
            {QStringLiteral("confirmed"), batchValue.confirmed}});
    }
    return result;
}

QVariantMap TrackerSyncCenterModel::buildGlobalSettings() const
{
    const TrackerGlobalSyncSettings settings = m_settings
        ? m_settings->globalSettings() : TrackerGlobalSyncSettings{};
    return {{QStringLiteral("trackerSyncEnabled"), settings.trackerSyncEnabled},
            {QStringLiteral("checkOnLaunch"), settings.checkOnLaunch},
            {QStringLiteral("backgroundDelivery"), settings.backgroundDelivery},
            {QStringLiteral("completionMessages"), settings.completionMessages},
            {QStringLiteral("profileAvailable"), m_profileAvailable},
            {QStringLiteral("editable"), m_profileAvailable && ownerHealthy()}};
}

QVariantMap TrackerSyncCenterModel::buildAggregateState() const
{
    const QVariantList connected = buildConnectedTrackers();
    const bool ownersAvailable = ownerHealthy();
    const bool syncEnabled = m_settings
        ? m_settings->globalSettings().trackerSyncEnabled : true;
    int waiting = 0;
    int syncing = 0;
    int attention = 0;
    int unresolved = 0;
    int attentionProviders = 0;
    for (const QVariant &rowValue : connected) {
        const QVariantMap row = rowValue.toMap();
        waiting += row.value(QStringLiteral("waitingCount")).toInt();
        unresolved += row.value(QStringLiteral("unresolvedCount")).toInt();
        const QString status = row.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("Syncing"))
            ++syncing;
        if (status == QLatin1String("Attention")) {
            ++attention;
            ++attentionProviders;
        }
    }
    if (m_connections && m_connections->healthy()) {
        for (const TrackerConnection &connection : m_connections->connections()) {
            if (connection.state != TrackerConnectionState::Disconnected)
                continue;
            const ProviderCounts counts = providerCounts(connection);
            waiting += counts.waiting;
            unresolved += counts.unresolved;
            if (counts.pending > 0 || counts.unresolved > 0) {
                ++attention;
                ++attentionProviders;
            }
        }
    }
    QString status = QStringLiteral("Empty");
    if (!ownersAvailable && (!connected.isEmpty() || attention > 0))
        status = QStringLiteral("Owner unavailable");
    else if (attention > 0)
        status = QStringLiteral("Attention");
    else if (!connected.isEmpty()) {
        if (syncing > 0)
            status = QStringLiteral("Syncing");
        else if (!syncEnabled)
            status = QStringLiteral("Paused");
        else if (waiting > 0)
            status = QStringLiteral("Waiting");
        else
            status = QStringLiteral("Healthy");
    }
    const QStringList eligible = eligibleProviderKeys();
    return {{QStringLiteral("status"), status},
            {QStringLiteral("healthy"), ownersAvailable && attention == 0},
            {QStringLiteral("ownerHealthy"), ownersAvailable},
            {QStringLiteral("ownerUnavailable"), !ownersAvailable},
            {QStringLiteral("syncing"), syncing > 0},
            {QStringLiteral("connectedCount"), connected.size()},
            {QStringLiteral("waitingCount"), waiting},
            {QStringLiteral("unresolvedCount"), unresolved},
            {QStringLiteral("attentionProviderCount"), attentionProviders},
            {QStringLiteral("canSyncAll"), !eligible.isEmpty()
                    && syncAllHandlerAvailable()},
            {QStringLiteral("currentActionKind"), QStringLiteral("none")},
            {QStringLiteral("revision"), QVariant::fromValue(m_revision)}};
}

QVariantList TrackerSyncCenterModel::buildImportRows(const TrackerImportBatch &batchValue) const
{
    QVariantList rows;
    rows.reserve(batchValue.items.size());
    for (const TrackerImportItem &item : batchValue.items) {
        QString title = item.remote.displayTitle;
        if (title.isEmpty() && item.remote.mapping)
            title = item.remote.mapping->canonical.displayName;
        if (title.isEmpty())
            title = QStringLiteral("Unmatched title");
        const std::optional<TrackerImportedProgressValue> local = item.remote.localAtPreview;
        const bool findMatchAvailable = titleMatchAvailable()
            && isSignalConnected(QMetaMethod::fromSignal(
                &TrackerSyncCenterModel::findMatchRequested));
        const QStringList choices = allowedImportChoices(item, findMatchAvailable);
        rows.append(QVariantMap{
            {QStringLiteral("reviewItemId"), publicImportItemId(batchValue.batchId,
                                                                  item.itemId)},
            {QStringLiteral("title"), title},
            {QStringLiteral("classification"), importClassificationKey(item.classification)},
            {QStringLiteral("state"), importStateKey(item.state)},
            {QStringLiteral("resolution"), importResolutionKey(item.resolution)},
            {QStringLiteral("matched"), item.remote.mapping.has_value()},
            {QStringLiteral("providerProgress"), item.remote.progress},
            {QStringLiteral("providerCompleted"), item.remote.completed},
            {QStringLiteral("hasLocalAtPreview"), local.has_value()},
            {QStringLiteral("localProgress"), local ? local->progress : -1},
            {QStringLiteral("localCompleted"), local ? local->completed : false},
            {QStringLiteral("nativeHistoryProtected"), item.remote.contradictsNativeHistory},
            {QStringLiteral("allowedChoices"), choices}});
    }
    return rows;
}

QString TrackerSyncCenterModel::publicImportItemId(const QString &batchId,
                                                   const QString &privateItemId) const
{
    const QString privateKey = batchId + QChar(0x1f) + privateItemId;
    const auto existing = m_privateToPublicImportItemId.constFind(privateKey);
    if (existing != m_privateToPublicImportItemId.cend())
        return *existing;
    const QString publicId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_privateToPublicImportItemId.insert(privateKey, publicId);
    m_publicToPrivateImportItemId.insert(publicId, qMakePair(batchId, privateItemId));
    return publicId;
}

QByteArray TrackerSyncCenterModel::snapshotFingerprint() const
{
    QJsonObject root;
    root.insert(QStringLiteral("profileAvailable"), m_profileAvailable);
    root.insert(QStringLiteral("ownerHealthy"), ownerHealthy());
    root.insert(QStringLiteral("titleSearchAvailable"), titleMatchAvailable());
    QJsonArray titleCandidates;
    if (m_titleIndex) {
        for (const TrackerCanonicalTitleCandidate &candidate : m_titleIndex->userCandidates()) {
            titleCandidates.append(QJsonObject{
                {QStringLiteral("canonicalMediaId"), candidate.canonicalMediaId},
                {QStringLiteral("historyKind"), candidate.historyKind},
                {QStringLiteral("historyId"), candidate.historyId},
                {QStringLiteral("displayName"), candidate.displayName}});
        }
    }
    root.insert(QStringLiteral("titleCandidates"), titleCandidates);
    if (m_settings) {
        const TrackerGlobalSyncSettings global = m_settings->globalSettings();
        root.insert(QStringLiteral("global"), QJsonObject{
            {QStringLiteral("trackerSyncEnabled"), global.trackerSyncEnabled},
            {QStringLiteral("checkOnLaunch"), global.checkOnLaunch},
            {QStringLiteral("backgroundDelivery"), global.backgroundDelivery},
            {QStringLiteral("completionMessages"), global.completionMessages}});
    }
    QJsonArray connections;
    if (m_connections) {
        for (const TrackerConnection &connection : m_connections->connections()) {
            connections.append(QJsonObject{
                {QStringLiteral("provider"), trackerProviderKey(connection.providerId)},
                {QStringLiteral("account"), connection.remoteAccountId},
                {QStringLiteral("generation"), QString::number(connection.connectionGeneration)},
                {QStringLiteral("state"), connectionStateKey(connection.state)},
                {QStringLiteral("capabilities"),
                 static_cast<int>(static_cast<quint32>(connection.capabilities))}});
        }
    }
    root.insert(QStringLiteral("connections"), connections);
    QJsonArray importedProgress;
    if (m_connections) {
        for (const TrackerConnection &connection : m_connections->connections()) {
            QJsonArray preview;
            if (m_importOwner) {
                const QVariantList entries = m_importOwner->importedProgressRemovalPreview(
                    connection.providerId, connection.remoteAccountId);
                for (const QVariant &entryValue : entries) {
                    const QVariantMap entry = entryValue.toMap();
                    preview.append(QJsonObject{
                        {QStringLiteral("title"), safePreviewTitle(
                            entry.value(QStringLiteral("title")).toString())},
                        {QStringLiteral("consequence"), safeRemovalConsequence(entry)}});
                }
            }
            importedProgress.append(QJsonObject{
                {QStringLiteral("provider"), trackerProviderKey(connection.providerId)},
                {QStringLiteral("account"), connection.remoteAccountId},
                {QStringLiteral("count"), importedProgressCount(connection)},
                {QStringLiteral("preview"), preview}});
        }
    }
    root.insert(QStringLiteral("importedProgress"), importedProgress);
    QJsonArray providerSettings;
    if (m_connections) {
        for (const TrackerConnection &connection : m_connections->connections()) {
            providerSettings.append(QJsonObject{
                {QStringLiteral("provider"), trackerProviderKey(connection.providerId)},
                {QStringLiteral("account"), connection.remoteAccountId},
                {QStringLiteral("pullAutomatically"), m_settings
                        && m_settings->pullAutomatically(connection.providerId,
                            connection.remoteAccountId, firstImportReviewed(connection))},
                {QStringLiteral("sendEnabled"), m_delivery
                        && m_delivery->providerSendEnabled(connection.providerId,
                                                           connection.remoteAccountId)},
                {QStringLiteral("livePlaybackTrackingEnabled"), m_scrobble
                        && m_scrobble->enabled(connection.providerId,
                                               connection.remoteAccountId)},
                {QStringLiteral("lastSuccessfulSyncAtMs"), m_settings
                        ? QString::number(m_settings->lastSuccessfulSyncAtMs(
                              connection.providerId, connection.remoteAccountId))
                        : QStringLiteral("0")}});
        }
    }
    root.insert(QStringLiteral("providerSettings"), providerSettings);
    QJsonArray imports;
    if (m_imports) {
        for (const TrackerImportBatch &batchValue : m_imports->batches()) {
            QJsonArray items;
            for (const TrackerImportItem &item : batchValue.items) {
                items.append(QJsonObject{
                    {QStringLiteral("id"), item.itemId},
                    {QStringLiteral("classification"), importClassificationKey(item.classification)},
                    {QStringLiteral("resolution"), importResolutionKey(item.resolution)},
                    {QStringLiteral("state"), importStateKey(item.state)},
                    {QStringLiteral("mapping"), mappingFingerprint(item.remote.mapping)},
                    {QStringLiteral("target"), targetFingerprint(
                         item.remote.exactProgressTarget)},
                    {QStringLiteral("local"), progressFingerprint(item.remote.localAtPreview)}});
            }
            imports.append(QJsonObject{
                {QStringLiteral("id"), batchValue.batchId},
                {QStringLiteral("provider"), trackerProviderKey(batchValue.providerId)},
                {QStringLiteral("account"), batchValue.remoteAccountId},
                {QStringLiteral("generation"), QString::number(batchValue.connectionGeneration)},
                {QStringLiteral("confirmed"), batchValue.confirmed},
                {QStringLiteral("cursorCommitted"), batchValue.cursorCommitted},
                {QStringLiteral("items"), items}});
        }
    }
    root.insert(QStringLiteral("imports"), imports);
    QJsonArray deliveries;
    if (m_delivery) {
        for (const TrackerDeliveryOperation &operation : m_delivery->operations()) {
            deliveries.append(QJsonObject{
                {QStringLiteral("id"), operation.operationId},
                {QStringLiteral("provider"), trackerProviderKey(operation.providerId)},
                {QStringLiteral("account"), operation.remoteAccountId},
                {QStringLiteral("state"), deliveryStateKey(operation.state)},
                {QStringLiteral("reason"), deliveryReasonKey(operation.reason)},
                {QStringLiteral("attemptCount"), operation.attemptCount}});
        }
    }
    root.insert(QStringLiteral("deliveries"), deliveries);
    QJsonArray scrobbles;
    if (m_scrobble) {
        for (const TrackerScrobbleIntent &intent : m_scrobble->intents()) {
            scrobbles.append(QJsonObject{
                {QStringLiteral("id"), intent.operationId},
                {QStringLiteral("provider"), trackerProviderKey(intent.providerId)},
                {QStringLiteral("account"), intent.remoteAccountId},
                {QStringLiteral("state"), scrobbleStateKey(intent.state)},
                {QStringLiteral("reason"), scrobbleReasonKey(intent.reason)},
                {QStringLiteral("attemptCount"), intent.attemptCount}});
        }
    }
    root.insert(QStringLiteral("scrobbles"), scrobbles);
    QJsonArray importedHistory;
    if (m_historyEvidence) {
        for (const TrackerImportedHistoryEvidence &entry : m_historyEvidence->contributions()) {
            importedHistory.append(QJsonObject{
                {QStringLiteral("provider"), trackerProviderKey(entry.mapping.remote.providerId)},
                {QStringLiteral("account"), entry.mapping.remote.remoteAccountId},
                {QStringLiteral("event"), entry.providerEventId},
                {QStringLiteral("snapshot"), entry.importSnapshotId},
                {QStringLiteral("occurredAtMs"), QString::number(entry.occurredAtMs)}});
        }
    }
    root.insert(QStringLiteral("importedHistory"), importedHistory);
    return QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact),
                                    QCryptographicHash::Sha256);
}

void TrackerSyncCenterModel::refreshRevision()
{
    const QByteArray fingerprint = snapshotFingerprint();
    if (fingerprint == m_lastFingerprint)
        return;
    if (!m_lastFingerprint.isEmpty() && m_revision < std::numeric_limits<quint64>::max())
        ++m_revision;
    m_lastFingerprint = fingerprint;
    m_titleMatchCandidateHandles.clear();
    m_exportReview.reset();
    emit modelChanged();
}

bool TrackerSyncCenterModel::acceptIntent(quint64 expectedRevision,
                                          const QString &action)
{
    refreshRevision();
    if (expectedRevision == m_revision)
        return true;
    finishIntent(false, action, QStringLiteral("stale_intent"));
    return false;
}

bool TrackerSyncCenterModel::finishIntent(bool accepted,
                                          const QString &action,
                                          const QString &code)
{
    refreshRevision();
    m_lastActionResult = {{QStringLiteral("accepted"), accepted},
                          {QStringLiteral("action"), action},
                          {QStringLiteral("code"), code},
                          {QStringLiteral("revision"), QVariant::fromValue(m_revision)}};
    emit modelChanged();
    return accepted;
}
