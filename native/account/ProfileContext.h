#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfilePaths.h"

#include <QObject>
#include <QString>

class ProfileContext final : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        quint64 revision
        READ revision
        NOTIFY changed)

public:
    explicit ProfileContext(QObject *parent = nullptr);

    const ProfilePaths &activeProfile() const;
    quint64 revision() const;

    void activateSealed(const QString &appDataRoot = QString());
    void activateLegacyLocal();
    void activateLocalOnly(const QString &appDataRoot = QString());
    bool activateAccount(const QString &accountId,
                         const QString &appDataRoot = QString());

signals:
    void changed();

private:
    void replace(const ProfilePaths &paths);

    ProfilePaths m_active;
    quint64 m_revision = 0;
};
