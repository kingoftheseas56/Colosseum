#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountSensitiveClipboard.h"

class WindowsAccountSensitiveClipboard final
    : public AccountSensitiveClipboard {
public:
    bool copyRecoveryKey(const QString &recoveryKey) override;
    bool clearIfTextMatchesDigest(
        const QByteArray &sha256Digest) override;

    static QByteArray textDigest(const QString &text);
};
