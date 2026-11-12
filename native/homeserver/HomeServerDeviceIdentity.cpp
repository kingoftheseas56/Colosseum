#include "HomeServerDeviceIdentity.h"
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#endif
QString HomeServerDeviceIdentity::defaultDirectory() {
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    const auto context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid() || env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
    const auto files = context.callObjectMethod("getNoBackupFilesDir", "()Ljava/io/File;");
    if (!files.isValid() || env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
    const auto path = files.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;");
    if (!path.isValid() || env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
    return path.toString() + "/home-servers";
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base.isEmpty() ? QString() : base + "/home-servers";
#endif
}
QString HomeServerDeviceIdentity::deviceName() {
#if defined(Q_OS_ANDROID)
    return "Colosseum (Android)";
#elif defined(Q_OS_WIN)
    return "Colosseum (Windows)";
#elif defined(Q_OS_MACOS)
    return "Colosseum (macOS)";
#else
    return "Colosseum (Linux)";
#endif
}
HomeServerDeviceIdentity::HomeServerDeviceIdentity(QString directory) {
    m_error = "Installation identity storage unavailable";
    if (directory.isEmpty()) directory = defaultDirectory();
    if (directory.isEmpty() || !QDir().mkpath(directory)) return;
    QLockFile lock(QDir(directory).filePath("installation-id.lock"));
    if (!lock.tryLock(0)) { m_error = "Installation identity is locked"; return; }
    const QString path = QDir(directory).filePath("installation-id");
    auto read = [&]() -> QString {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64) return {};
        const QString id = QString::fromLatin1(file.readAll()).trimmed();
        const QUuid uuid(id);
        return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id ? id : QString();
    };
    if (QFile::exists(path)) {
        // Requiring read/write prevents accidentally accepting a damaged or read-only installation store.
        QFile existing(path);
        if (!existing.open(QIODevice::ReadWrite)) { m_error = "Installation identity is not writable"; return; }
        existing.close();
        m_id = read();
        if (m_id.isEmpty()) m_error = "Invalid installation identity";
    } else {
        const QString generated = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(generated.toLatin1()) != generated.size() || !file.commit()) { m_error = "Installation identity save failed"; return; }
        m_id = read();
        if (m_id != generated) { m_id.clear(); m_error = "Installation identity read-back failed"; }
    }
    if (!m_id.isEmpty()) m_error.clear();
}
