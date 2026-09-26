#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

struct RatingsReviewsProviderMapping final {
    QString providerId;
    QString canonicalKey;
    QString status;
    QString providerMediaId;
    QString resolution;
    qint64 confirmedAtMs = 0;
    quint64 connectionGeneration = 0;
};

struct RatingsReviewsDeliveryOperation final {
    QString operationId;
    QString profileId;
    quint64 profileIncarnation = 0;
    QString providerId;
    quint64 connectionGeneration = 0;
    QString operationType;
    QString canonicalKey;
    quint64 canonicalRevision = 0;
    QString canonicalPayloadDigest;
    QString mappingProviderMediaId;
    QString conversionMapDigest;
    qint64 intentCreatedAtMs = 0;
    QString state;
    int attemptCount = 0;
    std::optional<qint64> lastAttemptAtMs;
    QString retryClass;
    QString adapterIdempotency;
    QJsonObject safePayload;
    QString staleReason;
};

struct RatingsReviewsDeliveryReceipt final {
    QString operationId;
    QString providerId;
    QString operationType;
    QString canonicalKey;
    QString canonicalPayloadDigest;
    QString status;
    int attemptNumber = 0;
    qint64 createdAtMs = 0;
    qint64 updatedAtMs = 0;
    qint64 completedAtMs = 0;
    QString safeProviderStatus;
    QString safeErrorClass;
    QString safeRemoteVersion;
    std::optional<qint64> safeRemoteTimestampMs;
    bool reconciled = false;
};

bool ratingsReviewsDeliveryIsUuid(const QString &value);
bool ratingsReviewsDeliveryIsSha256(const QString &value);
bool ratingsReviewsDeliveryIsOperationType(const QString &value);
bool ratingsReviewsDeliveryIsOperationState(const QString &value);
bool ratingsReviewsDeliveryIsTerminalState(const QString &value);
QString ratingsReviewsDeliveryMappingKey(
    const QString &providerId,
    const QString &canonicalKey);
QJsonObject ratingsReviewsDeliveryMappingToJson(
    const RatingsReviewsProviderMapping &mapping);
std::optional<RatingsReviewsProviderMapping> ratingsReviewsDeliveryMappingFromJson(
    const QJsonObject &object,
    QString *error = nullptr);
QJsonObject ratingsReviewsDeliveryOperationToJson(
    const RatingsReviewsDeliveryOperation &operation);
std::optional<RatingsReviewsDeliveryOperation> ratingsReviewsDeliveryOperationFromJson(
    const QJsonObject &object,
    QString *error = nullptr);
QJsonObject ratingsReviewsDeliveryReceiptToJson(
    const RatingsReviewsDeliveryReceipt &receipt);
std::optional<RatingsReviewsDeliveryReceipt> ratingsReviewsDeliveryReceiptFromJson(
    const QJsonObject &object,
    QString *error = nullptr);
