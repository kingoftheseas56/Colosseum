#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QString>

class AccountBootstrapStore {
public:
    explicit AccountBootstrapStore(const QString &settingsPath = QString());

    bool localOnlyChosen() const;
    bool setLocalOnlyChosen(bool chosen);

    bool onboardingCompleted() const;
    bool setOnboardingCompleted(bool completed);

    bool credentialClearPending() const;
    bool setCredentialClearPending(bool pending);

    QString rememberedAccountId() const;
    QString rememberedUsername() const;
    QString rememberedAvatarId() const;
    bool setRememberedIdentity(const QString &accountId, const QString &username, const QString &avatarId);
    bool clearRememberedIdentity();

    QString settingsPath() const;

private:
    QString m_settingsPath;
};
