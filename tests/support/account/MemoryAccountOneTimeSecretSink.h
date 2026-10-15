#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountOneTimeSecretSink.h"

class MemoryAccountOneTimeSecretSink final
    : public AccountOneTimeSecretSink {
public:
    bool presentRecoveryKey(
        const QString &recoveryKey,
        AccountRecoveryKeyPurpose purpose) override {
        if (m_failPresentation
            || recoveryKey.trimmed().isEmpty()) {
            return false;
        }

        clearRecoveryKey();
        m_lastRecoveryKey = recoveryKey;
        m_lastPurpose = purpose;
        ++m_presentCount;
        return true;
    }

    ~MemoryAccountOneTimeSecretSink() override {
        clearRecoveryKey();
    }

    // Non-consuming peek (takeRecoveryKey() clears; tests asserting the
    // delivered sentinel need to look without consuming it).
    QString recoveryKey() const {
        return m_lastRecoveryKey;
    }

    QString takeRecoveryKey() {
        QString value = m_lastRecoveryKey;
        clearRecoveryKey();
        return value;
    }

    AccountRecoveryKeyPurpose lastPurpose() const {
        return m_lastPurpose;
    }

    int presentCount() const {
        return m_presentCount;
    }

    void setFailPresentation(bool fail) {
        m_failPresentation = fail;
    }

private:
    void clearRecoveryKey() {
        if (!m_lastRecoveryKey.isEmpty())
            m_lastRecoveryKey.fill(QLatin1Char('\0'));
        m_lastRecoveryKey.clear();
    }

    QString m_lastRecoveryKey;
    AccountRecoveryKeyPurpose m_lastPurpose =
        AccountRecoveryKeyPurpose::AccountCreated;
    int m_presentCount = 0;
    bool m_failPresentation = false;
};
