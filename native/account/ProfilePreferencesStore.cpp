// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfilePreferencesStore.h"

#include <QSettings>

namespace {
constexpr auto kShowExplicitKey =
    "content/showExplicit";
constexpr auto kRememberSearchHistoryKey = "privacy/rememberSearchHistory";
constexpr auto kKeepActivityHistoryKey = "privacy/keepActivityHistory";
constexpr auto kSyncActivityHistoryKey = "privacy/syncActivityHistory";
}

ProfilePreferencesStore::
ProfilePreferencesStore(
    QObject *parent)
    : QObject(parent),
      m_settings(
          std::make_unique<QSettings>()) {
    setObjectName(
        QStringLiteral(
            "profilePreferencesStore"));
    load();
}

ProfilePreferencesStore::
ProfilePreferencesStore(
    const QString &iniPath,
    QObject *parent)
    : QObject(parent),
      m_settings(
          std::make_unique<QSettings>(
              iniPath,
              QSettings::IniFormat)) {
    setObjectName(
        QStringLiteral(
            "profilePreferencesStore"));
    load();
}

bool ProfilePreferencesStore::
showExplicit() const {
    return m_showExplicit;
}

bool ProfilePreferencesStore::
hasShowExplicitValue() const {
    return m_hasShowExplicitValue;
}

int ProfilePreferencesStore::
revision() const {
    return m_revision;
}

bool ProfilePreferencesStore::rememberSearchHistory() const { return m_rememberSearchHistory; }
bool ProfilePreferencesStore::keepActivityHistory() const { return m_keepActivityHistory; }
bool ProfilePreferencesStore::syncActivityHistory() const { return m_syncActivityHistory; }

void ProfilePreferencesStore::
setShowExplicit(
    bool showExplicitValue) {
    commitShowExplicit(
        showExplicitValue,
        true);
}

void ProfilePreferencesStore::setRememberSearchHistory(bool enabled) {
    if (m_rememberSearchHistory == enabled)
        return;
    m_settings->setValue(QString::fromLatin1(kRememberSearchHistoryKey), enabled);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError)
        return;
    m_rememberSearchHistory = enabled;
    ++m_revision;
    emit rememberSearchHistoryChanged();
    emit changed();
}

void ProfilePreferencesStore::setKeepActivityHistory(bool enabled) {
    if (m_keepActivityHistory == enabled)
        return;
    m_settings->setValue(QString::fromLatin1(kKeepActivityHistoryKey), enabled);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError)
        return;
    m_keepActivityHistory = enabled;
    ++m_revision;
    emit keepActivityHistoryChanged();
    emit changed();
}

void ProfilePreferencesStore::setSyncActivityHistory(bool enabled) {
    if (m_syncActivityHistory == enabled)
        return;
    m_settings->setValue(QString::fromLatin1(kSyncActivityHistoryKey), enabled);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError)
        return;
    m_syncActivityHistory = enabled;
    ++m_revision;
    emit syncActivityHistoryChanged();
    emit changed();
}

bool ProfilePreferencesStore::
applySyncedShowExplicit(
    bool showExplicitValue) {
    return commitShowExplicit(
        showExplicitValue,
        false);
}

bool ProfilePreferencesStore::
clearSyncedShowExplicit() {
    if (!m_hasShowExplicitValue
        && !m_showExplicit) {
        return true;
    }

    m_settings->remove(
        QString::fromLatin1(
            kShowExplicitKey));
    m_settings->sync();

    if (m_settings->status()
        != QSettings::NoError) {
        return false;
    }

    const bool visibleChanged =
        m_showExplicit;

    m_showExplicit = false;
    m_hasShowExplicitValue = false;
    ++m_revision;

    if (visibleChanged)
        emit showExplicitChanged();

    emit changed();
    return true;
}

bool ProfilePreferencesStore::
commitShowExplicit(
    bool showExplicitValue,
    bool localMutation) {
    if (m_showExplicit
            == showExplicitValue
        && m_hasShowExplicitValue) {
        return true;
    }

    m_settings->setValue(
        QString::fromLatin1(
            kShowExplicitKey),
        showExplicitValue);
    m_settings->sync();

    if (m_settings->status()
        != QSettings::NoError) {
        return false;
    }

    const bool visibleChanged =
        m_showExplicit
        != showExplicitValue;

    m_showExplicit =
        showExplicitValue;
    m_hasShowExplicitValue = true;
    ++m_revision;

    if (visibleChanged)
        emit showExplicitChanged();
    emit changed();

    if (localMutation)
        emit syncDirty();

    return true;
}

void ProfilePreferencesStore::load() {
    m_hasShowExplicitValue =
        m_settings->contains(
            QString::fromLatin1(
                kShowExplicitKey));

    m_showExplicit =
        m_settings
            ->value(
                QString::fromLatin1(
                    kShowExplicitKey),
                false)
            .toBool();
    m_rememberSearchHistory = m_settings->value(QString::fromLatin1(kRememberSearchHistoryKey), true).toBool();
    m_keepActivityHistory = m_settings->value(QString::fromLatin1(kKeepActivityHistoryKey), true).toBool();
    m_syncActivityHistory = m_settings->value(QString::fromLatin1(kSyncActivityHistoryKey), true).toBool();
}
