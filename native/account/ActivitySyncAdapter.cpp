#include "ActivitySyncAdapter.h"

#include "ActivityStore.h"

#include <QJsonObject>
#include <QUuid>
#include <QtGlobal>

ActivitySyncAdapter::ActivitySyncAdapter(
    ActivityStore *store,
    QObject *parent)
    : SyncAdapter(parent),
      m_store(store) {
    Q_ASSERT(store);
    setObjectName(QStringLiteral("activitySyncAdapter"));

    if (!store)
        return;

    connect(
        store,
        &ActivityStore::factCommitted,
        this,
        &ActivitySyncAdapter::handleFactCommitted);
}

QString ActivitySyncAdapter::categoryId() const {
    return QStringLiteral("activity_fact");
}

int ActivitySyncAdapter::schemaVersion() const {
    return 1;
}

quint64 ActivitySyncAdapter::revision() const {
    return m_revision;
}

bool ActivitySyncAdapter::missingRecordsAreDeletes() const {
    return false;
}

bool ActivitySyncAdapter::exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot) {
        return fail(
            error,
            QStringLiteral("Activity sync requires an export output object."));
    }
    if (!m_store) {
        return fail(
            error,
            QStringLiteral("The Activity owner is no longer available."));
    }

    QString ownerError;
    const QList<QVariantMap> facts =
        m_store->portableSyncFacts(&ownerError);
    if (!ownerError.isEmpty())
        return fail(error, ownerError);

    snapshot->revision = revision();
    snapshot->records.clear();
    snapshot->records.reserve(facts.size());

    for (const QVariantMap &fact : facts) {
        const QString eventId =
            fact.value(QStringLiteral("eventId")).toString();
        const QString lowerEventId = normalizedUuid(eventId);
        if (lowerEventId.isEmpty()) {
            return fail(
                error,
                QStringLiteral("The Activity owner returned an invalid event identity."));
        }

        SyncAdapterRecord record;
        record.recordKey =
            QStringLiteral("activity/") + lowerEventId;
        record.payload =
            QJsonObject::fromVariantMap(fact);
        snapshot->records.append(record);
    }

    return true;
}

bool ActivitySyncAdapter::applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    QString *error) {
    if (!m_store) {
        return fail(
            error,
            QStringLiteral("The Activity owner is no longer available."));
    }
    if (schemaVersionValue != schemaVersion()) {
        return fail(
            error,
            QStringLiteral("The Activity sync schema is unsupported."));
    }
    if (operation != SyncWireOperation::Put) {
        return fail(
            error,
            QStringLiteral("Activity facts are immutable and accept PUT only."));
    }

    QString lowerEventId;
    if (!decodeRecordKey(recordKey, &lowerEventId)) {
        return fail(
            error,
            QStringLiteral("The Activity sync record key is invalid."));
    }
    if (!payload.isObject()) {
        return fail(
            error,
            QStringLiteral("An Activity PUT requires an object payload."));
    }

    const QJsonObject object = payload.toObject();
    const QJsonValue eventIdValue =
        object.value(QStringLiteral("eventId"));
    if (!eventIdValue.isString()
        || normalizedUuid(eventIdValue.toString()) != lowerEventId) {
        return fail(
            error,
            QStringLiteral("The Activity payload identity does not match its record key."));
    }

    QString ownerError;
    m_applyingRemote = true;
    const bool applied =
        m_store->applySyncedPortableFact(
            object.toVariantMap(),
            &ownerError);
    m_applyingRemote = false;

    if (!applied) {
        return fail(
            error,
            ownerError.isEmpty()
                ? QStringLiteral("The Activity owner rejected the remote fact.")
                : ownerError);
    }
    return true;
}

void ActivitySyncAdapter::handleFactCommitted(
    const QVariantMap &event) {
    if (m_applyingRemote)
        return;
    if (!event.value(QStringLiteral("syncable")).toBool())
        return;

    ++m_revision;
    emit localMutationAvailable(m_revision);
}

QString ActivitySyncAdapter::normalizedUuid(const QString &value) {
    const QUuid parsed(value);
    if (parsed.isNull())
        return QString();
    return parsed.toString(QUuid::WithoutBraces).toLower();
}

bool ActivitySyncAdapter::decodeRecordKey(
    const QString &recordKey,
    QString *lowerEventId) {
    static const QString prefix = QStringLiteral("activity/");
    if (!isValidSyncWireRecordKey(recordKey)
        || !recordKey.startsWith(prefix)) {
        return false;
    }

    const QString suffix = recordKey.mid(prefix.size());
    const QString normalized = normalizedUuid(suffix);
    if (normalized.isEmpty() || suffix != normalized) {
        return false;
    }

    if (lowerEventId)
        *lowerEventId = suffix;
    return true;
}

bool ActivitySyncAdapter::fail(
    QString *error,
    const QString &detail) {
    if (error)
        *error = detail;
    return false;
}
