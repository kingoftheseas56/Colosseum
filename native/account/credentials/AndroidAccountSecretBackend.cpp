#include "AccountSecretBackend.h"
#include "../AndroidSecureStorageBackend.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QCoreApplication>

namespace {
constexpr auto kAlias = "org.brotherhood.colosseum.account.v1";
constexpr auto kSuffix = ".secret";

QJniObject bytes(const QByteArray &value) {
    QJniEnvironment env;
    jbyteArray array = env->NewByteArray(static_cast<jsize>(value.size()));
    if (!array || env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent))
        return {};
    env->SetByteArrayRegion(array, 0, static_cast<jsize>(value.size()),
                            reinterpret_cast<const jbyte *>(value.constData()));
    return QJniObject::fromLocalRef(array);
}

QByteArray bytes(const QJniObject &value) {
    if (!value.isValid())
        return {};
    QJniEnvironment env;
    const auto array = value.object<jbyteArray>();
    const jsize size = env->GetArrayLength(array);
    QByteArray result(size, Qt::Uninitialized);
    env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte *>(result.data()));
    if (env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent))
        return {};
    return result;
}

QJniObject strings(const QString &value) {
    QJniEnvironment env;
    jclass type = env->FindClass("java/lang/String");
    if (!type)
        return {};
    const auto text = QJniObject::fromString(value);
    jobjectArray array = env->NewObjectArray(1, type, text.object());
    env->DeleteLocalRef(type);
    return QJniObject::fromLocalRef(array);
}

QString storageDirectory() {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const auto directory = context.callObjectMethod("getNoBackupFilesDir", "()Ljava/io/File;");
    if (!directory.isValid())
        return {};
    const QString path = directory.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString();
    return path.isEmpty() ? QString() : path + QStringLiteral("/colosseum-account-secrets-v1");
}

QString entryName(const QString &target) {
    return QString::fromLatin1(QCryptographicHash::hash(target.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString legacyKey(const QString &target) {
    const QString active = QStringLiteral("Brotherhood.Colosseum.Account.Active.v1");
    const QString pending = QStringLiteral("Brotherhood.Colosseum.Account.PendingRevoke.v1.");
    if (target == active || target.startsWith(active + QLatin1Char('.')))
        return QStringLiteral("colosseum.account.active.v1") + target.mid(active.size());
    if (target.startsWith(pending))
        return QStringLiteral("colosseum.account.pending-revocation.v1.") + target.mid(pending.size());
    return {};
}

QString currentTarget(const QString &legacy) {
    const QString pending = QStringLiteral("colosseum.account.pending-revocation.v1.");
    return legacy.startsWith(pending)
        ? QStringLiteral("Brotherhood.Colosseum.Account.PendingRevoke.v1.") + legacy.mid(pending.size())
        : QString();
}

QJniObject key(const QString &directory) {
    const auto provider = QJniObject::fromString(QStringLiteral("AndroidKeyStore"));
    const auto alias = QJniObject::fromString(QString::fromLatin1(kAlias));
    const auto store = QJniObject::callStaticObjectMethod("java/security/KeyStore", "getInstance",
        "(Ljava/lang/String;)Ljava/security/KeyStore;", provider.object<jstring>());
    if (!store.isValid())
        return {};
    store.callMethod<void>("load", "(Ljava/io/InputStream;[C)V", nullptr, nullptr);
    const bool exists = store.callMethod<jboolean>("containsAlias", "(Ljava/lang/String;)Z", alias.object<jstring>());
    if (exists)
        return store.callObjectMethod("getKey", "(Ljava/lang/String;[C)Ljava/security/Key;", alias.object<jstring>(), nullptr);
    // Never replace a lost/invalidated key while ciphertext still exists.
    if (!QDir(directory).entryList({QStringLiteral("*.secret")}, QDir::Files).isEmpty())
        return {};
    const auto algorithm = QJniObject::fromString(QStringLiteral("AES"));
    const auto generator = QJniObject::callStaticObjectMethod("javax/crypto/KeyGenerator", "getInstance",
        "(Ljava/lang/String;Ljava/lang/String;)Ljavax/crypto/KeyGenerator;",
        algorithm.object<jstring>(), provider.object<jstring>());
    QJniObject builder("android/security/keystore/KeyGenParameterSpec$Builder", "(Ljava/lang/String;I)V",
        alias.object<jstring>(), jint(3)); // PURPOSE_ENCRYPT | PURPOSE_DECRYPT
    const auto modes = strings(QStringLiteral("GCM"));
    const auto padding = strings(QStringLiteral("NoPadding"));
    builder.callObjectMethod("setBlockModes", "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;", modes.object<jobjectArray>());
    builder.callObjectMethod("setEncryptionPaddings", "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;", padding.object<jobjectArray>());
    builder.callObjectMethod("setKeySize", "(I)Landroid/security/keystore/KeyGenParameterSpec$Builder;", jint(256));
    const auto spec = builder.callObjectMethod("build", "()Landroid/security/keystore/KeyGenParameterSpec;");
    if (!generator.isValid() || !spec.isValid())
        return {};
    generator.callMethod<void>("init", "(Ljava/security/spec/AlgorithmParameterSpec;)V", spec.object());
    return generator.callObjectMethod("generateKey", "()Ljavax/crypto/SecretKey;");
}

QJniObject cipher() {
    const auto transformation = QJniObject::fromString(QStringLiteral("AES/GCM/NoPadding"));
    return QJniObject::callStaticObjectMethod("javax/crypto/Cipher", "getInstance",
        "(Ljava/lang/String;)Ljavax/crypto/Cipher;", transformation.object<jstring>());
}

class AndroidAccountSecretBackend final : public AccountSecretBackend {
public:
    explicit AndroidAccountSecretBackend(AndroidSecureStorageBackend *legacy = nullptr) : m_legacy(legacy) {}

    bool isAvailable() const override {
        const QString directory = storageDirectory();
        if (directory.isEmpty() || !QDir().mkpath(directory))
            return false;
        return key(directory).isValid() && (!m_legacy || m_legacy->isAvailable());
    }

    bool write(const QString &target, const QByteArray &blob) override {
        if (target.isEmpty() || blob.isEmpty() || !isAvailable())
            return false;
        const QString directory = storageDirectory();
        const QString name = entryName(target);
        const auto secretKey = key(directory);
        const auto encryptor = cipher();
        if (!secretKey.isValid() || !encryptor.isValid())
            return false;
        encryptor.callMethod<void>("init", "(ILjava/security/Key;)V", jint(1), secretKey.object());
        // Bind ciphertext to its file name. Moving an entry to another target
        // cannot turn it into that target's credential.
        const auto aad = bytes(name.toLatin1());
        encryptor.callMethod<void>("updateAAD", "([B)V", aad.object<jbyteArray>());
        const QByteArray record = QJsonDocument(QJsonObject{
            {QStringLiteral("target"), target},
            {QStringLiteral("blob"), QString::fromLatin1(blob.toBase64())}}).toJson(QJsonDocument::Compact);
        const auto input = bytes(record);
        const auto encrypted = encryptor.callObjectMethod("doFinal", "([B)[B", input.object<jbyteArray>());
        const QByteArray payload = bytes(encrypted);
        const QByteArray iv = bytes(encryptor.callObjectMethod("getIV", "()[B"));
        if (payload.isEmpty() || iv.size() != 12)
            return false;
        const QByteArray envelope = QJsonDocument(QJsonObject{
            {QStringLiteral("version"), 1},
            {QStringLiteral("iv"), QString::fromLatin1(iv.toBase64())},
            {QStringLiteral("ciphertext"), QString::fromLatin1(payload.toBase64())}}).toJson(QJsonDocument::Compact);
        QSaveFile file(directory + QLatin1Char('/') + name + QLatin1String(kSuffix));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)
            || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
            return false;
        if (file.write(envelope) != envelope.size() || !file.commit())
            return false;
        // Commit the replacement before retiring the old session. If retirement
        // fails, report failure so the controller retains its storage tombstone.
        const QString oldKey = legacyKey(target);
        return !m_legacy || oldKey.isEmpty() || m_legacy->remove(oldKey);
    }

    std::optional<QByteArray> read(const QString &target) const override {
        const auto record = readRecord(entryName(target));
        if (!record && m_legacy && !legacyKey(target).isEmpty()) {
            const QString path = storageDirectory() + QLatin1Char('/') + entryName(target) + QLatin1String(kSuffix);
            // A corrupt new record must never resurrect an older session.
            if (!QFile::exists(path) && m_legacy->isAvailable())
                return m_legacy->read(legacyKey(target));
        }
        if (!record || record->value(QStringLiteral("target")).toString() != target)
            return std::nullopt;
        const auto decoded = QByteArray::fromBase64Encoding(record->value(QStringLiteral("blob")).toString().toLatin1(),
            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.isEmpty())
            return std::nullopt;
        return decoded.decoded;
    }

    bool remove(const QString &target) override {
        const QString directory = storageDirectory();
        if (directory.isEmpty())
            return false;
        const QString oldKey = legacyKey(target);
        if (m_legacy && !oldKey.isEmpty() && !m_legacy->remove(oldKey))
            return false;
        const QString path = directory + QLatin1Char('/') + entryName(target) + QLatin1String(kSuffix);
        return !QFile::exists(path) || QFile::remove(path);
    }

    QStringList targets(const QString &prefix) const override {
        QStringList result;
        const QString directory = storageDirectory();
        if (directory.isEmpty())
            return result;
        for (const QString &file : QDir(directory).entryList({QStringLiteral("*.secret")}, QDir::Files)) {
            const auto record = readRecord(file.chopped(7));
            if (record) {
                const QString target = record->value(QStringLiteral("target")).toString();
                if (target.startsWith(prefix))
                    result.append(target);
            }
        }
        const QString oldPrefix = legacyKey(prefix);
        if (m_legacy && !oldPrefix.isEmpty() && m_legacy->isAvailable()) {
            for (const QString &oldKey : m_legacy->keys(oldPrefix)) {
                const QString target = currentTarget(oldKey);
                if (target.startsWith(prefix))
                    result.append(target);
            }
        }
        result.removeDuplicates();
        return result;
    }

private:
    AndroidSecureStorageBackend *m_legacy = nullptr;
    std::optional<QJsonObject> readRecord(const QString &name) const {
        const QString directory = storageDirectory();
        if (directory.isEmpty())
            return std::nullopt;
        QFile file(directory + QLatin1Char('/') + name + QLatin1String(kSuffix));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
            return std::nullopt;
        const auto envelope = QJsonDocument::fromJson(file.readAll()).object();
        if (envelope.value(QStringLiteral("version")).toInt() != 1)
            return std::nullopt;
        const QByteArray iv = QByteArray::fromBase64(envelope.value(QStringLiteral("iv")).toString().toLatin1());
        const QByteArray payload = QByteArray::fromBase64(envelope.value(QStringLiteral("ciphertext")).toString().toLatin1());
        if (iv.size() != 12 || payload.size() < 16)
            return std::nullopt;
        const auto secretKey = key(directory);
        const auto decryptor = cipher();
        if (!secretKey.isValid() || !decryptor.isValid())
            return std::nullopt;
        const auto ivArray = bytes(iv);
        QJniObject parameters("javax/crypto/spec/GCMParameterSpec", "(I[B)V", jint(128), ivArray.object<jbyteArray>());
        decryptor.callMethod<void>("init", "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
            jint(2), secretKey.object(), parameters.object());
        const auto aad = bytes(name.toLatin1());
        decryptor.callMethod<void>("updateAAD", "([B)V", aad.object<jbyteArray>());
        const auto input = bytes(payload);
        const auto plaintext = decryptor.callObjectMethod("doFinal", "([B)[B", input.object<jbyteArray>());
        if (!plaintext.isValid())
            return std::nullopt;
        const auto record = QJsonDocument::fromJson(bytes(plaintext)).object();
        if (entryName(record.value(QStringLiteral("target")).toString()) != name)
            return std::nullopt;
        return record;
    }
};
}

std::shared_ptr<AccountSecretBackend> createAccountSecretBackend() {
    return std::make_shared<AndroidAccountSecretBackend>();
}

std::shared_ptr<AccountSecretBackend> createAndroidAccountSecretBackend(AndroidSecureStorageBackend *legacy) {
    return std::make_shared<AndroidAccountSecretBackend>(legacy);
}
