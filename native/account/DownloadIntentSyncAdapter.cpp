#include "DownloadIntentSyncAdapter.h"

#include "DownloadIntentStore.h"

DownloadIntentSyncAdapter::DownloadIntentSyncAdapter(
    DownloadIntentStore *store,
    QObject *parent)
    : SyncAdapter(parent),
      m_store(store) {
    Q_ASSERT(store);
    setObjectName(QStringLiteral("downloadIntentSyncAdapter"));
    if (store) {
        connect(store, &DownloadIntentStore::changed, this, [this] {
            if (m_store)
                emit localMutationAvailable(m_store->revision());
        });
    }
}

QString DownloadIntentSyncAdapter::categoryId() const {
    return QStringLiteral("desired_download_intent");
}

int DownloadIntentSyncAdapter::schemaVersion() const {
    return 1;
}

quint64 DownloadIntentSyncAdapter::revision() const {
    return m_store ? m_store->revision() : 0;
}

bool DownloadIntentSyncAdapter::exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!m_store) {
        if (error)
            *error = QStringLiteral("The download intent owner is unavailable.");
        return false;
    }
    return m_store->exportSnapshot(snapshot, error);
}

bool DownloadIntentSyncAdapter::applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    QString *error) {
    if (!m_store) {
        if (error)
            *error = QStringLiteral("The download intent owner is unavailable.");
        return false;
    }
    return m_store->applyRemote(
        recordKey,
        operation,
        payload,
        schemaVersion,
        error);
}
