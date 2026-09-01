#pragma once

#include "SyncAdapter.h"

#include <QPointer>
#include <QVariantMap>

class ActivityStore;

class ActivitySyncAdapter final : public SyncAdapter {
    Q_OBJECT

public:
    explicit ActivitySyncAdapter(
        ActivityStore *store,
        QObject *parent = nullptr);

    QString categoryId() const override;
    int schemaVersion() const override;
    quint64 revision() const override;
    bool missingRecordsAreDeletes() const override;

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
    void handleFactCommitted(
        const QVariantMap &event);

    static QString normalizedUuid(const QString &value);
    static bool decodeRecordKey(
        const QString &recordKey,
        QString *lowerEventId);
    static bool fail(
        QString *error,
        const QString &detail);

    QPointer<ActivityStore> m_store;
    quint64 m_revision = 0;
    bool m_applyingRemote = false;
};
