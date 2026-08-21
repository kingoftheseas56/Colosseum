#pragma once

#include <QByteArray>
#include <QUrl>

namespace Colosseum::WatchParty {

class ServiceEndpoint final
{
public:
    static QUrl configuredUrl();
    static QUrl defaultUrl();
    static QByteArray environmentVariableName();
};

} // namespace Colosseum::WatchParty
