// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfilePreferencesStore.h"

#include <QSettings>

namespace {
constexpr auto kShowExplicitKey =
    "content/showExplicit";
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

void ProfilePreferencesStore::
setShowExplicit(
    bool showExplicitValue) {
    commitShowExplicit(
        showExplicitValue,
        true);
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
}
