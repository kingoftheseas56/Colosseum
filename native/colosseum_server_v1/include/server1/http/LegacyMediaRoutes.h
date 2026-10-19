#pragma once
#include <QString>
namespace server1::media { class LegacyHlsService; }
namespace server1::http {
// Install after HLSv2 aliases: source module 564 intentionally gives aliases priority.
bool installLegacyMediaRoutes(void* router,media::LegacyHlsService* service,QString baseUrlLocal);
}
