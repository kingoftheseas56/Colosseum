#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QString>

enum class AccountRecoveryKeyPurpose {
    AccountCreated,
    PasswordRecovered,
    DeviceChallengeRecovered,
    ManualReplacement
};

class AccountOneTimeSecretSink {
public:
    virtual ~AccountOneTimeSecretSink() = default;

    // Transient native handoff. Implementations must not persist the key,
    // write it to logs, or expose it as ordinary AccountController state.
    virtual bool presentRecoveryKey(
        const QString &recoveryKey,
        AccountRecoveryKeyPurpose purpose) = 0;
};
