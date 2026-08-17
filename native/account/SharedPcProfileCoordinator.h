#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountProfileCoordinator.h"
#include "FirstAccountProfileCoordinator.h"

#include <QString>

class ProfileStoreRuntime;

class SharedPcProfileCoordinator final
    : public AccountProfileCoordinator {
public:
    explicit SharedPcProfileCoordinator(
        ProfileStoreRuntime *profileRuntime,
        const QString &appDataRoot = QString());

    bool prepareCreatedAccount(
        const QString &accountId,
        QString *error = nullptr) override;

    bool prepareAccountSession(
        const QString &accountId,
        QString *error = nullptr) override;

    bool prepareRememberedAccount(
        const QString &accountId,
        QString *error = nullptr) override;

    bool prepareLocalOnly(
        QString *error = nullptr) override;

    bool sealAccountSession(
        const QString &accountId,
        QString *error = nullptr) override;

private:
    ProfileStoreRuntime *m_profileRuntime = nullptr;
    FirstAccountProfileCoordinator m_firstAccount;
};
