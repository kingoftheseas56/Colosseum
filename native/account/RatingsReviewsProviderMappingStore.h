#pragma once

#include "RatingsReviewsDeliveryTypes.h"

#include <QString>

#include <optional>

class RatingsReviewsProviderMappingStore final
{
public:
    explicit RatingsReviewsProviderMappingStore(const QString &path);

    bool healthy(QString *error = nullptr) const;
    QString persistenceError() const;
    QString storagePath() const;
    std::optional<RatingsReviewsProviderMapping> mapping(
        const QString &providerId,
        const QString &canonicalKey) const;
    QList<RatingsReviewsProviderMapping> mappings() const;
    bool upsert(const RatingsReviewsProviderMapping &mapping,
                QString *error = nullptr);
    bool replaceAll(const QList<RatingsReviewsProviderMapping> &mappings,
                    QString *error = nullptr);
    bool flush(QString *error = nullptr) const;

private:
    bool load();
    bool persist(const QList<RatingsReviewsProviderMapping> &candidate,
                 QString *error) const;

    QString m_path;
    QList<RatingsReviewsProviderMapping> m_mappings;
    bool m_healthy = true;
    QString m_persistenceError;
};
