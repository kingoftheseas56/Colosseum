#pragma once

#include "SyncAdapter.h"

#include <QPointer>

class ProgressStore;

class WatchStateSyncAdapter final
    : public SyncAdapter {
    Q_OBJECT

public:
    explicit WatchStateSyncAdapter(
        ProgressStore *store,
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
    void handleStoreChanged();

    static bool fail(
        QString *error,
        const QString &detail);

    QPointer<ProgressStore> m_store;
    quint64 m_revision = 0;
    bool m_applyingRemote = false;
};
