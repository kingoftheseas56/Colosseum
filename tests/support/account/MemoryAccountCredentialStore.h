#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountCredentialStore.h"

class MemoryAccountCredentialStore final
    : public AccountCredentialStore {
public:
    bool isAvailable() const override {
        return m_available;
    }

    std::optional<StoredAccountCredential>
    loadActive() const override {
        if (!m_available)
            return std::nullopt;
        return m_active;
    }

    bool saveActive(
        const StoredAccountCredential &credential) override {
        if (!m_available || m_failWrites)
            return false;
        m_active = credential;
        return true;
    }

    bool clearActive() override {
        if (!m_available)
            return false;
        m_active.reset();
        return true;
    }

    QList<QByteArray>
    pendingRevocations() const override {
        if (!m_available)
            return {};
        return m_pending;
    }

    bool addPendingRevocation(
        const QByteArray &refreshToken) override {
        if (!m_available
            || m_failPendingWrites
            || refreshToken.isEmpty()) {
            return false;
        }

        if (!m_pending.contains(refreshToken))
            m_pending.append(refreshToken);
        return true;
    }

    bool removePendingRevocation(
        const QByteArray &refreshToken) override {
        if (!m_available)
            return false;
        return m_pending.removeAll(refreshToken) > 0;
    }

    void setAvailable(bool available) {
        m_available = available;
    }

    void setFailWrites(bool fail) {
        m_failWrites = fail;
    }

    void setFailPendingWrites(bool fail) {
        m_failPendingWrites = fail;
    }

private:
    std::optional<StoredAccountCredential> m_active;
    QList<QByteArray> m_pending;
    bool m_available = true;
    bool m_failWrites = false;
    bool m_failPendingWrites = false;
};
