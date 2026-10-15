#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QByteArray>
#include <QString>

class AccountSensitiveClipboard {
public:
    virtual ~AccountSensitiveClipboard() = default;

    virtual bool copyRecoveryKey(const QString &recoveryKey) = 0;

    // Clears only when the clipboard still contains text with the supplied
    // SHA-256 digest. Newer clipboard content is never erased.
    virtual bool clearIfTextMatchesDigest(
        const QByteArray &sha256Digest) = 0;
};
