#pragma once

#include "SyncAdapter.h"

class ProfilePreferencesStore;

class StremioLinkSyncAdapter final : public SyncAdapter {
    Q_OBJECT

public:
    explicit StremioLinkSyncAdapter(
        ProfilePreferencesStore *store,
        QObject *parent = nullptr);

    QString categoryId() const override;
    int schemaVersion() const override;
    quint64 revision() const override;
    bool exportSnapshot(SyncAdapterExport *snapshot, QString *error = nullptr) const override;
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

    static QString fixedRecordKey();

private:
    static bool fail(QString *error, const QString &message);
    ProfilePreferencesStore *m_store = nullptr;
};
