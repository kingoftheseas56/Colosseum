#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

struct StoredAccountCredential {
    QString accountId;
    QString deviceId;
    QByteArray refreshToken;
};

struct StoredAccountDeletion {
    QString accountId;
    QString requestId;
    QByteArray retryCapability;
};

struct StoredStremioCredential {
    QString profileId;
    QString accountId;
    QByteArray authKey;
};

class AccountCredentialStore {
public:
    virtual ~AccountCredentialStore() = default;

    virtual bool isAvailable() const = 0;

    virtual std::optional<StoredAccountCredential> loadActive() const = 0;
    virtual bool saveActive(const StoredAccountCredential &credential) = 0;
    virtual bool clearActive() = 0;

    virtual QList<QByteArray> pendingRevocations() const = 0;
    virtual bool addPendingRevocation(const QByteArray &refreshToken) = 0;
    virtual bool removePendingRevocation(const QByteArray &refreshToken) = 0;

    virtual QList<StoredAccountDeletion> pendingDeletions() const = 0;
    virtual bool savePendingDeletion(const StoredAccountDeletion &deletion) = 0;
    virtual bool removePendingDeletion(const QString &requestId) = 0;

    // Per-profile Stremio auth keys. Stores without a Stremio vault fail closed.
    virtual std::optional<StoredStremioCredential> loadStremio(
        const QString &profileId,
        const QString &accountId) const {
        Q_UNUSED(profileId)
        Q_UNUSED(accountId)
        return std::nullopt;
    }
    virtual bool saveStremio(const StoredStremioCredential &credential) {
        Q_UNUSED(credential)
        return false;
    }
    virtual bool clearStremio(const QString &profileId) {
        Q_UNUSED(profileId)
        return false;
    }
};
