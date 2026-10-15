#include "watchparty/WatchPartyServiceEndpoint.h"

#include <QString>
#include <QtGlobal>

namespace Colosseum::WatchParty {

QUrl ServiceEndpoint::configuredUrl()
{
    const QString configured = QString::fromUtf8(
        qgetenv(environmentVariableName().constData())).trimmed();
    return configured.isEmpty() ? defaultUrl() : QUrl(configured);
}

QUrl ServiceEndpoint::defaultUrl()
{
    return QUrl(QStringLiteral(
        "wss://colosseum-watchparty-relay.colosseum-watchparty-relay.workers.dev"));
}

QByteArray ServiceEndpoint::environmentVariableName()
{
    return QByteArrayLiteral("COLOSSEUM_WATCH_PARTY_URL");
}

} // namespace Colosseum::WatchParty
