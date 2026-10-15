#include "ActivitySyncAdapter.h"

#include "ActivityProjector.h"
#include "ActivityStore.h"
#include "SyncAdapterValidation.h"

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
    connect(
        store,
        &ActivityStore::resetCommitted,
        this,
        [this](quint64, qint64) {
            if (m_applyingRemote)
                return;
            ++m_revision;
            emit localMutationAvailable(m_revision);
        });
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
    snapshot->tombstones.clear();
    snapshot->records.reserve(facts.size() + 1);

    const QVariantMap reset = m_store->portableSyncReset();
    if (!reset.isEmpty()) {
        snapshot->records.append(SyncAdapterRecord{
            QStringLiteral("activity/reset"),
            QJsonObject::fromVariantMap(reset),
            -1});
    }

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

bool ActivitySyncAdapter::validateRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    SyncAdapterValidationError *error) const {
    return SyncAdapterValidation::activity(
        recordKey,
        operation,
        payload,
        schemaVersionValue,
        error);
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

    if (recordKey == QLatin1String("activity/reset")) {
        if (operation != SyncWireOperation::Put || !payload.isObject())
            return fail(error, QStringLiteral("An Activity reset requires a PUT object payload."));
        const QJsonObject reset = payload.toObject();
        const qint64 generation = reset.value(QStringLiteral("resetGeneration")).toInteger();
        const qint64 resetAtMs = reset.value(QStringLiteral("resetAtMs")).toInteger();
        if (reset.size() != 2 || generation <= 0 || resetAtMs <= 0)
            return fail(error, QStringLiteral("The Activity reset payload is malformed."));
        QString ownerError;
        m_applyingRemote = true;
        const bool applied = m_store->applySyncedReset(
            static_cast<quint64>(generation), resetAtMs, &ownerError);
        m_applyingRemote = false;
        if (!applied)
            return fail(error, ownerError.isEmpty()
                ? QStringLiteral("The Activity owner rejected the reset barrier.")
                : ownerError);
        return true;
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
