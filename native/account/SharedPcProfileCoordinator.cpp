// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SharedPcProfileCoordinator.h"

#include "ProfileStoreRuntime.h"

SharedPcProfileCoordinator::SharedPcProfileCoordinator(
    ProfileStoreRuntime *profileRuntime,
    const QString &appDataRoot)
    : m_profileRuntime(profileRuntime),
      m_firstAccount(
          profileRuntime,
          appDataRoot) {
    Q_ASSERT(m_profileRuntime);
}

bool SharedPcProfileCoordinator::prepareCreatedAccount(
    const QString &accountId,
    QString *error) {
    return m_firstAccount.prepareCreatedAccount(
        accountId,
        error);
}

bool SharedPcProfileCoordinator::prepareAccountSession(
    const QString &accountId,
    QString *error) {
    return m_firstAccount.prepareAccountSession(
        accountId,
        error);
}

bool SharedPcProfileCoordinator::attachLocalProfileToAccount(
    const QString &accountId,
    QString *error) {
    return m_firstAccount.attachLocalProfileToAccount(
        accountId,
        error);
}

bool SharedPcProfileCoordinator::prepareRememberedAccount(
    const QString &accountId,
    QString *error) {
    return m_firstAccount.prepareRememberedAccount(
        accountId,
        error);
}

bool SharedPcProfileCoordinator::prepareLocalOnly(
    QString *error) {
    return m_firstAccount.prepareLocalOnly(
        error);
}

bool SharedPcProfileCoordinator::sealAccountSession(
    const QString &accountId,
    QString *error) {
    return m_profileRuntime->sealAccountProfile(
        accountId,
        error);
}
