// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountBootstrapStore.h"

#include <QSettings>

namespace {
constexpr auto kLocalOnlyChosenKey = "account/localOnlyChosen";
constexpr auto kOnboardingCompletedKey = "account/onboardingCompleted";
constexpr auto kCredentialClearPendingKey = "account/credentialClearPending";
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

QString AccountBootstrapStore::settingsPath() const {
    return m_settingsPath;
}
