#pragma once
#include "server1/media/HardwareProfiler.h"
#include <functional>
namespace server1::http {
// Borrowed profiler and port provider must outlive the router. This installs
// only M564's /samples, /device-info and /hwaccel-profiler routes.
// GeneratedDefault serves original synthetic Colosseum samples when no directory
// is supplied. A supplied directory is always verified against pinned M934 data.
// ExternalPinnedOnly with no directory returns 404, for explicit no-assets mode.
enum class SampleMode { GeneratedDefault, ExternalPinnedOnly };
bool installAncillaryRoutes(void *router,media::HardwareProfiler &profiler,
                            std::function<unsigned short()> port, QString sampleDirectory={},
                            SampleMode mode=SampleMode::GeneratedDefault);
}
