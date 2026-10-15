#include "DownloadIntentStore.h"

#include "SyncOwnershipInventory.h"
#include "SyncPayloadFirewall.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDateTime>

namespace {
const QStringList &portableFields() {
    static const QStringList fields = {
        QStringLiteral("id"),
        QStringLiteral("world"),
        QStringLiteral("kind"),
        QStringLiteral("title"),
        QStringLiteral("subtitle"),
        QStringLiteral("seriesTitle"),
        QStringLiteral("season"),
        QStringLiteral("episode"),
        QStringLiteral("seriesId"),
        QStringLiteral("label"),
        QStringLiteral("author")
    };
    return fields;
}
}

DownloadIntentStore::DownloadIntentStore(QObject *parent)
    : QObject(parent) {
    setObjectName(QStringLiteral("downloadIntentStore"));
}

bool DownloadIntentStore::activate(
    const ProfilePaths &profile,
    QString *error) {
    if (profile.kind() != ProfilePaths::Kind::Account) {
        return setError(
            error,
            QStringLiteral(
                "Download intents require an active account profile."));
    }

    m_profile = profile;
    m_path = QDir(profile.profileRoot()).filePath(
        QStringLiteral("download-intents.json"));
    m_records.clear();
    m_tombstones.clear();
    m_tombstoneAbsentObserved.clear();
    m_active = true;
    if (!load(error)) {
        m_active = false;
        return false;
    }
    return refreshFromLocal(error);
}

void DownloadIntentStore::deactivate() {
    if (!m_active && m_path.isEmpty())
        return;
    m_profile = ProfilePaths::sealed();
    m_path.clear();
    m_records.clear();
    m_tombstones.clear();
    m_tombstoneAbsentObserved.clear();
    m_active = false;
    ++m_revision;
    emit changed();
}

void DownloadIntentStore::setLocalRecordProvider(
    std::function<QVariantList()> provider) {
    m_localRecordProvider = std::move(provider);
}

bool DownloadIntentStore::refreshFromLocal(QString *error) {
    if (!m_active)
        return setError(error, QStringLiteral("Download intents are not active."));

    const QHash<QString, QVariantMap> previousRecords = m_records;
    const QHash<QString, qint64> previousTombstones = m_tombstones;
    const QSet<QString> previousAbsent = m_tombstoneAbsentObserved;
    bool durableModified = false;
    if (m_localRecordProvider) {
        const QVariantList local = m_localRecordProvider();
        QSet<QString> localKeys;
        for (const QVariant &value : local) {
            const QVariantMap record = portableRecord(value.toMap());
            if (!validPortableRecord(record, error)) {
                if (record.isEmpty())
                    continue;
                m_records = previousRecords;
                m_tombstones = previousTombstones;
                m_tombstoneAbsentObserved = previousAbsent;
                return false;
            }
            const QString key = recordKey(record);
            localKeys.insert(key);
            if (m_tombstones.contains(key)
                && !m_tombstoneAbsentObserved.contains(key))
                continue;
            if (m_tombstones.remove(key)) {
                m_tombstoneAbsentObserved.remove(key);
                durableModified = true;
            }
            if (m_records.value(key) == record)
                continue;
            m_records.insert(key, record);
            durableModified = true;
        }

        const QStringList tombstoneKeys = m_tombstones.keys();
        for (const QString &key : tombstoneKeys) {
            if (localKeys.contains(key))
                continue;
            if (!m_tombstoneAbsentObserved.contains(key)) {
                m_tombstoneAbsentObserved.insert(key);
            }
        }
    }

    if (!durableModified)
        return true;
    if (!save(error)) {
        m_records = previousRecords;
        m_tombstones = previousTombstones;
        m_tombstoneAbsentObserved = previousAbsent;
        return false;
    }
    ++m_revision;
    emit changed();
    return true;
}

bool DownloadIntentStore::remember(
    const QVariantMap &input,
    QString *error) {
    if (!m_active)
        return setError(error, QStringLiteral("Download intents are not active."));
    const QVariantMap record = portableRecord(input);
    if (!validPortableRecord(record, error))
        return false;
    const QString key = recordKey(record);
    const auto old = m_records.constFind(key);
    const QVariantMap previous = old == m_records.constEnd() ? QVariantMap() : old.value();
    const auto oldTombstone = m_tombstones.constFind(key);
    const bool hadTombstone = oldTombstone != m_tombstones.constEnd();
    const qint64 previousTombstone = hadTombstone ? oldTombstone.value() : 0;
    const bool hadAbsentObservation = m_tombstoneAbsentObserved.contains(key);
    const bool modified = old == m_records.constEnd() || previous != record || hadTombstone;
    if (!modified)
        return true;
    m_records.insert(key, record);
    m_tombstones.remove(key);
    m_tombstoneAbsentObserved.remove(key);
    if (!save(error)) {
        if (previous.isEmpty())
            m_records.remove(key);
        else
            m_records.insert(key, previous);
        if (!hadTombstone)
            m_tombstones.remove(key);
        else
            m_tombstones.insert(key, previousTombstone);
        if (hadAbsentObservation)
            m_tombstoneAbsentObserved.insert(key);
        else
            m_tombstoneAbsentObserved.remove(key);
        return false;
    }
    ++m_revision;
    emit changed();
    return true;
}

bool DownloadIntentStore::cancel(
    const QString &inputKey,
    QString *error) {
    if (!m_active)
        return setError(error, QStringLiteral("Download intents are not active."));
    const QString key = inputKey.trimmed();
    if (!validRecordKey(key, error))
        return false;
    const auto oldRecord = m_records.constFind(key);
    const QVariantMap previous = oldRecord == m_records.constEnd() ? QVariantMap() : oldRecord.value();
    const auto oldTombstone = m_tombstones.constFind(key);
    const bool hadRecord = oldRecord != m_records.constEnd();
    const bool hadTombstone = oldTombstone != m_tombstones.constEnd();
    const qint64 previousTombstone = hadTombstone ? oldTombstone.value() : 0;
    const bool hadAbsentObservation = m_tombstoneAbsentObserved.contains(key);
    // A repeated cancellation after the provider was observed absent is a
    // newer durable delete. Disarm that observation so a stale provider row
    // cannot rearm the intent after this cancellation.
    if (!hadRecord && hadTombstone && !hadAbsentObservation)
        return true;
    const qint64 cancelledAt = QDateTime::currentMSecsSinceEpoch();
    m_records.remove(key);
    m_tombstones.insert(key, qMax(cancelledAt, previousTombstone));
    m_tombstoneAbsentObserved.remove(key);
    if (!save(error)) {
        if (!previous.isEmpty())
            m_records.insert(key, previous);
        if (!hadTombstone)
            m_tombstones.remove(key);
        else
            m_tombstones.insert(key, previousTombstone);
        if (hadAbsentObservation)
            m_tombstoneAbsentObserved.insert(key);
        else
            m_tombstoneAbsentObserved.remove(key);
        return false;
    }
    ++m_revision;
    emit changed();
    return true;
}

QVariantList DownloadIntentStore::records() const {
    QVariantList result;
    QStringList keys = m_records.keys();
    keys.sort();
    for (const QString &key : keys)
        result.append(m_records.value(key));
    return result;
}

bool DownloadIntentStore::exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot)
        return setError(error, QStringLiteral("Download intent export needs an output object."));
    if (!m_active)
        return setError(error, QStringLiteral("Download intents are not active."));

    snapshot->revision = m_revision;
    snapshot->records.clear();
    snapshot->tombstones = m_tombstones.keys();
    snapshot->tombstones.sort();
    const QVariantList current = records();
    for (const QVariant &value : current) {
        const QVariantMap record = value.toMap();
        if (!validPortableRecord(record, error))
            return false;
        snapshot->records.append(SyncAdapterRecord{
            recordKey(record),
            QJsonObject::fromVariantMap(record),
            -1});
    }
    return true;
}

bool DownloadIntentStore::applyRemote(
    const QString &key,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    QString *error) {
    if (!m_active)
        return setError(error, QStringLiteral("Download intents are not active."));
    if (schemaVersion != 1)
        return setError(error, QStringLiteral("The download intent sync schema is unsupported."));

    if (operation == SyncWireOperation::Delete) {
        if (!validRecordKey(key, error))
            return false;
        const bool hadTombstone = m_tombstones.contains(key);
        const bool hadRecord = m_records.contains(key);
        const bool hadAbsentObservation = m_tombstoneAbsentObserved.contains(key);
        // A repeated remote DELETE supersedes a prior absence observation.
        // Keep the durable tombstone and disarm the local reappearance gate.
        if (!hadRecord && hadTombstone && !hadAbsentObservation)
            return true;
        const QVariantMap previous = m_records.value(key);
        const qint64 cancelledAt = QDateTime::currentMSecsSinceEpoch();
        const qint64 oldTombstone = m_tombstones.value(key, 0);
        m_tombstones.insert(key, qMax(cancelledAt, oldTombstone));
        m_records.remove(key);
        m_tombstoneAbsentObserved.remove(key);
        if (!save(error)) {
            if (hadRecord) {
                m_records.insert(key, previous);
            }
            if (hadTombstone)
                m_tombstones.insert(key, oldTombstone);
            else
                m_tombstones.remove(key);
            if (hadAbsentObservation)
                m_tombstoneAbsentObserved.insert(key);
            else
                m_tombstoneAbsentObserved.remove(key);
            return setError(error, QStringLiteral("The download intent cancellation could not be committed."));
        }
        ++m_revision;
        emit changed();
        return true;
    }

    if (operation != SyncWireOperation::Put || !payload.isObject())
        return setError(error, QStringLiteral("A download intent requires an object payload."));

    const QJsonObject object = payload.toObject();
    const SyncPayloadValidation validation =
        SyncPayloadFirewall::validate(
            QStringLiteral("desired_download_intent"),
            object);
    if (!validation.allowed)
        return setError(error, validation.detail);

    const QVariantMap record = portableRecord(object, error);
    if (!validPortableRecord(record, error))
        return false;
    if (recordKey(record) != key)
        return setError(error, QStringLiteral("The download intent identity does not match its record key."));
    if (m_records.value(key) == record && !m_tombstones.contains(key))
        return true;

    const auto previous = m_records.constFind(key);
    const QVariantMap old = previous == m_records.constEnd()
        ? QVariantMap() : previous.value();
    const auto oldTombstone = m_tombstones.constFind(key);
    const bool hadTombstone = oldTombstone != m_tombstones.constEnd();
    const qint64 previousTombstone = hadTombstone ? oldTombstone.value() : 0;
    const bool hadAbsentObservation = m_tombstoneAbsentObserved.contains(key);
    m_records.insert(key, record);
    m_tombstones.remove(key);
    m_tombstoneAbsentObserved.remove(key);
    if (!save(error)) {
        if (old.isEmpty())
            m_records.remove(key);
        else
            m_records.insert(key, old);
        if (hadTombstone)
            m_tombstones.insert(key, previousTombstone);
        if (hadAbsentObservation)
            m_tombstoneAbsentObserved.insert(key);
        else
            m_tombstoneAbsentObserved.remove(key);
        return false;
    }
    ++m_revision;
    emit changed();
    return true;
}

QString DownloadIntentStore::recordKey(const QVariantMap &record) {
    return record.value(QStringLiteral("world")).toString()
        + QLatin1Char('/')
        + record.value(QStringLiteral("id")).toString();
}

bool DownloadIntentStore::validRecordKey(
    const QString &inputKey,
    QString *error) {
    const QString key = inputKey.trimmed();
    const QStringList parts = key.split(QLatin1Char('/'));
    if (parts.size() != 2 || parts.at(0).isEmpty() || parts.at(1).isEmpty()
        || parts.at(0).contains(QLatin1Char('\\'))
        || parts.at(1).contains(QLatin1Char('\\')))
        return setError(error, QStringLiteral("The download intent key is invalid."));
    return true;
}

QVariantMap DownloadIntentStore::portableRecord(const QVariantMap &record) {
    QVariantMap portable;
    for (const QString &field : portableFields()) {
        if (record.contains(field))
            portable.insert(field, record.value(field));
    }
    return portable;
}

bool DownloadIntentStore::validPortableRecord(
    const QVariantMap &record,
    QString *error) {
    const QString id = record.value(QStringLiteral("id")).toString().trimmed();
    const QString world = record.value(QStringLiteral("world")).toString().trimmed();
    const QString kind = record.value(QStringLiteral("kind")).toString().trimmed();
    if (id.isEmpty() || world.isEmpty() || kind.isEmpty()) {
        return setError(error, QStringLiteral("A download intent needs id, world, and kind."));
    }
    if (id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\'))
        || world.contains(QLatin1Char('/')) || world.contains(QLatin1Char('\\'))
        || kind.contains(QLatin1Char('/')) || kind.contains(QLatin1Char('\\'))) {
        return setError(error, QStringLiteral("A download intent contains an invalid identity."));
    }
    const SyncPayloadValidation validation =
        SyncPayloadFirewall::validate(
            QStringLiteral("desired_download_intent"),
            QJsonObject::fromVariantMap(record));
    if (!validation.allowed)
        return setError(error, validation.detail);
    return true;
}

QVariantMap DownloadIntentStore::portableRecord(
    const QJsonObject &object,
    QString *error) {
    QVariantMap record;
    for (const QString &field : portableFields()) {
        if (object.contains(field))
            record.insert(field, object.value(field).toVariant());
    }
    if (record.isEmpty())
        setError(error, QStringLiteral("The download intent payload is empty."));
    return record;
}

bool DownloadIntentStore::load(QString *error) {
    m_tombstoneAbsentObserved.clear();
    if (!QFileInfo::exists(m_path))
        return true;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return setError(error, QStringLiteral("The download intent store could not be opened."));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return setError(error, QStringLiteral("The download intent store is malformed."));
    const QJsonArray array = document.object().value(QStringLiteral("records")).toArray();
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            return setError(error, QStringLiteral("The download intent store contains an invalid record."));
        const QVariantMap record = portableRecord(value.toObject(), error);
        if (!validPortableRecord(record, error))
            return false;
        m_records.insert(recordKey(record), record);
    }
    const QJsonValue tombstoneValue = document.object().value(QStringLiteral("tombstones"));
    if (!tombstoneValue.isUndefined()) {
        if (!tombstoneValue.isArray())
            return setError(error, QStringLiteral("The download intent tombstones are malformed."));
        for (const QJsonValue &value : tombstoneValue.toArray()) {
            if (!value.isObject())
                return setError(error, QStringLiteral("A download intent tombstone is malformed."));
            const QJsonObject object = value.toObject();
            const QString key = object.value(QStringLiteral("record_key")).toString();
            bool ok = false;
            const qint64 at = object.value(QStringLiteral("cancelled_at_ms")).toString().toLongLong(&ok);
            if (!validRecordKey(key, error) || !ok || at <= 0 || m_tombstones.contains(key))
                return setError(error, QStringLiteral("A download intent tombstone is invalid or duplicated."));
            m_tombstones.insert(key, at);
            m_records.remove(key);
        }
    }
    return true;
}

bool DownloadIntentStore::save(QString *error) const {
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return setError(error, QStringLiteral("The download intent store directory could not be created."));
    QJsonArray array;
    const QVariantList current = records();
    for (const QVariant &value : current)
        array.append(QJsonObject::fromVariantMap(value.toMap()));
    QJsonArray tombstones;
    QStringList tombstoneKeys = m_tombstones.keys();
    tombstoneKeys.sort();
    for (const QString &key : tombstoneKeys)
        tombstones.append(QJsonObject{
            {QStringLiteral("record_key"), key},
            {QStringLiteral("cancelled_at_ms"), QString::number(m_tombstones.value(key))}});
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("The download intent store could not be written."));
    file.write(QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("records"), array},
        {QStringLiteral("tombstones"), tombstones}}).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return setError(error, QStringLiteral("The download intent store could not be committed."));
    return true;
}

bool DownloadIntentStore::setError(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
