// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProgressSyncAdapter.h"

#include "CoreStateSyncProjection.h"

#include "ProgressStore.h"

#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

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
        &ProgressStore::changed,
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
        return m_store
            ->removeSyncedEntry(
                kind,
                id);
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

    return m_store
        ->applySyncedEntry(
            merged);
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
