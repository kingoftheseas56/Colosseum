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

    static QString activeKey();
    static QString pendingPrefix();

private:
    static QByteArray encodeCredential(const StoredAccountCredential &credential);
    static std::optional<StoredAccountCredential> decodeCredential(const QByteArray &blob);
    static QString pendingKey(const QByteArray &refreshToken);

    AndroidSecureStorageBackend *m_backend = nullptr;
};
