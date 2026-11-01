#include "AccountSecretBackend.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#include <string>

namespace {
class WindowsAccountSecretBackend final : public AccountSecretBackend {
public:
    bool isAvailable() const override { return true; }

    bool write(const QString &target, const QByteArray &blob) override {
        if (target.isEmpty() || blob.isEmpty() || blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
            return false;
        const std::wstring name = target.toStdWString();
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(name.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = const_cast<LPWSTR>(L"Colosseum");
        return CredWriteW(&credential, 0) == TRUE;
    }

    std::optional<QByteArray> read(const QString &target) const override {
        const std::wstring name = target.toStdWString();
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(name.c_str(), CRED_TYPE_GENERIC, 0, &credential))
            return std::nullopt;
        const QByteArray blob(reinterpret_cast<const char *>(credential->CredentialBlob),
                              static_cast<qsizetype>(credential->CredentialBlobSize));
        CredFree(credential);
        return blob;
    }

    bool remove(const QString &target) override {
        const std::wstring name = target.toStdWString();
        return CredDeleteW(name.c_str(), CRED_TYPE_GENERIC, 0) == TRUE
            || GetLastError() == ERROR_NOT_FOUND;
    }

    QStringList targets(const QString &prefix) const override {
        QStringList result;
        const std::wstring filter = (prefix + QLatin1Char('*')).toStdWString();
        DWORD count = 0;
        PCREDENTIALW *credentials = nullptr;
        if (!CredEnumerateW(filter.c_str(), 0, &count, &credentials))
            return result;
        for (DWORD i = 0; i < count; ++i) {
            if (credentials[i] && credentials[i]->TargetName)
                result.append(QString::fromWCharArray(credentials[i]->TargetName));
        }
        CredFree(credentials);
        return result;
    }
};
}

std::shared_ptr<AccountSecretBackend> createAccountSecretBackend() {
    return std::make_shared<WindowsAccountSecretBackend>();
}
