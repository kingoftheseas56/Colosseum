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
    m_active = true;
    if (!load(error)) {
        m_active = false;
        return false;
    }
    return refreshFromLocal(error);
}

void DownloadIntentStore::setLocalRecordProvider(
    std::function<QVariantList()> provider) {
    m_localRecordProvider = std::move(provider);
}

bool DownloadIntentStore::refreshFromLocal(QString *error) {
    if (!m_active)
        return setError(error, QStringLiteral("Download intents are not active."));

    bool modified = false;
    if (m_localRecordProvider) {
        const QVariantList local = m_localRecordProvider();
        for (const QVariant &value : local) {
            const QVariantMap record = portableRecord(value.toMap());
            if (!validPortableRecord(record, error)) {
                if (record.isEmpty())
                    continue;
                return false;
            }
            const QString key = recordKey(record);
            if (m_records.value(key) == record)
                continue;
            m_records.insert(key, record);
            modified = true;
        }
    }

    if (!modified)
        return true;
    if (!save(error))
        return false;
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
        if (!m_records.contains(key))
            return true;
        const QVariantMap previous = m_records.take(key);
        if (!save(error)) {
            m_records.insert(key, previous);
            return false;
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
    if (m_records.value(key) == record)
        return true;

    const auto previous = m_records.constFind(key);
    const QVariantMap old = previous == m_records.constEnd()
        ? QVariantMap() : previous.value();
    m_records.insert(key, record);
    if (!save(error)) {
        if (old.isEmpty())
            m_records.remove(key);
        else
            m_records.insert(key, old);
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
    return true;
}

bool DownloadIntentStore::save(QString *error) const {
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return setError(error, QStringLiteral("The download intent store directory could not be created."));
    QJsonArray array;
    const QVariantList current = records();
    for (const QVariant &value : current)
        array.append(QJsonObject::fromVariantMap(value.toMap()));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("The download intent store could not be written."));
    file.write(QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("records"), array}}).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return setError(error, QStringLiteral("The download intent store could not be committed."));
    return true;
}

bool DownloadIntentStore::setError(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
