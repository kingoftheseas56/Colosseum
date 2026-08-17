#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncAdapter.h"

#include <QPointer>
#include <QTimer>

class ProgressStore;

class ProgressSyncAdapter final
    : public SyncAdapter {
    Q_OBJECT

public:
    explicit ProgressSyncAdapter(
        ProgressStore *store,
        QObject *parent = nullptr,
        int coalesceMs = 15 * 1000);

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
    void noteSyncDirty();
    void emitImmediateLocalMutation();

    QPointer<ProgressStore> m_store;
    QTimer m_coalesceTimer;
    quint64 m_syncRevision = 0;
};
