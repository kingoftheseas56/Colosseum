#pragma once
#include "server1/media/HardwareProfiler.h"
#include <functional>
namespace server1::http {
// Borrowed profiler and port provider must outlive the router. This installs
// only M564's /samples, /device-info and /hwaccel-profiler routes.
// No media payloads are bundled. sampleDirectory is an explicit directory of
// externally supplied pinned M934 files. Missing or mismatched files are 404.
bool installAncillaryRoutes(void *router,media::HardwareProfiler &profiler,
                            std::function<unsigned short()> port, QString sampleDirectory={});
}
