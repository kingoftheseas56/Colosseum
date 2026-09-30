#include "VaultAndroidStorageBridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {

qint64 jsonInt64(const QJsonValue& value, qint64 fallback = 0)
{
    if (value.isDouble())
        return static_cast<qint64>(value.toDouble());
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        if (ok)
            return parsed;
    }
    return fallback;
}

} // namespace

VaultAndroidStorageBridge::DecodeResult
VaultAndroidStorageBridge::decodeSnapshotJson(const QByteArray& payload)
{
    DecodeResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("Invalid Android Vault snapshot JSON");
        return result;
    }

    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("ok")).toBool(false)) {
        result.error = root.value(QStringLiteral("error")).toString(
            QStringLiteral("Android storage snapshot failed"));
        return result;
    }

    const QJsonValue sourcesValue = root.value(QStringLiteral("sources"));
    if (!sourcesValue.isArray()) {
        result.error = QStringLiteral("Android storage snapshot has no sources array");
        return result;
    }

    for (const QJsonValue& sourceValue : sourcesValue.toArray()) {
        if (!sourceValue.isObject())
            continue;
        const QJsonObject sourceObject = sourceValue.toObject();
        VaultAndroidIndexAdapter::SourceSnapshot source;
        source.rootUri = sourceObject.value(QStringLiteral("rootUri")).toString().trimmed();
        if (source.rootUri.isEmpty())
            continue;
        source.available = sourceObject.value(QStringLiteral("available")).toBool(true);

        const QJsonArray entries = sourceObject.value(QStringLiteral("entries")).toArray();
        for (const QJsonValue& entryValue : entries) {
            if (!entryValue.isObject())
                continue;
            const QJsonObject object = entryValue.toObject();
            VaultAndroidIndexAdapter::MediaEntry entry;
            entry.uri = object.value(QStringLiteral("uri")).toString().trimmed();
            entry.displayName = object.value(QStringLiteral("displayName")).toString().trimmed();
            entry.relativePath = object.value(QStringLiteral("relativePath")).toString();
            entry.mimeType = object.value(QStringLiteral("mimeType")).toString();
            entry.sizeBytes = jsonInt64(object.value(QStringLiteral("sizeBytes")));
            entry.modifiedMs = jsonInt64(object.value(QStringLiteral("modifiedMs")));
            entry.durationMs = jsonInt64(object.value(QStringLiteral("durationMs")), -1);
            if (!entry.uri.isEmpty() && !entry.displayName.isEmpty())
                source.entries.append(entry);
        }
        result.sources.append(source);
    }

    result.ok = true;
    return result;
}
