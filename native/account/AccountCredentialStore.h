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
};
