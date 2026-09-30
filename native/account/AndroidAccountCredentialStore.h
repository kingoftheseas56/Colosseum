#pragma once

#include "AccountCredentialStore.h"
#include "AndroidSecureStorageBackend.h"

class AndroidAccountCredentialStore final : public AccountCredentialStore {
public:
    explicit AndroidAccountCredentialStore(AndroidSecureStorageBackend *backend = nullptr);

    bool isAvailable() const override;
    std::optional<StoredAccountCredential> loadActive() const override;
    bool saveActive(const StoredAccountCredential &credential) override;
    bool clearActive() override;
    QList<QByteArray> pendingRevocations() const override;
    bool addPendingRevocation(const QByteArray &refreshToken) override;
    bool removePendingRevocation(const QByteArray &refreshToken) override;
    // Android does not yet persist account-deletion retries; fail closed.
    QList<StoredAccountDeletion> pendingDeletions() const override { return {}; }
    bool savePendingDeletion(const StoredAccountDeletion &) override { return false; }
    bool removePendingDeletion(const QString &) override { return false; }

    static QString activeKey();
    static QString pendingPrefix();

private:
    static QByteArray encodeCredential(const StoredAccountCredential &credential);
    static std::optional<StoredAccountCredential> decodeCredential(const QByteArray &blob);
    static QString pendingKey(const QByteArray &refreshToken);

    AndroidSecureStorageBackend *m_backend = nullptr;
};
