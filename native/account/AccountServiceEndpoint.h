#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QUrl>

class AccountServiceEndpoint {
public:
    static QUrl configuredUrl();
    static QByteArray environmentVariableName();
};
