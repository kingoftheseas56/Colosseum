#include "RatingsReviewsConversionSyncAdapter.h"

#include "ProfilePreferencesStore.h"

#include <QJsonObject>

namespace {
bool failText(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}

bool failValidation(
    SyncAdapterValidationError *error,
    const QString &code,
    const QString &detail,
    const QString &fieldPath = QString()) {
    if (error) {
        error->code = code;
        error->detail = detail;
        error->fieldPath = fieldPath;
    }
    return false;
}
}

RatingsReviewsConversionSyncAdapter::
RatingsReviewsConversionSyncAdapter(
    ProfilePreferencesStore *store,
    QObject *parent)
    : SyncAdapter(parent),
      m_store(store) {
    Q_ASSERT(m_store);
    setObjectName(QStringLiteral("ratingsReviewsConversionSyncAdapter"));
    connect(
        m_store,
        &ProfilePreferencesStore::ratingsReviewsConversionSyncDirty,
        this,
        [this]() { emit localMutationAvailable(revision()); });
}

QString RatingsReviewsConversionSyncAdapter::categoryId() const {
    return QStringLiteral("ratings_reviews_conversion_maps");
}

int RatingsReviewsConversionSyncAdapter::schemaVersion() const {
    return 1;
}

quint64 RatingsReviewsConversionSyncAdapter::revision() const {
    return m_store
        ? static_cast<quint64>(qMax(0, m_store->revision()))
        : 0;
}

bool RatingsReviewsConversionSyncAdapter::remoteApplyEmitsLocalMutation() const {
    return false;
}

QString RatingsReviewsConversionSyncAdapter::recordKeyForProvider(
    const QString &providerId) {
    return QStringLiteral("conversion/") + providerId;
}

bool RatingsReviewsConversionSyncAdapter::providerAllowed(
    const QString &providerId) const {
    if (RatingsReviewsConversionMap::isCanonicalProviderId(providerId))
        return true;
    if (!m_store
        || !m_store->m_conversionTestHook.syntheticDomainsEnabledForTests()) {
        return false;
    }
    return providerId == QStringLiteral("fixture-a")
        || providerId == QStringLiteral("fixture-b");
}

bool RatingsReviewsConversionSyncAdapter::parseRecordKey(
    const QString &recordKey,
    QString *providerId) const {
    const QString prefix = QStringLiteral("conversion/");
    if (!recordKey.startsWith(prefix))
        return false;
    const QString candidate = recordKey.mid(prefix.size());
    if (candidate.isEmpty()
        || candidate.contains(QLatin1Char('/'))
        || !providerAllowed(candidate)) {
        return false;
    }
    if (providerId)
        *providerId = candidate;
    return true;
}

bool RatingsReviewsConversionSyncAdapter::exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot)
        return failText(error, QStringLiteral("A conversion-map sync snapshot is required."));
    if (!m_store)
        return failText(error, QStringLiteral("The conversion-map preference owner is unavailable."));
    QString healthError;
    if (!m_store->ratingsReviewsConversionMapsHealthy(&healthError))
        return failText(error, healthError);

    snapshot->revision = revision();
    snapshot->records.clear();
    snapshot->tombstones.clear();
    snapshot->tombstoneEventMs.clear();
    for (const RatingsReviewsConversionMap &map :
         m_store->ratingsReviewsConversionMaps()) {
        QString validationError;
        if (!providerAllowed(map.providerId)
            || !RatingsReviewsConversionMap::validate(
                map,
                m_store->m_conversionTestHook,
                &validationError)) {
            return failText(
                error,
                validationError.isEmpty()
                    ? QStringLiteral("conversion_map_export_rejected")
                    : validationError);
        }
        snapshot->records.append(
            SyncAdapterRecord{recordKeyForProvider(map.providerId), map.toJson(), -1});
    }
    return true;
}

bool RatingsReviewsConversionSyncAdapter::decodePut(
    const QString &recordKey,
    const QJsonValue &payload,
    int schemaVersionValue,
    RatingsReviewsConversionMap *map,
    SyncAdapterValidationError *validationError,
    QString *error) const {
    if (schemaVersionValue != schemaVersion()) {
        if (validationError)
            return failValidation(validationError,
                QStringLiteral("unsupported_schema_version"),
                QStringLiteral("The conversion-map sync schema is unsupported."));
        return failText(error, QStringLiteral("The conversion-map sync schema is unsupported."));
    }
    QString providerId;
    if (!parseRecordKey(recordKey, &providerId)) {
        if (validationError)
            return failValidation(validationError,
                QStringLiteral("invalid_record_key"),
                QStringLiteral("The conversion-map record key is invalid."));
        return failText(error, QStringLiteral("The conversion-map record key is invalid."));
    }
    if (!payload.isObject()) {
        if (validationError)
            return failValidation(validationError,
                QStringLiteral("payload_invalid"),
                QStringLiteral("A conversion-map PUT requires an object payload."));
        return failText(error, QStringLiteral("A conversion-map PUT requires an object payload."));
    }
    QString mapError;
    const auto decoded = RatingsReviewsConversionMap::fromJson(
        payload.toObject(),
        m_store->m_conversionTestHook,
        &mapError);
    if (!decoded.has_value()) {
        if (validationError)
            return failValidation(validationError,
                QStringLiteral("payload_invalid"),
                mapError,
                QStringLiteral("$.outputs"));
        return failText(error, mapError);
    }
    if (decoded->providerId != providerId) {
        if (validationError)
            return failValidation(validationError,
                QStringLiteral("record_provider_mismatch"),
                QStringLiteral("Record key and provider_id disagree."),
                QStringLiteral("$.provider_id"));
        return failText(error, QStringLiteral("Record key and provider_id disagree."));
    }
    if (map)
        *map = *decoded;
    if (validationError)
        *validationError = {};
    if (error)
        error->clear();
    return true;
}

bool RatingsReviewsConversionSyncAdapter::validateRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    SyncAdapterValidationError *error) const {
    if (!m_store)
        return failValidation(error,
            QStringLiteral("owner_unavailable"),
            QStringLiteral("The conversion-map preference owner is unavailable."));
    if (schemaVersionValue != schemaVersion())
        return failValidation(error,
            QStringLiteral("unsupported_schema_version"),
            QStringLiteral("The conversion-map sync schema is unsupported."));

    QString providerId;
    if (!parseRecordKey(recordKey, &providerId))
        return failValidation(error,
            QStringLiteral("invalid_record_key"),
            QStringLiteral("The conversion-map record key is invalid."));
    if (operation == SyncWireOperation::Delete) {
        if (!payload.isNull() && !payload.isUndefined())
            return failValidation(error,
                QStringLiteral("payload_invalid"),
                QStringLiteral("A conversion-map DELETE must not carry a payload."));
        if (error)
            *error = {};
        return true;
    }
    if (operation != SyncWireOperation::Put)
        return failValidation(error,
            QStringLiteral("operation_invalid"),
            QStringLiteral("The conversion-map operation is unsupported."));

    RatingsReviewsConversionMap map;
    return decodePut(
        recordKey,
        payload,
        schemaVersionValue,
        &map,
        error,
        nullptr);
}

bool RatingsReviewsConversionSyncAdapter::applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    QString *error) {
    if (!m_store)
        return failText(error, QStringLiteral("The conversion-map preference owner is unavailable."));
    SyncAdapterValidationError validation;
    if (!validateRemote(recordKey, operation, payload, schemaVersionValue, &validation))
        return failText(error, validation.detail);

    QString providerId;
    if (!parseRecordKey(recordKey, &providerId))
        return failText(error, QStringLiteral("The conversion-map record key is invalid."));
    if (operation == SyncWireOperation::Delete)
        return m_store->clearSyncedRatingsReviewsConversionMap(providerId);

    RatingsReviewsConversionMap map;
    if (!decodePut(
            recordKey,
            payload,
            schemaVersionValue,
            &map,
            nullptr,
            error)) {
        return false;
    }
    return m_store->applySyncedRatingsReviewsConversionMap(map);
}
