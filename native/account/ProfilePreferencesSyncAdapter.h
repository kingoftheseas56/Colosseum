#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncAdapter.h"

class ProfilePreferencesStore;

class ProfilePreferencesSyncAdapter final
    : public SyncAdapter {
    Q_OBJECT

public:
    explicit ProfilePreferencesSyncAdapter(
        ProfilePreferencesStore *store,
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

    static QString fixedRecordKey();

private:
    static bool fail(
        QString *error,
        const QString &message);

    ProfilePreferencesStore *m_store =
        nullptr;
};
