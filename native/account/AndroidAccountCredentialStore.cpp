#include "AndroidAccountCredentialStore.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {
constexpr auto kActiveKey = "colosseum.account.active.v1";
constexpr auto kPendingPrefix = "colosseum.account.pending-revocation.v1.";
}

AndroidAccountCredentialStore::AndroidAccountCredentialStore(
    AndroidSecureStorageBackend *backend)
    : m_backend(backend) {
}

bool AndroidAccountCredentialStore::isAvailable() const {
    return m_backend && m_backend->isAvailable();
}

std::optional<StoredAccountCredential>
AndroidAccountCredentialStore::loadActive() const {
    if (!isAvailable())
        return std::nullopt;
    const auto blob = m_backend->read(activeKey());
    return blob.has_value() ? decodeCredential(*blob) : std::nullopt;
}

bool AndroidAccountCredentialStore::saveActive(
    const StoredAccountCredential &credential) {
    if (!isAvailable())
        return false;

    const QUuid accountId(credential.accountId.trimmed());
    const QUuid deviceId(credential.deviceId.trimmed());
    if (accountId.isNull() || deviceId.isNull() || credential.refreshToken.isEmpty())
        return false;

    StoredAccountCredential normalized = credential;
    normalized.accountId = accountId.toString(QUuid::WithoutBraces).toLower();
    normalized.deviceId = deviceId.toString(QUuid::WithoutBraces).toLower();
    return m_backend->write(activeKey(), encodeCredential(normalized));
}

bool AndroidAccountCredentialStore::clearActive() {
    return isAvailable() && m_backend->remove(activeKey());
}

QList<QByteArray> AndroidAccountCredentialStore::pendingRevocations() const {
    QList<QByteArray> tokens;
    if (!isAvailable())
        return tokens;

    const QStringList storedKeys = m_backend->keys(pendingPrefix());
    tokens.reserve(storedKeys.size());
    for (const QString &key : storedKeys) {
        const auto value = m_backend->read(key);
        if (value.has_value() && !value->isEmpty() && !tokens.contains(*value))
            tokens.append(*value);
    }
    return tokens;
}

bool AndroidAccountCredentialStore::addPendingRevocation(
    const QByteArray &refreshToken) {
    return isAvailable() && !refreshToken.isEmpty()
        && m_backend->write(pendingKey(refreshToken), refreshToken);
}

bool AndroidAccountCredentialStore::removePendingRevocation(
    const QByteArray &refreshToken) {
    return isAvailable() && !refreshToken.isEmpty()
        && m_backend->remove(pendingKey(refreshToken));
}

QString AndroidAccountCredentialStore::activeKey() {
    return QString::fromLatin1(kActiveKey);
}

QString AndroidAccountCredentialStore::pendingPrefix() {
    return QString::fromLatin1(kPendingPrefix);
}

QByteArray AndroidAccountCredentialStore::encodeCredential(
    const StoredAccountCredential &credential) {
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("account_id"), credential.accountId);
    object.insert(QStringLiteral("device_id"), credential.deviceId);
    object.insert(QStringLiteral("refresh_token"),
                  QString::fromLatin1(credential.refreshToken.toBase64()));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<StoredAccountCredential>
AndroidAccountCredentialStore::decodeCredential(const QByteArray &blob) {
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
    if (accountId.isNull() || deviceId.isNull() || credential.refreshToken.isEmpty())
        return std::nullopt;

    credential.accountId = accountId.toString(QUuid::WithoutBraces).toLower();
    credential.deviceId = deviceId.toString(QUuid::WithoutBraces).toLower();
    return credential;
}

QString AndroidAccountCredentialStore::pendingKey(
    const QByteArray &refreshToken) {
    const QByteArray digest = QCryptographicHash::hash(
        refreshToken, QCryptographicHash::Sha256).toHex();
    return pendingPrefix() + QString::fromLatin1(digest);
}
