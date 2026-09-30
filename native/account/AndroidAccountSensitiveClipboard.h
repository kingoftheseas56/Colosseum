#pragma once

#include "AccountSensitiveClipboard.h"

#include <QtGlobal>

class AndroidAccountSensitiveClipboard final
    : public AccountSensitiveClipboard {
public:
    bool copyRecoveryKey(const QString &recoveryKey) override {
        Q_UNUSED(recoveryKey);
        return false;
    }

    bool clearIfTextMatchesDigest(
        const QByteArray &sha256Digest) override {
        Q_UNUSED(sha256Digest);
        return false;
    }
};
