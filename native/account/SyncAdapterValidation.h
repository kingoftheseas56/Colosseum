#pragma once

#include "ActivityProjector.h"
#include "CoreStateSyncProjection.h"
#include "SyncAdapter.h"
#include "SyncPayloadFirewall.h"

#include <QJsonObject>
#include <QMetaType>
#include <QSet>
#include <QUuid>
#include <QVariant>

#include <cmath>
#include <limits>

// Side-effect-free validation shared by the registry and the shipping
// adapters. A failed preflight is typed compatibility evidence; owner I/O is
// still performed only by applyRemote() and remains a hard failure.
namespace SyncAdapterValidation {

inline bool fail(
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

inline bool decodeActivityKey(
    const QString &recordKey,
    QString *eventId,
    SyncAdapterValidationError *error) {
    const QString prefix = QStringLiteral("activity/");
    if (!isValidSyncWireRecordKey(recordKey)
        || !recordKey.startsWith(prefix)) {
        return fail(
            error,
            QStringLiteral("invalid_record_key"),
            QStringLiteral("The Activity sync record key is invalid."));
    }
    const QString suffix = recordKey.mid(prefix.size());
    const QUuid parsed(suffix);
    if (parsed.isNull()
        || parsed.toString(QUuid::WithoutBraces).toLower() != suffix) {
        return fail(
            error,
            QStringLiteral("invalid_record_key"),
            QStringLiteral("The Activity sync record key is invalid."));
    }
    if (eventId)
        *eventId = suffix;
    return true;
}

inline bool integer(
    const QJsonObject &object,
    const QString &field,
    qint64 *value,
    bool positive,
    SyncAdapterValidationError *error) {
    const QJsonValue raw = object.value(field);
    if (!raw.isDouble()) {
        return fail(
            error,
            QStringLiteral("payload_invalid"),
            QStringLiteral("The sync field must be an integer."),
            field);
    }

    const QVariant variant = raw.toVariant();
    qint64 parsed = 0;
    switch (variant.metaType().id()) {
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
        parsed = variant.toLongLong();
        break;
    case QMetaType::ULongLong: {
        const qulonglong unsignedValue = variant.toULongLong();
        if (unsignedValue > static_cast<qulonglong>(std::numeric_limits<qint64>::max())) {
            return fail(error, QStringLiteral("payload_invalid"),
                        QStringLiteral("The sync field must be an integer."), field);
        }
        parsed = static_cast<qint64>(unsignedValue);
        break;
    }
    case QMetaType::Double: {
        const double number = variant.toDouble();
        if (!std::isfinite(number) || std::trunc(number) != number) {
            return fail(error, QStringLiteral("payload_invalid"),
                        QStringLiteral("The sync field must be an integer."), field);
        }
        constexpr qint64 low = std::numeric_limits<qint64>::min();
        constexpr qint64 high = std::numeric_limits<qint64>::max();
        const qint64 lowResult = raw.toInteger(low);
        const qint64 highResult = raw.toInteger(high);
        if (lowResult != highResult) {
            return fail(error, QStringLiteral("payload_invalid"),
                        QStringLiteral("The sync field must be an integer."), field);
        }
        parsed = lowResult;
        break;
    }
    default:
        return fail(error, QStringLiteral("payload_invalid"),
                    QStringLiteral("The sync field must be an integer."), field);
    }
    if (positive && parsed <= 0) {
        return fail(
            error,
            QStringLiteral("payload_invalid"),
            QStringLiteral("The sync timestamp must be positive."),
            field);
    }
    if (value)
        *value = parsed;
    return true;
}

inline bool core(
    const QString &category,
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    SyncAdapterValidationError *error) {
    if (schemaVersion != 1) {
        return fail(
            error,
            QStringLiteral("unsupported_schema_version"),
            QStringLiteral("The incoming schema version is unsupported."));
    }

    QString left;
    QString right;
    bool (*decode)(const QString &, QString *, QString *) = nullptr;
    if (category == QLatin1String("collection"))
        decode = &CoreStateSyncProjection::decodeCollectionKey;
    else if (category == QLatin1String("continue_progress"))
        decode = &CoreStateSyncProjection::decodeProgressKey;
    else if (category == QLatin1String("full_history"))
        decode = &CoreStateSyncProjection::decodeHistoryKey;
    if (!decode
        || !decode(recordKey, &left, &right)) {
        return fail(
            error,
            QStringLiteral("invalid_record_key"),
            QStringLiteral("The sync record key is invalid."));
    }

    if (operation == SyncWireOperation::Delete)
        return true;
    if (operation != SyncWireOperation::Put || !payload.isObject()) {
        return fail(
            error,
            QStringLiteral("payload_invalid"),
            QStringLiteral("A sync PUT requires an object payload."));
    }

    const QJsonObject object = payload.toObject();
    CoreStateSyncProjection projected;
    if (category == QLatin1String("collection"))
        projected = CoreStateSyncProjection::collection(object.toVariantMap());
    else if (category == QLatin1String("continue_progress"))
        projected = CoreStateSyncProjection::progress(object.toVariantMap());
    else
        projected = CoreStateSyncProjection::history(object.toVariantMap());
    if (projected.disposition != CoreStateSyncProjection::Disposition::Portable
        || projected.recordKey != recordKey
        || projected.payload != object) {
        return fail(
            error,
            QStringLiteral("payload_invalid"),
            QStringLiteral("The sync payload cannot be materialized by its owner."));
    }

    if (category == QLatin1String("full_history")) {
        qint64 first = 0;
        qint64 last = 0;
        if (!integer(object, QStringLiteral("firstActivityAt"), &first, true, error)
            || !integer(object, QStringLiteral("lastActivityAt"), &last, true, error)
            || last < first) {
            return fail(
                error,
                QStringLiteral("payload_invalid"),
                QStringLiteral("The history timestamps are invalid."));
        }
        if (object.contains(QStringLiteral("completedAt"))) {
            qint64 completed = 0;
            if (!integer(object, QStringLiteral("completedAt"), &completed, true, error)
                || completed < first
                || completed > last) {
                return fail(
                    error,
                    QStringLiteral("payload_invalid"),
                    QStringLiteral("The completed history timestamp is invalid."));
            }
        }
    }
    return true;
}

inline bool activity(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    SyncAdapterValidationError *error) {
    if (schemaVersion != 1)
        return fail(error, QStringLiteral("unsupported_schema_version"),
                    QStringLiteral("The Activity sync schema is unsupported."));
    QString eventId;
    if (!decodeActivityKey(recordKey, &eventId, error))
        return false;
    if (operation != SyncWireOperation::Put || !payload.isObject())
        return fail(error, QStringLiteral("payload_invalid"),
                    QStringLiteral("Activity facts accept PUT object payloads only."));
    const QJsonObject object = payload.toObject();
    const QJsonValue identity = object.value(QStringLiteral("eventId"));
    const QUuid parsed(identity.toString());
    if (!identity.isString()
        || parsed.isNull()
        || parsed.toString(QUuid::WithoutBraces).toLower() != eventId) {
        return fail(error, QStringLiteral("record_identity_mismatch"),
                    QStringLiteral("The Activity payload identity does not match its key."));
    }
    const QJsonValue syncable = object.value(QStringLiteral("syncable"));
    if (!syncable.isBool() || !syncable.toBool()) {
        return fail(error, QStringLiteral("activity_not_syncable"),
                    QStringLiteral("Activity facts must be syncable."),
                    QStringLiteral("syncable"));
    }

    // ActivityStore persists these portable columns as NOT NULL. The local
    // exporter always emits them (empty strings are valid), so an admitted
    // remote fact must carry the same materializable shape.
    for (const QString &field : {
             QStringLiteral("itemLabel"),
             QStringLiteral("cover"),
             QStringLiteral("source")}) {
        const QJsonValue value = object.value(field);
        if (!value.isString()) {
            return fail(error, QStringLiteral("payload_invalid"),
                        QStringLiteral("The Activity fact is missing an owner field."),
                        field);
        }
    }

    static const QSet<QString> commonFields{
        QStringLiteral("v"), QStringLiteral("type"), QStringLiteral("eventId"),
        QStringLiteral("sessionId"), QStringLiteral("world"), QStringLiteral("kind"),
        QStringLiteral("titleKey"), QStringLiteral("itemKey"), QStringLiteral("title"),
        QStringLiteral("itemLabel"), QStringLiteral("cover"),
        QStringLiteral("utcOffsetMinutes"), QStringLiteral("syncable"),
        QStringLiteral("source")};
    static const QSet<QString> playbackFields{
        QStringLiteral("startAtMs"), QStringLiteral("endAtMs"),
        QStringLiteral("activeMs"), QStringLiteral("rateMilli")};
    static const QSet<QString> readingFields{
        QStringLiteral("atMs"), QStringLiteral("readingForm"),
        QStringLiteral("pageKeys"), QStringLiteral("progressMicros")};
    static const QSet<QString> completionFields{
        QStringLiteral("atMs"), QStringLiteral("reason")};

    try {
        ActivityProjector::validateEvent(object);
    } catch (const ActivityProjector::ValidationError &validationError) {
        return fail(error, QStringLiteral("payload_invalid"),
                    QString::fromUtf8(validationError.what()));
    }

    const QString type = object.value(QStringLiteral("type")).toString();
    const QSet<QString> *typeFields = nullptr;
    if (type == QLatin1String("playback_delta"))
        typeFields = &playbackFields;
    else if (type == QLatin1String("reading_delta"))
        typeFields = &readingFields;
    else if (type == QLatin1String("media_completed"))
        typeFields = &completionFields;
    if (!typeFields) {
        return fail(error, QStringLiteral("payload_invalid"),
                    QStringLiteral("The Activity fact type is not accepted."),
                    QStringLiteral("type"));
    }
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!commonFields.contains(it.key()) && !typeFields->contains(it.key())) {
            return fail(error, QStringLiteral("payload_invalid"),
                        QStringLiteral("The Activity fact contains an unsupported field."),
                        it.key());
        }
    }
    return true;
}

inline bool downloadIntent(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    SyncAdapterValidationError *error) {
    if (schemaVersion != 1)
        return fail(error, QStringLiteral("unsupported_schema_version"),
                    QStringLiteral("The download intent sync schema is unsupported."));
    const QStringList parts = recordKey.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (parts.size() != 2 || parts.at(0).isEmpty() || parts.at(1).isEmpty()
        || parts.at(0).contains(QLatin1Char('\\'))
        || parts.at(1).contains(QLatin1Char('\\'))) {
        return fail(error, QStringLiteral("invalid_record_key"),
                    QStringLiteral("The download intent record key is invalid."));
    }
    if (operation == SyncWireOperation::Delete)
        return true;
    if (operation != SyncWireOperation::Put || !payload.isObject())
        return fail(error, QStringLiteral("payload_invalid"),
                    QStringLiteral("A download intent PUT requires an object payload."));
    const QJsonObject object = payload.toObject();
    const QSet<QString> allowed{
        QStringLiteral("id"), QStringLiteral("world"), QStringLiteral("kind"),
        QStringLiteral("title"), QStringLiteral("subtitle"), QStringLiteral("seriesTitle"),
        QStringLiteral("season"), QStringLiteral("episode"), QStringLiteral("seriesId"),
        QStringLiteral("label"), QStringLiteral("author")};
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key()))
            return fail(error, QStringLiteral("payload_invalid"),
                        QStringLiteral("The download intent contains an unsupported field."), it.key());
    }
    for (const QString &field : {QStringLiteral("world"), QStringLiteral("id"), QStringLiteral("kind")}) {
        if (!object.value(field).isString() || object.value(field).toString().isEmpty())
            return fail(error, QStringLiteral("record_identity_mismatch"),
                        QStringLiteral("The download intent identity is incomplete."), field);
    }
    if (object.value(QStringLiteral("world")).toString() != parts.at(0)
        || object.value(QStringLiteral("id")).toString() != parts.at(1))
        return fail(error, QStringLiteral("record_identity_mismatch"),
                    QStringLiteral("The download intent identity does not match its key."));
    return true;
}

inline bool explicitPreference(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersion,
    SyncAdapterValidationError *error) {
    if (schemaVersion != 1)
        return fail(error, QStringLiteral("unsupported_schema_version"),
                    QStringLiteral("The preference sync schema is unsupported."));
    if (recordKey != QLatin1String("preferences/explicit-content"))
        return fail(error, QStringLiteral("invalid_record_key"),
                    QStringLiteral("The explicit-content preference key is invalid."));
    if (operation == SyncWireOperation::Delete)
        return true;
    if (operation != SyncWireOperation::Put || !payload.isObject())
        return fail(error, QStringLiteral("payload_invalid"),
                    QStringLiteral("The preference PUT requires an object payload."));
    const QJsonObject object = payload.toObject();
    if (object.size() != 1 || !object.value(QStringLiteral("showExplicit")).isBool())
        return fail(error, QStringLiteral("payload_invalid"),
                    QStringLiteral("The explicit-content preference payload is malformed."));
    return true;
}

} // namespace SyncAdapterValidation
