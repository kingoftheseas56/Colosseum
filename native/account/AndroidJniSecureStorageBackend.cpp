#include "AndroidJniSecureStorageBackend.h"

#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QJniObject>

namespace {
constexpr auto kSecureStoreClass = "org/colosseum/platform/SecureCredentialStore";

QJniObject androidContext() {
    return QNativeInterface::QAndroidApplication::context();
}

QJniObject javaString(const QString &value) {
    return QJniObject::fromString(value);
}
} // namespace
#endif

bool AndroidJniSecureStorageBackend::isAvailable() const {
#if defined(Q_OS_ANDROID)
    const QJniObject context = androidContext();
    if (!context.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(
        kSecureStoreClass, "isAvailable", "(Landroid/content/Context;)Z",
        context.object<jobject>());
#else
    return false;
#endif
}

std::optional<QByteArray> AndroidJniSecureStorageBackend::read(
    const QString &key) const {
#if defined(Q_OS_ANDROID)
    const QJniObject context = androidContext();
    if (!context.isValid() || key.trimmed().isEmpty())
        return std::nullopt;
    const QJniObject javaKey = javaString(key);
    const QJniObject result = QJniObject::callStaticObjectMethod(
        kSecureStoreClass, "read",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        context.object<jobject>(), javaKey.object<jstring>());
    if (!result.isValid())
        return std::nullopt;
    return QByteArray::fromBase64(result.toString().toLatin1());
#else
    Q_UNUSED(key)
    return std::nullopt;
#endif
}

bool AndroidJniSecureStorageBackend::write(
    const QString &key, const QByteArray &value) {
#if defined(Q_OS_ANDROID)
    const QJniObject context = androidContext();
    if (!context.isValid() || key.trimmed().isEmpty() || value.isEmpty())
        return false;
    const QJniObject javaKey = javaString(key);
    const QJniObject javaValue = javaString(
        QString::fromLatin1(value.toBase64()));
    return QJniObject::callStaticMethod<jboolean>(
        kSecureStoreClass, "write",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        context.object<jobject>(), javaKey.object<jstring>(),
        javaValue.object<jstring>());
#else
    Q_UNUSED(key)
    Q_UNUSED(value)
    return false;
#endif
}

bool AndroidJniSecureStorageBackend::remove(const QString &key) {
#if defined(Q_OS_ANDROID)
    const QJniObject context = androidContext();
    if (!context.isValid() || key.trimmed().isEmpty())
        return false;
    const QJniObject javaKey = javaString(key);
    return QJniObject::callStaticMethod<jboolean>(
        kSecureStoreClass, "remove",
        "(Landroid/content/Context;Ljava/lang/String;)Z",
        context.object<jobject>(), javaKey.object<jstring>());
#else
    Q_UNUSED(key)
    return false;
#endif
}

QStringList AndroidJniSecureStorageBackend::keys(const QString &prefix) const {
#if defined(Q_OS_ANDROID)
    const QJniObject context = androidContext();
    if (!context.isValid())
        return {};
    const QJniObject javaPrefix = javaString(prefix);
    const QJniObject result = QJniObject::callStaticObjectMethod(
        kSecureStoreClass, "keys",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        context.object<jobject>(), javaPrefix.object<jstring>());
    if (!result.isValid() || result.toString().isEmpty())
        return {};
    return result.toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    Q_UNUSED(prefix)
    return {};
#endif
}
