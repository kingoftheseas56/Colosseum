#pragma once
#include "server1/youtube/YouTubeResolver.h"
namespace server1::http {
// Resolver must outlive requests. The router uses guarded QObject ownership.
bool installYouTubeRoutes(void *router,youtube::YouTubeResolver &resolver);
}
