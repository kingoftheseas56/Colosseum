#pragma once

#include "AccountCredentialStore.h"
#include "credentials/AccountSecretBackend.h"

namespace windows_account_credential_store_detail {

// These pure helpers define the exact namespace shape used by credential
// enumeration and pending-revocation target derivation. They are kept outside
// the store API so tests can exercise the production matcher without touching
// the user's Credential Manager.
QString pendingTargetName(const QString &prefix, const QByteArray &refreshToken);
bool pendingTargetMatches(const QString &prefix, const QString &target);

}

class PlatformAccountCredentialStore final : public AccountCredentialStore {
public:
    explicit PlatformAccountCredentialStore(
        std::shared_ptr<AccountSecretBackend> backend = createAccountSecretBackend());

    bool isAvailable() const override;

    std::optional<StoredAccountCredential> loadActive() const override;
    bool saveActive(const StoredAccountCredential &credential) override;
    bool clearActive() override;

    QList<QByteArray> pendingRevocations() const override;
    bool addPendingRevocation(const QByteArray &refreshToken) override;
    bool removePendingRevocation(const QByteArray &refreshToken) override;

    QList<StoredAccountDeletion> pendingDeletions() const override;
    bool savePendingDeletion(const StoredAccountDeletion &deletion) override;
    bool removePendingDeletion(const QString &requestId) override;

    static QString activeTargetName();
    static QString pendingTargetPrefix();
    static QString deletionTargetPrefix();
    static QString stremioTargetName(const QString &profileId);

    std::optional<StoredStremioCredential> loadStremio(
        const QString &profileId,
        const QString &accountId) const override;
    bool saveStremio(const StoredStremioCredential &credential) override;
    bool clearStremio(const QString &profileId) override;

private:
    static QByteArray encodeCredential(const StoredAccountCredential &credential);
    static std::optional<StoredAccountCredential> decodeCredential(const QByteArray &blob);
    static QString pendingTargetName(const QByteArray &refreshToken);
    static QString deletionTargetName(const QString &requestId);
    static bool validStremioProfileId(const QString &profileId);
    static bool validStremioAccountId(const QString &accountId);
    static QByteArray encodeStremioCredential(const StoredStremioCredential &credential);
    static std::optional<StoredStremioCredential> decodeStremioCredential(const QByteArray &blob);

    bool writeGenericCredential(const QString &target, const QByteArray &blob);
    std::optional<QByteArray> readGenericCredential(const QString &target) const;
    bool deleteGenericCredential(const QString &target);
    QList<QString> enumerateTargets(const QString &prefix) const;
    std::shared_ptr<AccountSecretBackend> m_backend;
};
