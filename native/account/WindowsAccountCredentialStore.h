#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountCredentialStore.h"

class WindowsAccountCredentialStore final : public AccountCredentialStore {
public:
    bool isAvailable() const override;

    std::optional<StoredAccountCredential> loadActive() const override;
    bool saveActive(const StoredAccountCredential &credential) override;
    bool clearActive() override;

    QList<QByteArray> pendingRevocations() const override;
    bool addPendingRevocation(const QByteArray &refreshToken) override;
    bool removePendingRevocation(const QByteArray &refreshToken) override;

    static QString activeTargetName();
    static QString pendingTargetPrefix();

private:
    static QByteArray encodeCredential(const StoredAccountCredential &credential);
    static std::optional<StoredAccountCredential> decodeCredential(const QByteArray &blob);
    static QString pendingTargetName(const QByteArray &refreshToken);

    static bool writeGenericCredential(const QString &target, const QByteArray &blob);
    static std::optional<QByteArray> readGenericCredential(const QString &target);
    static bool deleteGenericCredential(const QString &target);
    static QList<QString> enumerateTargets(const QString &prefix);
};
