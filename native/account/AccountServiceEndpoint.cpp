// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountServiceEndpoint.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace {
#ifndef COLOSSEUM_NO_ACCOUNT_EDITION
constexpr auto kProductionAccountServiceUrl =
    "https://colosseum-account-service.onrender.com";
#endif
}

QUrl AccountServiceEndpoint::configuredUrl() {
#ifdef COLOSSEUM_NO_ACCOUNT_EDITION
    // No-account edition: no account service exists. The environment
    // override, the compile-time override and the production fallback are all
    // suppressed so no authentication or sync traffic can ever be constructed
    // (AccountHttpTransport fails every request on an empty base URL).
    Q_UNUSED(environmentVariableName())
    return {};
#else
    QString configured = QString::fromUtf8(
        qgetenv(environmentVariableName().constData()))
        .trimmed();

#ifdef COLOSSEUM_ACCOUNT_SERVICE_URL
    if (configured.isEmpty()) {
        configured = QString::fromUtf8(
            COLOSSEUM_ACCOUNT_SERVICE_URL)
            .trimmed();
    }
#endif

    if (configured.isEmpty())
        configured = QString::fromLatin1(kProductionAccountServiceUrl);

    return QUrl(configured);
#endif
}

QByteArray AccountServiceEndpoint::environmentVariableName() {
    return QByteArrayLiteral(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");
}
