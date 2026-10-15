#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncAdapter.h"

#include <QPointer>

class CollectionStore;

class CollectionSyncAdapter final
    : public SyncAdapter {
    Q_OBJECT

public:
    explicit CollectionSyncAdapter(
        CollectionStore *store,
        QObject *parent = nullptr);

    QString categoryId() const override;
    int schemaVersion() const override;
    quint64 revision() const override;
    bool remoteApplyEmitsLocalMutation() const override { return false; }

    bool exportSnapshot(
        SyncAdapterExport *snapshot,
        QString *error = nullptr) const override;

    bool validateRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        SyncAdapterValidationError *error = nullptr) const override;

    bool applyRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        QString *error = nullptr) override;

    bool applyRemoteAsync(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        std::function<void(bool, const QString &)> callback,
        QString *error = nullptr) override;

private:
    QPointer<CollectionStore> m_store;
};
