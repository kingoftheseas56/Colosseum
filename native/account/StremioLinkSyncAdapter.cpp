#include "StremioLinkSyncAdapter.h"

#include "ProfilePreferencesStore.h"

#include <QJsonObject>

StremioLinkSyncAdapter::StremioLinkSyncAdapter(
    ProfilePreferencesStore *store,
    QObject *parent)
    : SyncAdapter(parent),
      m_store(store) {
    Q_ASSERT(m_store);
    setObjectName(QStringLiteral("stremioLinkSyncAdapter"));
    connect(m_store, &ProfilePreferencesStore::stremioLinkDirty, this, [this] {
        emit localMutationAvailable(revision());
    });
}

QString StremioLinkSyncAdapter::categoryId() const {
    return QStringLiteral("stremio_link");
}

int StremioLinkSyncAdapter::schemaVersion() const { return 1; }

quint64 StremioLinkSyncAdapter::revision() const {
    return m_store ? static_cast<quint64>(qMax(0, m_store->revision())) : 0;
}

bool StremioLinkSyncAdapter::exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot || !m_store)
        return fail(error, QStringLiteral("The Stremio link owner is unavailable."));
    snapshot->revision = revision();
    snapshot->records.clear();
    snapshot->tombstones.clear();
    if (m_store->mainSyncProvider().isEmpty())
        return true;
    snapshot->records = {SyncAdapterRecord{
        fixedRecordKey(),
        QJsonObject{{QStringLiteral("mainSyncProvider"), QStringLiteral("stremio")}},
        -1}};
    return true;
}

bool StremioLinkSyncAdapter::validateRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    SyncAdapterValidationError *error) const {
    if (error)
        *error = {};
    if (schemaVersionValue != schemaVersion() || recordKey != fixedRecordKey())
        return false;
    if (operation == SyncWireOperation::Delete)
        return true;
    const QJsonObject object = payload.toObject();
    if (!payload.isObject() || object.size() != 1
        || object.value(QStringLiteral("mainSyncProvider")) != QStringLiteral("stremio")) {
        return false;
    }
    return true;
}

bool StremioLinkSyncAdapter::applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    QString *error) {
    if (!m_store || schemaVersionValue != schemaVersion() || recordKey != fixedRecordKey())
        return fail(error, QStringLiteral("The Stremio link record is invalid."));
    if (operation == SyncWireOperation::Delete)
        return m_store->clearSyncedMainSyncProvider();
    if (!validateRemote(recordKey, operation, payload, schemaVersionValue))
        return fail(error, QStringLiteral("The Stremio link payload is malformed."));
    return m_store->applySyncedMainSyncProvider(QStringLiteral("stremio"));
}

QString StremioLinkSyncAdapter::fixedRecordKey() {
    return QStringLiteral("preferences/main-sync-provider");
}

bool StremioLinkSyncAdapter::fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
