// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountDeviceIdentity.h"

#include <QSettings>
#include <QUuid>

#include <memory>

namespace {
constexpr auto kInstallIdKey = "account/deviceInstallId";

QString platformName() {
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("Linux");
#else
    return QStringLiteral("Desktop");
#endif
}
}

AccountDeviceIdentity::AccountDeviceIdentity(const QString &settingsPath)
    : m_settingsPath(settingsPath.trimmed()) {}

QString AccountDeviceIdentity::installId() {
    std::unique_ptr<QSettings> settings;
    if (m_settingsPath.isEmpty()) {
        settings = std::make_unique<QSettings>();
    } else {
        settings = std::make_unique<QSettings>(
            m_settingsPath,
            QSettings::IniFormat);
    }

    const QString existing = settings->value(
        QString::fromLatin1(kInstallIdKey)).toString().trimmed();
    const QUuid existingUuid(existing);
    if (!existingUuid.isNull())
        return existingUuid.toString(QUuid::WithoutBraces).toLower();

    const QString created = QUuid::createUuid()
        .toString(QUuid::WithoutBraces)
        .toLower();
    settings->setValue(QString::fromLatin1(kInstallIdKey), created);
    settings->sync();
    return created;
}

QString AccountDeviceIdentity::label() const {
    return platformName() + QStringLiteral(" desktop");
}

QString AccountDeviceIdentity::platform() const {
    return platformName();
}
