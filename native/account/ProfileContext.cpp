// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfileContext.h"

ProfileContext::ProfileContext(QObject *parent)
    : QObject(parent),
      m_active(ProfilePaths::sealed()) {}

const ProfilePaths &ProfileContext::activeProfile() const {
    return m_active;
}

quint64 ProfileContext::revision() const {
    return m_revision;
}

void ProfileContext::activateSealed(
    const QString &appDataRoot) {
    replace(ProfilePaths::sealed(appDataRoot));
}

void ProfileContext::activateLegacyLocal() {
    replace(ProfilePaths::legacyLocal());
}

void ProfileContext::activateLocalOnly(const QString &appDataRoot) {
    replace(ProfilePaths::localOnly(appDataRoot));
}

bool ProfileContext::activateAccount(const QString &accountId,
                                     const QString &appDataRoot) {
    const auto paths = ProfilePaths::account(accountId, appDataRoot);
    if (!paths.has_value())
        return false;

    replace(*paths);
    return true;
}

void ProfileContext::replace(const ProfilePaths &paths) {
    if (m_active.kind() == paths.kind()
        && m_active.profileId() == paths.profileId()
        && m_active.appDataRoot() == paths.appDataRoot()
        && m_active.profileRoot() == paths.profileRoot()) {
        return;
    }

    m_active = paths;
    ++m_revision;
    emit changed();
}
