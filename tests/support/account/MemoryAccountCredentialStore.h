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

    QList<StoredAccountDeletion> pendingDeletions() const override {
        if (!m_available)
            return {};
        return m_deletions;
    }

    bool savePendingDeletion(const StoredAccountDeletion &deletion) override {
        if (!m_available || m_failPendingWrites
            || deletion.accountId.isEmpty() || deletion.requestId.isEmpty()
            || deletion.retryCapability.isEmpty())
            return false;
        for (StoredAccountDeletion &existing : m_deletions) {
            if (existing.requestId == deletion.requestId) {
                if (existing.accountId != deletion.accountId
                    || existing.retryCapability != deletion.retryCapability)
                    return false;
                return true;
            }
        }
        m_deletions.append(deletion);
        return true;
    }

    bool removePendingDeletion(const QString &requestId) override {
        if (!m_available)
            return false;
        for (qsizetype index = 0; index < m_deletions.size(); ++index) {
            if (m_deletions.at(index).requestId == requestId) {
                m_deletions.removeAt(index);
                return true;
            }
        }
        return false;
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
    QList<StoredAccountDeletion> m_deletions;
    bool m_available = true;
    bool m_failWrites = false;
    bool m_failPendingWrites = false;
};
