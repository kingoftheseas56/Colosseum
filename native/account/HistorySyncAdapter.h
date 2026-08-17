#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncAdapter.h"

class HistoryStore;

class HistorySyncAdapter final
    : public SyncAdapter {
    Q_OBJECT

public:
    explicit HistorySyncAdapter(
        HistoryStore *store,
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
    static bool fail(
        QString *error,
        const QString &message);

    HistoryStore *m_store = nullptr;
};
