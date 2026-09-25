#include "TrackerHistoryEvidenceStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {

constexpr int kSchemaVersion = 3;
constexpr auto kFileName = "tracker-imported-history.json";

bool error(QString *out, const QString &message)
{
    if (out)
        *out = message;
    return false;
}

bool safeText(const QString &value, int maximum = 512)
{
    return !value.isEmpty() && value == value.trimmed() && value.size() <= maximum
        && !QDir::isAbsolutePath(value) && !value.contains(QLatin1String(".."));
}

bool validMapping(const TrackerTitleMapping &mapping)
{
    return !trackerProviderKey(mapping.remote.providerId).isEmpty()
        && safeText(mapping.remote.remoteAccountId, 128) && safeText(mapping.remote.remoteMediaId)
        && safeText(mapping.canonical.canonicalMediaId) && safeText(mapping.canonical.historyKind, 64)
        && safeText(mapping.canonical.historyId) && mapping.revision > 0;
}

QString eventKindKey(TrackerImportedEventKind kind)
{
    return kind == TrackerImportedEventKind::Activity ? QStringLiteral("activity")
                                                     : QStringLiteral("completion");
}

QString timestampSourceKey(TrackerEvidenceTimestampSource source)
{
    return source == TrackerEvidenceTimestampSource::ProviderEvent
        ? QStringLiteral("provider_event") : QStringLiteral("list_updated_at");
}

std::optional<TrackerEvidenceTimestampSource> timestampSourceFromKey(const QString &value)
{
    if (value == QLatin1String("provider_event"))
        return TrackerEvidenceTimestampSource::ProviderEvent;
    if (value == QLatin1String("list_updated_at"))
        return TrackerEvidenceTimestampSource::ListUpdatedAt;
    return std::nullopt;
}

QString timestampPrecisionKey(TrackerEvidenceTimestampPrecision precision)
{
    return precision == TrackerEvidenceTimestampPrecision::ExactMillisecond
        ? QStringLiteral("exact_millisecond") : QStringLiteral("date_only");
}

std::optional<TrackerEvidenceTimestampPrecision> timestampPrecisionFromKey(const QString &value)
{
    if (value == QLatin1String("exact_millisecond"))
        return TrackerEvidenceTimestampPrecision::ExactMillisecond;
    if (value == QLatin1String("date_only"))
        return TrackerEvidenceTimestampPrecision::DateOnly;
    return std::nullopt;
}

std::optional<TrackerImportedEventKind> eventKindFromKey(const QString &value)
{
    if (value == QLatin1String("activity"))
        return TrackerImportedEventKind::Activity;
    if (value == QLatin1String("completion"))
        return TrackerImportedEventKind::Completion;
    return std::nullopt;
}

QString provenanceKey(TrackerMappingProvenance value)
{
    return value == TrackerMappingProvenance::ExactProviderIdentity
        ? QStringLiteral("exact_provider_identity") : QStringLiteral("user_confirmed");
}

std::optional<TrackerMappingProvenance> provenanceFromKey(const QString &value)
{
    if (value == QLatin1String("exact_provider_identity"))
        return TrackerMappingProvenance::ExactProviderIdentity;
    if (value == QLatin1String("user_confirmed"))
        return TrackerMappingProvenance::UserConfirmed;
    return std::nullopt;
}

QString contributionId(const TrackerImportedHistoryEvidence &evidence)
{
    const QString supplied = evidence.providerEventId.trimmed();
    const QString eventKey = !supplied.isEmpty()
        ? supplied
        : QString::fromLatin1(QCryptographicHash::hash(
              (evidence.importSnapshotId + QChar(0x1f) + eventKindKey(evidence.eventKind)
               + QChar(0x1f) + QString::number(evidence.occurredAtMs)
               + QChar(0x1f) + timestampSourceKey(evidence.timestampSource)
               + QChar(0x1f) + timestampPrecisionKey(evidence.timestampPrecision)
               + QChar(0x1f) + evidence.eventPayloadFingerprint).toUtf8(),
              QCryptographicHash::Sha256).toHex());
    return trackerProviderKey(evidence.mapping.remote.providerId) + QChar(0x1f)
        + evidence.mapping.remote.remoteAccountId + QChar(0x1f)
        + evidence.mapping.remote.remoteMediaId + QChar(0x1f) + eventKey;
}

bool valid(const TrackerImportedHistoryEvidence &evidence)
{
    // An event ID is ideal. The conservative snapshot fingerprint fallback is
    // legal only when an exact mapped item, precise event time, and import
    // snapshot are all present; an updated list timestamp cannot enter here.
    return validMapping(evidence.mapping) && evidence.occurredAtMs > 0
        && evidence.timestampSource == TrackerEvidenceTimestampSource::ProviderEvent
        && evidence.timestampPrecision == TrackerEvidenceTimestampPrecision::ExactMillisecond
        && safeText(evidence.eventPayloadFingerprint)
        && (!evidence.providerEventId.isEmpty() ? safeText(evidence.providerEventId)
                                                 : safeText(evidence.importSnapshotId));
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
        && left.provenance == right.provenance && left.revision == right.revision;
}

bool sameEvent(const TrackerImportedHistoryEvidence &left,
               const TrackerImportedHistoryEvidence &right)
{
    return sameMapping(left.mapping, right.mapping) && left.eventKind == right.eventKind
        && left.providerEventId == right.providerEventId
        && left.importSnapshotId == right.importSnapshotId
        && left.occurredAtMs == right.occurredAtMs
        && left.timestampSource == right.timestampSource
        && left.timestampPrecision == right.timestampPrecision
        && left.eventPayloadFingerprint == right.eventPayloadFingerprint;
}

bool sameCanonicalTarget(const TrackerTitleMapping &left, const TrackerTitleMapping &right)
{
    return left.canonical.canonicalMediaId == right.canonical.canonicalMediaId
        && left.canonical.historyKind == right.canonical.historyKind
        && left.canonical.historyId == right.canonical.historyId;
}

bool sameProviderEvent(const TrackerImportedHistoryEvidence &left,
                       const TrackerImportedHistoryEvidence &right)
{
    // A stable provider event ID, not the import snapshot, is the idempotency
    // authority. Mapping provenance/revision and display labels may refresh;
    // its canonical target and event meaning may not.
    return sameCanonicalTarget(left.mapping, right.mapping)
        && left.eventKind == right.eventKind && left.providerEventId == right.providerEventId
        && left.occurredAtMs == right.occurredAtMs
        && left.timestampSource == right.timestampSource
        && left.timestampPrecision == right.timestampPrecision
        && left.eventPayloadFingerprint == right.eventPayloadFingerprint;
}

QJsonObject toJson(const TrackerImportedHistoryEvidence &evidence)
{
    const TrackerTitleMapping &mapping = evidence.mapping;
    return {{QStringLiteral("providerId"), trackerProviderKey(mapping.remote.providerId)},
            {QStringLiteral("remoteAccountId"), mapping.remote.remoteAccountId},
            {QStringLiteral("remoteMediaId"), mapping.remote.remoteMediaId},
            {QStringLiteral("canonicalMediaId"), mapping.canonical.canonicalMediaId},
            {QStringLiteral("historyKind"), mapping.canonical.historyKind},
            {QStringLiteral("historyId"), mapping.canonical.historyId},
            {QStringLiteral("displayName"), mapping.canonical.displayName},
            {QStringLiteral("mappingProvenance"), provenanceKey(mapping.provenance)},
            {QStringLiteral("mappingRevision"), QString::number(mapping.revision)},
            {QStringLiteral("eventKind"), eventKindKey(evidence.eventKind)},
            {QStringLiteral("providerEventId"), evidence.providerEventId},
            {QStringLiteral("importSnapshotId"), evidence.importSnapshotId},
            {QStringLiteral("occurredAtMs"), QString::number(evidence.occurredAtMs)},
            {QStringLiteral("timestampSource"), timestampSourceKey(evidence.timestampSource)},
            {QStringLiteral("timestampPrecision"), timestampPrecisionKey(evidence.timestampPrecision)},
            {QStringLiteral("eventPayloadFingerprint"), evidence.eventPayloadFingerprint}};
}

std::optional<TrackerImportedHistoryEvidence> fromJson(const QJsonObject &object)
{
    const auto provider = trackerProviderIdFromKey(object.value(QStringLiteral("providerId")).toString());
    const auto provenance = provenanceFromKey(object.value(QStringLiteral("mappingProvenance")).toString());
    const auto kind = eventKindFromKey(object.value(QStringLiteral("eventKind")).toString());
    const auto timestampSource = timestampSourceFromKey(
        object.value(QStringLiteral("timestampSource")).toString());
    const auto timestampPrecision = timestampPrecisionFromKey(
        object.value(QStringLiteral("timestampPrecision")).toString());
    bool revisionOk = false;
    bool occurredOk = false;
    const quint64 revision = object.value(QStringLiteral("mappingRevision")).toString().toULongLong(&revisionOk);
    const qint64 occurredAtMs = object.value(QStringLiteral("occurredAtMs")).toString().toLongLong(&occurredOk);
    TrackerImportedHistoryEvidence evidence{{{provider.value_or(TrackerProviderId::Mal),
                                               object.value(QStringLiteral("remoteAccountId")).toString(),
                                               object.value(QStringLiteral("remoteMediaId")).toString()},
                                              {object.value(QStringLiteral("canonicalMediaId")).toString(),
                                               object.value(QStringLiteral("historyKind")).toString(),
                                               object.value(QStringLiteral("historyId")).toString(),
                                               object.value(QStringLiteral("displayName")).toString()},
                                              provenance.value_or(TrackerMappingProvenance::ExactProviderIdentity),
                                              revision},
                                             kind.value_or(TrackerImportedEventKind::Activity),
                                             object.value(QStringLiteral("providerEventId")).toString(),
                                             object.value(QStringLiteral("importSnapshotId")).toString(),
                                             occurredAtMs,
                                             timestampSource.value_or(TrackerEvidenceTimestampSource::ListUpdatedAt),
                                             timestampPrecision.value_or(TrackerEvidenceTimestampPrecision::DateOnly),
                                             object.value(QStringLiteral("eventPayloadFingerprint")).toString()};
    if (!provider || !provenance || !kind || !timestampSource || !timestampPrecision || !revisionOk
        || !occurredOk || !valid(evidence))
        return std::nullopt;
    return evidence;
}

QString rowKey(const TrackerImportedHistoryEvidence &evidence)
{
    return evidence.mapping.canonical.canonicalMediaId + QChar(0x1f)
        + evidence.mapping.canonical.historyKind + QChar(0x1f)
        + evidence.mapping.canonical.historyId + QChar(0x1f) + eventKindKey(evidence.eventKind)
        + QChar(0x1f) + QString::number(evidence.occurredAtMs) + QChar(0x1f)
        + evidence.eventPayloadFingerprint;
}

QString sourceRemovalKey(TrackerProviderId providerId, const QString &remoteAccountId)
{
    return trackerProviderKey(providerId) + QChar(0x1f) + remoteAccountId;
}

} // namespace

TrackerHistoryEvidenceStore::TrackerHistoryEvidenceStore(const ProfilePaths &profile,
                                                         const TrackerMappingStore *mappings)
    : m_profile(profile), m_mappings(mappings), m_path(storagePath(profile))
{
    if (!m_mappings || !m_mappings->healthy()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker imported History needs its profile mapping store.");
    } else if (m_path.isEmpty()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker imported History is unavailable for this profile.");
    } else {
        load();
    }
}

QString TrackerHistoryEvidenceStore::storagePath(const ProfilePaths &profile)
{
    if (profile.kind() == ProfilePaths::Kind::Sealed
        || profile.kind() == ProfilePaths::Kind::LegacyLocal || profile.profileRoot().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(profile.profileRoot() + QLatin1Char('/') + QLatin1String(kFileName));
}

bool TrackerHistoryEvidenceStore::healthy(QString *out) const
{
    if (!m_healthy && out)
        *out = m_error;
    return m_healthy;
}

QList<TrackerImportedHistoryEvidence> TrackerHistoryEvidenceStore::contributions() const
{
    return m_contributions;
}

QList<TrackerHistoryEvidenceRow> TrackerHistoryEvidenceStore::rows() const
{
    QHash<QString, TrackerHistoryEvidenceRow> grouped;
    for (const TrackerImportedHistoryEvidence &evidence : m_contributions) {
        const QString key = rowKey(evidence);
        TrackerHistoryEvidenceRow &row = grouped[key];
        if (row.canonicalMediaId.isEmpty()) {
            row.canonicalMediaId = evidence.mapping.canonical.canonicalMediaId;
            row.historyKind = evidence.mapping.canonical.historyKind;
            row.historyId = evidence.mapping.canonical.historyId;
            row.eventKind = evidence.eventKind;
            row.occurredAtMs = evidence.occurredAtMs;
        }
        if (!row.providers.contains(evidence.mapping.remote.providerId))
            row.providers.append(evidence.mapping.remote.providerId);
        row.contributionIds.append(contributionId(evidence));
    }
    QList<TrackerHistoryEvidenceRow> result = grouped.values();
    std::sort(result.begin(), result.end(), [](const TrackerHistoryEvidenceRow &left,
                                               const TrackerHistoryEvidenceRow &right) {
        if (left.occurredAtMs != right.occurredAtMs)
            return left.occurredAtMs > right.occurredAtMs;
        return left.canonicalMediaId < right.canonicalMediaId;
    });
    return result;
}

bool TrackerHistoryEvidenceStore::adoptPrivateStateFrom(
    const TrackerHistoryEvidenceStore &source,
    QString *out)
{
    if (!healthy(out) || !source.healthy(out) || this == &source
        || m_profile.profileId() == source.m_profile.profileId()
        || !m_mappings || !m_mappings->healthy(out)) {
        return error(out, QStringLiteral("Tracker imported History cannot be adopted between these profiles."));
    }

    QList<TrackerImportedHistoryEvidence> candidate = m_contributions;
    QSet<QString> suppressed = m_suppressedSources;
    bool changed = false;
    for (const TrackerImportedHistoryEvidence &evidence : source.m_contributions) {
        const auto currentMapping = m_mappings->mapping(evidence.mapping.remote);
        if (!currentMapping || !sameMapping(*currentMapping, evidence.mapping)) {
            return error(out, QStringLiteral(
                "Tracker imported History cannot be adopted until its confirmed mappings match."));
        }

        const QString id = contributionId(evidence);
        const auto duplicate = std::find_if(candidate.cbegin(), candidate.cend(),
            [&id](const TrackerImportedHistoryEvidence &entry) {
                return contributionId(entry) == id;
            });
        if (duplicate != candidate.cend()) {
            const bool stableProviderEvent = !duplicate->providerEventId.isEmpty()
                && !evidence.providerEventId.isEmpty();
            if (!(stableProviderEvent ? sameProviderEvent(*duplicate, evidence)
                                      : sameEvent(*duplicate, evidence))) {
                return error(out, QStringLiteral(
                    "Tracker imported History has a conflicting event identity."));
            }
            continue;
        }
        candidate.append(evidence);
        changed = true;
    }
    for (const QString &sourceKey : source.m_suppressedSources) {
        if (suppressed.contains(sourceKey))
            continue;
        suppressed.insert(sourceKey);
        changed = true;
    }
    if (!changed)
        return true;
    if (!persist(candidate, suppressed, out))
        return false;
    m_contributions = candidate;
    m_suppressedSources = suppressed;
    return true;
}

bool TrackerHistoryEvidenceStore::record(const TrackerImportedHistoryEvidence &evidence, QString *out)
{
    if (!healthy(out) || !valid(evidence))
        return error(out, QStringLiteral("Imported History evidence is incomplete or untrusted."));
    if (!m_mappings || !m_mappings->healthy(out))
        return error(out, QStringLiteral("Tracker imported History mapping store is unavailable."));
    if (m_suppressedSources.contains(sourceRemovalKey(
            evidence.mapping.remote.providerId, evidence.mapping.remote.remoteAccountId))) {
        return error(out, QStringLiteral(
            "This tracker source was removed; choose Import again before restoring its History."));
    }
    const auto current = m_mappings->mapping(evidence.mapping.remote);
    if (!current || !sameMapping(*current, evidence.mapping)) {
        return error(out, QStringLiteral("Imported History evidence does not match the current confirmed title mapping."));
    }
    const QString id = contributionId(evidence);
    QList<TrackerImportedHistoryEvidence> next = m_contributions;
    for (const TrackerImportedHistoryEvidence &entry : next) {
        if (contributionId(entry) != id)
            continue;
        const bool stableProviderEvent = !entry.providerEventId.isEmpty()
            && !evidence.providerEventId.isEmpty();
        return (stableProviderEvent ? sameProviderEvent(entry, evidence) : sameEvent(entry, evidence))
            ? true : error(out, QStringLiteral("Imported History event identity conflicts with existing evidence."));
    }
    next.append(evidence);
    if (!persist(next, m_suppressedSources, out))
        return false;
    m_contributions = next;
    return true;
}

int TrackerHistoryEvidenceStore::removeSource(TrackerProviderId providerId,
                                              const QString &remoteAccountId,
                                              QString *out)
{
    if (!healthy(out) || trackerProviderKey(providerId).isEmpty() || !safeText(remoteAccountId, 128)) {
        error(out, QStringLiteral("Tracker imported History source is invalid."));
        return -1;
    }
    QList<TrackerImportedHistoryEvidence> next;
    int removed = 0;
    for (const TrackerImportedHistoryEvidence &entry : m_contributions) {
        if (entry.mapping.remote.providerId == providerId
            && entry.mapping.remote.remoteAccountId == remoteAccountId) {
            ++removed;
        } else {
            next.append(entry);
        }
    }
    QSet<QString> suppressed = m_suppressedSources;
    suppressed.insert(sourceRemovalKey(providerId, remoteAccountId));
    if (removed == 0 && suppressed == m_suppressedSources)
        return 0;
    if (!persist(next, suppressed, out))
        return -1;
    m_contributions = next;
    m_suppressedSources = suppressed;
    return removed;
}

bool TrackerHistoryEvidenceStore::sourceRemovalSuppressed(
    TrackerProviderId providerId,
    const QString &remoteAccountId) const
{
    return m_suppressedSources.contains(sourceRemovalKey(providerId, remoteAccountId));
}

bool TrackerHistoryEvidenceStore::allowSourceAgain(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    QString *out)
{
    if (!healthy(out) || trackerProviderKey(providerId).isEmpty()
        || !safeText(remoteAccountId, 128)) {
        return error(out, QStringLiteral("Tracker History source selection is invalid."));
    }
    QSet<QString> suppressed = m_suppressedSources;
    suppressed.remove(sourceRemovalKey(providerId, remoteAccountId));
    if (suppressed == m_suppressedSources)
        return true;
    if (!persist(m_contributions, suppressed, out))
        return false;
    m_suppressedSources = suppressed;
    return true;
}

bool TrackerHistoryEvidenceStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker imported History could not be opened.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || (version != 2 && version != kSchemaVersion)
        || root.value(QStringLiteral("profileId")).toString() != m_profile.profileId()
        || !root.value(QStringLiteral("contributions")).isArray()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker imported History is malformed or foreign.");
        return false;
    }
    QSet<QString> loadedSuppressedSources;
    if (version >= 3) {
        const QJsonValue removedSources = root.value(QStringLiteral("removedSources"));
        if (!removedSources.isArray()) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker imported History removal records are malformed.");
            return false;
        }
        for (const QJsonValue &value : removedSources.toArray()) {
            if (!value.isObject()) {
                m_healthy = false;
                m_error = QStringLiteral("Tracker imported History removal record is malformed.");
                return false;
            }
            const QJsonObject object = value.toObject();
            const auto provider = trackerProviderIdFromKey(
                object.value(QStringLiteral("providerId")).toString());
            const QString account = object.value(QStringLiteral("remoteAccountId")).toString();
            if (!provider || !safeText(account, 128)
                || loadedSuppressedSources.contains(sourceRemovalKey(*provider, account))) {
                m_healthy = false;
                m_error = QStringLiteral("Tracker imported History removal record is invalid or duplicated.");
                return false;
            }
            loadedSuppressedSources.insert(sourceRemovalKey(*provider, account));
        }
    }
    QSet<QString> ids;
    QList<TrackerImportedHistoryEvidence> loaded;
    for (const QJsonValue &value : root.value(QStringLiteral("contributions")).toArray()) {
        const auto evidence = value.isObject() ? fromJson(value.toObject()) : std::nullopt;
        if (!evidence || ids.contains(contributionId(*evidence))) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker imported History contribution is invalid or duplicated.");
            return false;
        }
        ids.insert(contributionId(*evidence));
        loaded.append(*evidence);
    }
    m_contributions = loaded;
    m_suppressedSources = loadedSuppressedSources;
    return true;
}

bool TrackerHistoryEvidenceStore::persist(
    const QList<TrackerImportedHistoryEvidence> &candidate,
    const QSet<QString> &suppressedSources,
    QString *out) const
{
    QJsonArray entries;
    QSet<QString> ids;
    for (const TrackerImportedHistoryEvidence &evidence : candidate) {
        if (!valid(evidence) || ids.contains(contributionId(evidence)))
            return error(out, QStringLiteral("Tracker imported History persistence is invalid."));
        ids.insert(contributionId(evidence));
        entries.append(toJson(evidence));
    }
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.dir().absolutePath()))
        return error(out, QStringLiteral("Tracker imported History directory could not be created."));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return error(out, QStringLiteral("Tracker imported History could not be written."));
    QStringList sortedSources;
    for (const QString &key : suppressedSources)
        sortedSources.append(key);
    std::sort(sortedSources.begin(), sortedSources.end());
    QJsonArray removedSources;
    for (const QString &key : sortedSources) {
        const int separator = key.indexOf(QChar(0x1f));
        if (separator <= 0)
            return error(out, QStringLiteral("Tracker History removal state is invalid."));
        const auto provider = trackerProviderIdFromKey(key.left(separator));
        const QString account = key.mid(separator + 1);
        if (!provider || !safeText(account, 128))
            return error(out, QStringLiteral("Tracker History removal state is invalid."));
        removedSources.append(QJsonObject{
            {QStringLiteral("providerId"), trackerProviderKey(*provider)},
            {QStringLiteral("remoteAccountId"), account}});
    }
    file.write(QJsonDocument({{QStringLiteral("version"), kSchemaVersion},
                              {QStringLiteral("profileId"), m_profile.profileId()},
                              {QStringLiteral("contributions"), entries},
                              {QStringLiteral("removedSources"), removedSources}})
                  .toJson(QJsonDocument::Compact));
    if (!file.commit())
        return error(out, QStringLiteral("Tracker imported History could not be saved atomically."));
    return true;
}
