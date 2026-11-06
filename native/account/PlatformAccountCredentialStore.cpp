#include "PlatformAccountCredentialStore.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <utility>


namespace {
constexpr auto kActiveTarget = "Brotherhood.Colosseum.Account.Active.v1";
constexpr auto kPendingPrefix = "Brotherhood.Colosseum.Account.PendingRevoke.v1.";
constexpr auto kDeletionPrefix = "Brotherhood.Colosseum.Account.PendingDeletion.v1.";
constexpr auto kStremioPrefix = "Brotherhood.Colosseum.Stremio.Profile.v1.";
constexpr auto kNuvioPrefix = "Brotherhood.Colosseum.Nuvio.Profile.v1.";

// Isolated Lanista/test instances re-root every AppData store under
// COLOSSEUM_APPDATA_TAG (main.cpp, ProgressStore/CollectionStore/
// SearchHistoryStore all do the same). The Windows credential vault is process-
// global, not path-scoped, so without this suffix a tagged test session would
// read and overwrite the real daily account's stored session. When the tag is
// unset (the real app) the credential keeps its plain, untagged target name.
QString taggedTargetKey() {
    const QString tag = qEnvironmentVariable("COLOSSEUM_APPDATA_TAG").trimmed();
    if (tag.isEmpty())
        return QString();

    return QString::fromLatin1(
        QCryptographicHash::hash(
            tag.toUtf8(),
            QCryptographicHash::Sha256).toHex());
}
}

namespace windows_account_credential_store_detail {

QString pendingTargetName(const QString &prefix, const QByteArray &refreshToken) {
    const QByteArray digest = QCryptographicHash::hash(
        refreshToken,
        QCryptographicHash::Sha256).toHex();
    return prefix + QString::fromLatin1(digest);
}

bool pendingTargetMatches(const QString &prefix, const QString &target) {
    constexpr qsizetype kDigestHexLength = 64;
    if (prefix.isEmpty() || !target.startsWith(prefix))
        return false;

    const QString suffix = target.mid(prefix.size());
    if (suffix.size() != kDigestHexLength)
        return false;

    for (const QChar character : suffix) {
        const bool isLowerHex =
            (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
            || (character >= QLatin1Char('a') && character <= QLatin1Char('f'));
        if (!isLowerHex)
            return false;
    }
    return true;
}

}

PlatformAccountCredentialStore::PlatformAccountCredentialStore(
    std::shared_ptr<AccountSecretBackend> backend) : m_backend(std::move(backend)) {}

bool PlatformAccountCredentialStore::isAvailable() const {
    return m_backend && m_backend->isAvailable();
}

std::optional<StoredAccountCredential> PlatformAccountCredentialStore::loadActive() const {
    const auto blob = readGenericCredential(activeTargetName());
    if (!blob.has_value())
        return std::nullopt;
    return decodeCredential(*blob);
}

bool PlatformAccountCredentialStore::saveActive(const StoredAccountCredential &credential) {
    const QUuid accountId(credential.accountId.trimmed());
    const QUuid deviceId(credential.deviceId.trimmed());
    if (accountId.isNull()
        || deviceId.isNull()
        || credential.refreshToken.isEmpty()) {
        return false;
    }

    StoredAccountCredential normalized = credential;
    normalized.accountId = accountId.toString(QUuid::WithoutBraces).toLower();
    normalized.deviceId = deviceId.toString(QUuid::WithoutBraces).toLower();
    return writeGenericCredential(activeTargetName(), encodeCredential(normalized));
}

bool PlatformAccountCredentialStore::clearActive() {
    return deleteGenericCredential(activeTargetName());
}

QList<QByteArray> PlatformAccountCredentialStore::pendingRevocations() const {
    QList<QByteArray> tokens;
    const QList<QString> targets = enumerateTargets(pendingTargetPrefix());
    tokens.reserve(targets.size());

    for (const QString &target : targets) {
        if (!windows_account_credential_store_detail::pendingTargetMatches(pendingTargetPrefix(), target))
            continue;
        const auto blob = readGenericCredential(target);
        if (!blob.has_value() || blob->isEmpty())
            continue;
        tokens.append(*blob);
    }
    return tokens;
}

bool PlatformAccountCredentialStore::addPendingRevocation(const QByteArray &refreshToken) {
    if (refreshToken.isEmpty())
        return false;
    return writeGenericCredential(pendingTargetName(refreshToken), refreshToken);
}

bool PlatformAccountCredentialStore::removePendingRevocation(const QByteArray &refreshToken) {
    if (refreshToken.isEmpty())
        return false;
    return deleteGenericCredential(pendingTargetName(refreshToken));
}

QList<StoredAccountDeletion> PlatformAccountCredentialStore::pendingDeletions() const {
    QList<StoredAccountDeletion> deletions;
    for (const QString &target : enumerateTargets(deletionTargetPrefix())) {
        const auto blob = readGenericCredential(target);
        if (!blob.has_value())
            continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(*blob, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            continue;
        const QJsonObject object = document.object();
        StoredAccountDeletion deletion;
        deletion.accountId = object.value(QStringLiteral("account_id")).toString();
        deletion.requestId = object.value(QStringLiteral("request_id")).toString();
        deletion.retryCapability = QByteArray::fromBase64(
            object.value(QStringLiteral("retry_capability")).toString().toLatin1(),
            QByteArray::Base64UrlEncoding);
        const QUuid accountId(deletion.accountId);
        const QUuid requestId(deletion.requestId);
        if (object.value(QStringLiteral("version")).toInt() != 1
            || accountId.isNull() || requestId.isNull()
            || deletion.retryCapability.size() != 32)
            continue;
        deletion.accountId = accountId.toString(QUuid::WithoutBraces).toLower();
        deletion.requestId = requestId.toString(QUuid::WithoutBraces).toLower();
        if (target != deletionTargetName(deletion.requestId))
            continue;
        deletions.append(deletion);
    }
    return deletions;
}

bool PlatformAccountCredentialStore::savePendingDeletion(
    const StoredAccountDeletion &deletion) {
    const QUuid accountId(deletion.accountId);
    const QUuid requestId(deletion.requestId);
    if (accountId.isNull() || requestId.isNull()
        || deletion.retryCapability.size() != 32)
        return false;
    const QString normalizedAccount = accountId.toString(QUuid::WithoutBraces).toLower();
    const QString normalizedRequest = requestId.toString(QUuid::WithoutBraces).toLower();
    const QString target = deletionTargetName(normalizedRequest);
    if (const auto existing = readGenericCredential(target); existing.has_value()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(*existing, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return false;
        const QJsonObject object = document.object();
        return object.value(QStringLiteral("account_id")).toString() == normalizedAccount
            && object.value(QStringLiteral("request_id")).toString() == normalizedRequest
            && QByteArray::fromBase64(
                   object.value(QStringLiteral("retry_capability")).toString().toLatin1(),
                   QByteArray::Base64UrlEncoding) == deletion.retryCapability;
    }
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("account_id"), normalizedAccount);
    object.insert(QStringLiteral("request_id"), normalizedRequest);
    object.insert(QStringLiteral("retry_capability"), QString::fromLatin1(
        deletion.retryCapability.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    return writeGenericCredential(
        target, QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool PlatformAccountCredentialStore::removePendingDeletion(const QString &requestId) {
    const QUuid parsed(requestId);
    if (parsed.isNull())
        return false;
    return deleteGenericCredential(deletionTargetName(
        parsed.toString(QUuid::WithoutBraces).toLower()));
}

QString PlatformAccountCredentialStore::activeTargetName() {
    const QString taggedKey = taggedTargetKey();
    if (taggedKey.isEmpty())
        return QString::fromLatin1(kActiveTarget);
    return QString::fromLatin1(kActiveTarget)
        + QStringLiteral(".Tagged.")
        + taggedKey;
}

QString PlatformAccountCredentialStore::pendingTargetPrefix() {
    const QString taggedKey = taggedTargetKey();
    if (taggedKey.isEmpty())
        return QString::fromLatin1(kPendingPrefix);
    return QString::fromLatin1(kPendingPrefix)
        + QStringLiteral("Tagged.")
        + taggedKey
        + QLatin1Char('.');
}

QString PlatformAccountCredentialStore::deletionTargetPrefix() {
    const QString taggedKey = taggedTargetKey();
    if (taggedKey.isEmpty())
        return QString::fromLatin1(kDeletionPrefix);
    return QString::fromLatin1(kDeletionPrefix)
        + QStringLiteral("Tagged.") + taggedKey + QLatin1Char('.');
}

QString PlatformAccountCredentialStore::stremioTargetName(const QString &profileId) {
    if (!validStremioProfileId(profileId))
        return QString();
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(
        profileId.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString taggedKey = taggedTargetKey();
    if (taggedKey.isEmpty())
        return QString::fromLatin1(kStremioPrefix) + digest;
    return QString::fromLatin1(kStremioPrefix)
        + QStringLiteral("Tagged.") + taggedKey + QLatin1Char('.') + digest;
}

QString PlatformAccountCredentialStore::nuvioTargetName(const QString &profileId) {
    if (!validStremioProfileId(profileId))
        return QString();
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(
        profileId.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString taggedKey = taggedTargetKey();
    if (taggedKey.isEmpty())
        return QString::fromLatin1(kNuvioPrefix) + digest;
    return QString::fromLatin1(kNuvioPrefix)
        + QStringLiteral("Tagged.") + taggedKey + QLatin1Char('.') + digest;
}

std::optional<StoredStremioCredential> PlatformAccountCredentialStore::loadStremio(
    const QString &profileId,
    const QString &accountId) const {
    if (!validStremioProfileId(profileId) || !validStremioAccountId(accountId))
        return std::nullopt;
    const auto blob = readGenericCredential(stremioTargetName(profileId));
    if (!blob.has_value())
        return std::nullopt;
    const auto credential = decodeStremioCredential(*blob);
    if (!credential.has_value() || credential->profileId != profileId
        || credential->accountId != accountId) {
        return std::nullopt;
    }
    return credential;
}

bool PlatformAccountCredentialStore::saveStremio(const StoredStremioCredential &credential) {
    if (!validStremioProfileId(credential.profileId)
        || !validStremioAccountId(credential.accountId)
        || credential.authKey.isEmpty()) {
        return false;
    }
    const QString target = stremioTargetName(credential.profileId);
    const QByteArray blob = encodeStremioCredential(credential);
    const auto previous = readGenericCredential(target);
    if (!writeGenericCredential(target, blob))
        return false;
    if (readGenericCredential(target) == std::optional<QByteArray>(blob))
        return true;
    // A successful backend write is insufficient if the credential cannot be
    // reopened. Retain the previous credential when reconnecting an account.
    if (previous)
        writeGenericCredential(target, *previous);
    else
        deleteGenericCredential(target);
    return false;
}

bool PlatformAccountCredentialStore::clearStremio(const QString &profileId) {
    const QString target = stremioTargetName(profileId);
    return !target.isEmpty() && deleteGenericCredential(target);
}

std::optional<StoredNuvioCredential> PlatformAccountCredentialStore::loadNuvio(
    const QString &profileId) const {
    const QString target = nuvioTargetName(profileId);
    if (target.isEmpty())
        return std::nullopt;
    const auto blob = readGenericCredential(target);
    if (!blob.has_value())
        return std::nullopt;
    const auto credential = decodeNuvioCredential(*blob);
    if (!credential.has_value() || credential->profileId != profileId)
        return std::nullopt;
    return credential;
}

bool PlatformAccountCredentialStore::saveNuvio(const StoredNuvioCredential &credential) {
    const QString target = nuvioTargetName(credential.profileId);
    const QUuid accountId(credential.accountId);
    const QUuid nuvioProfileId(credential.nuvioProfileId);
    if (target.isEmpty() || accountId.isNull()
        || credential.accessToken.isEmpty() || credential.refreshToken.isEmpty()
        || credential.accessToken.size() > 65536
        || credential.refreshToken.size() > 65536
        || credential.accessExpiresAtUtcMs <= 0
        || (credential.nuvioProfileIndex == 0 && !credential.nuvioProfileId.isEmpty())
        || (credential.nuvioProfileIndex != 0
            && (credential.nuvioProfileIndex < 1 || credential.nuvioProfileIndex > 6
                || nuvioProfileId.isNull()))) {
        return false;
    }

    StoredNuvioCredential normalized = credential;
    normalized.accountId = accountId.toString(QUuid::WithoutBraces);
    if (credential.nuvioProfileIndex != 0)
        normalized.nuvioProfileId = nuvioProfileId.toString(QUuid::WithoutBraces);
    const QByteArray blob = encodeNuvioCredential(normalized);
    const auto previous = readGenericCredential(target);
    if (!writeGenericCredential(target, blob))
        return false;
    if (readGenericCredential(target) == std::optional<QByteArray>(blob))
        return true;
    // A credential that cannot be read back cannot safely complete sign-in.
    if (previous)
        writeGenericCredential(target, *previous);
    else
        deleteGenericCredential(target);
    return false;
}

bool PlatformAccountCredentialStore::clearNuvio(const QString &profileId) {
    const QString target = nuvioTargetName(profileId);
    return !target.isEmpty() && deleteGenericCredential(target);
}

QByteArray PlatformAccountCredentialStore::encodeCredential(
    const StoredAccountCredential &credential) {
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("account_id"), credential.accountId);
    object.insert(QStringLiteral("device_id"), credential.deviceId);
    object.insert(QStringLiteral("refresh_token"),
                  QString::fromLatin1(credential.refreshToken.toBase64()));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<StoredAccountCredential> PlatformAccountCredentialStore::decodeCredential(
    const QByteArray &blob) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(blob, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toInt() != 1)
        return std::nullopt;

    StoredAccountCredential credential;
    credential.accountId = object.value(QStringLiteral("account_id")).toString().trimmed();
    credential.deviceId = object.value(QStringLiteral("device_id")).toString().trimmed();
    credential.refreshToken = QByteArray::fromBase64(
        object.value(QStringLiteral("refresh_token")).toString().toLatin1());

    const QUuid accountId(credential.accountId);
    const QUuid deviceId(credential.deviceId);
    if (accountId.isNull()
        || deviceId.isNull()
        || credential.refreshToken.isEmpty()) {
        return std::nullopt;
    }

    credential.accountId = accountId.toString(QUuid::WithoutBraces).toLower();
    credential.deviceId = deviceId.toString(QUuid::WithoutBraces).toLower();
    return credential;
}

QString PlatformAccountCredentialStore::pendingTargetName(const QByteArray &refreshToken) {
    return windows_account_credential_store_detail::pendingTargetName(
        pendingTargetPrefix(),
        refreshToken);
}

QString PlatformAccountCredentialStore::deletionTargetName(const QString &requestId) {
    return deletionTargetPrefix() + requestId;
}

bool PlatformAccountCredentialStore::validStremioProfileId(const QString &profileId) {
    const QString normalized = profileId.trimmed();
    const QUuid parsed(normalized);
    return normalized == profileId
        && (normalized == QStringLiteral("local") || normalized == QStringLiteral("legacy")
            || (!parsed.isNull()
                && normalized == parsed.toString(QUuid::WithoutBraces).toLower()));
}

bool PlatformAccountCredentialStore::validStremioAccountId(const QString &accountId) {
    const QString normalized = accountId.trimmed();
    if (normalized.isEmpty() || normalized.size() > 256 || normalized != accountId)
        return false;
    for (const QChar character : normalized) {
        if (character.isSpace() || character.unicode() < 0x20 || character.unicode() == 0x7f)
            return false;
    }
    return true;
}

QByteArray PlatformAccountCredentialStore::encodeStremioCredential(
    const StoredStremioCredential &credential) {
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("profile_id"), credential.profileId);
    object.insert(QStringLiteral("account_id"), credential.accountId);
    object.insert(QStringLiteral("auth_key"), QString::fromLatin1(
        credential.authKey.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<StoredStremioCredential> PlatformAccountCredentialStore::decodeStremioCredential(
    const QByteArray &blob) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(blob, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    StoredStremioCredential credential;
    credential.profileId = object.value(QStringLiteral("profile_id")).toString();
    credential.accountId = object.value(QStringLiteral("account_id")).toString();
    credential.authKey = QByteArray::fromBase64(
        object.value(QStringLiteral("auth_key")).toString().toLatin1(), QByteArray::Base64UrlEncoding);
    if (object.size() != 4 || object.value(QStringLiteral("version")).toInt() != 1
        || !validStremioProfileId(credential.profileId)
        || !validStremioAccountId(credential.accountId)
        || credential.authKey.isEmpty()) {
        return std::nullopt;
    }
    return credential;
}

QByteArray PlatformAccountCredentialStore::encodeNuvioCredential(
    const StoredNuvioCredential &credential) {
    const auto base64 = [](const QByteArray &token) {
        return QString::fromLatin1(token.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    QJsonObject object{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profile_id"), credential.profileId},
        {QStringLiteral("account_id"), credential.accountId},
        {QStringLiteral("access_token"), base64(credential.accessToken)},
        {QStringLiteral("refresh_token"), base64(credential.refreshToken)},
        {QStringLiteral("access_expires_at_utc_ms"), credential.accessExpiresAtUtcMs}};
    if (credential.nuvioProfileIndex != 0) {
        object.insert(QStringLiteral("version"), 2);
        object.insert(QStringLiteral("nuvio_profile_id"), credential.nuvioProfileId);
        object.insert(QStringLiteral("nuvio_profile_index"), credential.nuvioProfileIndex);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<StoredNuvioCredential> PlatformAccountCredentialStore::decodeNuvioCredential(
    const QByteArray &blob) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(blob, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;

    const QJsonObject object = document.object();
    const int version = object.value(QStringLiteral("version")).toInt();
    if (!((version == 1 && object.size() == 6) || (version == 2 && object.size() == 8)))
        return std::nullopt;
    StoredNuvioCredential credential;
    if (version == 2) {
        credential.nuvioProfileId = object.value(QStringLiteral("nuvio_profile_id")).toString();
        credential.nuvioProfileIndex = object.value(QStringLiteral("nuvio_profile_index")).toInt();
        const QUuid nuvioProfileId(credential.nuvioProfileId);
        if (nuvioProfileId.isNull()
            || nuvioProfileId.toString(QUuid::WithoutBraces) != credential.nuvioProfileId
            || credential.nuvioProfileIndex < 1 || credential.nuvioProfileIndex > 6) {
            return std::nullopt;
        }
    }
    credential.profileId = object.value(QStringLiteral("profile_id")).toString();
    credential.accountId = object.value(QStringLiteral("account_id")).toString();
    const QUuid accountId(credential.accountId);
    if (!validStremioProfileId(credential.profileId) || accountId.isNull()
        || accountId.toString(QUuid::WithoutBraces).toLower() != credential.accountId
        || !object.value(QStringLiteral("access_expires_at_utc_ms")).isDouble())
        return std::nullopt;
    credential.accessExpiresAtUtcMs =
        object.value(QStringLiteral("access_expires_at_utc_ms")).toInteger();
    if (credential.accessExpiresAtUtcMs <= 0)
        return std::nullopt;

    const auto access = QByteArray::fromBase64Encoding(
        object.value(QStringLiteral("access_token")).toString().toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    const auto refresh = QByteArray::fromBase64Encoding(
        object.value(QStringLiteral("refresh_token")).toString().toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!access || !refresh || access.decoded.isEmpty() || refresh.decoded.isEmpty()
        || access.decoded.size() > 65536 || refresh.decoded.size() > 65536)
        return std::nullopt;
    credential.accessToken = access.decoded;
    credential.refreshToken = refresh.decoded;
    return credential;
}

bool PlatformAccountCredentialStore::writeGenericCredential(
    const QString &target, const QByteArray &blob) {
    return m_backend && !target.isEmpty() && !blob.isEmpty() && m_backend->write(target, blob);
}

std::optional<QByteArray> PlatformAccountCredentialStore::readGenericCredential(
    const QString &target) const {
    return m_backend ? m_backend->read(target) : std::nullopt;
}

bool PlatformAccountCredentialStore::deleteGenericCredential(const QString &target) {
    return m_backend && m_backend->remove(target);
}

QList<QString> PlatformAccountCredentialStore::enumerateTargets(const QString &prefix) const {
    QList<QString> targets;
    if (m_backend) {
        for (const QString &target : m_backend->targets(prefix)) {
            if (target.startsWith(prefix))
                targets.append(target);
        }
    }
    return targets;
}
