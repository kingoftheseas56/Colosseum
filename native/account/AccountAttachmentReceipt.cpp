// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountAttachmentReceipt.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <utility>

namespace {
constexpr int kReceiptVersion = 1;
constexpr qsizetype kManifestLimit = 100;
constexpr qsizetype kManifestBytesLimit = 256 * 1024;
constexpr qsizetype kPreexistingMutationLimit = 10000;
constexpr qsizetype kPreexistingMutationBytesLimit = 512 * 1024;

QByteArray manifestBytes(
    const QList<SyncWireAttachmentManifestItem> &manifest) {
    QJsonArray array;
    for (const SyncWireAttachmentManifestItem &item : manifest)
        array.append(syncWireMutationToJson(item.mutation));
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QString computedManifestDigest(
    const QList<SyncWireAttachmentManifestItem> &manifest) {
    if (manifest.isEmpty())
        return QString();
    return QStringLiteral("sha256:")
        + QString::fromLatin1(QCryptographicHash::hash(
                                  manifestBytes(manifest),
                                  QCryptographicHash::Sha256)
                                  .toHex());
}

QByteArray normalizedItemHash(
    const SyncWireAttachmentManifestItem &item) {
    return item.canonicalPayloadHash.isEmpty()
        ? syncWireCanonicalPayloadHash(item.mutation)
        : item.canonicalPayloadHash;
}
}

QString AccountAttachmentReceipt::sourceKindLegacyLocal() {
    return QStringLiteral("legacy_local");
}

QString AccountAttachmentReceipt::sourceKindLocalOnly() {
    return QStringLiteral("local_only");
}

QString AccountAttachmentReceipt::retirementPhasePending() {
    return QStringLiteral("pending");
}

QString AccountAttachmentReceipt::retirementPhaseStarted() {
    return QStringLiteral("retirement_started");
}

QString AccountAttachmentReceipt::retirementPhaseSourceRetired() {
    return QStringLiteral("source_retired");
}

bool AccountAttachmentReceipt::save(const ProfilePaths &paths,
                                    const AccountAttachmentReceiptData &data,
                                    QString *error) {
    const QString path = paths.cloudAttachmentReceiptPath();
    if (path.isEmpty())
        return setError(error,
                        QStringLiteral("The cloud attachment receipt requires an account profile."));

    AccountAttachmentReceiptData normalized = data;
    if (normalized.retirementPhase.isEmpty()) {
        normalized.retirementPhase = normalized.sourceRetired
            ? retirementPhaseSourceRetired()
            : retirementPhasePending();
    }
    if (normalized.retirementPhase
            == retirementPhaseSourceRetired()) {
        normalized.sourceRetired = true;
    }
    if (!normalized.manifest.isEmpty()) {
        if (normalized.manifestDigest.isEmpty())
            normalized.manifestDigest = computedManifestDigest(normalized.manifest);
        for (SyncWireAttachmentManifestItem &item : normalized.manifest) {
            if (item.canonicalPayloadHash.isEmpty())
                item.canonicalPayloadHash = syncWireCanonicalPayloadHash(item.mutation);
        }
    }

    QString validationError;
    if (!isValidData(normalized, &validationError))
        return setError(error, validationError);

    return writeAtomic(path, normalized, error);
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
    updated.retirementPhase = retirementPhaseSourceRetired();
    return writeAtomic(paths.cloudAttachmentReceiptPath(), updated, error);
}

bool AccountAttachmentReceipt::markRetirementStarted(
    const ProfilePaths &paths,
    QString *error) {
    const ReadResult existing = read(paths);
    if (existing.status != ReadStatus::Ok) {
        const QString reason = existing.error.isEmpty()
            ? QStringLiteral("No cloud attachment receipt is pending.")
            : existing.error;
        return setError(error, reason);
    }

    if (existing.data.sourceRetired
        || existing.data.retirementPhase
               == retirementPhaseSourceRetired()) {
        return true;
    }

    AccountAttachmentReceiptData updated = existing.data;
    updated.sourceRetired = false;
    updated.retirementPhase = retirementPhaseStarted();
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

    if (data.sourceProfileId.toUtf8().size() > 256
        || data.sourceSemanticDigest.toUtf8().size() > 256
        || data.sourceActivityDigest.toUtf8().size() > 256) {
        return QStringLiteral("The cloud attachment source identity is too large.");
    }

    const auto validateOptionalUuid = [](const QString &value,
                                         const QString &label) -> QString {
        if (value.isEmpty())
            return QString();
        const QUuid parsed(value);
        if (parsed.isNull()
            || parsed.toString(QUuid::WithoutBraces) != value) {
            return QStringLiteral("The cloud attachment %1 must be a lowercase UUID.")
                .arg(label);
        }
        return QString();
    };
    if (const QString optionalError = validateOptionalUuid(
            data.accountId, QStringLiteral("account"));
        !optionalError.isEmpty())
        return optionalError;
    if (const QString optionalError = validateOptionalUuid(
            data.deviceId, QStringLiteral("device"));
        !optionalError.isEmpty())
        return optionalError;

    if (data.preexistingMutationIds.size() > kPreexistingMutationLimit)
        return QStringLiteral("The pre-existing account mutation set is too large.");
    QSet<QString> preexistingIds;
    QJsonArray preexistingArray;
    for (const QString &mutationId : data.preexistingMutationIds) {
        const QUuid parsedMutationId(mutationId);
        if (parsedMutationId.isNull()
            || parsedMutationId.toString(QUuid::WithoutBraces) != mutationId
            || preexistingIds.contains(mutationId)) {
            return QStringLiteral("The pre-existing account mutation set is invalid.");
        }
        preexistingIds.insert(mutationId);
        preexistingArray.append(mutationId);
    }
    if (QJsonDocument(preexistingArray).toJson(QJsonDocument::Compact).size()
        > kPreexistingMutationBytesLimit) {
        return QStringLiteral("The pre-existing account mutation set is too large.");
    }

    if (data.manifest.size() > kManifestLimit)
        return QStringLiteral("The cloud attachment manifest is too large.");

    QSet<QString> mutationIds;
    for (const SyncWireAttachmentManifestItem &item : data.manifest) {
        const SyncWireMutation &mutation = item.mutation;
        const QUuid mutationId(mutation.mutationId);
        const QUuid deviceId(mutation.deviceId);
        if (mutationId.isNull()
            || mutationId.toString(QUuid::WithoutBraces) != mutation.mutationId
            || deviceId.isNull()
            || deviceId.toString(QUuid::WithoutBraces) != mutation.deviceId
            || mutation.hlc.deviceId != mutation.deviceId
            || mutation.category.isEmpty()
            || mutation.category != mutation.category.trimmed().toLower()
            || !isValidSyncWireRecordKey(mutation.recordKey)
            || mutation.schemaVersion <= 0
            || mutation.hlc.physicalMs < 0
            || (mutation.operation == SyncWireOperation::Put
                && mutation.payload.isUndefined())
            || (mutation.operation == SyncWireOperation::Delete
                && !mutation.payload.isUndefined()
                && !mutation.payload.isNull())
            || mutationIds.contains(mutation.mutationId)) {
            return QStringLiteral("The cloud attachment manifest contains an invalid mutation.");
        }
        mutationIds.insert(mutation.mutationId);
        if (!item.canonicalPayloadHash.isEmpty()
            && item.canonicalPayloadHash.size() != 32) {
            return QStringLiteral("The cloud attachment manifest payload hash is invalid.");
        }
    }

    if (data.manifest.isEmpty()) {
        if (!data.manifestDigest.isEmpty())
            return QStringLiteral("The empty cloud attachment manifest cannot have a digest.");
    } else {
        if (data.manifestDigest.isEmpty()
            || data.manifestDigest != computedManifestDigest(data.manifest)) {
            return QStringLiteral("The cloud attachment manifest digest is invalid.");
        }
        if (manifestBytes(data.manifest).size() > kManifestBytesLimit)
            return QStringLiteral("The cloud attachment manifest is too large.");
    }

    const QString phase = data.retirementPhase.isEmpty()
        ? (data.sourceRetired
               ? retirementPhaseSourceRetired()
               : retirementPhasePending())
        : data.retirementPhase;
    if (phase != retirementPhasePending()
        && phase != retirementPhaseStarted()
        && phase != retirementPhaseSourceRetired()) {
        return QStringLiteral("The cloud attachment retirement phase is invalid.");
    }
    if ((phase == retirementPhaseSourceRetired()) != data.sourceRetired) {
        return QStringLiteral("The cloud attachment retirement phase is incoherent.");
    }

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
    if (!data.accountId.isEmpty())
        object.insert(QStringLiteral("account_id"), data.accountId);
    if (!data.deviceId.isEmpty())
        object.insert(QStringLiteral("device_id"), data.deviceId);
    if (!data.preexistingMutationIds.isEmpty()) {
        QStringList ordered = data.preexistingMutationIds;
        ordered.sort();
        QJsonArray preexisting;
        for (const QString &mutationId : std::as_const(ordered))
            preexisting.append(mutationId);
        object.insert(QStringLiteral("preexisting_mutation_ids"), preexisting);
    }
    if (!data.manifest.isEmpty()) {
        object.insert(QStringLiteral("manifest_digest"), data.manifestDigest);
        QJsonArray manifest;
        for (const SyncWireAttachmentManifestItem &item : data.manifest) {
            SyncWireAttachmentManifestItem normalized = item;
            normalized.canonicalPayloadHash = normalizedItemHash(item);
            manifest.append(syncWireAttachmentManifestItemToJson(normalized));
        }
        object.insert(QStringLiteral("manifest"), manifest);
    }
    object.insert(QStringLiteral("source_retired"), data.sourceRetired);
    object.insert(
        QStringLiteral("retirement_phase"),
        data.retirementPhase.isEmpty()
            ? (data.sourceRetired
                   ? retirementPhaseSourceRetired()
                   : retirementPhasePending())
            : data.retirementPhase);
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
    const QJsonValue accountId = object.value(QStringLiteral("account_id"));
    const QJsonValue deviceId = object.value(QStringLiteral("device_id"));
    const QJsonValue preexistingMutationIds =
        object.value(QStringLiteral("preexisting_mutation_ids"));
    const QJsonValue manifestDigest = object.value(QStringLiteral("manifest_digest"));
    const QJsonValue manifestValue = object.value(QStringLiteral("manifest"));
    const QJsonValue retirementPhase = object.value(QStringLiteral("retirement_phase"));

    // Missing keys arrive as Undefined values, so presence and type are one
    // check: every field must exist with the exact expected JSON type.
    if (!version.isDouble()
        || !attachmentId.isString()
        || !sourceKind.isString()
        || !sourceProfileId.isString()
        || !sourceSemanticDigest.isString()
        || !sourceActivityDigest.isString()
        || !sourceRetired.isBool()
        || (object.contains(QStringLiteral("retirement_phase"))
            && !retirementPhase.isString())
        || (object.contains(QStringLiteral("account_id")) && !accountId.isString())
        || (object.contains(QStringLiteral("device_id")) && !deviceId.isString())
        || (object.contains(QStringLiteral("preexisting_mutation_ids"))
            && !preexistingMutationIds.isArray())
        || (object.contains(QStringLiteral("manifest_digest")) && !manifestDigest.isString())
        || (object.contains(QStringLiteral("manifest")) && !manifestValue.isArray())) {
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
    data.accountId = accountId.toString();
    data.deviceId = deviceId.toString();
    if (preexistingMutationIds.isArray()) {
        for (const QJsonValue &value : preexistingMutationIds.toArray()) {
            if (!value.isString()) {
                result.error = QStringLiteral(
                    "The pre-existing account mutation set is malformed.");
                return result;
            }
            data.preexistingMutationIds.append(value.toString());
        }
    }
    data.manifestDigest = manifestDigest.toString();
    data.retirementPhase = object.contains(QStringLiteral("retirement_phase"))
        ? retirementPhase.toString()
        : (data.sourceRetired
               ? retirementPhaseSourceRetired()
               : retirementPhasePending());
    if (manifestValue.isArray()) {
        for (const QJsonValue &value : manifestValue.toArray()) {
            if (!value.isObject()) {
                result.error = QStringLiteral("The cloud attachment manifest is malformed.");
                return result;
            }
            const auto item = syncWireAttachmentManifestItemFromJson(value.toObject());
            if (!item.has_value()) {
                result.error = QStringLiteral("The cloud attachment manifest is malformed.");
                return result;
            }
            data.manifest.append(*item);
        }
    }
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
