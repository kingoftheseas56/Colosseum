#pragma once
#include "server1/settings/SettingsStore.h"
namespace server1::media { class HlsV2Service; }
namespace server1::settings {
// Call at composition startup and after POST /settings, on the application lane.
// Existing tracks retain their process config; new tracks use these values.
void applyMediaSettings(const SettingsStore &, media::HlsV2Service &, int cpuCores);
}
