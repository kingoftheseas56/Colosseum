// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProgressSyncAdapter.h"

#include "CoreStateSyncProjection.h"
#include "SyncAdapterValidation.h"

#include "ProgressStore.h"

#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

#include <utility>

ProgressSyncAdapter::
ProgressSyncAdapter(
    ProgressStore *store,
    QObject *parent,
    int coalesceMs)
    : SyncAdapter(parent),
      m_store(store) {
    Q_ASSERT(store);

    setObjectName(
        QStringLiteral(
            "progressSyncAdapter"));

    if (!store)
        return;

    m_syncRevision =
        static_cast<quint64>(
            qMax(
                0,
                store->revision()));

    m_coalesceTimer.setSingleShot(
        true);
    m_coalesceTimer.setInterval(
        qMax(
            1,
            coalesceMs));

    connect(
        &m_coalesceTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (!m_store)
                return;

            emit localMutationAvailable(
                revision());
        });

    connect(
        store,
        &ProgressStore::syncDirty,
        this,
        &ProgressSyncAdapter::
            noteSyncDirty);

    connect(
        store,
        &ProgressStore::localMutationChanged,
        this,
        &ProgressSyncAdapter::
            emitImmediateLocalMutation);
}

QString ProgressSyncAdapter::
categoryId() const {
    return QStringLiteral(
        "continue_progress");
}

int ProgressSyncAdapter::
schemaVersion() const {
    return 1;
}

quint64 ProgressSyncAdapter::
revision() const {
    if (!m_store)
        return m_syncRevision;

    return qMax(
        m_syncRevision,
        static_cast<quint64>(
            qMax(
                0,
                m_store->revision())));
}

bool ProgressSyncAdapter::
exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot) {
        if (error) {
            *error = QStringLiteral(
                "Continue/progress sync requires an export output object.");
        }
        return false;
    }

    if (!m_store) {
        if (error) {
            *error = QStringLiteral(
                "The Continue/progress owner is no longer available.");
        }
        return false;
    }

    if (!m_store->healthy(error))
        return false;

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
                progress(entry);

        if (projected.disposition
            == CoreStateSyncProjection::
                Disposition::LocalOnly) {
            continue;
        }

        if (projected.disposition
            != CoreStateSyncProjection::
                Disposition::Portable) {
            if (error) {
                *error =
                    projected.error.isEmpty()
                    ? QStringLiteral(
                          "Continue/progress contains a record that cannot be represented safely in ordinary sync.")
                    : projected.error;
            }
            return false;
        }

        snapshot->records.append(
            SyncAdapterRecord{
                projected.recordKey,
                projected.payload,
                projected.localOrderMs});
    }

    return true;
}

bool ProgressSyncAdapter::validateRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    SyncAdapterValidationError *error) const {
    return SyncAdapterValidation::core(
        QStringLiteral("continue_progress"),
        recordKey,
        operation,
        payload,
        schemaVersion,
        error);
}

bool ProgressSyncAdapter::
applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    QString *error) {
    if (!m_store) {
        if (error) {
            *error = QStringLiteral(
                "The Continue/progress owner is no longer available.");
        }
        return false;
    }

    if (schemaVersion != 1) {
        if (error) {
            *error = QStringLiteral(
                "The Continue/progress sync schema is unsupported.");
        }
        return false;
    }

    QString kind;
    QString id;
    if (!CoreStateSyncProjection::
             decodeProgressKey(
                 recordKey,
                 &kind,
                 &id)) {
        if (error) {
            *error = QStringLiteral(
                "The Continue/progress sync record key is invalid.");
        }
        return false;
    }

    if (operation
        == SyncWireOperation::Delete) {
        const bool removed = m_store->removeSyncedEntry(kind, id);
        if (!removed && error && error->isEmpty())
            *error = m_store->persistenceError();
        return removed;
    }

    if (!payload.isObject()) {
        if (error) {
            *error = QStringLiteral(
                "A Continue/progress PUT requires an object payload.");
        }
        return false;
    }

    const QJsonObject object =
        payload.toObject();
    if (object.value(
            QStringLiteral("kind"))
            .toString()
            != kind
        || object.value(
               QStringLiteral("id"))
               .toString()
               != id) {
        if (error) {
            *error = QStringLiteral(
                "The Continue/progress payload identity does not match its record key.");
        }
        return false;
    }

    const QVariantMap portableEntry =
        object.toVariantMap();

    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::
            progress(portableEntry);
    if (projected.disposition
            != CoreStateSyncProjection::
                Disposition::Portable
        || projected.recordKey
            != recordKey
        || projected.payload
            != object) {
        if (error) {
            *error =
                projected.error.isEmpty()
                ? QStringLiteral(
                      "The Continue/progress payload is not a canonical portable record.")
                : projected.error;
        }
        return false;
    }

    const QVariantMap existing =
        m_store->get(
            kind,
            id);
    const QVariantMap merged =
        CoreStateSyncProjection::
            mergePortableIntoLocal(
                existing,
                object);

    const bool applied = m_store->applySyncedEntry(merged);
    if (!applied && error && error->isEmpty())
        *error = m_store->persistenceError();
    return applied;
}

bool ProgressSyncAdapter::
applyRemoteAsync(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    std::function<void(bool, const QString &)> callback,
    QString *error) {
    auto reject = [&callback](const QString &message) {
        if (callback)
            callback(false, message);
    };

    if (!m_store) {
        reject(QStringLiteral(
            "The Continue/progress owner is no longer available."));
        return true;
    }
    if (!m_store->healthy(error)) {
        reject(m_store->persistenceError());
        return true;
    }
    if (schemaVersion != 1) {
        reject(QStringLiteral(
            "The Continue/progress sync schema is unsupported."));
        return true;
    }

    QString kind;
    QString id;
    if (!CoreStateSyncProjection::decodeProgressKey(recordKey, &kind, &id)) {
        reject(QStringLiteral(
            "The Continue/progress sync record key is invalid."));
        return true;
    }

    if (operation == SyncWireOperation::Delete) {
        m_store->removeSyncedEntryAsync(kind, id, std::move(callback));
        return true;
    }

    if (!payload.isObject()) {
        reject(QStringLiteral(
            "A Continue/progress PUT requires an object payload."));
        return true;
    }

    const QJsonObject object = payload.toObject();
    if (object.value(QStringLiteral("kind")).toString() != kind
        || object.value(QStringLiteral("id")).toString() != id) {
        reject(QStringLiteral(
            "The Continue/progress payload identity does not match its record key."));
        return true;
    }

    const QVariantMap portableEntry = object.toVariantMap();
    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::progress(portableEntry);
    if (projected.disposition != CoreStateSyncProjection::Disposition::Portable
        || projected.recordKey != recordKey
        || projected.payload != object) {
        reject(projected.error.isEmpty()
                   ? QStringLiteral(
                         "The Continue/progress payload is not a canonical portable record.")
                   : projected.error);
        return true;
    }

    const QVariantMap existing = m_store->get(kind, id);
    const QVariantMap merged =
        CoreStateSyncProjection::mergePortableIntoLocal(existing, object);
    m_store->applySyncedEntryAsync(merged, std::move(callback));
    return true;
}

void ProgressSyncAdapter::
noteSyncDirty() {
    if (!m_store)
        return;

    m_syncRevision =
        qMax(
            m_syncRevision,
            static_cast<quint64>(
                qMax(
                    0,
                    m_store->revision())))
        + 1;

    // Throttle rather than debounce. recordSilent() may arrive every five
    // seconds during uninterrupted playback; restarting the timer on every
    // tick could postpone sync indefinitely.
    if (!m_coalesceTimer.isActive())
        m_coalesceTimer.start();
}

void ProgressSyncAdapter::
emitImmediateLocalMutation() {
    if (!m_store)
        return;

    if (m_coalesceTimer.isActive())
        m_coalesceTimer.stop();

    emit localMutationAvailable(
        revision());
}
