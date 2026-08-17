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
};
