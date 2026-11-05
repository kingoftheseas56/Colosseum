// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountServiceEndpoint.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#ifndef COLOSSEUM_ACCOUNT_SERVICE_URL
#define COLOSSEUM_ACCOUNT_SERVICE_URL ""
#endif
#ifndef COLOSSEUM_COLLECTIONS_API_URL
#define COLOSSEUM_COLLECTIONS_API_URL ""
#endif

namespace {
#ifndef COLOSSEUM_NO_ACCOUNT_EDITION
constexpr auto kProductionAccountServiceUrl =
    "https://colosseum-account-service.onrender.com";
constexpr auto kProductionCollectionsApiUrl =
    "https://api.getcolosseum.world";

QUrl configuredEndpoint(
    const QByteArray &environmentVariable,
    const char *buildTimeUrl,
    const char *productionUrl) {
    QString configured = QString::fromUtf8(qgetenv(environmentVariable.constData())).trimmed();
    if (configured.isEmpty())
        configured = QString::fromUtf8(buildTimeUrl).trimmed();
    if (configured.isEmpty())
        configured = QString::fromLatin1(productionUrl);
    return QUrl(configured);
}
#endif
}

QUrl AccountServiceEndpoint::configuredUrl() {
#ifdef COLOSSEUM_NO_ACCOUNT_EDITION
    // Suppress all service URLs in the local-only edition.
    return {};
#else
    return configuredEndpoint(environmentVariableName(),
                              COLOSSEUM_ACCOUNT_SERVICE_URL,
                              kProductionAccountServiceUrl);
#endif
}

QUrl AccountServiceEndpoint::configuredCollectionsApiUrl() {
#ifdef COLOSSEUM_NO_ACCOUNT_EDITION
    return {};
#else
    return configuredEndpoint(QByteArrayLiteral("COLOSSEUM_COLLECTIONS_API_URL"),
                              COLOSSEUM_COLLECTIONS_API_URL,
                              kProductionCollectionsApiUrl);
#endif
}

QByteArray AccountServiceEndpoint::environmentVariableName() {
    return QByteArrayLiteral(
        "COLOSSEUM_ACCOUNT_SERVICE_URL");
}
