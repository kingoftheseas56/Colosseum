#pragma once

#include "RatingsReviewsDeliveryTypes.h"

#include <QString>

#include <optional>

class RatingsReviewsDeliveryOutbox final
{
public:
    explicit RatingsReviewsDeliveryOutbox(const QString &path);

    bool healthy(QString *error = nullptr) const;
    QString persistenceError() const;
    QList<RatingsReviewsDeliveryOperation> operations() const;
    std::optional<RatingsReviewsDeliveryOperation> operation(const QString &operationId) const;
    bool append(const QList<RatingsReviewsDeliveryOperation> &operations,
                QString *error = nullptr);
    bool replace(const RatingsReviewsDeliveryOperation &operation,
                 QString *error = nullptr);
    bool replaceAll(const QList<RatingsReviewsDeliveryOperation> &operations,
                    QString *error = nullptr);
    bool flush(QString *error = nullptr) const;

private:
    bool load();
    bool persist(const QList<RatingsReviewsDeliveryOperation> &candidate,
                 QString *error) const;

    QString m_path;
    QList<RatingsReviewsDeliveryOperation> m_operations;
    bool m_healthy = true;
    QString m_persistenceError;
};
