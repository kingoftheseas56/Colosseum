#include "TrackerMappingStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace {

constexpr int kSchemaVersion = 1;
constexpr auto kFileName = "tracker-mappings.json";

bool error(QString *out, const QString &message)
{
    if (out)
        *out = message;
    return false;
}

bool safeText(const QString &value, int maximum = 256)
{
    return !value.isEmpty() && value == value.trimmed() && value.size() <= maximum
        && !QDir::isAbsolutePath(value) && !value.contains(QLatin1String(".."));
}

bool validRemote(const TrackerRemoteMediaKey &remote)
{
    return !trackerProviderKey(remote.providerId).isEmpty()
        && safeText(remote.remoteAccountId, 128)
        && safeText(remote.remoteMediaId);
}

bool validCandidate(const TrackerCanonicalTitleCandidate &candidate)
{
    return safeText(candidate.canonicalMediaId) && safeText(candidate.historyKind, 64)
        && safeText(candidate.historyId) && !candidate.displayName.trimmed().isEmpty()
        && candidate.displayName == candidate.displayName.trimmed()
        && candidate.displayName.size() <= 512;
}

QString key(const TrackerRemoteMediaKey &remote)
{
    return trackerProviderKey(remote.providerId) + QChar(0x1f) + remote.remoteAccountId
        + QChar(0x1f) + remote.remoteMediaId;
}

QString provenanceKey(TrackerMappingProvenance value)
{
    return value == TrackerMappingProvenance::ExactProviderIdentity
        ? QStringLiteral("exact_provider_identity")
        : QStringLiteral("user_confirmed");
}

std::optional<TrackerMappingProvenance> provenanceFromKey(const QString &value)
{
    if (value == QLatin1String("exact_provider_identity"))
        return TrackerMappingProvenance::ExactProviderIdentity;
    if (value == QLatin1String("user_confirmed"))
        return TrackerMappingProvenance::UserConfirmed;
    return std::nullopt;
}

QJsonObject toJson(const TrackerTitleMapping &mapping)
{
    return {{QStringLiteral("providerId"), trackerProviderKey(mapping.remote.providerId)},
            {QStringLiteral("remoteAccountId"), mapping.remote.remoteAccountId},
            {QStringLiteral("remoteMediaId"), mapping.remote.remoteMediaId},
            {QStringLiteral("canonicalMediaId"), mapping.canonical.canonicalMediaId},
            {QStringLiteral("historyKind"), mapping.canonical.historyKind},
            {QStringLiteral("historyId"), mapping.canonical.historyId},
            {QStringLiteral("displayName"), mapping.canonical.displayName},
            {QStringLiteral("provenance"), provenanceKey(mapping.provenance)},
            {QStringLiteral("revision"), QString::number(mapping.revision)}};
}

std::optional<TrackerTitleMapping> fromJson(const QJsonObject &object)
{
    const auto provider = trackerProviderIdFromKey(object.value(QStringLiteral("providerId")).toString());
    const auto provenance = provenanceFromKey(object.value(QStringLiteral("provenance")).toString());
    bool revisionOk = false;
    const quint64 revision = object.value(QStringLiteral("revision")).toString().toULongLong(&revisionOk);
    TrackerTitleMapping mapping{{provider.value_or(TrackerProviderId::Mal),
                                 object.value(QStringLiteral("remoteAccountId")).toString(),
                                 object.value(QStringLiteral("remoteMediaId")).toString()},
                                {object.value(QStringLiteral("canonicalMediaId")).toString(),
                                 object.value(QStringLiteral("historyKind")).toString(),
                                 object.value(QStringLiteral("historyId")).toString(),
                                 object.value(QStringLiteral("displayName")).toString()},
                                provenance.value_or(TrackerMappingProvenance::ExactProviderIdentity),
                                revision};
    if (!provider || !provenance || !revisionOk || revision == 0
        || !validRemote(mapping.remote) || !validCandidate(mapping.canonical)) {
        return std::nullopt;
    }
    return mapping;
}

} // namespace

TrackerMappingStore::TrackerMappingStore(const ProfilePaths &profile)
    : m_profile(profile), m_path(storagePath(profile))
{
    if (m_path.isEmpty()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker mappings are unavailable for this profile.");
    } else {
        load();
    }
}

QString TrackerMappingStore::storagePath(const ProfilePaths &profile)
{
    if (profile.kind() == ProfilePaths::Kind::Sealed
        || profile.kind() == ProfilePaths::Kind::LegacyLocal || profile.profileRoot().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(profile.profileRoot() + QLatin1Char('/') + QLatin1String(kFileName));
}

bool TrackerMappingStore::healthy(QString *out) const
{
    if (!m_healthy && out)
        *out = m_error;
    return m_healthy;
}

QList<TrackerTitleMapping> TrackerMappingStore::mappings() const { return m_mappings; }

std::optional<TrackerTitleMapping> TrackerMappingStore::mapping(const TrackerRemoteMediaKey &remote) const
{
    for (const TrackerTitleMapping &entry : m_mappings) {
        if (key(entry.remote) == key(remote))
            return entry;
    }
    return std::nullopt;
}

bool TrackerMappingStore::adoptPrivateStateFrom(const TrackerMappingStore &source,
                                                 QString *out)
{
    if (!healthy(out) || !source.healthy(out) || this == &source
        || m_profile.profileId() == source.m_profile.profileId()) {
        return error(out, QStringLiteral("Tracker mappings cannot be adopted between these profiles."));
    }

    QList<TrackerTitleMapping> candidate = m_mappings;
    bool changed = false;
    for (const TrackerTitleMapping &mapping : source.m_mappings) {
        const auto found = std::find_if(candidate.begin(), candidate.end(),
            [&mapping](const TrackerTitleMapping &entry) {
                return key(entry.remote) == key(mapping.remote);
            });
        if (found == candidate.end()) {
            candidate.append(mapping);
            changed = true;
            continue;
        }
        if (toJson(*found) != toJson(mapping)) {
            return error(out, QStringLiteral(
                "The destination has a different mapping for a tracker title; it was left unchanged."));
        }
    }
    if (!changed)
        return true;
    if (!persist(candidate, out))
        return false;
    m_mappings = candidate;
    return true;
}

bool TrackerMappingStore::upsert(const TrackerRemoteMediaKey &remote,
                                 const TrackerCanonicalTitleCandidate &candidate,
                                 TrackerMappingProvenance provenance,
                                 QString *out)
{
    if (!healthy(out) || !validRemote(remote) || !validCandidate(candidate))
        return error(out, QStringLiteral("Tracker mapping is invalid."));
    QList<TrackerTitleMapping> next = m_mappings;
    bool found = false;
    for (TrackerTitleMapping &entry : next) {
        if (key(entry.remote) != key(remote))
            continue;
        entry = {remote, candidate, provenance, entry.revision + 1};
        found = true;
        break;
    }
    if (!found)
        next.append({remote, candidate, provenance, 1});
    if (!persist(next, out))
        return false;
    m_mappings = next;
    return true;
}

bool TrackerMappingStore::remove(const TrackerRemoteMediaKey &remote, QString *out)
{
    if (!healthy(out) || !validRemote(remote))
        return error(out, QStringLiteral("Tracker mapping is invalid."));
    QList<TrackerTitleMapping> next = m_mappings;
    const auto oldSize = next.size();
    next.erase(std::remove_if(next.begin(), next.end(), [&remote](const TrackerTitleMapping &entry) {
        return key(entry.remote) == key(remote);
    }), next.end());
    if (next.size() == oldSize)
        return true;
#ifdef COLOSSEUM_TRACKER_MAPPING_TESTING
    if (m_failNextRemovalPersistence) {
        m_failNextRemovalPersistence = false;
        return error(out, QStringLiteral("Injected tracker mapping removal failure."));
    }
#endif
    if (!persist(next, out))
        return false;
    m_mappings = next;
    return true;
}

bool TrackerMappingStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker mappings could not be opened.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || root.value(QStringLiteral("version")).toInt() != kSchemaVersion
        || root.value(QStringLiteral("profileId")).toString() != m_profile.profileId()
        || !root.value(QStringLiteral("mappings")).isArray()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker mappings are malformed or foreign.");
        return false;
    }
    QSet<QString> keys;
    QList<TrackerTitleMapping> loaded;
    for (const QJsonValue &value : root.value(QStringLiteral("mappings")).toArray()) {
        if (!value.isObject()) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker mapping entry is malformed.");
            return false;
        }
        const auto entry = fromJson(value.toObject());
        if (!entry || keys.contains(key(entry->remote))) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker mapping entry is invalid or duplicated.");
            return false;
        }
        keys.insert(key(entry->remote));
        loaded.append(*entry);
    }
    m_mappings = loaded;
    return true;
}

bool TrackerMappingStore::persist(const QList<TrackerTitleMapping> &candidate, QString *out) const
{
    QJsonArray entries;
    QSet<QString> keys;
    for (const TrackerTitleMapping &mapping : candidate) {
        if (!validRemote(mapping.remote) || !validCandidate(mapping.canonical)
            || mapping.revision == 0 || keys.contains(key(mapping.remote))) {
            return error(out, QStringLiteral("Tracker mapping persistence is invalid."));
        }
        keys.insert(key(mapping.remote));
        entries.append(toJson(mapping));
    }
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.dir().absolutePath()))
        return error(out, QStringLiteral("Tracker mapping directory could not be created."));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return error(out, QStringLiteral("Tracker mappings could not be written."));
    file.write(QJsonDocument({{QStringLiteral("version"), kSchemaVersion},
                              {QStringLiteral("profileId"), m_profile.profileId()},
                              {QStringLiteral("mappings"), entries}}).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return error(out, QStringLiteral("Tracker mappings could not be saved atomically."));
    return true;
}

TrackerExactMappingService::TrackerExactMappingService(
    TrackerMappingStore *store, const TrackerCanonicalTitleIndex *index)
    : m_store(store), m_index(index)
{
}

bool TrackerExactMappingService::mapExact(const TrackerRemoteMediaKey &remote, QString *out)
{
    if (!m_store || !m_index)
        return error(out, QStringLiteral("Tracker mapping owner is unavailable."));
    const auto candidate = m_index->exactCandidate(remote);
    if (!candidate)
        return error(out, QStringLiteral("No exact canonical identity exists for this tracker item."));
    return m_store->upsert(remote, *candidate, TrackerMappingProvenance::ExactProviderIdentity, out);
}

QList<TrackerCanonicalTitleCandidate> TrackerExactMappingService::findMatchCandidates() const
{
    return m_index ? m_index->userCandidates() : QList<TrackerCanonicalTitleCandidate>();
}

bool TrackerExactMappingService::confirmCandidate(const TrackerRemoteMediaKey &remote,
                                                   const QString &canonicalMediaId,
                                                   QString *out)
{
    if (!m_store || !m_index)
        return error(out, QStringLiteral("Tracker mapping owner is unavailable."));
    for (const TrackerCanonicalTitleCandidate &candidate : m_index->userCandidates()) {
        if (candidate.canonicalMediaId == canonicalMediaId)
            return m_store->upsert(remote, candidate, TrackerMappingProvenance::UserConfirmed, out);
    }
    return error(out, QStringLiteral("The chosen canonical title is not an available native candidate."));
}
