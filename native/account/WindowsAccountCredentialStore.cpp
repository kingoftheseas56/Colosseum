// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "WindowsAccountCredentialStore.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#ifdef Q_OS_LINUX
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QMap>
#endif

#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif


#ifdef Q_OS_LINUX
struct ColosseumSecretValue {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};
Q_DECLARE_METATYPE(ColosseumSecretValue)

QDBusArgument &operator<<(QDBusArgument &argument, const ColosseumSecretValue &value) {
    argument.beginStructure();
    argument << value.session << value.parameters << value.value << value.contentType;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, ColosseumSecretValue &value) {
    argument.beginStructure();
    argument >> value.session >> value.parameters >> value.value >> value.contentType;
    argument.endStructure();
    return argument;
}
#endif

namespace {
constexpr auto kActiveTarget = "Brotherhood.Colosseum.Account.Active.v1";
constexpr auto kPendingPrefix = "Brotherhood.Colosseum.Account.PendingRevoke.v1.";

QString taggedTargetKey() {
    const QString tag = qEnvironmentVariable("COLOSSEUM_APPDATA_TAG").trimmed();
    if (tag.isEmpty())
        return QString();

    return QString::fromLatin1(
        QCryptographicHash::hash(
            tag.toUtf8(),
            QCryptographicHash::Sha256).toHex());
}
#ifdef Q_OS_LINUX
constexpr auto kSecretServiceName = "org.freedesktop.secrets";
constexpr auto kSecretServicePath = "/org/freedesktop/secrets";

void registerSecretServiceTypes() {
    static const bool registered = [] {
        qDBusRegisterMetaType<ColosseumSecretValue>();
        qDBusRegisterMetaType<QMap<QString, QString>>();
        return true;
    }();
    Q_UNUSED(registered);
}

QMap<QString, QString> secretAttributes(const QString &target = QString()) {
    QMap<QString, QString> attributes;
    attributes.insert(QStringLiteral("application"), QStringLiteral("Colosseum"));
    if (!target.isEmpty())
        attributes.insert(QStringLiteral("target"), target);
    return attributes;
}

std::optional<QDBusObjectPath> openSecretSession() {
    registerSecretServiceTypes();
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return std::nullopt;
    QDBusInterface service(kSecretServiceName, kSecretServicePath,
                           QStringLiteral("org.freedesktop.Secret.Service"), bus);
    if (!service.isValid())
        return std::nullopt;
    const QDBusMessage reply = service.call(QStringLiteral("OpenSession"),
        QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString())));
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() < 2)
        return std::nullopt;
    const QDBusObjectPath session = qvariant_cast<QDBusObjectPath>(reply.arguments().at(1));
    if (session.path().isEmpty() || session.path() == QStringLiteral("/"))
        return std::nullopt;
    return session;
}

QList<QDBusObjectPath> searchSecretItems(const QString &target = QString()) {
    registerSecretServiceTypes();
    QList<QDBusObjectPath> items;
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return items;
    QDBusInterface service(kSecretServiceName, kSecretServicePath,
                           QStringLiteral("org.freedesktop.Secret.Service"), bus);
    const QDBusMessage reply = service.call(QStringLiteral("SearchItems"),
        QVariant::fromValue(secretAttributes(target)));
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() < 2)
        return items;
    items = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0));
    const auto locked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1));
    for (const auto &path : locked)
        if (!items.contains(path)) items.append(path);
    return items;
}

void closeSecretSession(const QDBusObjectPath &session) {
    if (session.path().isEmpty() || session.path() == QStringLiteral("/"))
        return;
    QDBusInterface iface(kSecretServiceName, session.path(),
                         QStringLiteral("org.freedesktop.Secret.Session"),
                         QDBusConnection::sessionBus());
    if (iface.isValid())
        iface.call(QStringLiteral("Close"));
}

bool secretServiceAvailable() {
    registerSecretServiceTypes();
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusInterface collection(kSecretServiceName,
                              QStringLiteral("/org/freedesktop/secrets/aliases/default"),
                              QStringLiteral("org.freedesktop.Secret.Collection"), bus);
    if (!collection.isValid() || collection.property("Locked").toBool())
        return false;
    const auto session = openSecretSession();
    if (!session.has_value())
        return false;
    closeSecretSession(*session);
    return true;
}
#endif
}

bool WindowsAccountCredentialStore::isAvailable() const {
#ifdef Q_OS_WIN
    return true;
#elif defined(Q_OS_LINUX)
    return secretServiceAvailable();
#else
    return false;
#endif
}

std::optional<StoredAccountCredential> WindowsAccountCredentialStore::loadActive() const {
    const auto blob = readGenericCredential(activeTargetName());
    if (!blob.has_value())
        return std::nullopt;
    return decodeCredential(*blob);
}

bool WindowsAccountCredentialStore::saveActive(const StoredAccountCredential &credential) {
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

bool WindowsAccountCredentialStore::clearActive() {
    return deleteGenericCredential(activeTargetName());
}

QList<QByteArray> WindowsAccountCredentialStore::pendingRevocations() const {
    QList<QByteArray> tokens;
    const QList<QString> targets = enumerateTargets(pendingTargetPrefix());
    tokens.reserve(targets.size());

    for (const QString &target : targets) {
        const auto blob = readGenericCredential(target);
        if (!blob.has_value() || blob->isEmpty())
            continue;
        tokens.append(*blob);
    }
    return tokens;
}

bool WindowsAccountCredentialStore::addPendingRevocation(const QByteArray &refreshToken) {
    if (refreshToken.isEmpty())
        return false;
    return writeGenericCredential(pendingTargetName(refreshToken), refreshToken);
}

bool WindowsAccountCredentialStore::removePendingRevocation(const QByteArray &refreshToken) {
    if (refreshToken.isEmpty())
        return false;
    return deleteGenericCredential(pendingTargetName(refreshToken));
}

QString WindowsAccountCredentialStore::activeTargetName() {
    const QString taggedKey = taggedTargetKey();
    if (taggedKey.isEmpty())
        return QString::fromLatin1(kActiveTarget);
    return QString::fromLatin1(kActiveTarget)
        + QStringLiteral(".Tagged.")
        + taggedKey;
}

QString WindowsAccountCredentialStore::pendingTargetPrefix() {
    const QString taggedKey = taggedTargetKey();
    if (taggedKey.isEmpty())
        return QString::fromLatin1(kPendingPrefix);
    return QString::fromLatin1(kPendingPrefix)
        + QStringLiteral("Tagged.")
        + taggedKey
        + QLatin1Char('.');
}

QByteArray WindowsAccountCredentialStore::encodeCredential(
    const StoredAccountCredential &credential) {
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("account_id"), credential.accountId);
    object.insert(QStringLiteral("device_id"), credential.deviceId);
    object.insert(QStringLiteral("refresh_token"),
                  QString::fromLatin1(credential.refreshToken.toBase64()));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<StoredAccountCredential> WindowsAccountCredentialStore::decodeCredential(
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

QString WindowsAccountCredentialStore::pendingTargetName(const QByteArray &refreshToken) {
    const QByteArray digest = QCryptographicHash::hash(
        refreshToken,
        QCryptographicHash::Sha256).toHex();
    return pendingTargetPrefix() + QString::fromLatin1(digest);
}

bool WindowsAccountCredentialStore::writeGenericCredential(
    const QString &target,
    const QByteArray &blob) {
#ifdef Q_OS_WIN
    if (target.isEmpty() || blob.isEmpty())
        return false;
    if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
        return false;

    const std::wstring targetWide = target.toStdWString();

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(targetWide.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(
        const_cast<char *>(blob.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"Colosseum");

    return CredWriteW(&credential, 0) == TRUE;
#elif defined(Q_OS_LINUX)
    if (target.isEmpty() || blob.isEmpty())
        return false;
    const auto session = openSecretSession();
    if (!session.has_value())
        return false;

    QVariantMap properties;
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"), target);
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
                      QVariant::fromValue(secretAttributes(target)));
    const ColosseumSecretValue secret{*session, QByteArray(), blob,
                                      QStringLiteral("application/octet-stream")};
    QDBusInterface collection(kSecretServiceName,
                              QStringLiteral("/org/freedesktop/secrets/aliases/default"),
                              QStringLiteral("org.freedesktop.Secret.Collection"),
                              QDBusConnection::sessionBus());
    const QDBusMessage reply = collection.call(QStringLiteral("CreateItem"),
        properties, QVariant::fromValue(secret), true);
    closeSecretSession(*session);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() < 2)
        return false;
    const auto item = qvariant_cast<QDBusObjectPath>(reply.arguments().at(0));
    const auto prompt = qvariant_cast<QDBusObjectPath>(reply.arguments().at(1));
    return !item.path().isEmpty() && prompt.path() == QStringLiteral("/");
#else
    Q_UNUSED(target);
    Q_UNUSED(blob);
    return false;
#endif
}

std::optional<QByteArray> WindowsAccountCredentialStore::readGenericCredential(
    const QString &target) {
#ifdef Q_OS_WIN
    const std::wstring targetWide = target.toStdWString();
    PCREDENTIALW credential = nullptr;
    if (CredReadW(
            targetWide.c_str(),
            CRED_TYPE_GENERIC,
            0,
            &credential) != TRUE) {
        return std::nullopt;
    }

    const QByteArray blob(
        reinterpret_cast<const char *>(credential->CredentialBlob),
        static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return blob;
#elif defined(Q_OS_LINUX)
    const auto session = openSecretSession();
    if (!session.has_value())
        return std::nullopt;
    const auto items = searchSecretItems(target);
    for (const auto &path : items) {
        QDBusInterface item(kSecretServiceName, path.path(),
                            QStringLiteral("org.freedesktop.Secret.Item"),
                            QDBusConnection::sessionBus());
        const QDBusMessage reply = item.call(QStringLiteral("GetSecret"),
                                             QVariant::fromValue(*session));
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
            continue;
        const ColosseumSecretValue secret =
            qdbus_cast<ColosseumSecretValue>(reply.arguments().at(0));
        closeSecretSession(*session);
        return secret.value;
    }
    closeSecretSession(*session);
    return std::nullopt;
#else
    Q_UNUSED(target);
    return std::nullopt;
#endif
}

bool WindowsAccountCredentialStore::deleteGenericCredential(const QString &target) {
#ifdef Q_OS_WIN
    const std::wstring targetWide = target.toStdWString();
    if (CredDeleteW(targetWide.c_str(), CRED_TYPE_GENERIC, 0) == TRUE)
        return true;
    return GetLastError() == ERROR_NOT_FOUND;
#elif defined(Q_OS_LINUX)
    const auto items = searchSecretItems(target);
    if (items.isEmpty())
        return true;
    bool ok = true;
    for (const auto &path : items) {
        QDBusInterface item(kSecretServiceName, path.path(),
                            QStringLiteral("org.freedesktop.Secret.Item"),
                            QDBusConnection::sessionBus());
        const QDBusMessage reply = item.call(QStringLiteral("Delete"));
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
            ok = false;
            continue;
        }
        const auto prompt = qvariant_cast<QDBusObjectPath>(reply.arguments().at(0));
        if (prompt.path() != QStringLiteral("/"))
            ok = false;
    }
    return ok;
#else
    Q_UNUSED(target);
    return false;
#endif
}

QList<QString> WindowsAccountCredentialStore::enumerateTargets(const QString &prefix) {
    QList<QString> targets;
#ifdef Q_OS_WIN
    const std::wstring filterWide = (prefix + QLatin1Char('*')).toStdWString();
    DWORD count = 0;
    PCREDENTIALW *credentials = nullptr;
    if (CredEnumerateW(filterWide.c_str(), 0, &count, &credentials) != TRUE)
        return targets;

    targets.reserve(static_cast<qsizetype>(count));
    for (DWORD index = 0; index < count; ++index) {
        if (!credentials[index] || !credentials[index]->TargetName)
            continue;
        const QString target = QString::fromWCharArray(credentials[index]->TargetName);
        if (target.startsWith(prefix))
            targets.append(target);
    }
    CredFree(static_cast<void *>(credentials));
#elif defined(Q_OS_LINUX)
    const auto items = searchSecretItems();
    for (const auto &path : items) {
        QDBusInterface props(kSecretServiceName, path.path(),
                             QStringLiteral("org.freedesktop.DBus.Properties"),
                             QDBusConnection::sessionBus());
        const QDBusMessage reply = props.call(QStringLiteral("Get"),
            QStringLiteral("org.freedesktop.Secret.Item"),
            QStringLiteral("Attributes"));
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
            continue;
        const QDBusVariant wrapped = qvariant_cast<QDBusVariant>(reply.arguments().at(0));
        const auto attributes = qdbus_cast<QMap<QString, QString>>(wrapped.variant());
        const QString target = attributes.value(QStringLiteral("target"));
        if (target.startsWith(prefix))
            targets.append(target);
    }
#else
    Q_UNUSED(prefix);
#endif
    return targets;
}
