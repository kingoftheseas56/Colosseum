#include "TrackerLifecycleCoordinator.h"

#include "TrackerConnectionStore.h"
#include "TrackerCredentialVault.h"
#include "TrackerDeliveryStore.h"
#include "TrackerHistoryEvidenceStore.h"
#include "TrackerImportStore.h"
#include "TrackerMappingStore.h"
#include "TrackerScrobbleStore.h"
#include "TrackerSyncSettingsStore.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <limits>

namespace {

bool setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool explicitProfile(const ProfilePaths &profile)
{
    return profile.kind() != ProfilePaths::Kind::Sealed
        && profile.kind() != ProfilePaths::Kind::LegacyLocal
        && !profile.profileRoot().isEmpty()
        && profile.isManagedProfilePath(profile.profileRoot());
}

bool sameDevice(const ProfilePaths &left, const ProfilePaths &right)
{
    return QDir::cleanPath(left.appDataRoot()) == QDir::cleanPath(right.appDataRoot());
}

bool removeFileIfPresent(const QString &path, QString *error)
{
    if (path.isEmpty() || !QFileInfo::exists(path) || QFile::remove(path))
        return true;
    return setError(error, QStringLiteral("Tracker-private profile data could not be removed."));
}

QString providerAccountKey(TrackerProviderId providerId, const QString &remoteAccountId)
{
    return trackerProviderKey(providerId) + QChar(0x1f) + remoteAccountId;
}

QString privateAdoptionCheckpointPath(const ProfilePaths &profile)
{
    return QDir::cleanPath(profile.profileRoot()
                           + QStringLiteral("/tracker-private-adoption.json"));
}

QList<QString> privateStatePaths(const ProfilePaths &profile)
{
    return {TrackerConnectionStore::storagePath(profile),
            TrackerMappingStore::storagePath(profile),
            TrackerHistoryEvidenceStore::storagePath(profile),
            TrackerImportStore::storagePath(profile),
            TrackerDeliveryStore::storagePath(profile),
            TrackerScrobbleStore::storagePath(profile),
            TrackerSyncSettingsStore::storagePath(profile)};
}

bool writePrivateAdoptionCheckpoint(const ProfilePaths &source,
                                    const ProfilePaths &destination,
                                    const QString &phase,
                                    const QStringList &needsAttention,
                                    QString *error)
{
    const QString path = privateAdoptionCheckpointPath(destination);
    QFile existing(path);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly))
            return setError(error, QStringLiteral("Tracker adoption checkpoint cannot be read."));
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(existing.readAll(), &parseError);
        const QJsonObject previous = document.object();
        existing.close();
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
            || previous.value(QStringLiteral("version")).toInt() != 1
            || previous.value(QStringLiteral("sourceProfileId")).toString() != source.profileId()
            || previous.value(QStringLiteral("destinationProfileId")).toString()
                   != destination.profileId()) {
            return setError(error, QStringLiteral(
                "A different or invalid tracker adoption checkpoint already exists."));
        }
    }

    QJsonArray attention;
    for (const QString &provider : needsAttention)
        attention.append(provider);
    const QJsonObject checkpoint{{QStringLiteral("version"), 1},
                                 {QStringLiteral("sourceProfileId"), source.profileId()},
                                 {QStringLiteral("destinationProfileId"), destination.profileId()},
                                 {QStringLiteral("phase"), phase},
                                 {QStringLiteral("needsAttentionProviders"), attention}};
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return setError(error, QStringLiteral("Tracker adoption checkpoint directory is unavailable."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("Tracker adoption checkpoint cannot be written."));
    const QByteArray payload = QJsonDocument(checkpoint).toJson(QJsonDocument::Compact);
    if (file.write(payload) != payload.size())
        return setError(error, QStringLiteral("Tracker adoption checkpoint could not be written."));
    if (!file.commit())
        return setError(error, QStringLiteral("Tracker adoption checkpoint could not be committed atomically: ")
            + file.errorString());
    return true;
}

} // namespace

bool TrackerLifecycleCoordinator::hasPrivateState(const ProfilePaths &profile,
                                                   bool *present,
                                                   QString *error)
{
    if (!present || !explicitProfile(profile))
        return setError(error, QStringLiteral("Tracker-private state is unavailable for this profile."));
    *present = false;
    for (const QString &path : privateStatePaths(profile)) {
        if (QFileInfo::exists(path)) {
            *present = true;
            break;
        }
    }
    return true;
}

bool TrackerLifecycleCoordinator::adoptPrivateState(const ProfilePaths &source,
                                                      const ProfilePaths &destination,
                                                      TrackerCredentialVault &vault,
                                                      qint64 nowMs,
                                                      QString *error)
{
    if (source.kind() != ProfilePaths::Kind::LocalOnly
        || destination.kind() != ProfilePaths::Kind::Account
        || !explicitProfile(source) || !explicitProfile(destination)
        || !sameDevice(source, destination) || source.profileId() == destination.profileId()) {
        return setError(error, QStringLiteral(
            "Tracker-private adoption requires a LocalOnly profile and an account profile on this device."));
    }

    bool hasState = false;
    if (!hasPrivateState(source, &hasState, error))
        return false;
    if (!hasState)
        return true;

    TrackerConnectionStore sourceConnections(source);
    TrackerConnectionStore destinationConnections(destination);
    if (!sourceConnections.healthy(error) || !destinationConnections.healthy(error)
        || !sourceConnections.refresh(error) || !destinationConnections.refresh(error)) {
        return false;
    }

    const QList<TrackerConnection> sourceConnectionRows = sourceConnections.connections();
    for (const TrackerConnection &sourceConnection : sourceConnectionRows) {
        const auto target = destinationConnections.connection(sourceConnection.providerId);
        if (target && target->remoteAccountId != sourceConnection.remoteAccountId
            && target->state != TrackerConnectionState::Disconnected) {
            return setError(error, QStringLiteral(
                "The account profile already owns a different tracker account; no tracker state was adopted."));
        }
        if (target && target->remoteAccountId != sourceConnection.remoteAccountId) {
            return setError(error, QStringLiteral(
                "The account profile has a different saved tracker identity; review it before adoption."));
        }
        if (sourceConnection.state == TrackerConnectionState::TransferPending) {
            return setError(error, QStringLiteral(
                "The source tracker connection has an unfinished move that must be recovered first."));
        }
        if (sourceConnection.state == TrackerConnectionState::Connected && target
            && target->state == TrackerConnectionState::Connected) {
            return setError(error, QStringLiteral(
                "Both profiles appear to own the same tracker account; ownership must be resolved before adoption."));
        }
    }

    if (!writePrivateAdoptionCheckpoint(source, destination,
                                        QStringLiteral("transferring"), {}, error)) {
        return false;
    }

    TrackerMappingStore sourceMappings(source);
    TrackerMappingStore destinationMappings(destination);
    TrackerHistoryEvidenceStore sourceEvidence(source, &sourceMappings);
    TrackerHistoryEvidenceStore destinationEvidence(destination, &destinationMappings);
    TrackerImportStore sourceImports(source, &sourceMappings, &sourceConnections);
    TrackerImportStore destinationImports(destination, &destinationMappings,
                                          &destinationConnections);
    TrackerDeliveryStore sourceDelivery(source, &sourceMappings, &sourceConnections);
    TrackerDeliveryStore destinationDelivery(destination, &destinationMappings,
                                              &destinationConnections);
    TrackerScrobbleStore sourceScrobble(source);
    TrackerScrobbleStore destinationScrobble(destination);
    TrackerSyncSettingsStore sourceSettings(source);
    TrackerSyncSettingsStore destinationSettings(destination);
    if (!sourceMappings.healthy(error) || !destinationMappings.healthy(error)
        || !sourceEvidence.healthy(error) || !destinationEvidence.healthy(error)
        || !sourceImports.healthy(error) || !destinationImports.healthy(error)
        || !sourceDelivery.healthy(error) || !destinationDelivery.healthy(error)
        || !sourceScrobble.healthy(error) || !destinationScrobble.healthy(error)
        || !sourceSettings.healthy(error) || !destinationSettings.healthy(error)) {
        return false;
    }

    // Each owner performs an idempotent, profile-rebound merge. First-export
    // consent and send preferences remain destination-local; conflicts in
    // mappings, import decisions, or operation receipts fail closed.
    if (!destinationMappings.adoptPrivateStateFrom(sourceMappings, error)
        || !destinationEvidence.adoptPrivateStateFrom(sourceEvidence, error)
        || !destinationImports.adoptPrivateStateFrom(sourceImports, error)
        || !destinationDelivery.adoptPrivateStateFrom(sourceDelivery, error)
        || !destinationScrobble.adoptPrivateStateFrom(sourceScrobble, error)
        || !destinationSettings.adoptPrivateStateFrom(sourceSettings, error)) {
        return false;
    }

    QStringList needsAttention;
    for (const TrackerConnection &sourceConnection : sourceConnectionRows) {
        if (!sourceConnections.refresh(error) || !destinationConnections.refresh(error))
            return false;
        const auto currentSource = sourceConnections.connection(sourceConnection.providerId);
        const auto currentDestination = destinationConnections.connection(sourceConnection.providerId);
        if (!currentSource)
            return setError(error, QStringLiteral("The source tracker binding changed during adoption."));

        if (currentSource->state == TrackerConnectionState::Disconnected
            && (!currentDestination
                || currentDestination->state == TrackerConnectionState::Disconnected)) {
            if (!currentDestination) {
                TrackerConnection copy = *currentSource;
                copy.state = TrackerConnectionState::Disconnected;
                if (!destinationConnections.upsert(copy, error))
                    return false;
            }
            continue;
        }

        if (currentSource->state == TrackerConnectionState::Connected
            && (!currentDestination
                || currentDestination->state == TrackerConnectionState::Disconnected)) {
            const quint64 sourceNext = currentSource->connectionGeneration
                    == std::numeric_limits<quint64>::max()
                ? 0 : currentSource->connectionGeneration + 1;
            const quint64 destinationNext = destinationConnections.nextConnectionGeneration(
                sourceConnection.providerId);
            const quint64 generation = std::max(sourceNext, destinationNext);
            if (generation == 0) {
                return setError(error, QStringLiteral("Tracker connection generation is exhausted."));
            }
            const TrackerConnection pending{sourceConnection.providerId,
                                            currentSource->remoteAccountId,
                                            generation,
                                            nowMs,
                                            currentSource->capabilities,
                                            TrackerConnectionState::TransferPending};
            if (!destinationConnections.beginTransferFrom(source, pending, error)
                || !sourceConnections.setDisconnected(sourceConnection.providerId,
                                                      currentSource->remoteAccountId,
                                                      currentSource->connectionGeneration,
                                                      error)) {
                return false;
            }
        }

        QString transferError;
        const bool moved = moveConnection(source, destination, sourceConnection.providerId,
                                          vault, nowMs, &transferError);
        if (!sourceConnections.refresh(error) || !destinationConnections.refresh(error))
            return false;
        const auto sourceAfter = sourceConnections.connection(sourceConnection.providerId);
        const auto destinationAfter = destinationConnections.connection(sourceConnection.providerId);
        const bool safePausedTransfer = sourceAfter
            && sourceAfter->state == TrackerConnectionState::Disconnected
            && destinationAfter
            && (destinationAfter->state == TrackerConnectionState::TransferPending
                || destinationAfter->state == TrackerConnectionState::Connected)
            && destinationAfter->remoteAccountId == sourceConnection.remoteAccountId;
        if (!moved) {
            // Keep account adoption Promoted while a tracker binding is only
            // reserved. The account-adoption recovery path retries this
            // handoff; committing here would strand the source credential
            // without a production reader for this private checkpoint.
            if (safePausedTransfer) {
                needsAttention.append(trackerProviderKey(sourceConnection.providerId));
                std::sort(needsAttention.begin(), needsAttention.end());
                needsAttention.erase(std::unique(needsAttention.begin(), needsAttention.end()),
                                     needsAttention.end());
                QString checkpointError;
                writePrivateAdoptionCheckpoint(source, destination,
                                               QStringLiteral("transferring"),
                                               needsAttention, &checkpointError);
            }
            return setError(error, transferError.isEmpty()
                ? QStringLiteral("Tracker connection ownership could not be transferred safely.")
                : transferError);
        }
        if (!destinationAfter
            || destinationAfter->state != TrackerConnectionState::Connected)
            return setError(error, QStringLiteral(
                "Tracker connection ownership did not reach the destination profile."));
    }

    TrackerMappingStore verifiedMappings(destination);
    TrackerConnectionStore verifiedConnections(destination);
    TrackerHistoryEvidenceStore verifiedEvidence(destination, &verifiedMappings);
    TrackerImportStore verifiedImports(destination, &verifiedMappings, &verifiedConnections);
    TrackerDeliveryStore verifiedDelivery(destination, &verifiedMappings, &verifiedConnections);
    TrackerScrobbleStore verifiedScrobble(destination);
    TrackerSyncSettingsStore verifiedSettings(destination);
    if (!verifiedConnections.healthy(error) || !verifiedMappings.healthy(error)
        || !verifiedEvidence.healthy(error) || !verifiedImports.healthy(error)
        || !verifiedDelivery.healthy(error) || !verifiedScrobble.healthy(error)
        || !verifiedSettings.healthy(error)
        || verifiedMappings.mappings().size() < sourceMappings.mappings().size()
        || verifiedEvidence.contributions().size() < sourceEvidence.contributions().size()
        || verifiedImports.batches().size() < sourceImports.batches().size()
        || verifiedDelivery.operations().size() < sourceDelivery.operations().size()
        || verifiedScrobble.intents().size() < sourceScrobble.intents().size()) {
        return setError(error, QStringLiteral(
            "Tracker-private adoption failed destination readback; the source state was preserved."));
    }

    std::sort(needsAttention.begin(), needsAttention.end());
    needsAttention.erase(std::unique(needsAttention.begin(), needsAttention.end()),
                         needsAttention.end());
    return writePrivateAdoptionCheckpoint(source, destination,
                                          QStringLiteral("verified"),
                                          needsAttention, error);
}

bool TrackerLifecycleCoordinator::reconnect(
    const ProfilePaths &profile,
    TrackerProviderId providerId,
    const QString &verifiedRemoteAccountId,
    const TrackerReconnectActions &actions,
    QString *error)
{
    TrackerConnectionStore connections(profile);
    if (!connections.healthy(error) || !connections.refresh(error))
        return false;
    const auto connection = connections.connection(providerId);
    if (!connection || connection->state != TrackerConnectionState::Connected)
        return setError(error, QStringLiteral("Reconnect requires a verified active tracker binding."));
    if (connection->remoteAccountId != verifiedRemoteAccountId)
        return setError(error, QStringLiteral("The signed-in tracker account changed; review Change account first."));
    if (!actions.pull || !actions.reconcileUnknown || !actions.resumeSafePending)
        return setError(error, QStringLiteral("Tracker refresh is not available in this build."));

    const auto bindingStillCurrent = [&]() {
        if (!connections.refresh(error))
            return false;
        const auto current = connections.connection(providerId);
        if (current && current->state == TrackerConnectionState::Connected
            && current->remoteAccountId == connection->remoteAccountId
            && current->connectionGeneration == connection->connectionGeneration) {
            return true;
        }
        return setError(error, QStringLiteral(
            "Tracker connection changed during reconnect; remaining work was stopped."));
    };

    if (!actions.pull(*connection, error))
        return false;
    if (!bindingStillCurrent())
        return false;
    if (!actions.reconcileUnknown(*connection, error))
        return false;
    if (!bindingStillCurrent())
        return false;

    TrackerMappingStore mappings(profile);
    TrackerDeliveryStore delivery(profile, &mappings, &connections);
    TrackerScrobbleStore scrobble(profile);
    if (!mappings.healthy(error) || !delivery.healthy(error) || !scrobble.healthy(error)
        || !delivery.resumeKnownUnsentAfterReconnect(
            providerId, connection->remoteAccountId, connection->connectionGeneration, error)
        || !scrobble.resumeKnownUnsentAfterReconnect(
            providerId, connection->remoteAccountId, connection->connectionGeneration, error)) {
        return false;
    }
    if (!bindingStillCurrent())
        return false;
    if (!actions.resumeSafePending(*connection, error))
        return false;
    return bindingStillCurrent();
}

bool TrackerLifecycleCoordinator::moveConnection(
    const ProfilePaths &source,
    const ProfilePaths &destination,
    TrackerProviderId providerId,
    TrackerCredentialVault &vault,
    qint64 nowMs,
    QString *error)
{
    if (!explicitProfile(source) || !explicitProfile(destination)
        || !sameDevice(source, destination) || source.profileId() == destination.profileId()
        || !vault.isAvailable()) {
        return setError(error, QStringLiteral("Tracker connection cannot be moved between these profiles."));
    }

    TrackerConnectionStore sourceConnections(source);
    TrackerConnectionStore destinationConnections(destination);
    if (!sourceConnections.healthy(error) || !destinationConnections.healthy(error))
        return false;
    const auto sourceConnection = sourceConnections.connection(providerId);
    if (!sourceConnection
        || sourceConnection->state == TrackerConnectionState::TransferPending) {
        return setError(error, QStringLiteral("The source profile has no tracker connection to move."));
    }
    const auto destinationConnection = destinationConnections.connection(providerId);
    if (destinationConnection
        && destinationConnection->state != TrackerConnectionState::Disconnected
        && destinationConnection->remoteAccountId != sourceConnection->remoteAccountId) {
        return setError(error, QStringLiteral("The destination profile already has a different tracker account."));
    }

    const auto sourceCredential = vault.loadForProfile(source.profileId(), providerId);
    const bool destinationAlreadyBound = destinationConnection
        && destinationConnection->state == TrackerConnectionState::Connected
        && destinationConnection->remoteAccountId == sourceConnection->remoteAccountId;
    const bool destinationMovePending = destinationConnection
        && destinationConnection->state == TrackerConnectionState::TransferPending
        && destinationConnection->remoteAccountId == sourceConnection->remoteAccountId;
    const bool sourceWasConnected =
        sourceConnection->state == TrackerConnectionState::Connected;
    if (!sourceWasConnected && !destinationMovePending && !destinationAlreadyBound) {
        return setError(error, QStringLiteral(
            "Only an active tracker connection or an interrupted move can be transferred."));
    }
    if (sourceCredential
        && (sourceCredential->slot.profileId != source.profileId()
            || sourceCredential->slot.providerId != providerId
            || sourceCredential->slot.remoteAccountId != sourceConnection->remoteAccountId)) {
        return setError(error, QStringLiteral("The source tracker credential does not match its binding."));
    }

    auto destinationCredential = vault.loadForProfile(destination.profileId(), providerId);
    if (destinationCredential
        && destinationCredential->slot.remoteAccountId != sourceConnection->remoteAccountId) {
        return setError(error, QStringLiteral("The destination has a different stored tracker credential."));
    }

    const bool destinationCredentialReusable = destinationCredential
        && destinationCredential->slot.profileId == destination.profileId()
        && destinationCredential->slot.providerId == providerId
        && destinationCredential->slot.remoteAccountId == sourceConnection->remoteAccountId
        && trackerCredentialIsReusable(*destinationCredential, nowMs);
    if (!destinationCredentialReusable
        && (!sourceCredential || !trackerCredentialIsReusable(*sourceCredential, nowMs))) {
        return setError(error, QStringLiteral("A reusable tracker credential could not be read."));
    }

    TrackerMappingStore sourceMappings(source);
    TrackerMappingStore destinationMappings(destination);
    TrackerDeliveryStore sourceDelivery(source, &sourceMappings, &sourceConnections);
    TrackerDeliveryStore destinationDelivery(destination, &destinationMappings,
                                              &destinationConnections);
    TrackerScrobbleStore sourceScrobble(source);
    TrackerScrobbleStore destinationScrobble(destination);
    if (!sourceMappings.healthy(error) || !destinationMappings.healthy(error)
        || !sourceDelivery.healthy(error) || !destinationDelivery.healthy(error)
        || !sourceScrobble.healthy(error) || !destinationScrobble.healthy(error)) {
        return false;
    }
    bool wroteDestinationCredential = false;
    quint64 destinationGeneration = destinationAlreadyBound || destinationMovePending
        ? destinationConnection->connectionGeneration : 0;
    if (!destinationCredentialReusable) {
        if (!sourceCredential) {
            return setError(error, QStringLiteral("A reusable source credential is required to finish the move."));
        }
        TrackerCredential moved = *sourceCredential;
        moved.slot.profileId = destination.profileId();
        if (!vault.saveAndVerify(moved)
            || !vault.hasReusableCredential(moved.slot, nowMs)) {
            if (destinationCredential)
                vault.saveAndVerify(*destinationCredential);
            else
                vault.clearForProfile(destination.profileId(), providerId);
            return setError(error, QStringLiteral("The destination credential could not be verified; the source remains intact."));
        }
        wroteDestinationCredential = true;
    }

    if (!destinationAlreadyBound && !destinationMovePending) {
        if (!sourceWasConnected)
            return setError(error, QStringLiteral("The source connection changed before transfer."));
        const quint64 sourceNext = sourceConnection->connectionGeneration
            == std::numeric_limits<quint64>::max()
            ? 0 : sourceConnection->connectionGeneration + 1;
        const quint64 destinationNext = destinationConnections.nextConnectionGeneration(providerId);
        const quint64 generation = std::max(sourceNext, destinationNext);
        if (generation == 0) {
            if (wroteDestinationCredential)
                destinationCredential
                    ? vault.saveAndVerify(*destinationCredential)
                    : vault.clearForProfile(destination.profileId(), providerId);
            return setError(error, QStringLiteral("Tracker connection generation is exhausted."));
        }
        destinationGeneration = generation;
        const TrackerConnection movedConnection{
            providerId,
            sourceConnection->remoteAccountId,
            generation,
            nowMs,
            sourceConnection->capabilities,
            TrackerConnectionState::TransferPending};
        if (!destinationConnections.beginTransferFrom(source, movedConnection, error)) {
            if (wroteDestinationCredential)
                destinationCredential
                    ? vault.saveAndVerify(*destinationCredential)
                    : vault.clearForProfile(destination.profileId(), providerId);
            return false;
        }
    }

    // The destination's TransferPending row reserves this account before the
    // source claim is released. A failed continuation remains recoverable.
    if (sourceWasConnected
        && !sourceConnections.setDisconnected(providerId,
                                              sourceConnection->remoteAccountId,
                                              sourceConnection->connectionGeneration,
                                              error)) {
        return false;
    }

    if (!destinationAlreadyBound
        && !destinationConnections.restoreConnected(
            providerId, sourceConnection->remoteAccountId, destinationGeneration, error)) {
        return false;
    }

    if (!sourceDelivery.removeProviderSettingsFor(
            providerId, sourceConnection->remoteAccountId, error)
        || !sourceScrobble.removePreferenceFor(
            providerId, sourceConnection->remoteAccountId, error)) {
        return false;
    }

    // Source outboxes and playback intents remain source-profile-private and
    // are never rebound. Source send preferences are discarded; destination
    // preferences retain only decisions made in that profile.
    if (vault.loadForProfile(source.profileId(), providerId)
        && !vault.clearForProfile(source.profileId(), providerId)) {
        return setError(error, QStringLiteral(
            "The move is complete, but the source credential remains stored for cleanup."));
    }
    return true;
}

bool TrackerLifecycleCoordinator::disconnect(
    const ProfilePaths &profile,
    TrackerProviderId providerId,
    TrackerDisconnectChoice choice,
    TrackerCredentialVault &vault,
    QString *error)
{
    if (choice == TrackerDisconnectChoice::Cancel)
        return false;
    if (!explicitProfile(profile))
        return setError(error, QStringLiteral("Tracker disconnect is unavailable for this profile."));
    TrackerConnectionStore connections(profile);
    TrackerMappingStore mappings(profile);
    TrackerDeliveryStore delivery(profile, &mappings, &connections);
    TrackerScrobbleStore scrobble(profile);
    return disconnect(profile, providerId, choice, vault, connections,
                      mappings, delivery, scrobble, error);
}

bool TrackerLifecycleCoordinator::disconnect(
    const ProfilePaths &profile,
    TrackerProviderId providerId,
    TrackerDisconnectChoice choice,
    TrackerCredentialVault &vault,
    TrackerConnectionStore &connections,
    TrackerMappingStore &mappings,
    TrackerDeliveryStore &delivery,
    TrackerScrobbleStore &scrobble,
    QString *error)
{
    if (choice == TrackerDisconnectChoice::Cancel)
        return false;
    if (!explicitProfile(profile))
        return setError(error, QStringLiteral("Tracker disconnect is unavailable for this profile."));
    if (choice != TrackerDisconnectChoice::KeepPaused
        && choice != TrackerDisconnectChoice::DiscardKnownUnsent) {
        return setError(error, QStringLiteral("The tracker disconnect choice is invalid."));
    }

    if (!connections.healthy(error))
        return false;
    const auto connection = connections.connection(providerId);
    if (!connection)
        return setError(error, QStringLiteral("The tracker is not connected to this profile."));
    if (connection->state == TrackerConnectionState::TransferPending) {
        return setError(error, QStringLiteral(
            "Finish or recover the tracker connection move before disconnecting."));
    }
    // The disconnect safety scan is meaningful only when both durable queue
    // owners loaded completely. Otherwise an unreadable journal could hide an
    // in-flight operation and the direct lifecycle API might clear its token.
    if (!delivery.healthy(error) || !scrobble.healthy(error))
        return false;
    for (const TrackerDeliveryOperation &operation : delivery.operations()) {
        if (operation.providerId == providerId
            && operation.remoteAccountId == connection->remoteAccountId
            && operation.state == TrackerDeliveryState::Delivering) {
            return setError(error, QStringLiteral(
                "A tracker update is still sending; let it finish before disconnecting."));
        }
    }
    for (const TrackerScrobbleIntent &intent : scrobble.intents()) {
        if (intent.providerId == providerId
            && intent.remoteAccountId == connection->remoteAccountId
            && intent.state == TrackerScrobbleState::Delivering) {
            return setError(error, QStringLiteral(
                "A tracker playback update is still sending; let it finish before disconnecting."));
        }
    }
    if (choice == TrackerDisconnectChoice::DiscardKnownUnsent
        && !mappings.healthy(error)) {
        return false;
    }

    std::optional<TrackerCredential> savedCredential;
    if (providerId == TrackerProviderId::Simkl) {
        if (!vault.isAvailable())
            return setError(error, QStringLiteral(
                "The tracker credential vault is unavailable; the connection remains active."));
        savedCredential = vault.loadForProfile(profile.profileId(), providerId);
        if (savedCredential
            && savedCredential->slot.remoteAccountId != connection->remoteAccountId) {
            return setError(error, QStringLiteral(
                "The saved SIMKL credential belongs to a different account."));
        }
        if (!vault.clearForProfile(profile.profileId(), providerId)) {
            if (savedCredential && !vault.saveAndVerify(*savedCredential)) {
                return setError(error, QStringLiteral(
                    "Credential cleanup failed and the previous credential could not be restored."));
            }
            return setError(error, QStringLiteral(
                "The tracker credential could not be removed; the connection remains active."));
        }
    }

    const auto restoreCredential = [&]() {
        if (providerId == TrackerProviderId::Simkl && savedCredential
            && !vault.saveAndVerify(*savedCredential)) {
            return setError(error, QStringLiteral(
                "The tracker action stopped, but its previous credential could not be restored."));
        }
        return true;
    };
    if (connection->state == TrackerConnectionState::Connected
        && !connections.setDisconnected(providerId, connection->remoteAccountId,
                                        connection->connectionGeneration, error)) {
        restoreCredential();
        return false;
    }
    if (choice == TrackerDisconnectChoice::DiscardKnownUnsent) {
        if (delivery.discardKnownUnsent(providerId, connection->remoteAccountId, error) < 0
            || scrobble.discardKnownUnsent(providerId, connection->remoteAccountId, error) < 0) {
            // The connection and credential are already fenced off. A later
            // retry can discard whichever known-unsent rows remain without
            // allowing a partially cleaned queue to reach the provider.
            setError(error, QStringLiteral(
                "The tracker is disconnected, but some known-unsent updates remain paused. "
                "Retry cleanup from Sync; uncertain outcomes were preserved."));
            return false;
        }
    }
    return true;
}

int TrackerLifecycleCoordinator::removeImportedHistory(
    const ProfilePaths &profile,
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    QString *error)
{
    if (!explicitProfile(profile)) {
        setError(error, QStringLiteral("Imported tracker History is unavailable for this profile."));
        return -1;
    }
    TrackerMappingStore mappings(profile);
    TrackerHistoryEvidenceStore evidence(profile, &mappings);
    if (!evidence.healthy(error))
        return -1;
    return evidence.removeSource(providerId, remoteAccountId, error);
}

bool TrackerLifecycleCoordinator::removeProfilePrivateStateForPermanentDeletion(
    const ProfilePaths &profile,
    TrackerCredentialVault &vault,
    QString *error)
{
    if (!explicitProfile(profile))
        return setError(error, QStringLiteral("Tracker-private profile removal is unavailable."));
    if (!vault.isAvailable())
        return setError(error, QStringLiteral("The tracker credential vault is unavailable."));
    // The current vault owns only the SIMKL namespace. Add each future
    // provider here when its credential namespace is implemented.
    if (!vault.clearForProfile(profile.profileId(), TrackerProviderId::Simkl)) {
        return setError(error, QStringLiteral(
            "Tracker credentials could not be removed from the local vault."));
    }

    const QStringList trackerFiles{
        TrackerConnectionStore::storagePath(profile),
        TrackerMappingStore::storagePath(profile),
        TrackerHistoryEvidenceStore::storagePath(profile),
        TrackerImportStore::storagePath(profile),
        TrackerDeliveryStore::storagePath(profile),
        TrackerScrobbleStore::storagePath(profile),
        TrackerSyncSettingsStore::storagePath(profile),
        privateAdoptionCheckpointPath(profile)};
    for (const QString &path : trackerFiles) {
        if (!removeFileIfPresent(path, error))
            return false;
    }
    return true;
}
