// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncProtocol.h"

#include <QJsonArray>
#include <QUuid>

#include <limits>

namespace {
std::optional<quint64> unsignedInteger(
    const QJsonValue &value) {
    if (value.isString()) {
        bool ok = false;
        const quint64 parsed =
            value.toString().toULongLong(&ok);
        if (ok)
            return parsed;
        return std::nullopt;
    }

    if (value.isDouble()) {
        const double number =
            value.toDouble();
        if (number < 0
            || number
                > static_cast<double>(
                    std::numeric_limits<quint64>::max())) {
            return std::nullopt;
        }
        return static_cast<quint64>(number);
    }

    return std::nullopt;
}

std::optional<qint64> signedInteger(
    const QJsonValue &value) {
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed =
            value.toString().toLongLong(&ok);
        if (ok)
            return parsed;
        return std::nullopt;
    }

    if (value.isDouble()) {
        const double number =
            value.toDouble();
        if (number
                < static_cast<double>(
                    std::numeric_limits<qint64>::min())
            || number
                > static_cast<double>(
                    std::numeric_limits<qint64>::max())) {
            return std::nullopt;
        }
        return static_cast<qint64>(number);
    }

    return std::nullopt;
}

QString normalizedUuid(
    const QString &value) {
    const QUuid parsed(value);
    if (parsed.isNull())
        return QString();

    return parsed.toString(
        QUuid::WithoutBraces)
        .toLower();
}

std::optional<SyncWireHlc>
hlcFromFields(
    const QJsonObject &object,
    const QString &physicalField,
    const QString &counterField,
    const QString &deviceField) {
    const auto physical =
        signedInteger(
            object.value(
                physicalField));
    const auto counter =
        unsignedInteger(
            object.value(
                counterField));
    const QString deviceId =
        normalizedUuid(
            object.value(
                deviceField)
                .toString());

    if (!physical.has_value()
        || *physical < 0
        || !counter.has_value()
        || deviceId.isEmpty()) {
        return std::nullopt;
    }

    SyncWireHlc hlc;
    hlc.physicalMs = *physical;
    hlc.counter = *counter;
    hlc.deviceId = deviceId;
    return hlc;
}
}

int compareSyncWireHlc(
    const SyncWireHlc &left,
    const SyncWireHlc &right) {
    if (left.physicalMs != right.physicalMs)
        return left.physicalMs < right.physicalMs ? -1 : 1;

    if (left.counter != right.counter)
        return left.counter < right.counter ? -1 : 1;

    const int deviceOrder =
        QString::compare(
            left.deviceId,
            right.deviceId,
            Qt::CaseSensitive);
    if (deviceOrder < 0)
        return -1;
    if (deviceOrder > 0)
        return 1;
    return 0;
}

bool syncWireHlcGreater(
    const SyncWireHlc &left,
    const SyncWireHlc &right) {
    return compareSyncWireHlc(
        left,
        right) > 0;
}

QString syncWireOperationName(
    SyncWireOperation operation) {
    return operation == SyncWireOperation::Put
        ? QStringLiteral("put")
        : QStringLiteral("delete");
}

std::optional<SyncWireOperation>
syncWireOperationFromName(
    const QString &name) {
    if (name == QLatin1String("put"))
        return SyncWireOperation::Put;

    if (name == QLatin1String("delete"))
        return SyncWireOperation::Delete;

    return std::nullopt;
}

bool isValidSyncWireRecordKey(
    const QString &recordKey) {
    if (recordKey.isEmpty()
        || recordKey.toUtf8().size() > 512
        || recordKey != recordKey.trimmed()
        || recordKey.startsWith(QLatin1Char('/'))
        || recordKey.startsWith(QLatin1Char('\\'))) {
        return false;
    }

    const QStringList segments =
        recordKey.split(
            QLatin1Char('/'),
            Qt::KeepEmptyParts);
    if (segments.isEmpty())
        return false;

    for (const QString &segment : segments) {
        if (segment.isEmpty()
            || segment == QLatin1String(".")
            || segment == QLatin1String("..")) {
            return false;
        }

        for (const QChar ch : segment) {
            if (ch.unicode() < 0x20
                || ch == QChar(0x7f)
                || ch == QLatin1Char('\\')) {
                return false;
            }
        }
    }

    return true;
}

QJsonObject syncWireMutationToJson(
    const SyncWireMutation &mutation) {
    QJsonObject object;
    object.insert(
        QStringLiteral("mutation_id"),
        mutation.mutationId);
    object.insert(
        QStringLiteral("device_id"),
        mutation.deviceId);
    object.insert(
        QStringLiteral("category"),
        mutation.category);
    object.insert(
        QStringLiteral("record_key"),
        mutation.recordKey);
    object.insert(
        QStringLiteral("schema_version"),
        mutation.schemaVersion);
    object.insert(
        QStringLiteral("hlc_physical_ms"),
        QString::number(
            mutation.hlc.physicalMs));
    object.insert(
        QStringLiteral("hlc_counter"),
        QString::number(
            mutation.hlc.counter));
    object.insert(
        QStringLiteral("operation"),
        syncWireOperationName(
            mutation.operation));

    if (mutation.operation
        == SyncWireOperation::Put) {
        object.insert(
            QStringLiteral("payload"),
            mutation.payload);
    }

    return object;
}

std::optional<SyncWireMutation>
syncWireMutationFromJson(
    const QJsonObject &object) {
    const QString mutationId =
        normalizedUuid(
            object.value(
                QStringLiteral("mutation_id"))
                .toString());
    const QString deviceId =
        normalizedUuid(
            object.value(
                QStringLiteral("device_id"))
                .toString());
    const QString category =
        object.value(
            QStringLiteral("category"))
            .toString();
    const QString recordKey =
        object.value(
            QStringLiteral("record_key"))
            .toString();
    const int schemaVersion =
        object.value(
            QStringLiteral("schema_version"))
            .toInt();
    const auto operation =
        syncWireOperationFromName(
            object.value(
                QStringLiteral("operation"))
                .toString());
    const auto hlc =
        hlcFromFields(
            object,
            QStringLiteral(
                "hlc_physical_ms"),
            QStringLiteral(
                "hlc_counter"),
            QStringLiteral(
                "device_id"));

    if (mutationId.isEmpty()
        || deviceId.isEmpty()
        || category.isEmpty()
        || category
            != category.trimmed().toLower()
        || !isValidSyncWireRecordKey(
            recordKey)
        || schemaVersion <= 0
        || !operation.has_value()
        || !hlc.has_value()
        || hlc->deviceId != deviceId) {
        return std::nullopt;
    }

    SyncWireMutation mutation;
    mutation.mutationId =
        mutationId;
    mutation.deviceId =
        deviceId;
    mutation.category =
        category;
    mutation.recordKey =
        recordKey;
    mutation.schemaVersion =
        schemaVersion;
    mutation.hlc =
        *hlc;
    mutation.operation =
        *operation;

    if (mutation.operation
        == SyncWireOperation::Put) {
        if (!object.contains(
                QStringLiteral("payload"))) {
            return std::nullopt;
        }

        mutation.payload =
            object.value(
                QStringLiteral("payload"));
    } else {
        // Deletes carry no payload. The wire may echo an explicit null (the
        // encoder never writes one, but defensive decoders accept it); the
        // in-memory contract is Undefined so payload-bearing checks fail fast.
        mutation.payload =
            QJsonValue(
                QJsonValue::Undefined);
        if (object.contains(
                QStringLiteral("payload"))
            && !object.value(
                    QStringLiteral("payload"))
                    .isNull()) {
            return std::nullopt;
        }
    }

    return mutation;
}

std::optional<SyncWirePushResult>
syncWirePushResultFromJson(
    const QJsonObject &object) {
    const QString mutationId =
        normalizedUuid(
            object.value(
                QStringLiteral("mutation_id"))
                .toString());
    if (mutationId.isEmpty())
        return std::nullopt;

    SyncWirePushResult result;
    result.mutationId =
        mutationId;
    result.accepted =
        object.value(
            QStringLiteral("accepted"))
            .toBool(false);
    result.code =
        object.value(
            QStringLiteral("code"))
            .toString();
    result.message =
        object.value(
            QStringLiteral("message"))
            .toString();

    if (object.contains(
            QStringLiteral("server_seq"))) {
        const auto serverSeq =
            unsignedInteger(
                object.value(
                    QStringLiteral(
                        "server_seq")));
        if (!serverSeq.has_value())
            return std::nullopt;
        result.serverSeq =
            *serverSeq;
    }

    if (result.accepted
        && result.serverSeq == 0) {
        return std::nullopt;
    }

    result.won =
        object.value(
            QStringLiteral("won"))
            .toBool(false);

    if (object.contains(
            QStringLiteral("current"))) {
        const QJsonValue currentValue =
            object.value(
                QStringLiteral("current"));
        if (!currentValue.isObject())
            return std::nullopt;

        const QJsonObject currentObject =
            currentValue.toObject();

        const QString currentMutationId =
            normalizedUuid(
                currentObject.value(
                    QStringLiteral(
                        "mutation_id"))
                    .toString());
        const auto currentOperation =
            syncWireOperationFromName(
                currentObject.value(
                    QStringLiteral(
                        "operation"))
                    .toString());
        const auto currentHlc =
            hlcFromFields(
                currentObject,
                QStringLiteral(
                    "hlc_physical_ms"),
                QStringLiteral(
                    "hlc_counter"),
                QStringLiteral(
                    "device_id"));
        const auto currentServerSeq =
            unsignedInteger(
                currentObject.value(
                    QStringLiteral(
                        "server_seq")));
        const int currentSchema =
            currentObject.value(
                QStringLiteral(
                    "schema_version"))
                .toInt();

        if (currentMutationId.isEmpty()
            || !currentOperation.has_value()
            || !currentHlc.has_value()
            || !currentServerSeq.has_value()
            || *currentServerSeq == 0
            || currentSchema <= 0) {
            return std::nullopt;
        }

        SyncWireCurrentMetadata current;
        current.mutationId =
            currentMutationId;
        current.deviceId =
            currentHlc->deviceId;
        current.schemaVersion =
            currentSchema;
        current.hlc =
            *currentHlc;
        current.operation =
            *currentOperation;
        current.serverSeq =
            *currentServerSeq;
        result.current =
            current;
    }

    return result;
}

std::optional<SyncWirePullEntry>
syncWirePullEntryFromJson(
    const QJsonObject &object) {
    const auto serverSeq =
        unsignedInteger(
            object.value(
                QStringLiteral(
                    "server_seq")));
    const QJsonValue mutationValue =
        object.value(
            QStringLiteral("mutation"));

    if (!serverSeq.has_value()
        || *serverSeq == 0
        || !mutationValue.isObject()) {
        return std::nullopt;
    }

    const auto mutation =
        syncWireMutationFromJson(
            mutationValue.toObject());
    if (!mutation.has_value())
        return std::nullopt;

    SyncWirePullEntry entry;
    entry.serverSeq =
        *serverSeq;
    entry.won =
        object.value(
            QStringLiteral("won"))
            .toBool(false);
    entry.mutation =
        *mutation;
    return entry;
}


std::optional<SyncWirePushResponse>
syncWirePushResponseFromJson(
    const QJsonObject &object) {
    const auto serverTime =
        signedInteger(
            object.value(
                QStringLiteral(
                    "server_time_ms")));
    const QJsonValue resultsValue =
        object.value(
            QStringLiteral("results"));

    if (!serverTime.has_value()
        || *serverTime < 0
        || !resultsValue.isArray()) {
        return std::nullopt;
    }

    SyncWirePushResponse response;
    response.serverTimeMs =
        *serverTime;

    for (const QJsonValue &value :
         resultsValue.toArray()) {
        if (!value.isObject())
            return std::nullopt;

        const auto result =
            syncWirePushResultFromJson(
                value.toObject());
        if (!result.has_value())
            return std::nullopt;

        response.results.append(
            *result);
    }

    return response;
}

std::optional<SyncWirePullResponse>
syncWirePullResponseFromJson(
    const QJsonObject &object) {
    const auto serverTime =
        signedInteger(
            object.value(
                QStringLiteral(
                    "server_time_ms")));
    const QJsonValue entriesValue =
        object.value(
            QStringLiteral("entries"));

    if (!serverTime.has_value()
        || *serverTime < 0
        || !entriesValue.isArray()) {
        return std::nullopt;
    }

    SyncWirePullResponse response;
    response.serverTimeMs =
        *serverTime;
    response.hasMore =
        object.value(
            QStringLiteral("has_more"))
            .toBool(false);

    quint64 previousSequence = 0;
    for (const QJsonValue &value :
         entriesValue.toArray()) {
        if (!value.isObject())
            return std::nullopt;

        const auto entry =
            syncWirePullEntryFromJson(
                value.toObject());
        if (!entry.has_value()
            || (!response.entries.isEmpty()
                && entry->serverSeq
                    <= previousSequence)) {
            return std::nullopt;
        }

        previousSequence =
            entry->serverSeq;
        response.entries.append(
            *entry);
    }

    return response;
}
