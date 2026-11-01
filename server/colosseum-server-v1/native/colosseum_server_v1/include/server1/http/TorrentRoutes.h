#pragma once
#include "server1/policy/EngineRegistry.h"
namespace server1::http {
// Registry must outlive router and all in-flight responses.
bool installTorrentRoutes(void *router, policy::EngineRegistry &registry);
}
