#pragma once
#include "server1/settings/SettingsStore.h"
namespace server1::settings {
// M564 getDefaults plus M804 pinned tracker defaults. Request options are merged
// by EngineRegistry afterwards. No updates are applied to existing engines.
[[nodiscard]] policy::Value runtimeEngineDefaults(const SettingsStore &, const std::string &hash, bool noCache = false);
}
