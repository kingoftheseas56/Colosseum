// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountBootstrapStore.h"

#include <QSettings>

namespace {
constexpr auto kLocalOnlyChosenKey = "account/localOnlyChosen";
constexpr auto kOnboardingCompletedKey = "account/onboardingCompleted";
constexpr auto kCredentialClearPendingKey = "account/credentialClearPending";
constexpr auto kRememberedAccountIdKey = "account/rememberedAccountId";
constexpr auto kRememberedUsernameKey = "account/rememberedUsername";
constexpr auto kRememberedAvatarIdKey = "account/rememberedAvatarId";
}

AccountBootstrapStore::AccountBootstrapStore(const QString &settingsPath)
    : m_settingsPath(settingsPath.trimmed()) {}

bool AccountBootstrapStore::localOnlyChosen() const {
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        return settings.value(
            QString::fromLatin1(kLocalOnlyChosenKey),
            false).toBool();
    }

    QSettings settings(m_settingsPath, QSettings::IniFormat);
    return settings.value(
        QString::fromLatin1(kLocalOnlyChosenKey),
        false).toBool();
}

bool AccountBootstrapStore::setLocalOnlyChosen(bool chosen) {
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        settings.setValue(
            QString::fromLatin1(kLocalOnlyChosenKey),
            chosen);
        settings.sync();
        return settings.status() == QSettings::NoError;
    }

    QSettings settings(m_settingsPath, QSettings::IniFormat);
    settings.setValue(
        QString::fromLatin1(kLocalOnlyChosenKey),
        chosen);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool AccountBootstrapStore::onboardingCompleted() const {
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        return settings.value(
            QString::fromLatin1(kOnboardingCompletedKey),
            false).toBool();
    }

    QSettings settings(m_settingsPath, QSettings::IniFormat);
    return settings.value(
        QString::fromLatin1(kOnboardingCompletedKey),
        false).toBool();
}

bool AccountBootstrapStore::setOnboardingCompleted(bool completed) {
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        settings.setValue(
            QString::fromLatin1(kOnboardingCompletedKey),
            completed);
        settings.sync();
        return settings.status() == QSettings::NoError;
    }

    QSettings settings(m_settingsPath, QSettings::IniFormat);
    settings.setValue(
        QString::fromLatin1(kOnboardingCompletedKey),
        completed);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool AccountBootstrapStore::credentialClearPending() const {
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        return settings.value(
            QString::fromLatin1(kCredentialClearPendingKey),
            false).toBool();
    }

    QSettings settings(m_settingsPath, QSettings::IniFormat);
    return settings.value(
        QString::fromLatin1(kCredentialClearPendingKey),
        false).toBool();
}

bool AccountBootstrapStore::setCredentialClearPending(bool pending) {
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        settings.setValue(
            QString::fromLatin1(kCredentialClearPendingKey),
            pending);
        settings.sync();
        return settings.status() == QSettings::NoError;
    }

    QSettings settings(m_settingsPath, QSettings::IniFormat);
    settings.setValue(
        QString::fromLatin1(kCredentialClearPendingKey),
        pending);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QString AccountBootstrapStore::rememberedAccountId() const {
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        return settings.value(QString::fromLatin1(kRememberedAccountIdKey)).toString();
    }
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    return settings.value(QString::fromLatin1(kRememberedAccountIdKey)).toString();
}

QString AccountBootstrapStore::rememberedUsername() const {
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        return settings.value(QString::fromLatin1(kRememberedUsernameKey)).toString();
    }
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    return settings.value(QString::fromLatin1(kRememberedUsernameKey)).toString();
}

QString AccountBootstrapStore::rememberedAvatarId() const {
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        return settings.value(QString::fromLatin1(kRememberedAvatarIdKey)).toString();
    }
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    return settings.value(QString::fromLatin1(kRememberedAvatarIdKey)).toString();
}

bool AccountBootstrapStore::setRememberedIdentity(
    const QString &accountId, const QString &username, const QString &avatarId) {
    auto write = [&](QSettings &settings) {
        settings.setValue(QString::fromLatin1(kRememberedAccountIdKey), accountId.trimmed());
        settings.setValue(QString::fromLatin1(kRememberedUsernameKey), username);
        settings.setValue(QString::fromLatin1(kRememberedAvatarIdKey), avatarId.trimmed());
        settings.sync();
        return settings.status() == QSettings::NoError;
    };
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        return write(settings);
    }
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    return write(settings);
}

bool AccountBootstrapStore::clearRememberedIdentity() {
    auto clear = [&](QSettings &settings) {
        settings.remove(QString::fromLatin1(kRememberedAccountIdKey));
        settings.remove(QString::fromLatin1(kRememberedUsernameKey));
        settings.remove(QString::fromLatin1(kRememberedAvatarIdKey));
        settings.sync();
        return settings.status() == QSettings::NoError;
    };
    if (m_settingsPath.isEmpty()) {
        QSettings settings;
        return clear(settings);
    }
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    return clear(settings);
}

QString AccountBootstrapStore::settingsPath() const {
    return m_settingsPath;
}
