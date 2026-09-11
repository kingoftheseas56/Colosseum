#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountCredentialStore.h"

namespace windows_account_credential_store_detail {

// These pure helpers define the exact namespace shape used by credential
// enumeration and pending-revocation target derivation. They are kept outside
// the store API so tests can exercise the production matcher without touching
// the user's Credential Manager.
QString pendingTargetName(const QString &prefix, const QByteArray &refreshToken);
bool pendingTargetMatches(const QString &prefix, const QString &target);

}

class WindowsAccountCredentialStore final : public AccountCredentialStore {
public:
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

private:
    static QByteArray encodeCredential(const StoredAccountCredential &credential);
    static std::optional<StoredAccountCredential> decodeCredential(const QByteArray &blob);
    static QString pendingTargetName(const QByteArray &refreshToken);
    static QString deletionTargetName(const QString &requestId);

    static bool writeGenericCredential(const QString &target, const QByteArray &blob);
    static std::optional<QByteArray> readGenericCredential(const QString &target);
    static bool deleteGenericCredential(const QString &target);
    static QList<QString> enumerateTargets(const QString &prefix);
};
