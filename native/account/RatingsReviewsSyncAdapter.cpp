#include "RatingsReviewsSyncAdapter.h"

#include "RatingsReviewsStore.h"

#include <QJsonObject>

namespace {
bool validRatingsReviewsKey(const QString &key) {
    if (key.size() != 68 || !key.startsWith(QLatin1String("rr1:")))
        return false;
    for (qsizetype i = 4; i < key.size(); ++i) {
        const QChar ch = key.at(i);
        if (!((ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
              || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')))) {
            return false;
        }
    }
    return true;
}

bool decodeRecord(
    const QString &recordKey,
    const QJsonValue &payload,
    RatingsReviewsStore::Record *record,
    QString *error) {
    if (!record || !payload.isObject()) {
        if (error)
            *error = QStringLiteral("Ratings/reviews PUT payload must be an object.");
        return false;
    }
    const QJsonObject object = payload.toObject();
    QJsonObject validatedRecords;
    QJsonObject validatedTombstones;
    QString validationError;
    if (!RatingsReviewsStore::mergeCanonicalState(
            QJsonObject{{recordKey, object}},
            QJsonObject(),
            QJsonObject(),
            QJsonObject(),
            &validatedRecords,
            &validatedTombstones,
            &validationError)) {
        if (error)
            *error = validationError;
        return false;
    }

    RatingsReviewsStore::Identity identity;
    identity.world = object.value(QStringLiteral("world")).toString();
    identity.kind = object.value(QStringLiteral("kind")).toString();
    identity.mediaId = object.value(QStringLiteral("media_id")).toString();
    record->identity = identity;
    const QJsonValue rating = object.value(QStringLiteral("rating"));
    if (rating.isNull())
        record->rating.reset();
    else
        record->rating = rating.toDouble();

    const QJsonValue review = object.value(QStringLiteral("review"));
    if (review.isNull())
        record->review.reset();
    else
        record->review = review.toString();

    record->spoiler = object.value(QStringLiteral("spoiler")).toBool();
    record->createdAtMs =
        static_cast<qint64>(object.value(QStringLiteral("created_at_ms")).toDouble());
    record->updatedAtMs =
        static_cast<qint64>(object.value(QStringLiteral("updated_at_ms")).toDouble());
    return true;
}

bool setValidationError(
    SyncAdapterValidationError *error,
    const QString &code,
    const QString &detail) {
    if (error) {
        error->code = code;
        error->detail = detail;
        error->fieldPath.clear();
    }
    return false;
}
}

RatingsReviewsSyncAdapter::RatingsReviewsSyncAdapter(
    RatingsReviewsStore *store,
    QObject *parent)
    : SyncAdapter(parent),
      m_store(store) {
    if (m_store) {
        connect(
            m_store,
            &RatingsReviewsStore::syncDirty,
            this,
            &SyncAdapter::localMutationAvailable);
    }
}

QString RatingsReviewsSyncAdapter::categoryId() const {
    return QStringLiteral("ratings_reviews");
}
int RatingsReviewsSyncAdapter::schemaVersion() const {
    return 1;
}

quint64 RatingsReviewsSyncAdapter::revision() const {
    return m_store ? m_store->revision() : 0;
}

bool RatingsReviewsSyncAdapter::exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot || !m_store) {
        if (error)
            *error = QStringLiteral("Ratings/reviews sync owner is unavailable.");
        return false;
    }
    if (!m_store->healthy(error))
        return false;

    SyncAdapterExport exported;
    exported.revision = m_store->revision();
    const QJsonObject records = m_store->recordsJson();
    for (auto it = records.constBegin(); it != records.constEnd(); ++it) {
        const QJsonObject object = it.value().toObject();
        SyncAdapterRecord record;
        record.recordKey = it.key();
        record.payload = object;
        record.localOrderMs =
            static_cast<qint64>(
                object.value(QStringLiteral("updated_at_ms")).toDouble());
        exported.records.append(record);
    }

    const QJsonObject tombstones = m_store->tombstonesJson();
    for (auto it = tombstones.constBegin(); it != tombstones.constEnd(); ++it) {
        const qint64 deletedAtMs =
            static_cast<qint64>(
                it.value().toObject()
                    .value(QStringLiteral("deleted_at_ms")).toDouble());
        exported.tombstones.append(it.key());
        exported.tombstoneEventMs.insert(it.key(), deletedAtMs);
    }
    *snapshot = exported;
    return true;
}

bool RatingsReviewsSyncAdapter::validateRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    SyncAdapterValidationError *error) const {
    SyncAdapterMutation mutation;
    mutation.categoryId = categoryId();
    mutation.recordKey = recordKey;
    mutation.schemaVersion = schemaVersion;
    mutation.operation = operation;
    mutation.payload = payload;
    return validateRemoteMutation(mutation, error);
}

bool RatingsReviewsSyncAdapter::validateRemoteMutation(
    const SyncAdapterMutation &mutation,
    SyncAdapterValidationError *error) const {
    if (error)
        *error = {};
    if (mutation.categoryId != categoryId() || mutation.schemaVersion != 1) {
        return setValidationError(
            error,
            QStringLiteral("unsupported_schema_version"),
            QStringLiteral("Ratings/reviews sync requires category ratings_reviews schema 1."));
    }
    if (!validRatingsReviewsKey(mutation.recordKey)) {
        return setValidationError(
            error,
            QStringLiteral("invalid_record_key"),
            QStringLiteral("Ratings/reviews sync record key is invalid."));
    }

    if (mutation.operation == SyncWireOperation::Delete) {
        if (!mutation.deletedAtMs.has_value() || *mutation.deletedAtMs <= 0) {
            return setValidationError(
                error,
                QStringLiteral("missing_deleted_at_ms"),
                QStringLiteral("Ratings/reviews DELETE requires a positive semantic delete timestamp."));
        }
        if (!mutation.payload.isUndefined() && !mutation.payload.isNull()) {
            return setValidationError(
                error,
                QStringLiteral("delete_payload_not_empty"),
                QStringLiteral("Ratings/reviews DELETE cannot carry a payload."));
        }
        return true;
    }

    if (mutation.deletedAtMs.has_value()) {
        return setValidationError(
            error,
            QStringLiteral("unexpected_deleted_at_ms"),
            QStringLiteral("Ratings/reviews PUT cannot carry a delete timestamp."));
    }
    RatingsReviewsStore::Record record;
    QString recordError;
    if (!decodeRecord(mutation.recordKey, mutation.payload, &record, &recordError)) {
        return setValidationError(
            error,
            QStringLiteral("payload_invalid"),
            recordError.isEmpty()
                ? QStringLiteral("Ratings/reviews PUT payload is invalid.")
                : recordError);
    }
    return true;
}

bool RatingsReviewsSyncAdapter::applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    QString *error) {
    SyncAdapterMutation mutation;
    mutation.categoryId = categoryId();
    mutation.recordKey = recordKey;
    mutation.schemaVersion = schemaVersion;
    mutation.operation = operation;
    mutation.payload = payload;
    return applyRemoteMutation(mutation, error);
}
bool RatingsReviewsSyncAdapter::applyRemoteMutation(
    const SyncAdapterMutation &mutation,
    QString *error) {
    SyncAdapterValidationError validation;
    if (!validateRemoteMutation(mutation, &validation)) {
        if (error)
            *error = validation.detail;
        return false;
    }
    if (!m_store) {
        if (error)
            *error = QStringLiteral("Ratings/reviews sync owner is unavailable.");
        return false;
    }

    if (mutation.operation == SyncWireOperation::Delete) {
        return m_store->applySyncedDelete(
            mutation.recordKey,
            *mutation.deletedAtMs,
            nullptr,
            error);
    }

    RatingsReviewsStore::Record record;
    if (!decodeRecord(mutation.recordKey, mutation.payload, &record, error))
        return false;
    return m_store->applySyncedPut(
        mutation.recordKey,
        record,
        nullptr,
        error);
}
bool RatingsReviewsSyncAdapter::applyRemoteMutationAsync(
    const SyncAdapterMutation &mutation,
    std::function<void(bool, const QString &)> callback,
    QString *error) {
    QString applyError;
    const bool applied = applyRemoteMutation(mutation, &applyError);
    if (callback)
        callback(applied, applyError);
    if (!applied && error)
        *error = applyError;
    return true;
}
