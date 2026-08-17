// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncPayloadFirewall.h"

#include "SyncOwnershipInventory.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {
constexpr int kMaximumPayloadDepth = 64;

const QSet<QString> &forbiddenFields() {
    static const QSet<QString> fields = {
        QStringLiteral("path"),
        QStringLiteral("filepath"),
        QStringLiteral("localpath"),
        QStringLiteral("absolutepath"),
        QStringLiteral("outputpath"),
        QStringLiteral("partpath"),
        QStringLiteral("defaultdownloaddir"),
        QStringLiteral("vaultdir"),
        QStringLiteral("oldpath"),
        QStringLiteral("newpath"),
        QStringLiteral("rootpath"),
        QStringLiteral("mediapath"),
        QStringLiteral("downloadpath"),
        QStringLiteral("sourcepath"),

        QStringLiteral("mediablob"),
        QStringLiteral("fileblob"),
        QStringLiteral("blob"),
        QStringLiteral("filebytes"),
        QStringLiteral("rawbytes"),
        QStringLiteral("contentbytes"),

        QStringLiteral("searchhistory"),
        QStringLiteral("savedstate"),
        QStringLiteral("sessionstate"),
        QStringLiteral("windowstate"),
        QStringLiteral("windowgeometry"),
        QStringLiteral("pipstate"),
        QStringLiteral("caststate"),
        QStringLiteral("roomstate"),

        QStringLiteral("password"),
        QStringLiteral("recoverykey"),
        QStringLiteral("accesstoken"),
        QStringLiteral("refreshtoken"),
        QStringLiteral("authorization"),
        QStringLiteral("cookie"),
        QStringLiteral("cookies"),
        QStringLiteral("apikey"),
        QStringLiteral("clientsecret"),
        QStringLiteral("secret"),
        QStringLiteral("credential"),
        QStringLiteral("credentials"),

        QStringLiteral("transporturl"),
        QStringLiteral("downloadurl"),
        QStringLiteral("streamurl"),
        QStringLiteral("feedurl")
    };
    return fields;
}
}

SyncPayloadValidation SyncPayloadFirewall::validate(
    const QString &categoryId,
    const QJsonValue &payload) {
    const SyncOwnershipEntry *entry =
        SyncOwnershipInventory::find(
            categoryId);
    if (!entry) {
        return reject(
            QStringLiteral("unknown_category"),
            QStringLiteral("$"),
            QStringLiteral(
                "The sync category is not present in the frozen ownership inventory."));
    }

    if (entry->disposition
        == SyncDisposition::LocalOnly) {
        return reject(
            entry->denialCode.isEmpty()
                ? QStringLiteral("category_local_only")
                : entry->denialCode,
            QStringLiteral("$"),
            QStringLiteral(
                "The category is classified local-only and cannot enter ordinary sync."));
    }

    if (entry->disposition
        == SyncDisposition::Secret) {
        return reject(
            entry->denialCode.isEmpty()
                ? QStringLiteral(
                      "secret_requires_protected_channel")
                : entry->denialCode,
            QStringLiteral("$"),
            QStringLiteral(
                "The category contains secret material and cannot enter ordinary sync."));
    }

    if (!entry->ordinaryPayloadEligible) {
        return reject(
            QStringLiteral(
                "category_not_exportable_yet"),
            QStringLiteral("$"),
            QStringLiteral(
                "The category is syncable by product policy, but its portable owner/export seam is not frozen yet."));
    }

    return scan(
        payload,
        QStringLiteral("$"),
        0);
}

bool SyncPayloadFirewall::isForbiddenFieldName(
    const QString &fieldName) {
    return forbiddenFields().contains(
        normalizedFieldName(fieldName));
}

bool SyncPayloadFirewall::isFilesystemPathValue(
    const QString &value) {
    const QString trimmed =
        value.trimmed();
    if (trimmed.isEmpty())
        return false;

    const QString lower =
        trimmed.toLower();

    if (lower.startsWith(
            QStringLiteral("file:/"))
        || lower.startsWith(
            QStringLiteral("qrc:/"))) {
        return true;
    }

    if (trimmed.startsWith(
            QStringLiteral("\\\\"))
        || trimmed.startsWith(
            QStringLiteral("\\\\?\\"))) {
        return true;
    }

    if (trimmed.startsWith(
            QStringLiteral("../"))
        || trimmed.startsWith(
            QStringLiteral("./"))
        || trimmed.startsWith(
            QStringLiteral("..\\"))
        || trimmed.startsWith(
            QStringLiteral(".\\"))) {
        return true;
    }

    static const QRegularExpression windowsDrive(
        QStringLiteral(
            "^[A-Za-z]:[\\\\/].+"));
    if (windowsDrive.match(trimmed).hasMatch())
        return true;

    if (trimmed.startsWith(QLatin1Char('/')))
        return true;

    return false;
}

SyncPayloadValidation SyncPayloadFirewall::scan(
    const QJsonValue &value,
    const QString &fieldPath,
    int depth) {
    if (depth > kMaximumPayloadDepth) {
        return reject(
            QStringLiteral("payload_too_deep"),
            fieldPath,
            QStringLiteral(
                "The ordinary sync payload exceeds the maximum validation depth."));
    }

    if (value.isObject()) {
        const QJsonObject object =
            value.toObject();

        for (auto it = object.constBegin();
             it != object.constEnd();
             ++it) {
            const QString childPath =
                fieldPath
                + QLatin1Char('.')
                + it.key();

            if (isForbiddenFieldName(
                    it.key())) {
                return reject(
                    QStringLiteral(
                        "forbidden_field"),
                    childPath,
                    QStringLiteral(
                        "The payload contains a field that is always local-only or secret."));
            }

            const SyncPayloadValidation child =
                scan(
                    it.value(),
                    childPath,
                    depth + 1);
            if (!child.allowed)
                return child;
        }

        return allow();
    }

    if (value.isArray()) {
        const QJsonArray array =
            value.toArray();
        for (qsizetype index = 0;
             index < array.size();
             ++index) {
            const QString childPath =
                fieldPath
                + QLatin1Char('[')
                + QString::number(index)
                + QLatin1Char(']');

            const SyncPayloadValidation child =
                scan(
                    array.at(index),
                    childPath,
                    depth + 1);
            if (!child.allowed)
                return child;
        }

        return allow();
    }

    if (value.isString()
        && isFilesystemPathValue(
            value.toString())) {
        return reject(
            QStringLiteral(
                "filesystem_path_value"),
            fieldPath,
            QStringLiteral(
                "Filesystem/resource paths cannot enter ordinary sync payloads."));
    }

    return allow();
}

QString SyncPayloadFirewall::normalizedFieldName(
    const QString &fieldName) {
    QString normalized;
    normalized.reserve(
        fieldName.size());

    for (const QChar ch : fieldName) {
        if (ch.isLetterOrNumber())
            normalized.append(ch.toLower());
    }

    return normalized;
}

SyncPayloadValidation SyncPayloadFirewall::allow() {
    SyncPayloadValidation result;
    result.allowed = true;
    return result;
}

SyncPayloadValidation SyncPayloadFirewall::reject(
    const QString &code,
    const QString &fieldPath,
    const QString &detail) {
    SyncPayloadValidation result;
    result.allowed = false;
    result.code = code;
    result.fieldPath = fieldPath;
    result.detail = detail;
    return result;
}
