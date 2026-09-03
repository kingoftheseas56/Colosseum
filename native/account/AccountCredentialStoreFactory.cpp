#include "AccountCredentialStoreFactory.h"

#if defined(Q_OS_ANDROID)
#include "AndroidAccountCredentialStore.h"
#include "AndroidJniSecureStorageBackend.h"
#elif defined(Q_OS_WIN)
#include "WindowsAccountCredentialStore.h"
#endif

namespace {

#if defined(Q_OS_ANDROID)
AndroidSecureStorageBackend *defaultAndroidSecureStorage() {
    static AndroidJniSecureStorageBackend backend;
    return &backend;
}
#endif

class UnavailableAccountCredentialStore final : public AccountCredentialStore {
public:
    bool isAvailable() const override { return false; }
    std::optional<StoredAccountCredential> loadActive() const override { return std::nullopt; }
    bool saveActive(const StoredAccountCredential &) override { return false; }
    bool clearActive() override { return false; }
    QList<QByteArray> pendingRevocations() const override { return {}; }
    bool addPendingRevocation(const QByteArray &) override { return false; }
    bool removePendingRevocation(const QByteArray &) override { return false; }
};

} // namespace

std::unique_ptr<AccountCredentialStore> createAccountCredentialStore(
    Colosseum::Platform::Kind kind,
    AndroidSecureStorageBackend *androidBackend) {
    switch (kind) {
    case Colosseum::Platform::Kind::WindowsDesktop:
#if defined(Q_OS_WIN)
        return std::make_unique<WindowsAccountCredentialStore>();
#else
        return std::make_unique<UnavailableAccountCredentialStore>();
#endif
    case Colosseum::Platform::Kind::Android:
#if defined(Q_OS_ANDROID)
        return std::make_unique<AndroidAccountCredentialStore>(
            androidBackend ? androidBackend : defaultAndroidSecureStorage());
#else
        Q_UNUSED(androidBackend)
        return std::make_unique<UnavailableAccountCredentialStore>();
#endif
    case Colosseum::Platform::Kind::LinuxDesktop:
    case Colosseum::Platform::Kind::Other:
        return std::make_unique<UnavailableAccountCredentialStore>();
    }
    return std::make_unique<UnavailableAccountCredentialStore>();
}
