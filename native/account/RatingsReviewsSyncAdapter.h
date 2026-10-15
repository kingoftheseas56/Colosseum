#pragma once

#include "SyncAdapter.h"

class RatingsReviewsStore;

class RatingsReviewsSyncAdapter final : public SyncAdapter
{
    Q_OBJECT

public:
    explicit RatingsReviewsSyncAdapter(
        RatingsReviewsStore *store,
        QObject *parent = nullptr);

    QString categoryId() const override;
    int schemaVersion() const override;
    quint64 revision() const override;

    bool missingRecordsAreDeletes() const override {
        return false;
    }

    bool remoteApplyEmitsLocalMutation() const override {
        return false;
    }

    bool exportSnapshot(
        SyncAdapterExport *snapshot,
        QString *error = nullptr) const override;

    bool validateRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        SyncAdapterValidationError *error = nullptr) const override;

    bool validateRemoteMutation(
        const SyncAdapterMutation &mutation,
        SyncAdapterValidationError *error = nullptr) const override;

    bool applyRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        QString *error = nullptr) override;

    bool applyRemoteMutation(
        const SyncAdapterMutation &mutation,
        QString *error = nullptr) override;

    bool applyRemoteMutationAsync(
        const SyncAdapterMutation &mutation,
        std::function<void(bool, const QString &)> callback,
        QString *error = nullptr) override;

private:
    RatingsReviewsStore *m_store = nullptr;
};
