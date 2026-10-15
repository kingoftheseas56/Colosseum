// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountServiceEndpoint.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace {
constexpr auto kProductionAccountServiceUrl =
    "https://colosseum-account-service.onrender.com";
}

QUrl AccountServiceEndpoint::configuredUrl() {
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
}

QByteArray AccountServiceEndpoint::environmentVariableName() {
    return QByteArrayLiteral(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");
}
