#include "TrackerConnectionStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>

#include <cmath>
#include <limits>
#include <memory>

namespace {

constexpr int kTrackerConnectionSchemaVersion = 2;
constexpr auto kConnectionFileName = "tracker-connections.json";

enum class ReadResult {
    Missing,
    Loaded,
    Invalid
};

bool setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString absolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString claimsLockPath(const ProfilePaths &profile)
{
    return QDir::cleanPath(profile.appDataRoot()
                           + QStringLiteral("/tracker-connection-claims.lock"));
}

std::unique_ptr<QLockFile> acquireClaimsLock(const ProfilePaths &profile, QString *error)
{
    if (profile.appDataRoot().trimmed().isEmpty()) {
        setError(error, QStringLiteral("Tracker account ownership could not be verified."));
        return nullptr;
    }
    const QString path = claimsLockPath(profile);
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        setError(error, QStringLiteral("Tracker account ownership could not be verified."));
        return nullptr;
    }
    auto lock = std::make_unique<QLockFile>(path);
    lock->setStaleLockTime(30000);
    if (!lock->tryLock(3000)) {
        setError(error, QStringLiteral("Tracker account ownership is busy; retry safely."));
        return nullptr;
    }
    return lock;
}

QJsonObject connectionToJson(const TrackerConnection &connection)
{
    return {
        {QStringLiteral("providerId"), trackerProviderKey(connection.providerId)},
        {QStringLiteral("remoteAccountId"), connection.remoteAccountId},
        {QStringLiteral("connectionGeneration"), QString::number(connection.connectionGeneration)},
        {QStringLiteral("verifiedAtMs"), QString::number(connection.verifiedAtMs)},
        {QStringLiteral("capabilities"), static_cast<qint64>(connection.capabilities.toInt())},
        {QStringLiteral("state"), connection.state == TrackerConnectionState::Connected
                                       ? QStringLiteral("connected")
                                   : connection.state == TrackerConnectionState::TransferPending
                                       ? QStringLiteral("transfer_pending")
                                       : QStringLiteral("disconnected")}
    };
}

std::optional<TrackerConnection> connectionFromJson(const QJsonObject &json,
                                                    QString *error)
{
    const auto providerId = trackerProviderIdFromKey(
        json.value(QStringLiteral("providerId")).toString());
    const QString remoteAccountId = json.value(
        QStringLiteral("remoteAccountId")).toString().trimmed();
    bool generationOk = false;
    const quint64 generation = json.value(
        QStringLiteral("connectionGeneration")).toString().toULongLong(&generationOk);
    bool verifiedAtOk = false;
    const qint64 verifiedAtMs = json.value(
        QStringLiteral("verifiedAtMs")).toString().toLongLong(&verifiedAtOk);
    const QJsonValue capabilitiesValue = json.value(QStringLiteral("capabilities"));
    const double rawCapabilities = capabilitiesValue.toDouble();

    if (!providerId || remoteAccountId.isEmpty() || !generationOk || generation == 0
        || !verifiedAtOk || !capabilitiesValue.isDouble()
        || !std::isfinite(rawCapabilities)
        || std::floor(rawCapabilities) != rawCapabilities
        || rawCapabilities < 0
        || rawCapabilities > std::numeric_limits<int>::max()
        || (json.value(QStringLiteral("state")).toString() != QLatin1String("connected")
            && json.value(QStringLiteral("state")).toString() != QLatin1String("disconnected")
            && json.value(QStringLiteral("state")).toString() != QLatin1String("transfer_pending"))) {
        setError(error, QStringLiteral("Tracker connection entry is malformed."));
        return std::nullopt;
    }

    const TrackerProviderCapabilities parsedCapabilities =
        TrackerProviderCapabilities::fromInt(static_cast<int>(rawCapabilities));
    if (!trackerProviderCapabilitiesAreKnown(parsedCapabilities)) {
        setError(error, QStringLiteral("Tracker connection entry requests unsupported capabilities."));
        return std::nullopt;
    }

    return TrackerConnection{
        *providerId,
        remoteAccountId,
        generation,
        verifiedAtMs,
        parsedCapabilities,
        json.value(QStringLiteral("state")).toString() == QLatin1String("connected")
            ? TrackerConnectionState::Connected
            : json.value(QStringLiteral("state")).toString() == QLatin1String("transfer_pending")
                ? TrackerConnectionState::TransferPending
                : TrackerConnectionState::Disconnected};
}

ReadResult readConnections(const QString &path,
                           const QString &expectedProfileId,
                           QList<TrackerConnection> *connections,
                           QString *error)
{
    QFile file(path);
    if (!file.exists())
        return ReadResult::Missing;
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Tracker connections could not be opened."));
        return ReadResult::Invalid;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Tracker connections are malformed."));
        return ReadResult::Invalid;
    }

    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt();
    if ((version != 1 && version != kTrackerConnectionSchemaVersion)
        || root.value(QStringLiteral("profileId")).toString() != expectedProfileId
        || !root.value(QStringLiteral("connections")).isArray()) {
        setError(error, QStringLiteral("Tracker connections have an unsupported or foreign schema."));
        return ReadResult::Invalid;
    }

    QList<TrackerConnection> loaded;
    QSet<QString> providers;
    for (const QJsonValue &value : root.value(QStringLiteral("connections")).toArray()) {
        if (!value.isObject()) {
            setError(error, QStringLiteral("Tracker connection entry is malformed."));
            return ReadResult::Invalid;
        }
        QString entryError;
        const auto connection = connectionFromJson(value.toObject(), &entryError);
        if (!connection) {
            setError(error, entryError);
            return ReadResult::Invalid;
        }
        const QString key = trackerProviderKey(connection->providerId);
        if (providers.contains(key)) {
            setError(error, QStringLiteral("Tracker connections contain duplicate providers."));
            return ReadResult::Invalid;
        }
        providers.insert(key);
        loaded.append(*connection);
    }

    if (connections)
        *connections = loaded;
    return ReadResult::Loaded;
}

bool validConnection(const TrackerConnection &connection, QString *error)
{
    if (trackerProviderKey(connection.providerId).isEmpty()
        || connection.remoteAccountId.trimmed().isEmpty()
        || connection.connectionGeneration == 0
        || !trackerProviderCapabilitiesAreKnown(connection.capabilities)
        || (connection.state != TrackerConnectionState::Connected
            && connection.state != TrackerConnectionState::Disconnected
            && connection.state != TrackerConnectionState::TransferPending)) {
        return setError(error, QStringLiteral("Tracker connection is invalid."));
    }
    return true;
}

} // namespace

TrackerConnectionStore::TrackerConnectionStore(const ProfilePaths &profile)
    : m_profile(profile),
      m_path(storagePath(profile))
{
    if (m_path.isEmpty()) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Tracker connections are unavailable for this profile.");
    } else {
        load();
    }
}

QString TrackerConnectionStore::storagePath(const ProfilePaths &profile)
{
    // LegacyLocal has no ProfilePaths child root. Its tracker-private adoption
    // needs the existing device-private lifecycle owner and is intentionally
    // deferred to Arc 35 Slice 7; this foundation must not invent a parallel
    // legacy path or claims file.
    if (profile.kind() == ProfilePaths::Kind::Sealed
        || profile.kind() == ProfilePaths::Kind::LegacyLocal
        || profile.profileRoot().isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(profile.profileRoot()
                           + QLatin1Char('/')
                           + QLatin1String(kConnectionFileName));
}

bool TrackerConnectionStore::healthy(QString *error) const
{
    if (!m_healthy && error)
        *error = m_persistenceError;
    return m_healthy;
}

bool TrackerConnectionStore::refresh(QString *error)
{
    if (!healthy(error))
        return false;
    const auto claimsLock = acquireClaimsLock(m_profile, error);
    return claimsLock && refreshFromDisk(error);
}

QString TrackerConnectionStore::persistenceError() const
{
    return m_persistenceError;
}

QList<TrackerConnection> TrackerConnectionStore::connections() const
{
    return m_connections;
}

std::optional<TrackerConnection> TrackerConnectionStore::connection(
    TrackerProviderId providerId) const
{
    for (const TrackerConnection &entry : m_connections) {
        if (entry.providerId == providerId)
            return entry;
    }
    return std::nullopt;
}

quint64 TrackerConnectionStore::nextConnectionGeneration(
    TrackerProviderId providerId) const
{
    const auto existing = connection(providerId);
    if (!existing)
        return 1;
    if (existing->connectionGeneration == std::numeric_limits<quint64>::max())
        return 0;
    return existing->connectionGeneration + 1;
}

bool TrackerConnectionStore::upsert(const TrackerConnection &connection,
                                    QString *error)
{
    if (!healthy(error) || !validConnection(connection, error)
        || connection.state == TrackerConnectionState::TransferPending) {
        if (connection.state == TrackerConnectionState::TransferPending)
            setError(error, QStringLiteral("Only the transfer lifecycle can reserve a tracker account."));
        return false;
    }

    const auto claimsLock = acquireClaimsLock(m_profile, error);
    if (!claimsLock || !refreshFromDisk(error)) {
        return false;
    }

    const auto existing = this->connection(connection.providerId);
    if (existing) {
        if (existing->state == TrackerConnectionState::TransferPending)
            return setError(error, QStringLiteral("A tracker connection move needs recovery first."));
        if (existing->state == TrackerConnectionState::Connected
            && existing->remoteAccountId != connection.remoteAccountId) {
            return setError(error, QStringLiteral(
                "Changing tracker accounts requires disconnecting the current account first."));
        }
        if (connection.connectionGeneration <= existing->connectionGeneration)
            return setError(error, QStringLiteral("Tracker connection generation is stale."));
    }

    QString ownershipError;
    const auto claim = externalAccountClaimUnlocked(
        connection.providerId, connection.remoteAccountId, nullptr, &ownershipError);
    if (claim == ExternalAccountClaim::Indeterminate)
        return setError(error, ownershipError);
    if (claim == ExternalAccountClaim::Claimed)
        return setError(error, QStringLiteral("This %1 account is already connected to another profile.")
                            .arg(trackerProviderDisplayName(connection.providerId)));

    QList<TrackerConnection> candidate = m_connections;
    bool replaced = false;
    for (TrackerConnection &current : candidate) {
        if (current.providerId == connection.providerId) {
            current = connection;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        candidate.append(connection);
    if (!persist(candidate, error))
        return false;
    m_connections = candidate;
    return true;
}

bool TrackerConnectionStore::beginTransferFrom(
    const ProfilePaths &sourceProfile,
    const TrackerConnection &transferPending,
    QString *error)
{
    if (!healthy(error) || !validConnection(transferPending, error)
        || transferPending.state != TrackerConnectionState::TransferPending
        || sourceProfile.profileRoot().isEmpty()
        || sourceProfile.profileId().isEmpty()
        || sourceProfile.profileId() == m_profile.profileId()
        || QDir::cleanPath(sourceProfile.appDataRoot())
            != QDir::cleanPath(m_profile.appDataRoot())) {
        return setError(error, QStringLiteral("Tracker transfer reservation is invalid."));
    }

    const auto claimsLock = acquireClaimsLock(m_profile, error);
    if (!claimsLock || !refreshFromDisk(error))
        return false;

    TrackerConnectionStore source(sourceProfile);
    if (!source.healthy(error) || !source.refreshFromDisk(error))
        return false;
    const auto sourceConnection = source.connection(transferPending.providerId);
    if (!sourceConnection
        || sourceConnection->state != TrackerConnectionState::Connected
        || sourceConnection->remoteAccountId != transferPending.remoteAccountId
        || transferPending.connectionGeneration <= sourceConnection->connectionGeneration) {
        return setError(error, QStringLiteral("The source tracker binding changed before transfer."));
    }

    const auto existing = connection(transferPending.providerId);
    if (existing) {
        if (existing->state == TrackerConnectionState::TransferPending
            && existing->remoteAccountId == transferPending.remoteAccountId
            && existing->connectionGeneration == transferPending.connectionGeneration) {
            return true;
        }
        if (existing->state == TrackerConnectionState::Connected
            || existing->remoteAccountId != transferPending.remoteAccountId
            || transferPending.connectionGeneration <= existing->connectionGeneration) {
            return setError(error, QStringLiteral("The destination tracker binding changed before transfer."));
        }
    }

    QString ownershipError;
    const auto claim = source.externalAccountClaimUnlocked(
        transferPending.providerId, transferPending.remoteAccountId, nullptr, &ownershipError);
    if (claim == ExternalAccountClaim::Indeterminate)
        return setError(error, ownershipError);
    if (claim == ExternalAccountClaim::Claimed)
        return setError(error, QStringLiteral("This %1 account is already reserved by another profile.")
                            .arg(trackerProviderDisplayName(transferPending.providerId)));

    QList<TrackerConnection> candidate = m_connections;
    bool replaced = false;
    for (TrackerConnection &current : candidate) {
        if (current.providerId == transferPending.providerId) {
            current = transferPending;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        candidate.append(transferPending);
    if (!persist(candidate, error))
        return false;
    m_connections = candidate;
    return true;
}

bool TrackerConnectionStore::setDisconnected(
    TrackerProviderId providerId,
    const QString &expectedRemoteAccountId,
    quint64 expectedGeneration,
    QString *error)
{
    if (!healthy(error) || trackerProviderKey(providerId).isEmpty()
        || expectedRemoteAccountId.trimmed().isEmpty() || expectedGeneration == 0) {
        return setError(error, QStringLiteral("Tracker disconnect request is invalid."));
    }

    const auto claimsLock = acquireClaimsLock(m_profile, error);
    if (!claimsLock || !refreshFromDisk(error)) {
        return false;
    }

    QList<TrackerConnection> candidate = m_connections;
    bool found = false;
    for (TrackerConnection &current : candidate) {
        if (current.providerId != providerId)
            continue;
        if (current.remoteAccountId != expectedRemoteAccountId
            || current.connectionGeneration != expectedGeneration) {
            return setError(error, QStringLiteral("Tracker connection changed before disconnect."));
        }
        current.state = TrackerConnectionState::Disconnected;
        found = true;
        break;
    }
    if (!found)
        return setError(error, QStringLiteral("Tracker connection was not found."));
    if (!persist(candidate, error))
        return false;
    m_connections = candidate;
    return true;
}

bool TrackerConnectionStore::restoreConnected(
    TrackerProviderId providerId,
    const QString &expectedRemoteAccountId,
    quint64 expectedGeneration,
    QString *error)
{
    if (!healthy(error) || trackerProviderKey(providerId).isEmpty()
        || expectedRemoteAccountId.trimmed().isEmpty() || expectedGeneration == 0) {
        return setError(error, QStringLiteral("Tracker connection recovery is invalid."));
    }

    const auto claimsLock = acquireClaimsLock(m_profile, error);
    if (!claimsLock || !refreshFromDisk(error))
        return false;

    QString ownershipError;
    const auto claim = externalAccountClaimUnlocked(
        providerId, expectedRemoteAccountId, nullptr, &ownershipError);
    if (claim == ExternalAccountClaim::Indeterminate)
        return setError(error, ownershipError);
    if (claim == ExternalAccountClaim::Claimed)
        return setError(error, QStringLiteral("This %1 account was claimed before recovery completed.")
                            .arg(trackerProviderDisplayName(providerId)));

    QList<TrackerConnection> candidate = m_connections;
    bool found = false;
    for (TrackerConnection &current : candidate) {
        if (current.providerId != providerId)
            continue;
        if (current.remoteAccountId != expectedRemoteAccountId
            || current.connectionGeneration != expectedGeneration
            || (current.state != TrackerConnectionState::Disconnected
                && current.state != TrackerConnectionState::TransferPending)) {
            return setError(error, QStringLiteral("Tracker connection changed before recovery."));
        }
        current.state = TrackerConnectionState::Connected;
        found = true;
        break;
    }
    if (!found)
        return setError(error, QStringLiteral("Tracker connection was not found."));
    if (!persist(candidate, error))
        return false;
    m_connections = candidate;
    return true;
}

TrackerConnectionStore::ExternalAccountClaim
TrackerConnectionStore::externalAccountClaim(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    QString *claimingProfileId,
    QString *error) const
{
    if (!m_healthy || remoteAccountId.trimmed().isEmpty()
        || m_profile.appDataRoot().trimmed().isEmpty()) {
        setError(error, QStringLiteral("Tracker account ownership could not be verified."));
        return ExternalAccountClaim::Indeterminate;
    }
    const auto claimsLock = acquireClaimsLock(m_profile, error);
    if (!claimsLock)
        return ExternalAccountClaim::Indeterminate;
    return externalAccountClaimUnlocked(providerId, remoteAccountId,
                                        claimingProfileId, error);
}

TrackerConnectionStore::ExternalAccountClaim
TrackerConnectionStore::externalAccountClaimUnlocked(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    QString *claimingProfileId,
    QString *error) const
{
    const QString stableId = remoteAccountId.trimmed();
    const QString profilesRoot = QDir::cleanPath(m_profile.appDataRoot()
                                                  + QStringLiteral("/profiles"));
    if (!m_healthy || stableId.isEmpty() || profilesRoot.isEmpty()) {
        setError(error, QStringLiteral("Tracker account ownership could not be verified."));
        return ExternalAccountClaim::Indeterminate;
    }

    const QDir root(profilesRoot);
    if (!root.exists())
        return ExternalAccountClaim::Unclaimed;

    const QString ownPath = absolutePath(m_path);
    const QStringList profileDirectories = root.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &directory : profileDirectories) {
        const QString path = QDir::cleanPath(root.filePath(
            directory + QLatin1Char('/') + QLatin1String(kConnectionFileName)));
        if (absolutePath(path) == ownPath)
            continue;

        QList<TrackerConnection> entries;
        QString readError;
        const ReadResult result = readConnections(path, directory, &entries, &readError);
        if (result == ReadResult::Missing)
            continue;
        if (result == ReadResult::Invalid) {
            setError(error, QStringLiteral("Tracker account ownership could not be verified: %1")
                         .arg(readError));
            return ExternalAccountClaim::Indeterminate;
        }

        for (const TrackerConnection &entry : entries) {
            if (entry.state != TrackerConnectionState::Disconnected
                && entry.providerId == providerId
                && entry.remoteAccountId == stableId) {
                if (claimingProfileId)
                    *claimingProfileId = directory;
                return ExternalAccountClaim::Claimed;
            }
        }
    }
    return ExternalAccountClaim::Unclaimed;
}

bool TrackerConnectionStore::load()
{
    QList<TrackerConnection> loaded;
    QString error;
    const ReadResult result = readConnections(m_path, m_profile.profileId(), &loaded, &error);
    if (result == ReadResult::Missing)
        return true;
    if (result == ReadResult::Invalid) {
        m_healthy = false;
        m_persistenceError = error;
        return false;
    }
    m_connections = loaded;
    return true;
}

bool TrackerConnectionStore::refreshFromDisk(QString *error)
{
    QList<TrackerConnection> loaded;
    QString readError;
    const ReadResult result = readConnections(m_path, m_profile.profileId(), &loaded, &readError);
    if (result == ReadResult::Invalid) {
        m_healthy = false;
        m_persistenceError = readError;
        return setError(error, readError);
    }
    m_connections = result == ReadResult::Loaded ? loaded : QList<TrackerConnection>();
    return true;
}

bool TrackerConnectionStore::persist(const QList<TrackerConnection> &candidate,
                                     QString *error) const
{
    QSet<QString> providers;
    QJsonArray entries;
    for (const TrackerConnection &connection : candidate) {
        if (!validConnection(connection, error))
            return false;
        const QString providerKey = trackerProviderKey(connection.providerId);
        if (providers.contains(providerKey))
            return setError(error, QStringLiteral("Tracker connections contain duplicate providers."));
        providers.insert(providerKey);
        entries.append(connectionToJson(connection));
    }

    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.dir().absolutePath()))
        return setError(error, QStringLiteral("Tracker connection directory could not be created."));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("Tracker connections could not be written."));
    file.write(QJsonDocument({
        {QStringLiteral("version"), kTrackerConnectionSchemaVersion},
        {QStringLiteral("profileId"), m_profile.profileId()},
        {QStringLiteral("connections"), entries}
    }).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return setError(error, QStringLiteral("Tracker connections could not be saved atomically."));
    return true;
}
