// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "CoreStateSyncProjection.h"

#include "SyncPayloadFirewall.h"
#include "SyncProtocol.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>

#include <optional>

namespace {
std::optional<QJsonValue> sanitizePortableValue(
    const QJsonValue &value) {
    if (value.isObject()) {
        QJsonObject output;
        const QJsonObject object =
            value.toObject();

        for (auto it = object.constBegin();
             it != object.constEnd();
             ++it) {
            if (SyncPayloadFirewall::
                    isForbiddenFieldName(
                        it.key())) {
                continue;
            }

            const auto child =
                sanitizePortableValue(
                    it.value());
            if (child.has_value()) {
                output.insert(
                    it.key(),
                    *child);
            }
        }

        return output;
    }

    if (value.isArray()) {
        QJsonArray output;
        const QJsonArray array =
            value.toArray();

        for (const QJsonValue &item :
             array) {
            const auto child =
                sanitizePortableValue(
                    item);
            if (!child.has_value()) {
                // Arrays have positional/ordered semantics. Dropping only one
                // machine-local element would silently rewrite the array.
                // Treat the complete array field as local-only instead.
                return std::nullopt;
            }

            output.append(*child);
        }

        return output;
    }

    if (value.isString()
        && SyncPayloadFirewall::
               isFilesystemPathValue(
                   value.toString())) {
        return std::nullopt;
    }

    return value;
}

bool containsLocalOnlyMaterial(
    const QJsonValue &value,
    const QString &fieldName = QString()) {
    if (!fieldName.isEmpty()
        && SyncPayloadFirewall::
               isForbiddenFieldName(
                   fieldName)) {
        return true;
    }

    if (value.isString()) {
        return SyncPayloadFirewall::
            isFilesystemPathValue(
                value.toString());
    }

    if (value.isArray()) {
        for (const QJsonValue &item :
             value.toArray()) {
            if (containsLocalOnlyMaterial(
                    item)) {
                return true;
            }
        }
        return false;
    }

    if (value.isObject()) {
        const QJsonObject object =
            value.toObject();

        for (auto it = object.constBegin();
             it != object.constEnd();
             ++it) {
            if (containsLocalOnlyMaterial(
                    it.value(),
                    it.key())) {
                return true;
            }
        }
    }

    return false;
}

QJsonValue mergePortableValue(
    const QJsonValue &existingLocal,
    const QJsonValue &portableRemote) {
    if (existingLocal.isObject()
        && portableRemote.isObject()) {
        const QJsonObject existing =
            existingLocal.toObject();
        QJsonObject merged =
            portableRemote.toObject();

        for (auto it = existing.constBegin();
             it != existing.constEnd();
             ++it) {
            if (SyncPayloadFirewall::
                    isForbiddenFieldName(
                        it.key())) {
                merged.insert(
                    it.key(),
                    it.value());
                continue;
            }

            const auto remoteIt =
                merged.constFind(
                    it.key());

            if (remoteIt != merged.constEnd()) {
                const QJsonValue remoteValue =
                    *remoteIt;

                if (it.value().isObject()
                    && remoteValue.isObject()) {
                    merged.insert(
                        it.key(),
                        mergePortableValue(
                            it.value(),
                            remoteValue));
                    continue;
                }

                if (it.value().isArray()
                    && remoteValue.isArray()
                    && containsLocalOnlyMaterial(
                        it.value())) {
                    // The exported form omits an array entirely when any
                    // element is local-only, so this branch is defensive.
                    merged.insert(
                        it.key(),
                        it.value());
                }
                continue;
            }

            if (containsLocalOnlyMaterial(
                    it.value(),
                    it.key())) {
                merged.insert(
                    it.key(),
                    it.value());
            }
        }

        return merged;
    }

    if (existingLocal.isArray()
        && portableRemote.isArray()
        && containsLocalOnlyMaterial(
            existingLocal)) {
        return existingLocal;
    }

    return portableRemote;
}

QString encodeComponent(
    const QString &value) {
    return QString::fromLatin1(
        value.toUtf8()
            .toBase64(
                QByteArray::
                    Base64UrlEncoding
                | QByteArray::
                    OmitTrailingEquals));
}

std::optional<QString> decodeComponent(
    const QString &encoded) {
    if (encoded.isEmpty())
        return std::nullopt;

    const QByteArray bytes =
        QByteArray::fromBase64(
            encoded.toLatin1(),
            QByteArray::Base64UrlEncoding);

    const QString decoded =
        QString::fromUtf8(bytes);
    if (decoded.isEmpty()
        || encodeComponent(decoded)
            != encoded) {
        return std::nullopt;
    }

    return decoded;
}

QString recordKey(
    const QString &prefix,
    const QString &left,
    const QString &right) {
    return prefix
        + QLatin1Char('/')
        + encodeComponent(left)
        + QLatin1Char('/')
        + encodeComponent(right);
}

bool decodeKey(
    const QString &recordKeyValue,
    const QString &prefix,
    QString *left,
    QString *right) {
    const QStringList parts =
        recordKeyValue.split(
            QLatin1Char('/'),
            Qt::KeepEmptyParts);

    if (parts.size() != 3
        || parts.at(0) != prefix) {
        return false;
    }

    const auto decodedLeft =
        decodeComponent(parts.at(1));
    const auto decodedRight =
        decodeComponent(parts.at(2));

    if (!decodedLeft.has_value()
        || !decodedRight.has_value()) {
        return false;
    }

    if (left)
        *left = *decodedLeft;
    if (right)
        *right = *decodedRight;
    return true;
}

CoreStateSyncProjection project(
    const QString &category,
    const QString &prefix,
    const QVariantMap &entry,
    const QString &leftField,
    const QString &rightField,
    const QString &orderField) {
    CoreStateSyncProjection result;

    const QString left =
        entry.value(leftField)
            .toString();
    const QString right =
        entry.value(rightField)
            .toString();

    if (left.isEmpty()
        || right.isEmpty()) {
        result.disposition =
            CoreStateSyncProjection::
                Disposition::Invalid;
        result.error =
            QStringLiteral(
                "The domain owner contains a record without its required logical identity.");
        return result;
    }

    if (SyncPayloadFirewall::
            isFilesystemPathValue(left)
        || SyncPayloadFirewall::
               isFilesystemPathValue(
                   right)) {
        result.disposition =
            CoreStateSyncProjection::
                Disposition::LocalOnly;
        return result;
    }

    const QJsonValue raw =
        QJsonValue::fromVariant(
            entry);
    const auto sanitized =
        sanitizePortableValue(raw);

    if (!sanitized.has_value()
        || !sanitized->isObject()) {
        result.disposition =
            CoreStateSyncProjection::
                Disposition::Invalid;
        result.error =
            QStringLiteral(
                "The domain owner record could not be projected into ordinary sync.");
        return result;
    }

    QJsonObject payload =
        sanitized->toObject();
    payload.insert(
        leftField,
        left);
    payload.insert(
        rightField,
        right);

    const SyncPayloadValidation validation =
        SyncPayloadFirewall::validate(
            category,
            payload);
    if (!validation.allowed) {
        result.disposition =
            CoreStateSyncProjection::
                Disposition::Invalid;
        result.error =
            validation.detail.isEmpty()
            ? validation.code
            : validation.detail;
        return result;
    }

    const QString key =
        recordKey(
            prefix,
            left,
            right);
    if (!isValidSyncWireRecordKey(key)) {
        result.disposition =
            CoreStateSyncProjection::
                Disposition::LocalOnly;
        return result;
    }

    result.disposition =
        CoreStateSyncProjection::
            Disposition::Portable;
    result.recordKey =
        key;
    result.payload =
        payload;

    bool orderOk = false;
    const qint64 order =
        entry.value(orderField)
            .toLongLong(&orderOk);
    if (orderOk && order > 0)
        result.localOrderMs = order;

    return result;
}
}

CoreStateSyncProjection
CoreStateSyncProjection::collection(
    const QVariantMap &entry) {
    return project(
        QStringLiteral("collection"),
        QStringLiteral("collection"),
        entry,
        QStringLiteral("world"),
        QStringLiteral("id"),
        QStringLiteral("addedAt"));
}

CoreStateSyncProjection
CoreStateSyncProjection::progress(
    const QVariantMap &entry) {
    return project(
        QStringLiteral(
            "continue_progress"),
        QStringLiteral("progress"),
        entry,
        QStringLiteral("kind"),
        QStringLiteral("id"),
        QStringLiteral("updatedAt"));
}

CoreStateSyncProjection
CoreStateSyncProjection::history(
    const QVariantMap &entry) {
    return project(
        QStringLiteral("full_history"),
        QStringLiteral("history"),
        entry,
        QStringLiteral("kind"),
        QStringLiteral("id"),
        QStringLiteral("lastActivityAt"));
}

bool CoreStateSyncProjection::
decodeCollectionKey(
    const QString &recordKeyValue,
    QString *world,
    QString *id) {
    return decodeKey(
        recordKeyValue,
        QStringLiteral("collection"),
        world,
        id);
}

bool CoreStateSyncProjection::
decodeProgressKey(
    const QString &recordKeyValue,
    QString *kind,
    QString *id) {
    return decodeKey(
        recordKeyValue,
        QStringLiteral("progress"),
        kind,
        id);
}

bool CoreStateSyncProjection::
decodeHistoryKey(
    const QString &recordKeyValue,
    QString *kind,
    QString *id) {
    return decodeKey(
        recordKeyValue,
        QStringLiteral("history"),
        kind,
        id);
}

QVariantMap CoreStateSyncProjection::
mergePortableIntoLocal(
    const QVariantMap &existingLocal,
    const QJsonObject &portableRemote) {
    const QJsonValue merged =
        mergePortableValue(
            QJsonValue::fromVariant(
                existingLocal),
            portableRemote);

    if (!merged.isObject())
        return portableRemote.toVariantMap();

    return merged
        .toObject()
        .toVariantMap();
}
