#pragma once

#include "SyncAdapter.h"

#include <QPointer>

class DownloadIntentStore;

class DownloadIntentSyncAdapter final : public SyncAdapter {
    Q_OBJECT

public:
    explicit DownloadIntentSyncAdapter(
        DownloadIntentStore *store,
        QObject *parent = nullptr);

    QString categoryId() const override;
    int schemaVersion() const override;
    quint64 revision() const override;

    bool exportSnapshot(
        SyncAdapterExport *snapshot,
        QString *error = nullptr) const override;

    bool applyRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        QString *error = nullptr) override;

private:
    QPointer<DownloadIntentStore> m_store;
};
