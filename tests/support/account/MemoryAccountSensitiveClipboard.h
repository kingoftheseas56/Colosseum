#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountSensitiveClipboard.h"
#include "account/WindowsAccountSensitiveClipboard.h"

class MemoryAccountSensitiveClipboard final
    : public AccountSensitiveClipboard {
public:
    ~MemoryAccountSensitiveClipboard() override {
        clearCurrentText();
    }

    bool copyRecoveryKey(
        const QString &recoveryKey) override {
        ++m_copyCount;
        if (m_failCopy)
            return false;

        clearCurrentText();
        m_currentText = recoveryKey;
        return true;
    }

    bool clearIfTextMatchesDigest(
        const QByteArray &sha256Digest) override {
        ++m_clearAttemptCount;
        if (m_failClear)
            return false;

        if (WindowsAccountSensitiveClipboard::textDigest(
                m_currentText)
            != sha256Digest) {
            return false;
        }

        clearCurrentText();
        ++m_clearCount;
        return true;
    }

    QString currentText() const {
        return m_currentText;
    }

    void replaceCurrentText(
        const QString &text) {
        clearCurrentText();
        m_currentText = text;
    }

    int copyCount() const {
        return m_copyCount;
    }

    int clearAttemptCount() const {
        return m_clearAttemptCount;
    }

    int clearCount() const {
        return m_clearCount;
    }

    void setFailCopy(bool fail) {
        m_failCopy = fail;
    }

    void setFailClear(bool fail) {
        m_failClear = fail;
    }

private:
    void clearCurrentText() {
        if (!m_currentText.isEmpty())
            m_currentText.fill(QLatin1Char('\0'));
        m_currentText.clear();
    }

    QString m_currentText;
    int m_copyCount = 0;
    int m_clearAttemptCount = 0;
    int m_clearCount = 0;
    bool m_failCopy = false;
    bool m_failClear = false;
};
