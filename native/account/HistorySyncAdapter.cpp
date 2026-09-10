// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "HistorySyncAdapter.h"

#include "CoreStateSyncProjection.h"
#include "HistoryStore.h"
#include "SyncAdapterValidation.h"

#include <QJsonObject>
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

    const qint64 resetGeneration =
        m_store->syncResetGeneration();
    const qint64 resetBarrierAtMs =
        m_store->syncResetBarrierAtMs();
    if (resetGeneration > 0 && resetBarrierAtMs > 0) {
        snapshot->records.append(
            SyncAdapterRecord{
                QStringLiteral("history/reset"),
                QJsonObject{
                    {QStringLiteral("resetGeneration"), resetGeneration},
                    {QStringLiteral("resetAtMs"), resetBarrierAtMs}},
                resetBarrierAtMs});
    }

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

bool HistorySyncAdapter::validateRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    SyncAdapterValidationError *error) const {
    return SyncAdapterValidation::core(
        QStringLiteral("full_history"),
        recordKey,
        operation,
        payload,
        schemaVersionValue,
        error);
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

    if (recordKey == QLatin1String("history/reset")) {
        if (operation != SyncWireOperation::Put || !payload.isObject())
            return fail(
                error,
                QStringLiteral(
                    "A History reset requires a PUT object payload."));
        const QJsonObject reset = payload.toObject();
        qint64 generation = 0;
        qint64 resetAtMs = 0;
        SyncAdapterValidationError validation;
        if (!SyncAdapterValidation::integer(
                reset,
                QStringLiteral("resetGeneration"),
                &generation,
                true,
                &validation)
            || !SyncAdapterValidation::integer(
                   reset,
                   QStringLiteral("resetAtMs"),
                   &resetAtMs,
                   true,
                   &validation)
            || reset.size() != 2) {
            return fail(
                error,
                QStringLiteral(
                    "The History reset payload is invalid."));
        }
        return m_store->applySyncedReset(generation, resetAtMs)
            ? true
            : fail(
                  error,
                  QStringLiteral(
                      "The History owner rejected the remote reset."));
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
