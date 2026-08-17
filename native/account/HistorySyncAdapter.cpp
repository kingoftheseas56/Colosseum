// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "HistorySyncAdapter.h"

#include "CoreStateSyncProjection.h"
#include "HistoryStore.h"

#include <QtGlobal>

HistorySyncAdapter::HistorySyncAdapter(
    HistoryStore *store,
    QObject *parent)
    : SyncAdapter(parent),
      m_store(store) {
    Q_ASSERT(m_store);

    setObjectName(
        QStringLiteral(
            "historySyncAdapter"));

    connect(
        m_store,
        &HistoryStore::syncDirty,
        this,
        [this]() {
            emit localMutationAvailable(
                revision());
        });
}

QString HistorySyncAdapter::categoryId() const {
    return QStringLiteral("full_history");
}

int HistorySyncAdapter::schemaVersion() const {
    return 1;
}

quint64 HistorySyncAdapter::revision() const {
    return m_store
        ? static_cast<quint64>(
              qMax(
                  0,
                  m_store->revision()))
        : 0;
}

bool HistorySyncAdapter::exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot)
        return fail(
            error,
            QStringLiteral(
                "A history sync snapshot output is required."));

    if (!m_store)
        return fail(
            error,
            QStringLiteral(
                "The History owner is unavailable."));

    QString ownerError;
    if (!m_store->healthy(
            &ownerError)) {
        return fail(
            error,
            ownerError.isEmpty()
                ? QStringLiteral(
                      "The History owner persistence is unhealthy.")
                : ownerError);
    }

    snapshot->revision =
        revision();
    snapshot->records.clear();

    const QVariantList entries =
        m_store->syncEntries();

    for (const QVariant &value :
         entries) {
        const QVariantMap entry =
            value.toMap();

        const CoreStateSyncProjection projected =
            CoreStateSyncProjection::
                history(entry);

        if (projected.disposition
            == CoreStateSyncProjection::
                Disposition::LocalOnly) {
            continue;
        }

        if (projected.disposition
            != CoreStateSyncProjection::
                Disposition::Portable) {
            return fail(
                error,
                projected.error.isEmpty()
                    ? QStringLiteral(
                          "A History record cannot be exported safely.")
                    : projected.error);
        }

        SyncAdapterRecord record;
        record.recordKey =
            projected.recordKey;
        record.payload =
            projected.payload;
        record.localOrderMs =
            projected.localOrderMs;

        snapshot->records.append(
            record);
    }

    return true;
}

bool HistorySyncAdapter::applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    QString *error) {
    if (!m_store)
        return fail(
            error,
            QStringLiteral(
                "The History owner is unavailable."));

    QString ownerError;
    if (!m_store->healthy(
            &ownerError)) {
        return fail(
            error,
            ownerError.isEmpty()
                ? QStringLiteral(
                      "The History owner persistence is unhealthy.")
                : ownerError);
    }

    if (schemaVersionValue
        != schemaVersion()) {
        return fail(
            error,
            QStringLiteral(
                "The History sync schema is unsupported."));
    }

    QString kind;
    QString id;
    if (!CoreStateSyncProjection::
            decodeHistoryKey(
                recordKey,
                &kind,
                &id)) {
        return fail(
            error,
            QStringLiteral(
                "The History record key is invalid."));
    }

    if (operation
        == SyncWireOperation::Delete) {
        return m_store
            ->removeSyncedRecord(
                kind,
                id);
    }

    if (!payload.isObject()) {
        return fail(
            error,
            QStringLiteral(
                "A History PUT requires an object payload."));
    }

    const QJsonObject object =
        payload.toObject();

    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::
            history(
                object.toVariantMap());

    if (projected.disposition
            != CoreStateSyncProjection::
                Disposition::Portable
        || projected.recordKey
            != recordKey) {
        return fail(
            error,
            QStringLiteral(
                "The History payload identity does not match its record key."));
    }

    if (!m_store->applySyncedRecord(
            object.toVariantMap())) {
        return fail(
            error,
            QStringLiteral(
                "The History owner rejected the remote record."));
    }

    return true;
}

bool HistorySyncAdapter::fail(
    QString *error,
    const QString &message) {
    if (error)
        *error = message;
    return false;
}
