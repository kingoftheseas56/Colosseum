#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QString>

class AccountDeviceIdentity {
public:
    explicit AccountDeviceIdentity(const QString &settingsPath = QString());

    QString installId();
    QString label() const;
    QString platform() const;

private:
    QString m_settingsPath;
};
