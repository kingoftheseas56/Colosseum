#include "TrackerCredentialVault.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <limits>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace {

constexpr auto kCredentialPrefix = "Brotherhood.Colosseum.TrackerCredential.v1.";
constexpr qsizetype kMaximumTokenBytes = 16 * 1024;
constexpr qsizetype kMaximumScopeCount = 8;

QString taggedTargetKey()
{
    const QString tag = qEnvironmentVariable("COLOSSEUM_APPDATA_TAG").trimmed();
    if (tag.isEmpty())
        return {};
    return QString::fromLatin1(QCryptographicHash::hash(
        tag.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool validProfileId(const QString &profileId)
{
    const QString normalized = profileId.trimmed();
    const QUuid uuid(normalized);
    return normalized == QLatin1String("local")
        || (!uuid.isNull() && normalized == uuid.toString(QUuid::WithoutBraces).toLower());
}

bool validRemoteAccountId(const QString &remoteAccountId)
{
    const QString normalized = remoteAccountId.trimmed();
    if (normalized.isEmpty() || normalized != remoteAccountId || normalized.size() > 64)
        return false;
    for (const QChar character : normalized) {
        if (!character.isDigit())
            return false;
    }
    return normalized != QLatin1String("0");
}

bool validScopes(const QStringList &scopes)
{
    if (scopes.isEmpty() || scopes.size() > kMaximumScopeCount)
        return false;
    for (const QString &scope : scopes) {
        if (scope != QLatin1String("media:read")
            && scope != QLatin1String("media:write")) {
            return false;
        }
    }
    return true;
}

bool validCredential(const TrackerCredential &credential)
{
    return credential.slot.providerId == TrackerProviderId::Simkl
        && validProfileId(credential.slot.profileId)
        && validRemoteAccountId(credential.slot.remoteAccountId)
        && !credential.accessToken.isEmpty()
        && credential.accessToken.size() <= kMaximumTokenBytes
        && !credential.refreshToken.isEmpty()
        && credential.refreshToken.size() <= kMaximumTokenBytes
        && credential.accessTokenExpiresAtMs > 0
        && credential.refreshTokenExpiresAtMs > credential.accessTokenExpiresAtMs
        && validScopes(credential.grantedScopes);
}

QByteArray encodeCredential(const TrackerCredential &credential)
{
    QJsonArray scopes;
    for (const QString &scope : credential.grantedScopes)
        scopes.append(scope);
    return QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profileId"), credential.slot.profileId},
        {QStringLiteral("providerId"), trackerProviderKey(credential.slot.providerId)},
        {QStringLiteral("remoteAccountId"), credential.slot.remoteAccountId},
        {QStringLiteral("accessToken"), QString::fromLatin1(credential.accessToken.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))},
        {QStringLiteral("refreshToken"), QString::fromLatin1(credential.refreshToken.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))},
        {QStringLiteral("accessExpiresAtMs"), QString::number(credential.accessTokenExpiresAtMs)},
        {QStringLiteral("refreshExpiresAtMs"), QString::number(credential.refreshTokenExpiresAtMs)},
        {QStringLiteral("scopes"), scopes}}).toJson(QJsonDocument::Compact);
}

std::optional<TrackerCredential> decodeCredential(const QByteArray &blob)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(blob, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    if (object.size() != 9 || object.value(QStringLiteral("version")).toInt() != 1)
        return std::nullopt;
    const auto providerId = trackerProviderIdFromKey(
        object.value(QStringLiteral("providerId")).toString());
    bool accessExpiryOk = false;
    bool refreshExpiryOk = false;
    TrackerCredential credential;
    credential.slot.profileId = object.value(QStringLiteral("profileId")).toString();
    credential.slot.providerId = providerId.value_or(TrackerProviderId::Mal);
    credential.slot.remoteAccountId = object.value(QStringLiteral("remoteAccountId")).toString();
    credential.accessToken = QByteArray::fromBase64(
        object.value(QStringLiteral("accessToken")).toString().toLatin1(),
        QByteArray::Base64UrlEncoding);
    credential.refreshToken = QByteArray::fromBase64(
        object.value(QStringLiteral("refreshToken")).toString().toLatin1(),
        QByteArray::Base64UrlEncoding);
    credential.accessTokenExpiresAtMs = object.value(QStringLiteral("accessExpiresAtMs"))
                                            .toString().toLongLong(&accessExpiryOk);
    credential.refreshTokenExpiresAtMs = object.value(QStringLiteral("refreshExpiresAtMs"))
                                             .toString().toLongLong(&refreshExpiryOk);
    const QJsonArray scopes = object.value(QStringLiteral("scopes")).toArray();
    for (const QJsonValue &value : scopes) {
        if (!value.isString())
            return std::nullopt;
        credential.grantedScopes.append(value.toString());
    }
    if (!providerId || !accessExpiryOk || !refreshExpiryOk || !validCredential(credential))
        return std::nullopt;
    return credential;
}

bool sameCredential(const TrackerCredential &left, const TrackerCredential &right)
{
    return left.slot.profileId == right.slot.profileId
        && left.slot.providerId == right.slot.providerId
        && left.slot.remoteAccountId == right.slot.remoteAccountId
        && left.accessToken == right.accessToken
        && left.refreshToken == right.refreshToken
        && left.accessTokenExpiresAtMs == right.accessTokenExpiresAtMs
        && left.refreshTokenExpiresAtMs == right.refreshTokenExpiresAtMs
        && left.grantedScopes == right.grantedScopes;
}

bool writeCredential(const QString &target, const QByteArray &blob)
{
#ifdef Q_OS_WIN
    if (target.isEmpty() || blob.isEmpty() || blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
        return false;
    const std::wstring targetWide = target.toStdWString();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(targetWide.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"Colosseum");
    return CredWriteW(&credential, 0) == TRUE;
#else
    Q_UNUSED(target);
    Q_UNUSED(blob);
    return false;
#endif
}

std::optional<QByteArray> readCredential(const QString &target)
{
#ifdef Q_OS_WIN
    if (target.isEmpty())
        return std::nullopt;
    const std::wstring targetWide = target.toStdWString();
    PCREDENTIALW credential = nullptr;
    if (CredReadW(targetWide.c_str(), CRED_TYPE_GENERIC, 0, &credential) != TRUE)
        return std::nullopt;
    const QByteArray blob(reinterpret_cast<const char *>(credential->CredentialBlob),
                          static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return blob;
#else
    Q_UNUSED(target);
    return std::nullopt;
#endif
}

bool deleteCredential(const QString &target)
{
#ifdef Q_OS_WIN
    if (target.isEmpty())
        return false;
    const std::wstring targetWide = target.toStdWString();
    if (CredDeleteW(targetWide.c_str(), CRED_TYPE_GENERIC, 0) == TRUE)
        return true;
    return GetLastError() == ERROR_NOT_FOUND;
#else
    Q_UNUSED(target);
    return false;
#endif
}

} // namespace

bool trackerCredentialIsReusable(const TrackerCredential &credential, qint64 nowMs)
{
    return validCredential(credential) && nowMs >= 0
        && credential.accessTokenExpiresAtMs > nowMs;
}

bool WindowsTrackerCredentialVault::isAvailable() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool WindowsTrackerCredentialVault::hasReusableCredential(
    const TrackerCredentialSlot &slot,
    qint64 nowMs) const
{
    const auto credential = loadForProfile(slot.profileId, slot.providerId);
    return credential.has_value() && credential->slot.remoteAccountId == slot.remoteAccountId
        && trackerCredentialIsReusable(*credential, nowMs);
}

bool WindowsTrackerCredentialVault::saveAndVerify(const TrackerCredential &credential)
{
    if (!isAvailable() || !validCredential(credential))
        return false;
    const QString target = targetName(credential.slot.profileId, credential.slot.providerId);
    if (!writeCredential(target, encodeCredential(credential)))
        return false;
    const auto readback = loadForProfile(credential.slot.profileId, credential.slot.providerId);
    if (readback.has_value() && sameCredential(*readback, credential))
        return true;
    deleteCredential(target);
    return false;
}

std::optional<TrackerCredential> WindowsTrackerCredentialVault::loadForProfile(
    const QString &profileId,
    TrackerProviderId providerId) const
{
    const QString target = targetName(profileId, providerId);
    const auto blob = readCredential(target);
    if (!blob.has_value())
        return std::nullopt;
    const auto credential = decodeCredential(*blob);
    if (!credential || credential->slot.profileId != profileId
        || credential->slot.providerId != providerId) {
        return std::nullopt;
    }
    return credential;
}

bool WindowsTrackerCredentialVault::clearForProfile(
    const QString &profileId,
    TrackerProviderId providerId)
{
    return deleteCredential(targetName(profileId, providerId));
}

QString WindowsTrackerCredentialVault::targetName(
    const QString &profileId,
    TrackerProviderId providerId)
{
    if (!validProfileId(profileId) || providerId != TrackerProviderId::Simkl)
        return {};
    const QString profileHash = QString::fromLatin1(QCryptographicHash::hash(
        profileId.toUtf8(), QCryptographicHash::Sha256).toHex());
    QString target = QString::fromLatin1(kCredentialPrefix)
        + trackerProviderKey(providerId) + QLatin1Char('.') + profileHash;
    const QString tag = taggedTargetKey();
    if (!tag.isEmpty())
        target += QStringLiteral(".Tagged.") + tag;
    return target;
}
