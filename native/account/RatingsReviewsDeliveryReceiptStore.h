#pragma once

#include "RatingsReviewsDeliveryTypes.h"

#include <QString>

#include <optional>

class RatingsReviewsDeliveryReceiptStore final
{
public:
    explicit RatingsReviewsDeliveryReceiptStore(const QString &path);

    bool healthy(QString *error = nullptr) const;
    QString persistenceError() const;
    QList<RatingsReviewsDeliveryReceipt> receipts() const;
    std::optional<RatingsReviewsDeliveryReceipt> receipt(const QString &operationId) const;
    bool upsert(const RatingsReviewsDeliveryReceipt &receipt,
                QString *error = nullptr);
    bool replaceAll(const QList<RatingsReviewsDeliveryReceipt> &receipts,
                    QString *error = nullptr);

private:
    bool load();
    bool persist(const QList<RatingsReviewsDeliveryReceipt> &candidate,
                 QString *error) const;

    QString m_path;
    QList<RatingsReviewsDeliveryReceipt> m_receipts;
    bool m_healthy = true;
    QString m_persistenceError;
};
