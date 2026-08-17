// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "CollectionSyncAdapter.h"

#include "CoreStateSyncProjection.h"

#include "CollectionStore.h"

#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

CollectionSyncAdapter::
CollectionSyncAdapter(
    CollectionStore *store,
    QObject *parent)
    : SyncAdapter(parent),
      m_store(store) {
    Q_ASSERT(store);

    setObjectName(
        QStringLiteral(
            "collectionSyncAdapter"));

    if (!store)
        return;

    connect(
        store,
        &CollectionStore::changed,
        this,
        [this]() {
            if (!m_store)
                return;

            emit localMutationAvailable(
                revision());
        });
}

QString CollectionSyncAdapter::
categoryId() const {
    return QStringLiteral("collection");
}

int CollectionSyncAdapter::
schemaVersion() const {
    return 1;
}

quint64 CollectionSyncAdapter::
revision() const {
    if (!m_store)
        return 0;

    return static_cast<quint64>(
        qMax(
            0,
            m_store->revision()));
}

bool CollectionSyncAdapter::
exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot) {
        if (error) {
            *error = QStringLiteral(
                "Collection sync requires an export output object.");
        }
        return false;
    }

    if (!m_store) {
        if (error) {
            *error = QStringLiteral(
                "The Collection owner is no longer available.");
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
                collection(entry);

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
                          "Collection contains a record that cannot be represented safely in ordinary sync.")
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

bool CollectionSyncAdapter::
applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    QString *error) {
    if (!m_store) {
        if (error) {
            *error = QStringLiteral(
                "The Collection owner is no longer available.");
        }
        return false;
    }

    if (schemaVersion != 1) {
        if (error) {
            *error = QStringLiteral(
                "The Collection sync schema is unsupported.");
        }
        return false;
    }

    QString world;
    QString id;
    if (!CoreStateSyncProjection::
             decodeCollectionKey(
                 recordKey,
                 &world,
                 &id)) {
        if (error) {
            *error = QStringLiteral(
                "The Collection sync record key is invalid.");
        }
        return false;
    }

    if (operation
        == SyncWireOperation::Delete) {
        m_store->remove(
            world,
            id);
        return true;
    }

    if (!payload.isObject()) {
        if (error) {
            *error = QStringLiteral(
                "A Collection PUT requires an object payload.");
        }
        return false;
    }

    const QJsonObject object =
        payload.toObject();

    if (object.value(
            QStringLiteral("world"))
            .toString()
            != world
        || object.value(
               QStringLiteral("id"))
               .toString()
               != id) {
        if (error) {
            *error = QStringLiteral(
                "The Collection payload identity does not match its record key.");
        }
        return false;
    }

    const QVariantMap portableEntry =
        object.toVariantMap();

    const CoreStateSyncProjection projected =
        CoreStateSyncProjection::
            collection(portableEntry);
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
                      "The Collection payload is not a canonical portable record.")
                : projected.error;
        }
        return false;
    }

    QVariantMap existing;
    const QVariantList localEntries =
        m_store->items(
            world);
    for (const QVariant &value :
         localEntries) {
        const QVariantMap candidate =
            value.toMap();
        if (candidate.value(
                QStringLiteral("id"))
                .toString()
            == id) {
            existing =
                candidate;
            break;
        }
    }

    const QVariantMap merged =
        CoreStateSyncProjection::
            mergePortableIntoLocal(
                existing,
                object);

    m_store->add(
        world,
        merged);
    return true;
}
