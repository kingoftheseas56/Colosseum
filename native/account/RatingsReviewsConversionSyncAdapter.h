#pragma once

#include "RatingsReviewsConversionMap.h"
#include "SyncAdapter.h"

class ProfilePreferencesStore;

class RatingsReviewsConversionSyncAdapter final : public SyncAdapter {
    Q_OBJECT
public:
    explicit RatingsReviewsConversionSyncAdapter(
        ProfilePreferencesStore *store,
        QObject *parent = nullptr);

    QString categoryId() const override;
    int schemaVersion() const override;
    quint64 revision() const override;
    bool remoteApplyEmitsLocalMutation() const override;

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

    static QString recordKeyForProvider(const QString &providerId);

private:
    bool providerAllowed(const QString &providerId) const;
    bool parseRecordKey(
        const QString &recordKey,
        QString *providerId) const;
    bool decodePut(
        const QString &recordKey,
        const QJsonValue &payload,
        int schemaVersion,
        RatingsReviewsConversionMap *map,
        SyncAdapterValidationError *validationError,
        QString *error) const;

    ProfilePreferencesStore *m_store = nullptr;
};
