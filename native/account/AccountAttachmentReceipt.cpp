// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountAttachmentReceipt.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>

namespace {
constexpr int kReceiptVersion = 1;
}

QString AccountAttachmentReceipt::sourceKindLegacyLocal() {
    return QStringLiteral("legacy_local");
}

QString AccountAttachmentReceipt::sourceKindLocalOnly() {
    return QStringLiteral("local_only");
}

bool AccountAttachmentReceipt::save(const ProfilePaths &paths,
                                    const AccountAttachmentReceiptData &data,
                                    QString *error) {
    const QString path = paths.cloudAttachmentReceiptPath();
    if (path.isEmpty())
        return setError(error,
                        QStringLiteral("The cloud attachment receipt requires an account profile."));

    QString validationError;
    if (!isValidData(data, &validationError))
        return setError(error, validationError);

    return writeAtomic(path, data, error);
}

AccountAttachmentReceipt::ReadResult AccountAttachmentReceipt::read(const ProfilePaths &paths) {
    ReadResult result;

    const QString path = paths.cloudAttachmentReceiptPath();
    if (path.isEmpty()) {
        result.status = ReadStatus::Invalid;
        result.error = QStringLiteral("The cloud attachment receipt requires an account profile.");
        return result;
    }

    const QFileInfo info(path);
    if (!info.exists()) {
        // Absence is not an error: no cloud attachment is pending.
        result.status = ReadStatus::Missing;
        return result;
    }

    if (!info.isFile()) {
        result.status = ReadStatus::Invalid;
        result.error = QStringLiteral("The cloud attachment receipt path is not a file.");
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.status = ReadStatus::Invalid;
        result.error = QStringLiteral("Could not open the cloud attachment receipt.");
        return result;
    }

    return parse(file.readAll());
}

bool AccountAttachmentReceipt::markSourceRetired(const ProfilePaths &paths,
                                                 QString *error) {
    const ReadResult existing = read(paths);
    if (existing.status != ReadStatus::Ok) {
        const QString reason = existing.error.isEmpty()
            ? QStringLiteral("No cloud attachment receipt is pending.")
            : existing.error;
        return setError(error, reason);
    }

    AccountAttachmentReceiptData updated = existing.data;
    updated.sourceRetired = true;
    return writeAtomic(paths.cloudAttachmentReceiptPath(), updated, error);
}

bool AccountAttachmentReceipt::clear(const ProfilePaths &paths,
                                     QString *error) {
    const QString path = paths.cloudAttachmentReceiptPath();
    if (path.isEmpty())
        return setError(error,
                        QStringLiteral("The cloud attachment receipt requires an account profile."));

    if (!QFileInfo::exists(path))
        return true;

    if (!QFile::remove(path))
        return setError(error, QStringLiteral("Could not remove the cloud attachment receipt."));
    return true;
}

bool AccountAttachmentReceipt::isValidData(const AccountAttachmentReceiptData &data,
                                           QString *error) {
    const QString validationError = validate(data);
    if (!validationError.isEmpty())
        return setError(error, validationError);
    return true;
}

QString AccountAttachmentReceipt::validate(const AccountAttachmentReceiptData &data) {
    if (data.version != kReceiptVersion)
        return QStringLiteral("The cloud attachment receipt version must be 1.");

    // Exact canonical lowercase form: QUuid::toString(WithoutBraces) is
    // lowercase, so any uppercase, braced, padded, or nil identity is rejected.
    const QUuid parsed(data.attachmentId);
    if (parsed.isNull()
        || parsed.toString(QUuid::WithoutBraces) != data.attachmentId) {
        return QStringLiteral("The cloud attachment identity must be a lowercase UUID.");
    }

    if (data.sourceKind != sourceKindLegacyLocal()
        && data.sourceKind != sourceKindLocalOnly()) {
        return QStringLiteral("The cloud attachment source kind is invalid.");
    }

    if (data.sourceProfileId.trimmed().isEmpty())
        return QStringLiteral("The cloud attachment source profile is required.");

    if (data.sourceSemanticDigest.trimmed().isEmpty())
        return QStringLiteral("The cloud attachment source semantic digest is required.");

    // sourceActivityDigest has no constraint here: the empty string is the
    // valid "source had no durable Activity ledger" sentinel.

    return QString();
}

QByteArray AccountAttachmentReceipt::serialize(const AccountAttachmentReceiptData &data) {
    QJsonObject object;
    object.insert(QStringLiteral("version"), kReceiptVersion);
    object.insert(QStringLiteral("attachment_id"), data.attachmentId);
    object.insert(QStringLiteral("source_kind"), data.sourceKind);
    object.insert(QStringLiteral("source_profile_id"), data.sourceProfileId);
    object.insert(QStringLiteral("source_semantic_digest"), data.sourceSemanticDigest);
    object.insert(QStringLiteral("source_activity_digest"), data.sourceActivityDigest);
    object.insert(QStringLiteral("source_retired"), data.sourceRetired);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool AccountAttachmentReceipt::writeAtomic(const QString &path,
                                           const AccountAttachmentReceiptData &data,
                                           QString *error) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return setError(error,
                        QStringLiteral("Could not create the cloud attachment receipt directory."));

    const QByteArray payload = serialize(data);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error,
                        QStringLiteral("Could not open the cloud attachment receipt for writing."));
    if (file.write(payload) != payload.size())
        return setError(error, QStringLiteral("Could not write the cloud attachment receipt."));
    if (!file.commit())
        return setError(error, QStringLiteral("Could not commit the cloud attachment receipt."));
    return true;
}

AccountAttachmentReceipt::ReadResult AccountAttachmentReceipt::parse(const QByteArray &payload) {
    ReadResult result;
    result.status = ReadStatus::Invalid;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("The cloud attachment receipt is malformed.");
        return result;
    }

    const QJsonObject object = document.object();
    const QJsonValue version = object.value(QStringLiteral("version"));
    const QJsonValue attachmentId = object.value(QStringLiteral("attachment_id"));
    const QJsonValue sourceKind = object.value(QStringLiteral("source_kind"));
    const QJsonValue sourceProfileId = object.value(QStringLiteral("source_profile_id"));
    const QJsonValue sourceSemanticDigest =
        object.value(QStringLiteral("source_semantic_digest"));
    const QJsonValue sourceActivityDigest =
        object.value(QStringLiteral("source_activity_digest"));
    const QJsonValue sourceRetired = object.value(QStringLiteral("source_retired"));

    // Missing keys arrive as Undefined values, so presence and type are one
    // check: every field must exist with the exact expected JSON type.
    if (!version.isDouble()
        || !attachmentId.isString()
        || !sourceKind.isString()
        || !sourceProfileId.isString()
        || !sourceSemanticDigest.isString()
        || !sourceActivityDigest.isString()
        || !sourceRetired.isBool()) {
        result.error =
            QStringLiteral("The cloud attachment receipt has missing fields or type mismatches.");
        return result;
    }

    AccountAttachmentReceiptData data;
    data.version = version.toInt();
    data.attachmentId = attachmentId.toString();
    data.sourceKind = sourceKind.toString();
    data.sourceProfileId = sourceProfileId.toString();
    data.sourceSemanticDigest = sourceSemanticDigest.toString();
    data.sourceActivityDigest = sourceActivityDigest.toString();
    data.sourceRetired = sourceRetired.toBool();

    const QString validationError = validate(data);
    if (!validationError.isEmpty()) {
        result.error = validationError;
        return result;
    }

    result.status = ReadStatus::Ok;
    result.data = data;
    return result;
}

bool AccountAttachmentReceipt::setError(QString *error, const QString &message) {
    if (error != nullptr)
        *error = message;
    return false;
}
