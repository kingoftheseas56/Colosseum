#pragma once
#include <QtGlobal>
#include <functional>
namespace server1::http {
// Archive URLs use only the trusted bound listener port, never the HTTP Host.
void mountNzbRoutes(void *router,std::function<quint16()> localPort={});
}
