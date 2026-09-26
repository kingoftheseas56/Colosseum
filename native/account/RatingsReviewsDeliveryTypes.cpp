#include "RatingsReviewsDeliveryTypes.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool requiredString(const QJsonObject &object,
                    const char *key,
                    QString *value,
                    QString *error)
{
    const QJsonValue json = object.value(QLatin1String(key));
    if (!json.isString() || json.toString().isEmpty())
        return fail(error, QStringLiteral("Missing required string '%1'.").arg(QLatin1String(key)));
    *value = json.toString();
    return true;
}

bool requiredInteger(const QJsonObject &object,
                     const char *key,
                     qint64 *value,
                     QString *error)
{
    const QJsonValue json = object.value(QLatin1String(key));
    if (!json.isDouble())
        return fail(error, QStringLiteral("Missing required integer '%1'.").arg(QLatin1String(key)));
    const double number = json.toDouble();
    const qint64 integer = json.toInteger();
    if (number != static_cast<double>(integer))
        return fail(error, QStringLiteral("Field '%1' must be an integer.").arg(QLatin1String(key)));
    *value = integer;
    return true;
}

bool isSafeReceiptString(const QString &value)
{
    return !value.contains(QLatin1Char('\n'))
        && !value.contains(QLatin1Char('\r'))
        && value.size() <= 512;
}

} // namespace

bool ratingsReviewsDeliveryIsUuid(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(value).hasMatch();
}

bool ratingsReviewsDeliveryIsSha256(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(value).hasMatch();
}

bool ratingsReviewsDeliveryIsOperationType(const QString &value)
{
    return value == QLatin1String("rating.set") || value == QLatin1String("review.set");
}

bool ratingsReviewsDeliveryIsOperationState(const QString &value)
{
    static const QSet<QString> allowed = {
        QStringLiteral("pending"), QStringLiteral("inFlight"),
        QStringLiteral("retrying"), QStringLiteral("succeeded"),
        QStringLiteral("needsAttention"), QStringLiteral("failedTerminal"),
        QStringLiteral("unknownOutcome"), QStringLiteral("cancelled")};
    return allowed.contains(value);
}

bool ratingsReviewsDeliveryIsTerminalState(const QString &value)
{
    return value == QLatin1String("succeeded")
        || value == QLatin1String("needsAttention")
        || value == QLatin1String("failedTerminal")
        || value == QLatin1String("cancelled");
}

QString ratingsReviewsDeliveryMappingKey(
    const QString &providerId,
    const QString &canonicalKey)
{
    return providerId + QChar(0x1f) + canonicalKey;
}

QJsonObject ratingsReviewsDeliveryMappingToJson(
    const RatingsReviewsProviderMapping &mapping)
{
    QJsonObject object = {
        {QStringLiteral("version"), 1},
        {QStringLiteral("provider_id"), mapping.providerId},
        {QStringLiteral("canonical_key"), mapping.canonicalKey},
        {QStringLiteral("status"), mapping.status},
        {QStringLiteral("confirmed_at_ms"), mapping.confirmedAtMs},
        {QStringLiteral("connection_generation"), static_cast<qint64>(mapping.connectionGeneration)}};
    if (mapping.status == QLatin1String("matched")) {
        object.insert(QStringLiteral("provider_media_id"), mapping.providerMediaId);
        object.insert(QStringLiteral("resolution"), mapping.resolution);
    }
    return object;
}

std::optional<RatingsReviewsProviderMapping> ratingsReviewsDeliveryMappingFromJson(
    const QJsonObject &object,
    QString *error)
{
    if (object.value(QStringLiteral("version")).toInt() != 1) {
        fail(error, QStringLiteral("Unsupported Ratings/Reviews mapping version."));
        return std::nullopt;
    }
    RatingsReviewsProviderMapping mapping;
    qint64 generation = 0;
    if (!requiredString(object, "provider_id", &mapping.providerId, error)
        || !requiredString(object, "canonical_key", &mapping.canonicalKey, error)
        || !requiredString(object, "status", &mapping.status, error)
        || !requiredInteger(object, "confirmed_at_ms", &mapping.confirmedAtMs, error)
        || !requiredInteger(object, "connection_generation", &generation, error)
        || generation < 0) {
        return std::nullopt;
    }
    mapping.connectionGeneration = static_cast<quint64>(generation);
    if (mapping.status != QLatin1String("matched")
        && mapping.status != QLatin1String("missing")
        && mapping.status != QLatin1String("ambiguous")
        && mapping.status != QLatin1String("unsupported")) {
        fail(error, QStringLiteral("Invalid Ratings/Reviews mapping status."));
        return std::nullopt;
    }
    if (mapping.status == QLatin1String("matched")) {
        if (!requiredString(object, "provider_media_id", &mapping.providerMediaId, error)
            || !requiredString(object, "resolution", &mapping.resolution, error)
            || (mapping.resolution != QLatin1String("exact")
                && mapping.resolution != QLatin1String("explicitUserAccepted"))) {
            if (error && error->isEmpty())
                *error = QStringLiteral("Matched mapping has invalid resolution.");
            return std::nullopt;
        }
    } else {
        // Earlier local writers emitted both empty fields for non-matches.
        // Admit that harmless shape while rejecting any writable identity.
        const QJsonValue mediaId = object.value(QStringLiteral("provider_media_id"));
        const QJsonValue resolution = object.value(QStringLiteral("resolution"));
        if ((!mediaId.isUndefined() && (!mediaId.isString() || !mediaId.toString().isEmpty()))
            || (!resolution.isUndefined()
                && (!resolution.isString() || !resolution.toString().isEmpty()))) {
            fail(error, QStringLiteral("Unmatched mapping contains writable media identity."));
            return std::nullopt;
        }
    }
    return mapping;
}

QJsonObject ratingsReviewsDeliveryOperationToJson(
    const RatingsReviewsDeliveryOperation &operation)
{
    QJsonObject object = {
        {QStringLiteral("version"), 1},
        {QStringLiteral("operation_id"), operation.operationId},
        {QStringLiteral("profile_id"), operation.profileId},
        {QStringLiteral("profile_incarnation"), static_cast<qint64>(operation.profileIncarnation)},
        {QStringLiteral("provider_id"), operation.providerId},
        {QStringLiteral("connection_generation"), static_cast<qint64>(operation.connectionGeneration)},
        {QStringLiteral("operation_type"), operation.operationType},
        {QStringLiteral("canonical_key"), operation.canonicalKey},
        {QStringLiteral("canonical_revision"), static_cast<qint64>(operation.canonicalRevision)},
        {QStringLiteral("canonical_payload_digest"), operation.canonicalPayloadDigest},
        {QStringLiteral("mapping_provider_media_id"), operation.mappingProviderMediaId},
        {QStringLiteral("conversion_map_digest"), operation.conversionMapDigest},
        {QStringLiteral("intent_created_at_ms"), operation.intentCreatedAtMs},
        {QStringLiteral("state"), operation.state},
        {QStringLiteral("attempt_count"), operation.attemptCount},
        {QStringLiteral("last_attempt_at_ms"), operation.lastAttemptAtMs
            ? QJsonValue(*operation.lastAttemptAtMs) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("retry_class"), operation.retryClass},
        {QStringLiteral("adapter_idempotency"), operation.adapterIdempotency},
        {QStringLiteral("safe_payload"), operation.safePayload}};
    if (!operation.staleReason.isEmpty())
        object.insert(QStringLiteral("stale_reason"), operation.staleReason);
    return object;
}

std::optional<RatingsReviewsDeliveryOperation> ratingsReviewsDeliveryOperationFromJson(
    const QJsonObject &object,
    QString *error)
{
    if (object.value(QStringLiteral("version")).toInt() != 1) {
        fail(error, QStringLiteral("Unsupported Ratings/Reviews delivery operation version."));
        return std::nullopt;
    }
    RatingsReviewsDeliveryOperation operation;
    qint64 incarnation = 0;
    qint64 generation = 0;
    qint64 revision = 0;
    qint64 attempts = 0;
    if (!requiredString(object, "operation_id", &operation.operationId, error)
        || !requiredString(object, "profile_id", &operation.profileId, error)
        || !requiredInteger(object, "profile_incarnation", &incarnation, error)
        || !requiredString(object, "provider_id", &operation.providerId, error)
        || !requiredInteger(object, "connection_generation", &generation, error)
        || !requiredString(object, "operation_type", &operation.operationType, error)
        || !requiredString(object, "canonical_key", &operation.canonicalKey, error)
        || !requiredInteger(object, "canonical_revision", &revision, error)
        || !requiredString(object, "canonical_payload_digest", &operation.canonicalPayloadDigest, error)
        || !requiredString(object, "mapping_provider_media_id", &operation.mappingProviderMediaId, error)
        || !requiredInteger(object, "intent_created_at_ms", &operation.intentCreatedAtMs, error)
        || !requiredString(object, "state", &operation.state, error)
        || !requiredInteger(object, "attempt_count", &attempts, error)
        || !requiredString(object, "retry_class", &operation.retryClass, error)
        || !requiredString(object, "adapter_idempotency", &operation.adapterIdempotency, error)
        || !object.value(QStringLiteral("safe_payload")).isObject()) {
        return std::nullopt;
    }
    const QJsonValue conversionMapDigest = object.value(QStringLiteral("conversion_map_digest"));
    if (!conversionMapDigest.isString()) {
        fail(error, QStringLiteral("Missing required string 'conversion_map_digest'."));
        return std::nullopt;
    }
    operation.conversionMapDigest = conversionMapDigest.toString();
    if (!ratingsReviewsDeliveryIsUuid(operation.operationId)
        || !ratingsReviewsDeliveryIsOperationType(operation.operationType)
        || !ratingsReviewsDeliveryIsOperationState(operation.state)
        || !ratingsReviewsDeliveryIsSha256(operation.canonicalPayloadDigest)
        || incarnation < 0 || generation < 0 || revision < 0 || attempts < 0) {
        fail(error, QStringLiteral("Invalid immutable Ratings/Reviews delivery operation binding."));
        return std::nullopt;
    }
    operation.profileIncarnation = static_cast<quint64>(incarnation);
    operation.connectionGeneration = static_cast<quint64>(generation);
    operation.canonicalRevision = static_cast<quint64>(revision);
    operation.attemptCount = static_cast<int>(attempts);
    const QJsonValue lastAttempt = object.value(QStringLiteral("last_attempt_at_ms"));
    if (!lastAttempt.isNull()) {
        qint64 value = 0;
        if (!lastAttempt.isDouble()
            || !requiredInteger(object, "last_attempt_at_ms", &value, error)) {
            return std::nullopt;
        }
        operation.lastAttemptAtMs = value;
    }
    operation.safePayload = object.value(QStringLiteral("safe_payload")).toObject();
    const QString kind = operation.safePayload.value(QStringLiteral("kind")).toString();
    if ((operation.operationType == QLatin1String("rating.set") && kind != QLatin1String("rating"))
        || (operation.operationType == QLatin1String("review.set") && kind != QLatin1String("review"))) {
        fail(error, QStringLiteral("Delivery safe payload does not match operation type."));
        return std::nullopt;
    }
    if (operation.operationType == QLatin1String("rating.set")
        && (!operation.safePayload.contains(QStringLiteral("native_value"))
            || !operation.safePayload.contains(QStringLiteral("source_rating"))
            || !ratingsReviewsDeliveryIsSha256(operation.conversionMapDigest))) {
        fail(error, QStringLiteral("Rating operation lacks a durable score binding."));
        return std::nullopt;
    }
    if (operation.operationType == QLatin1String("review.set")) {
        const QString text = operation.safePayload.value(QStringLiteral("text")).toString();
        const QString variant = operation.safePayload.value(QStringLiteral("variant")).toString();
        const QString variantDigest = operation.safePayload.value(QStringLiteral("variant_digest")).toString();
        if (text.isEmpty() || (variant != QLatin1String("canonical") && variant != QLatin1String("shortened"))
            || !ratingsReviewsDeliveryIsSha256(variantDigest)
            || !operation.conversionMapDigest.isEmpty()) {
            fail(error, QStringLiteral("Review operation lacks an exact private variant binding."));
            return std::nullopt;
        }
    }
    operation.staleReason = object.value(QStringLiteral("stale_reason")).toString();
    return operation;
}

QJsonObject ratingsReviewsDeliveryReceiptToJson(
    const RatingsReviewsDeliveryReceipt &receipt)
{
    return {
        {QStringLiteral("version"), 1},
        {QStringLiteral("operation_id"), receipt.operationId},
        {QStringLiteral("provider_id"), receipt.providerId},
        {QStringLiteral("operation_type"), receipt.operationType},
        {QStringLiteral("canonical_key"), receipt.canonicalKey},
        {QStringLiteral("canonical_payload_digest"), receipt.canonicalPayloadDigest},
        {QStringLiteral("status"), receipt.status},
        {QStringLiteral("attempt_number"), receipt.attemptNumber},
        {QStringLiteral("created_at_ms"), receipt.createdAtMs},
        {QStringLiteral("updated_at_ms"), receipt.updatedAtMs},
        {QStringLiteral("completed_at_ms"), receipt.completedAtMs},
        {QStringLiteral("safe_provider_status"), receipt.safeProviderStatus},
        {QStringLiteral("safe_error_class"), receipt.safeErrorClass},
        {QStringLiteral("safe_remote_version"), receipt.safeRemoteVersion},
        {QStringLiteral("safe_remote_timestamp_ms"), receipt.safeRemoteTimestampMs
            ? QJsonValue(*receipt.safeRemoteTimestampMs) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("reconciled"), receipt.reconciled}};
}

std::optional<RatingsReviewsDeliveryReceipt> ratingsReviewsDeliveryReceiptFromJson(
    const QJsonObject &object,
    QString *error)
{
    if (object.value(QStringLiteral("version")).toInt() != 1) {
        fail(error, QStringLiteral("Unsupported Ratings/Reviews receipt version."));
        return std::nullopt;
    }
    RatingsReviewsDeliveryReceipt receipt;
    qint64 attempt = 0;
    if (!requiredString(object, "operation_id", &receipt.operationId, error)
        || !requiredString(object, "provider_id", &receipt.providerId, error)
        || !requiredString(object, "operation_type", &receipt.operationType, error)
        || !requiredString(object, "canonical_key", &receipt.canonicalKey, error)
        || !requiredString(object, "canonical_payload_digest", &receipt.canonicalPayloadDigest, error)
        || !requiredString(object, "status", &receipt.status, error)
        || !requiredInteger(object, "attempt_number", &attempt, error)
        || !requiredInteger(object, "created_at_ms", &receipt.createdAtMs, error)
        || !requiredInteger(object, "updated_at_ms", &receipt.updatedAtMs, error)
        || !requiredInteger(object, "completed_at_ms", &receipt.completedAtMs, error)
        || !requiredString(object, "safe_provider_status", &receipt.safeProviderStatus, error)
        || !object.value(QStringLiteral("safe_error_class")).isString()
        || !object.value(QStringLiteral("safe_remote_version")).isString()
        || !object.value(QStringLiteral("reconciled")).isBool()) {
        return std::nullopt;
    }
    receipt.safeErrorClass = object.value(QStringLiteral("safe_error_class")).toString();
    receipt.safeRemoteVersion = object.value(QStringLiteral("safe_remote_version")).toString();
    receipt.reconciled = object.value(QStringLiteral("reconciled")).toBool();
    if (!ratingsReviewsDeliveryIsUuid(receipt.operationId)
        || !ratingsReviewsDeliveryIsOperationType(receipt.operationType)
        || !ratingsReviewsDeliveryIsSha256(receipt.canonicalPayloadDigest)
        || attempt < 1
        || (receipt.status != QLatin1String("succeeded")
            && receipt.status != QLatin1String("failedTerminal")
            && receipt.status != QLatin1String("needsAttention"))
        || !isSafeReceiptString(receipt.safeProviderStatus)
        || !isSafeReceiptString(receipt.safeErrorClass)
        || !isSafeReceiptString(receipt.safeRemoteVersion)) {
        fail(error, QStringLiteral("Invalid or unsafe Ratings/Reviews receipt."));
        return std::nullopt;
    }
    const QJsonValue timestamp = object.value(QStringLiteral("safe_remote_timestamp_ms"));
    if (!timestamp.isNull()) {
        qint64 value = 0;
        if (!timestamp.isDouble()
            || !requiredInteger(object, "safe_remote_timestamp_ms", &value, error)) {
            return std::nullopt;
        }
        receipt.safeRemoteTimestampMs = value;
    }
    receipt.attemptNumber = static_cast<int>(attempt);
    return receipt;
}
