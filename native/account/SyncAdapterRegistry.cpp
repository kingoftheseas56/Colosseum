// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncAdapterRegistry.h"

#include "SyncOwnershipInventory.h"
#include "SyncPayloadFirewall.h"

#include <QSet>

#include <algorithm>

SyncAdapterRegistry::SyncAdapterRegistry(
    QObject *parent)
    : QObject(parent) {
    setObjectName(
        QStringLiteral("syncAdapterRegistry"));
}

bool SyncAdapterRegistry::registerAdapter(
    SyncAdapter *adapter,
    SyncAdapterRegistryError *error) {
    if (error)
        *error = {};

    if (!registrationAllowed(
            adapter,
            error)) {
        return false;
    }

    const QString categoryId =
        canonicalCategory(
            adapter->categoryId());

    Entry entry;
    entry.categoryId = categoryId;
    entry.schemaVersion =
        adapter->schemaVersion();
    entry.identity = adapter;
    entry.adapter = adapter;

    entry.mutationConnection =
        connect(
            adapter,
            &SyncAdapter::localMutationAvailable,
            this,
            [this, categoryId](
                quint64 revision) {
                const Entry *current =
                    entryFor(categoryId);
                if (!current
                    || current->adapter.isNull()) {
                    return;
                }

                SyncAdapterRegistryError ignored;
                if (!identityMatches(
                        *current,
                        &ignored)) {
                    return;
                }

                if (m_remoteApplyDepth
                        .value(categoryId, 0)
                    > 0) {
                    return;
                }

                emit localMutationAvailable(
                    categoryId,
                    revision);
            });

    entry.destroyedConnection =
        connect(
            adapter,
            &QObject::destroyed,
            this,
            [this, categoryId](
                QObject *object) {
                handleAdapterDestroyed(
                    categoryId,
                    object);
            });

    m_entries.insert(
        categoryId,
        entry);

    emit adapterRegistered(
        categoryId);
    return true;
}

bool SyncAdapterRegistry::unregisterAdapter(
    const QString &categoryId) {
    const QString canonical =
        canonicalCategory(categoryId);
    auto it = m_entries.find(canonical);
    if (it == m_entries.end())
        return false;

    QObject::disconnect(
        it->mutationConnection);
    QObject::disconnect(
        it->destroyedConnection);

    m_entries.erase(it);
    m_remoteApplyDepth.remove(canonical);

    emit adapterUnregistered(
        canonical);
    return true;
}

bool SyncAdapterRegistry::contains(
    const QString &categoryId) const {
    return entryFor(categoryId) != nullptr;
}

QStringList SyncAdapterRegistry::registeredCategories() const {
    QStringList categories =
        m_entries.keys();
    std::sort(
        categories.begin(),
        categories.end());
    return categories;
}

bool SyncAdapterRegistry::exportSnapshot(
    const QString &categoryId,
    SyncAdapterSnapshot *snapshot,
    SyncAdapterRegistryError *error) const {
    if (error)
        *error = {};

    if (!snapshot) {
        return fail(
            error,
            QStringLiteral("invalid_argument"),
            QStringLiteral(
                "A snapshot output object is required."));
    }

    const Entry *entry =
        entryFor(categoryId);
    if (!entry) {
        return fail(
            error,
            QStringLiteral(
                "adapter_not_registered"),
            QStringLiteral(
                "No sync adapter is registered for the requested category."));
    }

    if (!identityMatches(
            *entry,
            error)) {
        return false;
    }

    SyncAdapter *adapter =
        entry->adapter.data();
    if (!adapter) {
        return fail(
            error,
            QStringLiteral("adapter_destroyed"),
            QStringLiteral(
                "The registered sync adapter no longer exists."));
    }

    SyncAdapterExport exported;
    QString adapterError;
    if (!adapter->exportSnapshot(
            &exported,
            &adapterError)) {
        return fail(
            error,
            QStringLiteral(
                "adapter_export_failed"),
            adapterError.trimmed().isEmpty()
                ? QStringLiteral(
                      "The sync adapter could not export its snapshot.")
                : adapterError);
    }

    QSet<QString> keys;
    for (const SyncAdapterRecord &record :
         exported.records) {
        if (!isValidSyncWireRecordKey(
                record.recordKey)) {
            return fail(
                error,
                QStringLiteral(
                    "invalid_record_key"),
                QStringLiteral(
                    "The adapter exported an invalid logical record key."));
        }

        if (keys.contains(
                record.recordKey)) {
            return fail(
                error,
                QStringLiteral(
                    "duplicate_record_key"),
                QStringLiteral(
                    "The adapter exported the same logical record key more than once."));
        }
        keys.insert(
            record.recordKey);

        if (!validatePutPayload(
                entry->categoryId,
                record.payload,
                error)) {
            return false;
        }
    }

    snapshot->categoryId =
        entry->categoryId;
    snapshot->schemaVersion =
        entry->schemaVersion;
    snapshot->revision =
        exported.revision;
    snapshot->missingRecordsAreDeletes =
        adapter->missingRecordsAreDeletes();
    snapshot->records =
        exported.records;
    return true;
}

bool SyncAdapterRegistry::applyRemote(
    const SyncAdapterMutation &mutation,
    SyncAdapterRegistryError *error) {
    if (error)
        *error = {};

    const QString categoryId =
        canonicalCategory(
            mutation.categoryId);
    if (mutation.categoryId
            != categoryId
        || categoryId.isEmpty()) {
        return fail(
            error,
            QStringLiteral(
                "noncanonical_category"),
            QStringLiteral(
                "Incoming category ids must be canonical inventory ids."));
    }

    if (!isValidSyncWireRecordKey(
            mutation.recordKey)) {
        return fail(
            error,
            QStringLiteral(
                "invalid_record_key"),
            QStringLiteral(
                "The incoming logical record key is invalid."));
    }

    const Entry *entry =
        entryFor(categoryId);
    if (!entry) {
        return fail(
            error,
            QStringLiteral(
                "adapter_not_registered"),
            QStringLiteral(
                "No sync adapter is registered for the incoming category."));
    }

    if (!identityMatches(
            *entry,
            error)) {
        return false;
    }

    SyncAdapter *adapter =
        entry->adapter.data();
    if (!adapter) {
        return fail(
            error,
            QStringLiteral("adapter_destroyed"),
            QStringLiteral(
                "The registered sync adapter no longer exists."));
    }

    if (mutation.schemaVersion
        != entry->schemaVersion) {
        return fail(
            error,
            QStringLiteral(
                "unsupported_schema_version"),
            QStringLiteral(
                "The incoming schema version does not match the registered adapter."));
    }

    if (mutation.operation
        == SyncWireOperation::Put) {
        if (!validatePutPayload(
                categoryId,
                mutation.payload,
                error)) {
            return false;
        }
    } else if (!mutation.payload.isUndefined()
               && !mutation.payload.isNull()) {
        return fail(
            error,
            QStringLiteral(
                "delete_payload_not_empty"),
            QStringLiteral(
                "A delete mutation cannot carry an ordinary payload."));
    }

    m_remoteApplyDepth[categoryId] =
        m_remoteApplyDepth.value(
            categoryId,
            0)
        + 1;

    QString adapterError;
    const bool applied =
        adapter->applyRemote(
            mutation.recordKey,
            mutation.operation,
            mutation.operation
                    == SyncWireOperation::Put
                ? mutation.payload
                : QJsonValue(),
            mutation.schemaVersion,
            &adapterError);

    const int depth =
        m_remoteApplyDepth.value(
            categoryId,
            1)
        - 1;
    if (depth <= 0)
        m_remoteApplyDepth.remove(categoryId);
    else
        m_remoteApplyDepth[categoryId] = depth;

    if (!applied) {
        return fail(
            error,
            QStringLiteral(
                "adapter_apply_failed"),
            adapterError.trimmed().isEmpty()
                ? QStringLiteral(
                      "The sync adapter rejected the remote record.")
                : adapterError);
    }

    emit remoteApplied(
        categoryId,
        mutation.recordKey,
        adapter->revision());
    return true;
}

bool SyncAdapterRegistry::registrationAllowed(
    SyncAdapter *adapter,
    SyncAdapterRegistryError *error) const {
    if (!adapter) {
        return fail(
            error,
            QStringLiteral(
                "adapter_required"),
            QStringLiteral(
                "A sync adapter instance is required."));
    }

    const QString rawCategory =
        adapter->categoryId();
    const QString categoryId =
        canonicalCategory(rawCategory);

    if (rawCategory != categoryId
        || categoryId.isEmpty()) {
        return fail(
            error,
            QStringLiteral(
                "noncanonical_category"),
            QStringLiteral(
                "Adapter category ids must already be canonical lowercase inventory ids."));
    }

    const SyncOwnershipEntry *inventory =
        SyncOwnershipInventory::find(
            categoryId);
    if (!inventory) {
        return fail(
            error,
            QStringLiteral(
                "unknown_category"),
            QStringLiteral(
                "The adapter category is absent from the frozen sync ownership inventory."));
    }

    if (inventory->disposition
        == SyncDisposition::Secret) {
        return fail(
            error,
            QStringLiteral(
                "secret_category"),
            QStringLiteral(
                "Secret categories cannot register ordinary sync adapters."));
    }

    if (inventory->disposition
        == SyncDisposition::LocalOnly) {
        return fail(
            error,
            QStringLiteral(
                "local_only_category"),
            QStringLiteral(
                "Local-only categories cannot register ordinary sync adapters."));
    }

    if (inventory->ownerStatus
        != SyncOwnerStatus::Confirmed) {
        return fail(
            error,
            QStringLiteral(
                "owner_not_confirmed"),
            QStringLiteral(
                "The category does not yet have a confirmed repository owner."));
    }

    if (!inventory->ordinaryPayloadEligible) {
        return fail(
            error,
            QStringLiteral(
                "category_not_exportable_yet"),
            QStringLiteral(
                "The category is not yet eligible for a portable ordinary-sync payload."));
    }

    if (adapter->schemaVersion() <= 0) {
        return fail(
            error,
            QStringLiteral(
                "invalid_schema_version"),
            QStringLiteral(
                "Adapter schema versions must be positive integers."));
    }

    if (m_entries.contains(categoryId)) {
        return fail(
            error,
            QStringLiteral(
                "duplicate_category"),
            QStringLiteral(
                "Exactly one adapter may be registered for a sync category."));
    }

    for (auto it = m_entries.constBegin();
         it != m_entries.constEnd();
         ++it) {
        if (it->identity == adapter) {
            return fail(
                error,
                QStringLiteral(
                    "adapter_already_registered"),
                QStringLiteral(
                    "One adapter instance may own only one registered sync category."));
        }
    }

    return true;
}

bool SyncAdapterRegistry::identityMatches(
    const Entry &entry,
    SyncAdapterRegistryError *error) const {
    SyncAdapter *adapter =
        entry.adapter.data();
    if (!adapter) {
        return fail(
            error,
            QStringLiteral(
                "adapter_destroyed"),
            QStringLiteral(
                "The registered sync adapter no longer exists."));
    }

    if (adapter->categoryId()
        != entry.categoryId) {
        return fail(
            error,
            QStringLiteral(
                "adapter_identity_changed"),
            QStringLiteral(
                "A registered adapter changed its category identity after registration."));
    }

    if (adapter->schemaVersion()
        != entry.schemaVersion) {
        return fail(
            error,
            QStringLiteral(
                "adapter_schema_changed"),
            QStringLiteral(
                "A registered adapter changed its schema version after registration."));
    }

    return true;
}

bool SyncAdapterRegistry::validatePutPayload(
    const QString &categoryId,
    const QJsonValue &payload,
    SyncAdapterRegistryError *error) const {
    const SyncPayloadValidation validation =
        SyncPayloadFirewall::validate(
            categoryId,
            payload);

    if (validation.allowed)
        return true;

    return fail(
        error,
        validation.code,
        validation.detail,
        validation.fieldPath);
}

SyncAdapterRegistry::Entry *
SyncAdapterRegistry::entryFor(
    const QString &categoryId) {
    const QString canonical =
        canonicalCategory(categoryId);
    auto it =
        m_entries.find(canonical);
    if (it == m_entries.end())
        return nullptr;
    return &it.value();
}

const SyncAdapterRegistry::Entry *
SyncAdapterRegistry::entryFor(
    const QString &categoryId) const {
    const QString canonical =
        canonicalCategory(categoryId);
    auto it =
        m_entries.constFind(canonical);
    if (it == m_entries.constEnd())
        return nullptr;
    return &it.value();
}

QString SyncAdapterRegistry::canonicalCategory(
    const QString &categoryId) {
    return categoryId
        .trimmed()
        .toLower();
}

bool SyncAdapterRegistry::fail(
    SyncAdapterRegistryError *error,
    const QString &code,
    const QString &detail,
    const QString &fieldPath) {
    if (error) {
        error->code = code;
        error->detail = detail;
        error->fieldPath = fieldPath;
    }
    return false;
}

void SyncAdapterRegistry::handleAdapterDestroyed(
    const QString &categoryId,
    QObject *object) {
    auto it =
        m_entries.find(categoryId);
    if (it == m_entries.end())
        return;

    if (static_cast<QObject *>(
            it->identity)
        != object) {
        return;
    }

    QObject::disconnect(
        it->mutationConnection);
    QObject::disconnect(
        it->destroyedConnection);

    m_entries.erase(it);
    m_remoteApplyDepth.remove(categoryId);

    emit adapterUnregistered(
        categoryId);
}
