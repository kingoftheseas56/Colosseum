#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QString>

class AccountProfileCoordinator {
public:
    virtual ~AccountProfileCoordinator() = default;

    virtual bool prepareCreatedAccount(
        const QString &accountId,
        QString *error = nullptr) = 0;

    virtual bool prepareAccountSession(
        const QString &accountId,
        QString *error = nullptr) = 0;

    virtual bool attachLocalProfileToAccount(
        const QString &accountId,
        QString *error = nullptr) {
        return prepareAccountSession(accountId, error);
    }

    virtual bool prepareRememberedAccount(
        const QString &accountId,
        QString *error = nullptr) = 0;

    virtual bool prepareLocalOnly(
        QString *error = nullptr) = 0;

    virtual bool sealAccountSession(
        const QString &accountId,
        QString *error = nullptr) = 0;
};
