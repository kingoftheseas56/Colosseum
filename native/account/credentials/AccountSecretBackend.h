#pragma once

#include <QByteArray>
#include <QStringList>
#include <memory>
#include <optional>

// Opaque account-owned blobs only. Implementations must never fall back to
// plaintext storage. Missing entries may be deleted successfully.
class AccountSecretBackend {
public:
    virtual ~AccountSecretBackend() = default;
    virtual bool isAvailable() const = 0;
    virtual bool write(const QString &target, const QByteArray &blob) = 0;
    virtual std::optional<QByteArray> read(const QString &target) const = 0;
    virtual bool remove(const QString &target) = 0;
    virtual QStringList targets(const QString &prefix) const = 0;
};

std::shared_ptr<AccountSecretBackend> createAccountSecretBackend();

#ifdef Q_OS_ANDROID
class AndroidSecureStorageBackend;
// Preserve sessions from the earlier Android vault during first token rotation.
std::shared_ptr<AccountSecretBackend> createAndroidAccountSecretBackend(AndroidSecureStorageBackend *legacy);
#endif
